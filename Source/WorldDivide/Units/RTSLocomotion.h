#pragma once

#include "CoreMinimal.h"
#include "Data/RTSUnitData.h"
#include "RTSLocomotion.generated.h"

/** What a vehicle's running gear did on its last update. */
UENUM(BlueprintType)
enum class ERTSLocomotionStatus : uint8
{
	/** Standing, with nothing to do. */
	Idle,
	Driving,
	Reversing,
	/** Turning on the spot: tracks running opposite ways. */
	Pivoting,
	/** Slowing down, toward a stop or a pivot. */
	Braking,
	/** Driving into something: the hull did not move as far as the gear tried. */
	Blocked,
};

/**
 * What a vehicle's hull actually did, after collision, for presentation (#151 animates the M20's tracks from it). Every
 * machine has it: the server from its own simulation, clients from the replicated speed and yaw rate. Moves the gear did
 * not drive, such as being pushed apart from another unit, backed out of an obstacle, teleported or corrected by the
 * network, never count as travel.
 *
 * Conventions: Unreal's, X forward, Y right, yaw positive to the right (clockwise seen from above). A right turn drives the
 * left side faster: over a step that drives the hull ForwardDistance and turns it Yaw radians,
 *   LeftTravel  = ForwardDistance + SideSpacing * Yaw / 2
 *   RightTravel = ForwardDistance - SideSpacing * Yaw / 2
 * which is #138's dLeft = ds - gauge * dYaw / 2 with its yaw counted positive to the left. SideSpacing is the track gauge,
 * or the wheels' axle track. Distances in cm, angles in degrees unless named Radians.
 */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSRealizedMotion
{
	GENERATED_BODY()

	/** cm/s along the hull's forward axis, negative reversing. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float ForwardSpeed = 0.f;

	/** Degrees per second, positive turning right. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float YawRate = 0.f;

	/** The front wheels' steering angle in degrees, positive to the right; zero for tracks. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float SteerAngle = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	ERTSLocomotionStatus Status = ERTSLocomotionStatus::Idle;

	/** Running totals since this machine started tracking the unit: forward travel in cm and turn in radians, both signed. */
	double DrivenDistance = 0.0;
	double TurnedRadians = 0.0;

	/** Running totals of each side's travel in cm, signed: what a track's links or a side's wheels rolled. */
	double LeftTravel = 0.0;
	double RightTravel = 0.0;

	/** Server only: what the gear was told this update, so a test can tell the order from the outcome. */
	float CommandedSpeed = 0.f;
	float CommandedYawRate = 0.f;

	/** Server only: ground displacement the gear did not drive (pushes, back-outs, teleports), summed, and how many there were. */
	FVector2D RecoveryDisplacement = FVector2D::ZeroVector;
	int32 RecoveryCount = 0;
};

/** The few values a client needs, beyond the replicated velocity, to rebuild FRTSRealizedMotion; sent only when they change. */
USTRUCT()
struct WORLDDIVIDE_API FRTSLocomotionNet
{
	GENERATED_BODY()

	/** Yaw rate in quarter degrees per second. */
	UPROPERTY()
	int16 YawRate = 0;

	/** Steering angle in half degrees. */
	UPROPERTY()
	int8 SteerAngle = 0;

	UPROPERTY()
	ERTSLocomotionStatus Status = ERTSLocomotionStatus::Idle;

	bool operator==(const FRTSLocomotionNet& Other) const
	{
		return YawRate == Other.YawRate && SteerAngle == Other.SteerAngle && Status == Other.Status;
	}
};

/**
 * Lightweight kinematics of tracked and wheeled hulls, free of the world so they can be tested and run for hundreds of
 * vehicles without allocating. Units: cm, seconds, radians; yaw positive to the right, as in Unreal.
 *
 * Tracked (differential drive): the tracks run at Left and Right; the hull drives at their mean and turns at their difference
 * over the gauge, v = (Left + Right) / 2 and yaw rate = (Left - Right) / gauge. Equal speeds drive straight, unequal ones
 * turn, opposite ones pivot.
 * Wheeled (bicycle model): the hull drives at Speed and turns at yaw rate = Speed * tan(Steer) / wheelbase, so it cannot
 * turn standing and reversing turns it the other way.
 * Both ramp their speeds by the profile's acceleration toward a faster speed and its braking toward a slower one, through
 * zero when they change direction, and move along the arc those speeds describe.
 */
namespace RTSLocomotion
{
	/** One unit's limits, from its data. */
	struct WORLDDIVIDE_API FLimits
	{
		ERTSLocomotionType Type = ERTSLocomotionType::Legs;
		float ForwardSpeed = 0.f;
		float ReverseSpeed = 0.f;
		float Acceleration = 1.f;
		float Braking = 1.f;
		float MaxYawRate = 1.f;
		float MaxLateralAcceleration = 1.f;
		/** Tracked. */
		float TrackGauge = 1.f;
		bool bAllowPivot = false;
		float PivotAngle = UE_HALF_PI;
		/** Wheeled. */
		float Wheelbase = 1.f;
		float AxleTrack = 1.f;
		float MaxSteer = 0.f;
		float SteerRate = 0.f;
		float MinTurnRadius = 0.f;

		static FLimits FromData(const URTSUnitData& Data);

		bool IsTracked() const { return Type == ERTSLocomotionType::Tracked; }
		bool IsWheeled() const { return Type == ERTSLocomotionType::Wheeled; }

		/** The distance between the two sides that roll: the track gauge, or the wheels' axle track. */
		float GetSideSpacing() const { return IsTracked() ? TrackGauge : AxleTrack; }

		/** How fast a track may run backward: in reverse, or on a pivot when that is allowed. */
		float GetTrackReverseLimit() const;
	};

	/** The running gear's state: the track speeds, or the wheels' speed and steering. */
	struct WORLDDIVIDE_API FDrive
	{
		float Left = 0.f;
		float Right = 0.f;
		float Speed = 0.f;
		float Steer = 0.f;

		float GetSpeed(const FLimits& Limits) const { return Limits.IsTracked() ? 0.5f * (Left + Right) : Speed; }
		float GetYawRate(const FLimits& Limits) const;
	};

	/**
	 * What the gear is told: a speed along the hull, signed, and the curvature to drive it on (1/cm, positive to the right),
	 * or for tracks a yaw rate to pivot at on the spot.
	 */
	struct WORLDDIVIDE_API FCommand
	{
		float Speed = 0.f;
		float Curvature = 0.f;
		bool bPivot = false;
		float PivotYawRate = 0.f;

		/** Tracks running at Left and Right, as the command that holds them there. */
		static FCommand FromTracks(const FLimits& Limits, float Left, float Right);
	};

	/** What one update drove: the distance along the arc, its turn, and where it ends in the hull's frame at its start. */
	struct WORLDDIVIDE_API FStep
	{
		float Distance = 0.f;
		float Yaw = 0.f;
		FVector2D Offset = FVector2D::ZeroVector;
	};

	/** Advances Drive by Dt toward Command within Limits; Out is the arc it drove. */
	WORLDDIVIDE_API void Step(const FLimits& Limits, const FCommand& Command, FDrive& Drive, float Dt, FStep& Out);

	/** Where an arc of Distance that turns Yaw ends, in the frame of its start. */
	WORLDDIVIDE_API FVector2D ArcChord(float Distance, float Yaw);

	/** How far Speed takes to brake to a stop at Braking. */
	WORLDDIVIDE_API float StoppingDistance(float Speed, float Braking);

	/**
	 * Ramps Speed toward Target over Dt: Accel toward a faster speed, Brake toward a slower one, through zero first when the
	 * sign changes. Returns the distance covered, exact for the ramp.
	 */
	WORLDDIVIDE_API float Ramp(float& Speed, float Target, float Accel, float Brake, float Dt);

	/** What route guidance knows of the unit and its path, for one update. */
	struct WORLDDIVIDE_API FGuidanceInput
	{
		FVector2D Location = FVector2D::ZeroVector;
		/** Radians. */
		float Yaw = 0.f;
		/** The gear's speed along the hull now, signed. */
		float Speed = 0.f;
		float Dt = 0.f;
		/** The path on the ground, from where the unit started to its goal; empty without a move. */
		TConstArrayView<FVector2D> Path;
		/** A heading to turn to while standing without a path, in radians: tracks pivot to it. */
		TOptional<float> FacingYaw;
		/** The hull's length, for how short a way back it reverses instead of turning round. */
		float HullLength = 0.f;
		/** From avoidance: a share of the speed to keep, and a curvature to add. */
		float SpeedScale = 1.f;
		float CurvatureBias = 0.f;
	};

	/** What guidance carries from one update to the next. Reset whenever the path changes. */
	struct WORLDDIVIDE_API FGuidanceState
	{
		/** The path point the unit heads for. */
		int32 Target = 1;
		bool bPivoting = false;
		bool bReversing = false;
		/** Wheeled: backing or swinging out of a turning circle that holds the goal, and how many times it did. */
		bool bManoeuvring = false;
		int32 Manoeuvres = 0;
		/** The goal cannot be reached within the profile: the move should end. */
		bool bFailed = false;
	};

	/**
	 * Turns the path into a command the gear can follow: pure pursuit of a point ahead on the path, braking in time for the
	 * goal and the corners, slowing for the curvature and grip. Tracks stop and pivot when their way turns more than the
	 * pivot angle, and at sharp corners; both reverse to a goal a short way behind when the profile allows it. A wheeled
	 * vehicle whose goal lies inside its turning circle backs (or swings) out of it a bounded number of times, then fails.
	 */
	WORLDDIVIDE_API FCommand Guide(const FLimits& Limits, const FGuidanceInput& Input, FGuidanceState& State);
}
