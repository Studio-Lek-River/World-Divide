#pragma once

#include "CoreMinimal.h"

struct FRTSUnitShape;

/** One unit to place in a formation: where it stands now and the size of its hull. */
struct WORLDDIVIDE_API FRTSFormationMember
{
	FVector Location = FVector::ZeroVector;

	/** Hull length (along the unit's facing) and width, in cm. */
	FVector2D Size = FVector2D::ZeroVector;

	FRTSFormationMember() = default;
	FRTSFormationMember(const FVector& InLocation, const FRTSUnitShape& Shape);
};

/**
 * Formations sized by the units' footprints. Rows run across the facing direction, front to back. Within a row each unit takes
 * its own width plus a side gap; each row is as deep as its longest unit plus a depth gap. The gaps grow with the unit, from
 * MinGap up to the approved real-scale clearances (2.5 m beside a vehicle, 3 m behind it), so infantry stands close and a
 * mixed group does not spread every soldier to a tank's spacing.
 */
namespace RTSFormation
{
	constexpr float MinGap = 75.f;
	constexpr float MaxSideGap = 250.f;
	constexpr float MaxDepthGap = 300.f;

	/** Clearance beside a unit Width wide, and behind one Length long. */
	WORLDDIVIDE_API float GetSideGap(float Width);
	WORLDDIVIDE_API float GetDepthGap(float Length);

	/**
	 * A slot per member, in the members' order, around Target facing FacingYaw (degrees), roughly square on the ground.
	 * Members of about the same length form a size class with rows of their own, so infantry rows are not spaced like the
	 * tanks'; the class standing furthest ahead goes in front. Within a class the members ahead along the facing take the
	 * front rows and keep their left-to-right order within a row, so units do not cross on the way. Sorts only, so
	 * O(n log n); equal inputs give equal slots.
	 */
	WORLDDIVIDE_API TArray<FVector> Plan(TConstArrayView<FRTSFormationMember> Members, const FVector& Target, float FacingYaw);
}
