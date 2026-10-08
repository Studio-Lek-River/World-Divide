#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "RTSLocalPlayer.generated.h"

/**
 * Keeps the seat token the lobby gave this player across the travel to the match, and logs in with it (?SeatToken=), so the
 * match's game mode gives the player the seat it held in the lobby.
 */
UCLASS()
class WORLDDIVIDE_API URTSLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	void SetSeatToken(const FString& Token) { SeatToken = Token; }
	const FString& GetSeatToken() const { return SeatToken; }

	virtual FString GetGameLoginOptions() const override;

private:
	FString SeatToken;
};
