#include "Combat/RTSShotEffect.h"

#include "Combat/RTSShotSound.h"
#include "Combat/RTSTargetable.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSSettings.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FName ShellColorParameter(TEXT("Color"));

	constexpr float MuzzleFlashSeconds = 0.08f;
	constexpr float ImpactSeconds = 0.25f;

	/** Emissive strength at full brightness, well into bloom. */
	constexpr float GlowStrength = 8.f;

	/** The effect mesh is a sphere this many cm across. */
	constexpr float MeshDiameter = 100.f;

	/** Sizes in cm at an effect scale of 1. */
	constexpr float MuzzleFlashSize = 60.f;
	constexpr float ShellThickness = 12.f;
	constexpr float ImpactStartSize = 40.f;
	constexpr float ImpactEndSize = 170.f;

	/** A fast shell is drawn longer, as a streak: this many cm per cm/s of speed. */
	constexpr float ShellStreakPerSpeed = 0.015f;

	constexpr float LightIntensity = 12.f;
	constexpr float LightRadius = 450.f;

	/** Light weapons make no sound where they hit; heavy ones blast. */
	constexpr float ImpactSoundMinScale = 0.8f;
}

ARTSShotEffect::ARTSShotEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	MuzzleFlash = CreateGlow(TEXT("MuzzleFlash"));
	MuzzleLight = CreateLight(TEXT("MuzzleLight"));
	Shell = CreateGlow(TEXT("Shell"));
	ImpactFlash = CreateGlow(TEXT("ImpactFlash"));
	ImpactLight = CreateLight(TEXT("ImpactLight"));

	bReplicates = false;
	SetActorEnableCollision(false);
}

UStaticMeshComponent* ARTSShotEffect::CreateGlow(FName Name)
{
	UStaticMeshComponent* Glow = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Glow->SetupAttachment(RootComponent);
	Glow->SetUsingAbsoluteLocation(true);
	Glow->SetUsingAbsoluteRotation(true);
	Glow->SetUsingAbsoluteScale(true);
	Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Glow->SetCanEverAffectNavigation(false);
	Glow->SetCastShadow(false);
	Glow->SetVisibility(false);
	return Glow;
}

UPointLightComponent* ARTSShotEffect::CreateLight(FName Name)
{
	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Name);
	Light->SetupAttachment(RootComponent);
	Light->SetUsingAbsoluteLocation(true);
	Light->SetCastShadows(false);
	Light->bUseInverseSquaredFalloff = false;
	Light->SetLightFalloffExponent(2.f);
	Light->SetIntensity(0.f);
	Light->SetVisibility(false);
	return Light;
}

ARTSShotEffect* ARTSShotEffect::Spawn(AActor& Shooter, const URTSWeaponData& Weapon, AActor* Target, const FVector& TargetLocation,
	float FlightTime)
{
	UWorld* World = Shooter.GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	const FVector AimPoint = Target ? GetAimPoint(*Target, Shooter.GetActorLocation()) : TargetLocation;
	const FVector Muzzle = GetMuzzlePoint(Shooter, AimPoint);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARTSShotEffect* Effect = World->SpawnActor<ARTSShotEffect>(Muzzle, FRotator::ZeroRotator, Params);
	if (Effect)
	{
		Effect->Init(Weapon, Target, Muzzle, AimPoint, FlightTime);
	}
	return Effect;
}

void ARTSShotEffect::Init(const URTSWeaponData& InWeapon, AActor* InTarget, const FVector& InMuzzle, const FVector& TargetLocation,
	float InFlightTime)
{
	Weapon = &InWeapon;
	Target = InTarget;
	Muzzle = InMuzzle;
	LastTargetPoint = TargetLocation;
	FlightTime = FMath::Max(InFlightTime, 0.f);

	const URTSSettings* Settings = GetDefault<URTSSettings>();
	UStaticMesh* Mesh = Settings->ShotEffectMesh.LoadSynchronous();
	UMaterialInterface* Material = Settings->ShotEffectMaterial.LoadSynchronous();
	for (UStaticMeshComponent* Glow : { MuzzleFlash.Get(), Shell.Get(), ImpactFlash.Get() })
	{
		Glow->SetStaticMesh(Mesh);
		if (Material)
		{
			Glow->SetMaterial(0, UMaterialInstanceDynamic::Create(Material, this));
		}
	}
	const FLinearColor FlashColor = FMath::Lerp(InWeapon.ShellColor, FLinearColor::White, 0.5f);
	for (UPointLightComponent* Light : { MuzzleLight.Get(), ImpactLight.Get() })
	{
		Light->SetLightColor(FlashColor);
		Light->SetAttenuationRadius(LightRadius * InWeapon.EffectScale);
	}
	MuzzleFlash->SetWorldLocation(Muzzle);
	MuzzleLight->SetWorldLocation(Muzzle);

	RTSShotSound::Play(*this, Muzzle, InWeapon.EffectScale, false, InWeapon.FireSound);
	UpdateMuzzleFlash();
	UpdateShell();
}

float ARTSShotEffect::GetLifetime() const
{
	return FMath::Max(MuzzleFlashSeconds, FlightTime + ImpactSeconds);
}

FVector ARTSShotEffect::GetAimPoint(const AActor& InTarget, const FVector& From)
{
	FVector BoundsOrigin;
	FVector BoundsExtent;
	InTarget.GetActorBounds(false, BoundsOrigin, BoundsExtent);
	const IRTSTargetable* Targetable = Cast<IRTSTargetable>(&InTarget);
	FVector Point = Targetable ? Targetable->GetClosestFootprintPoint(From) : InTarget.GetActorLocation();
	Point.Z = BoundsOrigin.Z;
	return Point;
}

FVector ARTSShotEffect::GetMuzzlePoint(const AActor& Shooter, const FVector& Toward)
{
	FVector BoundsOrigin;
	FVector BoundsExtent;
	Shooter.GetActorBounds(false, BoundsOrigin, BoundsExtent);
	const FVector Direction = (Toward - BoundsOrigin).GetSafeNormal2D();
	// The bounds are world aligned, so their corner reach overshoots a turned hull; most of it lands near the front.
	const float Reach = FVector(BoundsExtent.X, BoundsExtent.Y, 0.f).Size() * 0.7f;
	return BoundsOrigin + Direction * Reach + FVector(0.f, 0.f, BoundsExtent.Z * 0.3f);
}

FVector ARTSShotEffect::GetImpactPoint() const
{
	// The shell homes, so it lands wherever the target is now; where it was last seen once the target is gone.
	return Target.IsValid() ? GetAimPoint(*Target, Muzzle) : LastTargetPoint;
}

void ARTSShotEffect::SetGlow(UStaticMeshComponent& Glow, float Brightness) const
{
	if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Glow.GetMaterial(0)))
	{
		Material->SetVectorParameterValue(ShellColorParameter, Weapon->ShellColor * GlowStrength * Brightness);
	}
}

void ARTSShotEffect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	if (Target.IsValid())
	{
		LastTargetPoint = GetAimPoint(*Target, Muzzle);
	}
	UpdateMuzzleFlash();
	UpdateShell();
	UpdateImpact();
	if (Age >= GetLifetime())
	{
		Destroy();
	}
}

void ARTSShotEffect::UpdateMuzzleFlash()
{
	const float Alpha = Age / MuzzleFlashSeconds;
	const bool bVisible = Alpha < 1.f;
	MuzzleFlash->SetVisibility(bVisible);
	MuzzleLight->SetVisibility(bVisible);
	if (!bVisible)
	{
		return;
	}
	const float Scale = Weapon->EffectScale;
	MuzzleFlash->SetWorldScale3D(FVector(MuzzleFlashSize * Scale * FMath::Lerp(0.6f, 1.f, Alpha) / MeshDiameter));
	SetGlow(*MuzzleFlash, 1.f - Alpha);
	MuzzleLight->SetIntensity(LightIntensity * Scale * (1.f - Alpha));
}

void ARTSShotEffect::UpdateShell()
{
	const bool bFlying = FlightTime > 0.f && Age < FlightTime;
	Shell->SetVisibility(bFlying);
	if (!bFlying)
	{
		return;
	}
	const FVector End = GetImpactPoint();
	const auto PositionAt = [this, &End](float Alpha)
	{
		return FMath::Lerp(Muzzle, End, Alpha) + FVector(0.f, 0.f, Weapon->ShellArcHeight * 4.f * Alpha * (1.f - Alpha));
	};
	const float Alpha = Age / FlightTime;
	const FVector Position = PositionAt(Alpha);
	const FVector Heading = PositionAt(FMath::Min(Alpha + 0.02f, 1.f)) - Position;

	const float Thickness = ShellThickness * Weapon->EffectScale;
	const float Length = Thickness + Weapon->ShellSpeed * ShellStreakPerSpeed * Weapon->EffectScale;
	Shell->SetWorldLocationAndRotation(Position, Heading.IsNearlyZero() ? FRotator::ZeroRotator : Heading.Rotation());
	Shell->SetWorldScale3D(FVector(Length, Thickness, Thickness) / MeshDiameter);
	SetGlow(*Shell, 1.f);
}

void ARTSShotEffect::UpdateImpact()
{
	if (Age < FlightTime)
	{
		return;
	}
	const float Scale = Weapon->EffectScale;
	if (!bLanded)
	{
		bLanded = true;
		ImpactPoint = GetImpactPoint();
		ImpactFlash->SetWorldLocation(ImpactPoint);
		ImpactLight->SetWorldLocation(ImpactPoint);
		if (Weapon->ImpactSound || Scale >= ImpactSoundMinScale)
		{
			RTSShotSound::Play(*this, ImpactPoint, Scale, true, Weapon->ImpactSound);
		}
	}
	const float Alpha = (Age - FlightTime) / ImpactSeconds;
	const bool bVisible = Alpha < 1.f;
	ImpactFlash->SetVisibility(bVisible);
	ImpactLight->SetVisibility(bVisible);
	if (!bVisible)
	{
		return;
	}
	// Eases out: the blast swells fast, then lingers as it fades.
	const float Swell = 1.f - FMath::Square(1.f - Alpha);
	ImpactFlash->SetWorldScale3D(FVector(FMath::Lerp(ImpactStartSize, ImpactEndSize, Swell) * Scale / MeshDiameter));
	SetGlow(*ImpactFlash, 1.f - Alpha);
	ImpactLight->SetIntensity(LightIntensity * 1.5f * Scale * (1.f - Alpha));
}
