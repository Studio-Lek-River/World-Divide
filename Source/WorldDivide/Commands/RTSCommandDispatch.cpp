#include "Commands/RTSCommandDispatch.h"

#include "Commands/RTSFormation.h"
#include "Units/RTSUnit.h"

namespace RTSCommands
{
	TArray<ARTSUnit*> FilterOwnedUnits(TConstArrayView<ARTSUnit*> Units, int32 TeamId)
	{
		TArray<ARTSUnit*> Owned;
		Owned.Reserve(Units.Num());
		for (ARTSUnit* Unit : Units)
		{
			if (IsValid(Unit) && Unit->GetTeamId() == TeamId)
			{
				Owned.AddUnique(Unit);
			}
		}
		return Owned;
	}

	void Dispatch(TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command)
	{
		if (Units.IsEmpty())
		{
			return;
		}

		if (Command.Type != ERTSCommandType::Move || Units.Num() == 1)
		{
			for (ARTSUnit* Unit : Units)
			{
				Unit->ExecuteCommand(Command);
			}
			return;
		}

		FVector Centroid = FVector::ZeroVector;
		float Spacing = 0.f;
		TArray<FVector> Locations;
		Locations.Reserve(Units.Num());
		for (const ARTSUnit* Unit : Units)
		{
			Locations.Add(Unit->GetActorLocation());
			Centroid += Unit->GetActorLocation();
			Spacing = FMath::Max(Spacing, Unit->GetSelectionRadius() * 2.5f);
		}
		Centroid /= Units.Num();

		const FVector Heading = (Command.TargetLocation - Centroid).GetSafeNormal2D();
		const float FacingYaw = Heading.IsNearlyZero() ? 0.f : Heading.Rotation().Yaw;

		const TArray<FVector> Slots = RTSFormation::ComputeSlots(Command.TargetLocation, FacingYaw, Units.Num(), Spacing);
		const TArray<int32> Assignment = RTSFormation::AssignSlots(Locations, Slots);

		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			FRTSCommand UnitCommand = Command;
			UnitCommand.TargetLocation = Slots[Assignment[Index]];
			Units[Index]->ExecuteCommand(UnitCommand);
		}
	}
}
