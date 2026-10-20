#include "Units/RTSDustSubsystem.h"

#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSDayNightSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
		TEXT("cm from the nearest camera within which a vehicle shows dust (#224): 200 m, so none at the RTS camera's farthest zoom or at the screen's edges at its default zoom (#336, the producer's choice). 0 or less everywhere."));

	TAutoConsoleVariable<int32> CVarDustMaxVehicles(
		TEXT("rts.Fx.DustMaxVehicles"), 32,
		TEXT("Most vehicles showing dust at once (#224), the moving ones nearest a camera; each running system costs the game thread several microseconds."));

	TAutoConsoleVariable<float> CVarDustSurfaceInterval(
		TEXT("rts.Fx.DustSurfaceInterval"), 0.25f,
		TEXT("Seconds between two traces for the ground under one vehicle showing dust (#224), its physical material choosing the dust's rate, colour and shape; 0 every frame."));

	TAutoConsoleVariable<float> CVarDustNoneInterval(
		TEXT("rts.Fx.DustNoneInterval"), 1.f,
		TEXT("Seconds between two traces for the ground under a moving vehicle on ground that raises no dust, such as water (#336): it takes no place in rts.Fx.DustMaxVehicles, and raises dust again once a trace finds other ground."));

	/** The hull's back is sent again once it has turned by more than about 2.5 degrees: the cosine of that. */
	constexpr float ResendDirectionDot = 0.999f;

	const FName RateParameter(TEXT("User.Rate"));
	const FName ColorParameter(TEXT("User.Color"));
	const FName OpacityParameter(TEXT("User.Opacity"));
	const FName SizeParameter(TEXT("User.Size"));
	const FName DirectionParameter(TEXT("User.Direction"));
	const FName PointsParameter(TEXT("User.Points"));
	const FName SideParameter(TEXT("User.Side"));
	// The ground's look (FRTSDustSurface, #336), sent when the ground changes.
	const FName HazeScaleParameter(TEXT("User.HazeScale"));
	const FName OpacityScaleParameter(TEXT("User.OpacityScale"));
	const FName LifetimeParameter(TEXT("User.Lifetime"));
	const FName SpreadParameter(TEXT("User.Spread"));
	const FName LiftParameter(TEXT("User.Lift"));
	const FName SinkParameter(TEXT("User.Sink"));
	const FName SizeScaleParameter(TEXT("User.SizeScale"));
	const FName ChunkScaleParameter(TEXT("User.ChunkScale"));
	const FName ChunkSizeParameter(TEXT("User.ChunkSize"));
	const FName ChunkColorParameter(TEXT("User.ChunkColor"));

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

float URTSDustSubsystem::GetNoneInterval()
{
	return FMath::Max(RTSDust::CVarDustNoneInterval.GetValueOnGameThread(), 0.f);
}

FRTSDustSurface URTSDustSubsystem::GetGround(const FRTSVehicleDust& Dust, EPhysicalSurface Surface)
{
	if (const FRTSDustSurface* Listed = Dust.FindSurface(Surface))
	{
		return *Listed;
	}
	FRTSDustSurface Unlisted;
	Unlisted.Surface = Surface;
	Unlisted.Color = Dust.Color;
	return Unlisted;
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
		Entry->SentSurface = INDEX_NONE;
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
			// Not reset: dust raised before a stop still settles when the vehicle drives off again (#336). A machine that
			// never renders (tests under -nullrhi) keeps the emitter active, but Niagara does not run it.
			Component->Activate(false);
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
	if (Entry.Contacts.IsEmpty())
	{
		// A vehicle on water has no emitters yet, or none since its data changed, but still looks for the ground under them.
		GetContactPoints(*Unit.GetUnitData(), Entry.Contacts);
	}
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
		// them. Owned by no actor and attached to the hull, so they follow its tilt over the ground, and are not its parts. One
		// serves two contact points, as both tracks (#336): half the systems for Niagara to tick.
		GetContactPoints(*Unit.GetUnitData(), Entry.Contacts);
		for (int32 First = 0; First < Entry.Contacts.Num(); First += 2)
		{
			const bool bPair = First + 1 < Entry.Contacts.Num();
			const FVector Middle = bPair ? 0.5 * (Entry.Contacts[First] + Entry.Contacts[First + 1]) : Entry.Contacts[First];
			// From the middle to the second point in cm, in the component's space, which keeps none of the hull's scale.
			const FVector Side = bPair ? 0.5 * (Entry.Contacts[First + 1] - Entry.Contacts[First]) * Hull->GetComponentScale() : FVector::ZeroVector;
			UNiagaraComponent* Component = NewObject<UNiagaraComponent>(GetWorld());
			Component->SetAutoActivate(false);
			Component->SetAutoDestroy(false);
			Component->SetCastShadow(false);
			Component->SetCanEverAffectNavigation(false);
			// Lighting channel 1 only (#341): lit by the sun or moon (OnWorldBeginPlay), the sky and the headlights, never by
			// the red tail lights, which turned the dust behind a vehicle into a red cloud at night.
			Component->LightingChannels.bChannel0 = false;
			Component->LightingChannels.bChannel1 = true;
			Component->SetAsset(Dust.System);
			// The hull's scale stays out of the dust.
			Component->SetUsingAbsoluteScale(true);
			Component->SetupAttachment(Hull);
			Component->SetRelativeLocation(Middle);
			Component->SetVariableFloat(SizeParameter, Dust.Size);
			Component->SetVariableFloat(PointsParameter, bPair ? 2.f : 1.f);
			Component->SetVariableVec3(SideParameter, Side);
			Component->RegisterComponentWithWorld(GetWorld());
			Entry.Components.Add(Component);
		}
		Entry.bActive = false;
		Entry.SentRate = -1.f;
		Entry.SentOpacity = -1.f;
		Entry.SentSurface = INDEX_NONE;
		Entry.SentDirection = FVector3f::ZeroVector;
	}
	if (Entry.Components.IsEmpty())
	{
		return;
	}

	UpdateSurface(Entry, Unit, GetSurfaceInterval());
	const FRTSDustSurface Ground = GetGround(Dust, Entry.Surface);
	const float Rate = Entry.MotionRate * FMath::Max(Ground.RateScale, 0.f)
		* RTSNearestToView::GetRateScale(Entry.ViewDistance, GetDistance(), EffectsQuality);
	if (Rate <= 0.f)
	{
		// Ground that raises none, as water.
		SetActive(Entry, false);
		return;
	}
	const bool bSendRate = Entry.SentRate < 0.f || RTSNearestToView::Differs(Entry.SentRate, Rate);
	const bool bSendOpacity = Entry.SentOpacity < 0.f || RTSNearestToView::Differs(Entry.SentOpacity, Dust.Opacity);
	if (bSendRate || bSendOpacity)
	{
		for (UNiagaraComponent* Component : Entry.Components)
		{
			if (IsValid(Component))
			{
				Component->SetVariableFloat(RateParameter, Rate);
				Component->SetVariableFloat(OpacityParameter, Dust.Opacity);
			}
		}
		Entry.SentRate = Rate;
		Entry.SentOpacity = Dust.Opacity;
	}
	// The ground's colour and shape, on the same system: a vehicle driving from one ground onto another keeps the dust it
	// raised on the first, which settles as it was born. In the dark both colours are dimmed (#341), sent again at dusk and dawn.
	const URTSDayNightSubsystem* DayNight = GetWorld()->GetSubsystem<URTSDayNightSubsystem>();
	const bool bDark = DayNight && DayNight->IsDark();
	if (Entry.SentSurface != static_cast<int32>(Entry.Surface) || Entry.bSentDark != bDark)
	{
		const float Brightness = bDark ? FMath::Clamp(Dust.NightBrightness, 0.f, 1.f) : 1.f;
		const FLinearColor Color(Ground.Color.R * Brightness, Ground.Color.G * Brightness, Ground.Color.B * Brightness, Ground.Color.A);
		const FLinearColor ChunkColor(Ground.ChunkColor.R * Brightness, Ground.ChunkColor.G * Brightness,
			Ground.ChunkColor.B * Brightness, Ground.ChunkColor.A);
		for (UNiagaraComponent* Component : Entry.Components)
		{
			if (IsValid(Component))
			{
				Component->SetVariableLinearColor(ColorParameter, Color);
				Component->SetVariableFloat(HazeScaleParameter, Ground.HazeScale);
				Component->SetVariableFloat(OpacityScaleParameter, Ground.OpacityScale);
				Component->SetVariableFloat(LifetimeParameter, Ground.Lifetime);
				Component->SetVariableFloat(SpreadParameter, Ground.Spread);
				Component->SetVariableFloat(LiftParameter, Ground.Lift);
				Component->SetVariableFloat(SinkParameter, Ground.Sink);
				Component->SetVariableFloat(SizeScaleParameter, Ground.SizeScale);
				Component->SetVariableFloat(ChunkScaleParameter, Ground.ChunkScale);
				Component->SetVariableFloat(ChunkSizeParameter, Ground.ChunkSize);
				Component->SetVariableLinearColor(ChunkColorParameter, ChunkColor);
			}
		}
		Entry.SentSurface = static_cast<int32>(Entry.Surface);
		Entry.bSentDark = bDark;
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
	// budget show it. A vehicle at rest takes no place in the budget, nor one on ground that raised none when it was last
	// asked (water, #336): that one asks again now and then, outside the budget, and comes back once it is out.
	Nearest.Reset();
	const float NoneInterval = GetNoneInterval();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FRTSDustEntry& Entry = Entries[Index];
		const ARTSUnit& Unit = *Entry.Unit.Get();
		const FRTSVehicleDust& Dust = Unit.GetUnitData()->Dust;
		const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
		Entry.MotionRate = GetRate(Dust, Motion.ForwardSpeed, Motion.YawRate);
		Entry.SurfaceAge += DeltaSeconds;
		Entry.bShows = false;
		if (bEnabled && Entry.MotionRate > 0.f && Dust.HasDust() && IsNear(Entry, Unit))
		{
			const FRTSDustSurface* Ground = Dust.FindSurface(Entry.Surface);
			const float Scale = (Ground ? Ground->RateScale : 1.f)
				* RTSNearestToView::GetRateScale(Entry.ViewDistance, GetDistance(), EffectsQuality);
			if (Scale > 0.f)
			{
				Nearest.Emplace(Entry.ViewDistance * (Entry.bActive ? RTSNearestToView::KeepScale : 1.f), Index);
			}
			else
			{
				UpdateSurface(Entry, Unit, NoneInterval);
			}
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

	// The dust is on lighting channel 1 only (#341), so the sun or moon lights that channel too, on every map. Turned on in
	// place, as rts.Debug's night shots (FRTSShotNight) change the level's lights.
	if (DrawsOn(InWorld.GetNetMode()))
	{
		for (TActorIterator<ADirectionalLight> It(&InWorld); It; ++It)
		{
			ULightComponent* Light = It->GetLightComponent();
			if (Light && !Light->LightingChannels.bChannel1)
			{
				Light->LightingChannels.bChannel1 = true;
				Light->MarkRenderStateDirty();
			}
		}
	}
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
