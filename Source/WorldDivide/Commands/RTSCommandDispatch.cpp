#include "Commands/RTSCommandDispatch.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Buildings/RTSSuperweaponComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSFormation.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	TAutoConsoleVariable<bool> CVarDebugBuild(
		TEXT("rts.DebugBuild"), false,
		TEXT("Lets the 1-9 menu place the faction's buildings without a builder, and the server accept them."),
		ECVF_Cheat);

	/** The last FRTSCommand::OrderId Dispatch handed out. */
	int32 LastOrderId = 0;
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

		// One id for the whole group, so its refusal is told once however many frames its units' paths take (#174).
		FRTSCommand Order = Command;
		LastOrderId = LastOrderId == MAX_int32 ? 1 : LastOrderId + 1;
		Order.OrderId = LastOrderId;

		if (Command.Type == ERTSCommandType::ForceFire)
		{
			// Units that cannot fire at it keep their orders; the player hears of it only when none of them can. No spread:
			// everyone fires at the same target or spot.
			TArray<ARTSUnit*, TInlineAllocator<16>> Firing;
			for (ARTSUnit* Unit : Units)
			{
				if (Unit->CanForceFire(Order))
				{
					Firing.Add(Unit);
				}
			}
			if (Firing.IsEmpty())
			{
				const FVector Location = IsValid(Order.TargetActor) ? Order.TargetActor->GetActorLocation() : Order.TargetLocation;
				ARTSPlayerController::NotifyOrderRefused(Order.Issuer, Location, Order.OrderId);
			}
			for (ARTSUnit* Unit : Firing)
			{
				Unit->ExecuteCommand(Order);
			}
			return;
		}

		const bool bFormation = Command.Type == ERTSCommandType::Move || Command.Type == ERTSCommandType::AttackMove;
		if (!bFormation || Units.Num() == 1)
		{
			for (ARTSUnit* Unit : Units)
			{
				Unit->ExecuteCommand(Order);
			}
			return;
		}

		const TArray<FVector> Slots = PlanFormation(Units, Command.TargetLocation);
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			FRTSCommand UnitCommand = Order;
			UnitCommand.TargetLocation = Slots[Index];
			Units[Index]->ExecuteCommand(UnitCommand);
		}
	}

	TArray<FVector> PlanFormation(TConstArrayView<ARTSUnit*> Units, const FVector& Target)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSCommands::PlanFormation);

		FVector Centroid = FVector::ZeroVector;
		TArray<FRTSFormationMember> Members;
		Members.Reserve(Units.Num());
		for (const ARTSUnit* Unit : Units)
		{
			Members.Emplace(Unit->GetActorLocation(), Unit->GetShape());
			Centroid += Unit->GetActorLocation();
		}
		Centroid /= FMath::Max(Units.Num(), 1);

		// The formation faces the way the group travels.
		const FVector Heading = (Target - Centroid).GetSafeNormal2D();
		const float FacingYaw = Heading.IsNearlyZero() ? 0.f : Heading.Rotation().Yaw;
		return RTSFormation::Plan(Members, Target, FacingYaw);
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
		if (Command.Building)
		{
			BuildingOrder(Player, Command);
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
		case ERTSCommandType::LaunchSuperweapon:
			LaunchSuperweapon(Player, Command);
			return;
		default:
			break;
		}
		// Whatever the client put in the server-only fields, the order is this player's.
		FRTSCommand Issued = Command;
		Issued.Issuer = &Player;
		Dispatch(FilterOwnedUnits(Units, Player.GetTeamId()), Issued);
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
		const int32 QuarterTurns = Command.QuarterTurns % 4;
		if (!RTSBuildPlacement::CanPlace(World, *Data, Command.TargetLocation, Location, nullptr, QuarterTurns))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot place %s at %s"), TeamId, *Data->GetName(), *Location.ToString());
			return nullptr;
		}

		const FTransform Transform(FRotator(0.f, RTSBuildPlacement::GetYaw(QuarterTurns), 0.f), Location);
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
		// The debug ghost shows the builders' planned sites too, and those keep their spot.
		const URTSBuildingData* Data = Cast<URTSBuildingData>(Command.EntityData);
		if (Data && RTSBuildPlacement::OverlapsPlannedSite(World, Player.GetTeamId(), *Data, Command.TargetLocation, Command.QuarterTurns))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d already plans a building where %s would go"), Player.GetTeamId(), *Data->GetName());
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
		if (!IsValid(Building))
		{
			return false;
		}
		URTSProductionComponent* Production = Building->GetProductionComponent();
		return Command.QueueIndex == INDEX_NONE ? Production->CancelLast(Player, Command.EntityData)
			: Production->CancelAt(Player, Command.QueueIndex, Command.EntityData);
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

	bool BuildingOrder(ARTSPlayerState& Player, const FRTSCommand& Command)
	{
		ARTSBuilding* Building = Command.Building;
		bool bDone = false;
		if (IsValid(Building) && Building->GetTeamId() == Player.GetTeamId())
		{
			if (Command.Type == ERTSCommandType::ForceFire)
			{
				bDone = Building->ForceFire(Command.TargetActor, Command.TargetLocation);
			}
			else if (Command.Type == ERTSCommandType::Stop)
			{
				Building->StopFiring();
				bDone = true;
			}
		}
		if (!bDone)
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot order %s to %s"), Player.GetTeamId(), *GetNameSafe(Building),
				*UEnum::GetValueAsString(Command.Type));
			const FVector Location = IsValid(Command.TargetActor) ? Command.TargetActor->GetActorLocation() : Command.TargetLocation;
			ARTSPlayerController::NotifyOrderRefused(&Player, Location);
		}
		return bDone;
	}

	bool LaunchSuperweapon(ARTSPlayerState& Player, const FRTSCommand& Command)
	{
		const ARTSBuilding* Building = Cast<ARTSBuilding>(Command.TargetActor);
		if (!IsValid(Building) || Building->GetTeamId() != Player.GetTeamId() || !Building->GetSuperweaponComponent()->Launch(Command.TargetLocation))
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot launch %s"), Player.GetTeamId(), *GetNameSafe(Command.TargetActor));
			return false;
		}
		return true;
	}
}
