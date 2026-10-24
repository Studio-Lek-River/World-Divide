#include "Core/RTSGameState.h"

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSCivilBuilding.h"
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

void ARTSGameState::RegisterBuilding(ARTSBuilding& Building)
{
	UnregisterBuilding(Building);
	BuildingsByTeam.FindOrAdd(Building.GetTeamId()).Add(&Building);
}

void ARTSGameState::UnregisterBuilding(const ARTSBuilding& Building)
{
	for (TPair<int32, TArray<TWeakObjectPtr<ARTSBuilding>>>& Team : BuildingsByTeam)
	{
		Team.Value.RemoveAllSwap([&Building](const TWeakObjectPtr<ARTSBuilding>& Listed) { return Listed.Get() == &Building || !Listed.IsValid(); });
	}
}

void ARTSGameState::ForEachEnemyBuilding(int32 TeamId, TFunctionRef<void(ARTSBuilding&)> Visit) const
{
	for (const TPair<int32, TArray<TWeakObjectPtr<ARTSBuilding>>>& Team : BuildingsByTeam)
	{
		if (Team.Key == TeamId)
		{
			continue;
		}
		for (const TWeakObjectPtr<ARTSBuilding>& Listed : Team.Value)
		{
			if (ARTSBuilding* Building = Listed.Get())
			{
				Visit(*Building);
			}
		}
	}
}

void ARTSGameState::ForEachBuilding(TFunctionRef<void(ARTSBuilding&)> Visit) const
{
	for (const TPair<int32, TArray<TWeakObjectPtr<ARTSBuilding>>>& Team : BuildingsByTeam)
	{
		for (const TWeakObjectPtr<ARTSBuilding>& Listed : Team.Value)
		{
			if (ARTSBuilding* Building = Listed.Get())
			{
				Visit(*Building);
			}
		}
	}
}

void ARTSGameState::RegisterCivilBuilding(ARTSCivilBuilding& Building)
{
	CivilBuildings.AddUnique(&Building);
}

void ARTSGameState::UnregisterCivilBuilding(const ARTSCivilBuilding& Building)
{
	CivilBuildings.RemoveAllSwap([&Building](const TWeakObjectPtr<ARTSCivilBuilding>& Listed) { return Listed.Get() == &Building || !Listed.IsValid(); });
}

void ARTSGameState::ForEachCivilBuilding(TFunctionRef<void(ARTSCivilBuilding&)> Visit) const
{
	for (const TWeakObjectPtr<ARTSCivilBuilding>& Listed : CivilBuildings)
	{
		if (ARTSCivilBuilding* Building = Listed.Get())
		{
			Visit(*Building);
		}
	}
}

void ARTSGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Buildings placed in the level, or spawned before the game state, are listed now; later ones list themselves.
	if (HasAuthority() && GetWorld())
	{
		for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
		{
			RegisterBuilding(**It);
		}
	}
}

void ARTSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTSGameState, AliveTeams);
	DOREPLIFETIME(ARTSGameState, bMatchOver);
	DOREPLIFETIME(ARTSGameState, WinningTeam);
}
