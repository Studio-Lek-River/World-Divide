#include "Menu/RTSMainMenuGameMode.h"

#include "Menu/RTSMainMenuController.h"

ARTSMainMenuGameMode::ARTSMainMenuGameMode()
{
	PlayerControllerClass = ARTSMainMenuController::StaticClass();
	// Nothing to steer in the menu.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}
