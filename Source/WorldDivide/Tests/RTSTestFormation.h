#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Commands/RTSCommandDispatch.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

namespace RTSTests
{
	/**
	 * How far from Target the units of a formation move end: the farthest slot RTSCommands::PlanFormation gives Units, plus that
	 * unit's own length, which avoidance and the hull clearance may push it off its slot by. Call it before the order, while the
	 * units stand where the formation is planned from.
	 */
	inline float GetFormationArrivalRadius(TConstArrayView<ARTSUnit*> Units, const FVector& Target)
	{
		const TArray<FVector> Slots = RTSCommands::PlanFormation(Units, Target);
		float Radius = 0.f;
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			const FRTSFootprint Footprint = Units[Index]->GetFootprint();
			Radius = FMath::Max(Radius, static_cast<float>(FVector::Dist2D(Slots[Index], Target)) + 2.f * (Footprint.HalfSegment + Footprint.Radius));
		}
		return Radius;
	}

	/** The ground the hulls of a formation move to Target cover, from above: every slot grown by its unit's half length. */
	inline FBox2D GetFormationBounds(TConstArrayView<ARTSUnit*> Units, const FVector& Target)
	{
		const TArray<FVector> Slots = RTSCommands::PlanFormation(Units, Target);
		FBox2D Bounds(ForceInit);
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			const FRTSFootprint Footprint = Units[Index]->GetFootprint();
			const FVector2D Reach(Footprint.HalfSegment + Footprint.Radius);
			Bounds += FVector2D(Slots[Index]) - Reach;
			Bounds += FVector2D(Slots[Index]) + Reach;
		}
		return Bounds;
	}
}

#endif
