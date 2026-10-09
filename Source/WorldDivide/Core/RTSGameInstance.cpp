#include "Core/RTSGameInstance.h"

#include "Core/RTSSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Player/RTSLocalPlayer.h"

const TCHAR* URTSGameInstance::HostClosedLobbyMessage = TEXT("The host closed the lobby");
const TCHAR* URTSGameInstance::HostLeftMatchMessage = TEXT("The host left the match");

void URTSGameInstance::Init()
{
	Super::Init();
	NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &URTSGameInstance::HandleNetworkFailure);
}

void URTSGameInstance::Shutdown()
{
	GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	Super::Shutdown();
}

void URTSGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error)
{
	// Every game instance in the process hears every failure (PIE runs several); a join still pending has no world yet.
	const FWorldContext* Context = World ? GEngine->GetWorldContextFromWorld(World) : GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver);
	URTSLocalPlayer* LocalPlayer = Cast<URTSLocalPlayer>(GetFirstGamePlayer());
	if (!Context || Context != GetWorldContext() || !LocalPlayer || !LocalPlayer->GetMenuMessage().IsEmpty())
	{
		return;
	}
	if (FailureType == ENetworkFailure::PendingConnectionFailure)
	{
		if (!Error.IsEmpty())
		{
			LocalPlayer->SetMenuMessage(FText::FromString(Error));
		}
	}
	// By now the lost connection has taken the replicated actors with it, so the map tells where the client was: a client
	// plays either the lobby or a match.
	else if (World && World->GetNetMode() == NM_Client)
	{
		const bool bInLobby = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == GetDefault<URTSSettings>()->LobbyMap.GetLongPackageName();
		LocalPlayer->SetMenuMessage(FText::FromString(bInLobby ? HostClosedLobbyMessage : HostLeftMatchMessage));
	}
}
