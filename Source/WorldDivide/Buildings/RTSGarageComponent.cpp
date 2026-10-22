#include "Buildings/RTSGarageComponent.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Engine/CurveTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSNavAgents.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSLocomotion.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#include "Units/RTSUnitShape.h"

namespace
{
	/** A vehicle drives out of the garage at this share of its top speed. */
	constexpr float ExitSpeedShare = 0.5f;

	/** The door stays open this long once there is nothing more to send out, so a vehicle queued right after keeps it open. */
	constexpr float IdleOpenHold = 1.f;

	/** The door starts opening this much earlier than it takes to open, so it is open the frame the vehicle completes. */
	constexpr float DueMargin = 0.5f;

	/** How far beyond the end of the lane the owner's idle units on it are sent. */
	constexpr float LaneClearMargin = 300.f;

	/** The warning lights hang either side of the 12.6 m door opening, this far from the lane's middle, above the door. */
	constexpr float LightSpread = 680.f;
	const FVector LightAbovePivot(40.f, 0.f, 60.f);
	const FLinearColor LightColor(1.f, 0.45f, 0.05f);
	constexpr float LightCandelas = 300.f;
	constexpr float WarningLightRadius = 1200.f;

	/** The two lights take turns, each lit for half of this many seconds. */
	constexpr float FlashPeriod = 0.8f;

	/** A vehicle left in the bay of a destroyed HQ looks this often for navmesh under it, for at most AbandonWaitSeconds. */
	constexpr float AbandonPollSeconds = 0.25f;
	constexpr float AbandonWaitSeconds = 10.f;
	const FVector AbandonNavExtent(50.f, 50.f, 250.f);

	/** Points along a hull's footprint segment checked against a box: close enough for a hull no longer than its width thrice. */
	constexpr int32 FootprintSamples = 5;
}

URTSGarageComponent::URTSGarageComponent()
{
	// The building's tick drives the garage on the server; the component ticks only to pose the door and flash the lights.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

ARTSBuilding* URTSGarageComponent::GetBuilding() const
{
	return Cast<ARTSBuilding>(GetOwner());
}

double URTSGarageComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool URTSGarageComponent::Accepts(const URTSUnitData& Data) const
{
	return bHasGarage && Data.Locomotion.IsVehicle() && !Data.IsA<URTSAircraftData>();
}

void URTSGarageComponent::Enqueue(URTSUnitData& Data)
{
	check(GetOwner()->HasAuthority());
	Pending.Add(&Data);
}

void URTSGarageComponent::ApplyBuildingData()
{
	ARTSBuilding* Building = GetBuilding();
	const URTSBuildingData* Data = Building ? Building->GetBuildingData() : nullptr;
	for (UPointLightComponent* Light : Lights)
	{
		if (Light)
		{
			Light->DestroyComponent();
		}
	}
	Lights.Reset();
	DoorParts.Reset();
	PosedOpenness = -1.f;
	bHasGarage = Data && Data->HasGarage();
	if (!bHasGarage)
	{
		return;
	}

	// The bay, and the lane on from its door to the end of the exit lane, where a vehicle leaving it hands over to its path.
	const FVector Scale = Data->MeshScale;
	SpawnLocal = *Data->FindSocket(URTSBuildingData::GarageSpawnSocket) * Scale;
	const FRTSBuildingCollisionBox* Bay = Data->CollisionBoxes.FindByPredicate([](const FRTSBuildingCollisionBox& Box) { return Box.bGarage; });
	const FVector BayMin = Bay->Min * Scale;
	const FVector BayMax = Bay->Max * Scale;
	float LaneEnd = RTSBuildPlacement::GetFootprintExtent(Data->Footprint).X;
	if (const URTSUnitData* Widest = RTSBuildPlacement::GetWidestExitUnit(*Data))
	{
		LaneEnd = RTSBuildPlacement::GetExitDistance(*Data, *Widest) + RTSUnitShape::FromData(*Widest).GetHalfLength();
	}
	LaneBox = FBox2D(FVector2D(BayMin.X, BayMin.Y), FVector2D(FMath::Max(LaneEnd, BayMax.X), BayMax.Y));

	// The door's moving parts and the curves of the delivered cycle that move them.
	if (const UCurveTable* Motion = Data->GarageDoorMotion)
	{
		const FString Context = Data->GetName();
		auto Curve = [Motion, &Context](const FString& Row) { return Motion->FindCurve(FName(*Row), Context); };
		auto Add = [&](const FString& Name, const FString& Along, const TCHAR* Pitch, const TCHAR* Length = nullptr)
		{
			UStaticMeshComponent* Component = nullptr;
			int32 Instance = INDEX_NONE;
			const FRTSBuildingPart* Part = Data->Parts.FindByPredicate([&Name](const FRTSBuildingPart& P) { return P.Name == FName(*Name); });
			if (!Part || !Building->FindPart(FName(*Name), Component, Instance))
			{
				return;
			}
			FDoorPart& Door = DoorParts.AddDefaulted_GetRef();
			Door.Component = Component;
			Door.Instance = Instance;
			Door.Rest = Part->Transform;
			Door.X = Curve(Along + TEXT("_X"));
			Door.Z = Curve(Along + TEXT("_Z"));
			Door.Pitch = Curve(Pitch);
			Door.Length = Length ? Curve(Length) : nullptr;
		};
		for (int32 Section = 0; Section < 16; ++Section)
		{
			const FString Row = FString::Printf(TEXT("S%02d"), Section);
			Add(FString::Printf(TEXT("DoorSection%02d"), Section), Row, *(Row + TEXT("_P")));
		}
		// The seal rides the bottom section; the cables hang from it and shorten as it rises.
		Add(TEXT("DoorBottomSeal"), TEXT("S00"), TEXT("S00_P"));
		Add(TEXT("LiftCableEast"), TEXT("S00"), TEXT("Cable_P"), TEXT("Cable_L"));
		Add(TEXT("LiftCableWest"), TEXT("S00"), TEXT("Cable_P"), TEXT("Cable_L"));
		Add(TEXT("DriveShaft"), TEXT("Drive"), TEXT("Drive_P"));
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		const FVector* Pivot = Data->FindSocket(URTSBuildingData::GarageDoorPivotSocket);
		const FVector Above = (Pivot ? *Pivot * Scale : SpawnLocal) + LightAbovePivot;
		for (const float Side : { -1.f, 1.f })
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(Building, NAME_None, RF_Transient);
			Light->SetupAttachment(Building->GetRootComponent());
			Light->SetRelativeLocation(FVector(Above.X, SpawnLocal.Y + Side * LightSpread, Above.Z));
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(LightCandelas);
			Light->SetLightColor(LightColor);
			Light->SetAttenuationRadius(WarningLightRadius);
			Light->SetCastShadows(false);
			Light->SetVisibility(false);
			Building->AddInstanceComponent(Light);
			Light->RegisterComponent();
			Lights.Add(Light);
		}
	}
	UpdateVisuals();
}

float URTSGarageComponent::GetOpenness() const
{
	const ARTSBuilding* Building = GetBuilding();
	const URTSBuildingData* Data = Building ? Building->GetBuildingData() : nullptr;
	const float Seconds = Data ? FMath::Max(Data->GarageDoorSeconds, 0.1f) : 1.f;
	const float Moved = static_cast<float>(GetServerTime() - Door.StartTime) / Seconds;
	switch (Door.Phase)
	{
	case ERTSGarageDoor::Opening:
		return FMath::Clamp(Door.StartOpenness + Moved, 0.f, 1.f);
	case ERTSGarageDoor::Open:
		return 1.f;
	case ERTSGarageDoor::Closing:
		return FMath::Clamp(Door.StartOpenness - Moved, 0.f, 1.f);
	default:
		return 0.f;
	}
}

void URTSGarageComponent::SetPhase(ERTSGarageDoor Phase)
{
	check(GetOwner()->HasAuthority());
	Door.StartOpenness = GetOpenness();
	Door.StartTime = GetServerTime();
	Door.Phase = Phase;
	IdleOpenSeconds = 0.f;
	OnRep_Door();
}

bool URTSGarageComponent::IsVehicleDueSoon() const
{
	const ARTSBuilding* Building = GetBuilding();
	const URTSProductionComponent* Production = Building->GetProductionComponent();
	const URTSUnitData* Front = Production->GetQueue().IsEmpty() ? nullptr : Cast<URTSUnitData>(Production->GetQueue()[0]);
	// Low on power the door waits for the vehicle, rather than standing open through the longer build.
	return Front && Accepts(*Front) && Production->CanProduce()
		&& !ARTSPlayerState::IsLowPowerForTeam(*GetWorld(), Building->GetTeamId())
		&& Production->GetSecondsToComplete() <= Building->GetBuildingData()->GarageDoorSeconds + DueMargin;
}

void URTSGarageComponent::UpdateGarage(float DeltaSeconds)
{
	check(GetOwner()->HasAuthority());
	if (!bHasGarage)
	{
		return;
	}
	const float Openness = GetOpenness();
	if (Door.Phase == ERTSGarageDoor::Opening && Openness >= 1.f)
	{
		SetPhase(ERTSGarageDoor::Open);
	}
	else if (Door.Phase == ERTSGarageDoor::Closing && Openness <= 0.f)
	{
		SetPhase(ERTSGarageDoor::Closed);
	}

	UpdateExiting();

	// The door never comes down on a hull in the bay or under it; the apron out to the footprint's edge counts as under it.
	const float Edge = RTSBuildPlacement::GetFootprintExtent(GetBuilding()->GetBuildingData()->Footprint).X;
	const FBox2D DoorBox(LaneBox.Min, FVector2D(FMath::Min(LaneBox.Max.X, Edge), LaneBox.Max.Y));
	bool bUnderDoor = false;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It && !bUnderDoor; ++It)
	{
		bUnderDoor = !It->GetHealthComponent()->IsDead() && !It->GetAircraftComponent()->CanFly() && Overlaps(**It, DoorBox);
	}
	const bool bWanted = !Pending.IsEmpty() || Exiting.IsValid() || IsVehicleDueSoon();
	switch (Door.Phase)
	{
	case ERTSGarageDoor::Closed:
		if (bWanted)
		{
			SetPhase(ERTSGarageDoor::Opening);
		}
		break;
	case ERTSGarageDoor::Opening:
		// Cancelled, or the power dropped, before the vehicle completed: down again.
		if (!bWanted && !bUnderDoor)
		{
			SetPhase(ERTSGarageDoor::Closing);
		}
		break;
	case ERTSGarageDoor::Open:
		IdleOpenSeconds = bWanted || bUnderDoor ? 0.f : IdleOpenSeconds + DeltaSeconds;
		if (IdleOpenSeconds >= IdleOpenHold)
		{
			SetPhase(ERTSGarageDoor::Closing);
		}
		break;
	case ERTSGarageDoor::Closing:
		if (bWanted || bUnderDoor)
		{
			SetPhase(ERTSGarageDoor::Opening);
		}
		break;
	}

	// One at a time, once the door is open and nothing stands in the bay or on its lane.
	if (Door.Phase == ERTSGarageDoor::Open && !Pending.IsEmpty() && !Exiting.IsValid())
	{
		if (IsLaneOccupied())
		{
			ClearLane(nullptr, LaneBox);
		}
		else
		{
			URTSUnitData* Data = Pending[0];
			Pending.RemoveAt(0);
			if (Data)
			{
				SpawnExiting(*Data);
			}
		}
	}
}

FTransform URTSGarageComponent::GetSpawnTransform() const
{
	const AActor* Owner = GetOwner();
	return FTransform(FRotator(0.f, Owner->GetActorRotation().Yaw, 0.f), Owner->GetActorTransform().TransformPosition(SpawnLocal));
}

ARTSUnit* URTSGarageComponent::SpawnExiting(URTSUnitData& Data)
{
	ARTSBuilding* Building = GetBuilding();
	FTransform Transform = GetSpawnTransform();
	ARTSUnit* Unit = GetWorld()->SpawnActorDeferred<ARTSUnit>(
		ARTSUnit::StaticClass(), Transform, Building, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Unit)
	{
		return nullptr;
	}
	Unit->InitFromData(&Data, Building->GetTeamId());
	Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
	Unit->FinishSpawning(Transform);
	Exiting = Unit;
	UpdateExiting();
	return Unit;
}

void URTSGarageComponent::UpdateExiting()
{
	ARTSUnit* Unit = Exiting.Get();
	if (!Unit || Unit->IsActorBeingDestroyed() || Unit->GetHealthComponent()->IsDead())
	{
		Exiting.Reset();
		return;
	}
	ARTSBuilding* Building = GetBuilding();
	URTSUnitMovement* Movement = Unit->GetUnitMovement();
	const FVector Local = Building->GetActorTransform().InverseTransformPosition(Unit->GetActorLocation());
	// Out where a building without a garage puts the unit (RTSBuildPlacement::GetExitDistance), on the navmesh it drives.
	if (Local.X >= RTSBuildPlacement::GetExitDistance(*Building->GetBuildingData(), *Unit->GetUnitData()))
	{
		Movement->SetCommandOverride({});
		Exiting.Reset();
		// A unit given an order of its own on the way out keeps it.
		if (Unit->GetQueuedCommandCount() == 0)
		{
			Building->GetProductionComponent()->SendOut(*Unit);
		}
		return;
	}

	// It waits while anything stands on the lane ahead of it, or just past the lane's end within the hull's stopping distance,
	// and the owner's idle units there make way.
	const float Reach = Local.X + Unit->GetShape().GetHalfLength() + Movement->GetStoppingDistance() + LaneClearMargin;
	const FBox2D Ahead(FVector2D(Local.X, LaneBox.Min.Y), FVector2D(FMath::Max(LaneBox.Max.X, Reach), LaneBox.Max.Y));
	bool bBlocked = false;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It && !bBlocked; ++It)
	{
		bBlocked = *It != Unit && !It->GetHealthComponent()->IsDead() && !It->GetAircraftComponent()->CanFly() && Overlaps(**It, Ahead);
	}
	if (bBlocked)
	{
		ClearLane(Unit, Ahead);
	}
	RTSLocomotion::FCommand Drive;
	Drive.Speed = bBlocked ? 0.f : Unit->GetUnitData()->MoveSpeed * ExitSpeedShare;
	Movement->SetCommandOverride(Drive, /*bThroughNavObstacles*/ true);
}

void URTSGarageComponent::ClearLane(const ARTSUnit* Ignore, const FBox2D& Box)
{
	ARTSBuilding* Building = GetBuilding();
	const FTransform& Frame = Building->GetActorTransform();
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		if (Unit == Ignore || Unit == Exiting.Get() || Unit->GetTeamId() != Building->GetTeamId() || Unit->GetQueuedCommandCount() > 0
			|| Unit->GetHealthComponent()->IsDead() || Unit->GetAircraftComponent()->CanFly() || !Overlaps(*Unit, Box))
		{
			continue;
		}
		// Straight on out past Box's end, beside where it stood.
		const FRTSFootprint Footprint = Unit->GetFootprint();
		FVector Local = Frame.InverseTransformPosition(Unit->GetActorLocation());
		Local.X = Box.Max.X + Footprint.HalfSegment + Footprint.Radius + LaneClearMargin;
		Unit->StepAside(Frame.TransformPosition(Local));
	}
}

bool URTSGarageComponent::Overlaps(const ARTSUnit& Unit, const FBox2D& Box) const
{
	if (!Box.bIsValid)
	{
		return false;
	}
	const FTransform& Frame = GetOwner()->GetActorTransform();
	const FRTSFootprint Footprint = Unit.GetFootprint();
	const FVector Start = Frame.InverseTransformPosition(Footprint.Center - Footprint.Axis * Footprint.HalfSegment);
	const FVector End = Frame.InverseTransformPosition(Footprint.Center + Footprint.Axis * Footprint.HalfSegment);
	for (int32 Sample = 0; Sample < FootprintSamples; ++Sample)
	{
		const FVector Point = FMath::Lerp(Start, End, static_cast<float>(Sample) / (FootprintSamples - 1));
		if (Box.ComputeSquaredDistanceToPoint(FVector2D(Point)) < FMath::Square(Footprint.Radius))
		{
			return true;
		}
	}
	return false;
}

bool URTSGarageComponent::IsLaneOccupied(const ARTSUnit* Ignore) const
{
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (*It != Ignore && !It->GetHealthComponent()->IsDead() && !It->GetAircraftComponent()->CanFly() && Overlaps(**It, LaneBox))
		{
			return true;
		}
	}
	return false;
}

void URTSGarageComponent::Abandon()
{
	check(GetOwner()->HasAuthority());
	Pending.Reset();
	ARTSUnit* Unit = Exiting.Get();
	Exiting.Reset();
	if (!Unit || Unit->IsActorBeingDestroyed())
	{
		return;
	}
	Unit->GetUnitMovement()->SetCommandOverride({});
	if (Unit->GetQueuedCommandCount() > 0)
	{
		return;
	}
	// The vehicle stands where the bay was, off the navmesh until the hole the HQ cut is filled again: it sets off for the
	// rally point once it has navmesh under it, or gives up waiting after AbandonWaitSeconds.
	UWorld* World = GetWorld();
	const FVector Rally = GetBuilding()->GetProductionComponent()->GetRallyPoint();
	TWeakObjectPtr<ARTSUnit> Weak = Unit;
	TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
	TSharedRef<float> Waited = MakeShared<float>(0.f);
	World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(World, [World, Weak, Rally, Handle, Waited]()
	{
		ARTSUnit* Left = Weak.Get();
		*Waited += AbandonPollSeconds;
		const ARecastNavMesh* NavMesh = Left ? RTSNavAgents::GetNavMesh(*World, Left->GetUnitMovement()->GetNavAgentPropertiesRef()) : nullptr;
		FNavLocation Under;
		const bool bOnNavMesh = NavMesh && NavMesh->ProjectPoint(Left->GetNavAgentLocation(), Under, AbandonNavExtent);
		if (Left && !Left->GetHealthComponent()->IsDead() && Left->GetQueuedCommandCount() == 0 && (bOnNavMesh || *Waited >= AbandonWaitSeconds))
		{
			FRTSCommand Move;
			Move.Type = ERTSCommandType::Move;
			Move.TargetLocation = Rally;
			Left->ExecuteCommand(Move);
		}
		if (!Left || bOnNavMesh || *Waited >= AbandonWaitSeconds || Left->GetQueuedCommandCount() > 0)
		{
			World->GetTimerManager().ClearTimer(*Handle);
		}
	}), AbandonPollSeconds, /*bLoop*/ true);
}

void URTSGarageComponent::OnRep_Door()
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		SetComponentTickEnabled(true);
	}
}

void URTSGarageComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateVisuals();
	if (Door.Phase == ERTSGarageDoor::Closed && PosedOpenness == 0.f)
	{
		SetComponentTickEnabled(false);
	}
}

void URTSGarageComponent::UpdateVisuals()
{
	if (!bHasGarage || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const float Openness = GetOpenness();
	if (Openness != PosedOpenness)
	{
		PoseDoor(Openness);
	}
	const bool bFirst = FMath::Fmod(GetWorld()->GetTimeSeconds(), FlashPeriod) < 0.5f * FlashPeriod;
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		if (Lights[Index])
		{
			Lights[Index]->SetVisibility(AreLightsOn() && (Index == 0) == bFirst);
		}
	}
}

void URTSGarageComponent::PoseDoor(float Openness)
{
	PosedOpenness = Openness;
	const URTSBuildingData* Data = GetBuilding()->GetBuildingData();
	if (!Data || DoorParts.IsEmpty())
	{
		return;
	}
	// Closing plays the delivered opening backward.
	const float Time = FMath::Lerp(Data->GarageDoorOpenStart, Data->GarageDoorOpenEnd, Openness);
	TArray<UInstancedStaticMeshComponent*, TInlineAllocator<2>> Instanced;
	for (const FDoorPart& Part : DoorParts)
	{
		UStaticMeshComponent* Component = Part.Component.Get();
		if (!Component)
		{
			continue;
		}
		FTransform Transform = Part.Rest;
		FVector Location = Transform.GetLocation();
		Location.X = Part.X ? Part.X->Eval(Time, Location.X) : Location.X;
		Location.Z = Part.Z ? Part.Z->Eval(Time, Location.Z) : Location.Z;
		Transform.SetLocation(Location);
		if (Part.Pitch)
		{
			FRotator Rotation = Transform.Rotator();
			Rotation.Pitch = Part.Pitch->Eval(Time, Rotation.Pitch);
			Transform.SetRotation(Rotation.Quaternion());
		}
		if (Part.Length)
		{
			FVector Scale = Transform.GetScale3D();
			Scale.Z = Part.Length->Eval(Time, Scale.Z);
			Transform.SetScale3D(Scale);
		}
		if (UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component); Instances && Part.Instance != INDEX_NONE)
		{
			Instances->UpdateInstanceTransform(Part.Instance, Transform, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
			Instanced.AddUnique(Instances);
		}
		else
		{
			Component->SetRelativeTransform(Transform);
		}
	}
	for (UInstancedStaticMeshComponent* Instances : Instanced)
	{
		Instances->MarkRenderStateDirty();
	}
}

void URTSGarageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URTSGarageComponent, Door);
}
