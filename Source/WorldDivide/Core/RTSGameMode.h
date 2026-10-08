#pragma once

#include "CoreMinimal.h"
#include "Data/RTSAIBuildOrderData.h"
#include "GameFramework/GameModeBase.h"
#include "RTSGameMode.generated.h"

/**
 * Gives each joining player the next team id, the default faction from URTSSettings and the starting credits,
 * and starts their camera at the PlayerStart tagged "Team<id>". When play starts, every team with such a PlayerStart gets
 * its faction's completed headquarters there; with bSpawnAIOpponents, a team no player has joined by then gets an AI
 * opponent first.
 */
UCLASS()
class WORLDDIVIDE_API ARTSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSGameMode();

	/** Reads the AIDifficulty option (?AIDifficulty=Easy|Normal|Hard) from the URL. */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void StartPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Economy", meta = (ClampMin = "0"))
	int32 StartingCredits = 10000;

	/** Skirmish: an ARTSAIPlayer takes every team seat still empty when play starts. */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	bool bSpawnAIOpponents = false;

	UPROPERTY(EditDefaultsOnly, Category = "AI")
	ERTSAIDifficulty AIDifficulty = ERTSAIDifficulty::Normal;

private:
	/**
	 * Gives each team PlayerStart without a player an ARTSAIPlayer, with the team, the default faction and the starting
	 * credits a player gets. Later players get the teams after them.
	 */
	void SpawnAIOpponents();

	/**
	 * Spawns the completed URTSFactionData::Headquarters of each team's faction, or of the default faction for a team
	 * without a player yet, on the grid spot of the team's PlayerStart.
	 */
	void SpawnHeadquarters();

	int32 NextTeamId = 0;
};
