#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
class URTSBuildingData;
class URTSEntityData;

/** Grid and validity rules for placing a building. The client's ghost and the server's re-check share them. */
namespace RTSBuildPlacement
{
	/** Edge length of one build grid cell in cm. */
	constexpr float GridSize = 100.f;

	/**
	 * Moves Location so the footprint's edges lie on grid lines: even sizes centre on a grid line, odd sizes on a cell
	 * centre. Z is kept.
	 */
	WORLDDIVIDE_API FVector SnapToGrid(const FVector& Location, const FIntPoint& Footprint);

	/** Half the footprint in cm, with zero height. */
	WORLDDIVIDE_API FVector GetFootprintExtent(const FIntPoint& Footprint);

	/**
	 * Snaps RequestedLocation, puts it on the ground and checks the spot: the footprint must not overlap a unit, building or
	 * obstacle, and its centre and corners must lie on the navmesh. OutLocation is the snapped spot even when it is invalid.
	 * OutBlockers, when given, receives every actor overlapping the footprint; it stays empty when the navmesh rules the spot out.
	 */
	WORLDDIVIDE_API bool CanPlace(const UWorld& World, const URTSBuildingData& Data, const FVector& RequestedLocation, FVector& OutLocation,
		TArray<AActor*>* OutBlockers = nullptr);

	/** Relative Z that stands the data's mesh on the actor origin, and the mesh's scaled height. */
	WORLDDIVIDE_API void GetMeshPlacement(const URTSEntityData& Data, float& OutBaseOffset, float& OutHeight);
}
