#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Combat/RTSTargetable.h"
#include "Commands/RTSCommand.h"
#include "RTSUnit.generated.h"

class AAIController;
class UCapsuleComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UFloatingPawnMovement;
class URTSAircraftComponent;
class URTSBuilderComponent;
class URTSHarvesterComponent;
class URTSHealthComponent;
class URTSWeaponComponent;
class URTSUnitData;
struct FPathFollowingResult;

/**
 * A light pawn (no ACharacter) so hundreds of units stay cheap. Stats come from URTSUnitData.
 * Orders are executed on the server only; clients see the result through replication. An aircraft, whose data is
 * URTSAircraftData, hands its orders to its aircraft component and flies instead of walking the navmesh.
 */
UCLASS()
class WORLDDIVIDE_API ARTSUnit : public APawn, public IRTSTargetable
{
	GENERATED_BODY()

public:
	ARTSUnit();

	/** Server only, between SpawnActorDeferred and FinishSpawning. */
	void InitFromData(URTSUnitData* InUnitData, int32 InTeamId);

	/** Server only. Sets weapon damage and maximum health from the data and the upgrades the owner has researched. */
	void ApplyUpgrades();

	/**
	 * Server only. Replaces the order queue, or appends to it when Command.bQueue is set.
	 * A Harvest for a unit that cannot harvest becomes a move up to the source, as a Build naming an own construction site
	 * becomes a move up to the site for a unit that cannot build. A builder's new building is paid here, queued or not, and
	 * refused when the player cannot afford it; it is refunded if the order ends before the building is placed.
	 */
	void ExecuteCommand(const FRTSCommand& InCommand);

	/**
	 * Server only. Moves to Location ahead of the current order, then picks the queue up again where it was: a builder resumes
	 * the site it already placed. Used to clear a building site, not a player order. Aircraft ignore it.
	 */
	void StepAside(const FVector& Location);

	/**
	 * Server only. Pauses the current move when the next stretch of its path leads into Footprint, a new building whose
	 * navmesh hole is not built yet, so the unit waits at its edge. Returns true when it paused. Aircraft never pause.
	 */
	bool HoldShortOf(const FBox& Footprint);

	/** Server only. True while HoldShortOf keeps the move paused; any new move ends the hold. */
	bool IsMoveHeld() const;

	/** Server only. Ends a hold: the current order paths again, now around the building. */
	void ReleaseHold();

	/** Server only. Called by the AI controller whenever a path-following request ends. */
	void NotifyMoveFinished(const FPathFollowingResult& Result);

	/** Server only. Where the last move order sent the unit, unset once an order of another kind starts. */
	TOptional<FVector> GetMoveGoal() const { return MoveGoal; }

	/**
	 * Server only. Ends a move or attack-move under way as done, for a unit that reached the crowd already standing on its
	 * goal. See URTSAvoidanceSubsystem.
	 */
	void ArriveInCrowd();

	URTSUnitData* GetUnitData() const { return UnitData; }
	virtual int32 GetTeamId() const override { return TeamId; }
	virtual URTSHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	URTSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }
	URTSHarvesterComponent* GetHarvesterComponent() const { return HarvesterComponent; }
	URTSBuilderComponent* GetBuilderComponent() const { return BuilderComponent; }
	URTSAircraftComponent* GetAircraftComponent() const { return AircraftComponent; }
	float GetSelectionRadius() const;
	int32 GetQueuedCommandCount() const { return CommandQueue.Num(); }

	/** The edge of the selection circle facing From. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	/** Server only: an aircraft in flight. Clients see aircraft as on the ground. */
	virtual bool IsAirborne() const override;

	/** Every machine. Jolts the mesh back for a moment, as a shot recoils; cosmetic only. */
	void PlayFireKick(float EffectScale);

	/** Distance from the actor origin down to the ground contact point. */
	float GetGroundOffset() const;

	/**
	 * Server only. Stands a ground unit on the ground under it, which the movement does not follow by itself; aircraft keep
	 * their own height. Leaves the unit where it is over no ground. Runs every tick; public so tests can step it.
	 */
	void SnapToGround();

	/** Every machine. How far the mesh leans with the slope under a vehicle that tilts to the ground; zero for the rest. */
	FRotator GetMeshTilt() const { return MeshTilt; }

	/** A unit whose data gives it a turret aims with it instead of turning the whole unit. */
	bool HasTurret() const;

	/** Every machine. The turret's yaw relative to the hull, in degrees; zero without a turret. */
	float GetTurretYaw() const { return TurretYaw; }

	/**
	 * Server only. Traverses the turret toward the weapon's target, or back to the front without one. Runs every tick; public
	 * so tests can step it.
	 */
	void UpdateTurret(float DeltaSeconds);

	/** True once the turret points at Target closely enough to fire; always true without a turret, which turns the unit. */
	bool IsAimedAt(const AActor& Target) const;

	/** Every machine. The barrel tip in world space, on the gun when the data gives one; only meaningful with a turret. */
	FVector GetMuzzleLocation() const;

	/** A turret unit whose data also gives it a gun that elevates. */
	bool HasGun() const;

	/** Every machine. The gun's elevation in degrees, up positive; zero without a gun. */
	float GetGunPitch() const { return GunPitch; }

	/**
	 * Server only. Holds the turret at Yaw and the gun at Pitch (degrees, relative to the hull) instead of aiming at the
	 * weapon's target, for inspecting a vehicle; unset gives the aiming back. Shots still wait for a turret aimed at the target.
	 */
	void SetAimOverride(TOptional<FVector2D> YawPitch);

	/**
	 * Every machine. Where URTSUnitData::Parts[PartIndex] is drawn, in world space; false when there is no such part. Wheels
	 * include their roll.
	 */
	bool GetPartTransform(int32 PartIndex, FTransform& OutTransform) const;

	/**
	 * Every machine. Rolls the wheels by how far the unit drove and turned since the last call, each by the ground speed on
	 * its own side; nothing to do while the unit stands. Runs every tick; public so tests can step it.
	 */
	void UpdateRunningGear();

	static FLinearColor GetTeamColor(int32 InTeamId);

	/** Material vector parameter that receives the team colour, on units and buildings alike. */
	static const FName TeamColorParameter;

	/** Feet, not eye height: navmesh queries start from the ground. */
	virtual FVector GetNavAgentLocation() const override;

	virtual void PostInitializeComponents() override;
	virtual void Destroyed() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_UnitData();

	UFUNCTION()
	void OnRep_TeamId();

	/** Server only. Warns the team's players unless the instigator is on the unit's own team. */
	UFUNCTION()
	void HandleDamaged(float Amount, AActor* DamageInstigator);

	/** Server only. */
	UFUNCTION()
	void HandleDeath(AActor* Killer);

private:
	void ApplyUnitData();
	void ApplyTeamColor();

	/** The turret's yaw toward Target relative to the hull, in degrees. */
	float GetTurretYawToward(const AActor& Target) const;

	UFUNCTION()
	void OnRep_TurretYaw();

	UFUNCTION()
	void OnRep_GunPitch();

	/** Creates the gun and the parts the data asks for, replacing any made before. */
	void CreateParts();

	void StartNextCommand();

	/** Empties the order queue, refunding what its orders still hold. */
	void ClearCommands();

	/** Drops the front order, refunding what it still holds. */
	void PopCommand();

	/**
	 * Returns false when the command is already done or invalid and should be dropped. A Build hands its payment to the builder
	 * component.
	 */
	bool StartCommand(AAIController& AI, FRTSCommand& Command);

	/**
	 * Paths to Location, stopping at the edge on the unit's side when it lies inside a wall or a building. Returns true while
	 * the unit moves; false when it cannot path there or already stands at the goal.
	 */
	bool RequestMove(AAIController& AI, const FVector& Location);

	/**
	 * Each tick while an Attack is the current order: drop it once the target is gone, otherwise close in or hold.
	 * Units are followed; buildings stand in their own navmesh hole, so the unit walks to a reachable spot outside the
	 * footprint, facing side first, and drops the order when there is none in range.
	 */
	void UpdateAttackOrder();

	/**
	 * Each tick while an AttackMove is the current order: stand and fight while the weapon has an enemy in range, otherwise
	 * walk on to the target, and end the order once the unit stands there.
	 */
	void UpdateAttackMoveOrder();

	/** Each tick while a Harvest is the current order: the harvester component runs it and says when it is over. */
	void UpdateHarvestOrder(float DeltaSeconds);

	/** Each tick while a Build is the current order: the builder component runs it and says when it is over. */
	void UpdateBuildOrder(float DeltaSeconds);

	/**
	 * Points the weapon at what the current order allows: the attack target, anything in range when idle or attack-moving,
	 * nothing while moving otherwise.
	 */
	void ApplyWeaponMode();

	/** Server only. Each tick: runs the current order, stands the unit on the ground and turns it, or flies it. */
	void TickOrders(float DeltaSeconds);

	void FaceTowardYaw(float Yaw, float DeltaSeconds);

	/** Eases the mesh back from a fire kick. Returns true when the mesh has to move. */
	bool UpdateFireKick(float DeltaSeconds);

	/**
	 * Eases the mesh of a vehicle that tilts to the ground toward the slope under it, and every other mesh back upright.
	 * Returns true when the mesh has to move.
	 */
	bool UpdateTilt(float DeltaSeconds);

	/** Places the mesh from its rest location, tilt and fire kick; it leans around the bottom of the capsule. */
	void UpdateMeshTransform();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCapsuleComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Attached to Mesh, so it leans and kicks with the hull; hidden without a turret. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Turret;

	/** Attached to Turret; made by CreateParts only for data with a gun. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Gun;

	/** The components CreateParts made for URTSUnitData::Parts: one per part, one instanced component per wheel mesh. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Components")
	TArray<TObjectPtr<UStaticMeshComponent>> PartComponents;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UFloatingPawnMovement> Movement;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHarvesterComponent> HarvesterComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSBuilderComponent> BuilderComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSAircraftComponent> AircraftComponent;

	UPROPERTY(ReplicatedUsing = OnRep_UnitData)
	TObjectPtr<URTSUnitData> UnitData;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	/** Relative to the hull, in degrees. The server traverses it; clients follow, as they follow the unit's rotation. */
	UPROPERTY(ReplicatedUsing = OnRep_TurretYaw)
	float TurretYaw = 0.f;

	/** Degrees, up positive. The server elevates it; clients follow. */
	UPROPERTY(ReplicatedUsing = OnRep_GunPitch)
	float GunPitch = 0.f;

	/** Server only. See SetAimOverride. */
	TOptional<FVector2D> AimOverride;

	/** Where each of URTSUnitData::Parts is drawn: its component, and its instance when that is instanced. */
	struct FPartSlot
	{
		UStaticMeshComponent* Component = nullptr;
		int32 Instance = INDEX_NONE;
	};
	TArray<FPartSlot> PartSlots;

	/** A wheel's roll, kept per instance of its instanced component. */
	struct FWheel
	{
		UInstancedStaticMeshComponent* Component = nullptr;
		int32 Instance = 0;
		/** Its rest place on the hull, before the roll. */
		FTransform Rest;
		/** In world cm; with Side, how far it rolls for a distance driven and an angle turned. */
		float Radius = 1.f;
		float Side = 0.f;
		float Angle = 0.f;
	};
	TArray<FWheel> Wheels;

	/** The unit's place when UpdateRunningGear last ran. */
	TOptional<FTransform> LastGearPose;

	/** Server only. Front entry is the order being executed. A UPROPERTY so GC sees and clears attack targets. */
	UPROPERTY(Transient)
	TArray<FRTSCommand> CommandQueue;

	bool bRequestingMove = false;
	bool bMoveFinishedDuringRequest = false;

	/** Set while an attack-move stops to fight, so the aborted move does not end the order. */
	bool bStoppingToFight = false;

	TOptional<FVector> MoveGoal;

	/** Where ApplyUnitData stood the mesh; a fire kick moves it back from here and returns it. */
	FVector MeshRestLocation = FVector::ZeroVector;
	float FireKickDistance = 0.f;
	float FireKickTimeLeft = 0.f;

	FRotator MeshTilt = FRotator::ZeroRotator;

	/** Server only. The ground's normal where SnapToGround last stood the unit; clients trace their own. */
	TOptional<FVector> GroundNormal;
};
