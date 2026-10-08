#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSShotEffect.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class URTSWeaponData;

/**
 * One shot as the players see and hear it: a muzzle flash, a glowing shell homing on the target, and a flash where it
 * lands. Purely cosmetic and local to each machine; the damage is URTSShotSubsystem's, which flies the shell for the same time.
 */
UCLASS(NotPlaceable)
class WORLDDIVIDE_API ARTSShotEffect : public AActor
{
	GENERATED_BODY()

public:
	ARTSShotEffect();

	/**
	 * Fires from Shooter at Target, or at TargetLocation when Target is gone, landing after FlightTime seconds.
	 * Returns null on a dedicated server.
	 */
	static ARTSShotEffect* Spawn(AActor& Shooter, const URTSWeaponData& Weapon, AActor* Target, const FVector& TargetLocation,
		float FlightTime);

	/** Seconds from the shot until the effect is over and destroys itself. */
	float GetLifetime() const;

	/** The point the shell flies to on Target: the edge of its footprint facing From, halfway up. */
	static FVector GetAimPoint(const AActor& Target, const FVector& From);

	/** Where a shot leaves Shooter toward Toward: the edge of its bounds on that side, two thirds up. */
	static FVector GetMuzzlePoint(const AActor& Shooter, const FVector& Toward);

	virtual void Tick(float DeltaSeconds) override;

private:
	void Init(const URTSWeaponData& Weapon, AActor* Target, const FVector& Muzzle, const FVector& TargetLocation, float FlightTime);
	FVector GetImpactPoint() const;
	void UpdateMuzzleFlash();
	void UpdateShell();
	void UpdateImpact();

	UStaticMeshComponent* CreateGlow(FName Name);
	UPointLightComponent* CreateLight(FName Name);
	void SetGlow(UStaticMeshComponent& Glow, float Brightness) const;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MuzzleFlash;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UPointLightComponent> MuzzleLight;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Shell;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ImpactFlash;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UPointLightComponent> ImpactLight;

	UPROPERTY(Transient)
	TObjectPtr<const URTSWeaponData> Weapon;

	TWeakObjectPtr<AActor> Target;
	FVector Muzzle = FVector::ZeroVector;

	/** Where the shell lands when the target is gone: its last known aim point. */
	FVector LastTargetPoint = FVector::ZeroVector;

	FVector ImpactPoint = FVector::ZeroVector;
	float FlightTime = 0.f;
	float Age = 0.f;
	bool bLanded = false;
};
