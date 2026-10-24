#pragma once

#include "CoreMinimal.h"
#include "Core/RTSSkirmishSeats.h"
#include "GameFramework/GameModeBase.h"
#include "RTSGameMode.generated.h"

class APlayerStart;
class APlayerState;

/**
 * Gives each joining player a team, the default faction from URTSSettings and the starting credits, and starts their camera
 * at their team's PlayerStart. Without Seats, players take the next team id and team <id> plays from the PlayerStart tagged
 * "Team<id>". With Seats, as in a skirmish, each seat is a team: a human takes the seat FindSeatForPlayer gives them, an AI
 * opponent takes each AI seat, a Closed seat stays empty, and each team plays from the start spot of its seat. When play
 * starts, every team with a PlayerStart gets its faction's completed headquarters there, and the starting units placed at
 * a spot go to the team playing from it.
 */
UCLASS()
class WORLDDIVIDE_API ARTSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSGameMode();

	/** Reads the Seats option (?Seats=Human@0,AI-Hard@1,Closed, see RTSSkirmish::ParseSeats) over the default Seats. */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	/** Refuses a player when every seat for a human is taken. */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void StartPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Economy", meta = (ClampMin = "0"))
	int32 StartingCredits = 10000;

	/** The seats of the match, one per team; empty for a plain map where every PlayerStart is a team of its own. */
	UPROPERTY(EditDefaultsOnly, Category = "Seats")
	TArray<FRTSSeat> Seats;

	/** The seats in play, with every start spot assigned. */
	const TArray<FRTSSeat>& GetSeats() const { return Seats; }

	/** The PlayerStart TeamId plays from, or null. */
	APlayerStart* FindTeamStart(int32 TeamId) const;

	/**
	 * Where the host's restart travels: with Seats this map again with the same seats and start spots, so the humans find
	 * their seats by their tokens; else "?Restart".
	 */
	FString GetRestartURL() const;

	/** Login option with which a player names the seat it held in the lobby (see URTSLocalPlayer). */
	static const TCHAR* SeatTokenOption;

private:
	/** Each team's PlayerStart, by team id: the seat's start spot with Seats, else the PlayerStart tagged with the team. */
	void FindTeamStarts();

	/** The seat a player joining with Options plays, other than those taken by players but Joining, or INDEX_NONE. */
	int32 ChooseTeam(const FString& Options, const APlayerState* Joining) const;

	/**
	 * Hands the starting units placed at each start spot to the team playing from it, and removes those at spots nobody
	 * plays, before they spawn.
	 */
	void AssignUnitSpawners();

	/** Gives each AI seat an ARTSAIPlayer, with its team, difficulty, the default faction and the starting credits. */
	void SpawnAIOpponents();

	/**
	 * Spawns the completed URTSFactionData::Headquarters of each team's faction, or of the default faction for a team
	 * without a player yet, on the grid spot of the team's PlayerStart, turned to RTSBuildPlacement::GetHeadquartersYaw of
	 * the PlayerStart's facing.
	 */
	void SpawnHeadquarters();

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<APlayerStart>> TeamStarts;

	int32 NextTeamId = 0;
};
