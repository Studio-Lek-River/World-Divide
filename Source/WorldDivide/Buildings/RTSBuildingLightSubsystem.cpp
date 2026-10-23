#include "Buildings/RTSBuildingLightSubsystem.h"

#include "Algo/Count.h"
#include "Buildings/RTSBuilding.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSDayNightSubsystem.h"
#include "Data/RTSBuildingData.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Scalability.h"
#include "Units/RTSNearestToView.h"

namespace
{
	TAutoConsoleVariable<int32> CVarBuildingLightMaxBuildings(
		TEXT("rts.Fx.BuildingLightMaxBuildings"), 4,
		TEXT("Most buildings showing the real lights at their lamps at once in the dark (#195), those nearest a camera; the rest only glow."));

	TAutoConsoleVariable<int32> CVarBuildingLightMax(
		TEXT("rts.Fx.BuildingLightMax"), 24,
		TEXT("Most real kit lights shown at once in the dark (BUILDING_KIT.md, Light budgets, #391), the nearest buildings' first; a starting hypothesis Gate A measures."));

	/** Secondary kit lights shine only at this effects quality and above: High. */
	constexpr int32 SecondaryLightQuality = 2;
}

void FRTSBuildingLightTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Subsystem && TickType != LEVELTICK_ViewportsOnly)
	{
		Subsystem->UpdateAll(DeltaTime);
	}
}

FString FRTSBuildingLightTickFunction::DiagnosticMessage()
{
	return TEXT("URTSBuildingLightSubsystem::UpdateAll");
}

int32 URTSBuildingLightSubsystem::GetMaxKitLights()
{
	return FMath::Max(CVarBuildingLightMax.GetValueOnGameThread(), 0);
}

int32 URTSBuildingLightSubsystem::GetMaxBuildings()
{
	return FMath::Max(CVarBuildingLightMaxBuildings.GetValueOnGameThread(), 0);
}

void URTSBuildingLightSubsystem::Register(ARTSBuilding& Building)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (FEntry* Entry = Entries.FindByPredicate([&Building](const FEntry& E) { return E.Building.Get() == &Building; }))
	{
		Entry->bNight.Reset();
		return;
	}
	Entries.Add({ &Building, {} });
}

void URTSBuildingLightSubsystem::Unregister(ARTSBuilding& Building)
{
	Entries.RemoveAllSwap([&Building](const FEntry& E) { return E.Building.Get() == &Building; });
}

int32 URTSBuildingLightSubsystem::GetShownBuildingCount() const
{
	return static_cast<int32>(Algo::CountIf(Entries, [](const FEntry& E) { return E.Building.IsValid() && E.Building->AreLightsShown(); }));
}

void URTSBuildingLightSubsystem::UpdateAll(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSBuildingLightSubsystem::UpdateAll);
	const URTSDayNightSubsystem* DayNight = GetWorld() ? GetWorld()->GetSubsystem<URTSDayNightSubsystem>() : nullptr;
	const bool bDark = DayNight && DayNight->IsDark();
	Entries.RemoveAllSwap([](const FEntry& E) { return !E.Building.IsValid(); });
	if (Entries.IsEmpty())
	{
		return;
	}
	RTSNearestToView::GatherCameras(GetWorld(), CameraLocations);

	// Every building's glow follows the dark; in it, the constructed ones nearest a camera light their surroundings too.
	Nearest.Reset();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FEntry& Entry = Entries[Index];
		ARTSBuilding& Building = *Entry.Building.Get();
		if (!Entry.bNight.IsSet() || *Entry.bNight != bDark)
		{
			Building.SetLightsNight(bDark);
			Entry.bNight = bDark;
		}
		// One LOD decision for the whole building, shell, parts and kit pieces, so a kit mesh shared between buildings
		// switches with each of them (BUILDING_KIT.md, option C2).
		const URTSBuildingData* Data = Building.GetBuildingData();
		if (Data && Data->LODSwitchDistance > 0.f)
		{
			const float Distance = RTSNearestToView::GetDistance(CameraLocations, ViewLocationOverride, Building.GetActorLocation());
			Building.SetForcedLOD(Distance > Data->LODSwitchDistance ? 1 : 0);
		}
		if (bDark && Building.IsConstructed() && !Building.GetLightComponents().IsEmpty())
		{
			const float Distance = RTSNearestToView::GetDistance(CameraLocations, ViewLocationOverride, Building.GetActorLocation());
			Nearest.Emplace(Distance * (Building.AreLightsShown() ? RTSNearestToView::KeepScale : 1.f), Index);
		}
	}
	RTSNearestToView::KeepNearest(Nearest, GetMaxBuildings());

	TBitArray<> Shows(false, Entries.Num());
	for (const TPair<float, int32>& Near : Nearest)
	{
		Shows[Near.Value] = true;
	}
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		ARTSBuilding& Building = *Entries[Index].Building.Get();
		if (Building.AreLightsShown() != Shows[Index])
		{
			Building.ShowLights(Shows[Index]);
		}
	}

	// The kit lights of the buildings that show theirs, nearest first, within the budget: they flash, flicker and turn with
	// their lenses. Nothing here by day, when none is shown.
	if (Nearest.IsEmpty())
	{
		return;
	}
	Nearest.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	const double Time = GetWorld()->GetTimeSeconds();
	const bool bSecondary = Scalability::GetQualityLevels().EffectsQuality >= SecondaryLightQuality;
	int32 Budget = GetMaxKitLights();
	for (const TPair<float, int32>& Near : Nearest)
	{
		Entries[Near.Value].Building->UpdateKitLights(Time, CameraLocations, ViewLocationOverride, bSecondary, Budget);
	}
}

void URTSBuildingLightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	TickFunction.Subsystem = this;
	TickFunction.TickGroup = TG_PostUpdateWork;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void URTSBuildingLightSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	Entries.Reset();
	Super::Deinitialize();
}
