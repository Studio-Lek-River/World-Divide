#pragma once

#include "CoreMinimal.h"

class URTSUnitData;

/**
 * A unit's size as its data gives it, in the actor's frame, so it is known before the unit spawns. The hull is the
 * collision box and its length and width are the footprint; the selection bounds add the turret for the brackets.
 * SelectionRadius sizes it only for data without a mesh: it is the interaction radius (docking, build reach, holding short).
 */
struct WORLDDIVIDE_API FRTSUnitShape
{
	/** GetHullBox times MeshScale, centred on the actor origin, so its bottom is the ground under the unit. */
	FBox Hull = FBox(FVector(-60.f), FVector(60.f));

	/** The hull plus the turret and what rides on it, without the gun; its bottom is the hull's. */
	FBox Selection = FBox(FVector(-60.f), FVector(60.f));

	/** Where the mesh sits relative to the actor so that its hull fills Hull. */
	FVector MeshOffset = FVector::ZeroVector;

	float GetHalfLength() const { return Hull.GetExtent().X; }
	float GetHalfWidth() const { return Hull.GetExtent().Y; }
};

/**
 * A ground footprint in world space: a stadium, a segment along the unit's longer axis swept by half its shorter side.
 * Infantry, as long as wide, is a circle.
 */
struct WORLDDIVIDE_API FRTSFootprint
{
	FVector Center = FVector::ZeroVector;

	/** Unit vector along the segment, in the ground plane. */
	FVector Axis = FVector::ForwardVector;

	/** Half the segment's length; zero for a circle. */
	float HalfSegment = 0.f;

	float Radius = 0.f;

	/** The point of the segment closest to From, in the ground plane. */
	FVector GetClosestSegmentPoint(const FVector& From) const;

	/** The edge point facing From, at Center's height. */
	FVector GetClosestEdgePoint(const FVector& From) const;
};

namespace RTSUnitShape
{
	/** The hull mesh's box, in its own space, grown by the parts on the hull: running gear reaches the ground below it. */
	FBox GetHullBox(const URTSUnitData& Data);

	/** Without a mesh the hull is a cube of SelectionRadius half extent, as the old capsule was. */
	WORLDDIVIDE_API FRTSUnitShape FromData(const URTSUnitData& Data);

	/** The footprint of a unit of Shape standing at Transform. */
	WORLDDIVIDE_API FRTSFootprint MakeFootprint(const FRTSUnitShape& Shape, const FTransform& Transform);

	/** The closest points of segments A0-A1 and B0-B1, in the ground plane. */
	WORLDDIVIDE_API void GetClosestSegmentPoints(const FVector2D& A0, const FVector2D& A1, const FVector2D& B0, const FVector2D& B1,
		FVector2D& OutA, FVector2D& OutB);

	/** Ground distance between the edges of two footprints; negative where they overlap. */
	WORLDDIVIDE_API float GetGap(const FRTSFootprint& A, const FRTSFootprint& B);
}
