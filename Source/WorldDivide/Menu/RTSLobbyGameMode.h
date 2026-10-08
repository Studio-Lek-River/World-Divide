#pragma once

#include "CoreMinimal.h"
#include "Core/RTSSkirmishSeats.h"
#include "GameFramework/GameModeBase.h"
#include "RTSLobbyGameMode.generated.h"

class ARTSLobbyGameState;

/**
 * The lobby of a skirmish: the seats of ARTSSkirmishGameMode, with its closed seats open. A joining player takes the first
 * open seat and gets a token to find it again in the match; one who leaves opens it again. The host sets each seat open,
 * closed or to an AI of a difficulty and the start spot of every seat, each player the start spot of its own. The host's
 * start travels everyone to the skirmish map with the seats in its Seats option.
 */
UCLASS()
class WORLDDIVIDE_API ARTSLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSLobbyGameMode();

	/** The seats start as ARTSSkirmishGameMode's defaults, with its closed seats open. */
	virtual void InitGameState() override;

	/** Refuses a player once no seat is open. */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** Requester changes seat Index to Type (Difficulty for an AI). False when RTSSkirmish::CanChangeType refuses it. */
	bool SetSeatType(const APlayerController& Requester, int32 Index, ERTSSeatType Type, ERTSAIDifficulty Difficulty);

	/** Requester moves seat Index to Spot. False when RTSSkirmish::CanChangeStartSpot refuses it. */
	bool SetStartSpot(const APlayerController& Requester, int32 Index, int32 Spot);

	/** The URL the match starts with: the skirmish map, listening when hosted, and the seats. */
	FString GetMatchURL() const;

	/** The host starts the match once RTSSkirmish::CanStart allows it. False when it does not travel. */
	bool StartMatch(const APlayerController& Requester);

	/** Requester is the host: the local player of a listen server or of an offline game. */
	static bool IsHost(const APlayerController& Requester);

private:
	ARTSLobbyGameState* GetLobbyState() const;
};
