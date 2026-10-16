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
 * Every machine. Rolls and places the wheels of every vehicle once a frame (ARTSUnit::UpdateRunningGear), after everything
 * that moves a hull: the units' own ticks, and avoidance's pushes, which run later as a tickable. The wheels are placed in
 * world space rather than carried by the hull's move (#165), so they must not be placed before the hull's last move.
 */
UCLASS()
class WORLDDIVIDE_API URTSRunningGearSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A unit with wheels; ARTSUnit::CreateParts adds it. Adding one twice keeps one entry. */
	void Register(ARTSUnit& Unit);
	void Unregister(ARTSUnit& Unit);

	/** Places the wheels of every registered unit. Public so tests can step it. */
	void PlaceAll();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	TArray<TWeakObjectPtr<ARTSUnit>> Units;
	FRTSRunningGearTickFunction TickFunction;
};
