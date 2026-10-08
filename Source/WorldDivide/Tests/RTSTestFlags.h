#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
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
