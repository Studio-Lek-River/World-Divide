#pragma once

#include "CoreMinimal.h"

class AActor;
class ARTSUnit;
class UWorld;
class URTSBuildingData;
class URTSEntityData;
class URTSUnitData;

/**
 * A building's ground footprint in world space: a rectangle about Center, turned with the building. Placed buildings turn
 * in quarter turns, the starting headquarters on a diagonal (#426), so nothing may assume it is axis aligned.
 */
struct WORLDDIVIDE_API FRTSBuildingFootprint
{
	FVector Center = FVector::ZeroVector;

	/** Unit vector along the building's own X, its exit side, in the ground plane. */
	FVector Axis = FVector::ForwardVector;

	/** Half the footprint along the building's own X and Y, in cm. */
	FVector2D Extent = FVector2D::ZeroVector;

	/** Unit vector along the building's own Y, as AActor::GetActorRightVector. */
	FVector GetSide() const { return FVector(-Axis.Y, Axis.X, 0.f); }

	/** World to the building's own frame, in the ground plane. */
	FVector2D ToLocal(const FVector& World) const;

	/** The building's own frame to world, at Center's height. */
	FVector ToWorld(const FVector2D& Local) const;

	/** From clamped to the rectangle, at Center's height. */
	FVector GetClosestPoint(const FVector& From) const;

	/** The four ground corners, at Center's height. */
	void GetCorners(FVector (&OutCorners)[4]) const;

	/** The axis-aligned box round the rectangle. */
	FBox2D GetBounds() const;

	/** The two rectangles are at least Gap apart along one of their sides' directions. */
	bool IsApart(const FRTSBuildingFootprint& Other, float Gap) const;
};

/** Grid and validity rules for placing a building. The client's ghost and the server's re-check share them. */
namespace RTSBuildPlacement
{
	/** Edge length of one build grid cell in cm. */
	constexpr float GridSize = 100.f;

	/** Yaw in degrees every starting headquarters stands at: its garage faces southwest, on a true diagonal (#426). */
	constexpr float HeadquartersYaw = 225.f;

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
	 * The footprint a building covers in the world after QuarterTurns 90-degree turns: X and Y swap on odd turns. Placed
	 * buildings only turn in quarter turns, so a turned footprint stays axis aligned; MakeFootprint takes any yaw.
	 */
	WORLDDIVIDE_API FIntPoint GetRotatedFootprint(const FIntPoint& Footprint, int32 QuarterTurns);

	/** Yaw in degrees of a building turned QuarterTurns times. */
	WORLDDIVIDE_API float GetYaw(int32 QuarterTurns);

	/** QuarterTurns of an actor's yaw, 0 to 3. */
	WORLDDIVIDE_API int32 GetQuarterTurns(float Yaw);

	/** The ground footprint of a building of the data's Footprint, unturned, standing at Location turned to Yaw degrees. */
	WORLDDIVIDE_API FRTSBuildingFootprint MakeFootprint(const FIntPoint& Footprint, const FVector& Location, float Yaw);

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
