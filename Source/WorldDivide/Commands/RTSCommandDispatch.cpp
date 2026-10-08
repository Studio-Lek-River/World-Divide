#include "Commands/RTSCommandDispatch.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSFormation.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	TAutoConsoleVariable<bool> CVarDebugBuild(
		TEXT("rts.DebugBuild"), false,
		TEXT("Lets the 1-9 menu place the faction's buildings without a builder, and the server accept them."),
		ECVF_Cheat);
}

namespace RTSCommands
{
	bool IsDebugBuildEnabled()
	{
		return CVarDebugBuild.GetValueOnAnyThread();
	}

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

		const bool bFormation = Command.Type == ERTSCommandType::Move || Command.Type == ERTSCommandType::AttackMove;
		if (!bFormation || Units.Num() == 1)
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

	void Issue(ARTSPlayerState& Player, TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command)
	{
		UWorld* World = Player.GetWorld();
		if (!World)
		{
			return;
		}
		if (ARTSGameState::IsMatchOver(*World))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d's order comes after the match is over"), Player.GetTeamId());
			return;
		}
		if (Command.Type == ERTSCommandType::Build && Units.IsEmpty())
		{
			// The debug build menu: no builder, so the server places the building directly, if debug build is on.
			if (IsDebugBuildEnabled())
			{
				PurchaseBuilding(*World, Player, Command);
			}
			else
			{
				UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d may not build without a builder"), Player.GetTeamId());
			}
			return;
		}
		switch (Command.Type)
		{
		case ERTSCommandType::CancelConstruction:
			CancelConstruction(Player, Command);
			return;
		case ERTSCommandType::Produce:
			Produce(Player, Command);
			return;
		case ERTSCommandType::CancelProduction:
			CancelProduction(Player, Command);
			return;
		case ERTSCommandType::SetRallyPoint:
			SetRallyPoint(Player, Command);
			return;
		default:
			break;
		}
		Dispatch(FilterOwnedUnits(Units, Player.GetTeamId()), Command);
	}

	ARTSBuilding* PlaceBuilding(UWorld& World, const URTSFactionData* Faction, int32 TeamId, const FRTSCommand& Command)
	{
		URTSBuildingData* Data = Cast<URTSBuildingData>(Command.EntityData);
		if (!Data || !Faction || !Faction->Buildings.Contains(Data))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d may not build %s"), TeamId, *GetNameSafe(Command.EntityData));
			return nullptr;
		}
		// Checked again here: another builder's site may have taken the last one since this order was paid.
		if (RTSProduction::IsAtBuildLimit(World, TeamId, *Data))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d already owns %d of %s"), TeamId, Data->MaxCount, *Data->GetName());
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
		const URTSEntityData* Missing = Command.EntityData
			? RTSProduction::FindMissingPrerequisite(World, Player.GetTeamId(), *Command.EntityData) : nullptr;
		if (Missing)
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d needs %s for %s"), Player.GetTeamId(), *Missing->GetName(), *Command.EntityData->GetName());
			return nullptr;
		}
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

	bool Produce(ARTSPlayerState& Player, const FRTSCommand& Command)
	{
		const ARTSBuilding* Building = Cast<ARTSBuilding>(Command.TargetActor);
		return IsValid(Building) && Building->GetProductionComponent()->Enqueue(Player, Command.EntityData);
	}

	bool CancelProduction(ARTSPlayerState& Player, const FRTSCommand& Command)
	{
		const ARTSBuilding* Building = Cast<ARTSBuilding>(Command.TargetActor);
		return IsValid(Building) && Building->GetProductionComponent()->CancelLast(Player, Command.EntityData);
	}

	bool SetRallyPoint(ARTSPlayerState& Player, const FRTSCommand& Command)
	{
		const ARTSBuilding* Building = Cast<ARTSBuilding>(Command.TargetActor);
		if (!IsValid(Building) || Building->GetTeamId() != Player.GetTeamId() || !Building->GetProductionComponent()->CanProduce())
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot set a rally point for %s"), Player.GetTeamId(), *GetNameSafe(Command.TargetActor));
			return false;
		}
		Building->GetProductionComponent()->SetRallyPoint(Command.TargetLocation);
		return true;
	}
}
