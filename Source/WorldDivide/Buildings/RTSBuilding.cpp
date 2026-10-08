#include "Buildings/RTSBuilding.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	/** A fresh construction site still shows a sliver of the building. */
	constexpr float MinConstructionScale = 0.05f;

	/** Collision height when the data has no mesh. */
	constexpr float DefaultHeight = 200.f;

	/** Gap between the footprint edge and a unit spawned at the exit, beyond the unit's radius. */
	constexpr float SpawnClearance = 80.f;

	/** A producer's rally point starts this far outside the exit edge, so finished units clear the spawn spot. */
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

	// From now on the building keeps its team in the match.
	ARTSGameState* GameState = GetWorld()->GetGameState<ARTSGameState>();
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
	// Construction and production hold once the match is over.
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
		ProductionComponent->SetRallyPoint(GetExitLocation(DefaultRallyDistance));
	}
	ApplyConstructionVisual();
}

void ARTSBuilding::RefreshWeapon()
{
	check(HasAuthority());
	const bool bPowered = BuildingData && (!BuildingData->bRequiresPower || !ARTSPlayerState::IsLowPowerForTeam(*GetWorld(), TeamId));
	WeaponComponent->SetWeaponData(IsConstructed() && bPowered ? BuildingData->Weapon.Get() : nullptr);
}

void ARTSBuilding::UpdateNavCut(float DeltaSeconds)
{
	check(HasAuthority());
	NavCutWaited += DeltaSeconds;

	// The rebuild runs asynchronously; the hole is there once the centre is off the navmesh and no build is left.
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation NavPoint;
	const bool bCut = !NavSystem
		|| (!NavSystem->IsNavigationBuildInProgress()
			&& !NavSystem->ProjectPointToNavigation(GetActorLocation(), NavPoint, NavCutQueryExtent));
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
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint);
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

	// Only completed buildings count towards the grid; cancelled or destroyed sites never did.
	if (HasAuthority() && IsConstructed() && GetWorld())
	{
		if (ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(*GetWorld(), TeamId))
		{
			Player->RecalculatePower(this);
		}
	}

	// Destroyed or cancelled, the team's last building that is not a defence takes the team out of the match.
	UWorld* World = GetWorld();
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

FVector ARTSBuilding::GetExitLocation(float Clearance) const
{
	const FVector Extent = BuildingData ? RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint) : FVector::ZeroVector;
	FVector Location = GetActorLocation() + FVector(Extent.X + Clearance, 0.f, 0.f);
	if (const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation NavPoint;
		if (NavSystem->ProjectPointToNavigation(Location, NavPoint, FVector(200.f, 200.f, 250.f)))
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

	FTransform Transform(GetExitLocation(Data->SelectionRadius + SpawnClearance));
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

FVector ARTSBuilding::GetClosestFootprintPoint(const FVector& From) const
{
	const FVector Center = GetActorLocation();
	if (!BuildingData)
	{
		return Center;
	}
	// Buildings are never rotated, so the footprint is axis aligned.
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint);
	return FVector(
		FMath::Clamp(From.X, Center.X - Extent.X, Center.X + Extent.X),
		FMath::Clamp(From.Y, Center.Y - Extent.Y, Center.Y + Extent.Y),
		Center.Z);
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

	const float Height = MeshHeight > 0.f ? MeshHeight : DefaultHeight;
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint);
	Collision->SetBoxExtent(FVector(Extent.X, Extent.Y, Height * 0.5f));
	Collision->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));

	ApplyConstructionVisual();
	ApplyTeamColor();
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
	if (!Mesh->GetStaticMesh())
	{
		return;
	}
	// Every slot, since a model can carry several materials.
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(Slot))
		{
			Material->SetVectorParameterValue(ARTSUnit::TeamColorParameter, ARTSUnit::GetTeamColor(TeamId));
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
