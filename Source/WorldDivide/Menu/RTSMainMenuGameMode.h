#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RTSMainMenuGameMode.generated.h"

/** The game mode of L_MainMenu: nothing but the menu screen of ARTSMainMenuController. */
UCLASS()
class WORLDDIVIDE_API ARTSMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSMainMenuGameMode();
};
