#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RTSCameraPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

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

	/** Positive zooms in. One mouse-wheel notch is 1. */
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
	float PanSpeed = 3000.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float DefaultZoom = 3000.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinZoom = 175.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxZoom = 7000.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomStep = 400.f;

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
	float TiltStartZoom = 3000.f;

	/** The camera stays at least this far above the ground or building under it. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinCameraHeight = 200.f;

	/** How far past the edge of the map the pawn may go. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float EdgeMargin = 500.f;

	/** The map's ground when its level has no NavMeshBoundsVolume, which otherwise covers the ground. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FBox2D FallbackMapArea = FBox2D(FVector2D(-15000.0), FVector2D(15000.0));

private:
	/** Tilts the arm to match its current length and lifts it clear of the ground or building under the camera. */
	void UpdateArm();

	/** Location with its X and Y moved into PanArea. */
	FVector ClampToPanArea(const FVector& Location) const;

	FBox2D PanArea = FBox2D(FVector2D(-15000.0), FVector2D(15000.0));

	FVector2D PendingPan = FVector2D::ZeroVector;
	float PendingYaw = 0.f;
	float TargetZoom = 0.f;
};
