#include "Player/RTSCameraPawn.h"

#include "Camera/CameraComponent.h"
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
	SpringArm->SetRelativeRotation(FRotator(Pitch, 0.f, 0.f));
	SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
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
}
