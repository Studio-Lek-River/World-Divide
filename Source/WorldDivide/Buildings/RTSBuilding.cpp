#include "Buildings/RTSBuilding.h"

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuildingLightSubsystem.h"
#include "Buildings/RTSBuildingMotionSubsystem.h"
#include "Buildings/RTSBuildingRuin.h"
#include "Buildings/RTSBuildingStateComponent.h"
#include "Buildings/RTSGarageComponent.h"
#include "Buildings/RTSProductionComponent.h"
#include "Buildings/RTSSuperweaponComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSFlagTypes.h"
#include "Data/RTSKitPieceData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureLightProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSNearestToView.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"
#include "UObject/UObjectIterator.h"
#include "WorldDivide.h"

namespace
{
	/** A fresh construction site still shows a sliver of the building. */
	constexpr float MinConstructionScale = 0.05f;

	/** Collision height when the data has no mesh. */
	constexpr float DefaultCollisionHeight = 200.f;

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

	TAutoConsoleVariable<int32> CVarKitLensSteady(
		TEXT("rts.Debug.KitLensSteady"), 0,
		TEXT("Holds the blinking kit lenses (the mast LEDs) lit, to picture them shining."));

	/** Shows every building's kit looks again, so a brightness cvar takes effect at once. */
	void RefreshKitBrightness(IConsoleVariable*)
	{
		for (TObjectIterator<ARTSBuilding> It; It; ++It)
		{
			if (!It->IsTemplate() && It->GetWorld())
			{
				const TArray<FRTSKitLook> Looks = It->GetKitLooks();
				It->SetKitLooks(Looks);
			}
		}
	}

	TAutoConsoleVariable<float> CVarKitBrightness(
		TEXT("rts.Debug.KitBrightness"), 1.f,
		TEXT("Scales the kit lamps' brightness on this machine (#453): light, lens glow, glare and beam; 1 as the data says."),
		FConsoleVariableDelegate::CreateStatic(&RefreshKitBrightness));

	TAutoConsoleVariable<FString> CVarKitBrightnessOnly(
		TEXT("rts.Debug.KitBrightnessOnly"), TEXT(""),
		TEXT("Limits rts.Debug.KitBrightness to the mount with this name or the piece whose id ends in it (03_EntranceLight); empty scales all."),
		FConsoleVariableDelegate::CreateStatic(&RefreshKitBrightness));

	/** How much rts.Debug.KitBrightness scales Part's lamp: 1 unless it is set and matches the part. */
	float GetDebugBrightness(const FRTSKitPart& Part)
	{
		const FString Only = CVarKitBrightnessOnly.GetValueOnGameThread();
		const bool bMatches = Only.IsEmpty() || Part.Name == FName(*Only) || Part.Piece.EndsWith(Only);
		return bMatches ? FMath::Max(CVarKitBrightness.GetValueOnGameThread(), 0.f) : 1.f;
	}
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
	GarageComponent = CreateDefaultSubobject<URTSGarageComponent>(TEXT("Garage"));
	StateComponent = CreateDefaultSubobject<URTSBuildingStateComponent>(TEXT("State"));

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
	// Only cancelling refunds; a construction site the enemy destroys is lost with everything paid for it. Its rubble is
	// cosmetic, so the footprint is free at once.
	StateComponent->Refresh();
	ARTSBuildingRuin::SpawnFor(*this);
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
	if (GetNetMode() != NM_DedicatedServer)
	{
		StateComponent->Update(DeltaSeconds);
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
		GarageComponent->UpdateGarage(DeltaSeconds);
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
	bOwnerLowPower = ARTSPlayerState::IsLowPowerForTeam(*GetWorld(), TeamId);
	const bool bPowered = BuildingData && (!BuildingData->bRequiresPower || !bOwnerLowPower);
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

	const FRTSBuildingFootprint Footprint = GetGroundFootprint();
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

	// Nothing more leaves the garage.
	if (HasAuthority())
	{
		GarageComponent->Abandon();
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

void ARTSBuilding::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (URTSBuildingMotionSubsystem* Motion = GetWorld() ? GetWorld()->GetSubsystem<URTSBuildingMotionSubsystem>() : nullptr)
	{
		Motion->Unregister(*this);
	}
	if (URTSBuildingLightSubsystem* Lights = GetWorld() ? GetWorld()->GetSubsystem<URTSBuildingLightSubsystem>() : nullptr)
	{
		Lights->Unregister(*this);
	}
	Super::EndPlay(EndPlayReason);
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
	const float LaneOffset = BuildingData ? RTSBuildPlacement::GetExitLaneOffset(*BuildingData) : 0.f;
	FVector Location = GetActorLocation() + GetActorForwardVector() * (Extent.X + Clearance) + GetActorRightVector() * LaneOffset;
	if (const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*GetWorld(), Agent ? *Agent : RTSNavAgents::GetNarrowestAgent()))
	{
		FNavLocation NavPoint;
		if (NavMesh->ProjectPoint(Location, NavPoint, FVector(200.f, 200.f, 250.f)))
		{
			FVector Nudge(NavPoint.Location.X - Location.X, NavPoint.Location.Y - Location.Y, 0.f);
			// A lane off the middle (the garage's) is the lane the drive-out leaves on: nudged along it only, never out of it.
			if (LaneOffset != 0.f)
			{
				Nudge = GetActorForwardVector() * FVector::DotProduct(Nudge, GetActorForwardVector());
			}
			Location += Nudge;
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

void ARTSBuilding::DeliverUnit(URTSUnitData& Data)
{
	check(HasAuthority());
	if (GarageComponent->Accepts(Data))
	{
		GarageComponent->Enqueue(Data);
		return;
	}
	if (ARTSUnit* Unit = SpawnUnitAtExit(&Data))
	{
		ProductionComponent->SendOut(*Unit);
	}
}

bool ARTSBuilding::SetKitInstanceData(FName Part, TConstArrayView<float> Values)
{
	UStaticMeshComponent* Component = nullptr;
	int32 Instance = INDEX_NONE;
	UInstancedStaticMeshComponent* Instanced = FindPart(Part, Component, Instance) ? Cast<UInstancedStaticMeshComponent>(Component) : nullptr;
	if (!Instanced || Instance == INDEX_NONE || Instanced->NumCustomDataFloats != RTSKitLensData::Floats
		|| Values.Num() != RTSKitLensData::Floats)
	{
		return false;
	}
	// Per instance, then one update of the instances: never a render state rebuild (BUILDING_KIT.md, #387).
	Instanced->SetCustomData(Instance, Values);
	Instanced->MarkRenderInstancesDirty();
	return true;
}

void ARTSBuilding::SetForcedLOD(int32 LOD)
{
	if (ForcedLOD == LOD)
	{
		return;
	}
	ForcedLOD = LOD;
	// ForcedLodModel counts from 1; 0 lets the renderer choose. A mesh of one LOD keeps it.
	const int32 Model = LOD == INDEX_NONE ? 0 : LOD + 1;
	for (UStaticMeshComponent* Component : TInlineComponentArray<UStaticMeshComponent*>(this))
	{
		if (Component->GetStaticMesh() && Component->GetStaticMesh()->GetNumLODs() > 1 && Component->ForcedLodModel != Model)
		{
			Component->SetForcedLodModel(Model);
		}
	}
}

bool ARTSBuilding::FindPart(FName Name, UStaticMeshComponent*& OutComponent, int32& OutInstance) const
{
	const TPair<int32, int32>* Found = PartIndex.Find(Name);
	if (!Found || !PartComponents.IsValidIndex(Found->Key) || !PartComponents[Found->Key])
	{
		return false;
	}
	OutComponent = PartComponents[Found->Key];
	OutInstance = Found->Value;
	return true;
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
	return GetGroundFootprint().GetClosestPoint(From);
}

FRTSBuildingFootprint ARTSBuilding::GetGroundFootprint() const
{
	return RTSBuildPlacement::MakeFootprint(BuildingData ? BuildingData->Footprint : FIntPoint::ZeroValue, GetActorLocation(),
		GetActorRotation().Yaw);
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
	ForcedLOD = INDEX_NONE;
	const FRTSKitExpansion Kit = RTSKit::Expand(*BuildingData);
	for (const FString& Error : Kit.Errors)
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("%s: %s"), *GetName(), *Error);
	}
	ApplyParts(Kit);
	ApplyLights(Kit);
	ApplyCollision(MeshHeight > 0.f ? MeshHeight : DefaultCollisionHeight);
	GarageComponent->ApplyBuildingData();

	ApplyConstructionVisual();
	ApplyTeamColor();
}

void ARTSBuilding::ApplyParts(const FRTSKitExpansion& Kit)
{
	for (UStaticMeshComponent* Part : PartComponents)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	PartComponents.Reset();
	PartIndex.Reset();
	KitParts = Kit.PartInfo;
	KitLooks.Init(FRTSKitLook(), KitParts.Num());
	KitSpins.Init(FKitSpin(), KitParts.Num());

	// The flag the building flies: its cloth part takes the flag's material and is as wide as its height times the flag's
	// aspect (#351).
	const FRTSFlagType* Flag = BuildingData->FlagTypes && !BuildingData->FlagPart.IsNone()
		? BuildingData->FlagTypes->Find(BuildingData->FlagType) : nullptr;

	// The data's parts, then the mounted kit pieces (RTSKit::Expand), which always draw instanced with their lens's
	// per-instance data (BUILDING_KIT.md), even alone.
	const int32 OwnParts = BuildingData->Parts.Num();
	TArray<const FRTSBuildingPart*> Parts;
	Parts.Reserve(OwnParts + Kit.Parts.Num());
	for (const FRTSBuildingPart& Part : BuildingData->Parts)
	{
		Parts.Add(&Part);
	}
	for (const FRTSBuildingPart& Part : Kit.Parts)
	{
		Parts.Add(&Part);
	}

	// Parts that share a mesh and how it draws (the HQ's sixteen door sections, its two mast beacons) draw as one instanced
	// component, one instance per part in the data's order: one draw per material section instead of one per part (#358). A
	// part that turns on its own and the flag draw on their own.
	auto Shares = [this](const FRTSBuildingPart& Part)
	{
		return !Part.Motion.IsAnimated() && Part.Name != BuildingData->FlagPart;
	};
	auto SameDraw = [&Shares, &Parts, OwnParts](int32 IndexA, int32 IndexB)
	{
		const FRTSBuildingPart& A = *Parts[IndexA];
		const FRTSBuildingPart& B = *Parts[IndexB];
		return (IndexA >= OwnParts) == (IndexB >= OwnParts) && Shares(A) && Shares(B) && A.Mesh == B.Mesh
			&& A.bCastShadow == B.bCastShadow && A.BoundsScale == B.BoundsScale && A.MaxDrawDistance == B.MaxDrawDistance
			&& A.WPODisableDistance == B.WPODisableDistance;
	};
	TArray<bool> Done;
	Done.Init(false, Parts.Num());
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (Done[Index])
		{
			continue;
		}
		const FRTSBuildingPart& Data = *Parts[Index];
		const bool bKit = Index >= OwnParts;
		TArray<FTransform> Instances;
		TArray<FName> Names;
		TArray<int32> KitIndices;
		for (int32 Other = Index; Other < Parts.Num(); ++Other)
		{
			if (!Done[Other] && (Other == Index || SameDraw(Index, Other)))
			{
				Done[Other] = true;
				Instances.Add(Parts[Other]->Transform);
				Names.Add(Parts[Other]->Name);
				KitIndices.Add(Other - OwnParts);
			}
		}
		const bool bFlag = Flag && Data.Name == BuildingData->FlagPart && Instances.Num() == 1;
		if (bFlag)
		{
			FVector Scale = Instances[0].GetScale3D();
			Scale.X = Scale.Z * Flag->Aspect;
			Instances[0].SetScale3D(Scale);
		}

		// Cosmetic, like Mesh: the collision boxes alone block and are picked.
		UStaticMeshComponent* Part = nullptr;
		const bool bInstanced = bKit || Instances.Num() > 1;
		if (bInstanced)
		{
			UInstancedStaticMeshComponent* Instanced = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
			if (bKit)
			{
				// Before the instances: setting the count resets their data.
				Instanced->SetNumCustomDataFloats(RTSKitLensData::Floats);
			}
			Instanced->AddInstances(Instances, /*bShouldReturnIndices*/ false);
			if (bKit)
			{
				for (int32 Instance = 0; Instance < KitIndices.Num(); ++Instance)
				{
					Instanced->SetCustomData(Instance, MakeArrayView(Kit.InstanceData).Slice(
						KitIndices[Instance] * RTSKitLensData::Floats, RTSKitLensData::Floats));
				}
			}
			Part = Instanced;
		}
		else
		{
			Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Part->SetRelativeTransform(Instances[0]);
		}
		Part->SetStaticMesh(Data.Mesh);
		if (bKit && CVarKitLensSteady.GetValueOnGameThread() != 0)
		{
			for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
			{
				float Blink = 0.f;
				UMaterialInterface* Material = Part->GetMaterial(Slot);
				if (Material && Material->GetScalarParameterValue(TEXT("Blink"), Blink) && Blink > 0.f)
				{
					Part->CreateDynamicMaterialInstance(Slot, Material)->SetScalarParameterValue(TEXT("Blink"), 0.f);
				}
			}
		}
		if (bFlag)
		{
			Part->SetMaterial(0, Flag->Material);
		}
		Part->SetWorldPositionOffsetDisableDistance(FMath::RoundToInt(Data.WPODisableDistance));
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(Data.bCastShadow);
		Part->SetBoundsScale(Data.BoundsScale);
		Part->SetCullDistance(Data.MaxDrawDistance);
		Part->SetupAttachment(Mesh);
		AddInstanceComponent(Part);
		Part->RegisterComponent();
		for (int32 Instance = 0; Instance < Names.Num(); ++Instance)
		{
			PartIndex.Add(Names[Instance], { PartComponents.Num(), bInstanced ? Instance : INDEX_NONE });
		}
		PartComponents.Add(Part);
	}

	if (URTSBuildingMotionSubsystem* Motion = GetWorld() ? GetWorld()->GetSubsystem<URTSBuildingMotionSubsystem>() : nullptr)
	{
		Motion->Register(*this);
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
	// A building of several meshes colours them all; the state component puts their lamps (CPD LampOn, the HQ's
	// Operational) on or out (#196).
	const FVector4 Color(ARTSUnit::GetTeamColor(TeamId));
	Mesh->SetCustomPrimitiveDataVector4(RTSPrimitiveData::TeamColor, Color);
	for (UStaticMeshComponent* Part : PartComponents)
	{
		if (Part)
		{
			Part->SetCustomPrimitiveDataVector4(RTSPrimitiveData::TeamColor, Color);
		}
	}
	ApplyLightData();
	StateComponent->Refresh(/*bForce*/ true);
}

ULocalLightComponent* ARTSBuilding::CreateLight(const FRTSBuildingLight& Data)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	ULocalLightComponent* Light = nullptr;
	switch (Data.Type)
	{
	case ERTSBuildingLightType::Spot:
	{
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this, NAME_None, RF_Transient);
		Spot->SetInnerConeAngle(FMath::Min(Data.InnerConeAngle, Data.OuterConeAngle));
		Spot->SetOuterConeAngle(Data.OuterConeAngle);
		Light = Spot;
		break;
	}
	case ERTSBuildingLightType::Rect:
	{
		URectLightComponent* Rect = NewObject<URectLightComponent>(this, NAME_None, RF_Transient);
		Rect->SetSourceWidth(Data.SourceWidth);
		Rect->SetSourceHeight(Data.SourceHeight);
		Rect->SetBarnDoorAngle(Data.BarnDoorAngle);
		Rect->SetBarnDoorLength(Data.BarnDoorLength);
		Light = Rect;
		break;
	}
	default:
		Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
		break;
	}
	// It lights what it faces directly: no shadows, no GI, fog or translucency, as the vehicles' lights (#223). The lamp's
	// own glow is its material's, its beam in the air M_Kit_Glow's (#440: faked, not volumetric fog).
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetCastShadows(false);
	Light->bAffectGlobalIllumination = false;
	Light->bAffectTranslucentLighting = false;
	Light->IndirectLightingIntensity = 0.f;
	Light->VolumetricScatteringIntensity = 0.f;
	// A hot centre and a soft edge (#440): its profile, normalised so its candelas stay its brightness, and a source of size.
	Light->IESTexture = Data.Profile;
	Light->bUseIESBrightness = false;
	if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
	{
		Point->SetSourceRadius(Data.SourceRadius);
		Point->SetSourceLength(Data.SourceLength);
	}
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(Data.Candelas);
	Light->SetLightColor(Data.Color);
	Light->SetAttenuationRadius(Data.Radius);
	Light->SetCanEverAffectNavigation(false);
	Light->ComponentTags.Add(GetLightGroupTag(Data.Group));
	// On Mesh, as the parts, so the lamps stay on the model; its scale stays out of the light.
	Light->SetUsingAbsoluteScale(true);
	Light->SetupAttachment(Mesh);
	Light->SetRelativeLocationAndRotation(Data.Location, Data.Rotation);
	Light->SetVisibility(false);
	AddInstanceComponent(Light);
	Light->RegisterComponent();
	return Light;
}

void ARTSBuilding::ApplyKitGlows(const FRTSKitExpansion& Kit)
{
	for (UInstancedStaticMeshComponent* Glow : GlowComponents)
	{
		if (Glow)
		{
			Glow->DestroyComponent();
		}
	}
	GlowComponents.Reset();
	KitGlows.Reset();
	GlowIsBeam.Reset();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// One component per mesh: every beam of a piece, and every halo, draw together.
	for (const FRTSKitGlow& Glow : Kit.Glows)
	{
		if (!Glow.Mesh || !KitParts.IsValidIndex(Glow.Part))
		{
			continue;
		}
		int32 Component = GlowComponents.IndexOfByPredicate([&Glow](const UInstancedStaticMeshComponent* C) { return C && C->GetStaticMesh() == Glow.Mesh; });
		if (Component == INDEX_NONE)
		{
			UInstancedStaticMeshComponent* Instanced = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
			Instanced->SetNumCustomDataFloats(RTSKitGlowData::Floats);
			Instanced->SetStaticMesh(Glow.Mesh);
			Instanced->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Instanced->SetGenerateOverlapEvents(false);
			Instanced->SetCanEverAffectNavigation(false);
			Instanced->SetCastShadow(false);
			Instanced->SetupAttachment(Mesh);
			Instanced->SetVisibility(false);
			AddInstanceComponent(Instanced);
			Instanced->RegisterComponent();
			Component = GlowComponents.Add(Instanced);
			GlowIsBeam.Add(Glow.bBeam);
		}
		const int32 Instance = GlowComponents[Component]->AddInstance(Glow.Transform);
		KitGlows.Add({ Glow.Part, Component, Instance, Glow.Color });
	}
	ShowKitGlows();
}

void ARTSBuilding::ShowKitGlows()
{
	const bool bBeams = URTSBuildingLightSubsystem::AreKitBeamsOn();
	for (int32 Index = 0; Index < GlowComponents.Num(); ++Index)
	{
		if (GlowComponents[Index])
		{
			GlowComponents[Index]->SetVisibility(bLightsNight && (bBeams || !GlowIsBeam[Index]));
		}
	}
}

FName ARTSBuilding::GetLightGroupTag(FName Group)
{
	return FName(*(TEXT("Light.") + Group.ToString()));
}

void ARTSBuilding::ApplyLights(const FRTSKitExpansion& Kit)
{
	for (ULocalLightComponent* Light : LightComponents)
	{
		if (Light)
		{
			Light->DestroyComponent();
		}
	}
	LightComponents.Reset();
	KitLights.Reset();
	bLightsShown = false;
	for (const FRTSBuildingLight& Data : BuildingData->Lights)
	{
		if (Data.Group != URTSBuildingData::GarageBeaconsGroup)
		{
			if (ULocalLightComponent* Light = CreateLight(Data))
			{
				LightComponents.Add(Light);
			}
		}
	}
	OwnLightCount = LightComponents.Num();
	// Kit.Lights holds one light per part whose piece has one, in the parts' order.
	int32 KitLight = 0;
	for (int32 Part = 0; Part < KitParts.Num() && KitLight < Kit.Lights.Num(); ++Part)
	{
		if (!KitParts[Part].bHasLight)
		{
			continue;
		}
		const FRTSBuildingLight& Data = Kit.Lights[KitLight++];
		if (ULocalLightComponent* Light = CreateLight(Data))
		{
			KitLights.Add({ Part, LightComponents.Add(Light), Data.Candelas, Data.Color, Data.Rotation });
		}
	}
	ApplyKitGlows(Kit);
	ApplyKitLightLooks();
	// Every building with parts or lights: the subsystem raises their glow in the dark and shows the nearest ones' lights.
	if (URTSBuildingLightSubsystem* Lights = GetWorld() ? GetWorld()->GetSubsystem<URTSBuildingLightSubsystem>() : nullptr)
	{
		if (!PartComponents.IsEmpty() || !LightComponents.IsEmpty())
		{
			Lights->Register(*this);
		}
		else
		{
			Lights->Unregister(*this);
		}
	}
}

void ARTSBuilding::ApplyLightData()
{
	// All four at once: writing one index fills those below it that were unset with 0, which would switch a group off.
	const float Values[] = {
		IsLightGroupOn(TEXT("Windows")) ? 1.f : 0.f,
		IsLightGroupOn(TEXT("Fixtures")) ? 1.f : 0.f,
		IsLightGroupOn(TEXT("Accents")) ? 1.f : 0.f,
		bLightsNight ? 1.f : 0.f,
	};
	static_assert(RTSPrimitiveData::LightNight == RTSPrimitiveData::LightWindows + 3);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
	{
		Mesh->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LightWindows + Index, Values[Index]);
	}
	for (UStaticMeshComponent* Part : PartComponents)
	{
		if (Part)
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
			{
				Part->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LightWindows + Index, Values[Index]);
			}
		}
	}
}

void ARTSBuilding::SetLightGroupOn(FName Group, bool bOn)
{
	if (IsLightGroupOn(Group) == bOn)
	{
		return;
	}
	if (bOn)
	{
		LightGroupsOff.Remove(Group);
	}
	else
	{
		LightGroupsOff.Add(Group);
	}
	ApplyLightData();
	ShowLights(bLightsShown);
}

void ARTSBuilding::SetLightsNight(bool bNight)
{
	if (bLightsNight != bNight)
	{
		bLightsNight = bNight;
		ApplyLightData();
		ShowKitGlows();
	}
}

void ARTSBuilding::ShowLights(bool bShow)
{
	bLightsShown = bShow;
	for (int32 Index = 0; Index < LightComponents.Num(); ++Index)
	{
		ULocalLightComponent* Light = LightComponents[Index];
		if (!Light)
		{
			continue;
		}
		if (Index >= OwnLightCount)
		{
			// A kit light: its look switches it (UpdateKitLights), not its group, so a battery light outlives the Fixtures.
			if (!bShow)
			{
				Light->SetVisibility(false);
			}
			continue;
		}
		bool bGroupOn = true;
		for (const FName& Off : LightGroupsOff)
		{
			bGroupOn &= !Light->ComponentHasTag(GetLightGroupTag(Off));
		}
		Light->SetVisibility(bShow && bGroupOn);
	}
}

void ARTSBuilding::SetKitLooks(TConstArrayView<FRTSKitLook> Looks)
{
	if (Looks.Num() != KitParts.Num())
	{
		return;
	}
	KitLooks = Looks;
	// A turning beacon switched on or off spins up or coasts down from now, its lamp glowing up or fading (#459).
	const float Now = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
	for (int32 Index = 0; Index < KitParts.Num(); ++Index)
	{
		FKitSpin& Spin = KitSpins[Index];
		const bool bOn = KitLooks[Index].Level > 0.f;
		if (KitParts[Index].Behaviour != ERTSKitLightBehaviour::Rotate || bOn == (Spin.On > Spin.Off))
		{
			Spin.Level = bOn ? KitLooks[Index].Level : Spin.Level;
			continue;
		}
		if (bOn)
		{
			Spin.On = Now;
			Spin.Off = Now - 1.f;
			Spin.Level = KitLooks[Index].Level;
		}
		else
		{
			Spin.Off = Now;
		}
	}
	// Per instance, then one update of each component's instances: never a render state rebuild (BUILDING_KIT.md, #387).
	TArray<UInstancedStaticMeshComponent*, TInlineAllocator<8>> Touched;
	for (int32 Index = 0; Index < KitParts.Num(); ++Index)
	{
		const FRTSKitPart& Part = KitParts[Index];
		const FRTSKitLook& Look = KitLooks[Index];
		UStaticMeshComponent* Component = nullptr;
		int32 Instance = INDEX_NONE;
		UInstancedStaticMeshComponent* Instanced = FindPart(Part.Name, Component, Instance) ? Cast<UInstancedStaticMeshComponent>(Component) : nullptr;
		if (!Instanced || Instance == INDEX_NONE || Instanced->NumCustomDataFloats != RTSKitLensData::Floats)
		{
			continue;
		}
		const FLinearColor Color = Part.IndicatorColor * (Look.bBattery ? RTSBuildingState::BatteryTint : FLinearColor::White);
		float Lens[RTSKitLensData::Floats] = {};
		Lens[RTSKitLensData::Level] = GetKitLevel(Index) * Part.Brightness * GetDebugBrightness(Part);
		// The state already puts a normal lens out with its group; the Fixtures gate stays for the look between refreshes.
		Lens[RTSKitLensData::Group] = Part.Group == TEXT("Fixtures") && !Look.bBattery && !Part.bEmergency ? 1.f : 0.f;
		Lens[RTSKitLensData::Fault] = Look.Fault;
		Lens[RTSKitLensData::Flash] = Look.Flash;
		Lens[RTSKitLensData::Color + 0] = Color.R;
		Lens[RTSKitLensData::Color + 1] = Color.G;
		Lens[RTSKitLensData::Color + 2] = Color.B;
		Lens[RTSKitLensData::SpinOn] = KitSpins[Index].On;
		Lens[RTSKitLensData::SpinOff] = KitSpins[Index].Off;
		Instanced->SetCustomData(Instance, Lens);
		Touched.AddUnique(Instanced);
	}
	for (UInstancedStaticMeshComponent* Instanced : Touched)
	{
		Instanced->MarkRenderInstancesDirty();
	}
	ApplyKitLightLooks();
}

void ARTSBuilding::ApplyKitLightLooks()
{
	for (const FKitLight& Kit : KitLights)
	{
		ULocalLightComponent* Light = LightComponents[Kit.Component];
		if (!Light || !KitLooks.IsValidIndex(Kit.Part))
		{
			continue;
		}
		const FRTSKitLook& Look = KitLooks[Kit.Part];
		// The mount's Brightness is in its Candelas and glow colours already (RTSKit::Expand).
		Light->SetIntensity(Kit.Candelas * GetKitLevel(Kit.Part) * GetDebugBrightness(KitParts[Kit.Part]));
		Light->SetLightColor(Kit.Color * (Look.bBattery ? RTSBuildingState::BatteryTint : FLinearColor::White));
	}
	// The beams and halos glow with their part's look, flicker and flash included (M_Kit_Glow repeats the lens's sums).
	for (const FKitGlow& Glow : KitGlows)
	{
		UInstancedStaticMeshComponent* Component = GlowComponents[Glow.Component];
		if (!Component || !KitLooks.IsValidIndex(Glow.Part))
		{
			continue;
		}
		const FRTSKitLook& Look = KitLooks[Glow.Part];
		const FLinearColor Color = Glow.Color * (Look.bBattery ? RTSBuildingState::BatteryTint : FLinearColor::White);
		float Values[RTSKitGlowData::Floats] = {};
		Values[RTSKitGlowData::Level] = GetKitLevel(Glow.Part) * GetDebugBrightness(KitParts[Glow.Part]);
		Values[RTSKitGlowData::Fault] = Look.Fault;
		Values[RTSKitGlowData::Flash] = Look.Flash;
		Values[RTSKitGlowData::Color + 0] = Color.R;
		Values[RTSKitGlowData::Color + 1] = Color.G;
		Values[RTSKitGlowData::Color + 2] = Color.B;
		if (KitParts[Glow.Part].Behaviour == ERTSKitLightBehaviour::Rotate)
		{
			// Its beam turns and its halo flashes as the beam faces the camera, in M_Kit_Glow (#459).
			Values[RTSKitGlowData::Turn] = RTSKit::RotateRate;
			Values[RTSKitGlowData::SpinOn] = KitSpins[Glow.Part].On;
			Values[RTSKitGlowData::SpinOff] = KitSpins[Glow.Part].Off;
		}
		Component->SetCustomData(Glow.Instance, Values);
	}
	for (UInstancedStaticMeshComponent* Component : GlowComponents)
	{
		if (Component)
		{
			Component->MarkRenderInstancesDirty();
		}
	}
}

float ARTSBuilding::GetKitLevel(int32 Part) const
{
	return KitParts[Part].Behaviour == ERTSKitLightBehaviour::Rotate ? KitSpins[Part].Level : KitLooks[Part].Level;
}

void ARTSBuilding::UpdateKitLights(double Time, TConstArrayView<FVector> Cameras, const TOptional<FVector>& Override, bool bSecondary, int32& Budget)
{
	for (const FKitLight& Kit : KitLights)
	{
		ULocalLightComponent* Light = LightComponents[Kit.Component];
		if (!Light)
		{
			continue;
		}
		const FRTSKitPart& Part = KitParts[Kit.Part];
		const FRTSKitLook& Look = KitLooks[Kit.Part];
		// A turning beacon shines while its lamp glows, coasting down included, and takes no budget once dark (#459).
		const bool bTurning = Part.Behaviour == ERTSKitLightBehaviour::Rotate;
		const RTSKit::FSpin Spin = bTurning ? RTSKit::GetSpin(KitSpins[Kit.Part].On, KitSpins[Kit.Part].Off, Time) : RTSKit::FSpin();
		bool bOn = bLightsShown && GetKitLevel(Kit.Part) > 0.f && (!bTurning || Spin.Lamp > 0.f) && Budget > 0
			&& (Part.Priority == ERTSKitLightPriority::Primary || bSecondary);
		if (bOn && Part.MaxDrawDistance > 0.f)
		{
			bOn = RTSNearestToView::GetDistance(Cameras, Override, Light->GetComponentLocation()) <= Part.MaxDrawDistance;
		}
		if (bOn)
		{
			// A light in the dark part of its flash or flicker still holds its place, so the others do not swap in and out.
			--Budget;
			bOn = RTSKit::IsLensLit(Look.Fault, Look.Flash, Time);
		}
		if (bOn && bTurning)
		{
			// In step with its mirror's SWEEP, which faces the piece's +X at no turns, as the light does at its rest.
			const double Turn = FMath::Frac(Spin.Turns) * 360.0;
			Light->SetRelativeRotation(FRotator(Kit.Rotation.Pitch, Kit.Rotation.Yaw + Turn, Kit.Rotation.Roll));
			Light->SetIntensity(Kit.Candelas * GetKitLevel(Kit.Part) * GetDebugBrightness(Part) * Spin.Lamp);
		}
		Light->SetVisibility(bOn);
	}
}

int32 ARTSBuilding::GetLitKitLightCount() const
{
	int32 Count = 0;
	for (const FKitLight& Kit : KitLights)
	{
		const ULocalLightComponent* Light = LightComponents[Kit.Component];
		Count += Light && Light->IsVisible() ? 1 : 0;
	}
	return Count;
}

void ARTSBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ARTSBuilding, BuildingData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARTSBuilding, TeamId, COND_InitialOnly);
	DOREPLIFETIME(ARTSBuilding, ConstructionProgress);
	DOREPLIFETIME_CONDITION(ARTSBuilding, PaidCost, COND_InitialOnly);
	DOREPLIFETIME(ARTSBuilding, bOwnerLowPower);
}
