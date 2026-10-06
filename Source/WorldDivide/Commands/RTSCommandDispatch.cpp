#include "Commands/RTSCommandDispatch.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSFormation.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Engine/World.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

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

	ARTSBuilding* PlaceBuilding(UWorld& World, const URTSFactionData* Faction, int32 TeamId, const FRTSCommand& Command)
	{
		URTSBuildingData* Data = Cast<URTSBuildingData>(Command.EntityData);
		if (!Data || !Faction || !Faction->Buildings.Contains(Data))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d may not build %s"), TeamId, *GetNameSafe(Command.EntityData));
			return nullptr;
		}

		// Clients pick the spot, so the server snaps and checks it again.
		FVector Location;
		if (!RTSBuildPlacement::CanPlace(World, *Data, Command.TargetLocation, Location))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot place %s at %s"), TeamId, *Data->GetName(), *Location.ToString());
			return nullptr;
		}

		const FTransform Transform(Location);
		ARTSBuilding* Building = World.SpawnActorDeferred<ARTSBuilding>(
			ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Building)
		{
			Building->InitFromData(Data, TeamId);
			Building->FinishSpawning(Transform);
		}
		return Building;
	}

	ARTSBuilding* PurchaseBuilding(UWorld& World, ARTSPlayerState& Player, const FRTSCommand& Command)
	{
		const int32 Cost = Command.EntityData ? Command.EntityData->Cost : 0;
		if (Player.GetCredits() < Cost)
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot afford %s (%d of %d credits)"),
				Player.GetTeamId(), *GetNameSafe(Command.EntityData), Player.GetCredits(), Cost);
			return nullptr;
		}

		ARTSBuilding* Building = PlaceBuilding(World, Player.GetFaction(), Player.GetTeamId(), Command);
		if (Building)
		{
			Player.TrySpendCredits(Cost);
			Building->SetPaidCost(Cost);
		}
		return Building;
	}

	int32 CancelConstruction(ARTSPlayerState& Player, const FRTSCommand& Command)
	{
		ARTSBuilding* Building = Cast<ARTSBuilding>(Command.TargetActor);
		if (!IsValid(Building) || Building->GetTeamId() != Player.GetTeamId() || Building->IsConstructed()
			|| Building->GetHealthComponent()->IsDead())
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot cancel %s"), Player.GetTeamId(), *GetNameSafe(Command.TargetActor));
			return INDEX_NONE;
		}
		const int32 Refund = Building->GetCancelRefund();
		Player.AddCredits(Refund);
		Building->Destroy();
		return Refund;
	}
}
