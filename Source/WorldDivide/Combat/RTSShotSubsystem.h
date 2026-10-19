#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSShotSubsystem.generated.h"

class ARTSShotEffect;
class UMaterialInterface;
class UNiagaraComponent;
class UStaticMesh;
class URTSWeaponData;

/**
 * Server-side shells in flight. Each one homes on its target and deals its damage when it lands, even when the shooter
 * died in the meantime. A target that is gone or dead by then takes nothing. A shell fired at a ground location (#305)
 * lands there and deals its damage as a blast through RTSCombat::ApplyAreaDamage. Shells hold while the match is over.
 *
 * And, on every machine that draws, the shots players see (#227): a pool of ARTSShotEffect, at most rts.Fx.ShotPoolSize
 * of them, started by PlayShot and advanced here, flying for the same time as the shell. When all are busy the oldest is
 * started again. The pool also keeps the budget of shot lights shining at once, rts.Fx.MaxShotLights.
 *
 * A weapon with a URTSWeaponData::MuzzleSmoke (vehicle cannons, #221) leaves a puff of smoke at the muzzle, from a pool of
 * Niagara components that belong to no actor, so the smoke stays where it was fired as the turret turns. At most
 * rts.Fx.MuzzleSmokePerShooter puffs per shooter and rts.Fx.MuzzleSmokePoolSize in all; past either the oldest is started
 * again. Past rts.Fx.MuzzleSmokeDistance from every camera a shot leaves none; nearer, the puff has fewer particles the
 * further away it is and the lower the effects quality.
 */
UCLASS()
class WORLDDIVIDE_API URTSShotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Server only. Target must be an IRTSTargetable. */
	void AddShell(AActor& Target, float Damage, AActor* Instigator, float FlightTime);

	/** Server only. A shell that lands on Location after FlightTime and deals Damage to everything within Radius. */
	void AddGroundShell(const FVector& Location, float Damage, float Radius, AActor* Instigator, float FlightTime);

	/** Server only. Flies every shell DeltaSeconds on and deals the damage of those that land. Public so tests can step it. */
	void Advance(float DeltaSeconds);

	int32 GetShellsInFlight() const { return Shells.Num(); }

	/**
	 * Shows a shot from Shooter's Muzzle (X along the bore) at Target, or at TargetLocation when Target is gone, landing after
	 * FlightTime seconds, and leaves the weapon's muzzle smoke. Does nothing on a dedicated server.
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

	/** Muzzle smoke puffs started in this world, those drifting now, and the Niagara components the pool has made for them. */
	int32 GetMuzzleSmokesPlayed() const { return MuzzleSmokesPlayed; }
	int32 GetLiveMuzzleSmokes() const { return LiveMuzzleSmokes; }
	int32 GetMuzzleSmokeComponents() const { return SmokeComponents.Num(); }

	/** The puffs of Shooter drifting now. */
	int32 GetLiveMuzzleSmokes(const AActor& Shooter) const;

	/** The pool's component that played Shooter's latest puff, null when none drifts. Public for the tests. */
	UNiagaraComponent* GetLatestMuzzleSmoke(const AActor& Shooter) const;

	/** How many particles a puff seen from ViewDistance cm has at the effects quality EffectsQuality (0 low to 3 epic). */
	static int32 GetMuzzleSmokeCount(float ViewDistance, int32 EffectsQuality);

	/** Whether a shot seen from ViewDistance cm leaves smoke (rts.Fx.MuzzleSmokeDistance). */
	static bool IsMuzzleSmokeInRange(float ViewDistance);

	static int32 GetMuzzleSmokePoolSize();
	static int32 GetMuzzleSmokePerShooter();

	/** Seconds a puff drifts: NS_MuzzleSmoke's longest particle lifetime. */
	static constexpr float MuzzleSmokeSeconds = 3.f;

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

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FShell
	{
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<AActor> Instigator;
		float Damage = 0.f;
		float TimeLeft = 0.f;
		/** A ground shell's landing point and blast radius; a shell at a target has none. */
		TOptional<FVector> Location;
		float Radius = 0.f;
	};

	TArray<FShell> Shells;

	struct FMuzzleSmoke
	{
		TWeakObjectPtr<AActor> Shooter;
		float Age = 0.f;
		bool bLive = false;
	};

	/** Leaves Weapon's muzzle smoke at Muzzle, seen from ViewDistance cm. */
	void PlayMuzzleSmoke(AActor& Shooter, const URTSWeaponData& Weapon, const FTransform& Muzzle, float ViewDistance);

	/** The slot of SmokeComponents the next puff of Shooter plays in, INDEX_NONE when the pool is empty and may not grow. */
	int32 PickMuzzleSmoke(const AActor& Shooter);

	/** The muzzle smoke pool: its components and, at the same index, what each one plays. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> SmokeComponents;

	TArray<FMuzzleSmoke> Smokes;

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
	int32 MuzzleSmokesPlayed = 0;
	int32 LiveMuzzleSmokes = 0;
	TOptional<FVector> ViewLocationOverride;
};
