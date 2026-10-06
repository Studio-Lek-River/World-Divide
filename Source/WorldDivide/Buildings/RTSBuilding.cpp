#include "Buildings/RTSBuilding.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Units/RTSUnit.h"

namespace
{
	/** A fresh construction site still shows a sliver of the building. */
	constexpr float MinConstructionScale = 0.05f;

	/** Collision height when the data has no mesh. */
	constexpr float DefaultHeight = 200.f;

	/** Gap between the footprint edge and a unit spawned on completion, beyond the unit's radius. */
	constexpr float SpawnClearance = 80.f;
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
	ApplyBuildingData();
}

void ARTSBuilding::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	HealthComponent->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
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

	if (HasAuthority() && bAutoConstruct && !IsConstructed())
	{
		UpdateConstruction(DeltaSeconds);
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
	}
	ApplyConstructionVisual();
}

void ARTSBuilding::RefreshWeapon()
{
	check(HasAuthority());
	const bool bPowered = BuildingData && (!BuildingData->bRequiresPower || !ARTSPlayerState::IsLowPowerForTeam(*GetWorld(), TeamId));
	WeaponComponent->SetWeaponData(IsConstructed() && bPowered ? BuildingData->Weapon.Get() : nullptr);
}

void ARTSBuilding::Destroyed()
{
	// Only completed buildings count towards the grid; cancelled or destroyed sites never did.
	if (HasAuthority() && IsConstructed() && GetWorld())
	{
		if (ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(*GetWorld(), TeamId))
		{
			Player->RecalculatePower(this);
		}
	}
	Super::Destroyed();
}

void ARTSBuilding::SpawnCompletionUnit()
{
	URTSUnitData* UnitData = BuildingData->SpawnOnComplete;
	if (!UnitData)
	{
		return;
	}

	// Outside the +X edge, the front until buildings can rotate; nudged onto the navmesh when one exists.
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(BuildingData->Footprint);
	FVector Location = GetActorLocation() + FVector(Extent.X + UnitData->SelectionRadius + SpawnClearance, 0.f, 0.f);
	if (const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation NavPoint;
		if (NavSystem->ProjectPointToNavigation(Location, NavPoint, FVector(200.f, 200.f, 250.f)))
		{
			Location.X = NavPoint.Location.X;
			Location.Y = NavPoint.Location.Y;
		}
	}

	FTransform Transform(Location);
	ARTSUnit* Unit = GetWorld()->SpawnActorDeferred<ARTSUnit>(
		ARTSUnit::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Unit)
	{
		return;
	}
	Unit->InitFromData(UnitData, TeamId);
	Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
	Unit->FinishSpawning(Transform);

	if (Unit->GetHarvesterComponent()->CanHarvest())
	{
		FRTSCommand Harvest;
		Harvest.Type = ERTSCommandType::Harvest;
		Unit->ExecuteCommand(Harvest);
	}
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
	if (Mesh->GetStaticMesh())
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(ARTSUnit::TeamColorParameter,ARTSUnit::GetTeamColor(TeamId));
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
