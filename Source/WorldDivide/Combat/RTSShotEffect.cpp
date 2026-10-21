#include "Combat/RTSShotEffect.h"

#include "Combat/RTSShotSound.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSTargetable.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Units/RTSPrimitiveData.h"

namespace
{
	TAutoConsoleVariable<float> CVarMuzzleLightDistance(
		TEXT("rts.Fx.MuzzleLightDistance"), 25000.f,
		TEXT("cm from the nearest camera within which a shot lights its muzzle (#220), past the default RTS view (about 180 m); 0 or less lights every one."));

	TAutoConsoleVariable<float> CVarMuzzleFlashDistance(
		TEXT("rts.Fx.MuzzleFlashDistance"), 50000.f,
		TEXT("cm from the nearest camera within which a shot shows its Niagara muzzle flash (#220), past the farthest RTS view; 0 or less shows every one."));

	/** The muzzle light's boost (GetMuzzleLightBoost) starts past this many cm from the camera and is full at the far end. */
	constexpr float MuzzleLightBoostStart = 5000.f;
	constexpr float MuzzleLightBoostEnd = 25000.f;
	constexpr float MuzzleLightMaxBoost = 3.f;
	/**
	 * The boost widens the light far more than it brightens it: full width and full intensity together clipped the ground
	 * around a distant gun to a white disc by day (#312).
	 */
	constexpr float MuzzleLightMaxIntensityBoost = 1.1f;
	/** The muzzle light is mostly the shell's fire colour, a warm glow rather than a white spot (#312). */
	constexpr float MuzzleLightWhite = 0.1f;
	/** Steeper than the impact light's, so the glow gathers around the gun and fades out without a disc edge (#312). */
	constexpr float MuzzleLightFalloffExponent = 4.f;

	/** The intensity's share of a GetMuzzleLightBoost. */
	float GetMuzzleLightIntensityBoost(float Boost)
	{
		return FMath::Lerp(1.f, MuzzleLightMaxIntensityBoost, (Boost - 1.f) / (MuzzleLightMaxBoost - 1.f));
	}

	const FName FlashColorParameter(TEXT("User.Color"));
	const FName FlashScaleParameter(TEXT("User.Scale"));
	const FName FlashBrakeParameter(TEXT("User.Brake"));

	/** GetMuzzleLightCurve: full this long, then falling by e every DecaySeconds, flickering FlickerHz, out at the end. */
	constexpr float MuzzleLightPeakSeconds = 0.015f;
	constexpr float MuzzleLightDecaySeconds = 0.035f;
	constexpr float MuzzleLightFlickerHz = 55.f;
	constexpr float MuzzleLightFlicker = 0.22f;
	constexpr float MuzzleLightOutSeconds = 0.03f;

	constexpr float MuzzleFlashSeconds = 0.08f;
	/** How long a Niagara muzzle flash (#220) and its light last: NS_MuzzleFlash's lifetime (#354). */
	constexpr float NiagaraFlashSeconds = 0.2f;
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
	/** A Niagara muzzle flash's light is this many times brighter, so it reads in daylight too. */
	constexpr float NiagaraFlashLightScale = 3.f;
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

float ARTSShotEffect::GetMuzzleLightBoost(float ViewDistance)
{
	return FMath::GetMappedRangeValueClamped(FVector2f(MuzzleLightBoostStart, MuzzleLightBoostEnd), FVector2f(1.f, MuzzleLightMaxBoost),
		ViewDistance);
}

float ARTSShotEffect::GetMuzzleLightCurve(float Age)
{
	if (Age < 0.f || Age >= NiagaraFlashSeconds)
	{
		return 0.f;
	}
	if (Age < MuzzleLightPeakSeconds)
	{
		return 1.f;
	}
	const float Fall = FMath::Exp(-(Age - MuzzleLightPeakSeconds) / MuzzleLightDecaySeconds);
	const float Flicker = 1.f - MuzzleLightFlicker * 0.5f * (1.f - FMath::Cos(UE_TWO_PI * MuzzleLightFlickerHz * Age));
	const float Out = FMath::Min((NiagaraFlashSeconds - Age) / MuzzleLightOutSeconds, 1.f);
	return Fall * Flicker * Out;
}

bool ARTSShotEffect::IsMuzzleLightInRange(float ViewDistance)
{
	const float Distance = CVarMuzzleLightDistance.GetValueOnGameThread();
	return Distance <= 0.f || ViewDistance <= Distance;
}

bool ARTSShotEffect::IsMuzzleFlashInRange(float ViewDistance)
{
	const float Distance = CVarMuzzleFlashDistance.GetValueOnGameThread();
	return Distance <= 0.f || ViewDistance <= Distance;
}

bool ARTSShotEffect::IsNiagaraFlashLive() const
{
	return bNiagaraFlashLive;
}

void ARTSShotEffect::PlayNiagaraFlash(const FTransform& MuzzleTransform)
{
	if (!NiagaraFlash)
	{
		NiagaraFlash = NewObject<UNiagaraComponent>(this, TEXT("NiagaraFlash"));
		NiagaraFlash->SetupAttachment(RootComponent);
		NiagaraFlash->SetUsingAbsoluteLocation(true);
		NiagaraFlash->SetUsingAbsoluteRotation(true);
		NiagaraFlash->SetUsingAbsoluteScale(true);
		NiagaraFlash->SetAutoActivate(false);
		NiagaraFlash->SetAutoDestroy(false);
		NiagaraFlash->SetCastShadow(false);
		NiagaraFlash->SetCanEverAffectNavigation(false);
		NiagaraFlash->RegisterComponent();
	}
	if (NiagaraFlash->GetAsset() != Weapon->MuzzleFlash)
	{
		NiagaraFlash->SetAsset(Weapon->MuzzleFlash);
	}
	NiagaraFlash->SetWorldLocationAndRotation(MuzzleTransform.GetLocation(), MuzzleTransform.GetRotation());
	NiagaraFlash->SetVariableLinearColor(FlashColorParameter, FMath::Lerp(Weapon->ShellColor, FLinearColor::White, 0.3f) * GlowStrength);
	NiagaraFlash->SetVariableFloat(FlashScaleParameter, Weapon->EffectScale);
	NiagaraFlash->SetVariableInt(FlashBrakeParameter, Weapon->bMuzzleBrake ? 1 : 0);
	// A machine that never renders (tests under -nullrhi) places the flash but Niagara does not run it. A full reset, not
	// ActivateSystem: once the last shot's flash has played out, ActivateSystem wakes the system but none of its one-shot
	// emitters, so only the first shot of a pooled effect flashed (#354).
	NiagaraFlash->ResetSystem();
	bNiagaraFlashLive = true;
	Pool->CountMuzzleFlash();
}

void ARTSShotEffect::Start(URTSShotSubsystem& InPool, AActor& Shooter, const URTSWeaponData& InWeapon, AActor* InTarget,
	const FVector& TargetLocation, float InFlightTime, const FTransform& MuzzleTransform, float ViewDistance)
{
	Stop();
	Pool = &InPool;
	Weapon = &InWeapon;
	Target = InTarget;
	LastTargetPoint = InTarget ? GetAimPoint(*InTarget, Shooter.GetActorLocation()) : TargetLocation;
	// The target's bounds walk all of its parts, so they are measured once; it is a vehicle or a building and keeps its height.
	TargetAimHeight = InTarget ? LastTargetPoint.Z - InTarget->GetActorLocation().Z : 0.f;
	Muzzle = MuzzleTransform.GetLocation();
	FlightTime = FMath::Max(InFlightTime, 0.f);
	Age = 0.f;
	bLanded = false;

	// A vehicle gun's flash is its Niagara system, not shown at all far from every camera; other weapons keep the glow.
	const bool bNiagaraFlash = InWeapon.MuzzleFlash != nullptr;
	bMuzzleGlow = !bNiagaraFlash;
	bMuzzleLightInRange = !bNiagaraFlash || IsMuzzleLightInRange(ViewDistance);
	MuzzleLightBoost = bNiagaraFlash ? GetMuzzleLightBoost(ViewDistance) : 1.f;
	if (bNiagaraFlash && IsMuzzleFlashInRange(ViewDistance))
	{
		PlayNiagaraFlash(MuzzleTransform);
	}

	const FLinearColor FlashColor = FMath::Lerp(InWeapon.ShellColor, FLinearColor::White, 0.5f);
	for (UPointLightComponent* Light : { MuzzleLight.Get(), ImpactLight.Get() })
	{
		Light->SetLightColor(FlashColor);
		Light->SetAttenuationRadius(LightRadius * InWeapon.EffectScale);
	}
	MuzzleLight->SetAttenuationRadius(LightRadius * InWeapon.EffectScale * MuzzleLightBoost);
	if (bNiagaraFlash)
	{
		MuzzleLight->SetLightColor(FMath::Lerp(InWeapon.ShellColor, FLinearColor::White, MuzzleLightWhite));
	}
	MuzzleLight->SetLightFalloffExponent(bNiagaraFlash ? MuzzleLightFalloffExponent : 2.f);
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
	// The Niagara flash ends itself; the next shot restarts it.
	bNiagaraFlashLive = false;
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
	return FMath::Max(bMuzzleGlow ? MuzzleFlashSeconds : NiagaraFlashSeconds, FlightTime + ImpactSeconds);
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
	const float Alpha = Age / (bMuzzleGlow ? MuzzleFlashSeconds : NiagaraFlashSeconds);
	const bool bVisible = Alpha < 1.f;
	MuzzleFlash->SetVisibility(bVisible && bMuzzleGlow);
	// The system ends itself after the same time.
	bNiagaraFlashLive &= bVisible;
	SetLightOn(*MuzzleLight, bMuzzleLightSlot, bVisible && bMuzzleLightInRange);
	if (!bVisible)
	{
		return;
	}
	const float Scale = Weapon->EffectScale;
	if (bMuzzleGlow)
	{
		MuzzleFlash->SetWorldScale3D(FVector(MuzzleFlashSize * Scale * FMath::Lerp(0.6f, 1.f, Alpha) / MeshDiameter));
		SetGlow(*MuzzleFlash, 1.f - Alpha);
	}
	if (bMuzzleLightSlot)
	{
		// The placeholder glow's light fades with it; a Niagara flash's follows its fire, a sharp peak and a flicker down.
		const float Brightness = bMuzzleGlow ? 1.f - Alpha : NiagaraFlashLightScale * GetMuzzleLightIntensityBoost(MuzzleLightBoost)
			* GetMuzzleLightCurve(Age);
		MuzzleLight->SetIntensity(LightIntensity * Scale * Brightness);
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
