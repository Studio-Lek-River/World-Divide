#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSWeaponComponent.generated.h"

class ARTSUnit;
class URTSWeaponData;

/**
 * Server-side gunner of an ARTSUnit: picks a target, waits out the reload and deals instant-hit damage.
 * The unit decides what it may shoot (an ordered target, anything in range, or nothing while moving);
 * turning and closing in are the unit's job. Clients never run it; they see the damage through health replication.
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

	/** Server only. Shoot only this unit, once it is in range; null clears the order. */
	void SetOrderedTarget(ARTSUnit* InTarget);

	/** Server only. Without an ordered target, engage the nearest enemy in range while this is set. */
	void SetAutoAcquire(bool bEnable);

	/** A living unit of another team. */
	bool CanTarget(const ARTSUnit* Candidate) const;

	/** Distance to the edge of the target's footprint is within range. */
	bool IsInRange(const ARTSUnit* Candidate) const;

	ARTSUnit* GetTarget() const { return Target.Get(); }

	/** Server only. Advances the reload and fires when possible. Public so tests can step it deterministically. */
	void UpdateWeapon(float DeltaSeconds);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ARTSUnit* FindNearestEnemyInRange() const;

	UPROPERTY(Transient)
	TObjectPtr<URTSWeaponData> WeaponData;

	TWeakObjectPtr<ARTSUnit> Target;
	bool bTargetOrdered = false;
	bool bAutoAcquire = true;

	/** Seconds until the next shot; zero means ready, so the first shot on a new target is immediate. */
	float Cooldown = 0.f;
};
