#include "Combat/RTSShotEffect.h"

#include "Combat/RTSShotSound.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSTargetable.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSUnit.h"

namespace
{
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
	// URTSShotSubsystem advances the effects it has started.
	PrimaryActorTick.bCanEverTick = false;

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
	// A flash this short lights what it touches directly; it feeds neither Lumen nor translucency.
	Light->bAffectGlobalIllumination = false;
	Light->bAffectTranslucentLighting = false;
	Light->IndirectLightingIntensity = 0.f;
	Light->VolumetricScatteringIntensity = 0.f;
	Light->bUseInverseSquaredFalloff = false;
	Light->SetLightFalloffExponent(2.f);
	Light->SetIntensity(0.f);
	Light->SetVisibility(false);
	return Light;
}

void ARTSShotEffect::SetLook(UStaticMesh* Mesh, UMaterialInterface* Material)
{
	for (UStaticMeshComponent* Glow : GetGlows())
	{
		Glow->SetStaticMesh(Mesh);
		Glow->SetMaterial(0, Material);
	}
}

TArray<UStaticMeshComponent*> ARTSShotEffect::GetGlows() const
{
	return { MuzzleFlash.Get(), Shell.Get(), ImpactFlash.Get() };
}

void ARTSShotEffect::Start(URTSShotSubsystem& InPool, AActor& Shooter, const URTSWeaponData& InWeapon, AActor* InTarget,
	const FVector& TargetLocation, float InFlightTime)
{
	Stop();
	Pool = &InPool;
	Weapon = &InWeapon;
	Target = InTarget;
	LastTargetPoint = InTarget ? GetAimPoint(*InTarget, Shooter.GetActorLocation()) : TargetLocation;
	// The target's bounds walk all of its parts, so they are measured once; it is a vehicle or a building and keeps its height.
	TargetAimHeight = InTarget ? LastTargetPoint.Z - InTarget->GetActorLocation().Z : 0.f;
	Muzzle = GetMuzzlePoint(Shooter, LastTargetPoint);
	FlightTime = FMath::Max(InFlightTime, 0.f);
	Age = 0.f;
	bLanded = false;

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

void ARTSShotEffect::Stop()
{
	for (UStaticMeshComponent* Glow : { MuzzleFlash.Get(), Shell.Get(), ImpactFlash.Get() })
	{
		Glow->SetVisibility(false);
	}
	SetLightOn(*MuzzleLight, bMuzzleLightSlot, false);
	SetLightOn(*ImpactLight, bImpactLightSlot, false);
	Target.Reset();
}

void ARTSShotEffect::SetLightOn(UPointLightComponent& Light, bool& bHasSlot, bool bOn)
{
	if (bOn && !bHasSlot)
	{
		bHasSlot = Pool.IsValid() && Pool->TakeShotLight();
	}
	else if (!bOn && bHasSlot)
	{
		bHasSlot = false;
		if (Pool.IsValid())
		{
			Pool->ReleaseShotLight();
		}
	}
	Light.SetVisibility(bHasSlot);
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
	if (const ARTSUnit* Unit = Cast<ARTSUnit>(&Shooter); Unit && Unit->HasTurret())
	{
		return Unit->GetMuzzleLocation();
	}
	FVector BoundsOrigin;
	FVector BoundsExtent;
	Shooter.GetActorBounds(false, BoundsOrigin, BoundsExtent);
	const FVector Direction = (Toward - BoundsOrigin).GetSafeNormal2D();
	// The bounds are world aligned, so their corner reach overshoots a turned hull; most of it lands near the front.
	const float Reach = FVector(BoundsExtent.X, BoundsExtent.Y, 0.f).Size() * 0.7f;
	return BoundsOrigin + Direction * Reach + FVector(0.f, 0.f, BoundsExtent.Z * 0.3f);
}

FVector ARTSShotEffect::GetTargetPoint() const
{
	// GetAimPoint with the height measured when the shot started.
	const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Target.Get());
	FVector Point = Targetable ? Targetable->GetClosestFootprintPoint(Muzzle) : Target->GetActorLocation();
	Point.Z = Target->GetActorLocation().Z + TargetAimHeight;
	return Point;
}

FVector ARTSShotEffect::GetImpactPoint() const
{
	// The shell homes, so it lands wherever the target is now; where it was last seen once the target is gone.
	return Target.IsValid() ? GetTargetPoint() : LastTargetPoint;
}

void ARTSShotEffect::SetGlow(UStaticMeshComponent& Glow, float Brightness) const
{
	Glow.SetCustomPrimitiveDataVector4(RTSPrimitiveData::ShotGlow, FVector4(Weapon->ShellColor * GlowStrength * Brightness));
}

bool ARTSShotEffect::Advance(float DeltaSeconds)
{
	if (!Weapon)
	{
		return false;
	}
	Age += DeltaSeconds;
	if (Target.IsValid())
	{
		LastTargetPoint = GetTargetPoint();
	}
	UpdateMuzzleFlash();
	UpdateShell();
	UpdateImpact();
	return Age < GetLifetime();
}

void ARTSShotEffect::UpdateMuzzleFlash()
{
	const float Alpha = Age / MuzzleFlashSeconds;
	const bool bVisible = Alpha < 1.f;
	MuzzleFlash->SetVisibility(bVisible);
	SetLightOn(*MuzzleLight, bMuzzleLightSlot, bVisible);
	if (!bVisible)
	{
		return;
	}
	const float Scale = Weapon->EffectScale;
	MuzzleFlash->SetWorldScale3D(FVector(MuzzleFlashSize * Scale * FMath::Lerp(0.6f, 1.f, Alpha) / MeshDiameter));
	SetGlow(*MuzzleFlash, 1.f - Alpha);
	if (bMuzzleLightSlot)
	{
		MuzzleLight->SetIntensity(LightIntensity * Scale * (1.f - Alpha));
	}
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
	SetLightOn(*ImpactLight, bImpactLightSlot, bVisible);
	if (!bVisible)
	{
		return;
	}
	// Eases out: the blast swells fast, then lingers as it fades.
	const float Swell = 1.f - FMath::Square(1.f - Alpha);
	ImpactFlash->SetWorldScale3D(FVector(FMath::Lerp(ImpactStartSize, ImpactEndSize, Swell) * Scale / MeshDiameter));
	SetGlow(*ImpactFlash, 1.f - Alpha);
	if (bImpactLightSlot)
	{
		ImpactLight->SetIntensity(LightIntensity * 1.5f * Scale * (1.f - Alpha));
	}
}
