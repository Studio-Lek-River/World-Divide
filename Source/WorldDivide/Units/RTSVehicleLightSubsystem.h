#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSVehicleLightSubsystem.generated.h"

class ARTSUnit;
class USpotLightComponent;
class URTSVehicleLightSubsystem;

/** Runs URTSVehicleLightSubsystem::UpdateAll once a frame, in TG_PostUpdateWork. */
struct FRTSVehicleLightTickFunction : public FTickFunction
{
	URTSVehicleLightSubsystem* Subsystem = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
};

/** One vehicle with lights: whether its lamps were last told they are on, and its real lights, made when it first shows them. */
USTRUCT()
struct FRTSVehicleLightEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<ARTSUnit> Unit;

	/** The spot light at the middle of the headlights and the one at the middle of the tail lamps; null until first shown. */
	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> Headlight;

	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> Taillight;

	/** What the lamps were last told (RTSPrimitiveData::LampOn); unset until the first update after it registered. */
	TOptional<bool> bLampsOn;
	/** cm from the nearest camera this frame, whether it is among those that light, and whether its lights are on. */
	float ViewDistance = 0.f;
	bool bShows = false;
	bool bActive = false;
};

/**
 * Every machine that draws: the head- and rear lights of every vehicle whose data has lamps (#223), once a frame after every
 * hull has moved. In the dark (URTSDayNightSubsystem::IsDark) every such vehicle's lamps glow brighter, one primitive data
 * write per part when it changes; of them, only the rts.Fx.VehicleLightMaxVehicles nearest a camera within
 * rts.Fx.VehicleLightDistance also light the ground, through two spot lights each, so no more than twice that many real
 * lights are ever on. A dedicated server keeps none.
 */
UCLASS()
class WORLDDIVIDE_API URTSVehicleLightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A unit whose data has lights; ARTSUnit::CreateParts adds it. Adding one again makes its lights anew. */
	void Register(ARTSUnit& Unit);
	void Unregister(ARTSUnit& Unit);

	/** Switches every registered unit's lamps and lights for the dark and the cameras. Public for the tests. */
	void UpdateAll(float DeltaSeconds);

	/** The real lights on now, over every unit. */
	int32 GetActiveLightCount() const;

	/** The vehicles whose real lights are on now. */
	int32 GetActiveVehicleCount() const;

	/** The unit's spot lights, the headlight first; null until it first showed them. For the tests. */
	USpotLightComponent* GetHeadlight(const ARTSUnit& Unit) const;
	USpotLightComponent* GetTaillight(const ARTSUnit& Unit) const;

	/** Makes the distance to the cameras measured from Location instead, or from the cameras again when unset. For the tests. */
	void SetViewLocationOverride(TOptional<FVector> Location) { ViewLocationOverride = Location; }

	/** rts.Fx.VehicleLightDistance: cm from the nearest camera within which a vehicle lights the ground; 0 or less everywhere. */
	static float GetDistance();

	/** rts.Fx.VehicleLightMaxVehicles: most vehicles lighting the ground at once, the nearest a camera. */
	static int32 GetMaxVehicles();

	/** The game thread seconds UpdateAll took since the last call, for the bench. */
	static double TakeSeconds();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	FRTSVehicleLightEntry* Find(const ARTSUnit& Unit);
	const FRTSVehicleLightEntry* Find(const ARTSUnit& Unit) const;
	void DestroyLights(FRTSVehicleLightEntry& Entry);
	/** Makes the entry's spot lights, attached to the hull at the middle of its lamps, if it has none yet. */
	void MakeLights(FRTSVehicleLightEntry& Entry, ARTSUnit& Unit);
	void SetActive(FRTSVehicleLightEntry& Entry, bool bActive);

	UPROPERTY(Transient)
	TArray<FRTSVehicleLightEntry> Entries;

	/** The local cameras this frame and the vehicles near enough to light, kept between frames so they do not allocate. */
	TArray<FVector> CameraLocations;
	TArray<TPair<float, int32>> Nearest;
	TOptional<FVector> ViewLocationOverride;
	FRTSVehicleLightTickFunction TickFunction;
};
