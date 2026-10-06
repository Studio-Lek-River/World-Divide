#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Buildings/RTSBuilding.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
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

		UWorld* World = nullptr;
	};
}

#endif
