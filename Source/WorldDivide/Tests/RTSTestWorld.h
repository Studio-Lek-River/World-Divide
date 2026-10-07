#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Buildings/RTSBuilding.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Economy/RTSSupplySource.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Units/RTSUnit.h"

namespace RTSTests
{
	/**
	 * A game world with initialized actors, so units and buildings bind their death handling, but no BeginPlay:
	 * nothing ticks and tests step weapons and construction themselves.
	 */
	struct FRTSTestWorld
	{
		FRTSTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FRTSTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		ARTSUnit* SpawnUnit(URTSUnitData* Data, int32 TeamId, const FVector& Location) const
		{
			const FTransform Transform(Location);
			ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform);
			Unit->InitFromData(Data, TeamId);
			Unit->FinishSpawning(Transform);
			return Unit;
		}

		ARTSBuilding* SpawnBuilding(URTSBuildingData* Data, int32 TeamId, const FVector& Location) const
		{
			const FTransform Transform(Location);
			ARTSBuilding* Building = World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform);
			Building->InitFromData(Data, TeamId);
			Building->FinishSpawning(Transform);
			return Building;
		}

		ARTSSupplySource* SpawnSupplySource(int32 Amount, const FIntPoint& Footprint, const FVector& Location) const
		{
			const FTransform Transform(Location);
			ARTSSupplySource* Source = World->SpawnActorDeferred<ARTSSupplySource>(ARTSSupplySource::StaticClass(), Transform);
			Source->Amount = Amount;
			Source->Footprint = Footprint;
			Source->FinishSpawning(Transform);
			return Source;
		}

		/** Gives the world an ARTSGameState, which tracks the teams in the match. Call before SpawnPlayer and before any building. */
		ARTSGameState* SpawnGameState() const
		{
			ARTSGameState* GameState = World->SpawnActor<ARTSGameState>();
			World->SetGameState(GameState);
			return GameState;
		}

		/** A player on TeamId, registered with a game state so ARTSPlayerState::FindForTeam finds it. */
		ARTSPlayerState* SpawnPlayer(int32 TeamId, int32 Credits) const
		{
			if (!World->GetGameState())
			{
				World->SetGameState(World->SpawnActor<AGameStateBase>());
			}
			ARTSPlayerState* Player = World->SpawnActor<ARTSPlayerState>();
			if (!World->GetGameState()->PlayerArray.Contains(Player))
			{
				World->GetGameState()->AddPlayerState(Player);
			}
			Player->SetTeamId(TeamId);
			Player->AddCredits(Credits);
			return Player;
		}

		UWorld* World = nullptr;
	};
}

#endif
