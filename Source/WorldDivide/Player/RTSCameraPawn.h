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
	float MinZoom = 800.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxZoom = 7000.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomStep = 400.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomInterpSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "Camera", meta = (ClampMin = "-89", ClampMax = "-10"))
	float Pitch = -55.f;

	/** The pawn may not wander further than this from the world origin. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxDistanceFromOrigin = 15000.f;

private:
	FVector2D PendingPan = FVector2D::ZeroVector;
	float PendingYaw = 0.f;
	float TargetZoom = 0.f;
};
