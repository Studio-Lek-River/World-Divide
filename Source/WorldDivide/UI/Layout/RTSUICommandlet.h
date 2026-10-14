#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RTSUICommandlet.generated.h"

struct FRTSLayoutValidationContext;

/**
 * The UI design round's headless checks (ue.ps1 -Task uibindings and -Task uicheck, docs/ui/AI_TEAM.md):
 * -run=RTSUI -Bindings=<file> writes what a layout may name (DescribeBindings) as JSON for /ui-brief;
 * -run=RTSUI -Validate=<UI dir> validates the layouts of an unpacked delivery before /ui-intake copies it, logging each
 * problem as "UICHECK: <file>: <element>: <problem>". Relative paths are taken from the project folder. Exit code 1 on a
 * problem or a bad command line.
 */
UCLASS()
class WORLDDIVIDE_API URTSUICommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URTSUICommandlet();

	virtual int32 Main(const FString& Params) override;

	/**
	 * The game's binding sources, each with its panels, keys, lists (with their item keys) and actions, and the widgets,
	 * theme colours, fonts and sprite names of Context, as JSON.
	 */
	static FString DescribeBindings(const FRTSLayoutValidationContext& Context);
};
