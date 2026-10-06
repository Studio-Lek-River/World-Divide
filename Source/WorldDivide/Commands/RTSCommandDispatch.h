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

	/** Server only. A move is spread over a formation around the target; other orders go to every unit unchanged. */
	WORLDDIVIDE_API void Dispatch(TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command);

	/**
	 * Server only. Spawns the building of a Build command for TeamId once the faction may build it and the snapped spot
	 * passes placement. Returns the new building, or null when the command is rejected.
	 */
	WORLDDIVIDE_API ARTSBuilding* PlaceBuilding(UWorld& World, const URTSFactionData* Faction, int32 TeamId, const FRTSCommand& Command);

	/** Server only. PlaceBuilding for Player's faction and team, charged to Player: refused when they cannot afford it. */
	WORLDDIVIDE_API ARTSBuilding* PurchaseBuilding(UWorld& World, ARTSPlayerState& Player, const FRTSCommand& Command);

	/**
	 * Server only. Removes the command's TargetActor, one of Player's buildings still under construction, and refunds
	 * its ARTSBuilding::GetCancelRefund to Player. Returns the refund, or INDEX_NONE when the command is rejected.
	 */
	WORLDDIVIDE_API int32 CancelConstruction(ARTSPlayerState& Player, const FRTSCommand& Command);
}
