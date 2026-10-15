#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace RTSTests
{
	/** Shared by every test file: unity builds merge their anonymous namespaces, so one definition must live here. */
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/**
	 * How many game seconds a PIE smoke test runs per real second: -RTSTestTimeDilation=N on the command line, 1 without it.
	 * Gameplay only reads its frame's delta time, and the tests measure game time with World->GetTimeSeconds(), so a
	 * dilated run checks the same things in a fraction of the time; only their timeouts, in real seconds, grow more generous.
	 */
	inline float GetTestTimeDilation()
	{
		float Dilation = 1.f;
		FParse::Value(FCommandLine::Get(), TEXT("RTSTestTimeDilation="), Dilation);
		return FMath::Max(Dilation, 1.f);
	}

	/** Every PlayerStart of World has navmesh 10 m beside it, clear of the footprint of the headquarters standing on it. */
	inline bool HasNavigationAtStarts(UWorld& World)
	{
		const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		bool bAny = false;
		for (TActorIterator<APlayerStart> It(&World); It && NavSystem; ++It)
		{
			float Clear = 0.f;
			for (TActorIterator<ARTSBuilding> Building(&World); Building; ++Building)
			{
				const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Building->GetFootprint());
				const FVector Offset = It->GetActorLocation() - Building->GetActorLocation();
				if (FMath::Abs(Offset.X) <= Extent.X && FMath::Abs(Offset.Y) <= Extent.Y)
				{
					Clear = FMath::Max(Clear, static_cast<float>(Extent.Y - Offset.Y));
				}
			}
			FNavLocation NavPoint;
			const FVector Beside = It->GetActorLocation() + FVector(0.f, Clear + 1000.f, 0.f);
			if (!NavSystem->ProjectPointToNavigation(Beside, NavPoint, FVector(100.f, 100.f, 300.f)))
			{
				return false;
			}
			bAny = true;
		}
		return bAny;
	}

	/**
	 * True once World has a navmesh for every nav agent class, none is rebuilding and the narrowest covers its PlayerStarts.
	 * The maps are saved with their navmeshes built, so a play world has them from the start, whichever map the editor
	 * opened first.
	 */
	inline bool IsNavigationReady(UWorld& World)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!NavSystem || NavSystem->IsNavigationBuildInProgress())
		{
			return false;
		}
		for (const FNavDataConfig& Agent : RTSNavAgents::GetAgents())
		{
			const ANavigationData* NavData = RTSNavAgents::GetNavMesh(World, Agent);
			if (!NavData || NavData->GetConfig().Name != Agent.Name)
			{
				return false;
			}
		}
		return HasNavigationAtStarts(World);
	}

	/** Runs World at the test time dilation. A listen server replicates it to its clients. */
	inline void ApplyTestTimeDilation(UWorld& World)
	{
		if (AWorldSettings* Settings = World.GetWorldSettings())
		{
			Settings->SetTimeDilation(GetTestTimeDilation());
		}
	}
}

#endif
