#include "Combat/RTSShotSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSAreaDamage.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSTargetable.h"
#include "Core/RTSGameState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Scalability.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShotPoolSize(
		TEXT("rts.Fx.ShotPoolSize"), 256,
		TEXT("Most shot effects playing at once (#227); past it the oldest is started again."));

	TAutoConsoleVariable<int32> CVarMaxShotLights(
		TEXT("rts.Fx.MaxShotLights"), 32,
		TEXT("Most muzzle and impact lights shining at once (#227); the flashes past it glow without a light."));

	TAutoConsoleVariable<int32> CVarMuzzleSmokePoolSize(
		TEXT("rts.Fx.MuzzleSmokePoolSize"), 64,
		TEXT("Most muzzle smoke puffs drifting at once (#221); past it the oldest is started again."));

	TAutoConsoleVariable<int32> CVarMuzzleSmokePerShooter(
		TEXT("rts.Fx.MuzzleSmokePerShooter"), 2,
		TEXT("Most muzzle smoke puffs of one shooter drifting at once (#221); past it its oldest is started again."));

	TAutoConsoleVariable<float> CVarMuzzleSmokeDistance(
		TEXT("rts.Fx.MuzzleSmokeDistance"), 30000.f,
		TEXT("cm from the nearest camera within which a shot leaves muzzle smoke (#221), past the default RTS view; 0 or less leaves it on every one."));

	/** Particles of a puff up close at epic effects quality, and the fewest any puff has. */
	constexpr int32 MuzzleSmokeMaxCount = 10;
	constexpr int32 MuzzleSmokeMinCount = 2;

	/** The share of MuzzleSmokeMaxCount at each effects quality, low to epic; cinematic is epic. */
	constexpr float MuzzleSmokeQualityScale[] = { 0.3f, 0.5f, 0.75f, 1.f };

	/** The share left at rts.Fx.MuzzleSmokeDistance, falling off from 1 at the camera. */
	constexpr float MuzzleSmokeFarScale = 0.5f;

	const FName SmokeCountParameter(TEXT("User.Count"));
	const FName SmokeDirectionParameter(TEXT("User.Direction"));
	const FName SmokeScaleParameter(TEXT("User.Scale"));
}

void URTSShotSubsystem::AddShell(AActor& Target, float Damage, AActor* Instigator, float FlightTime)
{
	Shells.Add({ &Target, Instigator, Damage, FlightTime });
}

void URTSShotSubsystem::AddGroundShell(const FVector& Location, float Damage, float Radius, AActor* Instigator, float FlightTime)
{
	Shells.Add({ nullptr, Instigator, Damage, FlightTime, Location, Radius });
}

void URTSShotSubsystem::Advance(float DeltaSeconds)
{
	for (int32 Index = 0; Index < Shells.Num();)
	{
		FShell& Shell = Shells[Index];
		Shell.TimeLeft -= DeltaSeconds;
		// Same tolerance as the weapon reload, so a flight that is a whole number of frames lands on its last frame.
		if (Shell.TimeLeft > KINDA_SMALL_NUMBER)
		{
			++Index;
			continue;
		}
		// Off the list before the damage, which may kill the target and run its death handling.
		const FShell Landed = Shell;
		Shells.RemoveAtSwap(Index);
		if (Landed.Location)
		{
			RTSCombat::ApplyAreaDamage(*GetWorld(), *Landed.Location, Landed.Damage, Landed.Radius, URTSWeaponData::SplashEdgeFraction,
				Landed.Instigator.Get());
			continue;
		}
		const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Landed.Target.Get());
		if (Targetable && IsValid(Landed.Target.Get()) && !Targetable->GetHealthComponent()->IsDead())
		{
			Targetable->GetHealthComponent()->ApplyDamage(Landed.Damage, Landed.Instigator.Get());
		}
	}
}

int32 URTSShotSubsystem::GetPoolSize()
{
	return FMath::Max(CVarShotPoolSize.GetValueOnGameThread(), 1);
}

int32 URTSShotSubsystem::GetMaxShotLights()
{
	return FMath::Max(CVarMaxShotLights.GetValueOnGameThread(), 0);
}

float URTSShotSubsystem::GetViewDistance(const FVector& Location) const
{
	if (ViewLocationOverride)
	{
		return FVector::Dist(*ViewLocationOverride, Location);
	}
	const UWorld* World = GetWorld();
	if (!World || !FApp::CanEverRender())
	{
		return 0.f;
	}
	float Nearest = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Player = It->Get();
		if (Player && Player->IsLocalController() && Player->PlayerCameraManager)
		{
			Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist(Player->PlayerCameraManager->GetCameraLocation(), Location)));
		}
	}
	return Nearest == TNumericLimits<float>::Max() ? 0.f : Nearest;
}

int32 URTSShotSubsystem::GetLiveMuzzleFlashes() const
{
	int32 Live = 0;
	// An effect back in the pool has stopped its flash.
	for (const ARTSShotEffect* Effect : LiveEffects)
	{
		Live += IsValid(Effect) && Effect->IsNiagaraFlashLive() ? 1 : 0;
	}
	return Live;
}

void URTSShotSubsystem::PlayShot(AActor& Shooter, const URTSWeaponData& Weapon, AActor* Target, const FVector& TargetLocation,
	float FlightTime, const FTransform& Muzzle)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!bLookResolved)
	{
		bLookResolved = true;
		const URTSSettings* Settings = GetDefault<URTSSettings>();
		EffectMesh = Settings->ShotEffectMesh.LoadSynchronous();
		EffectMaterial = Settings->ShotEffectMaterial.LoadSynchronous();
	}

	ARTSShotEffect* Effect = nullptr;
	while (!Effect && !FreeEffects.IsEmpty())
	{
		Effect = FreeEffects.Pop(EAllowShrinking::No);
		Effect = IsValid(Effect) ? Effect : nullptr;
	}
	if (!Effect && LiveEffects.Num() < GetPoolSize())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Effect = World->SpawnActor<ARTSShotEffect>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Effect)
		{
			Effect->SetLook(EffectMesh, EffectMaterial);
		}
	}
	if (!Effect && !LiveEffects.IsEmpty())
	{
		// Every effect is busy: the oldest, nearest its end, plays the new shot.
		Effect = LiveEffects[0];
		LiveEffects.RemoveAt(0, EAllowShrinking::No);
	}
	const float ViewDistance = GetViewDistance(Muzzle.GetLocation());
	if (IsValid(Effect))
	{
		Effect->Start(*this, Shooter, Weapon, Target, TargetLocation, FlightTime, Muzzle, ViewDistance);
		LiveEffects.Add(Effect);
	}
	if (Weapon.MuzzleSmoke && IsMuzzleSmokeInRange(ViewDistance))
	{
		PlayMuzzleSmoke(Shooter, Weapon, Muzzle, ViewDistance);
	}
}

int32 URTSShotSubsystem::GetMuzzleSmokePoolSize()
{
	return FMath::Max(CVarMuzzleSmokePoolSize.GetValueOnGameThread(), 1);
}

int32 URTSShotSubsystem::GetMuzzleSmokePerShooter()
{
	return FMath::Max(CVarMuzzleSmokePerShooter.GetValueOnGameThread(), 1);
}

bool URTSShotSubsystem::IsMuzzleSmokeInRange(float ViewDistance)
{
	const float Distance = CVarMuzzleSmokeDistance.GetValueOnGameThread();
	return Distance <= 0.f || ViewDistance <= Distance;
}

int32 URTSShotSubsystem::GetMuzzleSmokeCount(float ViewDistance, int32 EffectsQuality)
{
	const float Quality = MuzzleSmokeQualityScale[FMath::Clamp(EffectsQuality, 0, static_cast<int32>(UE_ARRAY_COUNT(MuzzleSmokeQualityScale)) - 1)];
	const float Distance = CVarMuzzleSmokeDistance.GetValueOnGameThread();
	const float Far = Distance > 0.f ? FMath::Lerp(1.f, MuzzleSmokeFarScale, FMath::Clamp(ViewDistance / Distance, 0.f, 1.f)) : 1.f;
	return FMath::Max(FMath::RoundToInt(MuzzleSmokeMaxCount * Quality * Far), MuzzleSmokeMinCount);
}

int32 URTSShotSubsystem::GetLiveMuzzleSmokes(const AActor& Shooter) const
{
	int32 Live = 0;
	for (const FMuzzleSmoke& Smoke : Smokes)
	{
		Live += Smoke.bLive && Smoke.Shooter.Get() == &Shooter ? 1 : 0;
	}
	return Live;
}

UNiagaraComponent* URTSShotSubsystem::GetLatestMuzzleSmoke(const AActor& Shooter) const
{
	int32 Latest = INDEX_NONE;
	for (int32 Index = 0; Index < Smokes.Num(); ++Index)
	{
		if (Smokes[Index].bLive && Smokes[Index].Shooter.Get() == &Shooter && (Latest == INDEX_NONE || Smokes[Index].Age < Smokes[Latest].Age))
		{
			Latest = Index;
		}
	}
	return Latest == INDEX_NONE ? nullptr : SmokeComponents[Latest].Get();
}

int32 URTSShotSubsystem::PickMuzzleSmoke(const AActor& Shooter)
{
	int32 Free = INDEX_NONE;
	int32 Oldest = INDEX_NONE;
	int32 OwnOldest = INDEX_NONE;
	int32 OwnLive = 0;
	for (int32 Index = 0; Index < Smokes.Num(); ++Index)
	{
		const FMuzzleSmoke& Smoke = Smokes[Index];
		if (!Smoke.bLive)
		{
			Free = Free == INDEX_NONE ? Index : Free;
			continue;
		}
		Oldest = Oldest == INDEX_NONE || Smoke.Age > Smokes[Oldest].Age ? Index : Oldest;
		if (Smoke.Shooter.Get() == &Shooter)
		{
			++OwnLive;
			OwnOldest = OwnOldest == INDEX_NONE || Smoke.Age > Smokes[OwnOldest].Age ? Index : OwnOldest;
		}
	}
	if (OwnLive >= GetMuzzleSmokePerShooter())
	{
		return OwnOldest;
	}
	if (Free != INDEX_NONE)
	{
		return Free;
	}
	if (Smokes.Num() < GetMuzzleSmokePoolSize())
	{
		// Belongs to no actor, so the smoke stays in the world where it was fired, whatever the shooter does next.
		UNiagaraComponent* Component = NewObject<UNiagaraComponent>(GetWorld());
		Component->SetAutoActivate(false);
		Component->SetAutoDestroy(false);
		Component->SetCastShadow(false);
		Component->SetCanEverAffectNavigation(false);
		Component->RegisterComponentWithWorld(GetWorld());
		SmokeComponents.Add(Component);
		return Smokes.AddDefaulted();
	}
	return Oldest;
}

void URTSShotSubsystem::PlayMuzzleSmoke(AActor& Shooter, const URTSWeaponData& Weapon, const FTransform& Muzzle, float ViewDistance)
{
	const int32 Index = PickMuzzleSmoke(Shooter);
	if (Index == INDEX_NONE || !IsValid(SmokeComponents[Index]))
	{
		return;
	}
	UNiagaraComponent& Component = *SmokeComponents[Index];
	if (Component.GetAsset() != Weapon.MuzzleSmoke)
	{
		Component.SetAsset(Weapon.MuzzleSmoke);
	}
	// Unrotated: User.Direction is already the bore in world space, and the puffs' velocity is taken in the system's frame.
	Component.SetWorldLocationAndRotation(Muzzle.GetLocation(), FQuat::Identity);
	Component.SetVariableInt(SmokeCountParameter, GetMuzzleSmokeCount(ViewDistance, Scalability::GetQualityLevels().EffectsQuality));
	Component.SetVariableVec3(SmokeDirectionParameter, Muzzle.GetUnitAxis(EAxis::X));
	Component.SetVariableFloat(SmokeScaleParameter, Weapon.EffectScale);
	// A machine that never renders (tests under -nullrhi) places the puff but Niagara does not run it.
	Component.ActivateSystem(true);

	FMuzzleSmoke& Smoke = Smokes[Index];
	LiveMuzzleSmokes += Smoke.bLive ? 0 : 1;
	Smoke.Shooter = &Shooter;
	Smoke.Age = 0.f;
	Smoke.bLive = true;
	++MuzzleSmokesPlayed;
}

void URTSShotSubsystem::AdvanceEffects(float DeltaSeconds)
{
	// Compacted in place, so the playing effects stay oldest first.
	int32 Kept = 0;
	for (int32 Index = 0; Index < LiveEffects.Num(); ++Index)
	{
		ARTSShotEffect* Effect = LiveEffects[Index];
		if (!IsValid(Effect))
		{
			continue;
		}
		if (Effect->Advance(DeltaSeconds))
		{
			LiveEffects[Kept++] = Effect;
		}
		else
		{
			Effect->Stop();
			FreeEffects.Add(Effect);
		}
	}
	LiveEffects.SetNum(Kept, EAllowShrinking::No);

	// A puff ends itself; its slot is free again after the same time.
	for (FMuzzleSmoke& Smoke : Smokes)
	{
		if (Smoke.bLive)
		{
			Smoke.Age += DeltaSeconds;
			if (Smoke.Age >= MuzzleSmokeSeconds)
			{
				Smoke.bLive = false;
				--LiveMuzzleSmokes;
			}
		}
	}
}

bool URTSShotSubsystem::TakeShotLight()
{
	if (LiveShotLights >= GetMaxShotLights())
	{
		return false;
	}
	++LiveShotLights;
	return true;
}

void URTSShotSubsystem::ReleaseShotLight()
{
	LiveShotLights = FMath::Max(LiveShotLights - 1, 0);
}

void URTSShotSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// The shots players see play on while the match is over, as the flashes of its last shots fade.
	if (!LiveEffects.IsEmpty() || LiveMuzzleSmokes > 0)
	{
		AdvanceEffects(DeltaTime);
	}

	if (!Shells.IsEmpty() && !ARTSGameState::IsMatchOver(*GetWorld()))
	{
		Advance(DeltaTime);
	}
}

void URTSShotSubsystem::Deinitialize()
{
	for (UNiagaraComponent* Component : SmokeComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	SmokeComponents.Reset();
	Smokes.Reset();
	LiveMuzzleSmokes = 0;
	Super::Deinitialize();
}

TStatId URTSShotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URTSShotSubsystem, STATGROUP_Tickables);
}
