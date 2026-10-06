#include "Core/RTSGameMode.h"

#include "Core/RTSPlayerState.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSHUD.h"
#include "Player/RTSPlayerController.h"

ARTSGameMode::ARTSGameMode()
{
	DefaultPawnClass = ARTSCameraPawn::StaticClass();
	PlayerControllerClass = ARTSPlayerController::StaticClass();
	PlayerStateClass = ARTSPlayerState::StaticClass();
	HUDClass = ARTSHUD::StaticClass();
}

FString ARTSGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	// The team must be known before Super picks the start spot.
	if (ARTSPlayerState* PlayerState = NewPlayerController->GetPlayerState<ARTSPlayerState>())
	{
		PlayerState->SetTeamId(NextTeamId++);
	}
	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

AActor* ARTSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (const ARTSPlayerState* PlayerState = Player ? Player->GetPlayerState<ARTSPlayerState>() : nullptr)
	{
		const FName TeamTag(*FString::Printf(TEXT("Team%d"), PlayerState->GetTeamId()));
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->PlayerStartTag == TeamTag)
			{
				return *It;
			}
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}
