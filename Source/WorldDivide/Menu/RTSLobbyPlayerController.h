#pragma once

#include "CoreMinimal.h"
#include "Core/RTSSkirmishSeats.h"
#include "GameFramework/PlayerController.h"
#include "RTSLobbyPlayerController.generated.h"

class SRTSLobbyWidget;

/** A player in the lobby: shows the lobby screen and sends the player's map and seat changes to the server's ARTSLobbyGameMode. */
UCLASS()
class WORLDDIVIDE_API ARTSLobbyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTSLobbyPlayerController();

	/** The lobby's seats as this player sees them; empty until they replicate. */
	TConstArrayView<FRTSSeat> GetSeats() const;

	/** The index of the lobby's map in URTSSettings::SkirmishMaps, as this player sees it. */
	int32 GetMapIndex() const;

	/** This player hosts the lobby and may change every seat and start the match. */
	bool IsHost() const;

	/** This player's own APlayerState::GetPlayerId, or INDEX_NONE. */
	int32 GetOwnPlayerId() const;

	/** The seat this player holds, or null until the seats replicate. */
	const FRTSSeat* GetOwnSeat() const;

	UFUNCTION(Server, Reliable)
	void Server_SetMap(int32 Index);

	UFUNCTION(Server, Reliable)
	void Server_SetSeatType(int32 Index, ERTSSeatType Type, ERTSAIDifficulty Difficulty);

	UFUNCTION(Server, Reliable)
	void Server_SetStartSpot(int32 Index, int32 Spot);

	UFUNCTION(Server, Reliable)
	void Server_SetReady(bool bReady);

	/** The token by which the match gives this player its seat, kept by its URTSLocalPlayer across the travel. */
	UFUNCTION(Client, Reliable)
	void Client_SetSeatToken(const FString& Token);

	/** Host only: starts the match. */
	bool RequestStart();

	/** Back to the main menu; the host closes the lobby for everyone. */
	void LeaveLobby();

	virtual void ReceivedPlayer() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TSharedPtr<SRTSLobbyWidget> Widget;
};
