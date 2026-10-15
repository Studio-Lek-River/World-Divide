#include "Commands/RTSFormation.h"

#include "Units/RTSUnitShape.h"

FRTSFormationMember::FRTSFormationMember(const FVector& InLocation, const FRTSUnitShape& Shape)
	: Location(InLocation)
	, Size(2.f * Shape.GetHalfLength(), 2.f * Shape.GetHalfWidth())
{
}

namespace
{
	/** A size class may hold units down to this share of its longest row pitch; shorter ones start the next class. */
	constexpr float SizeClassRatio = 0.75f;

	float GetColumnPitch(const FRTSFormationMember& Member)
	{
		return Member.Size.Y + RTSFormation::GetSideGap(Member.Size.Y);
	}

	float GetRowPitch(const FRTSFormationMember& Member)
	{
		return Member.Size.X + RTSFormation::GetDepthGap(Member.Size.X);
	}
}

namespace RTSFormation
{
	float GetSideGap(float Width)
	{
		return FMath::Clamp(Width, MinGap, MaxSideGap);
	}

	float GetDepthGap(float Length)
	{
		return FMath::Clamp(Length, MinGap, MaxDepthGap);
	}

	TArray<FVector> Plan(TConstArrayView<FRTSFormationMember> Members, const FVector& Target, float FacingYaw)
	{
		const int32 Count = Members.Num();
		TArray<FVector> Slots;
		if (Count == 0)
		{
			return Slots;
		}
		Slots.SetNum(Count);

		// Ahead (along the facing) and to the right (across it) of each member.
		const FRotator Facing(0.f, FacingYaw, 0.f);
		const FVector Forward = Facing.Vector();
		const FVector Right = Facing.RotateVector(FVector::RightVector);
		TArray<FVector2D> Projected;
		Projected.Reserve(Count);
		for (const FRTSFormationMember& Member : Members)
		{
			Projected.Emplace(FVector::DotProduct(Member.Location, Forward), FVector::DotProduct(Member.Location, Right));
		}
		// Every sort breaks its ties down to the index, so equal inputs give equal slots.
		auto FrontToBack = [&Projected](int32 A, int32 B)
		{
			if (Projected[A].X != Projected[B].X)
			{
				return Projected[A].X > Projected[B].X;
			}
			return Projected[A].Y != Projected[B].Y ? Projected[A].Y < Projected[B].Y : A < B;
		};
		auto LeftToRight = [&Projected](int32 A, int32 B)
		{
			return Projected[A].Y != Projected[B].Y ? Projected[A].Y < Projected[B].Y : A < B;
		};

		// Size classes, so a row of infantry is not as deep as the tank beside it: members sorted by row pitch, longest first,
		// and cut where the pitch falls below SizeClassRatio of the class's longest.
		TArray<int32> BySize;
		BySize.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			BySize.Add(Index);
		}
		BySize.Sort([&Members, &FrontToBack](int32 A, int32 B)
		{
			const float PitchA = GetRowPitch(Members[A]);
			const float PitchB = GetRowPitch(Members[B]);
			return PitchA != PitchB ? PitchA > PitchB : FrontToBack(A, B);
		});
		struct FSizeClass
		{
			TArrayView<int32> Members;
			double Front = 0.0;
			float ColumnPitch = 0.f;
		};
		TArray<FSizeClass> Classes;
		for (int32 Start = 0; Start < Count;)
		{
			const float Longest = GetRowPitch(Members[BySize[Start]]);
			int32 End = Start + 1;
			while (End < Count && GetRowPitch(Members[BySize[End]]) >= Longest * SizeClassRatio)
			{
				++End;
			}
			FSizeClass& Class = Classes.AddDefaulted_GetRef();
			Class.Members = TArrayView<int32>(BySize).Slice(Start, End - Start);
			for (const int32 Index : Class.Members)
			{
				Class.Front += Projected[Index].X / Class.Members.Num();
				Class.ColumnPitch += GetColumnPitch(Members[Index]) / Class.Members.Num();
			}
			Start = End;
		}
		// The class that stands furthest ahead on average goes in front.
		Classes.StableSort([](const FSizeClass& A, const FSizeClass& B) { return A.Front > B.Front; });

		// Roughly square on the ground: every class as wide as the square of the whole group's area.
		double Area = 0.0;
		for (const FRTSFormationMember& Member : Members)
		{
			Area += static_cast<double>(GetColumnPitch(Member)) * GetRowPitch(Member);
		}
		const double Width = FMath::Sqrt(Area);

		// Within a class, rows front to back, each left to right, laid out by the members' own widths; rows go back by depth.
		TArray<FVector2D> Offsets;
		Offsets.SetNum(Count);
		float RowX = 0.f;
		float FrontEdge = 0.f;
		float BackEdge = 0.f;
		float PreviousLength = -1.f;
		for (FSizeClass& Class : Classes)
		{
			Class.Members.Sort(FrontToBack);
			const int32 Columns = FMath::Clamp(FMath::CeilToInt32(Width / FMath::Max(Class.ColumnPitch, 1.f)), 1, Class.Members.Num());
			for (int32 RowStart = 0; RowStart < Class.Members.Num(); RowStart += Columns)
			{
				TArrayView<int32> Row = Class.Members.Slice(RowStart, FMath::Min(Columns, Class.Members.Num() - RowStart));
				Row.Sort(LeftToRight);

				float Length = 0.f;
				float RowWidth = 0.f;
				for (int32 Column = 0; Column < Row.Num(); ++Column)
				{
					const FVector2D& Size = Members[Row[Column]].Size;
					Length = FMath::Max(Length, static_cast<float>(Size.X));
					RowWidth += Size.Y;
					if (Column > 0)
					{
						RowWidth += FMath::Max(GetSideGap(Members[Row[Column - 1]].Size.Y), GetSideGap(Size.Y));
					}
				}

				if (PreviousLength < 0.f)
				{
					FrontEdge = Length * 0.5f;
				}
				else
				{
					RowX -= (PreviousLength + Length) * 0.5f + FMath::Max(GetDepthGap(PreviousLength), GetDepthGap(Length));
				}
				BackEdge = RowX - Length * 0.5f;
				PreviousLength = Length;

				float Y = -RowWidth * 0.5f;
				for (int32 Column = 0; Column < Row.Num(); ++Column)
				{
					const float MemberWidth = Members[Row[Column]].Size.Y;
					if (Column > 0)
					{
						Y += FMath::Max(GetSideGap(Members[Row[Column - 1]].Size.Y), GetSideGap(MemberWidth));
					}
					Offsets[Row[Column]] = FVector2D(RowX, Y + MemberWidth * 0.5f);
					Y += MemberWidth;
				}
			}
		}

		// Centred on the target in depth as well, so the group lands around the clicked point rather than behind it.
		const float Shift = (FrontEdge + BackEdge) * 0.5f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Slots[Index] = Target + Facing.RotateVector(FVector(Offsets[Index].X - Shift, Offsets[Index].Y, 0.f));
		}
		return Slots;
	}
}
