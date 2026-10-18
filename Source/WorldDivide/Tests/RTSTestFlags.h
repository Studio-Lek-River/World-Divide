#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#endif
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "NavMesh/RecastNavMesh.h"
#include "HAL/IConsoleManager.h"
#include "Navigation/RTSMoveRequestSubsystem.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "Misc/App.h"
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

	/** The fixed game step rate in Hz the PIE smoke tests tick at: -RTSTestFixedStep=N on the command line, 0 (off) without it. */
	inline double GetTestFixedStepRate()
	{
		double Rate = 0.0;
		FParse::Value(FCommandLine::Get(), TEXT("RTSTestFixedStep="), Rate);
		return FMath::Max(Rate, 0.0);
	}

	/** rts.Nav.MoveRequestsPerFrame: move requests per frame in place of the real-time ms budget while above zero. */
	inline IConsoleVariable* GetMoveRequestsPerFrameVariable()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Nav.MoveRequestsPerFrame"));
	}

	/**
	 * For the rest of this PIE session move requests keep to the real-time rts.Nav.MoveRequestBudgetMs again, as they do
	 * outside the tests: for the tests of that budget itself, which a fixed-step run otherwise replaces with a request count.
	 */
	inline void UseRealTimeMoveBudget()
	{
		if (IConsoleVariable* PerFrame = GetMoveRequestsPerFrameVariable())
		{
			PerFrame->Set(0, ECVF_SetByCode);
		}
	}

	/**
	 * Finishes every navmesh rebuild of World in the frame it started, instead of on worker threads a CPU-dependent number of
	 * frames later, so a fixed-step run cuts and closes building holes in the same frames on any machine (#179).
	 */
	inline void CompleteNavigationBuild(UWorld& World)
	{
		if (!World.IsGameWorld())
		{
			return;
		}
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!NavSystem || !NavSystem->IsNavigationBuildInProgress())
		{
			return;
		}
		for (TActorIterator<ANavigationData> It(&World); It; ++It)
		{
			It->EnsureBuildCompletion();
		}
	}

#if WITH_EDITOR
	/**
	 * Ticks the engine at a fixed 1/Rate game seconds per frame, without the editor's frame rate cap, until this PIE session
	 * ends: game time then runs as fast as the CPU ticks instead of in step with real time. Whatever else hung on real time
	 * is made to follow the frames for the session too, so the same run plays out the same on any machine and under any load
	 * (#179): move requests run rts.Nav.MoveRequestsPerFrame at a time instead of a ms budget, and navmesh rebuilds finish in
	 * their frame. The engine's previous time step and request count are restored when PIE ends, so the Session Frontend
	 * leaves the editor as it was.
	 */
	inline void ApplyTestFixedStep(double Rate)
	{
		static FDelegateHandle RestoreHandle;
		static FDelegateHandle NavigationHandle;
		if (RestoreHandle.IsValid())
		{
			return;
		}
		const bool bWasFixed = FApp::UseFixedTimeStep();
		const double WasDeltaTime = FApp::GetFixedDeltaTime();
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / Rate);
		IConsoleVariable* PerFrame = GetMoveRequestsPerFrameVariable();
		const int32 WasPerFrame = PerFrame ? PerFrame->GetInt() : 0;
		if (PerFrame && WasPerFrame <= 0)
		{
			PerFrame->Set(URTSMoveRequestSubsystem::TestRequestsPerFrame, ECVF_SetByCode);
		}
		NavigationHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* World, ELevelTick, float)
		{
			if (World)
			{
				CompleteNavigationBuild(*World);
			}
		});
		RestoreHandle = FEditorDelegates::EndPIE.AddLambda([bWasFixed, WasDeltaTime, WasPerFrame](bool)
		{
			FApp::SetUseFixedTimeStep(bWasFixed);
			FApp::SetFixedDeltaTime(WasDeltaTime);
			if (IConsoleVariable* PerFrame = GetMoveRequestsPerFrameVariable())
			{
				PerFrame->Set(WasPerFrame, ECVF_SetByCode);
			}
			FWorldDelegates::OnWorldPostActorTick.Remove(NavigationHandle);
			NavigationHandle.Reset();
			FEditorDelegates::EndPIE.Remove(RestoreHandle);
			RestoreHandle.Reset();
		});
	}

	/**
	 * For the rest of this PIE session the engine ticks in step with real time again, and move requests keep to the
	 * real-time budget, as every session that did not apply the fixed step itself ran before #179: for tests whose checks
	 * hang on real time, such as emulated network lag. Ending PIE still puts the editor back as it was.
	 */
	inline void UseRealTimeStep()
	{
		FApp::SetUseFixedTimeStep(false);
		UseRealTimeMoveBudget();
	}
#endif

	/**
	 * Speeds up World's game time for the test run. With -RTSTestFixedStep the engine ticks at that fixed step as fast as it
	 * can (every world of the session alike); otherwise World runs at the test time dilation, which a listen server
	 * replicates to its clients.
	 */
	inline void ApplyTestTimeDilation(UWorld& World)
	{
		const double FixedStepRate = GetTestFixedStepRate();
		if (FixedStepRate > 0.0)
		{
#if WITH_EDITOR
			ApplyTestFixedStep(FixedStepRate);
#endif
			return;
		}
		if (AWorldSettings* Settings = World.GetWorldSettings())
		{
			Settings->SetTimeDilation(GetTestTimeDilation());
		}
	}
}

#endif
