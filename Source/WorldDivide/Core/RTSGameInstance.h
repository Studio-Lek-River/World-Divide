#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "RTSGameInstance.generated.h"

class UNetDriver;

/**
 * Tells the main menu why the engine sent the player there after a network failure: a refused join shows the server's
 * reason (such as "The lobby is full"), a lost connection in the lobby (URTSSettings::LobbyMap) shows "The host closed
 * the lobby", and one in a match shows "The host left the match". A message already set, such as the reason for a kick,
 * is kept.
 */
UCLASS()
class WORLDDIVIDE_API URTSGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	static const TCHAR* HostClosedLobbyMessage;
	static const TCHAR* HostLeftMatchMessage;

	virtual void Init() override;
	virtual void Shutdown() override;

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error);

	FDelegateHandle NetworkFailureHandle;
};
