#pragma once

#include "CoreMinimal.h"

class AActor;
class ARTSUnit;
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
	 * The footprint a building covers in the world after QuarterTurns 90-degree turns: X and Y swap on odd turns. Buildings
	 * only turn in quarter turns, so a turned footprint stays axis aligned.
	 */
	WORLDDIVIDE_API FIntPoint GetRotatedFootprint(const FIntPoint& Footprint, int32 QuarterTurns);

	/** Yaw in degrees of a building turned QuarterTurns times. */
	WORLDDIVIDE_API float GetYaw(int32 QuarterTurns);

	/** QuarterTurns of an actor's yaw, 0 to 3. */
	WORLDDIVIDE_API int32 GetQuarterTurns(float Yaw);

	/**
	 * Snaps RequestedLocation, puts it on the ground and checks the spot: the footprint must not overlap a unit, building or
	 * obstacle, and its centre and corners must lie on the navmesh. OutLocation is the snapped spot even when it is invalid.
	 * OutBlockers, when given, receives every actor overlapping the footprint; it stays empty when the navmesh rules the spot out.
	 * QuarterTurns turns the footprint (GetRotatedFootprint).
	 */
	WORLDDIVIDE_API bool CanPlace(const UWorld& World, const URTSBuildingData& Data, const FVector& RequestedLocation, FVector& OutLocation,
		TArray<AActor*>* OutBlockers = nullptr, int32 QuarterTurns = 0);

	/**
	 * CanPlace for a builder of TeamId: units of that team on the footprint do not count, since the builder sends them off
	 * when it arrives. Enemy units, buildings and obstacles still rule the spot out.
	 */
	WORLDDIVIDE_API bool CanBuilderPlace(const UWorld& World, const URTSBuildingData& Data, const FVector& RequestedLocation,
		FVector& OutLocation, int32 TeamId, int32 QuarterTurns = 0);

	/**
	 * The footprint at RequestedLocation, snapped, overlaps a building an own builder of TeamId was ordered to place and has
	 * not placed yet (URTSBuilderComponent::GetPlannedSites), other than IgnoreBuilder's. Touching edges do not count.
	 */
	WORLDDIVIDE_API bool OverlapsPlannedSite(const UWorld& World, int32 TeamId, const URTSBuildingData& Data, const FVector& RequestedLocation,
		int32 QuarterTurns, const ARTSUnit* IgnoreBuilder = nullptr);

	/** Relative Z that stands the data's mesh on the actor origin, and the mesh's scaled height. */
	WORLDDIVIDE_API void GetMeshPlacement(const URTSEntityData& Data, float& OutBaseOffset, float& OutHeight);
}
