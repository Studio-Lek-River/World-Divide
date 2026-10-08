#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RTSMainMenuController.generated.h"

class SRTSMainMenuWidget;

/** Shows the main menu: a skirmish offline, hosting a lobby, joining one by address, or quitting. */
UCLASS()
class WORLDDIVIDE_API ARTSMainMenuController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTSMainMenuController();

	/** Opens the lobby offline, where the player sets up a match against AI opponents. */
	void PlaySkirmish();

	/** Opens the lobby as a listen server that other players can join. */
	void HostGame();

	/** Joins the lobby hosted at Address, such as 127.0.0.1. */
	void JoinGame(const FString& Address);

	void QuitGame();

	virtual void ReceivedPlayer() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TSharedPtr<SRTSMainMenuWidget> Widget;
};
