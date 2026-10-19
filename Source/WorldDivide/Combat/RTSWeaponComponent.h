#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "RTSWeaponComponent.generated.h"

class ARTSBuilding;
class ARTSUnit;
class URTSWeaponData;

/**
 * Server-side gunner of a unit or building: picks an enemy target, waits out the reload and fires a shell, whose damage
 * URTSShotSubsystem deals when it lands (at once for a weapon without shell speed).
 * Enemy units come before enemy buildings when it picks a target on its own. It keeps checking its target every frame but
 * looks for a new one five times a second, weapons spread over the frames, and at once when the target is lost; units
 * come from URTSAvoidanceSubsystem::QueryRadius and buildings from ARTSGameState's lists per team.
 * The owner decides what it may shoot (an ordered target, a forced one of any team, an ordered ground location, anything in
 * range, or nothing while moving);
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

	/**
	 * Server only. Shoot only this target, once it is in range; null clears the order, a ground location's too. A forced order
	 * (force fire, #305) may be at a target of any team, as CanForceTarget allows.
	 */
	void SetOrderedTarget(AActor* InTarget, bool bForce = false);

	/** Server only. Shoot only at Location on the ground, once it is in range, with the weapon's splash. */
	void SetOrderedLocation(const FVector& Location);

	/** Server only. Drops an ordered target or location. */
	void ClearOrder();

	/** The ground location the weapon is ordered to shoot at, if any. */
	const TOptional<FVector>& GetOrderedLocation() const { return OrderedLocation; }

	/** Server only. Without an ordered target, engage the nearest enemy in range while this is set. */
	void SetAutoAcquire(bool bEnable);

	/**
	 * A living IRTSTargetable of another team than the owner's that the weapon can hit, in flight or on the ground. Buildings
	 * under construction count.
	 */
	bool CanTarget(const AActor* Candidate) const;

	/** As CanTarget, but of any team, the owner's own included: what force fire may shoot. Never the owner itself. */
	bool CanForceTarget(const AActor* Candidate) const;

	/** The weapon can fire at a location on the ground. */
	bool CanTargetGround() const;

	/** Server only. Shots left before the weapon holds fire, as an aircraft's load per sortie; INDEX_NONE for no limit. */
	void SetAmmo(int32 InAmmo) { Ammo = InAmmo; }

	/** Shots left, INDEX_NONE for no limit. Replicates for the HUD. */
	int32 GetAmmo() const { return Ammo; }

	/** Server only. Shots fired since the weapon was made; for the tests. */
	int32 GetShotsFired() const { return ShotsFired; }

	/** Distance to the edge of the target's footprint is within range. */
	bool IsInRange(const AActor* Candidate) const;

	/** Distance along the ground to Location is within range. */
	bool IsInRange(const FVector& Location) const;

	AActor* GetTarget() const { return Target.Get(); }

	/** Where the weapon aims: its ordered location, else its target's centre; unset with neither. */
	TOptional<FVector> GetAimPoint() const;

	/** Server only. Advances the reload and fires when possible. Public so tests can step it deterministically. */
	void UpdateWeapon(float DeltaSeconds);

	/**
	 * Where a shot leaves Shooter toward Toward, X along the shot: a turret unit's barrel tip (ARTSUnit::GetMuzzleTransform),
	 * otherwise the edge of its bounds on that side, two thirds up, facing Toward.
	 */
	static FTransform GetMuzzleTransform(const AActor& Shooter, const FVector& Toward);

	/** Seconds every weapon has spent searching for a target since the last call, which resets it; for rts.Debug.Bench. */
	static double TakeSearchSeconds();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Every machine but a dedicated server shows the shot. Unreliable: a lost shot is only a missing effect. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayShot(URTSWeaponData* Weapon, AActor* ShotTarget, FVector_NetQuantize TargetLocation, float FlightTime);

	/** Fires a shot at Location, which lands with the weapon's splash. */
	void FireAtLocation(const FVector& Location);

	/** The nearest enemy unit in range, or failing that the nearest enemy building in range. */
	AActor* FindNearestEnemyInRange() const;

	/** The next search is at this weapon's next slot, at most SearchInterval from now. */
	void ScheduleSearch();

	/** The nearest enemy unit the weapon may shoot right now, measured centre to centre. */
	ARTSUnit* FindNearestUnitInRange() const;

	/** The nearest enemy building the weapon may shoot right now, measured centre to centre. */
	ARTSBuilding* FindNearestBuildingInRange() const;

	/** Whether Candidate may be shot and lies nearer Origin than the nearest so far, which it then becomes. */
	template <typename T>
	void ConsiderCandidate(T* Candidate, const FVector& Origin, T*& Nearest, float& NearestDistSq) const;

	UPROPERTY(Transient)
	TObjectPtr<URTSWeaponData> WeaponData;

	TWeakObjectPtr<AActor> Target;
	int32 TeamId = 0;
	float DamageMultiplier = 1.f;
	TOptional<FVector> OrderedLocation;
	bool bTargetOrdered = false;
	bool bTargetForced = false;
	bool bAutoAcquire = true;

	UPROPERTY(Replicated)
	int32 Ammo = INDEX_NONE;

	int32 ShotsFired = 0;

	/** Seconds until the next shot; zero means ready, so the first shot on a new target is immediate. */
	float Cooldown = 0.f;

	/** Seconds until the next search for a target; zero means search now, so a new weapon looks at once. */
	float SearchCooldown = 0.f;

	/** Seconds this weapon has been updated, which places its searches on its own share of the frames. */
	double SearchClock = 0.0;
};
