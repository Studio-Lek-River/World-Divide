#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSWeaponData.generated.h"

class UNiagaraSystem;
class USoundBase;

/**
 * A weapon that fires a homing shell: the damage lands when the shell arrives, wherever the target has gone by then.
 * Units and buildings reference one from their entity data; none means unarmed.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Seconds the shell flies over Distance cm; 0 for a weapon that hits at once. */
	float GetFlightTime(float Distance) const { return ShellSpeed > 0.f ? Distance / ShellSpeed : 0.f; }

	/** The weapon can hit a target that is airborne, or one on the ground when bAirborne is false. */
	bool CanHit(bool bAirborne) const { return bAirborne ? bCanTargetAir : bCanTargetGround; }

	/** Reach in cm, measured to the edge of the target's footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0"))
	float Range = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0"))
	float Damage = 10.f;

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.01"))
	float ReloadSeconds = 1.f;

	/** Shell speed in cm/s, measured to the edge of the target's footprint. 0 hits at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0"))
	float ShellSpeed = 0.f;

	/** Hits units and buildings on the ground, aircraft parked on their pads included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	bool bCanTargetGround = true;

	/** Hits aircraft in flight, as anti-air does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	bool bCanTargetAir = false;

	/** How high the shell arcs above the straight line at mid-flight, in cm; 0 flies straight. Cosmetic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0"))
	float ShellArcHeight = 0.f;

	/** Size of the muzzle flash, shell and impact, and how deep the synthesized shot sounds: 1 is a light cannon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0.1"))
	float EffectScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FLinearColor ShellColor = FLinearColor(1.f, 0.55f, 0.15f);

	/**
	 * Played at the muzzle along the bore (#220), with the user parameters Color (from ShellColor) and Scale (EffectScale);
	 * none shows the placeholder glow. Vehicle guns have one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<UNiagaraSystem> MuzzleFlash;

	/**
	 * A puff of smoke left at the muzzle after each shot (#221): it leaves along the bore, then drifts in world space as the
	 * turret turns. User parameters Count, Direction and Scale (EffectScale); none leaves no smoke. Vehicle cannons have one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<UNiagaraSystem> MuzzleSmoke;

	/** Played at the muzzle; none plays a synthesized shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<USoundBase> FireSound;

	/** Played where the shell lands; none plays a synthesized blast for heavy weapons and nothing for light ones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<USoundBase> ImpactSound;
};
