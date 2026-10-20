#include "Units/RTSExhaustSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Scalability.h"
#include "Units/RTSLocomotion.h"
#include "Units/RTSNearestToView.h"
#include "Units/RTSUnit.h"

namespace
{
	TAutoConsoleVariable<bool> CVarExhaust(
		TEXT("rts.Fx.Exhaust"), true,
		TEXT("Vehicles show engine exhaust at their outlets (#222); off, every emitter stops."));

	TAutoConsoleVariable<float> CVarExhaustDistance(
		TEXT("rts.Fx.ExhaustDistance"), 20000.f,
		TEXT("cm from the nearest camera within which a vehicle shows exhaust (#222), the near half of the default RTS view; 0 or less everywhere."));

	TAutoConsoleVariable<int32> CVarExhaustMaxVehicles(
		TEXT("rts.Fx.ExhaustMaxVehicles"), 48,
		TEXT("Most vehicles showing exhaust at once (#222), those nearest a camera; each running system costs the game thread several microseconds."));

	/** The outlet's direction is sent again once it has turned by more than about 2.5 degrees: the cosine of that. */
	constexpr float ResendDirectionDot = 0.999f;

	const FName RateParameter(TEXT("User.Rate"));
	const FName ColorParameter(TEXT("User.Color"));
	const FName OpacityParameter(TEXT("User.Opacity"));
	const FName SizeParameter(TEXT("User.Size"));
	const FName DirectionParameter(TEXT("User.Direction"));

	uint64 ExhaustCycles = 0;
}

void FRTSExhaustTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Subsystem && TickType != LEVELTICK_ViewportsOnly)
	{
		Subsystem->UpdateAll(DeltaTime);
	}
}

FString FRTSExhaustTickFunction::DiagnosticMessage()
{
	return TEXT("URTSExhaustSubsystem::UpdateAll");
}

bool URTSExhaustSubsystem::IsEnabled()
{
	return CVarExhaust.GetValueOnGameThread();
}

float URTSExhaustSubsystem::GetDistance()
{
	return CVarExhaustDistance.GetValueOnGameThread();
}

int32 URTSExhaustSubsystem::GetMaxVehicles()
{
	return FMath::Max(CVarExhaustMaxVehicles.GetValueOnGameThread(), 0);
}

float URTSExhaustSubsystem::GetRate(float BaseRate, float ViewDistance, int32 EffectsQuality)
{
	return FMath::Max(BaseRate, 0.f) * RTSNearestToView::GetRateScale(ViewDistance, GetDistance(), EffectsQuality);
}

double URTSExhaustSubsystem::TakeSeconds()
{
	const double Seconds = FPlatformTime::ToSeconds64(ExhaustCycles);
	ExhaustCycles = 0;
	return Seconds;
}

FRTSExhaustEntry* URTSExhaustSubsystem::Find(const ARTSUnit& Unit)
{
	return Entries.FindByPredicate([&Unit](const FRTSExhaustEntry& Entry) { return Entry.Unit.Get() == &Unit; });
}

const FRTSExhaustEntry* URTSExhaustSubsystem::Find(const ARTSUnit& Unit) const
{
	return Entries.FindByPredicate([&Unit](const FRTSExhaustEntry& Entry) { return Entry.Unit.Get() == &Unit; });
}

void URTSExhaustSubsystem::Register(ARTSUnit& Unit)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (FRTSExhaustEntry* Entry = Find(Unit))
	{
		// New data: the outlets may have changed, so the emitters are made again when a camera is near.
		DestroyComponents(*Entry);
		Entry->SentRate = -1.f;
		Entry->SentOpacity = -1.f;
		Entry->SentColor.Reset();
		Entry->SentDirection = FVector3f::ZeroVector;
		return;
	}
	FRTSExhaustEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Unit = &Unit;
}

void URTSExhaustSubsystem::Unregister(ARTSUnit& Unit)
{
	const int32 Index = Entries.IndexOfByPredicate([&Unit](const FRTSExhaustEntry& Entry) { return Entry.Unit.Get() == &Unit; });
	if (Index != INDEX_NONE)
	{
		DestroyComponents(Entries[Index]);
		Entries.RemoveAtSwap(Index);
	}
}

void URTSExhaustSubsystem::DestroyComponents(FRTSExhaustEntry& Entry)
{
	for (UNiagaraComponent* Component : Entry.Components)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	Entry.Components.Reset();
	Entry.bActive = false;
}

ERTSExhaustState URTSExhaustSubsystem::GetState(const ARTSUnit& Unit) const
{
	const FRTSExhaustEntry* Entry = Find(Unit);
	return Entry ? Entry->State : ERTSExhaustState::Idle;
}

TConstArrayView<TObjectPtr<UNiagaraComponent>> URTSExhaustSubsystem::GetComponents(const ARTSUnit& Unit) const
{
	const FRTSExhaustEntry* Entry = Find(Unit);
	return Entry ? TConstArrayView<TObjectPtr<UNiagaraComponent>>(Entry->Components) : TConstArrayView<TObjectPtr<UNiagaraComponent>>();
}

int32 URTSExhaustSubsystem::GetActiveEmitters() const
{
	int32 Active = 0;
	for (const FRTSExhaustEntry& Entry : Entries)
	{
		Active += Entry.bActive ? Entry.Components.Num() : 0;
	}
	return Active;
}

float URTSExhaustSubsystem::GetViewDistance(const FVector& Location) const
{
	return RTSNearestToView::GetDistance(CameraLocations, ViewLocationOverride, Location);
}

void URTSExhaustSubsystem::UpdateState(FRTSExhaustEntry& Entry, const ARTSUnit& Unit, float DeltaSeconds) const
{
	const FRTSVehicleExhaust& Exhaust = Unit.GetUnitData()->Exhaust;
	const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
	const float Speed = FMath::Abs(Motion.ForwardSpeed);
	if (DeltaSeconds > 0.f)
	{
		// How fast the speed climbs, either way round; braking and reversing out of a forward drive count as falling.
		const float Climb = Entry.bSpeedKnown ? (Speed - Entry.LastSpeed) / DeltaSeconds : 0.f;
		const float Blend = 1.f - FMath::Exp(-DeltaSeconds / FMath::Max(Exhaust.AccelerationSmoothing, 0.01f));
		Entry.Climb += (Climb - Entry.Climb) * Blend;
		Entry.HoldLeft = Entry.Climb > Exhaust.AccelerationThreshold ? Exhaust.AccelerationHold : FMath::Max(Entry.HoldLeft - DeltaSeconds, 0.f);
		Entry.LastSpeed = Speed;
		Entry.bSpeedKnown = true;
	}
	const bool bMoving = Speed >= Exhaust.IdleSpeed || Motion.Status == ERTSLocomotionStatus::Pivoting;
	Entry.State = Entry.HoldLeft > 0.f ? ERTSExhaustState::Accelerating : bMoving ? ERTSExhaustState::Moving : ERTSExhaustState::Idle;
}

void URTSExhaustSubsystem::SetActive(FRTSExhaustEntry& Entry, bool bActive)
{
	if (Entry.bActive == bActive)
	{
		return;
	}
	Entry.bActive = bActive;
	for (UNiagaraComponent* Component : Entry.Components)
	{
		if (!IsValid(Component))
		{
			continue;
		}
		if (bActive)
		{
			// Not reset, as the dust (#336): the puffs out before a stop drift on. A machine that never renders (tests under
			// -nullrhi) keeps the emitter active, but Niagara does not run it.
			Component->Activate(false);
		}
		else
		{
			// The puffs already out drift on and fade.
			Component->Deactivate();
		}
	}
}

bool URTSExhaustSubsystem::IsNear(FRTSExhaustEntry& Entry, const ARTSUnit& Unit) const
{
	const UStaticMeshComponent* Hull = Unit.GetMesh();
	if (!Hull || !Unit.GetUnitData()->Exhaust.HasExhaust())
	{
		return false;
	}
	const float Distance = GetDistance();
	Entry.ViewDistance = GetViewDistance(Hull->GetComponentLocation());
	return Distance <= 0.f || Entry.ViewDistance <= Distance * (Entry.bActive ? RTSNearestToView::StopScale : 1.f);
}

void URTSExhaustSubsystem::UpdateEmitters(FRTSExhaustEntry& Entry, ARTSUnit& Unit, int32 EffectsQuality)
{
	const FRTSVehicleExhaust& Exhaust = Unit.GetUnitData()->Exhaust;
	UStaticMeshComponent* Hull = Unit.GetMesh();
	if (Entry.Components.IsEmpty())
	{
		// Made the first time a camera comes near, and kept: a vehicle driving in and out of view starts and stops them.
		// Owned by no actor and attached to the hull, so they follow its tilt and rock and are not counted among its parts.
		for (const FVector& Outlet : Exhaust.Outlets)
		{
			UNiagaraComponent* Component = NewObject<UNiagaraComponent>(GetWorld());
			Component->SetAutoActivate(false);
			Component->SetAutoDestroy(false);
			Component->SetCastShadow(false);
			Component->SetCanEverAffectNavigation(false);
			Component->SetAsset(Exhaust.System);
			// The hull's scale stays out of the puffs.
			Component->SetUsingAbsoluteScale(true);
			Component->SetupAttachment(Hull);
			Component->SetRelativeLocation(Outlet);
			Component->SetVariableFloat(SizeParameter, Exhaust.Size);
			Component->RegisterComponentWithWorld(GetWorld());
			Entry.Components.Add(Component);
		}
		Entry.bActive = false;
		Entry.SentRate = -1.f;
		Entry.SentOpacity = -1.f;
		Entry.SentColor.Reset();
		Entry.SentDirection = FVector3f::ZeroVector;
	}

	const float BaseRate = Entry.State == ERTSExhaustState::Accelerating ? Exhaust.AcceleratingRate
		: Entry.State == ERTSExhaustState::Moving ? Exhaust.MovingRate : Exhaust.IdleRate;
	const float Opacity = Entry.State == ERTSExhaustState::Accelerating ? Exhaust.AcceleratingOpacity
		: Entry.State == ERTSExhaustState::Moving ? Exhaust.MovingOpacity : Exhaust.IdleOpacity;
	const FLinearColor& Color = Entry.State == ERTSExhaustState::Accelerating ? Exhaust.AcceleratingColor
		: Entry.State == ERTSExhaustState::Moving ? Exhaust.MovingColor : Exhaust.IdleColor;
	// Plus the puffs that keep the trail closed as the hull drives off and leaves them behind.
	const float Rate = GetRate(BaseRate + Entry.LastSpeed / 100.f * Exhaust.PuffsPerMetre, Entry.ViewDistance, EffectsQuality);
	const bool bSendRate = Entry.SentRate < 0.f || RTSNearestToView::Differs(Entry.SentRate, Rate);
	const bool bSendOpacity = Entry.SentOpacity < 0.f || RTSNearestToView::Differs(Entry.SentOpacity, Opacity);
	// The colour only changes with the state, so it is sent as it is.
	const bool bSendColor = !Entry.SentColor.IsSet() || !Entry.SentColor->Equals(Color);
	if (bSendRate || bSendOpacity || bSendColor)
	{
		for (UNiagaraComponent* Component : Entry.Components)
		{
			if (IsValid(Component))
			{
				Component->SetVariableFloat(RateParameter, Rate);
				Component->SetVariableLinearColor(ColorParameter, Color);
				Component->SetVariableFloat(OpacityParameter, Opacity);
			}
		}
		Entry.SentRate = Rate;
		Entry.SentOpacity = Opacity;
		Entry.SentColor = Color;
	}
	// The puffs live in world space, so they are told which way the outlet faces there as the hull turns and tilts.
	const FVector3f Direction(Hull->GetComponentTransform().TransformVectorNoScale(
		Exhaust.Direction.IsNearlyZero() ? FVector(-1.f, 0.f, 0.f) : Exhaust.Direction.GetSafeNormal()));
	if (FVector3f::DotProduct(Direction, Entry.SentDirection) < ResendDirectionDot)
	{
		for (UNiagaraComponent* Component : Entry.Components)
		{
			if (IsValid(Component))
			{
				Component->SetVariableVec3(DirectionParameter, FVector(Direction));
			}
		}
		Entry.SentDirection = Direction;
	}
	SetActive(Entry, true);
}

void URTSExhaustSubsystem::UpdateAll(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSExhaustSubsystem::UpdateAll);
	const uint64 StartCycles = FPlatformTime::Cycles64();

	const bool bEnabled = IsEnabled();
	RTSNearestToView::GatherCameras(GetWorld(), CameraLocations);
	const int32 EffectsQuality = Scalability::GetQualityLevels().EffectsQuality;

	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		const ARTSUnit* Unit = Entries[Index].Unit.Get();
		if (!Unit || !Unit->GetUnitData())
		{
			DestroyComponents(Entries[Index]);
			Entries.RemoveAtSwap(Index);
		}
	}

	// Every vehicle's state, and those near enough a camera to show exhaust; of them, the nearest within the budget do.
	Nearest.Reset();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FRTSExhaustEntry& Entry = Entries[Index];
		const ARTSUnit& Unit = *Entry.Unit.Get();
		UpdateState(Entry, Unit, DeltaSeconds);
		Entry.bShows = false;
		if (bEnabled && IsNear(Entry, Unit))
		{
			Nearest.Emplace(Entry.ViewDistance * (Entry.bActive ? RTSNearestToView::KeepScale : 1.f), Index);
		}
	}
	RTSNearestToView::KeepNearest(Nearest, GetMaxVehicles());
	for (const TPair<float, int32>& Near : Nearest)
	{
		Entries[Near.Value].bShows = true;
	}

	for (FRTSExhaustEntry& Entry : Entries)
	{
		if (Entry.bShows)
		{
			UpdateEmitters(Entry, *Entry.Unit.Get(), EffectsQuality);
		}
		else
		{
			SetActive(Entry, false);
		}
	}
	ExhaustCycles += FPlatformTime::Cycles64() - StartCycles;
}

void URTSExhaustSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// After everything that moves a hull, as the running gear (URTSRunningGearSubsystem), before the render updates are sent.
	TickFunction.Subsystem = this;
	TickFunction.TickGroup = TG_PostUpdateWork;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void URTSExhaustSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	for (FRTSExhaustEntry& Entry : Entries)
	{
		DestroyComponents(Entry);
	}
	Entries.Reset();
	Super::Deinitialize();
}
