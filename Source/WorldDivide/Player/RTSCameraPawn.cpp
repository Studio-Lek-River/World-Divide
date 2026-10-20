#include "Player/RTSCameraPawn.h"

#include "Buildings/RTSBuilding.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Player/RTSCameraSettings.h"

namespace RTSCameraPawn
{
	/** The screen's corners as AddZoomInput's screen points: top left, top right, bottom right, bottom left. */
	const FVector2D ScreenCorners[] = { FVector2D(0.f, 0.f), FVector2D(1.f, 0.f), FVector2D(1.f, 1.f), FVector2D(0.f, 1.f) };

	/** Within this the arm and the yaw have reached what they ease to. */
	constexpr float ZoomSnap = 0.5f;
	constexpr float YawSnap = 0.05f;
	constexpr float GroundSnap = 1.f;
	/** A pan slower than this, in cm/s, with no input behind it, has stopped. */
	constexpr float PanStopSpeed = 1.f;
	/** A trace that hits a building again tries beneath it, this many times at most. */
	constexpr int32 GroundTraceAttempts = 4;
}

ARTSCameraPawn::ARTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	const URTSCameraSettings* Settings = GetDefault<URTSCameraSettings>();
	PanSpeed = Settings->PanSpeed;
	DefaultZoom = Settings->DefaultZoom;
	MinZoom = Settings->MinZoom;
	MaxZoom = Settings->MaxZoom;
	ZoomStepFraction = Settings->ZoomStepFraction;
	ZoomInterpSpeed = Settings->ZoomInterpSpeed;
	TopDownPitch = Settings->TopDownPitch;
	NearPitch = Settings->NearPitch;
	TiltStartZoom = Settings->TiltStartZoom;
	MinCameraHeight = Settings->MinCameraHeight;
	EdgeMargin = Settings->EdgeMargin;
	FallbackMapArea = Settings->FallbackMapArea;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = Settings->CameraLagSpeed;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	// Less perspective over a wide area than the engine's 90. Horizontal at 16:9; keeping the vertical field of view, a
	// wider screen sees more ground to the sides rather than less ahead.
	Camera->SetFieldOfView(Settings->FieldOfView);
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->SetAspectRatioAxisConstraint(Settings->bKeepVerticalFOV ? EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV
		: EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV);

	// Each client drives its own camera; the server copy just stays where it spawned.
	SetReplicatingMovement(false);
}

void ARTSCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	UpdatePanArea();
	TargetZoom = FMath::Clamp(DefaultZoom, MinZoom, MaxZoom);
	SpringArm->TargetArmLength = TargetZoom;
	SetYawNow(GetActorRotation().Yaw);
	JumpTo(GetActorLocation());
}

void ARTSCameraPawn::UpdatePanArea()
{
	// Every level setup_testmap.py builds has a NavMeshBoundsVolume over exactly its ground. Level actors load on every
	// machine, so a client's camera finds the same bounds as the host's.
	FBox2D MapArea(ForceInit);
	for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
	{
		const FBox Bounds = It->GetBounds().GetBox();
		MapArea += FBox2D(FVector2D(Bounds.Min), FVector2D(Bounds.Max));
	}
	PanArea = (MapArea.bIsValid ? MapArea : FallbackMapArea).ExpandBy(EdgeMargin);
}

FRTSCameraTuning ARTSCameraPawn::GetTuning() const
{
	FRTSCameraTuning Tuning;
	Tuning.bKeepVerticalFOV = Camera->bOverrideAspectRatioAxisConstraint
		&& Camera->AspectRatioAxisConstraint == EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
	Tuning.FieldOfView = Camera->FieldOfView;
	Tuning.PanSpeed = PanSpeed;
	Tuning.DefaultZoom = DefaultZoom;
	Tuning.MinZoom = MinZoom;
	Tuning.MaxZoom = MaxZoom;
	Tuning.ZoomStepFraction = ZoomStepFraction;
	Tuning.TiltStartZoom = TiltStartZoom;
	Tuning.EdgeMargin = EdgeMargin;
	Tuning.FallbackMapArea = FallbackMapArea;
	return Tuning;
}

void ARTSCameraPawn::ApplyTuning(const FRTSCameraTuning& Tuning)
{
	PanSpeed = Tuning.PanSpeed;
	DefaultZoom = Tuning.DefaultZoom;
	MinZoom = Tuning.MinZoom;
	MaxZoom = Tuning.MaxZoom;
	ZoomStepFraction = Tuning.ZoomStepFraction;
	TiltStartZoom = Tuning.TiltStartZoom;
	EdgeMargin = Tuning.EdgeMargin;
	FallbackMapArea = Tuning.FallbackMapArea;

	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->SetAspectRatioAxisConstraint(Tuning.bKeepVerticalFOV ? EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV
		: EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV);
	// Either way the field of view is the horizontal one at the camera's 16:9 aspect ratio; keeping the vertical widens it
	// on a wider screen.
	Camera->SetFieldOfView(Tuning.FieldOfView);

	UpdatePanArea();
	SetZoomNow(TargetZoom);
}

float ARTSCameraPawn::GetPitchForZoom(float ArmLength) const
{
	if (TiltStartZoom <= MinZoom + KINDA_SMALL_NUMBER || MinZoom <= KINDA_SMALL_NUMBER)
	{
		return TopDownPitch;
	}
	// Along the arm's logarithm, as the wheel zooms, so every notch over the tilt tilts the camera about as much.
	const float Alpha = FMath::Clamp(FMath::Loge(FMath::Max(ArmLength, MinZoom) / MinZoom) / FMath::Loge(TiltStartZoom / MinZoom), 0.f, 1.f);
	return FMath::Lerp(NearPitch, TopDownPitch, FMath::SmoothStep(0.f, 1.f, Alpha));
}

float ARTSCameraPawn::GetZoom() const
{
	return SpringArm->TargetArmLength;
}

FVector ARTSCameraPawn::GetCameraGoal() const
{
	const FVector Origin = SpringArm->GetComponentLocation() + SpringArm->TargetOffset;
	return Origin - SpringArm->GetComponentRotation().Vector() * SpringArm->TargetArmLength;
}

FRotator ARTSCameraPawn::GetCameraGoalRotation() const
{
	return SpringArm->GetComponentRotation();
}

FVector ARTSCameraPawn::GetScreenDirection(const FVector2D& ScreenPoint) const
{
	// The view's half-width and half-height at one unit ahead, as the engine's projection has them for the constraint.
	float ViewAspect = Camera->AspectRatio;
	if (const APlayerController* Player = GetController<APlayerController>())
	{
		int32 SizeX = 0;
		int32 SizeY = 0;
		Player->GetViewportSize(SizeX, SizeY);
		ViewAspect = SizeX > 0 && SizeY > 0 ? float(SizeX) / SizeY : ViewAspect;
	}
	const float HalfFOV = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView / 2.f));
	const bool bKeepVertical = Camera->bOverrideAspectRatioAxisConstraint
		&& Camera->AspectRatioAxisConstraint == EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
	const float TanY = bKeepVertical ? HalfFOV / Camera->AspectRatio : HalfFOV / ViewAspect;
	const float TanX = bKeepVertical ? TanY * ViewAspect : HalfFOV;
	const FVector Local(1.f, (2.f * ScreenPoint.X - 1.f) * TanX, (1.f - 2.f * ScreenPoint.Y) * TanY);
	return GetCameraGoalRotation().RotateVector(Local).GetSafeNormal();
}

FVector ARTSCameraPawn::GetGroundAlong(const FVector& Origin, const FVector& Direction, float Z, bool* bOutReached) const
{
	const float Reach = GetDefault<URTSCameraSettings>()->FootprintMaxDistance * FMath::Max(SpringArm->TargetArmLength, 1.f);
	if (Direction.Z < -KINDA_SMALL_NUMBER)
	{
		const float Along = (Z - Origin.Z) / Direction.Z;
		const FVector Ground = Origin + Direction * Along;
		if (Along >= 0.f && FVector2D::Distance(FVector2D(Ground), FVector2D(Origin)) <= Reach)
		{
			if (bOutReached)
			{
				*bOutReached = true;
			}
			return Ground;
		}
	}
	if (bOutReached)
	{
		*bOutReached = false;
	}
	// Towards the horizon: as far as the footprint reaches.
	return FVector(FVector2D(Origin) + FVector2D(Direction).GetSafeNormal() * Reach, Z);
}

TStaticArray<FVector, 4> ARTSCameraPawn::GetViewFootprint() const
{
	TStaticArray<FVector, 4> Corners;
	const FVector Origin = GetCameraGoal();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Corners[Index] = GetGroundAlong(Origin, GetScreenDirection(RTSCameraPawn::ScreenCorners[Index]), GetActorLocation().Z);
	}
	return Corners;
}

FBox2D ARTSCameraPawn::GetViewFootprintBounds() const
{
	FBox2D Bounds(ForceInit);
	for (const FVector& Corner : GetViewFootprint())
	{
		Bounds += FVector2D(Corner);
	}
	return Bounds;
}

bool ARTSCameraPawn::DeprojectToGround(const FVector2D& ScreenPoint, float Z, FVector& OutLocation) const
{
	bool bReached = false;
	OutLocation = GetGroundAlong(GetCameraGoal(), GetScreenDirection(ScreenPoint), Z, &bReached);
	return bReached;
}

TOptional<float> ARTSCameraPawn::TraceGround(const FVector& Location)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSCameraGround), false, this);
	const FVector Start(Location.X, Location.Y, Location.Z + MaxZoom);
	const FVector End(Location.X, Location.Y, Location.Z - MaxZoom);
	for (int32 Attempt = 0; Attempt < RTSCameraPawn::GroundTraceAttempts; ++Attempt)
	{
		++TraceCount;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			return {};
		}
		if (!Cast<ARTSBuilding>(Hit.GetActor()))
		{
			return float(Hit.ImpactPoint.Z);
		}
		Params.AddIgnoredActor(Hit.GetActor());
	}
	return {};
}

void ARTSCameraPawn::UpdateArm(bool bTrace)
{
	SpringArm->SetRelativeRotation(FRotator(GetPitchForZoom(SpringArm->TargetArmLength), 0.f, 0.f));
	if (!bTrace)
	{
		return;
	}

	// The pivot rests on the ground, so a near-horizontal arm puts the camera at or under ground level.
	// Lift the whole arm until the camera clears whatever is below it. Units ignore WorldStatic, so only
	// terrain and buildings lift it, never a unit driving past.
	SpringArm->TargetOffset = FVector::ZeroVector;
	const FVector Goal = GetCameraGoal();
	const FVector TraceStart(Goal.X, Goal.Y, Goal.Z + MaxZoom);
	const FVector TraceEnd(Goal.X, Goal.Y, Goal.Z - MaxZoom);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSCameraClearance), false, this);
	FHitResult Ground;
	++TraceCount;
	if (GetWorld()->LineTraceSingleByChannel(Ground, TraceStart, TraceEnd, ECC_WorldStatic, Params))
	{
		SpringArm->TargetOffset.Z = FMath::Max(0.f, Ground.ImpactPoint.Z + MinCameraHeight - Goal.Z);
	}
}

FVector ARTSCameraPawn::ClampToPanArea(const FVector& Location) const
{
	// The footprint moves with the pivot, so it is worked out once where the pawn stands and shifted to Location.
	const FBox2D View = GetViewFootprintBounds().ShiftBy(FVector2D(Location) - FVector2D(GetActorLocation()));
	FVector2D Offset = FVector2D::ZeroVector;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		if (View.Max[Axis] - View.Min[Axis] > PanArea.Max[Axis] - PanArea.Min[Axis])
		{
			Offset[Axis] = PanArea.GetCenter()[Axis] - View.GetCenter()[Axis];
		}
		else if (View.Min[Axis] < PanArea.Min[Axis])
		{
			Offset[Axis] = PanArea.Min[Axis] - View.Min[Axis];
		}
		else if (View.Max[Axis] > PanArea.Max[Axis])
		{
			Offset[Axis] = PanArea.Max[Axis] - View.Max[Axis];
		}
	}
	return Location + FVector(Offset, 0.f);
}

void ARTSCameraPawn::AddPanInput(const FVector2D& Input)
{
	PendingPan += Input;
}

void ARTSCameraPawn::AddZoomInput(float Steps)
{
	TargetZoom = FMath::Clamp(TargetZoom * FMath::Pow(1.f + ZoomStepFraction, -Steps), MinZoom, MaxZoom);
	ZoomFocus.Reset();
}

void ARTSCameraPawn::AddZoomInput(float Steps, const FVector2D& ScreenPoint)
{
	const float Before = TargetZoom;
	AddZoomInput(Steps);
	if (FMath::IsNearlyEqual(TargetZoom, Before))
	{
		return;
	}

	// One trace per wheel notch finds the ground under the cursor, hills included; over nothing, the pivot's plane.
	const FVector Origin = GetCameraGoal();
	const FVector Direction = GetScreenDirection(ScreenPoint);
	bool bReached = false;
	FVector Focus = GetGroundAlong(Origin, Direction, GetActorLocation().Z, &bReached);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSCameraZoomFocus), false, this);
	const float Reach = 2.f * GetDefault<URTSCameraSettings>()->FootprintMaxDistance * SpringArm->TargetArmLength;
	++TraceCount;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * Reach, ECC_WorldStatic, Params))
	{
		Focus = Hit.ImpactPoint;
		bReached = true;
	}
	if (bReached)
	{
		ZoomFocus = Focus;
		ZoomFocusDirection = GetCameraGoalRotation().UnrotateVector(Direction);
	}
}

void ARTSCameraPawn::ApplyZoomFocus()
{
	const FVector Origin = GetCameraGoal();
	const FVector Direction = GetCameraGoalRotation().RotateVector(ZoomFocusDirection);
	if (Direction.Z > -KINDA_SMALL_NUMBER || (ZoomFocus->Z - Origin.Z) / Direction.Z < 0.f)
	{
		// The cursor's ray looks over the horizon at this pitch; the arm will tilt it back down.
		return;
	}
	// Everything the arm shows moves with the pivot, so shifting the pivot by the miss puts the focus back under the cursor.
	const FVector Under = Origin + Direction * ((ZoomFocus->Z - Origin.Z) / Direction.Z);
	SetActorLocation(GetActorLocation() + FVector(FVector2D(*ZoomFocus) - FVector2D(Under), 0.f));
}

void ARTSCameraPawn::SetZoomNow(float ArmLength)
{
	TargetZoom = FMath::Clamp(ArmLength, MinZoom, MaxZoom);
	SpringArm->TargetArmLength = TargetZoom;
	ZoomFocus.Reset();
	UpdateArm(true);
	SetActorLocation(ClampToPanArea(GetActorLocation()));
	SettleTime = GetDefault<URTSCameraSettings>()->TraceSettleSeconds;
}

void ARTSCameraPawn::AddRotateInput(float Degrees)
{
	YawLeft += Degrees;
	ZoomFocus.Reset();
}

void ARTSCameraPawn::ResetYawToNorth()
{
	YawLeft = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, 0.f);
	ZoomFocus.Reset();
}

void ARTSCameraPawn::SetYawNow(float Yaw)
{
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	AppliedYaw = GetActorRotation().Yaw;
	YawLeft = 0.f;
	ZoomFocus.Reset();
	UpdateArm(true);
	SetActorLocation(ClampToPanArea(GetActorLocation()));
	SettleTime = GetDefault<URTSCameraSettings>()->TraceSettleSeconds;
}

bool ARTSCameraPawn::StartGrabPan(const FVector2D& ScreenPoint)
{
	bGrabPanning = DeprojectToGround(ScreenPoint, GetActorLocation().Z, GrabPoint);
	if (bGrabPanning)
	{
		ZoomFocus.Reset();
		PanVelocity = FVector2D::ZeroVector;
	}
	return bGrabPanning;
}

void ARTSCameraPawn::UpdateGrabPan(const FVector2D& ScreenPoint)
{
	FVector Under;
	if (!bGrabPanning || !DeprojectToGround(ScreenPoint, GrabPoint.Z, Under))
	{
		return;
	}
	// Measured from the camera goal, not the lagging camera, so the grabbed ground lands under the cursor without overshoot.
	const FVector2D Shift = FVector2D(GrabPoint) - FVector2D(Under);
	if (Shift.IsNearlyZero())
	{
		return;
	}
	SetActorLocation(ClampToPanArea(GetActorLocation() + FVector(Shift, 0.f)));
	SettleTime = GetDefault<URTSCameraSettings>()->TraceSettleSeconds;
}

void ARTSCameraPawn::StopGrabPan()
{
	bGrabPanning = false;
}

void ARTSCameraPawn::JumpTo(const FVector& Location)
{
	// The spring arm looks at the pawn, so moving the pawn centres the view; its lag glides the camera after it. The pivot
	// lands on the ground at once, so a scripted view is the same every time.
	FVector Target(Location.X, Location.Y, GetActorLocation().Z);
	GroundZ = TraceGround(Target);
	if (GroundZ)
	{
		Target.Z = *GroundZ;
	}
	SetActorLocation(Target);
	PendingPan = FVector2D::ZeroVector;
	PanVelocity = FVector2D::ZeroVector;
	ZoomFocus.Reset();
	UpdateArm(true);
	SetActorLocation(ClampToPanArea(GetActorLocation()));
	SettleTime = GetDefault<URTSCameraSettings>()->TraceSettleSeconds;
}

void ARTSCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const URTSCameraSettings* Settings = GetDefault<URTSCameraSettings>();
	bool bMoved = false;

	// A yaw set from elsewhere becomes the one to keep.
	const float Yaw = GetActorRotation().Yaw;
	if (!FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(AppliedYaw, Yaw), 0.01f))
	{
		AppliedYaw = Yaw;
		YawLeft = 0.f;
	}
	if (!FMath::IsNearlyZero(YawLeft, 0.01f))
	{
		const float Step = FMath::Abs(YawLeft) < RTSCameraPawn::YawSnap ? YawLeft : YawLeft * FMath::Min(1.f, DeltaSeconds * Settings->RotateEaseSpeed);
		YawLeft -= Step;
		SetActorRotation(FRotator(0.f, Yaw + Step, 0.f));
		AppliedYaw = GetActorRotation().Yaw;
		bMoved = true;
	}

	const float OldArm = SpringArm->TargetArmLength;
	float Arm = FMath::FInterpTo(OldArm, TargetZoom, DeltaSeconds, ZoomInterpSpeed);
	Arm = FMath::IsNearlyEqual(Arm, TargetZoom, RTSCameraPawn::ZoomSnap) ? TargetZoom : Arm;
	SpringArm->TargetArmLength = Arm;
	bMoved |= Arm != OldArm;

	FVector2D DesiredVelocity = FVector2D::ZeroVector;
	if (!PendingPan.IsNearlyZero())
	{
		const FRotator YawOnly(0.f, GetActorRotation().Yaw, 0.f);
		const FVector Forward = YawOnly.RotateVector(FVector::ForwardVector);
		const FVector Right = YawOnly.RotateVector(FVector::RightVector);
		const FVector Direction = (Forward * PendingPan.Y + Right * PendingPan.X).GetClampedToMaxSize(1.f);
		DesiredVelocity = FVector2D(Direction) * PanSpeed * Arm / DefaultZoom;
		ZoomFocus.Reset();
	}
	PendingPan = FVector2D::ZeroVector;
	PanVelocity = FMath::Vector2DInterpTo(PanVelocity, DesiredVelocity, DeltaSeconds, Settings->PanEaseSpeed);
	if (DesiredVelocity.IsZero() && PanVelocity.SizeSquared() < FMath::Square(RTSCameraPawn::PanStopSpeed))
	{
		PanVelocity = FVector2D::ZeroVector;
	}
	if (!PanVelocity.IsZero())
	{
		SetActorLocation(GetActorLocation() + FVector(PanVelocity * DeltaSeconds, 0.f));
		bMoved = true;
	}

	// The ground is checked only while the camera moves and for a moment after: a still camera traces nothing.
	if (bMoved)
	{
		SettleTime = Settings->TraceSettleSeconds;
	}
	const bool bTrace = SettleTime > 0.f;
	if (bTrace)
	{
		if (const TOptional<float> Ground = TraceGround(GetActorLocation()))
		{
			GroundZ = Ground;
		}
	}
	FVector Location = GetActorLocation();
	if (GroundZ && Location.Z != *GroundZ)
	{
		Location.Z = FMath::FInterpTo(Location.Z, *GroundZ, DeltaSeconds, Settings->GroundFollowSpeed);
		Location.Z = FMath::IsNearlyEqual(Location.Z, *GroundZ, RTSCameraPawn::GroundSnap) ? *GroundZ : Location.Z;
		SetActorLocation(Location);
		SettleTime = FMath::Max(SettleTime, Settings->TraceSettleSeconds);
	}

	UpdateArm(bTrace);
	if (ZoomFocus)
	{
		ApplyZoomFocus();
		if (Arm == TargetZoom)
		{
			ZoomFocus.Reset();
		}
	}
	if (bTrace)
	{
		SetActorLocation(ClampToPanArea(GetActorLocation()));
	}
	SettleTime -= DeltaSeconds;
}
