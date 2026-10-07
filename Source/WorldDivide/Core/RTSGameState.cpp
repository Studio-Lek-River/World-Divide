#include "Core/RTSGameState.h"

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Data/RTSBuildingData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "WorldDivide.h"

bool ARTSGameState::CountsForSurvival(const ARTSBuilding& Building)
{
	const URTSBuildingData* Data = Building.GetBuildingData();
	return Data && !Data->bIsDefense;
}

void ARTSGameState::RefreshTeams(const ARTSBuilding* Removed)
{
	check(HasAuthority());
	UWorld* World = GetWorld();
	if (!World || bMatchOver)
	{
		return;
	}

	// A full scan rather than counts per team, so nothing can drift; it only runs when a building is placed or falls.
	TArray<int32> Teams;
	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		const ARTSBuilding* Building = *It;
		if (Building != Removed && !Building->IsActorBeingDestroyed() && !Building->GetHealthComponent()->IsDead()
			&& CountsForSurvival(*Building))
		{
			Teams.AddUnique(Building->GetTeamId());
		}
	}
	Teams.Sort();

	const bool bTeamDropped = AliveTeams.ContainsByPredicate([&Teams](int32 Team) { return !Teams.Contains(Team); });
	AliveTeams = MoveTemp(Teams);
	if (bTeamDropped && AliveTeams.Num() <= 1)
	{
		bMatchOver = true;
		WinningTeam = AliveTeams.Num() == 1 ? AliveTeams[0] : INDEX_NONE;
		UE_LOG(LogWorldDivide, Log, TEXT("Match over, winning team %d"), WinningTeam);
	}
}

ERTSMatchResult ARTSGameState::GetMatchResult(int32 TeamId) const
{
	if (!bMatchOver)
	{
		return ERTSMatchResult::InProgress;
	}
	return WinningTeam != INDEX_NONE && TeamId == WinningTeam ? ERTSMatchResult::Victory : ERTSMatchResult::Defeat;
}

bool ARTSGameState::IsMatchOver(const UWorld& World)
{
	const ARTSGameState* GameState = World.GetGameState<ARTSGameState>();
	return GameState && GameState->IsMatchOver();
}

void ARTSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTSGameState, AliveTeams);
	DOREPLIFETIME(ARTSGameState, bMatchOver);
	DOREPLIFETIME(ARTSGameState, WinningTeam);
}
