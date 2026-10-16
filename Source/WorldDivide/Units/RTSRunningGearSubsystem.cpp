#include "Units/RTSRunningGearSubsystem.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSUnit.h"

void FRTSRunningGearTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Subsystem && TickType != LEVELTICK_ViewportsOnly)
	{
		Subsystem->PlaceAll();
	}
}

FString FRTSRunningGearTickFunction::DiagnosticMessage()
{
	return TEXT("URTSRunningGearSubsystem::PlaceAll");
}

void URTSRunningGearSubsystem::Register(ARTSUnit& Unit)
{
	Units.AddUnique(&Unit);
}

void URTSRunningGearSubsystem::Unregister(ARTSUnit& Unit)
{
	Units.RemoveSwap(&Unit);
}

void URTSRunningGearSubsystem::PlaceAll()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSRunningGearSubsystem::PlaceAll);

	for (int32 Index = Units.Num() - 1; Index >= 0; --Index)
	{
		if (ARTSUnit* Unit = Units[Index].Get())
		{
			Unit->UpdateRunningGear();
		}
		else
		{
			Units.RemoveAtSwap(Index);
		}
	}
}

void URTSRunningGearSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// After the actor ticks and the tickables, avoidance among them, and before the frame's render updates are sent.
	TickFunction.Subsystem = this;
	TickFunction.TickGroup = TG_PostUpdateWork;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void URTSRunningGearSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	Units.Reset();
	Super::Deinitialize();
}
