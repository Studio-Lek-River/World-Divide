#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "RTSConsoleToolset.generated.h"

/// Runs console commands in the World Divide editor, in the running Play-In-Editor session when there is one.
UCLASS(MinimalAPI)
class URTSConsoleToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Runs a console command, e.g. "stat fps" or "rts.UI.Reload".
	 * During Play-In-Editor it runs on the first local player of the PIE session, so stat overlays show in the
	 * PIE viewport; otherwise it runs in the editor world.
	 * @param Command The console command line to run.
	 * @return The text the command printed, or a note that it printed nothing. Raises a script error when the
	 *   command is empty or not recognised.
	 */
	UFUNCTION(meta = (AICallable), Category = "RTSConsoleToolset")
	static FString ExecuteConsoleCommand(const FString& Command);
};
