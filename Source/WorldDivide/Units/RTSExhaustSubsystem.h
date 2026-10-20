#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSExhaustSubsystem.generated.h"

class ARTSUnit;
class UNiagaraComponent;
class URTSExhaustSubsystem;

/** What a vehicle's engine shows, from how its hull moves (#222). */
UENUM(BlueprintType)
enum class ERTSExhaustState : uint8
{
	Idle,
	Moving,
	Accelerating,
};

/** Runs URTSExhaustSubsystem::UpdateAll once a frame, in TG_PostUpdateWork. */
struct FRTSExhaustTickFunction : public FTickFunction
{
	URTSExhaustSubsystem* Subsystem = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
};

/** One vehicle with exhaust: its engine's state and the emitters at its outlets, made when a camera first comes near. */
USTRUCT()
struct FRTSExhaustEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<ARTSUnit> Unit;

	/** One per outlet, attached to the hull; empty until the unit first comes within rts.Fx.ExhaustDistance of a camera. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> Components;

	ERTSExhaustState State = ERTSExhaustState::Idle;
	float LastSpeed = 0.f;
	/** The speed's climb in cm/s², smoothed, and the seconds the accelerating state still holds. */
	float Climb = 0.f;
	float HoldLeft = 0.f;
	/** cm from the nearest camera this frame, and whether the vehicle is among those showing exhaust. */
	float ViewDistance = 0.f;
	bool bShows = false;
	bool bSpeedKnown = false;
	bool bActive = false;
	/** What the emitters were last sent, so they are only sent again when it changes. */
	float SentRate = -1.f;
	float SentOpacity = -1.f;
	float SentLifetime = -1.f;
	float SentLift = -1.f;
	TOptional<FLinearColor> SentColor;
	FVector3f SentDirection = FVector3f::ZeroVector;
};

/**
 * Every machine that draws: the engine exhaust of every vehicle whose data has outlets (#222), once a frame after every hull
 * has moved. Each vehicle's state comes from the change of its realized forward speed, the same on every machine; the
 * emitters are made only near a camera, stopped past rts.Fx.ExhaustDistance or beyond the rts.Fx.ExhaustMaxVehicles nearest,
 * and thinned by the effects quality. A dedicated server keeps none.
 */
UCLASS()
class WORLDDIVIDE_API URTSExhaustSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A unit whose data has exhaust; ARTSUnit::CreateParts adds it. Adding one again makes its emitters anew. */
	void Register(ARTSUnit& Unit);
	void Unregister(ARTSUnit& Unit);

	/** Works out every registered unit's state and drives its emitters, DeltaSeconds after the last time. Public for the tests. */
	void UpdateAll(float DeltaSeconds);

	/** The unit's engine state; Idle for a unit without exhaust. */
	ERTSExhaustState GetState(const ARTSUnit& Unit) const;

	/** The unit's emitters, one per outlet; empty until a camera first came near. For the tests. */
	TConstArrayView<TObjectPtr<UNiagaraComponent>> GetComponents(const ARTSUnit& Unit) const;

	/** Emitters running now, over every unit. */
	int32 GetActiveEmitters() const;

	/** Makes the distance to the cameras measured from Location instead, or from the cameras again when unset. For the tests. */
	void SetViewLocationOverride(TOptional<FVector> Location) { ViewLocationOverride = Location; }

	/** rts.Fx.Exhaust: vehicles show exhaust at all. */
	static bool IsEnabled();

	/** rts.Fx.ExhaustDistance: cm from the nearest camera within which a vehicle shows exhaust; 0 or less everywhere. */
	static float GetDistance();

	/** rts.Fx.ExhaustMaxVehicles: most vehicles showing exhaust at once, the nearest a camera. */
	static int32 GetMaxVehicles();

	/**
	 * Puffs a second from an outlet whose data asks for BaseRate, ViewDistance cm from the nearest camera, at the effects
	 * quality EffectsQuality (0 low to 3 epic): fewer at lower quality, and toward half as many at the distance's end.
	 */
	static float GetRate(float BaseRate, float ViewDistance, int32 EffectsQuality);

	/** The game thread seconds UpdateAll took since the last call, for the bench. */
	static double TakeSeconds();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	FRTSExhaustEntry* Find(const ARTSUnit& Unit);
	const FRTSExhaustEntry* Find(const ARTSUnit& Unit) const;
	void DestroyComponents(FRTSExhaustEntry& Entry);
	void UpdateState(FRTSExhaustEntry& Entry, const ARTSUnit& Unit, float DeltaSeconds) const;
	/** Measures the entry's distance to the cameras; whether it is within rts.Fx.ExhaustDistance and has exhaust to show. */
	bool IsNear(FRTSExhaustEntry& Entry, const ARTSUnit& Unit) const;
	void UpdateEmitters(FRTSExhaustEntry& Entry, ARTSUnit& Unit, int32 EffectsQuality);
	void SetActive(FRTSExhaustEntry& Entry, bool bActive);
	/** cm from the nearest camera of this machine to Location; 0 on a machine that never renders, or without a camera. */
	float GetViewDistance(const FVector& Location) const;

	UPROPERTY(Transient)
	TArray<FRTSExhaustEntry> Entries;

	/** The local cameras this frame, kept between frames so gathering them does not allocate. */
	TArray<FVector> CameraLocations;
	/** The vehicles near enough to show exhaust this frame, by distance and entry, kept between frames too. */
	TArray<TPair<float, int32>> Nearest;
	TOptional<FVector> ViewLocationOverride;
	FRTSExhaustTickFunction TickFunction;
};
