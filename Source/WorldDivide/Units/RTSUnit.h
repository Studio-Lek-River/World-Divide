#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Combat/RTSTargetable.h"
#include "Commands/RTSCommand.h"
#include "Navigation/RTSHullClearance.h"
#include "Units/RTSLocomotion.h"
#include "Units/RTSUnitShape.h"
#include "RTSUnit.generated.h"

/**
 * Where a machine draws from, for ARTSUnit::UpdateRunningGear: a track's phase goes to its material only on a machine that
 * draws, and only within Range of one of its players' cameras (beyond it the material does not move the links; #151). The
 * phase keeps counting either way, so the links come back where they should be. A camera with a direction also sends it only
 * to tracks within its view's cone, widened by Margin (#212).
 */
struct FRTSTrackViews
{
	bool bDraws = true;
	TArray<FVector, TInlineAllocator<4>> Locations;
	/** Each camera's unit forward vector and half the angle across its view's diagonal, in radians; a camera without one sees all round. */
	TArray<FVector, TInlineAllocator<4>> Directions;
	TArray<float, TInlineAllocator<4>> HalfAngles;
	/** cm; zero is no limit. */
	float Range = 0.f;
	/** cm the view's cone is widened by round a track's bounds, for its shadow and the camera's next move. */
	float Margin = 0.f;
};

class AAIController;
class URTSWeaponData;

/**
 * One shot as the machines that draw see it (#217): raised by ARTSUnit::OnShot once per authoritative shot, on a listen
 * server's host and on every client, never on a dedicated server. For the gun's recoil, flash and smoke.
 */
struct FRTSShotEvent
{
	const URTSWeaponData* Weapon = nullptr;
	/** The barrel tip in world space, X along the bore (ARTSUnit::GetMuzzleTransform). */
	FTransform Muzzle;
	/** Unit vector from the muzzle toward where the shot is aimed. */
	FVector Direction = FVector::ForwardVector;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSShotDelegate, const FRTSShotEvent&);

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class URTSAircraftComponent;
class URTSBuilderComponent;
class URTSHarvesterComponent;
class URTSHealthComponent;
class URTSWeaponComponent;
class URTSUnitData;
class URTSUnitMovement;
struct FPathFollowingResult;

/** How ARTSUnit::RequestMove went. */
enum class ERTSMoveRequest : uint8
{
	Moving,
	/** Already stands at the goal: the order is done. */
	AtGoal,
	/** No spot near the target is reachable, or no path to it: the order is dropped and its issuer told. */
	Refused,
};

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

	/**
	 * Server only. True while the front order, a move or an attack-move, waits for URTSMoveRequestSubsystem to search its
	 * path (#174). The unit stands meanwhile, or brakes and finishes a back-off; the order stays at the front of its queue.
	 */
	bool IsMoveRequestPending() const { return bMoveRequestPending; }

	/** Server only. Whether Ticket is the move request the unit waits on, and not one a newer order or a dropped one made stale. */
	bool IsMoveRequestCurrent(uint32 Ticket) const { return bMoveRequestPending && Ticket == MoveRequestTicket; }

	/**
	 * Server only, for URTSMoveRequestSubsystem. Searches the waiting move's path and starts it (RequestMove). A refused
	 * first start tells the issuer, once for the whole group (FRTSCommand::OrderId); a refused or finished move drops the
	 * order and starts the next. Nothing happens once the match is over.
	 */
	void RunMoveRequest();

	/** Server only. Where the last move order sent the unit, unset once an order of another kind starts. */
	TOptional<FVector> GetMoveGoal() const { return MoveGoal; }

	/** Server only. The spot that move order named, which MoveGoal moves out of a wall or a building onto the navmesh. */
	TOptional<FVector> GetMoveTarget() const { return MoveTarget; }

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
	URTSUnitMovement* GetUnitMovement() const { return Movement; }

	/** Every machine. What the tracks or wheels of a vehicle did, for presentation (#151); see FRTSRealizedMotion. */
	const FRTSRealizedMotion& GetRealizedMotion() const;

	/** The gear state clients rebuild the realized motion from; the server's movement sets it. */
	const FRTSLocomotionNet& GetLocomotionNet() const { return LocomotionNet; }
	void SetLocomotionNet(const FRTSLocomotionNet& Net);
	/**
	 * The interaction radius from the data: how far the unit stands off a dock or a building site and holds short of a new
	 * building. Not its size; that is GetShape.
	 */
	float GetSelectionRadius() const;

	/**
	 * How far from a building's edge the unit stands to dock at it or build it: its interaction radius, or, when wider, the
	 * erosion of the navmesh it walks, closer than which its centre cannot come.
	 */
	float GetStandOffRadius() const;

	int32 GetQueuedCommandCount() const { return CommandQueue.Num(); }

	/** The unit's hull, selection bounds and mesh placement, from its data; every machine has them. */
	const FRTSUnitShape& GetShape() const { return Shape; }

	/**
	 * The box the brackets and the health bar frame, in the actor's frame (transform it by GetActorTransform): the hull and the
	 * turret, from the ground to the roof.
	 */
	FBox GetSelectionBounds() const { return Shape.Selection; }

	/** The selection bounds' centre in world space, the point a drag box has to cover to select the unit. */
	FVector GetSelectionCenter() const;

	/** The hull's outline on the ground, turning with the unit. */
	FRTSFootprint GetFootprint() const;

	/** Server only. Where the hull was last found clear of obstacles; see URTSAvoidanceSubsystem. */
	FRTSHullClearanceCache& GetHullClearanceCache() { return HullClearanceCache; }

	/** The edge of the footprint facing From. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	/** Server only: an aircraft in flight. Clients see aircraft as on the ground. */
	virtual bool IsAirborne() const override;

	/** Every machine. Jolts the mesh back for a moment, as a shot recoils; cosmetic only. */
	void PlayFireKick(float EffectScale);

	/**
	 * Every machine that draws. The unit's reaction to its own shot, by URTSUnitData::GunRecoil (#218): the gun's recoiling
	 * parts (FRTSVehiclePart::bRecoils) snap back along the bore and run out again, the gun jumps on its trunnions and the
	 * hull rocks back on its suspension away from the shot. A shot before they have settled carries on from where they are.
	 * Cosmetic only: the replicated aim, the actor, its collision and its movement do not change.
	 */
	void PlayGunRecoil();

	/** Every machine. Runs the recoil on by DeltaSeconds and places the parts it moves; Tick runs it, public so tests can step it. */
	void UpdateGunRecoil(float DeltaSeconds);

	/** Every machine. How far back the recoiling parts are now, in cm before MeshScale; zero at rest. */
	float GetGunRecoilOffset() const { return GunRecoilOffset; }

	/** Every machine. Degrees the gun's jump lifts it above GetGunPitch now. */
	float GetGunJump() const { return GunJump.GetAngle(); }

	/** Every machine. Degrees the hull is rocked back now: X lifting its front, Y lifting its right side. */
	FVector2D GetHullRock() const { return FVector2D(HullLift.GetAngle(), HullSide.GetAngle()); }

	/** Every machine. True while the gun recoils, jumps or the hull rocks. */
	bool IsRecoiling() const { return GunRecoilTime.IsSet() || GunJump.IsActive() || HullLift.IsActive() || HullSide.IsActive(); }

	/**
	 * Every machine, for inspecting a vehicle (rts.Debug.UnitShots). Holds the recoiling parts Time seconds into a stroke
	 * from rest; unset lets them run out again.
	 */
	void SetGunRecoilHold(TOptional<float> Time);

	/** Distance from the actor origin down to the ground contact point: half the hull's height. */
	float GetGroundOffset() const;

	/** Server only. The ground's normal where SnapToGround last stood the unit; up before it first did. */
	FVector GetGroundNormal() const { return GroundNormal.Get(FVector::UpVector); }

	/**
	 * Server only. Stands a ground unit on the ground under it, which the movement does not follow by itself; aircraft keep
	 * their own height. Leaves the unit where it is over no ground. Runs every tick but traces only once the unit has moved
	 * more than a centimetre since it last stood on the ground; public so tests can step it.
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

	/**
	 * True once the turret points at Target closely enough to fire, and not while its aim is blocked (IsAimBlocked); always
	 * true without a turret, which turns the unit.
	 */
	bool IsAimedAt(const AActor& Target) const;

	/**
	 * Server only. True while the barrel's reach comes near an obstacle, so UpdateTurret keeps it out (RTSBarrelClearance)
	 * and URTSAvoidanceSubsystem does not drive it in. False without a barrel.
	 */
	bool IsBarrelNearObstacle() const { return bBarrelNearObstacle; }

	/**
	 * Server only. True while the barrel cannot point at the weapon's target without reaching into an obstacle: the turret
	 * holds where it is clear, the gun waits, and a unit standing still turns its hull toward the target.
	 */
	bool IsAimBlocked() const { return bAimBlocked; }

	/** Server only. Whether the barrel, with the turret and gun as they are, would be clear of obstacles with the unit at Pose. */
	bool IsBarrelClearAt(const FTransform& Pose) const;

	/** Every machine. The barrel as drawn, from where it starts on the turret to the muzzle; false without a turret. */
	bool GetBarrelSegment(FVector& OutStart, FVector& OutEnd) const;

	/** Every machine. The barrel tip in world space, on the gun when the data gives one; only meaningful with a turret. */
	FVector GetMuzzleLocation() const;

	/**
	 * Every machine. The barrel tip with X along the bore: the gun mesh's MuzzleSocketName socket, else the gun at
	 * URTSUnitData::GunMuzzleOffset, else the turret at MuzzleOffset. Follows TurretYaw and GunPitch; only meaningful with a turret.
	 */
	FTransform GetMuzzleTransform() const;

	/** The gun mesh's socket at the tip of its bore. */
	static const FName MuzzleSocketName;

	/**
	 * Every machine that draws. Raised once per shot from URTSWeaponComponent::Multicast_PlayShot; nothing is built for a
	 * unit nobody listens to.
	 */
	FRTSShotDelegate& OnShot() { return ShotDelegate; }

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
	 * Every machine. Rolls the wheels and moves the tracks' links by how far the unit drove and turned since the last call,
	 * each by the travel of its own side (for a tracked vehicle the realized side travel, FRTSRealizedMotion), and places
	 * the wheels on the hull where it stands now; nothing to do while the unit stands. Runs once a frame after every hull
	 * move (URTSRunningGearSubsystem); public so tests can step it. Views says where this machine draws from; without it,
	 * every track's phase goes to its material. Returns how many tracks' phases went to their materials (#212).
	 */
	int32 UpdateRunningGear(const FRTSTrackViews* Views = nullptr);

	/** Every machine. The phase through one link of the track URTSUnitData::Parts[PartIndex] is; false when it is none. */
	bool GetTrackPhase(int32 PartIndex, float& OutPhase) const;

	/**
	 * Every machine. The phase the material of the track URTSUnitData::Parts[PartIndex] draws now, [0, 1), worked out from the
	 * phase, link rate, start time and acceleration its custom primitive data has (RTSPrimitiveData::TrackPhase, #216) as
	 * M_Vehicle works it out; false when it is no track.
	 */
	bool GetTrackMaterialPhase(int32 PartIndex, float& OutPhase) const;

	/**
	 * Of a link: how far the phase a track's material works out from its link rate may drift from the realized travel's before
	 * the track sends its phase again (#216). About 3 mm on the M20, too little to see the links jump by.
	 */
	static constexpr float TrackDriftLimit = 0.02f;

	/**
	 * Of a link per frame: a link rate off from the realized travel's by this much sends the phase at once, before its drift
	 * could wrap past half a link and look small.
	 */
	static constexpr float TrackRateLimit = 0.25f;

	/** Every machine. The components of the unit's tracks, animated or rigid (#212). */
	void GetTrackComponents(TArray<const UStaticMeshComponent*, TInlineAllocator<2>>& OutComponents) const;

	/** Every machine. The roll in radians, [0, 2 pi), of the wheel URTSUnitData::Parts[PartIndex] is; false when it is none. */
	bool GetWheelAngle(int32 PartIndex, float& OutRadians) const;

	/** The colour of team InTeamId; units and buildings give it to their materials as RTSPrimitiveData::TeamColor. */
	static FLinearColor GetTeamColor(int32 InTeamId);

	/** Feet, not eye height: navmesh queries start from the ground. */
	virtual FVector GetNavAgentLocation() const override;

	virtual void PostInitializeComponents() override;
	virtual void Destroyed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

	/**
	 * Turns the wanted turret Yaw and gun Pitch into the closest ones that keep the barrel out of obstacles, where the unit
	 * stands and where its path takes it shortly, once the barrel's reach comes near one. See IsAimBlocked.
	 */
	void KeepBarrelClear(float& Yaw, float& Pitch, bool bHasTarget);

	/**
	 * Whether the turret can turn to the clear Aim (yaw, pitch) without sweeping its barrel through an obstacle on the way:
	 * Aim stays when the short way is clear, becomes a point round the back when only the long way is, or keeps the turret
	 * where it is while the gun first moves to Aim's pitch when only that clears the swing. False when neither way is clear.
	 */
	bool RouteToClearAim(FVector2D& Aim) const;

	/** Where the unit will stand, facing along its path, once it has driven Distance further along it; here without a path. */
	FTransform GetPoseAlongPath(float Distance) const;

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
	 * Paths to Location by a full path. When it lies inside a wall or a building, paths to the reachable spot nearest it
	 * instead (RTSMoveDestination). Refused when Location lies in an enclosed area or on another navmesh island.
	 */
	ERTSMoveRequest RequestMove(AAIController& AI, const FVector& Location);

	/**
	 * Starts the front order's move: queues it with URTSMoveRequestSubsystem, which runs RunMoveRequest now or in a later
	 * frame, and stops the move under way when it has to wait. bTellRefusal tells the issuer if it is refused. In a world
	 * without the subsystem it runs RequestMove at once. False when the order is over already: refused, or at its goal.
	 */
	bool StartMoveRequest(AAIController& AI, bool bTellRefusal);

	/** Makes a waiting move request stale, for an order that ends or is replaced before it ran. */
	void CancelMoveRequest();

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

	/**
	 * Server only. Each tick for a vehicle under way: one whose gear has been stuck against something for a moment backs off
	 * a little and paths again; stuck again soon after, or its goal out of reach of its steering, it gives the move up.
	 */
	void UpdateBlockedMove();

	void FaceTowardYaw(float Yaw, float DeltaSeconds);

	/** Eases the mesh back from a fire kick. Returns true when the mesh has to move. */
	bool UpdateFireKick(float DeltaSeconds);

	/** Places the recoiling parts GunRecoilOffset back from their rest along the gun's bore. */
	void PlaceRecoilParts();

	/** Sets the gun's rotation from GunPitch and its jump. */
	void PlaceGun();

	/**
	 * Eases the mesh of a vehicle that tilts to the ground toward the slope under it, and every other mesh back upright.
	 * Returns true when the mesh has to move.
	 */
	bool UpdateTilt(float DeltaSeconds);

	FRTSShotDelegate ShotDelegate;

	/** Places the mesh from its rest location, tilt and fire kick; it leans around the bottom of the hull box. */
	void UpdateMeshTransform();

	/** The hull box: picking, the movement component's updated component and the Pawn overlaps of building placement. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

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
	TObjectPtr<URTSUnitMovement> Movement;

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

	UPROPERTY(Replicated)
	FRTSLocomotionNet LocomotionNet;

	/** Server only. World seconds of the last time UpdateBlockedMove paths a stuck vehicle again. */
	double BlockedReplanTime = -1.0e9;

	/** The realized travel UpdateRunningGear last rolled the wheels to: forward and turned, and each side's. */
	double GearDriven = 0.0;
	double GearTurned = 0.0;
	double GearLeft = 0.0;
	double GearRight = 0.0;
	/** World seconds UpdateRunningGear last ran at, for the tracks' link rates. */
	TOptional<double> GearTime;

	/** Server only. See SetAimOverride. */
	TOptional<FVector2D> AimOverride;

	/** Server only. Where the barrel's reach was last found clear of the navmesh's walls; see KeepBarrelClear. */
	FRTSHullClearanceCache BarrelReachCache;
	bool bBarrelNearObstacle = false;
	bool bAimBlocked = false;
	/** World seconds after which a barrel near an obstacle looks at its reach, and its aim, again. */
	double NextBarrelReachCheck = 0.0;
	double NextBarrelAimCheck = 0.0;
	/** The aim last wanted and the clear one found for it. */
	FVector2D CheckedAim = FVector2D::ZeroVector;
	FVector2D ClearAim = FVector2D::ZeroVector;
	bool bClearAimFound = false;

	/** Where each of URTSUnitData::Parts is drawn: its component, its instance and wheel when that is instanced, its track. */
	struct FPartSlot
	{
		UStaticMeshComponent* Component = nullptr;
		int32 Instance = INDEX_NONE;
		int32 Wheel = INDEX_NONE;
		int32 Track = INDEX_NONE;
	};

	/**
	 * A track whose links move (FRTSVehiclePart::TrackPitch, #151): its material shifts every link part of the way to the
	 * next one's place by the phase, so one link of travel looks the same as none and the phase only counts through one.
	 */
	struct FTrack
	{
		UStaticMeshComponent* Component = nullptr;
		/** One link's length in world cm. */
		float Pitch = 1.f;
		/** In world cm, from the hull's middle, negative on the left: whose travel moves it. */
		float Side = 0.f;
		/**
		 * [0, 1): how far each link is on its way toward the next one's place, which lies ahead of it along the loop's
		 * bottom run. Driving forward runs the bottom of the track backward under the hull, so forward travel lowers it.
		 */
		float Phase = 0.f;
		/** Links per second the phase changed by over the last frame: negative driving forward, zero standing. */
		float Rate = 0.f;
		/** Links per second squared Rate changed by since the frame before. */
		float Acceleration = 0.f;
		/**
		 * What the material has (#216): the phase at SentStart, in world seconds, and the link rate and acceleration it counts
		 * on by from there. The custom primitive data starts at zero, which is a standing track at phase 0.
		 */
		float SentPhase = 0.f;
		float SentRate = 0.f;
		float SentStart = 0.f;
		float SentAcceleration = 0.f;
	};
	TArray<FTrack> Tracks;

	/** Makes the part in Slot a track, or for a unit whose tracks do not animate draws it with its rigid material. */
	void AddTrack(FPartSlot& Slot, const FRTSVehiclePart& Part);

	/** The phase Track's material draws at world time Now, unwrapped, as M_Vehicle works it out from what it was sent. */
	static float GetSentPhase(const FTrack& Track, float Now);

	/** True when Track's phase should go to its material now; see FRTSTrackViews. */
	bool IsTrackDrawn(const FTrack& Track, const FRTSTrackViews* Views) const;
	TArray<FPartSlot> PartSlots;

	/** A wheel's roll, kept per instance of its instanced component. */
	struct FWheel
	{
		/** Its set in WheelSets, and its instance there. */
		int32 Set = 0;
		int32 Instance = 0;
		/** Its rest place on the hull, before the roll. */
		FTransform Rest;
		/** In world cm (FRTSVehiclePart::RollRadius); with Side, how far it rolls for a distance driven and an angle turned. */
		float Radius = 1.f;
		float Side = 0.f;
		float Angle = 0.f;
	};
	TArray<FWheel> Wheels;

	/**
	 * The instanced component for every wheel of one mesh, with the world transforms UpdateRunningGear writes to it and
	 * those it wrote the frame before, for the renderer's motion vectors.
	 */
	struct FWheelSet
	{
		UInstancedStaticMeshComponent* Component = nullptr;
		TArray<FTransform> Transforms;
		TArray<FTransform> PrevTransforms;
	};
	TArray<FWheelSet> WheelSets;

	/** The hull's world transform UpdateRunningGear last placed the wheels on. */
	TOptional<FTransform> GearHull;
	/** The wheels' previous transforms are their current ones: standing, they show no motion. */
	bool bWheelsSettled = true;

	/** Where Wheel is drawn in world space with its roll, on the hull as it stands now. */
	FTransform GetWheelTransform(const FWheel& Wheel) const;

	/** The unit's place when UpdateRunningGear last ran. */
	TOptional<FTransform> LastGearPose;

	/** Server only. Front entry is the order being executed. A UPROPERTY so GC sees and clears attack targets. */
	UPROPERTY(Transient)
	TArray<FRTSCommand> CommandQueue;

	bool bRequestingMove = false;
	bool bMoveFinishedDuringRequest = false;

	/** Server only. See IsMoveRequestPending; the ticket tells the waiting request from older, stale ones. */
	bool bMoveRequestPending = false;
	bool bMoveRequestTellsRefusal = false;
	uint32 MoveRequestTicket = 0;

	/** Set while an attack-move stops to fight, so the aborted move does not end the order. */
	bool bStoppingToFight = false;

	TOptional<FVector> MoveGoal;
	TOptional<FVector> MoveTarget;

	/** From UnitData in ApplyUnitData; the box is fitted to its hull. */
	FRTSUnitShape Shape;

	/** Server only. URTSAvoidanceSubsystem's last hull clearance check. */
	FRTSHullClearanceCache HullClearanceCache;

	/** Where ApplyUnitData stood the mesh; a fire kick moves it back from here and returns it. */
	FVector MeshRestLocation = FVector::ZeroVector;
	float FireKickDistance = 0.f;
	float FireKickTimeLeft = 0.f;

	/** A part on the gun that recoils (FRTSVehiclePart::bRecoils), and where it rests in the gun's space. */
	struct FRecoilPart
	{
		UStaticMeshComponent* Component = nullptr;
		FVector Rest = FVector::ZeroVector;
	};
	TArray<FRecoilPart> RecoilParts;

	/** Seconds into the running recoil stroke; unset at rest. */
	TOptional<float> GunRecoilTime;
	/** How far back the stroke started, in cm before MeshScale. */
	float GunRecoilFrom = 0.f;
	float GunRecoilOffset = 0.f;
	/** Holds the recoil where it is (SetGunRecoilHold). */
	bool bGunRecoilHeld = false;
	/** The gun's jump on its trunnions, and the hull's rock: lifting its front and its right side, in degrees. */
	FRTSRecoilSpring GunJump;
	FRTSRecoilSpring HullLift;
	FRTSRecoilSpring HullSide;

	FRotator MeshTilt = FRotator::ZeroRotator;

	/** Server only. The ground's normal where SnapToGround last stood the unit; clients trace their own. */
	TOptional<FVector> GroundNormal;

	/**
	 * Where the unit stood after the last ground trace: SnapToGround's on the server, UpdateTilt's on clients. Within
	 * GroundTraceSkipDistance of it the ground is the same, so a unit standing still does not trace again.
	 */
	TOptional<FVector> GroundTraceLocation;

	/** Clients only. The ground's normal UpdateTilt last traced. */
	FVector ClientGroundNormal = FVector::UpVector;
};
