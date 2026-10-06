#include "Core/RTSPlayerState.h"

#include "Data/RTSFactionData.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
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

void ARTSPlayerState::AddCredits(int32 Delta)
{
	check(HasAuthority());
	Credits = FMath::Max(Credits + Delta, 0);
}

bool ARTSPlayerState::TrySpendCredits(int32 Amount)
{
	check(HasAuthority());
	if (Amount < 0 || Credits < Amount)
	{
		return false;
	}
	Credits -= Amount;
	return true;
}

ARTSPlayerState* ARTSPlayerState::FindForTeam(const UWorld& World, int32 InTeamId)
{
	if (const AGameStateBase* GameState = World.GetGameState())
	{
		for (APlayerState* Player : GameState->PlayerArray)
		{
			ARTSPlayerState* State = Cast<ARTSPlayerState>(Player);
			if (State && State->TeamId == InTeamId)
			{
				return State;
			}
		}
	}
	return nullptr;
}

void ARTSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTSPlayerState, TeamId);
	DOREPLIFETIME(ARTSPlayerState, Faction);
	DOREPLIFETIME_CONDITION(ARTSPlayerState, Credits, COND_OwnerOnly);
}
