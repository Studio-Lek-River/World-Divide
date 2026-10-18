#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSRunningGearSubsystem.generated.h"

class ARTSUnit;
class URTSRunningGearSubsystem;

/** Runs URTSRunningGearSubsystem::PlaceAll once a frame, in TG_PostUpdateWork. */
struct FRTSRunningGearTickFunction : public FTickFunction
{
	URTSRunningGearSubsystem* Subsystem = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
};

/**
 * Every machine. Rolls and places the wheels and moves the tracks' links of every vehicle once a frame
 * (ARTSUnit::UpdateRunningGear), after everything that moves a hull: the units' own ticks, and avoidance's pushes, which run
 * later as a tickable. The wheels are placed in world space rather than carried by the hull's move (#165), so they must not
 * be placed before the hull's last move.
 */
UCLASS()
class WORLDDIVIDE_API URTSRunningGearSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A unit with wheels or tracks; ARTSUnit::CreateParts adds it. Adding one twice keeps one entry. */
	void Register(ARTSUnit& Unit);
	void Unregister(ARTSUnit& Unit);

	/** Places the wheels and moves the tracks of every registered unit. Public so tests can step it. */
	void PlaceAll();

	/** rts.Vehicle.TrackAnimation: tracks move their links (#151), read when a unit makes its parts. */
	static bool AnimatesTracks();

	/** rts.Vehicle.TrackAnimationDistance: cm from the camera past which the renderer leaves the links' shift out; 0 never. */
	static float GetTrackAnimationDistance();

	/** The game thread seconds PlaceAll took since the last call, for the bench. */
	static double TakeSeconds();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	TArray<TWeakObjectPtr<ARTSUnit>> Units;
	FRTSRunningGearTickFunction TickFunction;
};
