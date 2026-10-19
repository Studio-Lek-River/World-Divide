#include "Combat/RTSShotSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSTargetable.h"
#include "Core/RTSGameState.h"
#include "Core/RTSSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShotPoolSize(
		TEXT("rts.Fx.ShotPoolSize"), 256,
		TEXT("Most shot effects playing at once (#227); past it the oldest is started again."));

	TAutoConsoleVariable<int32> CVarMaxShotLights(
		TEXT("rts.Fx.MaxShotLights"), 32,
		TEXT("Most muzzle and impact lights shining at once (#227); the flashes past it glow without a light."));
}

void URTSShotSubsystem::AddShell(AActor& Target, float Damage, AActor* Instigator, float FlightTime)
{
	Shells.Add({ &Target, Instigator, Damage, FlightTime });
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
	if (IsValid(Effect))
	{
		Effect->Start(*this, Shooter, Weapon, Target, TargetLocation, FlightTime, Muzzle, GetViewDistance(Muzzle.GetLocation()));
		LiveEffects.Add(Effect);
	}
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
	if (!LiveEffects.IsEmpty())
	{
		AdvanceEffects(DeltaTime);
	}

	if (!Shells.IsEmpty() && !ARTSGameState::IsMatchOver(*GetWorld()))
	{
		Advance(DeltaTime);
	}
}

TStatId URTSShotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URTSShotSubsystem, STATGROUP_Tickables);
}
