#include "Menu/RTSLobbyGameState.h"

#include "Net/UnrealNetwork.h"

void ARTSLobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARTSLobbyGameState, Seats);
}
