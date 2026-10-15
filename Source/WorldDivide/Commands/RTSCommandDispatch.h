#pragma once

#include "CoreMinimal.h"
#include "Commands/RTSCommand.h"

class ARTSBuilding;
class ARTSPlayerState;
class ARTSUnit;
class URTSFactionData;

namespace RTSCommands
{
	/** Drops null, duplicate and foreign units: a player may only order their own team. */
	WORLDDIVIDE_API TArray<ARTSUnit*> FilterOwnedUnits(TConstArrayView<ARTSUnit*> Units, int32 TeamId);

	/** Server only. A move or attack-move is spread over a formation around the target; other orders go to every unit unchanged. */
	WORLDDIVIDE_API void Dispatch(TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command);

	/** The slot Dispatch sends each of Units to on a move to Target, in the units' order: RTSFormation::Plan facing the way they travel. */
	WORLDDIVIDE_API TArray<FVector> PlanFormation(TConstArrayView<ARTSUnit*> Units, const FVector& Target);

	/**
	 * Server only. Executes Player's order for Units, the one path every order takes, from a player's controller or an AI
	 * opponent: refused once the match is over; a Build without units only in debug build; building orders checked against
	 * Player; unit orders dispatched to those of Units that are Player's.
	 */
	WORLDDIVIDE_API void Issue(ARTSPlayerState& Player, TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command);

	/**
	 * Server only. Spawns the building of a Build command for TeamId once the faction may build it, the team is below its
	 * MaxCount and the snapped spot passes placement. Returns the new building, or null when the command is rejected.
	 */
	WORLDDIVIDE_API ARTSBuilding* PlaceBuilding(UWorld& World, const URTSFactionData* Faction, int32 TeamId, const FRTSCommand& Command);

	/**
	 * A Build order without units is placed directly, as by the debug build menu. Off unless the cheat console variable
	 * rts.DebugBuild is set; otherwise only builder units place buildings.
	 */
	WORLDDIVIDE_API bool IsDebugBuildEnabled();

	/**
	 * Server only. PlaceBuilding for Player's faction and team, charged to Player: refused when they lack one of its
	 * prerequisites or cannot afford it.
	 */
	WORLDDIVIDE_API ARTSBuilding* PurchaseBuilding(UWorld& World, ARTSPlayerState& Player, const FRTSCommand& Command);

	/**
	 * Server only. Removes the command's TargetActor, one of Player's buildings still under construction, and refunds
	 * its ARTSBuilding::GetCancelRefund to Player. Returns the refund, or INDEX_NONE when the command is rejected.
	 */
	WORLDDIVIDE_API int32 CancelConstruction(ARTSPlayerState& Player, const FRTSCommand& Command);

	/**
	 * Server only. Production orders for the command's TargetActor, one of Player's buildings: Produce queues EntityData,
	 * CancelProduction takes it off the queue (at QueueIndex, or its last entry), SetRallyPoint moves the rally point to TargetLocation. Returns
	 * false when the command is rejected.
	 */
	WORLDDIVIDE_API bool Produce(ARTSPlayerState& Player, const FRTSCommand& Command);
	WORLDDIVIDE_API bool CancelProduction(ARTSPlayerState& Player, const FRTSCommand& Command);
	WORLDDIVIDE_API bool SetRallyPoint(ARTSPlayerState& Player, const FRTSCommand& Command);

	/**
	 * Server only. Launches the command's TargetActor, one of Player's superweapons, at TargetLocation once it is ready.
	 * Returns false when the command is rejected.
	 */
	WORLDDIVIDE_API bool LaunchSuperweapon(ARTSPlayerState& Player, const FRTSCommand& Command);
}
