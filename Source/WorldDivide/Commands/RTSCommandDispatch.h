#pragma once

#include "CoreMinimal.h"
#include "Commands/RTSCommand.h"

class ARTSUnit;

namespace RTSCommands
{
	/** Drops null, duplicate and foreign units: a player may only order their own team. */
	WORLDDIVIDE_API TArray<ARTSUnit*> FilterOwnedUnits(TConstArrayView<ARTSUnit*> Units, int32 TeamId);

	/** Server only. A move is spread over a formation around the target; other orders go to every unit unchanged. */
	WORLDDIVIDE_API void Dispatch(TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command);
}
