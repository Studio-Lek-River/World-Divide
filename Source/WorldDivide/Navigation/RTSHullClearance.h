#pragma once

#include "CoreMinimal.h"

class ARecastNavMesh;
class UPrimitiveComponent;
class UWorld;
struct FNavPathPoint;
struct FRTSFootprint;
struct FRTSUnitShape;

/**
 * Keeps a unit's hull out of obstacles, for any hull size, on the navmesh of the unit's agent class (RTSNavAgents).
 *
 * The navmesh is the free ground shrunk by the agent's radius E, so for a point p the distance to the nearest obstacle is
 * E plus p's distance to the navmesh's edge when p lies on the navmesh, and E minus its distance to the navmesh when it lies
 * off it. The hull is the footprint's box (ARTSUnit::GetFootprint): the stadium along its long axis, sampled as discs, and
 * the box corners the stadium rounds off, sampled as points. Clearance is the smallest distance from any of them to an
 * obstacle: negative where the hull overlaps one. The class keeps a path wide enough for the hull's width; this keeps its
 * length, its corners and whatever width the class leaves over clear, so a new hull needs no navigation code.
 *
 * Accurate to the navmesh: a margin of half a cell covers its voxels and the simplification of its edges. Recast grows box
 * obstacles (buildings, null-area volumes) by the radius square rather than round, so beside such an obstacle's corner the
 * clearance errs on the safe side by up to (sqrt 2 - 1) times the agent radius; against its faces it is exact.
 *
 * Meshes are rasterized into voxels instead, which round each face out to the next cell edge: on Recast's far side, Unreal's
 * -X and -Y, by a whole cell when the face lies on one (#163, WorldDivide.Navigation.HullClearance.EdgeOffsets). The
 * navmesh alone cannot tell where in that cell the face is, so it errs safe by up to GetNavUncertainty. Within that of an
 * obstacle the collision decides: a path into one ends where the hull's box overlaps nothing solid (AdjustPath), and
 * URTSAvoidanceSubsystem only backs a hull that close out when its box overlaps something (IsSolidClear).
 *
 * A solid navigation leaves out does not cut the navmesh at all, so none of this sees it: FRTSSolidNeighbourhood finds such
 * solids round a vehicle's hull, and its gear and avoidance's pushes keep the hull out of them by their collision (#168).
 */
namespace RTSHullClearance
{
	/**
	 * The clearance of Footprint, its centre on the ground, on NavMesh. Walls are looked for out to the hull's reach plus
	 * ExtraReach; with none that near the result is a lower bound that grows with ExtraReach. OutAway, when given, is the
	 * ground direction that takes the closest sample away from its obstacle. FLT_MAX when there is no navmesh under it.
	 */
	WORLDDIVIDE_API float GetClearance(const ARecastNavMesh& NavMesh, const FRTSFootprint& Footprint, FVector2D* OutAway = nullptr,
		float ExtraReach = 0.f);

	/**
	 * Fits a path to a hull of Shape driving it, on NavMesh: a path ending at an obstacle ends where the hull, facing along
	 * its last stretch, stops short of it, and each corner moves away from the walls beside it until the hull clears them
	 * at every heading it turns through there, from the way in to the way out; in a passage too tight for that, as far as it
	 * can. Points[0] is where the unit stands and stays.
	 * Beyond, when given, is the spot the order aimed at, past the path's end inside an obstacle: a path whose end the hull
	 * clears goes on toward it, off the navmesh, until the hull meets the obstacle, so a hull shorter than the navmesh's
	 * erosion does not stop short of it; and then on along its last stretch, by up to GetNavUncertainty, as long as its box
	 * stays a small gap clear of anything solid, so it stops at the same gap from every face.
	 */
	WORLDDIVIDE_API void AdjustPath(const ARecastNavMesh& NavMesh, const FRTSUnitShape& Shape, TArray<FNavPathPoint>& Points,
		const FVector* Beyond = nullptr);

	/** How far NavMesh's edge lies from the obstacles around it: its agent radius rounded up to whole cells, as Recast rasterizes it. */
	WORLDDIVIDE_API float GetErosion(const ARecastNavMesh& NavMesh);

	/**
	 * How much further than GetClearance reads an obstacle may truly be: the cell a mesh's face is rounded out to, and the
	 * margin. A clearance no lower than minus this may still leave the hull clear of everything solid.
	 */
	WORLDDIVIDE_API float GetNavUncertainty(const ARecastNavMesh& NavMesh);

	/**
	 * True when the box of Footprint, grown by Skin, overlaps nothing solid between a step above GroundZ and Height above
	 * it, tilted to GroundNormal: one overlap against the world's static and dynamic geometry, which units are not.
	 */
	WORLDDIVIDE_API bool IsSolidClear(const UWorld& World, const FRTSFootprint& Footprint, float GroundZ, float Height, float Skin,
		const FVector& GroundNormal = FVector::UpVector);

	/** A hull's box as the collision sees it, in world space. */
	struct FHullBox
	{
		FVector Center = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector HalfExtent = FVector::ZeroVector;
	};

	/**
	 * The box of Footprint grown by Skin, from StepHeight above the ground under its centre, at GroundZ, up to Height above
	 * it, its floor parallel to the ground of GroundNormal: on a slope the hull's ends lie no closer to the ground than its
	 * middle, so the ground and a ramp the hull stands on never reach into it.
	 */
	WORLDDIVIDE_API FHullBox MakeHullBox(const FRTSFootprint& Footprint, float GroundZ, const FVector& GroundNormal, float StepHeight,
		float Height, float Skin);

	/** The footprint of Shape standing at Location, its long side along Heading. */
	WORLDDIVIDE_API FRTSFootprint MakeFootprint(const FRTSUnitShape& Shape, const FVector& Location, const FVector2D& Heading);
}

/** Where a unit's hull was last found clear and by how much, so it is only checked again once it may have closed that gap. */
struct FRTSHullClearanceCache
{
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D Axis = FVector2D(1.f, 0.f);
	/** The clearance found; negative when there is no check to reuse. */
	float Slack = -1.f;
	/** World seconds after which a newly placed building may stand in the gap. */
	double ExpireTime = 0.0;

	/** True while Footprint cannot have moved or turned into anything since the check. */
	WORLDDIVIDE_API bool IsClear(const FRTSFootprint& Footprint, double Now) const;
};

/**
 * The solids near a hull that the navmesh leaves out (#168): a wall or prop whose navigation is off stands on the navmesh
 * without cutting it, so GetClearance cannot see it. One overlap of a box round the hull, wide enough for any heading
 * within Margin of where it was made, finds what blocks a pawn there and is not navigation relevant; everything that is,
 * the ground, ramps, blocks and buildings, is the navmesh's. The hull's own box is checked against those few components
 * alone, and only while there are any: on open ground a moving hull pays one overlap every second or so.
 */
struct FRTSSolidNeighbourhood
{
	FVector Center = FVector::ZeroVector;
	/** How far the hull may drive from Center and still lie inside what was looked at; negative when nothing was. */
	float Margin = -1.f;
	/** World seconds after which a newly placed prop may stand there. */
	double ExpireTime = 0.0;
	TArray<TWeakObjectPtr<UPrimitiveComponent>, TInlineAllocator<4>> Solids;

	void Reset()
	{
		Margin = -1.f;
		Solids.Reset();
	}

	/** True while Footprint, at any heading, lies inside what was looked at. */
	WORLDDIVIDE_API bool IsCurrent(const FRTSFootprint& Footprint, double Now) const;

	/**
	 * Looks again round Footprint, standing at GroundZ with a hull Height high, for a hull that may drive up to InMargin
	 * before Now + Lifetime.
	 */
	WORLDDIVIDE_API void Update(const UWorld& World, const FRTSFootprint& Footprint, float GroundZ, float Height, float InMargin,
		double Now, double Lifetime);

	/**
	 * True when Box overlaps none of the solids, or goes no deeper into one than Current, the hull where it stands, already
	 * is: a hull a prop was dropped on may still drive out of it. Tolerance shrinks both boxes, so a hull stopped against a
	 * face may drive and turn along it.
	 */
	WORLDDIVIDE_API bool IsClear(const RTSHullClearance::FHullBox& Box, const RTSHullClearance::FHullBox& Current, float Tolerance) const;
};

/** What the solid neighbourhoods cost on this machine since the benchmark last read it, game thread only. */
struct FRTSSolidStats
{
	/** Broad overlaps made, and checks that reused a neighbourhood instead. */
	int32 Updates = 0;
	int32 Reuses = 0;
	/** Hull checks against a neighbourhood with solids in it, and the component queries they made. */
	int32 Checks = 0;
	int32 ComponentQueries = 0;
	double BroadSeconds = 0.0;
	double NarrowSeconds = 0.0;

	/** Counts a check that found its neighbourhood current. */
	WORLDDIVIDE_API static void AddReuse();

	/** The stats gathered since the last call, which starts them again. */
	WORLDDIVIDE_API static FRTSSolidStats Take();
};
