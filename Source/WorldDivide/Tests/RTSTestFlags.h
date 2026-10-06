#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace RTSTests
{
	/** Shared by every test file: unity builds merge their anonymous namespaces, so one definition must live here. */
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

#endif
