#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSShotSubsystem.generated.h"

class ARTSShotEffect;
class UMaterialInterface;
class UStaticMesh;
class URTSWeaponData;

/**
 * Server-side shells in flight. Each one homes on its target and deals its damage when it lands, even when the shooter
 * died in the meantime. A target that is gone or dead by then takes nothing. Shells hold while the match is over.
 *
 * And, on every machine that draws, the shots players see (#227): a pool of ARTSShotEffect, at most rts.Fx.ShotPoolSize
 * of them, started by PlayShot and advanced here, flying for the same time as the shell. When all are busy the oldest is
 * started again. The pool also keeps the budget of shot lights shining at once, rts.Fx.MaxShotLights.
 */
UCLASS()
class WORLDDIVIDE_API URTSShotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Server only. Target must be an IRTSTargetable. */
	void AddShell(AActor& Target, float Damage, AActor* Instigator, float FlightTime);

	/** Server only. Flies every shell DeltaSeconds on and deals the damage of those that land. Public so tests can step it. */
	void Advance(float DeltaSeconds);

	int32 GetShellsInFlight() const { return Shells.Num(); }

	/**
	 * Shows a shot from Shooter's Muzzle (X along the bore) at Target, or at TargetLocation when Target is gone, landing after
	 * FlightTime seconds. Does nothing on a dedicated server.
	 */
	void PlayShot(AActor& Shooter, const URTSWeaponData& Weapon, AActor* Target, const FVector& TargetLocation, float FlightTime,
		const FTransform& Muzzle);

	/**
	 * cm from Location to the nearest camera of this machine's local players, for the muzzle flash's culling (#220). 0 on a
	 * machine that never renders, or without a camera, so tests under -nullrhi see every flash.
	 */
	float GetViewDistance(const FVector& Location) const;

	/** Makes GetViewDistance measure from Location instead of the cameras, or from the cameras again when unset. For the tests. */
	void SetViewLocationOverride(TOptional<FVector> Location) { ViewLocationOverride = Location; }

	/** Counts a Niagara muzzle flash started by an effect. */
	void CountMuzzleFlash() { ++MuzzleFlashesPlayed; }

	/** Niagara muzzle flashes started in this world, and those playing now. */
	int32 GetMuzzleFlashesPlayed() const { return MuzzleFlashesPlayed; }
	int32 GetLiveMuzzleFlashes() const;

	/** Moves every shot effect DeltaSeconds on and takes those that are over back into the pool. Public so tests can step it. */
	void AdvanceEffects(float DeltaSeconds);

	/** A light slot for a shot effect from the budget; false when it is spent. Give it back with ReleaseShotLight. */
	bool TakeShotLight();
	void ReleaseShotLight();

	int32 GetLiveEffects() const { return LiveEffects.Num(); }
	int32 GetPooledEffects() const { return FreeEffects.Num(); }
	int32 GetLiveShotLights() const { return LiveShotLights; }

	static int32 GetPoolSize();
	static int32 GetMaxShotLights();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FShell
	{
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<AActor> Instigator;
		float Damage = 0.f;
		float TimeLeft = 0.f;
	};

	TArray<FShell> Shells;

	/** Shot effects that are playing, oldest first, and those waiting hidden in the pool. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ARTSShotEffect>> LiveEffects;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ARTSShotEffect>> FreeEffects;

	/** URTSSettings' effect mesh and material, resolved with the first shot. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> EffectMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> EffectMaterial;

	bool bLookResolved = false;
	int32 LiveShotLights = 0;
	int32 MuzzleFlashesPlayed = 0;
	TOptional<FVector> ViewLocationOverride;
};
