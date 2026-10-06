#include "Economy/RTSSupplySource.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "NavAreas/NavArea_Null.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** Share of the full height an empty source keeps, so the spot stays visible. */
	constexpr float MinRemainingScale = 0.1f;
}

ARTSSupplySource::ARTSSupplySource()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Root);
	Collision->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	// Carved out of the navmesh like a building, without a navmesh on top.
	Collision->SetCanEverAffectNavigation(true);
	Collision->bDynamicObstacle = true;
	Collision->SetAreaClassOverride(UNavArea_Null::StaticClass());

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(Root);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCanEverAffectNavigation(false);

	bReplicates = true;
	SetNetUpdateFrequency(2.f);
	bAlwaysRelevant = true;
}

void ARTSSupplySource::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	Remaining = Amount;
	ApplyFootprint();
}

void ARTSSupplySource::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Clients start full too and catch up through replication.
	Remaining = Amount;
	ApplyFootprint();
}

int32 ARTSSupplySource::Take(int32 Wanted)
{
	check(HasAuthority());
	const int32 Taken = FMath::Clamp(Wanted, 0, Remaining);
	Remaining -= Taken;
	ApplyRemainingVisual();
	return Taken;
}

FVector ARTSSupplySource::GetClosestFootprintPoint(const FVector& From) const
{
	const FVector Center = GetActorLocation();
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Footprint);
	return FVector(
		FMath::Clamp(From.X, Center.X - Extent.X, Center.X + Extent.X),
		FMath::Clamp(From.Y, Center.Y - Extent.Y, Center.Y + Extent.Y),
		Center.Z);
}

ARTSSupplySource* ARTSSupplySource::FindNearest(const UWorld& World, const FVector& From, float MaxDistance,
	TConstArrayView<TWeakObjectPtr<ARTSSupplySource>> Exclude)
{
	ARTSSupplySource* Nearest = nullptr;
	float NearestDistance = MaxDistance;
	for (TActorIterator<ARTSSupplySource> It(&World); It; ++It)
	{
		if (It->IsDepleted() || Exclude.Contains(*It))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(From, It->GetClosestFootprintPoint(From));
		if (Distance <= NearestDistance)
		{
			Nearest = *It;
			NearestDistance = Distance;
		}
	}
	return Nearest;
}

void ARTSSupplySource::OnRep_Remaining()
{
	ApplyRemainingVisual();
}

void ARTSSupplySource::ApplyFootprint()
{
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Footprint);
	Collision->SetBoxExtent(FVector(Extent.X, Extent.Y, Height * 0.5f));
	Collision->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));
	MeshComponent->SetStaticMesh(Mesh);
	ApplyRemainingVisual();
}

void ARTSSupplySource::ApplyRemainingVisual()
{
	if (!Mesh)
	{
		return;
	}
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	if (Bounds.BoxExtent.X <= 0.f || Bounds.BoxExtent.Y <= 0.f || Bounds.BoxExtent.Z <= 0.f)
	{
		return;
	}

	const float Fraction = Amount > 0 ? FMath::Max(static_cast<float>(Remaining) / Amount, MinRemainingScale) : MinRemainingScale;
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Footprint);
	const FVector Scale(Extent.X / Bounds.BoxExtent.X, Extent.Y / Bounds.BoxExtent.Y, Height * 0.5f * Fraction / Bounds.BoxExtent.Z);
	MeshComponent->SetRelativeScale3D(Scale);
	// Stand the scaled mesh on the actor origin.
	MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, (Bounds.BoxExtent.Z - Bounds.Origin.Z) * Scale.Z));
}

void ARTSSupplySource::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTSSupplySource, Remaining);
}
