#pragma once

#include "CoreMinimal.h"

namespace RTSFormation
{
	/**
	 * Offsets for Count units in a roughly square grid centred on the origin, Spacing apart.
	 * The grid faces +X; a partial last row is centred.
	 */
	WORLDDIVIDE_API TArray<FVector> ComputeOffsets(int32 Count, float Spacing);

	/** ComputeOffsets rotated to face FacingYaw (degrees) and moved to Center. */
	WORLDDIVIDE_API TArray<FVector> ComputeSlots(const FVector& Center, float FacingYaw, int32 Count, float Spacing);

	/**
	 * Greedy nearest-slot assignment so units don't cross paths on the way. Returns, per unit, the index of its slot.
	 * Slots.Num() must be >= UnitLocations.Num().
	 */
	WORLDDIVIDE_API TArray<int32> AssignSlots(const TArray<FVector>& UnitLocations, const TArray<FVector>& Slots);
}
