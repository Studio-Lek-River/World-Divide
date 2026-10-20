#include "Units/RTSDustSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Scalability.h"
#include "Units/RTSGround.h"
#include "Units/RTSLocomotion.h"
#include "Units/RTSNearestToView.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

// Named, not anonymous: the unity build shares one scope with the exhaust's, which has values of the same names.
namespace RTSDust
{
	TAutoConsoleVariable<bool> CVarDust(
		TEXT("rts.Fx.Dust"), true,
		TEXT("Ground vehicles raise dust behind their tracks or wheels as they drive and turn (#224); off, every emitter stops."));

	TAutoConsoleVariable<float> CVarDustDistance(
		TEXT("rts.Fx.DustDistance"), 20000.f,
		TEXT("cm from the nearest camera within which a vehicle shows dust (#224), as the exhaust's; 0 or less everywhere."));

	TAutoConsoleVariable<int32> CVarDustMaxVehicles(
		TEXT("rts.Fx.DustMaxVehicles"), 32,
		TEXT("Most vehicles showing dust at once (#224), the moving ones nearest a camera; each running system costs the game thread several microseconds."));

	TAutoConsoleVariable<float> CVarDustSurfaceInterval(
		TEXT("rts.Fx.DustSurfaceInterval"), 0.25f,
		TEXT("Seconds between two traces for the ground under one vehicle showing dust (#224), its physical material choosing the dust's rate and colour; 0 every frame."));

	/** The hull's back is sent again once it has turned by more than about 2.5 degrees: the cosine of that. */
	constexpr float ResendDirectionDot = 0.999f;

	const FName RateParameter(TEXT("User.Rate"));
	const FName ColorParameter(TEXT("User.Color"));
	const FName OpacityParameter(TEXT("User.Opacity"));
	const FName SizeParameter(TEXT("User.Size"));
	const FName DirectionParameter(TEXT("User.Direction"));

	uint64 DustCycles = 0;

	/** A share of the interval from 0 to 1 for Unit, the same each time, so the vehicles' traces fall on different frames. */
	float GetPhase(const ARTSUnit& Unit)
	{
		return static_cast<float>(GetTypeHash(&Unit) % 1024u) / 1024.f;
	}
}

void FRTSDustTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Subsystem && TickType != LEVELTICK_ViewportsOnly)
	{
		Subsystem->UpdateAll(DeltaTime);
	}
}

FString FRTSDustTickFunction::DiagnosticMessage()
{
	return TEXT("URTSDustSubsystem::UpdateAll");
}

bool URTSDustSubsystem::IsEnabled()
{
	return RTSDust::CVarDust.GetValueOnGameThread();
}

float URTSDustSubsystem::GetDistance()
{
	return RTSDust::CVarDustDistance.GetValueOnGameThread();
}

int32 URTSDustSubsystem::GetMaxVehicles()
{
	return FMath::Max(RTSDust::CVarDustMaxVehicles.GetValueOnGameThread(), 0);
}

float URTSDustSubsystem::GetSurfaceInterval()
{
	return FMath::Max(RTSDust::CVarDustSurfaceInterval.GetValueOnGameThread(), 0.f);
}

float URTSDustSubsystem::GetRate(const FRTSVehicleDust& Dust, float ForwardSpeed, float YawRate, float SurfaceScale)
{
	const float Driving = FMath::Max(FMath::Abs(ForwardSpeed) - Dust.MinSpeed, 0.f) * Dust.SpeedScale;
	const float Turning = FMath::Abs(YawRate) * Dust.TurnScale;
	return FMath::Min(Driving + Turning, Dust.MaxRate) * FMath::Max(SurfaceScale, 0.f);
}

void URTSDustSubsystem::GetContactPoints(const URTSUnitData& Data, TArray<FVector>& OutPoints)
{
	OutPoints.Reset();
	if (!Data.Dust.ContactPoints.IsEmpty())
	{
		OutPoints = Data.Dust.ContactPoints;
		return;
	}
	const FBox Hull = RTSUnitShape::GetHullBox(Data);
	const FRTSLocomotionProfile& Locomotion = Data.Locomotion;
	if (!Hull.IsValid || !Locomotion.IsVehicle())
	{
		return;
	}
	// The locomotion's distances are the unit's, the hull box the mesh's own: back through MeshScale.
	const FVector Scale(FMath::Max(FMath::Abs(Data.MeshScale.X), KINDA_SMALL_NUMBER), FMath::Max(FMath::Abs(Data.MeshScale.Y), KINDA_SMALL_NUMBER), 1.f);
	if (Locomotion.Type == ERTSLocomotionType::Tracked)
	{
		// The rear end of each track, at the hull's back, on the ground.
		const float Side = 0.5f * Locomotion.TrackGauge / Scale.Y;
		OutPoints.Emplace(Hull.Min.X, -Side, Hull.Min.Z);
		OutPoints.Emplace(Hull.Min.X, Side, Hull.Min.Z);
	}
	else
	{
		// The rear wheels: the axles sit a wheelbase apart about the hull's middle.
		const float Rear = Hull.GetCenter().X - 0.5f * Locomotion.Wheelbase / Scale.X;
		const float Side = 0.5f * Locomotion.AxleTrack / Scale.Y;
		OutPoints.Emplace(Rear, -Side, Hull.Min.Z);
		OutPoints.Emplace(Rear, Side, Hull.Min.Z);
	}
}

double URTSDustSubsystem::TakeSeconds()
{
	const double Seconds = FPlatformTime::ToSeconds64(RTSDust::DustCycles);
	RTSDust::DustCycles = 0;
	return Seconds;
}

FRTSDustEntry* URTSDustSubsystem::Find(const ARTSUnit& Unit)
{
	return Entries.FindByPredicate([&Unit](const FRTSDustEntry& Entry) { return Entry.Unit.Get() == &Unit; });
}

const FRTSDustEntry* URTSDustSubsystem::Find(const ARTSUnit& Unit) const
{
	return Entries.FindByPredicate([&Unit](const FRTSDustEntry& Entry) { return Entry.Unit.Get() == &Unit; });
}

void URTSDustSubsystem::Register(ARTSUnit& Unit)
{
	const UWorld* World = GetWorld();
	if (!World || !DrawsOn(World->GetNetMode()))
	{
		return;
	}
	if (FRTSDustEntry* Entry = Find(Unit))
	{
		// New data: the contact points may have changed, so the emitters are made again when a camera is near.
		DestroyComponents(*Entry);
		Entry->SentRate = -1.f;
		Entry->SentOpacity = -1.f;
		Entry->SentColor.Reset();
		Entry->SentDirection = FVector3f::ZeroVector;
		return;
	}
	FRTSDustEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Unit = &Unit;
}

void URTSDustSubsystem::Unregister(ARTSUnit& Unit)
{
	const int32 Index = Entries.IndexOfByPredicate([&Unit](const FRTSDustEntry& Entry) { return Entry.Unit.Get() == &Unit; });
	if (Index != INDEX_NONE)
	{
		DestroyComponents(Entries[Index]);
		Entries.RemoveAtSwap(Index);
	}
}

void URTSDustSubsystem::DestroyComponents(FRTSDustEntry& Entry)
{
	for (UNiagaraComponent* Component : Entry.Components)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	Entry.Components.Reset();
	Entry.Contacts.Reset();
	Entry.bActive = false;
}

TConstArrayView<TObjectPtr<UNiagaraComponent>> URTSDustSubsystem::GetComponents(const ARTSUnit& Unit) const
{
	const FRTSDustEntry* Entry = Find(Unit);
	return Entry ? TConstArrayView<TObjectPtr<UNiagaraComponent>>(Entry->Components) : TConstArrayView<TObjectPtr<UNiagaraComponent>>();
}

EPhysicalSurface URTSDustSubsystem::GetSurface(const ARTSUnit& Unit) const
{
	const FRTSDustEntry* Entry = Find(Unit);
	return Entry ? Entry->Surface.GetValue() : SurfaceType_Default;
}

int32 URTSDustSubsystem::GetSurfaceQueries(const ARTSUnit& Unit) const
{
	const FRTSDustEntry* Entry = Find(Unit);
	return Entry ? Entry->SurfaceQueries : 0;
}

int32 URTSDustSubsystem::GetActiveEmitters() const
{
	int32 Active = 0;
	for (const FRTSDustEntry& Entry : Entries)
	{
		Active += Entry.bActive ? Entry.Components.Num() : 0;
	}
	return Active;
}

void URTSDustSubsystem::SetActive(FRTSDustEntry& Entry, bool bActive)
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
			// A machine that never renders (tests under -nullrhi) keeps the emitter active, but Niagara does not run it.
			Component->Activate(true);
		}
		else
		{
			// The dust already raised drifts on and settles.
			Component->Deactivate();
		}
	}
}

bool URTSDustSubsystem::IsNear(FRTSDustEntry& Entry, const ARTSUnit& Unit) const
{
	const UStaticMeshComponent* Hull = Unit.GetMesh();
	if (!Hull)
	{
		return false;
	}
	const float Distance = GetDistance();
	Entry.ViewDistance = RTSNearestToView::GetDistance(CameraLocations, ViewLocationOverride, Hull->GetComponentLocation());
	return Distance <= 0.f || Entry.ViewDistance <= Distance * (Entry.bActive ? RTSNearestToView::StopScale : 1.f);
}

void URTSDustSubsystem::UpdateSurface(FRTSDustEntry& Entry, const ARTSUnit& Unit, float Interval)
{
	if (Entry.SurfaceQueries > 0 && Entry.SurfaceAge < Interval)
	{
		return;
	}
	const UStaticMeshComponent* Hull = Unit.GetMesh();
	const UWorld* World = GetWorld();
	if (!Hull || !World || Entry.Contacts.IsEmpty())
	{
		return;
	}
	// Once, under the middle of the contact points, from as high above the ground as the hull is tall to as far below it.
	FVector Middle = FVector::ZeroVector;
	for (const FVector& Contact : Entry.Contacts)
	{
		Middle += Contact;
	}
	const FVector Ground = Hull->GetComponentTransform().TransformPosition(Middle / Entry.Contacts.Num());
	const float Span = static_cast<float>(Unit.GetShape().Hull.GetSize().Z);
	EPhysicalSurface Surface = Entry.Surface;
	Entry.bSurfaceKnown = RTSGround::TraceSurface(*World, Ground, Span, Span, Surface) || Entry.bSurfaceKnown;
	Entry.Surface = Surface;
	// The first trace sets the vehicle's own share of the interval, so the vehicles that set off together trace apart.
	Entry.SurfaceAge = Entry.SurfaceQueries == 0 ? -Interval * RTSDust::GetPhase(Unit) : 0.f;
	++Entry.SurfaceQueries;
}

void URTSDustSubsystem::UpdateEmitters(FRTSDustEntry& Entry, ARTSUnit& Unit, int32 EffectsQuality)
{
	using namespace RTSDust;
	const FRTSVehicleDust& Dust = Unit.GetUnitData()->Dust;
	UStaticMeshComponent* Hull = Unit.GetMesh();
	if (Entry.Components.IsEmpty())
	{
		// Made the first time the vehicle shows dust near a camera, and kept: one driving in and out of view starts and stops
		// them. Owned by no actor and attached to the hull, so they follow its tilt over the ground, and are not its parts.
		GetContactPoints(*Unit.GetUnitData(), Entry.Contacts);
		for (const FVector& Contact : Entry.Contacts)
		{
			UNiagaraComponent* Component = NewObject<UNiagaraComponent>(GetWorld());
			Component->SetAutoActivate(false);
			Component->SetAutoDestroy(false);
			Component->SetCastShadow(false);
			Component->SetCanEverAffectNavigation(false);
			Component->SetAsset(Dust.System);
			// The hull's scale stays out of the dust.
			Component->SetUsingAbsoluteScale(true);
			Component->SetupAttachment(Hull);
			Component->SetRelativeLocation(Contact);
			Component->SetVariableFloat(SizeParameter, Dust.Size);
			Component->RegisterComponentWithWorld(GetWorld());
			Entry.Components.Add(Component);
		}
		Entry.bActive = false;
		Entry.SentRate = -1.f;
		Entry.SentOpacity = -1.f;
		Entry.SentColor.Reset();
		Entry.SentDirection = FVector3f::ZeroVector;
	}
	if (Entry.Components.IsEmpty())
	{
		return;
	}

	UpdateSurface(Entry, Unit, GetSurfaceInterval());
	const FRTSDustSurface* Ground = Dust.FindSurface(Entry.Surface);
	const float SurfaceScale = Ground ? Ground->RateScale : 1.f;
	const FLinearColor& Color = Ground ? Ground->Color : Dust.Color;
	const float Rate = Entry.MotionRate * FMath::Max(SurfaceScale, 0.f)
		* RTSNearestToView::GetRateScale(Entry.ViewDistance, GetDistance(), EffectsQuality);
	if (Rate <= 0.f)
	{
		// Ground that raises none, as water.
		SetActive(Entry, false);
		return;
	}
	const bool bSendRate = Entry.SentRate < 0.f || RTSNearestToView::Differs(Entry.SentRate, Rate);
	const bool bSendOpacity = Entry.SentOpacity < 0.f || RTSNearestToView::Differs(Entry.SentOpacity, Dust.Opacity);
	const bool bSendColor = !Entry.SentColor.IsSet() || !Entry.SentColor->Equals(Color);
	if (bSendRate || bSendOpacity || bSendColor)
	{
		for (UNiagaraComponent* Component : Entry.Components)
		{
			if (IsValid(Component))
			{
				Component->SetVariableFloat(RateParameter, Rate);
				Component->SetVariableLinearColor(ColorParameter, Color);
				Component->SetVariableFloat(OpacityParameter, Dust.Opacity);
			}
		}
		Entry.SentRate = Rate;
		Entry.SentOpacity = Dust.Opacity;
		Entry.SentColor = Color;
	}
	// The dust is thrown off the way the tracks or wheels leave the ground behind: backward driving forward, and ahead in reverse.
	const float Away = Unit.GetRealizedMotion().ForwardSpeed < 0.f ? 1.f : -1.f;
	const FVector3f Direction(Hull->GetComponentTransform().TransformVectorNoScale(FVector(Away, 0.f, 0.f)));
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

void URTSDustSubsystem::UpdateAll(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSDustSubsystem::UpdateAll);
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

	// Every vehicle's dust from its motion, and those raising some near enough a camera; of them, the nearest within the
	// budget show it. A vehicle at rest takes no place in the budget.
	Nearest.Reset();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FRTSDustEntry& Entry = Entries[Index];
		const ARTSUnit& Unit = *Entry.Unit.Get();
		const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
		Entry.MotionRate = GetRate(Unit.GetUnitData()->Dust, Motion.ForwardSpeed, Motion.YawRate);
		Entry.SurfaceAge += DeltaSeconds;
		Entry.bShows = false;
		if (bEnabled && Entry.MotionRate > 0.f && Unit.GetUnitData()->Dust.HasDust() && IsNear(Entry, Unit))
		{
			Nearest.Emplace(Entry.ViewDistance * (Entry.bActive ? RTSNearestToView::KeepScale : 1.f), Index);
		}
	}
	RTSNearestToView::KeepNearest(Nearest, GetMaxVehicles());
	for (const TPair<float, int32>& Near : Nearest)
	{
		Entries[Near.Value].bShows = true;
	}

	for (FRTSDustEntry& Entry : Entries)
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
	RTSDust::DustCycles += FPlatformTime::Cycles64() - StartCycles;
}

void URTSDustSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// After everything that moves a hull, as the exhaust (URTSExhaustSubsystem), before the render updates are sent.
	TickFunction.Subsystem = this;
	TickFunction.TickGroup = TG_PostUpdateWork;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void URTSDustSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	for (FRTSDustEntry& Entry : Entries)
	{
		DestroyComponents(Entry);
	}
	Entries.Reset();
	Super::Deinitialize();
}
