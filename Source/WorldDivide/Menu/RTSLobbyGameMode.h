#pragma once

#include "CoreMinimal.h"
#include "Core/RTSSkirmishSeats.h"
#include "GameFramework/GameModeBase.h"
#include "RTSLobbyGameMode.generated.h"

class ARTSLobbyGameState;

/**
 * The lobby of a skirmish: the host picks a map from URTSSettings::SkirmishMaps, which sets the seats, one per start spot,
 * from its default seats with the closed ones open. A joining player takes the first open seat and gets a token to find it
 * again in the match; one who leaves opens it again. The host sets each seat open, closed or to an AI of a difficulty and
 * the start spot of every seat, each player the start spot of its own. Each player readies its seat, the host's always is;
 * a seat that moves, or a change of map, must be readied again. Once every human is ready, the host's start travels
 * everyone to the chosen map with the seats in its Seats option.
 */
UCLASS()
class WORLDDIVIDE_API ARTSLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSLobbyGameMode();

	/** The lobby starts on the first of URTSSettings::SkirmishMaps, with its seats. */
	virtual void InitGameState() override;

	/** Refuses a player once no seat is open. */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** Requester changes seat Index to Type (Difficulty for an AI). False when RTSSkirmish::CanChangeType refuses it. */
	bool SetSeatType(const APlayerController& Requester, int32 Index, ERTSSeatType Type, ERTSAIDifficulty Difficulty);

	/** Requester moves seat Index to Spot. False when RTSSkirmish::CanChangeStartSpot refuses it. */
	bool SetStartSpot(const APlayerController& Requester, int32 Index, int32 Spot);

	/**
	 * Requester picks map Index of URTSSettings::SkirmishMaps; the seats resize to its start spots (RTSSkirmish::ResizeSeats).
	 * False when RTSSkirmish::CanChangeMap refuses it or there is no such map.
	 */
	bool SetMap(const APlayerController& Requester, int32 Index);

	/** Requester readies its own seat or takes that back. False when RTSSkirmish::SetReady refuses it. */
	bool SetReady(const APlayerController& Requester, bool bReady);

	/** The URL the match starts with: the chosen map, listening when hosted, and the seats. Empty without a map. */
	FString GetMatchURL() const;

	/** The host starts the match once RTSSkirmish::CanStart allows it, so once every human is ready. False when it does not travel. */
	bool StartMatch(const APlayerController& Requester);

	/** Requester is the host: the local player of a listen server or of an offline game. */
	static bool IsHost(const APlayerController& Requester);

private:
	ARTSLobbyGameState* GetLobbyState() const;
};
