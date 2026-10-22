#pragma once

#include "CoreMinimal.h"

class AActor;
class ARTSUnit;
class UWorld;
class URTSBuildingData;
class URTSEntityData;
class URTSUnitData;

/** Grid and validity rules for placing a building. The client's ghost and the server's re-check share them. */
namespace RTSBuildPlacement
{
	/** Edge length of one build grid cell in cm. */
	constexpr float GridSize = 100.f;

	/** Gap between the footprint's exit edge and a unit spawned at the exit, beyond the unit's longer half. */
	constexpr float ExitClearance = 200.f;

	/**
	 * The widest ground unit a building of Data sends out of its exit, produced or brought along, by the nav agent class it
	 * walks and then by its hull; null when it sends out none.
	 */
	WORLDDIVIDE_API const URTSUnitData* GetWidestExitUnit(const URTSBuildingData& Data);

	/** How far in front of the building's centre a unit of Unit spawns: half the footprint, the unit's longer half and ExitClearance. */
	WORLDDIVIDE_API float GetExitDistance(const URTSBuildingData& Data, const URTSUnitData& Unit);

	/**
	 * How far out from the exit edge the widest unit a building of Data sends out (GetWidestExitUnit) needs room: to where
	 * it spawns, and on for its own length (what CanUnitsLeave checks). Zero when it sends out no ground unit.
	 */
	WORLDDIVIDE_API float GetExitLaneLength(const URTSBuildingData& Data);

	/**
	 * How far to the side of the building's centre line, along its own Y, the exit lane runs: the garage's lane for a
	 * building with one (URTSBuildingData::GarageExitSocket, #192), else zero, the middle of the exit edge.
	 */
	WORLDDIVIDE_API float GetExitLaneOffset(const URTSBuildingData& Data);

	/**
	 * Whether the widest unit a building of Data sends out (GetWidestExitUnit) fits at its exit at Location, turned
	 * QuarterTurns, and can drive a hull's length straight on, on that unit's own navmesh. True for a building whose units
	 * all walk the narrowest class: anything placeable leaves them room.
	 */
	WORLDDIVIDE_API bool CanUnitsLeave(const UWorld& World, const URTSBuildingData& Data, const FVector& Location, int32 QuarterTurns);

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
	 * obstacle, its centre and corners must lie on the narrowest agent's navmesh, and the units it sends out must fit out of
	 * its exit (CanUnitsLeave). OutLocation is the snapped spot even when it is invalid.
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
