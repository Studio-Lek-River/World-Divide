#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RTSGameMode.generated.h"

/**
 * Gives each joining player the next team id and the default faction from URTSSettings,
 * and starts their camera at the PlayerStart tagged "Team<id>".
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

private:
	int32 NextTeamId = 0;
};
