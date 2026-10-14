#pragma once

#include "CoreMinimal.h"
#include "RTSCommand.generated.h"

class URTSEntityData;

UENUM(BlueprintType)
enum class ERTSCommandType : uint8
{
	None,
	Move,
	Stop,
	/** Close in on TargetActor and fire until it dies. */
	Attack,
	/**
	 * Builders walk to TargetLocation, place EntityData (a building) there and construct it while they stay; when TargetActor
	 * is an own construction site they resume that instead. Units that cannot build move up to a named site. Without units the
	 * server places the building directly, but only in debug build (RTSCommands::IsDebugBuildEnabled).
	 */
	Build,
	/**
	 * Haul supplies from TargetActor (a supply source), or from the nearest reachable one when it is null, to the nearest
	 * reachable own depot until ordered otherwise or no source is left in reach. Units that cannot harvest move to the source
	 * instead.
	 */
	Harvest,
	/** Cancel TargetActor, an own building under construction, refunding the unbuilt share of what was paid for it. */
	CancelConstruction,
	/** Queue EntityData, a unit, at TargetActor, an own production building, paying its cost. */
	Produce,
	/**
	 * Take EntityData off TargetActor's production queue, refunding its cost: the entry at QueueIndex when it still holds
	 * EntityData, otherwise nothing; the last queued one when QueueIndex is INDEX_NONE.
	 */
	CancelProduction,
	/** Send units TargetActor produces to TargetLocation; harvesters then start harvesting from there. */
	SetRallyPoint,
	/** Move to TargetLocation, stopping to fight enemies that come into range and moving on once none is left. */
	AttackMove,
	/** Launch TargetActor, an own superweapon that is ready, at TargetLocation. */
	LaunchSuperweapon,
};

/**
 * Every player order is one of these. Orders travel client -> server through ARTSPlayerController::Server_IssueCommand
 * and only the server executes them, so singleplayer and multiplayer share one code path.
 */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	ERTSCommandType Type = ERTSCommandType::None;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	TObjectPtr<AActor> TargetActor = nullptr;

	/** What to build or produce. */
	UPROPERTY(BlueprintReadWrite, Category = "Command")
	TObjectPtr<URTSEntityData> EntityData = nullptr;

	/** Append to the unit's order queue instead of replacing it (Shift). */
	UPROPERTY(BlueprintReadWrite, Category = "Command")
	bool bQueue = false;

	/** Build: 90-degree turns of the new building, 0 to 3 (RTSBuildPlacement::GetRotatedFootprint). */
	UPROPERTY(BlueprintReadWrite, Category = "Command")
	uint8 QuarterTurns = 0;

	/** CancelProduction: the queue entry to cancel; INDEX_NONE takes the last queued EntityData. */
	UPROPERTY(BlueprintReadWrite, Category = "Command")
	int32 QueueIndex = INDEX_NONE;

	/** Server only, never sent by clients: credits already taken for this order, refunded if it ends before its building is placed. */
	UPROPERTY(NotReplicated)
	int32 PaidCost = 0;
};
