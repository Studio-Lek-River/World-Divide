#pragma once

#include "CoreMinimal.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Navigation/RTSHullClearance.h"
#include "Units/RTSLocomotion.h"
#include "RTSUnitMovement.generated.h"

class ARTSUnit;
class URTSUnitData;
struct FNavPathPoint;
struct FRTSFootprint;

/**
 * Moves an ARTSUnit. Units on legs move as a floating pawn does: path following sets their velocity and the unit turns to
 * face it. Vehicles (URTSUnitData::Locomotion, tracked or wheeled) are driven by their running gear instead, in three
 * layers kept apart:
 *  1. Navigation: path following and the hull-fitted path (RTSHullClearance::AdjustPath) only say where to go; their move
 *     requests are ignored.
 *  2. Locomotion (server only): route guidance turns the path into a command for the gear, the gear's kinematics
 *     (RTSLocomotion) turn it into an arc, and the arc is checked against obstacles before the hull moves along it, by as much
 *     of it as is clear. The hull never slides sideways or turns by itself.
 *  3. Presentation: GetRealizedMotion says what the gear did, on every machine, for the tracks and wheels to animate.
 * Everything that moves a vehicle besides its gear (avoidance pushes, backing out of obstacles, teleports) is told apart
 * from what it drove and reported as recovery.
 */
UCLASS()
class WORLDDIVIDE_API URTSUnitMovement : public UFloatingPawnMovement
{
	GENERATED_BODY()

public:
	URTSUnitMovement();

	/** From the unit's data and shape, on every machine; a profile that is not valid falls back to legs, with a warning. */
	void ApplyUnitData(const URTSUnitData& Data, float InHullLength);

	/** True for a tracked or wheeled unit, whose gear drives it. */
	bool UsesLocomotion() const { return bUsesLocomotion; }

	const RTSLocomotion::FLimits& GetLimits() const { return Limits; }

	/** What the hull did, for presentation; see FRTSRealizedMotion. */
	const FRTSRealizedMotion& GetRealizedMotion() const { return Realized; }

	/** Server only. The gear's state now. */
	const RTSLocomotion::FDrive& GetDrive() const { return Drive; }

	/** Server only. How far the hull takes to brake to a stop from its speed now. */
	float GetStoppingDistance() const;

	/** Server only. Seconds the gear has been driving into something without getting anywhere; zero once it moves again. */
	float GetBlockedSeconds() const { return BlockedSeconds; }

	/** Server only. True when guidance found the goal out of reach of the profile (a wheeled vehicle that cannot turn to it). */
	bool HasGuidanceFailed() const { return Guidance.bFailed; }

	/** Server only. Guidance gives the goal up as out of reach, for tests; a new path starts it over. */
	void FailGuidance() { Guidance.bFailed = true; }

	/** Server only. The path point guidance heads for: path following counts the points before it as reached. */
	int32 GetGuidanceTarget() const { return Guidance.Target; }

	/** Server only. Path following has a new or updated path: guidance starts on it from its first stretch. */
	void SetPath(const TArray<FNavPathPoint>& Points);

	/** Server only. A heading, in degrees, for a standing tracked unit to pivot to, such as toward its target; unset for none. */
	void SetFacingRequest(TOptional<float> Yaw) { FacingYaw = Yaw; }

	/** Server only. Avoidance's request for the next update: a share of the speed to keep and a curvature (1/cm) to add. */
	void SetAvoidance(float SpeedScale, float CurvatureBias);

	/** Server only. Drives with Command instead of following the path, for tests; unset follows the path again. */
	void SetCommandOverride(TOptional<RTSLocomotion::FCommand> Command) { CommandOverride = Command; }

	/**
	 * Server only. Backs straight off what the hull is stuck against for Seconds, slowly, before it follows its path again;
	 * nothing for a profile that never reverses.
	 */
	void BackOff(float Seconds);

	/** Server only. Stops the gear dead, not braking: the match ended, or the unit was put somewhere else. */
	void HaltImmediately();

	/**
	 * Server only. Whether the hull may stand at Footprint as far as the solids the navmesh leaves out go (#168): clear of
	 * them, or no deeper into one than it is now. Looks round the hull again only once it may have left what it last saw.
	 */
	bool IsClearOfSolids(const FRTSFootprint& Footprint);

	/** Server only. The solids the navmesh leaves out round the hull, as last looked at. */
	const FRTSSolidNeighbourhood& GetSolidNeighbourhood() const { return Solids; }

	/** Real seconds every unit's gear took on this machine since the last call; the benchmark reads it once a frame. */
	static double TakeLocomotionSeconds();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;
	virtual void RequestPathMove(const FVector& MoveInput) override;
	/** Vehicles brake from here on their gear rather than stopping dead; path following stops this way at every goal. */
	virtual void StopMovementImmediately() override;

private:
	/** Server only: guidance, the gear and the collision-checked move. */
	void TickLocomotion(float DeltaTime);

	/** Clients only: the realized motion from the replicated velocity and the unit's replicated gear state. */
	void TickPresentation(float DeltaTime);

	/** The guidance command for this update, from the path or the facing asked for. */
	RTSLocomotion::FCommand MakeCommand(float DeltaTime);

	/**
	 * Moves the hull along as much of Step as is clear of obstacles, of unsupported ground and, for a barrel near one, of
	 * reaching it into one: all of it, or the largest of a few shares of it, or none. Returns the share it moved. Tracks stopped
	 * at a slant against a wall they run along turn parallel to it instead, by OutAlignYaw radians.
	 */
	float CommitStep(const RTSLocomotion::FStep& Step, float DeltaTime, float& OutAlignYaw);

	/** The hull's clearance where it stands, found once per update when a move is not clear, and the way out of it. */
	struct FCurrentClearance
	{
		float Clearance = NAN;
		FVector2D Away = FVector2D::ZeroVector;
	};

	/**
	 * Whether the hull may stand at Location facing Yaw (degrees): clear, or no deeper in an obstacle than it is now and not
	 * moved toward it.
	 */
	bool IsPoseClear(const FVector& Location, float Yaw, FCurrentClearance& InOutCurrent);

	/** Whether there is ground under every corner of Footprint, near GroundZ. */
	bool IsGroundUnder(const FRTSFootprint& Footprint, float GroundZ) const;

	/** Adds a driven step to the realized motion. */
	void AddTravel(float Distance, float YawRadians);

	ARTSUnit* GetUnit() const;

	bool bUsesLocomotion = false;
	RTSLocomotion::FLimits Limits;
	RTSLocomotion::FDrive Drive;
	RTSLocomotion::FGuidanceState Guidance;
	float HullLength = 0.f;

	/** The path on the ground, copied once per path. */
	TArray<FVector2D> PathPoints;

	TOptional<float> FacingYaw;
	TOptional<RTSLocomotion::FCommand> CommandOverride;
	float BackOffSeconds = 0.f;
	float AvoidanceSpeedScale = 1.f;
	float AvoidanceCurvatureBias = 0.f;

	FRTSRealizedMotion Realized;
	float BlockedSeconds = 0.f;
	/** The way, forward (1) or back (-1), the gear was last stopped driving straight into something; 0 when it was not. */
	float BlockedDirection = 0.f;

	/** Server only: where the gear left the hull, so anything else that moved it since counts as recovery. */
	TOptional<FVector2D> CommittedLocation;
	float CommittedYaw = 0.f;

	/** Server only. See IsClearOfSolids. */
	FRTSSolidNeighbourhood Solids;
};
