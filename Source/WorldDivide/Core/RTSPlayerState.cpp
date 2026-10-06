#include "Core/RTSPlayerState.h"

#include "Net/UnrealNetwork.h"

void ARTSPlayerState::SetTeamId(int32 InTeamId)
{
	check(HasAuthority());
	TeamId = InTeamId;
}

void ARTSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTSPlayerState, TeamId);
}
