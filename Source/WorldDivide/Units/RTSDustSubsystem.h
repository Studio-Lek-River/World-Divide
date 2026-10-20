#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSDustSubsystem.generated.h"

class ARTSUnit;
class UNiagaraComponent;
class URTSDustSubsystem;
class URTSUnitData;
struct FRTSDustSurface;
struct FRTSVehicleDust;

/** Runs URTSDustSubsystem::UpdateAll once a frame, in TG_PostUpdateWork. */
struct FRTSDustTickFunction : public FTickFunction
{
	URTSDustSubsystem* Subsystem = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
};

/** One vehicle that raises dust: the ground under it and the emitters at its contact points, made when a camera first comes near. */
USTRUCT()
struct FRTSDustEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<ARTSUnit> Unit;

	/** One per two contact points (#336), between them and attached to the hull; empty until the unit first shows dust. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> Components;

	/** The contact points in the hull mesh's own space, from the data or its locomotion, found with the emitters. */
	TArray<FVector> Contacts;

	/** Puffs a second from each contact point this frame, from the motion alone, before the ground and the quality. */
	float MotionRate = 0.f;
	/** cm from the nearest camera this frame, and whether the vehicle is among those showing dust. */
	float ViewDistance = 0.f;
	bool bShows = false;
	bool bActive = false;

	/** The ground under the vehicle when it was last asked, the seconds since, and how often it was asked. */
	TEnumAsByte<EPhysicalSurface> Surface = SurfaceType_Default;
	bool bSurfaceKnown = false;
	float SurfaceAge = 0.f;
	int32 SurfaceQueries = 0;

	/** What the emitters were last sent, so they are only sent again when it changes: the ground's look by its surface type. */
	float SentRate = -1.f;
	float SentOpacity = -1.f;
	int32 SentSurface = INDEX_NONE;
	/** Whether the colours sent were the dark ones (FRTSVehicleDust::NightBrightness). */
	bool bSentDark = false;
	FVector3f SentDirection = FVector3f::ZeroVector;
};

/**
 * Every machine that draws: the dust behind every ground vehicle whose data has a dust system (#224), once a frame after
 * every hull has moved. Its rate comes from the vehicle's realized speed and yaw rate, the same on every machine, so nothing
 * more replicates; the ground's physical material, asked once per vehicle every rts.Fx.DustSurfaceInterval, scales,
 * colours and shapes it (FRTSDustSurface, #336). The emitters are made only near a camera, run only while the vehicle raises
 * dust, stop past rts.Fx.DustDistance or beyond the rts.Fx.DustMaxVehicles nearest, and thin out with the effects quality.
 * A vehicle on ground that raises none (water) takes no place among those nearest and asks for the ground only every
 * rts.Fx.DustNoneInterval, so it raises dust again once it is out. A dedicated server keeps none.
 */
UCLASS()
class WORLDDIVIDE_API URTSDustSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A unit whose data has dust; ARTSUnit::CreateParts adds it. Adding one again makes its emitters anew. */
	void Register(ARTSUnit& Unit);
	void Unregister(ARTSUnit& Unit);

	/** Works out every registered unit's dust and drives its emitters, DeltaSeconds after the last time. Public for the tests. */
	void UpdateAll(float DeltaSeconds);

	/** The unit's emitters, one per two contact points; empty until it first showed dust. For the tests. */
	TConstArrayView<TObjectPtr<UNiagaraComponent>> GetComponents(const ARTSUnit& Unit) const;

	/** The ground under the unit when it was last asked; SurfaceType_Default before that or for a unit without dust. */
	EPhysicalSurface GetSurface(const ARTSUnit& Unit) const;

	/** How often the ground under the unit was asked for since it was registered. For the tests. */
	int32 GetSurfaceQueries(const ARTSUnit& Unit) const;

	/** Emitters running now, over every unit. */
	int32 GetActiveEmitters() const;

	/** Makes the distance to the cameras measured from Location instead, or from the cameras again when unset. For the tests. */
	void SetViewLocationOverride(TOptional<FVector> Location) { ViewLocationOverride = Location; }

	/** Whether a machine of NetMode draws dust at all: every one but a dedicated server. */
	static bool DrawsOn(ENetMode NetMode) { return NetMode != NM_DedicatedServer; }

	/** rts.Fx.Dust: vehicles raise dust at all. */
	static bool IsEnabled();

	/** rts.Fx.DustDistance: cm from the nearest camera within which a vehicle shows dust; 0 or less everywhere. */
	static float GetDistance();

	/** rts.Fx.DustMaxVehicles: most vehicles showing dust at once, the nearest a camera. */
	static int32 GetMaxVehicles();

	/** rts.Fx.DustSurfaceInterval: seconds between two questions for the ground under one vehicle. */
	static float GetSurfaceInterval();

	/** rts.Fx.DustNoneInterval: the same for a moving vehicle on ground that raises no dust, outside the budget. */
	static float GetNoneInterval();

	/** Dust's look on the ground Surface: its entry in Surfaces, or else the struct's defaults in Dust's own colour. */
	static FRTSDustSurface GetGround(const FRTSVehicleDust& Dust, EPhysicalSurface Surface);

	/**
	 * Puffs a second from one of Dust's contact points on a hull driving at ForwardSpeed cm/s and turning at YawRate degrees
	 * a second, on ground of SurfaceScale (FRTSDustSurface::RateScale): 0 at rest, rising with speed and with turning.
	 */
	static float GetRate(const FRTSVehicleDust& Dust, float ForwardSpeed, float YawRate, float SurfaceScale = 1.f);

	/**
	 * Where Data's vehicle raises dust, on the ground in its hull mesh's own space: Dust.ContactPoints when set; otherwise
	 * the rear of each track (tracked) or the rear wheels (wheeled), from the locomotion and the hull's box; none for legs.
	 */
	static void GetContactPoints(const URTSUnitData& Data, TArray<FVector>& OutPoints);

	/** The game thread seconds UpdateAll took since the last call, for the bench. */
	static double TakeSeconds();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	FRTSDustEntry* Find(const ARTSUnit& Unit);
	const FRTSDustEntry* Find(const ARTSUnit& Unit) const;
	void DestroyComponents(FRTSDustEntry& Entry);
	/** Measures the entry's distance to the cameras; whether it is within rts.Fx.DustDistance. */
	bool IsNear(FRTSDustEntry& Entry, const ARTSUnit& Unit) const;
	/** Asks for the ground under the unit when the interval has passed since it last did; finds its contact points first. */
	void UpdateSurface(FRTSDustEntry& Entry, const ARTSUnit& Unit, float Interval);
	void UpdateEmitters(FRTSDustEntry& Entry, ARTSUnit& Unit, int32 EffectsQuality);
	void SetActive(FRTSDustEntry& Entry, bool bActive);

	UPROPERTY(Transient)
	TArray<FRTSDustEntry> Entries;

	/** The local cameras this frame, kept between frames so gathering them does not allocate. */
	TArray<FVector> CameraLocations;
	/** The vehicles near enough to show dust this frame, by distance and entry, kept between frames too. */
	TArray<TPair<float, int32>> Nearest;
	TOptional<FVector> ViewLocationOverride;
	FRTSDustTickFunction TickFunction;
};
