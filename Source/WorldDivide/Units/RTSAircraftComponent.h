#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Commands/RTSCommand.h"
#include "RTSAircraftComponent.generated.h"

class ARTSBuilding;
class ARTSUnit;
class URTSAircraftData;

UENUM()
enum class ERTSAircraftState : uint8
{
	/** On its pad, or on the ground where it was spawned without one; rearms while on a pad. */
	Parked,
	/** In the air, climbing to and holding its cruise altitude. */
	Flying,
	/** Gliding down onto its pad. */
	Landing,
};

UENUM()
enum class ERTSAircraftMission : uint8
{
	None,
	/** Fly at the target, or a ground point for force fire, and make passes until the load is spent or the target is gone. */
	Attack,
	/** Fly to a point and circle there. */
	Move,
	/** Fly back to the pad, or circle where it is until a pad is free. */
	Return,
};

/**
 * Server-side pilot of an aircraft unit. It ignores the navmesh: in the air it always flies forward at the unit's MoveSpeed,
 * turns at its TurnRate toward where its mission leads, climbs to its cruise altitude and rolls into its turns. An Attack
 * order takes it off its pad once fully rearmed, at the target until the load is spent or the target is gone, then back to
 * the pad, where it rearms; a ForceFire does the same at a target of any team or at a point on the ground. Move and AttackMove send it to circle a point; Stop sends it home. A pad lost with its airfield
 * is replaced by a free pad of another own airfield; until there is one, it circles where it is.
 * Every unit has one; it stays inert unless the unit's data is URTSAircraftData. Clients see it fly through movement
 * replication, its ammo through the weapon and whether it is rearming through IsRearming.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSAircraftComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSAircraftComponent();

	/** Server only. Null means the owner cannot fly. An aircraft starts parked with a full load. */
	void SetAircraftData(URTSAircraftData* InAircraftData);

	bool CanFly() const { return AircraftData != nullptr; }

	/** In flight, landing included; a parked aircraft stands on the ground. */
	bool IsAirborne() const { return CanFly() && State != ERTSAircraftState::Parked; }

	/** Server only. Binds the aircraft to a free pad of Airfield and parks it there. False, leaving it as it is, when none is free. */
	bool ParkAt(ARTSBuilding& Airfield);

	/** Server only. Takes over the unit's orders; the order queue stays empty, so Shift has no effect. */
	void ExecuteCommand(const FRTSCommand& Command);

	/**
	 * Whether the aircraft can carry out Command as a ForceFire: its weapon may fire at it (URTSWeaponComponent::CanForceFireAt),
	 * and it is parked, to rearm first, or has ammo left.
	 */
	bool CanForceFire(const FRTSCommand& Command) const;

	/** Server only. Flies the mission and rearms on the pad. Public so tests can step it deterministically. */
	void UpdateAircraft(float DeltaSeconds);

	/** Server only. Its airfield is going: the aircraft looks for another pad, taking off if it stood on this one. */
	void NotifyPadLost();

	/** Server only. Frees the pad, for an aircraft that is going. */
	void ReleasePad();

	/** Parked on a pad and refilling its load. Replicated, so clients see it too. */
	bool IsRearming() const { return bRearming; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	ERTSAircraftState GetState() const { return State; }
	ERTSAircraftMission GetMission() const { return Mission; }
	ARTSBuilding* GetAirfield() const { return Airfield.Get(); }
	int32 GetPadIndex() const { return PadIndex; }

	/** Radius of the circle it flies at full turn, in cm. */
	float GetTurnRadius() const;

	/** The constructed own airfield, standing, with a free pad nearest From, or null. */
	static ARTSBuilding* FindFreeAirfield(const UWorld& World, int32 TeamId, const FVector& From);

private:
	ARTSUnit* GetUnit() const;

	/** Where the aircraft parks on its pad, actor origin included. */
	FVector GetPadParkLocation() const;

	/** Height of the ground under the aircraft: its airfield's, or where it last stood. */
	float GetGroundZ() const;

	/**
	 * The ground height the aircraft holds its cruise altitude over in flight: the ground under Location, or higher ground
	 * ahead along Yaw less what it climbs before it gets there, so it clears a mountain at cruise altitude. GetGroundZ where
	 * there is no ground.
	 */
	float GetTerrainZ(const FVector& Location, float Yaw) const;

	/** Leaves the pad, or the ground, if parked; the mission decides where it flies. */
	void TakeOff();

	/** Each tick in the air: picks the point the mission flies to, ending the mission when it is over, and flies there. */
	void UpdateFlight(float DeltaSeconds);

	/** Turns toward Goal, flies forward and climbs or descends to cruise altitude, banking into the turn. */
	void FlyToward(const FVector& Goal, float DeltaSeconds);

	/** Each tick while landing: glides straight onto the pad and parks there. */
	void UpdateLanding(float DeltaSeconds);

	/** Refills the load while parked on a pad. */
	void UpdateRearm(float DeltaSeconds);

	/** Ends the mission and heads home. */
	void ReturnToPad();

	/** Forgets the attack target or ground point. */
	void ClearAttack();

	/** The attack can go on: a ground point always, a forced target while CanForceTarget allows it, else while CanTarget does. */
	bool IsAttackValid() const;

	/** Points the weapon at the attack target or ground point in flight, at nothing otherwise. */
	void ApplyWeaponMode();

	/** Binds to a free pad of the nearest own airfield with one; false when there is none. */
	bool ClaimNearestPad();

	int32 GetAmmo() const;
	void SetAmmo(int32 Ammo) const;

	UPROPERTY(Transient)
	TObjectPtr<URTSAircraftData> AircraftData;

	TWeakObjectPtr<ARTSBuilding> Airfield;
	int32 PadIndex = INDEX_NONE;

	ERTSAircraftState State = ERTSAircraftState::Parked;
	ERTSAircraftMission Mission = ERTSAircraftMission::None;

	TWeakObjectPtr<AActor> Target;

	/** The ground point of a ForceFire without a target. */
	TOptional<FVector> AttackLocation;

	/** The attack is a ForceFire: at a target of any team, or at AttackLocation. */
	bool bForced = false;

	FVector MovePoint = FVector::ZeroVector;

	/** Where a pad-less aircraft circles until a pad is free. */
	FVector LoiterPoint = FVector::ZeroVector;

	/** Height of the ground where the aircraft last stood. */
	float LastGroundZ = 0.f;

	/** Seconds of rearming toward the next shot. */
	float RearmProgress = 0.f;

	UPROPERTY(Replicated)
	bool bRearming = false;

	/** Seconds before a pad-less aircraft looks for a free pad again. */
	float PadSearchCooldown = 0.f;

	/** Roll the aircraft holds now, in degrees; eases toward the bank its turn asks for. */
	float Roll = 0.f;
};
