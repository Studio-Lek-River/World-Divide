#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RTSCameraSettings.generated.h"

/**
 * The RTS camera's tuning (#295), kept in DefaultGame.ini so it stays reviewable text. ARTSCameraPawn and
 * ARTSPlayerController read it; rts.Debug.CameraShots overrides a pawn's copy through FRTSCameraTuning.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "World Divide Camera"))
class WORLDDIVIDE_API URTSCameraSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Horizontal field of view at 16:9, in degrees. */
	UPROPERTY(Config, EditAnywhere, Category = "View", meta = (ClampMin = "20", ClampMax = "120"))
	float FieldOfView = 60.f;

	/** Keeps the vertical field of view on other aspect ratios, so a wider screen sees more ground to the sides. */
	UPROPERTY(Config, EditAnywhere, Category = "View")
	bool bKeepVerticalFOV = true;

	/** About 175 m of ground across the screen at the camera's 60 degrees: an MBT is about 6 % of its width. */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom")
	float DefaultZoom = 15000.f;

	/** Close inspection of a single vehicle: about 14 m of ground across at NearPitch (#339). */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom")
	float MinZoom = 1200.f;

	/** About 520 m across: a whole base, its approaches and the middle of the map. */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom")
	float MaxZoom = 45000.f;

	/** How much of the arm's length one wheel notch zooms, so a notch feels the same at every height. */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom", meta = (ClampMin = "0.01", ClampMax = "0.9"))
	float ZoomStepFraction = 0.2f;

	/** How fast the arm eases to the zoom asked for, per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom")
	float ZoomInterpSpeed = 10.f;

	/** Pitch at TiltStartZoom and further out. */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom", meta = (ClampMin = "-89", ClampMax = "-10"))
	float TopDownPitch = -55.f;

	/** Pitch at MinZoom: still looking down on the ground, with no sky in the picture (#339, producer 2026-10-20). */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom", meta = (ClampMin = "-89", ClampMax = "0"))
	float NearPitch = -35.f;

	/** Arm length below which the camera starts tilting from TopDownPitch towards NearPitch. */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom")
	float TiltStartZoom = 6000.f;

	/** How fast the camera's lag catches up with where the arm puts it, per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Zoom")
	float CameraLagSpeed = 12.f;

	/** cm/s at DefaultZoom; scales with zoom so panning feels the same at every height. */
	UPROPERTY(Config, EditAnywhere, Category = "Pan")
	float PanSpeed = 8500.f;

	/** How fast keyboard and edge panning ease in and out, per second: higher starts and stops crisper. */
	UPROPERTY(Config, EditAnywhere, Category = "Pan", meta = (ClampMin = "0.5"))
	float PanEaseSpeed = 8.f;

	/** Pixels from the viewport edge where the camera starts to scroll. */
	UPROPERTY(Config, EditAnywhere, Category = "Pan")
	float EdgeScrollMargin = 12.f;

	/** How far past the edge of the map the view may go. */
	UPROPERTY(Config, EditAnywhere, Category = "Bounds")
	float EdgeMargin = 2000.f;

	/** The map's ground when its level has no NavMeshBoundsVolume, which otherwise covers the ground. */
	UPROPERTY(Config, EditAnywhere, Category = "Bounds")
	FBox2D FallbackMapArea = FBox2D(FVector2D(-50000.0), FVector2D(50000.0));

	/**
	 * How far from the camera, in arm lengths along the ground, the view footprint reaches at most: the part of a near,
	 * flat view towards the horizon that the bounds ignore.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Bounds", meta = (ClampMin = "1"))
	float FootprintMaxDistance = 2.f;

	/** The camera stays at least this far above the ground or building under it. */
	UPROPERTY(Config, EditAnywhere, Category = "Ground")
	float MinCameraHeight = 200.f;

	/** How fast the pivot eases to the height of the ground under it, per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Ground")
	float GroundFollowSpeed = 6.f;

	/** Seconds the camera keeps checking the ground after it last moved; when still, it traces nothing. */
	UPROPERTY(Config, EditAnywhere, Category = "Ground")
	float TraceSettleSeconds = 0.5f;

	/** How fast the yaw eases to the one asked for, per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Rotation")
	float RotateEaseSpeed = 8.f;

	/** Degrees per second while Q or E is held. */
	UPROPERTY(Config, EditAnywhere, Category = "Rotation")
	float KeyRotateSpeed = 90.f;

	/** Degrees of yaw per pixel of Alt + middle-mouse drag. */
	UPROPERTY(Config, EditAnywhere, Category = "Rotation")
	float DragRotateSpeed = 0.3f;
};
