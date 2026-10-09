#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "RTSLocalPlayer.generated.h"

/**
 * Keeps the seat token the lobby gave this player across the travel to the match, and logs in with it (?SeatToken=), so the
 * match's game mode gives the player the seat it held in the lobby. Also carries a message to the main menu, such as why the
 * player had to leave a lobby (set by the lobby on a kick and by URTSGameInstance on a network failure).
 */
UCLASS()
class WORLDDIVIDE_API URTSLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	void SetSeatToken(const FString& Token) { SeatToken = Token; }
	const FString& GetSeatToken() const { return SeatToken; }

	void SetMenuMessage(const FText& Message) { MenuMessage = Message; }
	const FText& GetMenuMessage() const { return MenuMessage; }

	virtual FString GetGameLoginOptions() const override;

private:
	FString SeatToken;
	FText MenuMessage;
};
