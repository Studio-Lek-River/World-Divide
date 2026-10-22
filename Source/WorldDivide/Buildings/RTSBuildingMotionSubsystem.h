#pragma once

#include "CoreMinimal.h"
#include "Data/RTSBuildingData.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSBuildingMotionSubsystem.generated.h"

class ARTSBuilding;
class UStaticMeshComponent;
class URTSBuildingMotionSubsystem;

/** Runs URTSBuildingMotionSubsystem::UpdateAll once a frame, in TG_PostUpdateWork. */
struct FRTSBuildingMotionTickFunction : public FTickFunction
{
	URTSBuildingMotionSubsystem* Subsystem = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
};

/** One part of a building that turns on its own: where it rests and how far it has turned. */
struct FRTSMovingPart
{
	TWeakObjectPtr<UStaticMeshComponent> Component;
	/** Its relative transform at rest, in the building's frame (Mesh's). */
	FTransform Rest;
	FRTSPartMotion Motion;
	float MaxDrawDistance = 0.f;
	/** The entry's part it rides on (FRTSPartMotion::FollowPart), INDEX_NONE for none. */
	int32 Follow = INDEX_NONE;
	/** The building's own share of the part's rates (FRTSPartMotion::RateJitter). */
	float RateScale = 1.f;
	/** Degrees turned so far about each axis, and the share of the sweep's period gone by, both wrapped. */
	FRotator Spin = FRotator::ZeroRotator;
	float SweepPhase = 0.f;
	/** This frame's turn about its own pivot, in its own frame. */
	FTransform Turn;
};

/** One building with moving parts. */
struct FRTSMotionEntry
{
	TWeakObjectPtr<ARTSBuilding> Building;
	TArray<FRTSMovingPart> Parts;
	/** The motion scale the parts run at, easing towards the building's (MotionScaleRate); below zero until the first frame. */
	float Scale = -1.f;
};

/**
 * Every machine that draws: the parts of every building whose data gives them a motion (FRTSPartMotion, #351), such as the
 * HQ's fans, radar and wing dish, turned once a frame about their pivots, in place of a tick per building. A part is posed
 * only while its building was drawn lately and a camera is within its draw distance; the angles run on regardless, at the
 * building's equipment motion scale (ARTSBuilding::SetEquipmentMotionScale), eased towards it at MotionScaleRate, so the
 * parts spin down and up again rather than stop dead. A dedicated server keeps none.
 */
UCLASS()
class WORLDDIVIDE_API URTSBuildingMotionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A building whose parts were just made (ARTSBuilding::ApplyParts); again replaces its entry, none of them moving drops it. */
	void Register(ARTSBuilding& Building);
	void Unregister(ARTSBuilding& Building);

	/** Turns every registered part DeltaSeconds on and poses those in view. Public for the tests. */
	void UpdateAll(float DeltaSeconds);

	/** How fast a building's parts follow a change of its equipment motion scale, per second. */
	static constexpr float MotionScaleRate = 0.5f;

	/** The building's moving parts, 0 for one not registered. For the tests. */
	int32 GetMovingPartCount(const ARTSBuilding& Building) const;

	/** Poses parts whether drawn or not, as if a camera stood at the building. For the tests, which never draw. */
	void SetPoseUnseen(bool bInPoseUnseen) { bPoseUnseen = bInPoseUnseen; }

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	/** Advances the part's angles by DeltaSeconds at Scale and works out its turn about its own pivot. */
	static void Advance(FRTSMovingPart& Part, float DeltaSeconds, float Scale);

	/** The part's relative transform now: its turn, its rest, and the turn of the part it rides on, about that part's pivot. */
	static FTransform GetPose(const FRTSMotionEntry& Entry, const FRTSMovingPart& Part);

	TArray<FRTSMotionEntry> Entries;

	/** The local cameras this frame, kept between frames so gathering them does not allocate. */
	TArray<FVector> CameraLocations;
	bool bPoseUnseen = false;
	FRTSBuildingMotionTickFunction TickFunction;
};
