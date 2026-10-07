#include "Buildings/RTSBuildPlacement.h"

#include "Data/RTSBuildingData.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

namespace
{
	/** How far a footprint point may lie from the navmesh. Covers the agent-radius margin around existing buildings. */
	constexpr float NavTolerance = 75.f;
	constexpr float NavHeightTolerance = 250.f;

	/** The overlap box starts this far above the ground so the ground itself never counts. */
	constexpr float GroundClearance = 20.f;
	constexpr float OverlapHeight = 200.f;
}

namespace RTSBuildPlacement
{
	FVector SnapToGrid(const FVector& Location, const FIntPoint& Footprint)
	{
		auto SnapAxis = [](double Value, int32 Cells)
		{
			return Cells % 2 == 0
				? FMath::RoundToDouble(Value / GridSize) * GridSize
				: FMath::FloorToDouble(Value / GridSize) * GridSize + GridSize * 0.5;
		};
		return FVector(SnapAxis(Location.X, Footprint.X), SnapAxis(Location.Y, Footprint.Y), Location.Z);
	}

	FVector GetFootprintExtent(const FIntPoint& Footprint)
	{
		return FVector(Footprint.X * GridSize * 0.5f, Footprint.Y * GridSize * 0.5f, 0.f);
	}

	bool CanPlace(const UWorld& World, const URTSBuildingData& Data, const FVector& RequestedLocation, FVector& OutLocation,
		TArray<AActor*>* OutBlockers)
	{
		if (OutBlockers)
		{
			OutBlockers->Reset();
		}
		OutLocation = SnapToGrid(RequestedLocation, Data.Footprint);

		const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!NavSystem)
		{
			return false;
		}

		// The requested height is not trusted: take it from the navmesh, then from the ground just below it.
		const FVector QueryExtent(NavTolerance, NavTolerance, NavHeightTolerance);
		FNavLocation NavPoint;
		if (!NavSystem->ProjectPointToNavigation(OutLocation, NavPoint, QueryExtent))
		{
			return false;
		}
		OutLocation.Z = NavPoint.Location.Z;

		FHitResult GroundHit;
		const FVector TraceStart = OutLocation + FVector(0.f, 0.f, GroundClearance);
		if (World.LineTraceSingleByObjectType(GroundHit, TraceStart, TraceStart - FVector(0.f, 0.f, NavHeightTolerance), ECC_WorldStatic))
		{
			OutLocation.Z = GroundHit.Location.Z;
		}

		const FVector Extent = GetFootprintExtent(Data.Footprint);
		for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(1.f, 1.f) })
		{
			const FVector Point = OutLocation + FVector(Corner.X * Extent.X, Corner.Y * Extent.Y, 0.f);
			if (!NavSystem->ProjectPointToNavigation(Point, NavPoint, QueryExtent))
			{
				return false;
			}
		}

		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		// Shrunk by a centimetre so buildings may stand flush against each other.
		const FVector BoxExtent(Extent.X - 1.f, Extent.Y - 1.f, OverlapHeight * 0.5f);
		const FVector BoxCenter = OutLocation + FVector(0.f, 0.f, GroundClearance + BoxExtent.Z);
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSBuildPlacement));
		if (!OutBlockers)
		{
			return !World.OverlapAnyTestByObjectType(BoxCenter, FQuat::Identity, Objects, FCollisionShape::MakeBox(BoxExtent), Params);
		}

		TArray<FOverlapResult> Overlaps;
		World.OverlapMultiByObjectType(Overlaps, BoxCenter, FQuat::Identity, Objects, FCollisionShape::MakeBox(BoxExtent), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (AActor* Actor = Overlap.GetActor())
			{
				OutBlockers->AddUnique(Actor);
			}
		}
		return Overlaps.IsEmpty();
	}

	void GetMeshPlacement(const URTSEntityData& Data, float& OutBaseOffset, float& OutHeight)
	{
		OutBaseOffset = 0.f;
		OutHeight = 0.f;
		if (Data.Mesh)
		{
			const FBoxSphereBounds Bounds = Data.Mesh->GetBounds();
			OutBaseOffset = (Bounds.BoxExtent.Z - Bounds.Origin.Z) * Data.MeshScale.Z;
			OutHeight = Bounds.BoxExtent.Z * 2.f * Data.MeshScale.Z;
		}
	}
}
