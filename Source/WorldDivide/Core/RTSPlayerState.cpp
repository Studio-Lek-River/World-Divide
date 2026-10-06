#include "Core/RTSPlayerState.h"

#include "Data/RTSFactionData.h"
#include "Net/UnrealNetwork.h"

void ARTSPlayerState::SetTeamId(int32 InTeamId)
{
	check(HasAuthority());
	TeamId = InTeamId;
}

void ARTSPlayerState::SetFaction(URTSFactionData* InFaction)
{
	check(HasAuthority());
	Faction = InFaction;
}

void ARTSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTSPlayerState, TeamId);
	DOREPLIFETIME(ARTSPlayerState, Faction);
}
