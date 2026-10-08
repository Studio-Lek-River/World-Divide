#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
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

	/** Every PlayerStart of World has navmesh 10 m beside it, clear of the headquarters standing on it. */
	inline bool HasNavigationAtStarts(UWorld& World)
	{
		const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		bool bAny = false;
		for (TActorIterator<APlayerStart> It(&World); It && NavSystem; ++It)
		{
			FNavLocation NavPoint;
			const FVector Beside = It->GetActorLocation() + FVector(0.f, 1000.f, 0.f);
			if (!NavSystem->ProjectPointToNavigation(Beside, NavPoint, FVector(100.f, 100.f, 300.f)))
			{
				return false;
			}
			bAny = true;
		}
		return bAny;
	}

	/**
	 * True once World's navmesh is not rebuilding and covers its PlayerStarts. The maps are saved with their navmesh built,
	 * so a play world has it from the start, whichever map the editor opened first.
	 */
	inline bool IsNavigationReady(UWorld& World)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		return NavSystem && NavSystem->GetDefaultNavDataInstance() && !NavSystem->IsNavigationBuildInProgress()
			&& HasNavigationAtStarts(World);
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
