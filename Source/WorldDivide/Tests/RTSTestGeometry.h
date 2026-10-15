#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Units/RTSUnitShape.h"

namespace RTSTests
{
	/** The hull box of Footprint on the ground: the stadium's box, corners in order around it. */
	inline TArray<FVector2D, TFixedAllocator<4>> GetHullCorners(const FRTSFootprint& Footprint)
	{
		const FVector2D Center(Footprint.Center);
		const FVector2D Along = FVector2D(Footprint.Axis) * (Footprint.HalfSegment + Footprint.Radius);
		const FVector2D Across = FVector2D(-Footprint.Axis.Y, Footprint.Axis.X) * Footprint.Radius;
		return { Center + Along + Across, Center - Along + Across, Center - Along - Across, Center + Along - Across };
	}

	/**
	 * Ground distance between the hull box of Footprint and Box, an obstacle's box seen from above: the gap between them, or
	 * minus how deep they overlap along the axis that separates them soonest.
	 */
	inline float GetHullGap(const FRTSFootprint& Footprint, const FBox2D& Box)
	{
		const TArray<FVector2D, TFixedAllocator<4>> Hull = GetHullCorners(Footprint);
		const FVector2D Obstacle[4] = { Box.Min, FVector2D(Box.Max.X, Box.Min.Y), Box.Max, FVector2D(Box.Min.X, Box.Max.Y) };
		const FVector2D Axes[4] = { FVector2D(Footprint.Axis).GetSafeNormal(), FVector2D(-Footprint.Axis.Y, Footprint.Axis.X).GetSafeNormal(),
			FVector2D(1.f, 0.f), FVector2D(0.f, 1.f) };

		float Overlap = FLT_MAX;
		for (const FVector2D& Axis : Axes)
		{
			float HullMin = FLT_MAX, HullMax = -FLT_MAX, BoxMin = FLT_MAX, BoxMax = -FLT_MAX;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				HullMin = FMath::Min(HullMin, FVector2D::DotProduct(Hull[Index], Axis));
				HullMax = FMath::Max(HullMax, FVector2D::DotProduct(Hull[Index], Axis));
				BoxMin = FMath::Min(BoxMin, FVector2D::DotProduct(Obstacle[Index], Axis));
				BoxMax = FMath::Max(BoxMax, FVector2D::DotProduct(Obstacle[Index], Axis));
			}
			Overlap = FMath::Min(Overlap, FMath::Min(HullMax, BoxMax) - FMath::Max(HullMin, BoxMin));
		}
		if (Overlap > 0.f)
		{
			return -Overlap;
		}

		// Apart: the closest pair of edges.
		float Gap = FLT_MAX;
		for (int32 A = 0; A < 4; ++A)
		{
			for (int32 B = 0; B < 4; ++B)
			{
				FVector2D OnHull;
				FVector2D OnBox;
				RTSUnitShape::GetClosestSegmentPoints(Hull[A], Hull[(A + 1) % 4], Obstacle[B], Obstacle[(B + 1) % 4], OnHull, OnBox);
				Gap = FMath::Min(Gap, FVector2D::Distance(OnHull, OnBox));
			}
		}
		return Gap;
	}
}

#endif
