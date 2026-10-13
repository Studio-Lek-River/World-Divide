#pragma once

#include "CoreMinimal.h"
#include "CommonInputBaseTypes.h"
#include "RTSUIInputData.generated.h"

class UDataTable;

/**
 * Common UI's click and back actions (Config/DefaultGame.ini, CommonInputSettings): the left mouse button and Esc, which
 * closes the top modal screen of the HUD. Mouse and keyboard only. The actions live in a table built in code rather than
 * as an asset, like the player controller's input, so every binding is reviewable text.
 */
UCLASS()
class WORLDDIVIDE_API URTSUIInputData : public UCommonUIInputData
{
	GENERATED_BODY()

public:
	URTSUIInputData();

private:
	UPROPERTY()
	TObjectPtr<UDataTable> Actions;
};
