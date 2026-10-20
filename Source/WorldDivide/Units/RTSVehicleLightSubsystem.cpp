#include "Units/RTSVehicleLightSubsystem.h"

#include "Algo/Count.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSDayNightSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSNearestToView.h"
#include "Units/RTSUnit.h"

namespace
{
	TAutoConsoleVariable<float> CVarVehicleLightDistance(
		TEXT("rts.Fx.VehicleLightDistance"), 20000.f,
		TEXT("cm from the nearest camera within which a vehicle lights the ground in the dark (#223), the near half of the default RTS view; 0 or less everywhere."));

	TAutoConsoleVariable<int32> CVarVehicleLightMaxVehicles(
		TEXT("rts.Fx.VehicleLightMaxVehicles"), 12,
		TEXT("Most vehicles lighting the ground at once in the dark (#223), those nearest a camera; each has two spot lights, so at most twice this many are on."));

	/** As the exhaust's (#222): a vehicle lighting the ground stops past this share of the distance, and counts as nearer. */
	constexpr float LightStopScale = 1.1f;
	constexpr float LightKeepScale = 0.9f;

	uint64 LightCycles = 0;

	/** A spot light for one end of a vehicle, attached to its hull at Offset, pointing along Direction tilted DownPitch down. */
	USpotLightComponent* MakeSpot(UWorld& World, UStaticMeshComponent& Hull, const FVector& Offset, float Yaw, float DownPitch,
		const FLinearColor& Color, float Intensity, float Radius, float InnerCone, float OuterCone)
	{
		USpotLightComponent* Light = NewObject<USpotLightComponent>(&World);
		Light->SetMobility(EComponentMobility::Movable);
		// It lights what it touches directly: no shadows, no Lumen, no fog or translucency, as the shot lights (#220).
		Light->SetCastShadows(false);
		Light->bAffectGlobalIllumination = false;
		Light->bAffectTranslucentLighting = false;
		Light->IndirectLightingIntensity = 0.f;
		Light->VolumetricScatteringIntensity = 0.f;
		Light->bUseInverseSquaredFalloff = true;
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(Intensity);
		Light->SetLightColor(Color);
		Light->SetAttenuationRadius(Radius);
		Light->SetInnerConeAngle(FMath::Min(InnerCone, OuterCone));
		Light->SetOuterConeAngle(OuterCone);
		Light->SetCanEverAffectNavigation(false);
		// The hull's scale stays out of the light; its place on the hull does not.
		Light->SetUsingAbsoluteScale(true);
		Light->SetupAttachment(&Hull);
		Light->SetRelativeLocationAndRotation(Offset, FRotator(-DownPitch, Yaw, 0.f));
		Light->SetVisibility(false);
		Light->RegisterComponentWithWorld(&World);
		return Light;
	}
}

void FRTSVehicleLightTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Subsystem && TickType != LEVELTICK_ViewportsOnly)
	{
		Subsystem->UpdateAll(DeltaTime);
	}
}

FString FRTSVehicleLightTickFunction::DiagnosticMessage()
{
	return TEXT("URTSVehicleLightSubsystem::UpdateAll");
}

float URTSVehicleLightSubsystem::GetDistance()
{
	return CVarVehicleLightDistance.GetValueOnGameThread();
}

int32 URTSVehicleLightSubsystem::GetMaxVehicles()
{
	return FMath::Max(CVarVehicleLightMaxVehicles.GetValueOnGameThread(), 0);
}

double URTSVehicleLightSubsystem::TakeSeconds()
{
	const double Seconds = FPlatformTime::ToSeconds64(LightCycles);
	LightCycles = 0;
	return Seconds;
}

FRTSVehicleLightEntry* URTSVehicleLightSubsystem::Find(const ARTSUnit& Unit)
{
	return Entries.FindByPredicate([&Unit](const FRTSVehicleLightEntry& Entry) { return Entry.Unit.Get() == &Unit; });
}

const FRTSVehicleLightEntry* URTSVehicleLightSubsystem::Find(const ARTSUnit& Unit) const
{
	return Entries.FindByPredicate([&Unit](const FRTSVehicleLightEntry& Entry) { return Entry.Unit.Get() == &Unit; });
}

void URTSVehicleLightSubsystem::Register(ARTSUnit& Unit)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (FRTSVehicleLightEntry* Entry = Find(Unit))
	{
		// New data or new parts: the lamps are told again and the lights made again where the data puts them.
		DestroyLights(*Entry);
		Entry->bLampsOn.Reset();
		return;
	}
	FRTSVehicleLightEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Unit = &Unit;
}

void URTSVehicleLightSubsystem::Unregister(ARTSUnit& Unit)
{
	const int32 Index = Entries.IndexOfByPredicate([&Unit](const FRTSVehicleLightEntry& Entry) { return Entry.Unit.Get() == &Unit; });
	if (Index != INDEX_NONE)
	{
		DestroyLights(Entries[Index]);
		Entries.RemoveAtSwap(Index);
	}
}

void URTSVehicleLightSubsystem::DestroyLights(FRTSVehicleLightEntry& Entry)
{
	for (USpotLightComponent* Light : { Entry.Headlight.Get(), Entry.Taillight.Get() })
	{
		if (IsValid(Light))
		{
			Light->DestroyComponent();
		}
	}
	Entry.Headlight = nullptr;
	Entry.Taillight = nullptr;
	Entry.bActive = false;
}

int32 URTSVehicleLightSubsystem::GetActiveLightCount() const
{
	int32 Active = 0;
	for (const FRTSVehicleLightEntry& Entry : Entries)
	{
		Active += Entry.bActive ? (Entry.Headlight ? 1 : 0) + (Entry.Taillight ? 1 : 0) : 0;
	}
	return Active;
}

int32 URTSVehicleLightSubsystem::GetActiveVehicleCount() const
{
	return static_cast<int32>(Algo::CountIf(Entries, [](const FRTSVehicleLightEntry& Entry) { return Entry.bActive; }));
}

USpotLightComponent* URTSVehicleLightSubsystem::GetHeadlight(const ARTSUnit& Unit) const
{
	const FRTSVehicleLightEntry* Entry = Find(Unit);
	return Entry ? Entry->Headlight.Get() : nullptr;
}

USpotLightComponent* URTSVehicleLightSubsystem::GetTaillight(const ARTSUnit& Unit) const
{
	const FRTSVehicleLightEntry* Entry = Find(Unit);
	return Entry ? Entry->Taillight.Get() : nullptr;
}

void URTSVehicleLightSubsystem::MakeLights(FRTSVehicleLightEntry& Entry, ARTSUnit& Unit)
{
	if (Entry.Headlight || Entry.Taillight)
	{
		return;
	}
	// Made the first time the vehicle lights the ground, and kept: one driving in and out of view switches them on and off.
	// Owned by no actor and attached to the hull, so they follow its turn, tilt and rock and are not counted among its parts.
	const FRTSVehicleLights& Lights = Unit.GetUnitData()->Lights;
	UStaticMeshComponent& Hull = *Unit.GetMesh();
	UWorld& World = *GetWorld();
	if (!Lights.HeadlightOffsets.IsEmpty())
	{
		Entry.Headlight = MakeSpot(World, Hull, FRTSVehicleLights::GetMidpoint(Lights.HeadlightOffsets), 0.f, Lights.HeadlightDownPitch,
			Lights.HeadlightColor, Lights.HeadlightIntensity, Lights.HeadlightRadius, Lights.HeadlightInnerCone, Lights.HeadlightOuterCone);
	}
	if (!Lights.TaillightOffsets.IsEmpty())
	{
		Entry.Taillight = MakeSpot(World, Hull, FRTSVehicleLights::GetMidpoint(Lights.TaillightOffsets), 180.f, Lights.TaillightDownPitch,
			Lights.TaillightColor, Lights.TaillightIntensity, Lights.TaillightRadius, Lights.TaillightInnerCone, Lights.TaillightOuterCone);
	}
	Entry.bActive = false;
}

void URTSVehicleLightSubsystem::SetActive(FRTSVehicleLightEntry& Entry, bool bActive)
{
	if (Entry.bActive == bActive)
	{
		return;
	}
	Entry.bActive = bActive;
	for (USpotLightComponent* Light : { Entry.Headlight.Get(), Entry.Taillight.Get() })
	{
		if (IsValid(Light))
		{
			Light->SetVisibility(bActive);
		}
	}
}

void URTSVehicleLightSubsystem::UpdateAll(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSVehicleLightSubsystem::UpdateAll);
	const uint64 StartCycles = FPlatformTime::Cycles64();

	const URTSDayNightSubsystem* DayNight = GetWorld() ? GetWorld()->GetSubsystem<URTSDayNightSubsystem>() : nullptr;
	const bool bDark = DayNight && DayNight->IsDark();
	RTSNearestToView::GatherCameras(GetWorld(), CameraLocations);

	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		const ARTSUnit* Unit = Entries[Index].Unit.Get();
		if (!Unit || !Unit->GetUnitData() || !Unit->GetMesh())
		{
			DestroyLights(Entries[Index]);
			Entries.RemoveAtSwap(Index);
		}
	}

	// Every vehicle's lamps follow the dark; in it, those near enough a camera light the ground, the nearest within the budget.
	Nearest.Reset();
	const float Distance = GetDistance();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FRTSVehicleLightEntry& Entry = Entries[Index];
		ARTSUnit& Unit = *Entry.Unit.Get();
		if (!Entry.bLampsOn.IsSet() || *Entry.bLampsOn != bDark)
		{
			Unit.SetLampsOn(bDark);
			Entry.bLampsOn = bDark;
		}
		Entry.bShows = false;
		if (!bDark || !Unit.GetUnitData()->Lights.HasLights())
		{
			continue;
		}
		Entry.ViewDistance = RTSNearestToView::GetDistance(CameraLocations, ViewLocationOverride, Unit.GetMesh()->GetComponentLocation());
		if (Distance <= 0.f || Entry.ViewDistance <= Distance * (Entry.bActive ? LightStopScale : 1.f))
		{
			Nearest.Emplace(Entry.ViewDistance * (Entry.bActive ? LightKeepScale : 1.f), Index);
		}
	}
	RTSNearestToView::KeepNearest(Nearest, GetMaxVehicles());
	for (const TPair<float, int32>& Near : Nearest)
	{
		Entries[Near.Value].bShows = true;
	}

	for (FRTSVehicleLightEntry& Entry : Entries)
	{
		if (Entry.bShows)
		{
			MakeLights(Entry, *Entry.Unit.Get());
		}
		SetActive(Entry, Entry.bShows);
	}
	LightCycles += FPlatformTime::Cycles64() - StartCycles;
}

void URTSVehicleLightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// After everything that moves a hull, as the exhaust (URTSExhaustSubsystem), before the render updates are sent.
	TickFunction.Subsystem = this;
	TickFunction.TickGroup = TG_PostUpdateWork;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void URTSVehicleLightSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	for (FRTSVehicleLightEntry& Entry : Entries)
	{
		DestroyLights(Entry);
	}
	Entries.Reset();
	Super::Deinitialize();
}
