#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSBuildingLightSubsystem.generated.h"

class ARTSBuilding;
class URTSBuildingLightSubsystem;

/** Runs URTSBuildingLightSubsystem::UpdateAll once a frame, in TG_PostUpdateWork. */
struct FRTSBuildingLightTickFunction : public FTickFunction
{
	URTSBuildingLightSubsystem* Subsystem = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
};

/**
 * Every machine that draws: the night look of the buildings (#195). In the dark (URTSDayNightSubsystem::IsDark) every
 * registered building's lit windows and lamps glow at their night strength (ARTSBuilding::SetLightsNight), and of the
 * constructed ones with real lights at their lamps, the rts.Fx.BuildingLightMaxBuildings nearest a camera show them
 * (ARTSBuilding::ShowLights), and of those the kit lights flash, flicker and turn here, at most rts.Fx.BuildingLightMax of them
 * (ARTSBuilding::UpdateKitLights). By day every light is off and costs nothing. A dedicated server keeps none. It also makes
 * each building's one LOD decision for its LODSwitchDistance (ARTSBuilding::SetForcedLOD, BUILDING_KIT.md option C2).
 */
UCLASS()
class WORLDDIVIDE_API URTSBuildingLightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A building with parts or lights; ARTSBuilding::ApplyLights adds it. Adding one again sets it anew. */
	void Register(ARTSBuilding& Building);
	void Unregister(ARTSBuilding& Building);

	/** Switches every registered building for the dark and the cameras. Public for the tests. */
	void UpdateAll(float DeltaSeconds);

	/** The buildings whose real lights are shown now. */
	int32 GetShownBuildingCount() const;

	/** Makes the distance to the cameras measured from Location instead, or from the cameras again when unset. For the tests. */
	void SetViewLocationOverride(TOptional<FVector> Location) { ViewLocationOverride = Location; }

	/** rts.Fx.BuildingLightMaxBuildings: most buildings showing their real lights at once, those nearest a camera. */
	static int32 GetMaxBuildings();

	/** rts.Fx.BuildingLightMax: most real kit lights shown at once, the nearest buildings' first. */
	static int32 GetMaxKitLights();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	struct FEntry
	{
		TWeakObjectPtr<ARTSBuilding> Building;
		/** What the building was last told; unset until the first update after it registered. */
		TOptional<bool> bNight;
	};
	TArray<FEntry> Entries;

	/** The local cameras this frame and the buildings that may show, kept between frames so they do not allocate. */
	TArray<FVector> CameraLocations;
	TArray<TPair<float, int32>> Nearest;
	TOptional<FVector> ViewLocationOverride;
	FRTSBuildingLightTickFunction TickFunction;
};
