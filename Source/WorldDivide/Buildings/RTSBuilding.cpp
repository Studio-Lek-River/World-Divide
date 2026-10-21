#include "Buildings/RTSBuilding.h"

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSProductionComponent.h"
#include "Buildings/RTSSuperweaponComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"
#include "WorldDivide.h"

namespace
{
	/** A fresh construction site still shows a sliver of the building. */
	constexpr float MinConstructionScale = 0.05f;

	/** Collision height when the data has no mesh. */
	constexpr float DefaultHeight = 200.f;

	/**
	 * A producer's rally point starts at the end of its exit lane (RTSBuildPlacement::GetExitLaneLength), a hull length past
	 * where its widest unit spawns, so finished units clear the spawn spot; this far outside the exit edge for a building
	 * that sends out no ground unit.
	 */
	constexpr float DefaultRallyDistance = 400.f;

	/** Units held short of a new building go on regardless once its navmesh hole takes this long. */
	constexpr float NavCutTimeout = 10.f;

	/** How close to the footprint centre a navmesh point still counts as the hole not being there yet. */
	const FVector NavCutQueryExtent(10.f, 10.f, 250.f);
}

ARTSBuilding::ARTSBuilding()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Root);
	Collision->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	// A null-area obstacle carves the footprint out of the navmesh without growing a navmesh on the roof.
	Collision->SetCanEverAffectNavigation(true);
	Collision->bDynamicObstacle = true;
	Collision->SetAreaClassOverride(UNavArea_Null::StaticClass());

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	HealthComponent = CreateDefaultSubobject<URTSHealthComponent>(TEXT("Health"));
	WeaponComponent = CreateDefaultSubobject<URTSWeaponComponent>(TEXT("Weapon"));
	ProductionComponent = CreateDefaultSubobject<URTSProductionComponent>(TEXT("Production"));
	SuperweaponComponent = CreateDefaultSubobject<URTSSuperweaponComponent>(TEXT("Superweapon"));

	bReplicates = true;
	SetNetUpdateFrequency(10.f);
	// Every player sees every building for now; fog of war will decide relevancy later.
	bAlwaysRelevant = true;
}

void ARTSBuilding::InitFromData(URTSBuildingData* InBuildingData, int32 InTeamId)
{
	check(HasAuthority());
	BuildingData = InBuildingData;
	TeamId = InTeamId;
	HealthComponent->InitHealth(BuildingData ? BuildingData->MaxHealth : 1.f);
	// The weapon stays empty until construction completes.
	WeaponComponent->SetTeamId(TeamId);
	WeaponComponent->SetWeaponData(nullptr);
	ApplyUpgrades();
	ApplyBuildingData();
	Pads.SetNum(GetPadCount());

	// From now on the building keeps its team in the match, and its team's enemies find it.
	ARTSGameState* GameState = GetWorld()->GetGameState<ARTSGameState>();
	if (GameState)
	{
		GameState->RegisterBuilding(*this);
	}
	if (GameState && ARTSGameState::CountsForSurvival(*this))
	{
		GameState->RefreshTeams();
	}
}

void ARTSBuilding::ApplyUpgrades()
{
	check(HasAuthority());
	if (!BuildingData)
	{
		return;
	}
	const UWorld& World = *GetWorld();
	HealthComponent->SetMaxHealth(BuildingData->MaxHealth * ARTSPlayerState::GetStatMultiplierForTeam(World, TeamId, *BuildingData, ERTSUpgradeStat::MaxHealth));
	WeaponComponent->SetDamageMultiplier(ARTSPlayerState::GetStatMultiplierForTeam(World, TeamId, *BuildingData, ERTSUpgradeStat::WeaponDamage));
}

void ARTSBuilding::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	HealthComponent->OnDamaged.AddDynamic(this, &ThisClass::HandleDamaged);
	HealthComponent->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);

	// A building placed in the level never passes through InitFromData.
	if (GetNetMode() != NM_Client)
	{
		if (ARTSGameState* GameState = GetWorld()->GetGameState<ARTSGameState>())
		{
			GameState->RegisterBuilding(*this);
		}
	}
}

void ARTSBuilding::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// Before any unit moves another frame towards the footprint.
		bNavCutPending = true;
		UpdateNavCut(0.f);
	}
}

void ARTSBuilding::HandleDamaged(float Amount, AActor* DamageInstigator)
{
	ARTSPlayerController::NotifyTeamAttacked(*this, TeamId, DamageInstigator, ERTSAttackAlert::Base);
	ARTSAIPlayer::NotifyBuildingDamaged(*this, DamageInstigator);
}

void ARTSBuilding::HandleDeath(AActor* Killer)
{
	// Only cancelling refunds; a construction site the enemy destroys is lost with everything paid for it.
	Destroy();
}

void ARTSBuilding::SetPaidCost(int32 InPaidCost)
{
	check(HasAuthority());
	PaidCost = FMath::Max(InPaidCost, 0);
}

int32 ARTSBuilding::GetCancelRefund() const
{
	return IsConstructed() ? 0 : FMath::FloorToInt(PaidCost * (1.f - ConstructionProgress));
}

void ARTSBuilding::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bNavCutPending)
	{
		UpdateNavCut(DeltaSeconds);
	}
	// Construction, production and a superweapon's charge hold once the match is over.
	if (!HasAuthority() || ARTSGameState::IsMatchOver(*GetWorld()))
	{
		return;
	}
	if (bAutoConstruct && !IsConstructed())
	{
		UpdateConstruction(DeltaSeconds);
	}
	if (IsConstructed())
	{
		ProductionComponent->UpdateProduction(DeltaSeconds);
		SuperweaponComponent->UpdateCharge(DeltaSeconds);
		UpdateForcedFire();
	}
}

void ARTSBuilding::UpdateConstruction(float DeltaSeconds)
{
	check(HasAuthority());
	if (!BuildingData || IsConstructed())
	{
		return;
	}

	const float Rate = ARTSPlayerState::GetProductionRateForTeam(*GetWorld(), TeamId);
	ConstructionProgress = BuildingData->BuildTime > 0.f
		? FMath::Min(ConstructionProgress + DeltaSeconds * Rate / BuildingData->BuildTime, 1.f)
		: 1.f;
	if (IsConstructed())
	{
		// The owner's new totals decide whether this building's own weapon is powered; recalculating refreshes it.
		if (ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(*GetWorld(), TeamId))
		{
			Player->RecalculatePower();
		}
		else
		{
			RefreshWeapon();
		}
		SpawnCompletionUnit();
		const float ExitLane = RTSBuildPlacement::GetExitLaneLength(*BuildingData);
		ProductionComponent->SetRallyPoint(GetExitLocation(ExitLane > 0.f ? ExitLane : DefaultRallyDistance));
		if (SuperweaponComponent->IsSuperweapon())
		{
			SuperweaponComponent->StartCharging();
			ARTSPlayerController::NotifySuperweapon(*this, ERTSSuperweaponAlert::Built);
		}
	}
	ApplyConstructionVisual();
}

void ARTSBuilding::RefreshWeapon()
{
	check(HasAuthority());
	const bool bPowered = BuildingData && (!BuildingData->bRequiresPower || !ARTSPlayerState::IsLowPowerForTeam(*GetWorld(), TeamId));
	WeaponComponent->SetWeaponData(IsConstructed() && bPowered ? BuildingData->Weapon.Get() : nullptr);
}

bool ARTSBuilding::ForceFire(AActor* Target, const FVector& Location)
{
	check(HasAuthority());
	// Clients pick the target, so it is checked here.
	if (!IsConstructed() || !WeaponComponent->CanForceFireAt(Target, Location)
		|| !(Target ? WeaponComponent->IsInRange(Target) : WeaponComponent->IsInRange(Location)))
	{
		return false;
	}
	if (Target)
	{
		WeaponComponent->SetOrderedTarget(Target, /*bForce*/ true);
	}
	else
	{
		WeaponComponent->SetOrderedLocation(Location);
	}
	ForcedTarget = Target;
	return true;
}

void ARTSBuilding::StopFiring()
{
	check(HasAuthority());
	WeaponComponent->ClearOrder();
	ForcedTarget.Reset();
}

void ARTSBuilding::UpdateForcedFire()
{
	if (ForcedTarget.IsExplicitlyNull())
	{
		return;
	}
	// The weapon drops a target that died or can no longer be hit itself. Range is 0 while unpowered, so a short power dip
	// keeps the order.
	AActor* Target = ForcedTarget.Get();
	if (!Target || WeaponComponent->GetTarget() != Target || (WeaponComponent->IsArmed() && !WeaponComponent->IsInRange(Target)))
	{
		StopFiring();
	}
}

void ARTSBuilding::UpdateNavCut(float DeltaSeconds)
{
	check(HasAuthority());
	NavCutWaited += DeltaSeconds;

	// The rebuild runs asynchronously; the hole is there once the centre is off every agent's navmesh and no build is left.
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	bool bCut = !NavSystem || !NavSystem->IsNavigationBuildInProgress();
	for (const FNavDataConfig& Agent : RTSNavAgents::GetAgents())
	{
		const ARecastNavMesh* NavMesh = NavSystem ? RTSNavAgents::GetNavMesh(*GetWorld(), Agent) : nullptr;
		FNavLocation NavPoint;
		bCut = bCut && !(NavMesh && NavMesh->ProjectPoint(GetActorTransform().TransformPosition(NavProbeOffset), NavPoint, NavCutQueryExtent));
	}
	if (bCut || NavCutWaited >= NavCutTimeout || !BuildingData)
	{
		if (!bCut)
		{
			UE_LOG(LogWorldDivide, Warning, TEXT("%s: navmesh hole not built after %.0f s, releasing held units"), *GetName(), NavCutWaited);
		}
		bNavCutPending = false;
		ReleaseHeldUnits();
		return;
	}

	const FVector Center = GetActorLocation();
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(GetFootprint());
	const FBox Footprint(Center - FVector(Extent.X, Extent.Y, 0.f), Center + FVector(Extent.X, Extent.Y, 0.f));
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsMoveHeld() && It->HoldShortOf(Footprint))
		{
			HeldUnits.AddUnique(*It);
		}
	}
}

void ARTSBuilding::ReleaseHeldUnits()
{
	// Held by this building or not, a unit only goes on while it still waits.
	for (const TWeakObjectPtr<ARTSUnit>& Unit : HeldUnits)
	{
		if (Unit.IsValid())
		{
			Unit->ReleaseHold();
		}
	}
	HeldUnits.Reset();
}

void ARTSBuilding::Destroyed()
{
	// A site cancelled or destroyed before its hole was built lets its waiting units go.
	if (HasAuthority() && bNavCutPending)
	{
		bNavCutPending = false;
		ReleaseHeldUnits();
	}

	// Aircraft bound here look for another pad.
	if (HasAuthority())
	{
		TArray<TWeakObjectPtr<ARTSUnit>> Bound = MoveTemp(Pads);
		Pads.Reset();
		for (const TWeakObjectPtr<ARTSUnit>& Aircraft : Bound)
		{
			if (Aircraft.IsValid())
			{
				Aircraft->GetAircraftComponent()->NotifyPadLost();
			}
		}
	}

	// Only completed buildings count towards the grid; cancelled or destroyed sites never did.
	if (HasAuthority() && IsConstructed() && GetWorld())
	{
		if (ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(*GetWorld(), TeamId))
		{
			Player->RecalculatePower(this);
		}
	}

	UWorld* World = GetWorld();
	if (ARTSGameState* GameState = HasAuthority() && World ? World->GetGameState<ARTSGameState>() : nullptr)
	{
		GameState->UnregisterBuilding(*this);
	}

	// Destroyed or cancelled, the team's last building that is not a defence takes the team out of the match.
	if (HasAuthority() && World && !World->bIsTearingDown && ARTSGameState::CountsForSurvival(*this))
	{
		if (ARTSGameState* GameState = World->GetGameState<ARTSGameState>())
		{
			GameState->RefreshTeams(this);
		}
	}
	Super::Destroyed();
}

void ARTSBuilding::SpawnCompletionUnit()
{
	ARTSUnit* Unit = SpawnUnitAtExit(BuildingData->SpawnOnComplete);
	if (Unit && Unit->GetHarvesterComponent()->CanHarvest())
	{
		FRTSCommand Harvest;
		Harvest.Type = ERTSCommandType::Harvest;
		Unit->ExecuteCommand(Harvest);
	}
}

FVector ARTSBuilding::GetExitLocation(float Clearance, const FNavAgentProperties* Agent) const
{
	const FVector Extent = BuildingData ? RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint) : FVector::ZeroVector;
	FVector Location = GetActorLocation() + GetActorForwardVector() * (Extent.X + Clearance);
	if (const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*GetWorld(), Agent ? *Agent : RTSNavAgents::GetNarrowestAgent()))
	{
		FNavLocation NavPoint;
		if (NavMesh->ProjectPoint(Location, NavPoint, FVector(200.f, 200.f, 250.f)))
		{
			Location.X = NavPoint.Location.X;
			Location.Y = NavPoint.Location.Y;
		}
	}
	return Location;
}

ARTSUnit* ARTSBuilding::SpawnUnitAtExit(URTSUnitData* Data)
{
	check(HasAuthority());
	if (!Data)
	{
		return nullptr;
	}

	// Clear of the building by the unit's longer half, whichever way it faces, on the navmesh it walks: placement made sure
	// it fits there (RTSBuildPlacement::CanUnitsLeave).
	const FRTSUnitShape Shape = RTSUnitShape::FromData(*Data);
	FTransform Transform(GetExitLocation(FMath::Max(Shape.GetHalfLength(), Shape.GetHalfWidth()) + RTSBuildPlacement::ExitClearance,
		&RTSNavAgents::SelectAgent(Shape)));
	ARTSUnit* Unit = GetWorld()->SpawnActorDeferred<ARTSUnit>(
		ARTSUnit::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Unit)
	{
		return nullptr;
	}
	Unit->InitFromData(Data, TeamId);
	Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
	Unit->FinishSpawning(Transform);
	return Unit;
}

int32 ARTSBuilding::GetPadCount() const
{
	return BuildingData ? BuildingData->AirfieldPads : 0;
}

FVector ARTSBuilding::GetPadLocation(int32 Index) const
{
	const FVector Center = GetActorLocation();
	const int32 Count = GetPadCount();
	if (Count <= 0)
	{
		return Center;
	}
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint);
	const float Spacing = 2.f * Extent.Y / Count;
	// The collision box spans the building's full height, so its top is the roof.
	const float Roof = 2.f * Collision->GetUnscaledBoxExtent().Z;
	return Center + GetActorRotation().RotateVector(FVector(0.f, -Extent.Y + (Index + 0.5f) * Spacing, Roof));
}

ARTSUnit* ARTSBuilding::GetPadAircraft(int32 Index) const
{
	return Pads.IsValidIndex(Index) ? Pads[Index].Get() : nullptr;
}

int32 ARTSBuilding::GetFreePadCount() const
{
	int32 Free = 0;
	for (const TWeakObjectPtr<ARTSUnit>& Aircraft : Pads)
	{
		Free += Aircraft.IsValid() ? 0 : 1;
	}
	for (const TObjectPtr<URTSEntityData>& Queued : ProductionComponent->GetQueue())
	{
		Free -= Queued && Queued->IsA<URTSAircraftData>() ? 1 : 0;
	}
	return Free;
}

int32 ARTSBuilding::ClaimPad(ARTSUnit& Aircraft)
{
	check(HasAuthority());
	const int32 Current = Pads.IndexOfByPredicate([&Aircraft](const TWeakObjectPtr<ARTSUnit>& Bound) { return Bound.Get() == &Aircraft; });
	if (Current != INDEX_NONE)
	{
		return Current;
	}
	if (GetFreePadCount() <= 0)
	{
		return INDEX_NONE;
	}
	const int32 Index = Pads.IndexOfByPredicate([](const TWeakObjectPtr<ARTSUnit>& Bound) { return !Bound.IsValid(); });
	if (Index != INDEX_NONE)
	{
		Pads[Index] = &Aircraft;
	}
	return Index;
}

void ARTSBuilding::ReleasePad(const ARTSUnit& Aircraft)
{
	check(HasAuthority());
	const int32 Index = Pads.IndexOfByPredicate([&Aircraft](const TWeakObjectPtr<ARTSUnit>& Bound) { return Bound.Get() == &Aircraft; });
	if (Index != INDEX_NONE)
	{
		Pads[Index] = nullptr;
	}
}

FVector ARTSBuilding::GetClosestFootprintPoint(const FVector& From) const
{
	const FVector Center = GetActorLocation();
	if (!BuildingData)
	{
		return Center;
	}
	// Buildings only turn in quarter turns, so the footprint stays axis aligned.
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(GetFootprint());
	return FVector(
		FMath::Clamp(From.X, Center.X - Extent.X, Center.X + Extent.X),
		FMath::Clamp(From.Y, Center.Y - Extent.Y, Center.Y + Extent.Y),
		Center.Z);
}

FIntPoint ARTSBuilding::GetFootprint() const
{
	return BuildingData
		? RTSBuildPlacement::GetRotatedFootprint(BuildingData->Footprint, RTSBuildPlacement::GetQuarterTurns(GetActorRotation().Yaw))
		: FIntPoint::ZeroValue;
}

void ARTSBuilding::OnRep_BuildingData()
{
	ApplyBuildingData();
}

void ARTSBuilding::OnRep_TeamId()
{
	ApplyTeamColor();
}

void ARTSBuilding::OnRep_ConstructionProgress()
{
	ApplyConstructionVisual();
}

void ARTSBuilding::ApplyBuildingData()
{
	if (!BuildingData)
	{
		return;
	}

	float MeshHeight = 0.f;
	RTSBuildPlacement::GetMeshPlacement(*BuildingData, MeshBaseOffset, MeshHeight);
	Mesh->SetStaticMesh(BuildingData->Mesh);
	ApplyParts();
	ApplyCollision(MeshHeight > 0.f ? MeshHeight : DefaultHeight);

	ApplyConstructionVisual();
	ApplyTeamColor();
}

void ARTSBuilding::ApplyParts()
{
	for (UStaticMeshComponent* Part : PartComponents)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	PartComponents.Reset();

	// Parts that share a mesh and how it draws (the HQ's sixteen door sections) draw as one instanced component, one
	// instance per part in the data's order: one draw per material section instead of one per part (#358).
	auto SameDraw = [](const FRTSBuildingPart& A, const FRTSBuildingPart& B)
	{
		return A.Mesh == B.Mesh && A.bCastShadow == B.bCastShadow && A.BoundsScale == B.BoundsScale
			&& A.MaxDrawDistance == B.MaxDrawDistance;
	};
	const TArray<FRTSBuildingPart>& Parts = BuildingData->Parts;
	TArray<bool> Done;
	Done.Init(false, Parts.Num());
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (Done[Index])
		{
			continue;
		}
		const FRTSBuildingPart& Data = Parts[Index];
		TArray<FTransform> Instances;
		for (int32 Other = Index; Other < Parts.Num(); ++Other)
		{
			if (!Done[Other] && SameDraw(Data, Parts[Other]))
			{
				Done[Other] = true;
				Instances.Add(Parts[Other].Transform);
			}
		}

		// Cosmetic, like Mesh: the collision boxes alone block and are picked.
		UStaticMeshComponent* Part = nullptr;
		if (Instances.Num() > 1)
		{
			UInstancedStaticMeshComponent* Instanced = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
			Instanced->AddInstances(Instances, /*bShouldReturnIndices*/ false);
			Part = Instanced;
		}
		else
		{
			Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Part->SetRelativeTransform(Data.Transform);
		}
		Part->SetStaticMesh(Data.Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(Data.bCastShadow);
		Part->SetBoundsScale(Data.BoundsScale);
		Part->SetCullDistance(Data.MaxDrawDistance);
		Part->SetupAttachment(Mesh);
		AddInstanceComponent(Part);
		Part->RegisterComponent();
		PartComponents.Add(Part);
	}
}

void ARTSBuilding::ApplyCollision(float Height)
{
	for (UBoxComponent* Box : ExtraCollision)
	{
		if (Box)
		{
			Box->DestroyComponent();
		}
	}
	ExtraCollision.Reset();

	// In the actor's own frame: the boxes turn with the actor, and the navmesh hole with them.
	const TArray<FRTSBuildingCollisionBox>& Boxes = BuildingData->CollisionBoxes;
	if (Boxes.IsEmpty())
	{
		const FVector Extent = RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint);
		Collision->SetBoxExtent(FVector(Extent.X, Extent.Y, Height * 0.5f));
		Collision->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));
		NavProbeOffset = FVector::ZeroVector;
		return;
	}

	const FVector Scale = BuildingData->MeshScale;
	NavProbeOffset = FVector::ZeroVector;
	bool bProbeInside = false;
	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		const FBox Box(Boxes[Index].Min * Scale, Boxes[Index].Max * Scale);
		UBoxComponent* Component = Collision;
		if (Index > 0)
		{
			Component = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
			Component->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
			Component->SetCanEverAffectNavigation(true);
			Component->bDynamicObstacle = true;
			Component->SetAreaClassOverride(UNavArea_Null::StaticClass());
			Component->SetupAttachment(Root);
			AddInstanceComponent(Component);
		}
		Component->SetBoxExtent(Box.GetExtent());
		Component->SetRelativeLocation(Box.GetCenter());
		if (Index > 0)
		{
			Component->RegisterComponent();
			ExtraCollision.Add(Component);
		}
		// The hole is looked for at the footprint centre when a box covers it, else in the middle of the first box.
		const FVector Ground(0.f, 0.f, Box.Min.Z);
		if (!bProbeInside && Box.IsInsideOrOnXY(Ground))
		{
			bProbeInside = true;
		}
	}
	if (!bProbeInside)
	{
		const FBox First(Boxes[0].Min * Scale, Boxes[0].Max * Scale);
		NavProbeOffset = FVector(First.GetCenter().X, First.GetCenter().Y, 0.f);
	}
}

TArray<UBoxComponent*> ARTSBuilding::GetCollisionComponents() const
{
	TArray<UBoxComponent*> Boxes = { Collision.Get() };
	Boxes.Append(ExtraCollision);
	return Boxes;
}

void ARTSBuilding::ApplyConstructionVisual()
{
	if (!BuildingData)
	{
		return;
	}
	const float Fraction = FMath::Max(ConstructionProgress, MinConstructionScale);
	Mesh->SetRelativeScale3D(BuildingData->MeshScale * FVector(1.f, 1.f, Fraction));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, MeshBaseOffset * Fraction));
}

void ARTSBuilding::ApplyTeamColor()
{
	// As custom primitive data, so buildings of a kind keep sharing their material instances whatever their team (#150).
	// A building of several meshes colours them all; their lit windows and lamps (CPD LampOn, the HQ's Operational) stay on
	// until the building's power state drives them (#196).
	const FVector4 Color(ARTSUnit::GetTeamColor(TeamId));
	Mesh->SetCustomPrimitiveDataVector4(RTSPrimitiveData::TeamColor, Color);
	Mesh->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LampOn, 1.f);
	for (UStaticMeshComponent* Part : PartComponents)
	{
		if (Part)
		{
			Part->SetCustomPrimitiveDataVector4(RTSPrimitiveData::TeamColor, Color);
			Part->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LampOn, 1.f);
		}
	}
}

void ARTSBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ARTSBuilding, BuildingData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARTSBuilding, TeamId, COND_InitialOnly);
	DOREPLIFETIME(ARTSBuilding, ConstructionProgress);
	DOREPLIFETIME_CONDITION(ARTSBuilding, PaidCost, COND_InitialOnly);
}
