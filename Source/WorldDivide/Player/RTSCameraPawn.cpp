#include "Player/RTSCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

ARTSCameraPawn::ARTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.f;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	// Each client drives its own camera; the server copy just stays where it spawned.
	SetReplicatingMovement(false);
}

void ARTSCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	TargetZoom = FMath::Clamp(DefaultZoom, MinZoom, MaxZoom);
	SpringArm->TargetArmLength = TargetZoom;
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
	UpdateArm();
}

float ARTSCameraPawn::GetPitchForZoom(float ArmLength) const
{
	const float TiltRange = TiltStartZoom - MinZoom;
	if (TiltRange <= KINDA_SMALL_NUMBER)
	{
		return TopDownPitch;
	}
	const float Alpha = FMath::Clamp((ArmLength - MinZoom) / TiltRange, 0.f, 1.f);
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

void ARTSCameraPawn::UpdateArm()
{
	SpringArm->SetRelativeRotation(FRotator(GetPitchForZoom(SpringArm->TargetArmLength), 0.f, 0.f));

	// The pivot rests on the ground, so a near-horizontal arm puts the camera at or under ground level.
	// Lift the whole arm until the camera clears whatever is below it. Units ignore WorldStatic, so only
	// terrain and buildings lift it, never a unit driving past.
	SpringArm->TargetOffset = FVector::ZeroVector;
	const FVector Goal = GetCameraGoal();
	const FVector TraceStart(Goal.X, Goal.Y, Goal.Z + MaxZoom);
	const FVector TraceEnd(Goal.X, Goal.Y, Goal.Z - MaxZoom);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSCameraClearance), false, this);
	FHitResult Ground;
	if (GetWorld()->LineTraceSingleByChannel(Ground, TraceStart, TraceEnd, ECC_WorldStatic, Params))
	{
		SpringArm->TargetOffset.Z = FMath::Max(0.f, Ground.ImpactPoint.Z + MinCameraHeight - Goal.Z);
	}
}

void ARTSCameraPawn::AddPanInput(const FVector2D& Input)
{
	PendingPan += Input;
}

void ARTSCameraPawn::AddZoomInput(float Steps)
{
	TargetZoom = FMath::Clamp(TargetZoom - Steps * ZoomStep, MinZoom, MaxZoom);
}

void ARTSCameraPawn::AddRotateInput(float Degrees)
{
	PendingYaw += Degrees;
}

void ARTSCameraPawn::JumpTo(const FVector& Location)
{
	// The spring arm looks at the pawn, so moving the pawn centres the view; its lag glides the camera after it.
	const FVector2D Flat = FVector2D(Location).GetClampedToMaxSize(MaxDistanceFromOrigin);
	SetActorLocation(FVector(Flat.X, Flat.Y, GetActorLocation().Z));
	PendingPan = FVector2D::ZeroVector;
}

void ARTSCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetZoom, DeltaSeconds, ZoomInterpSpeed);

	if (!FMath::IsNearlyZero(PendingYaw))
	{
		AddActorWorldRotation(FRotator(0.f, PendingYaw, 0.f));
		PendingYaw = 0.f;
	}

	if (!PendingPan.IsNearlyZero())
	{
		const FRotator YawOnly(0.f, GetActorRotation().Yaw, 0.f);
		const FVector Forward = YawOnly.RotateVector(FVector::ForwardVector);
		const FVector Right = YawOnly.RotateVector(FVector::RightVector);
		const FVector Direction = (Forward * PendingPan.Y + Right * PendingPan.X).GetClampedToMaxSize(1.f);
		const float ZoomScale = SpringArm->TargetArmLength / DefaultZoom;

		FVector NewLocation = GetActorLocation() + Direction * PanSpeed * ZoomScale * DeltaSeconds;
		const FVector2D Flat(NewLocation);
		if (Flat.Size() > MaxDistanceFromOrigin)
		{
			const FVector2D Clamped = Flat.GetSafeNormal() * MaxDistanceFromOrigin;
			NewLocation.X = Clamped.X;
			NewLocation.Y = Clamped.Y;
		}
		SetActorLocation(NewLocation);
		PendingPan = FVector2D::ZeroVector;
	}

	UpdateArm();
}
