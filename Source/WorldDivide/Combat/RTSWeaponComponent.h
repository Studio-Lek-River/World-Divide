#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "RTSWeaponComponent.generated.h"

class URTSWeaponData;

/**
 * Server-side gunner of a unit or building: picks an enemy target, waits out the reload and fires a shell, whose damage
 * URTSShotSubsystem deals when it lands (at once for a weapon without shell speed).
 * Enemy units come before enemy buildings when it picks a target on its own.
 * The owner decides what it may shoot (an ordered target, anything in range, or nothing while moving);
 * turning and closing in are the unit's job. Clients never run it; they see each shot through Multicast_PlayShot and
 * the damage through health replication.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSWeaponComponent();

	/** Server only. Null leaves the owner unarmed. */
	void SetWeaponData(URTSWeaponData* InWeaponData);

	URTSWeaponData* GetWeaponData() const { return WeaponData; }
	bool IsArmed() const { return WeaponData != nullptr; }
	float GetRange() const;

	/** Server only. Scales the weapon's damage, for upgrades; it stays when the weapon data changes. */
	void SetDamageMultiplier(float InDamageMultiplier) { DamageMultiplier = InDamageMultiplier; }

	/** Damage per shot after upgrades; 0 while unarmed. */
	float GetDamage() const;

	/** Server only. The owner's team; targets of any other team are enemies. */
	void SetTeamId(int32 InTeamId) { TeamId = InTeamId; }

	/** Server only. Shoot only this target, once it is in range; null clears the order. */
	void SetOrderedTarget(AActor* InTarget);

	/** Server only. Without an ordered target, engage the nearest enemy in range while this is set. */
	void SetAutoAcquire(bool bEnable);

	/** A living IRTSTargetable of another team than the owner's. Buildings under construction count. */
	bool CanTarget(const AActor* Candidate) const;

	/** Distance to the edge of the target's footprint is within range. */
	bool IsInRange(const AActor* Candidate) const;

	AActor* GetTarget() const { return Target.Get(); }

	/** Server only. Advances the reload and fires when possible. Public so tests can step it deterministically. */
	void UpdateWeapon(float DeltaSeconds);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Every machine but a dedicated server shows the shot. Unreliable: a lost shot is only a missing effect. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayShot(URTSWeaponData* Weapon, AActor* ShotTarget, FVector_NetQuantize TargetLocation, float FlightTime);

	/** The nearest enemy unit in range, or failing that the nearest enemy building in range. */
	AActor* FindNearestEnemyInRange() const;

	/** The nearest T the weapon may shoot right now, measured centre to centre. */
	template <typename T>
	T* FindNearestInRange() const;

	UPROPERTY(Transient)
	TObjectPtr<URTSWeaponData> WeaponData;

	TWeakObjectPtr<AActor> Target;
	int32 TeamId = 0;
	float DamageMultiplier = 1.f;
	bool bTargetOrdered = false;
	bool bAutoAcquire = true;

	/** Seconds until the next shot; zero means ready, so the first shot on a new target is immediate. */
	float Cooldown = 0.f;
};
