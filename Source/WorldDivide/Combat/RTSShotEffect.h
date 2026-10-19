#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSShotEffect.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class URTSShotSubsystem;
class URTSWeaponData;

/**
 * One shot as the players see and hear it: a muzzle flash, a glowing shell homing on the target, and a flash where it
 * lands. Purely cosmetic and local to each machine; the damage is URTSShotSubsystem's, which flies the shell for the same time.
 *
 * Effects are pooled (#227): URTSShotSubsystem::PlayShot starts one from its pool and advances it every frame, and it goes
 * back hidden when it is over. The glows' colour is custom primitive data (RTSPrimitiveData::ShotGlow), so they keep the
 * shared M_ShotFx; a light shines only while URTSShotSubsystem's light budget gives it a slot.
 */
UCLASS(NotPlaceable)
class WORLDDIVIDE_API ARTSShotEffect : public AActor
{
	GENERATED_BODY()

public:
	ARTSShotEffect();

	/** Gives the glows their mesh and material, once, when the pool makes the effect. */
	void SetLook(UStaticMesh* Mesh, UMaterialInterface* Material);

	/**
	 * Fires from Shooter's MuzzleLocation at Target, or at TargetLocation when Target is gone, landing after FlightTime
	 * seconds. Pool hands out the light slots.
	 */
	void Start(URTSShotSubsystem& Pool, AActor& Shooter, const URTSWeaponData& Weapon, AActor* Target, const FVector& TargetLocation,
		float FlightTime, const FVector& MuzzleLocation);

	/** Moves the effect DeltaSeconds on; returns false once it is over. */
	bool Advance(float DeltaSeconds);

	/** Hides everything and gives back its light slots, ready to start again. */
	void Stop();

	/** Seconds from the shot until the effect is over. */
	float GetLifetime() const;

	/** The point the shell flies to on Target: the edge of its footprint facing From, halfway up. */
	static FVector GetAimPoint(const AActor& Target, const FVector& From);

	/** The glows: the muzzle flash, the shell and the impact flash. Public for the tests. */
	TArray<UStaticMeshComponent*> GetGlows() const;

private:
	FVector GetTargetPoint() const;
	FVector GetImpactPoint() const;
	void UpdateMuzzleFlash();
	void UpdateShell();
	void UpdateImpact();

	UStaticMeshComponent* CreateGlow(FName Name);
	UPointLightComponent* CreateLight(FName Name);
	void SetGlow(UStaticMeshComponent& Glow, float Brightness) const;

	/** Shows or hides Light, taking a slot from the pool's budget to show it and giving it back when hidden. */
	void SetLightOn(UPointLightComponent& Light, bool& bHasSlot, bool bOn);

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

	TWeakObjectPtr<URTSShotSubsystem> Pool;
	TWeakObjectPtr<AActor> Target;
	FVector Muzzle = FVector::ZeroVector;

	/** How far above the target's location its aim point is: the middle of its bounds, measured once when the shot starts. */
	float TargetAimHeight = 0.f;

	/** Where the shell lands when the target is gone: its last known aim point. */
	FVector LastTargetPoint = FVector::ZeroVector;

	FVector ImpactPoint = FVector::ZeroVector;
	float FlightTime = 0.f;
	float Age = 0.f;
	bool bLanded = false;
	bool bMuzzleLightSlot = false;
	bool bImpactLightSlot = false;
};
