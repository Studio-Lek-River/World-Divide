#include "Commands/RTSFormation.h"

namespace RTSFormation
{
	TArray<FVector> ComputeOffsets(int32 Count, float Spacing)
	{
		TArray<FVector> Offsets;
		if (Count <= 0)
		{
			return Offsets;
		}
		Offsets.Reserve(Count);

		// Columns run across the facing direction (Y), rows go back along -X.
		const int32 Columns = FMath::CeilToInt32(FMath::Sqrt(static_cast<float>(Count)));
		const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Row = Index / Columns;
			const int32 Column = Index % Columns;
			const int32 ColumnsInRow = FMath::Min(Columns, Count - Row * Columns);

			const float Y = (Column - (ColumnsInRow - 1) * 0.5f) * Spacing;
			const float X = ((Rows - 1) * 0.5f - Row) * Spacing;
			Offsets.Add(FVector(X, Y, 0.f));
		}
		return Offsets;
	}

	TArray<FVector> ComputeSlots(const FVector& Center, float FacingYaw, int32 Count, float Spacing)
	{
		const FRotator Facing(0.f, FacingYaw, 0.f);
		TArray<FVector> Slots = ComputeOffsets(Count, Spacing);
		for (FVector& Slot : Slots)
		{
			Slot = Center + Facing.RotateVector(Slot);
		}
		return Slots;
	}

	TArray<int32> AssignSlots(const TArray<FVector>& UnitLocations, const TArray<FVector>& Slots)
	{
		check(Slots.Num() >= UnitLocations.Num());

		TArray<int32> Assignment;
		Assignment.Init(INDEX_NONE, UnitLocations.Num());
		TArray<bool> Taken;
		Taken.Init(false, Slots.Num());

		// Repeatedly take the globally closest free (unit, slot) pair. O(n^3) but n is a selection, not the whole army.
		for (int32 Round = 0; Round < UnitLocations.Num(); ++Round)
		{
			int32 BestUnit = INDEX_NONE;
			int32 BestSlot = INDEX_NONE;
			double BestDistSq = TNumericLimits<double>::Max();

			for (int32 Unit = 0; Unit < UnitLocations.Num(); ++Unit)
			{
				if (Assignment[Unit] != INDEX_NONE)
				{
					continue;
				}
				for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
				{
					if (Taken[Slot])
					{
						continue;
					}
					const double DistSq = FVector::DistSquared2D(UnitLocations[Unit], Slots[Slot]);
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestUnit = Unit;
						BestSlot = Slot;
					}
				}
			}

			Assignment[BestUnit] = BestSlot;
			Taken[BestSlot] = true;
		}
		return Assignment;
	}
}
