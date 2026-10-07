#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RTSGameMode.generated.h"

/**
 * Gives each joining player the next team id, the default faction from URTSSettings and the starting credits,
 * and starts their camera at the PlayerStart tagged "Team<id>". When play starts, every team with such a PlayerStart gets
 * its faction's completed headquarters there.
 */
UCLASS()
class WORLDDIVIDE_API ARTSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSGameMode();

	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void StartPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Economy", meta = (ClampMin = "0"))
	int32 StartingCredits = 10000;

private:
	/**
	 * Spawns the completed URTSFactionData::Headquarters of each team's faction, or of the default faction for a team
	 * without a player yet, on the grid spot of the team's PlayerStart.
	 */
	void SpawnHeadquarters();

	int32 NextTeamId = 0;
};
