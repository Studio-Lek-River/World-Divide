#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSUnitData.generated.h"

/** What a vehicle part rides on: it follows that part's turn, and the gun's elevation for Gun. */
UENUM(BlueprintType)
enum class ERTSVehiclePartMount : uint8
{
	Hull,
	Turret,
	Gun,
};

class UMaterialInterface;
class UNiagaraSystem;

/** A part beyond the hull, turret and gun: an add-on such as a weapon station, or a road wheel. */
USTRUCT(BlueprintType)
struct FRTSVehiclePart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	ERTSVehiclePartMount MountedOn = ERTSVehiclePartMount::Hull;

	/** Where the part's pivot sits in the space of what it is mounted on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector Offset = FVector::ZeroVector;

	/** The part's turn on its mount, such as a right-hand wheel that is the left one turned round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FRotator Rotation = FRotator::ZeroRotator;

	/**
	 * A hull wheel that rolls with the ground under it as the unit drives and turns, about its own Y axis; its radius is half
	 * the mesh's height. Wheels sharing a mesh are drawn as one instanced component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	bool bWheel = false;

	/**
	 * A wheel's rolling radius in cm, before the unit's MeshScale: where its rim meets the ground, or for a sprocket or an
	 * idler the track's centre line wrapped round it. Zero takes half the mesh's height.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (ClampMin = "0", EditCondition = "bWheel"))
	float RollRadius = 0.f;

	/**
	 * Non-zero makes a hull part a track: the length in cm of one of its links, before the unit's MeshScale. Its material
	 * (M_Vehicle with TrackAnimation, #151) moves the links round their loop by the travel of the track's side, from the
	 * part's Offset.Y, which the unit gives it as a phase through one link and a link rate in custom primitive data (#216).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (ClampMin = "0"))
	float TrackPitch = 0.f;

	/**
	 * A track's material without the link animation, for every slot: drawn instead while rts.Vehicle.TrackAnimation is 0,
	 * and the one a track's far LODs take once it has them (#149). None keeps the mesh's own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TObjectPtr<UMaterialInterface> RigidMaterial;

	/** A part on the gun that slides back along the bore when it fires and runs out again: the barrel (GunRecoil, #218). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	bool bRecoils = false;
};

/**
 * How a vehicle reacts to its own gun firing, drawn on each machine that shows it (#218): the barrel (FRTSVehiclePart::
 * bRecoils) snaps back along the bore and the recuperator runs it out again, braked by its buffer toward the end; the
 * gun kicks up on its trunnions; and the hull rocks back on its suspension, lifting on the side the gun points to.
 */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSGunRecoil
{
	GENERATED_BODY()

	/** How far the barrel slides back in cm, before the unit's MeshScale; 0 does not recoil. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0"))
	float Distance = 0.f;

	/** Seconds to slide back the full stroke: a real gun takes about a hundredth, a frame on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0.001"))
	float RecoilSeconds = 0.015f;

	/** Seconds the barrel stays fully back before it runs out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0"))
	float HoldSeconds = 0.03f;

	/** Seconds to run back out to rest: quick at first, then slowed by the buffer over the last part. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0.01"))
	float ReturnSeconds = 0.7f;

	/** Degrees the gun kicks up on its trunnions at its highest; 0 does not. It swings back past rest once and settles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0"))
	float JumpDegrees = 0.f;

	/** Seconds the gun's jump takes to settle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0.05"))
	float JumpSeconds = 0.4f;

	/**
	 * How the jump settles, as the damping ratio of its spring: the stiff elevation drive at 0.45 swings back past rest by about
	 * a fifth of the kick; nearer 1 it swings back less.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float JumpDamping = 0.45f;

	/**
	 * Degrees the hull rocks back at its most, lifting on the side the gun points to and pivoting on the opposite edge of
	 * its bottom, so it never sinks into the ground; 0 leaves the hull still. Only the mesh moves: the actor, its collision
	 * and its movement do not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0"))
	float HullRockDegrees = 0.f;

	/** Seconds the hull takes to settle on its suspension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0.05"))
	float HullRockSeconds = 1.f;

	/**
	 * How the rock settles, as the damping ratio of its spring (#219): at 0.6 the hull swings back past rest by under a tenth
	 * of its rock, a few millimetres on the M20, so it comes to rest without a visible bounce.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float HullRockDamping = 0.6f;

	/** How far back the barrel is, in cm before MeshScale, Time seconds into a stroke that started From cm back. */
	float OffsetAt(float Time, float From) const;

	/** Seconds a stroke lasts, back, held and out again. */
	float GetSeconds() const { return RecoilSeconds + HoldSeconds + ReturnSeconds; }

	/** Seconds until the stroke, the jump and the rock have all settled. */
	float GetSettleSeconds() const { return FMath::Max3(GetSeconds(), JumpSeconds, HullRockSeconds); }
};

/**
 * A vehicle's running engine, drawn on each machine that shows it (#222): light exhaust at its outlets, from how the hull
 * actually moves (FRTSRealizedMotion), never from its orders. Idle below IdleSpeed, accelerating while its speed climbs
 * faster than AccelerationThreshold, moving otherwise. No outlets, no exhaust.
 */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSVehicleExhaust
{
	GENERATED_BODY()

	/**
	 * The engine's continuous emitter, driven by its User.Rate, User.Color, User.Opacity, User.Size and User.Direction
	 * (URTSExhaustSubsystem).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust")
	TObjectPtr<UNiagaraSystem> System;

	/** Where the exhaust leaves the hull, in Mesh's own space as the parts' offsets (MeshScale applies). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust")
	TArray<FVector> Outlets;

	/** Which way the exhaust leaves the outlets, in Mesh's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust")
	FVector Direction = FVector(-1.f, 0.f, 0.f);

	/** Puffs a second from each outlet, standing, moving and accelerating, before the effects quality scales them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0"))
	float IdleRate = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0"))
	float MovingRate = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0"))
	float AcceleratingRate = 20.f;

	/**
	 * More puffs a second for each metre a second the hull drives, so the trail it leaves stays closed at any speed: 5 a
	 * metre, one every 20 cm, a third of a puff's width.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0"))
	float PuffsPerMetre = 5.f;

	/**
	 * The colour a puff is born with, standing, moving and accelerating (#325): the engine's character. A light diesel grey
	 * by default; a puff keeps its colour as the state changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust")
	FLinearColor IdleColor = FLinearColor(0.42f, 0.41f, 0.4f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust")
	FLinearColor MovingColor = FLinearColor(0.42f, 0.41f, 0.4f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust")
	FLinearColor AcceleratingColor = FLinearColor(0.42f, 0.41f, 0.4f);

	/** How opaque a puff is born, standing, moving and accelerating: light in normal running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0", ClampMax = "1"))
	float IdleOpacity = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0", ClampMax = "1"))
	float MovingOpacity = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0", ClampMax = "1"))
	float AcceleratingOpacity = 0.6f;

	/** How wide a puff is born in cm; it grows as it drifts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "1"))
	float Size = 60.f;

	/** cm/s under which the hull counts as standing, unless it pivots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0"))
	float IdleSpeed = 30.f;

	/** cm/s² the hull's speed must climb by, smoothed, to count as accelerating. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "1"))
	float AccelerationThreshold = 100.f;

	/** Seconds over which the speed's climb is smoothed, so a client's replicated speed does not flicker the state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0.01"))
	float AccelerationSmoothing = 0.15f;

	/** Seconds the accelerating state holds after the climb last passed the threshold, so it does not flicker either. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exhaust", meta = (ClampMin = "0"))
	float AccelerationHold = 0.4f;

	bool HasExhaust() const { return System != nullptr && !Outlets.IsEmpty(); }
};

/**
 * A damped spring kicked by each shot (FRTSGunRecoil's gun jump and hull rock): an angle that rises to its peak, swings back
 * past rest by a little and settles within its settle time. A kick before it has settled adds to how it moves now, so
 * the angle never jumps.
 */
struct WORLDDIVIDE_API FRTSRecoilSpring
{
	/** Kicks the spring so that from rest it would peak at Peak (degrees); Damping below 1 sets how far it swings back. */
	void Kick(float Peak, float SettleSeconds, float Damping);

	/** Runs the spring on; at its settle time it comes to rest at exactly zero. */
	void Advance(float DeltaSeconds);

	float GetAngle() const { return Angle; }
	bool IsActive() const { return bActive; }
	void Reset() { *this = FRTSRecoilSpring(); }

private:
	/** The angle and rate at the last kick, the time since, and the spring's decay and swing from that kick. */
	float FromAngle = 0.f;
	float FromRate = 0.f;
	float Time = 0.f;
	float Settle = 1.f;
	float Decay = 0.f;
	float Swing = 1.f;
	float Angle = 0.f;
	bool bActive = false;

	/** The angle and rate Elapsed seconds after the last kick. */
	void Evaluate(float Elapsed, float& OutAngle, float& OutRate) const;
};

/** What moves a ground unit: legs walk anywhere they face, tracks and wheels drive the hull (RTSLocomotion). */
UENUM(BlueprintType)
enum class ERTSLocomotionType : uint8
{
	/** Infantry: walks the way its path goes and turns to face it. Aircraft fly by their own rules and ignore it. */
	Legs,
	/** Left and right tracks at their own speeds: drives, turns and, when allowed, pivots on the spot. */
	Tracked,
	/** Steered front wheels: turns only while rolling, no tighter than its turning circle. */
	Wheeled,
};

/**
 * How a vehicle's tracks or wheels move its hull (RTSLocomotion, URTSUnitMovement). URTSUnitData::MoveSpeed is the top
 * speed forward and URTSUnitData::TurnRate the fastest it turns. Distances in cm, angles in degrees, times in seconds.
 */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSLocomotionProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion")
	ERTSLocomotionType Type = ERTSLocomotionType::Legs;

	/** Top speed backwards in cm/s; 0 never reverses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "0"))
	float ReverseSpeed = 0.f;

	/** cm/s each second gained toward a faster speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1"))
	float Acceleration = 400.f;

	/** cm/s each second shed toward a slower speed or a standstill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1"))
	float Braking = 1000.f;

	/** Tracked: the distance between the middles of the two tracks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Tracked"))
	float TrackGauge = 280.f;

	/** Tracked: turns on the spot, one track forward and one back, when its way lies far to the side or behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (EditCondition = "Type == ERTSLocomotionType::Tracked"))
	bool bAllowPivot = true;

	/** Tracked: turns on the spot first once its way lies more than this many degrees off its heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", ClampMax = "179", EditCondition = "Type == ERTSLocomotionType::Tracked"))
	float PivotAngle = 60.f;

	/** Wheeled: the distance between the front and rear axles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float Wheelbase = 400.f;

	/** Wheeled: the distance between the left and right wheels, for how far each side rolls in a turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float AxleTrack = 220.f;

	/** Wheeled: the furthest the front wheels steer either way, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", ClampMax = "80", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float MaxSteerAngle = 35.f;

	/** Wheeled: degrees per second the front wheels steer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float SteerRate = 60.f;

	/** Wheeled: the tightest turn in cm, at the rear axle; 0 leaves it to the steering, Wheelbase / tan(MaxSteerAngle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "0", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float MinTurnRadius = 0.f;

	/** cm/s² of sideways pull a turn may take before the vehicle slows for it: its grip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1"))
	float MaxLateralAcceleration = 400.f;

	/** True for tracks and wheels. */
	bool IsVehicle() const { return Type != ERTSLocomotionType::Legs; }

	/** Wheeled: the tightest turn the profile allows, from MinTurnRadius and the steering. */
	float GetMinTurnRadius() const;

	/** Whether every value the type uses is finite and in range; the first problem in OutError. Legs is always valid. */
	bool Validate(FString* OutError = nullptr) const;
};

UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSUnitData : public URTSEntityData
{
	GENERATED_BODY()

public:
	/** Top speed in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "0"))
	float MoveSpeed = 400.f;

	/** Degrees per second the unit turns toward its heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float TurnRate = 360.f;

	/** Tracks or wheels, or legs (the default); aircraft ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FRTSLocomotionProfile Locomotion;

	/** Vehicles tilt their mesh to the slope they stand on; infantry stays upright. Aircraft ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	bool bTiltsToGround = false;

	/**
	 * A turret that traverses on its own, carried by Mesh: the hull then only turns to drive and the turret aims. Without
	 * one the whole unit turns toward its target.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> TurretMesh;

	/** Where the turret's pivot sits on Mesh, in Mesh's own space (MeshScale applies to both). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector TurretOffset = FVector::ZeroVector;

	/** The barrel tip in the turret's own space, where shots start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector MuzzleOffset = FVector::ZeroVector;

	/** Degrees per second the turret traverses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float TurretTurnRate = 120.f;

	/**
	 * A gun that elevates on its own, carried by the turret: it pitches toward its target's height. With one, shots start at
	 * GunMuzzleOffset on the gun instead of MuzzleOffset on the turret.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> GunMesh;

	/** Where the gun's trunnion sits on the turret, in the turret's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector GunOffset = FVector::ZeroVector;

	/** The barrel tip in the gun's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector GunMuzzleOffset = FVector::ZeroVector;

	/** Depression and elevation limits in degrees, up positive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float GunMinPitch = -8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float GunMaxPitch = 20.f;

	/** Degrees per second the gun elevates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float GunElevationRate = 30.f;

	/** How the gun's recoiling parts (the barrel) move on each shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FRTSGunRecoil GunRecoil;

	/** The engine's exhaust; none without outlets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FRTSVehicleExhaust Exhaust;

	/** Add-ons and running gear, placed on the hull, the turret or the gun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TArray<FRTSVehiclePart> Parts;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
