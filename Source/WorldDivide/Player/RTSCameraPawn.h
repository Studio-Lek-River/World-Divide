#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "GameFramework/Pawn.h"
#include "RTSCameraPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

/**
 * The camera's tuning as one set, for scripted comparisons of camera settings (rts.Debug.CameraShots). A pawn starts with
 * URTSCameraSettings' values; ApplyTuning overrides its copy.
 */
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
 * Top-down RTS camera. The pawn's pivot rests on the ground and eases to its height; a spring arm holds the camera above
 * and behind it. The wheel zooms towards the ground under the cursor, the yaw eases to the one asked for, panning eases in
 * and out, and the ground the view shows, not the pivot, stays within the map. It only checks the ground while it moves.
 * Input is gathered by ARTSPlayerController and applied here each tick. Purely local: it never replicates movement.
 */
UCLASS()
class WORLDDIVIDE_API ARTSCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ARTSCameraPawn();

	/** X = right, Y = forward, each in [-1, 1]. Accumulates until the next tick, which eases the pan's speed towards it. */
	void AddPanInput(const FVector2D& Input);

	/** Positive zooms in. One mouse-wheel notch is 1 and changes the arm by ZoomStepFraction of its length. */
	void AddZoomInput(float Steps);

	/**
	 * Zooms towards the ground under ScreenPoint, the view's width and height as 0 to 1 from its top left: the ground
	 * there stays under that point while the arm eases. Over the sky it zooms on the screen's centre.
	 */
	void AddZoomInput(float Steps, const FVector2D& ScreenPoint);

	/** Degrees of yaw added to the yaw the camera eases to. */
	void AddRotateInput(float Degrees);

	/** Eases the yaw to north (0) the short way round, keeping the zoom and the place. */
	void ResetYawToNorth();

	/** Sets the yaw at once, without easing to it; for scripted views. */
	void SetYawNow(float Yaw);

	/** The yaw the camera eases to, unwrapped: past 180 degrees from the current yaw when the input turned that far. */
	float GetTargetYaw() const { return GetActorRotation().Yaw + YawLeft; }

	/** Grabs the ground under ScreenPoint (as AddZoomInput's): UpdateGrabPan then keeps it under the cursor. False over the sky. */
	bool StartGrabPan(const FVector2D& ScreenPoint);
	void UpdateGrabPan(const FVector2D& ScreenPoint);
	void StopGrabPan();
	bool IsGrabPanning() const { return bGrabPanning; }

	/**
	 * Centres the view on Location's ground position at once, with the pivot on the ground there, within the same bounds
	 * as panning. Instant, so scripted views are the same every time.
	 */
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

	/** The camera's rotation once the spring arm's lag has caught up. */
	FRotator GetCameraGoalRotation() const;

	/**
	 * The ground the view shows from the camera goal: the screen's top left, top right, bottom right and bottom left
	 * corners on the plane at the pivot's height, each at most FootprintMaxDistance arm lengths from the camera along the
	 * ground.
	 */
	TStaticArray<FVector, 4> GetViewFootprint() const;

	/** The XY bounds of GetViewFootprint: what the pan area keeps inside it. */
	FBox2D GetViewFootprintBounds() const;

	/**
	 * Where the view ray through ScreenPoint (as AddZoomInput's) meets the plane at Z; false above the horizon or past the
	 * footprint's reach.
	 */
	bool DeprojectToGround(const FVector2D& ScreenPoint, float Z, FVector& OutLocation) const;

	/** The world XY rectangle the view stays in: the map's ground plus EdgeMargin. Set when play begins. */
	const FBox2D& GetPanArea() const { return PanArea; }

	/** How many ground traces the camera has made; it makes none while it stands still (PERFORMANCE_AUDIT.md). */
	int32 GetTraceCount() const { return TraceCount; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	// This pawn's copy of URTSCameraSettings, which rts.Debug.CameraShots overrides through ApplyTuning. Transient: the
	// settings are the one place the tuning is kept.

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float PanSpeed = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float DefaultZoom = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float MinZoom = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float MaxZoom = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float ZoomStepFraction = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float ZoomInterpSpeed = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float TopDownPitch = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float NearPitch = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float TiltStartZoom = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float MinCameraHeight = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	float EdgeMargin = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Camera")
	FBox2D FallbackMapArea = FBox2D(ForceInit);

private:
	/** PanArea from the level's NavMeshBoundsVolumes, or FallbackMapArea, plus EdgeMargin. */
	void UpdatePanArea();

	/**
	 * Tilts the arm to match its current length; with bTrace also lifts it clear of the ground or building under the
	 * camera, else it keeps the last lift.
	 */
	void UpdateArm(bool bTrace);

	/** The ground's height under Location, ignoring buildings so the pivot never climbs a roof; unset over nothing. */
	TOptional<float> TraceGround(const FVector& Location);

	/** Checks the ground again when a building appears, so a still camera lifts over it. */
	void OnActorSpawned(AActor* Actor);

	/**
	 * Location moved so the view footprint stays within PanArea's extent along the camera's right and forward axes, or
	 * centred on it along an axis where it is larger, and so the pivot, the ground at the screen's centre, stays within
	 * PanArea itself.
	 */
	FVector ClampToPanArea(const FVector& Location) const;

	/** The view ray through ScreenPoint from the camera goal. */
	FVector GetScreenDirection(const FVector2D& ScreenPoint) const;

	/** Where the ray from Origin along Direction meets the plane at Z, at most the footprint's reach along the ground. */
	FVector GetGroundAlong(const FVector& Origin, const FVector& Direction, float Z, bool* bOutReached = nullptr) const;

	/** Moves the pivot so the zoom's focus stays under its screen point after the arm moved. */
	void ApplyZoomFocus();

	FBox2D PanArea = FBox2D(FVector2D(-15000.0), FVector2D(15000.0));

	FVector2D PendingPan = FVector2D::ZeroVector;
	/** World XY, cm/s. */
	FVector2D PanVelocity = FVector2D::ZeroVector;
	float TargetZoom = 0.f;
	/**
	 * Degrees still to turn, unwrapped, so a turn of more than 180 degrees keeps going the way the input went instead of
	 * the short way back.
	 */
	float YawLeft = 0.f;
	/** The yaw this pawn last set, to notice a SetActorRotation from elsewhere and take it as the new target. */
	float AppliedYaw = 0.f;

	/** The ground point the zoom keeps under the cursor, and the cursor's view ray in the camera's own space. */
	TOptional<FVector> ZoomFocus;
	FVector ZoomFocusDirection = FVector::ForwardVector;

	bool bGrabPanning = false;
	FVector GrabPoint = FVector::ZeroVector;

	/** The ground height the pivot eases to, from the last trace. */
	TOptional<float> GroundZ;
	/** Seconds left to keep tracing after the camera last moved. */
	float SettleTime = 0.f;
	int32 TraceCount = 0;
	FDelegateHandle BuildingSpawnedHandle;
};
