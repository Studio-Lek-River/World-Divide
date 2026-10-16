#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RTSCameraPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

/** The camera's tuning as one set, for scripted comparisons of camera settings (rts.Debug.CameraShots). */
struct FRTSCameraTuning
{
	/** Horizontal field of view at 16:9, in degrees. */
	float FieldOfView = 60.f;
	float PanSpeed = 0.f;
	float DefaultZoom = 0.f;
	float MinZoom = 0.f;
	float MaxZoom = 0.f;
	float ZoomStepFraction = 0.f;
	float TiltStartZoom = 0.f;
	float EdgeMargin = 0.f;
	FBox2D FallbackMapArea = FBox2D(ForceInit);
	/** Keeps the vertical field of view on other aspect ratios, so a wider screen sees more ground to the sides. */
	bool bKeepVerticalFOV = false;
};

/**
 * Top-down RTS camera. The pawn sits on the ground; a spring arm holds the camera above and behind it.
 * Input is gathered by ARTSPlayerController and applied here each tick. Purely local: it never replicates movement.
 */
UCLASS()
class WORLDDIVIDE_API ARTSCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ARTSCameraPawn();

	/** X = right, Y = forward, each in [-1, 1]. Accumulates until the next tick. */
	void AddPanInput(const FVector2D& Input);

	/** Positive zooms in. One mouse-wheel notch is 1 and changes the arm by ZoomStepFraction of its length. */
	void AddZoomInput(float Steps);

	/** Degrees of yaw. */
	void AddRotateInput(float Degrees);

	/** Centres the view on Location's ground position at once, within the same bounds as panning. */
	void JumpTo(const FVector& Location);

	/** Spring-arm pitch for an arm length: TopDownPitch from TiltStartZoom up, easing to NearPitch at MinZoom. */
	float GetPitchForZoom(float ArmLength) const;

	/** Current arm length, which eases towards the zoom the input asked for. */
	float GetZoom() const;

	/** Sets the zoom at once, within MinZoom and MaxZoom, without easing to it; for scripted views. */
	void SetZoomNow(float ArmLength);

	float GetMinZoom() const { return MinZoom; }
	float GetMaxZoom() const { return MaxZoom; }
	float GetDefaultZoom() const { return DefaultZoom; }

	UCameraComponent* GetCameraComponent() const { return Camera; }

	FRTSCameraTuning GetTuning() const;

	/** Takes Tuning's settings at once: the field of view, the pan area and the zoom, kept within the new limits. */
	void ApplyTuning(const FRTSCameraTuning& Tuning);

	/** Where the camera settles once the spring arm's lag has caught up. */
	FVector GetCameraGoal() const;

	/** The world XY rectangle the pawn stays in: the map's ground plus EdgeMargin. Set when play begins. */
	const FBox2D& GetPanArea() const { return PanArea; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	/** cm/s at DefaultZoom; scales with zoom so panning feels the same at every height. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float PanSpeed = 8500.f;

	/** About 175 m of ground across the screen at the camera's 60 degrees: an MBT is about 6 % of its width. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float DefaultZoom = 15000.f;

	/** Close inspection of a single vehicle, the camera just outside its hull. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinZoom = 600.f;

	/** About 520 m across: a whole base, its approaches and the middle of the map. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxZoom = 45000.f;

	/** How much of the arm's length one wheel notch zooms, so a notch feels the same at every height. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (ClampMin = "0.01", ClampMax = "0.9"))
	float ZoomStepFraction = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomInterpSpeed = 10.f;

	/** Pitch at TiltStartZoom and further out. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (ClampMin = "-89", ClampMax = "-10"))
	float TopDownPitch = -55.f;

	/** Pitch at MinZoom: close to the horizon, as seen from the ground. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (ClampMin = "-89", ClampMax = "0"))
	float NearPitch = -8.f;

	/** Arm length below which the camera starts tilting from TopDownPitch towards NearPitch. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float TiltStartZoom = 6000.f;

	/** The camera stays at least this far above the ground or building under it. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinCameraHeight = 200.f;

	/** How far past the edge of the map the pawn may go. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float EdgeMargin = 2000.f;

	/** The map's ground when its level has no NavMeshBoundsVolume, which otherwise covers the ground. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FBox2D FallbackMapArea = FBox2D(FVector2D(-50000.0), FVector2D(50000.0));

private:
	/** PanArea from the level's NavMeshBoundsVolumes, or FallbackMapArea, plus EdgeMargin. */
	void UpdatePanArea();

	/** Tilts the arm to match its current length and lifts it clear of the ground or building under the camera. */
	void UpdateArm();

	/** Location with its X and Y moved into PanArea. */
	FVector ClampToPanArea(const FVector& Location) const;

	FBox2D PanArea = FBox2D(FVector2D(-15000.0), FVector2D(15000.0));

	FVector2D PendingPan = FVector2D::ZeroVector;
	float PendingYaw = 0.f;
	float TargetZoom = 0.f;
};
