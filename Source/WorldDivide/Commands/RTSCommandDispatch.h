#pragma once

#include "CoreMinimal.h"
#include "Commands/RTSCommand.h"

class ARTSBuilding;
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
}
