#include "Menu/RTSLobbyGameState.h"

#include "Core/RTSSettings.h"
#include "Data/RTSMapData.h"
#include "Net/UnrealNetwork.h"

void ARTSLobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARTSLobbyGameState, Seats);
	DOREPLIFETIME(ARTSLobbyGameState, MapIndex);
}

const URTSMapData* ARTSLobbyGameState::LoadMap(int32 Index)
{
	const TArray<TSoftObjectPtr<URTSMapData>>& Maps = GetDefault<URTSSettings>()->SkirmishMaps;
	return Maps.IsValidIndex(Index) ? Maps[Index].LoadSynchronous() : nullptr;
}
