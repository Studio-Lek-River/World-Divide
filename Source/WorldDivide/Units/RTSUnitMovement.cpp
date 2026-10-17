#include "Units/RTSUnitMovement.h"

#include "AIController.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSUnitData.h"
#include "HAL/IConsoleManager.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/RTSHullClearance.h"
#include "Navigation/RTSNavAgents.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSGround.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"
#include "WorldDivide.h"

namespace
{
	TAutoConsoleVariable<bool> CVarLocomotionState(
		TEXT("rts.Net.LocomotionState"), true,
		TEXT("Replicates vehicles' yaw rate, steering and gear status for their tracks and wheels on clients (#138); 0 to measure ")
		TEXT("the traffic without it."));

	/**
	 * As URTSAvoidanceSubsystem keeps hulls out of obstacles: a hull found clear is not checked again until it may have driven
	 * or turned that far, or a building may have gone up; within GearSolidAbove of an obstacle the collision decides, with a
	 * box grown by GearSolidSkin.
	 */
	constexpr float GearCheckExtraReach = 200.f;
	constexpr double GearCheckSeconds = 1.0;
	constexpr float GearSolidAbove = 10.f;
	constexpr float GearSolidSkin = 2.f;

	/**
	 * The solids the navmesh leaves out (#168) are looked for round the hull out to the distance it drives in about
	 * SolidLookSeconds, at least SolidMinMargin; each unit looks again a little earlier or later than that, so units that
	 * started together do not all look on the same frame. A hull may graze one by up to SolidTolerance, so it can drive and
	 * turn along a face it stopped against. Without a step height of its own, a hull steps over this much.
	 */
	constexpr float SolidMinMargin = 200.f;
	constexpr double SolidLookSeconds = 1.0;
	constexpr float SolidTolerance = 0.5f;
	constexpr float SolidStepHeight = 50.f;

	/** A step that swings the hull's corners further than this is checked halfway too, so a pivot cannot swing through a corner. */
	constexpr float RotationCheckDistance = 10.f;

	/**
	 * A blocked step tries these shares of itself, largest first, before the hull stays put: at top speed it ends within
	 * about a centimetre of what stopped it. Once stopped, the gear does not try the same way again until it is told to
	 * drive another way: pressing on would only creep the last of that centimetre a step at a time.
	 */
	constexpr float BlockedShares[] = { 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f };
	constexpr float BlockedTurn = 1.e-3f;

	/** How fast a stuck vehicle backs off, in cm/s, and for how long at least on wheels. */
	constexpr float BackOffSpeed = 200.f;
	constexpr float WheeledBackOffSeconds = 1.5f;

	/** A move along a wall may read this much (cm) deeper or toward it on the navmesh: its reading wavers by that much. */
	constexpr float NavNoise = 0.5f;

	/** How far (cm) a hull moving along or away from what it touches may graze into it: #138's tolerance for new penetration. */
	constexpr float GrazeDepth = 1.f;

	/** A move that turns the hull less than this (degrees) drives it straight. */
	constexpr float StraightTurn = 0.25f;

	/** The ground under a hull's corners is looked for this far above and below the ground under its middle. */
	constexpr float GroundCheckUp = 100.f;
	constexpr float GroundCheckDown = 200.f;

	/** Tracks that cannot drive on turn toward their path point first when it lies more than this off their heading (radians). */
	constexpr float PathAlignAngle = UE_PI / 180.f;

	/** Tracks stopped at a slant this much at most against a wall they run along turn parallel to it (radians). */
	constexpr float WallAlignAngle = UE_PI / 6.f;

	/** Displacement below this (cm, degrees) between updates is the floating point of the transform, not recovery. */
	constexpr float RecoveryMinDistance = 0.1f;
	constexpr float RecoveryMinTurn = 0.01f;

	/** Below these the hull counts as standing still and not turning, in cm/s and degrees per second. */
	constexpr float GearStandingSpeed = 1.f;
	constexpr float StandingYawRate = 0.1f;

	/** A blocked step that still drove further than this (cm) is progress, not a vehicle stuck against something. */
	constexpr float BlockedProgress = 0.1f;

	/** Quantization of FRTSLocomotionNet. */
	constexpr float NetYawRateStep = 0.25f;
	constexpr float NetSteerStep = 0.5f;

	/** Cycles every unit's gear took on this machine since the benchmark last read them. */
	uint64 LocomotionCycles = 0;
}

URTSUnitMovement::URTSUnitMovement()
{
}

void URTSUnitMovement::ApplyUnitData(const URTSUnitData& Data, float InHullLength)
{
	const FRTSLocomotionProfile& Profile = Data.Locomotion;
	bool bVehicle = Profile.IsVehicle() && !Data.IsA<URTSAircraftData>();
	FString Error;
	if (bVehicle && !Profile.Validate(&Error))
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("%s: locomotion profile not valid (%s); the unit walks instead"), *Data.GetName(), *Error);
		bVehicle = false;
	}
	bUsesLocomotion = bVehicle;
	Limits = bVehicle ? RTSLocomotion::FLimits::FromData(Data) : RTSLocomotion::FLimits();
	HullLength = InHullLength;
	Drive = RTSLocomotion::FDrive();
	Guidance = RTSLocomotion::FGuidanceState();
	BlockedSeconds = 0.f;
	CommittedLocation.Reset();
	Solids.Reset();
}

float URTSUnitMovement::GetStoppingDistance() const
{
	if (!bUsesLocomotion)
	{
		return RTSLocomotion::StoppingDistance(Velocity.Size2D(), FMath::Max(Deceleration, 1.f));
	}
	return RTSLocomotion::StoppingDistance(Drive.GetSpeed(Limits), Limits.Braking);
}

void URTSUnitMovement::SetPath(const TArray<FNavPathPoint>& Points)
{
	PathPoints.Reset(Points.Num());
	for (const FNavPathPoint& Point : Points)
	{
		PathPoints.Add(FVector2D(Point.Location));
	}
	Guidance = RTSLocomotion::FGuidanceState();
	BlockedSeconds = 0.f;
	BlockedDirection = 0.f;
}

void URTSUnitMovement::SetAvoidance(float SpeedScale, float CurvatureBias)
{
	AvoidanceSpeedScale = FMath::Clamp(SpeedScale, 0.f, 1.f);
	AvoidanceCurvatureBias = CurvatureBias;
}

void URTSUnitMovement::BackOff(float Seconds)
{
	// Wheels back off longer, swinging their nose toward their way: they cannot turn on the spot afterwards.
	BackOffSeconds = Limits.ReverseSpeed > 0.f ? (Limits.IsWheeled() ? FMath::Max(Seconds, WheeledBackOffSeconds) : Seconds) : 0.f;
	bFaceAfterBackOff = true;
	BlockedDirection = 0.f;
}

void URTSUnitMovement::HaltImmediately()
{
	BackOffSeconds = 0.f;
	Drive = RTSLocomotion::FDrive();
	BlockedDirection = 0.f;
	Guidance = RTSLocomotion::FGuidanceState();
	BlockedSeconds = 0.f;
	Realized.ForwardSpeed = 0.f;
	Realized.YawRate = 0.f;
	Realized.Status = ERTSLocomotionStatus::Idle;
	Velocity = FVector::ZeroVector;
	UpdateComponentVelocity();
}

double URTSUnitMovement::TakeLocomotionSeconds()
{
	const double Seconds = FPlatformTime::ToSeconds64(LocomotionCycles);
	LocomotionCycles = 0;
	return Seconds;
}

void URTSUnitMovement::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed)
{
	// A vehicle's gear reads the path itself; path following never sets its speed.
	if (!bUsesLocomotion)
	{
		Super::RequestDirectMove(MoveVelocity, bForceMaxSpeed);
	}
}

void URTSUnitMovement::RequestPathMove(const FVector& MoveInput)
{
	if (!bUsesLocomotion)
	{
		Super::RequestPathMove(MoveInput);
	}
}

void URTSUnitMovement::StopMovementImmediately()
{
	// The gear brakes from its own speed once it has no path left; HaltImmediately stops it dead.
	if (!bUsesLocomotion)
	{
		Super::StopMovementImmediately();
	}
}

void URTSUnitMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!bUsesLocomotion)
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
		return;
	}
	// Past the floating pawn's own integration of its velocity: the gear drives the hull.
	UPawnMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (ShouldSkipUpdate(DeltaTime) || !PawnOwner || !UpdatedComponent || DeltaTime <= 0.f)
	{
		return;
	}
	if (PawnOwner->HasAuthority())
	{
		TickLocomotion(DeltaTime);
	}
	else
	{
		TickPresentation(DeltaTime);
	}
}

ARTSUnit* URTSUnitMovement::GetUnit() const
{
	return Cast<ARTSUnit>(PawnOwner);
}

RTSLocomotion::FCommand URTSUnitMovement::MakeCommand(float DeltaTime)
{
	if (CommandOverride)
	{
		return *CommandOverride;
	}
	if (BackOffSeconds > 0.f)
	{
		BackOffSeconds -= DeltaTime;
		RTSLocomotion::FCommand Back;
		Back.Speed = -FMath::Min(Limits.ReverseSpeed, BackOffSpeed);
		if (Limits.IsWheeled() && PathPoints.IsValidIndex(Guidance.Target))
		{
			// Backing with the wheels turned away from the way swings the nose toward it.
			const FTransform& Pose = UpdatedComponent->GetComponentTransform();
			const float Side = FVector2D::DotProduct(PathPoints[Guidance.Target] - FVector2D(Pose.GetLocation()), FVector2D(Pose.GetUnitAxis(EAxis::Y)));
			Back.Curvature = (Side >= 0.f ? -1.f : 1.f) / Limits.MinTurnRadius;
		}
		return Back;
	}
	const AAIController* AI = PawnOwner ? Cast<AAIController>(PawnOwner->GetController()) : nullptr;
	const UPathFollowingComponent* PathFollowing = AI ? AI->GetPathFollowingComponent() : nullptr;
	const bool bMoving = PathFollowing && PathFollowing->GetStatus() == EPathFollowingStatus::Moving && PathPoints.Num() >= 2;
	if (!bMoving && (Guidance.Target != 1 || Guidance.bPivoting || Guidance.bReversing || Guidance.bManoeuvring || Guidance.bFailed))
	{
		Guidance = RTSLocomotion::FGuidanceState();
	}
	// Backed off from what stopped it, a track faces its new path on the spot before it drives: from a standstill both
	// tracks speed up alike, so it would set off straight on its old heading, back into the same corner (#173).
	if (bMoving && bFaceAfterBackOff)
	{
		Guidance.bPivoting = true;
	}
	bFaceAfterBackOff = false;

	RTSLocomotion::FGuidanceInput Input;
	const FTransform& Pose = UpdatedComponent->GetComponentTransform();
	Input.Location = FVector2D(Pose.GetLocation());
	Input.Yaw = FMath::DegreesToRadians(Pose.Rotator().Yaw);
	Input.Speed = Drive.GetSpeed(Limits);
	Input.Dt = DeltaTime;
	if (bMoving)
	{
		Input.Path = PathPoints;
	}
	if (FacingYaw)
	{
		Input.FacingYaw = FMath::DegreesToRadians(*FacingYaw);
	}
	Input.HullLength = HullLength;
	Input.SpeedScale = AvoidanceSpeedScale;
	Input.CurvatureBias = AvoidanceCurvatureBias;
	// Avoidance asks again every update it still needs to.
	AvoidanceSpeedScale = 1.f;
	AvoidanceCurvatureBias = 0.f;
	TRACE_CPUPROFILER_EVENT_SCOPE(RTSLocomotion::Guide);
	return RTSLocomotion::Guide(Limits, Input, Guidance);
}

void URTSUnitMovement::TickLocomotion(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSUnitMovement::TickLocomotion);
	const uint64 StartCycles = FPlatformTime::Cycles64();

	// Whatever moved the hull since the gear last did, the gear did not drive.
	const FTransform& Pose = UpdatedComponent->GetComponentTransform();
	const FVector2D Location(Pose.GetLocation());
	const float Yaw = Pose.Rotator().Yaw;
	if (CommittedLocation)
	{
		const FVector2D Moved = Location - *CommittedLocation;
		if (Moved.SizeSquared() > FMath::Square(RecoveryMinDistance) || FMath::Abs(FMath::FindDeltaAngleDegrees(CommittedYaw, Yaw)) > RecoveryMinTurn)
		{
			Realized.RecoveryDisplacement += Moved;
			++Realized.RecoveryCount;
			// Moved off whatever stopped it: the gear may try that way again.
			BlockedDirection = 0.f;
		}
	}

	const RTSLocomotion::FCommand Command = MakeCommand(DeltaTime);
	Realized.CommandedSpeed = Command.bPivot ? 0.f : Command.Speed;
	Realized.CommandedYawRate = FMath::RadiansToDegrees(Command.bPivot ? Command.PivotYawRate : Command.Speed * Command.Curvature);

	const float SpeedBefore = Drive.GetSpeed(Limits);
	RTSLocomotion::FStep Step;
	RTSLocomotion::Step(Limits, Command, Drive, DeltaTime, Step);
	const bool bStill = FMath::IsNearlyZero(Step.Distance) && FMath::IsNearlyZero(Step.Yaw);
	const bool bPressing = BlockedDirection != 0.f && FMath::Sign(Step.Distance) == BlockedDirection && FMath::Abs(Step.Yaw) < BlockedTurn;
	float Turned = 0.f;
	const float Share = bStill ? 1.f : bPressing ? 0.f : CommitStep(Step, DeltaTime, Turned);
	// A turn along a wall (CommitStep) is driven too: the tracks pivot.
	const bool bTurnedInPlace = Share == 0.f && Turned != 0.f;
	BlockedDirection = Share < 1.f && !bStill && !bTurnedInPlace ? FMath::Sign(Step.Distance) : 0.f;
	const float Driven = Step.Distance * Share;
	if (Share < 1.f)
	{
		// Up against something: the gear stops there, it does not keep turning in place.
		Drive.Left = 0.f;
		Drive.Right = 0.f;
		Drive.Speed = 0.f;
	}
	AddTravel(Driven, Turned);

	Realized.ForwardSpeed = Driven / DeltaTime;
	Realized.YawRate = FMath::RadiansToDegrees(Turned / DeltaTime);
	Realized.SteerAngle = Limits.IsWheeled() ? FMath::RadiansToDegrees(Drive.Steer) : 0.f;
	const bool bStanding = FMath::Abs(Realized.ForwardSpeed) < GearStandingSpeed;
	if (Share < 1.f)
	{
		Realized.Status = ERTSLocomotionStatus::Blocked;
	}
	else if (bStanding && FMath::Abs(Realized.YawRate) >= StandingYawRate)
	{
		Realized.Status = ERTSLocomotionStatus::Pivoting;
	}
	else if (bStanding)
	{
		Realized.Status = ERTSLocomotionStatus::Idle;
	}
	else if (FMath::Abs(Drive.GetSpeed(Limits)) < FMath::Abs(SpeedBefore) - UE_KINDA_SMALL_NUMBER)
	{
		Realized.Status = ERTSLocomotionStatus::Braking;
	}
	else
	{
		Realized.Status = Realized.ForwardSpeed < 0.f ? ERTSLocomotionStatus::Reversing : ERTSLocomotionStatus::Driving;
	}
	// A turn on the spot against something neither counts as stuck nor as getting anywhere.
	if (!bTurnedInPlace)
	{
		BlockedSeconds = Share < 1.f && FMath::Abs(Driven) < BlockedProgress ? BlockedSeconds + DeltaTime : 0.f;
	}

	// Velocity is what the hull did, for whoever reads it: avoidance, the barrel's look-ahead, replication.
	Velocity = UpdatedComponent->GetForwardVector().GetSafeNormal2D() * Realized.ForwardSpeed;
	UpdateComponentVelocity();

	const FTransform& After = UpdatedComponent->GetComponentTransform();
	CommittedLocation = FVector2D(After.GetLocation());
	CommittedYaw = After.Rotator().Yaw;

	if (ARTSUnit* Unit = GetUnit(); Unit && CVarLocomotionState.GetValueOnGameThread())
	{
		FRTSLocomotionNet Net;
		Net.YawRate = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Realized.YawRate / NetYawRateStep), MIN_int16, MAX_int16));
		Net.SteerAngle = static_cast<int8>(FMath::Clamp(FMath::RoundToInt(Realized.SteerAngle / NetSteerStep), MIN_int8, MAX_int8));
		Net.Status = Realized.Status;
		Unit->SetLocomotionNet(Net);
	}
	LocomotionCycles += FPlatformTime::Cycles64() - StartCycles;
}

void URTSUnitMovement::TickPresentation(float DeltaTime)
{
	const ARTSUnit* Unit = GetUnit();
	if (!Unit)
	{
		return;
	}
	// The server's velocity, replicated with the unit's movement, is the hull's forward speed; corrections of the pose are not.
	const FRTSLocomotionNet& Net = Unit->GetLocomotionNet();
	Realized.ForwardSpeed = FVector::DotProduct(Velocity, UpdatedComponent->GetForwardVector().GetSafeNormal2D());
	Realized.YawRate = Net.YawRate * NetYawRateStep;
	Realized.SteerAngle = Net.SteerAngle * NetSteerStep;
	Realized.Status = Net.Status;
	AddTravel(Realized.ForwardSpeed * DeltaTime, FMath::DegreesToRadians(Realized.YawRate) * DeltaTime);
}

void URTSUnitMovement::AddTravel(float Distance, float YawRadians)
{
	const float Half = 0.5f * Limits.GetSideSpacing();
	Realized.DrivenDistance += Distance;
	Realized.TurnedRadians += YawRadians;
	Realized.LeftTravel += Distance + Half * YawRadians;
	Realized.RightTravel += Distance - Half * YawRadians;
}

bool URTSUnitMovement::IsPoseClear(const FVector& Location, float Yaw, FCurrentClearance& InOutCurrent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSUnitMovement::IsPoseClear);
	ARTSUnit* Unit = GetUnit();
	UWorld* World = GetWorld();
	if (!Unit || !World)
	{
		return true;
	}
	const FRTSUnitShape& Shape = Unit->GetShape();
	const FRTSFootprint Footprint = RTSUnitShape::MakeFootprint(Shape, FTransform(FRotator(0.f, Yaw, 0.f), Location));
	// What the navmesh leaves out it cannot read either: the collision alone decides, wherever the navmesh says the hull is.
	if (!IsClearOfSolids(Footprint))
	{
		return false;
	}
	FRTSHullClearanceCache& Cache = Unit->GetHullClearanceCache();
	const double Now = World->GetTimeSeconds();
	if (Cache.IsClear(Footprint, Now))
	{
		return true;
	}
	const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*World, GetNavAgentPropertiesRef());
	if (!NavMesh)
	{
		return true;
	}
	const float Clearance = RTSHullClearance::GetClearance(*NavMesh, Footprint, nullptr, GearCheckExtraReach);
	auto Remember = [&](float Slack)
	{
		Cache.Center = FVector2D(Footprint.Center);
		Cache.Axis = FVector2D(Footprint.Axis);
		Cache.Slack = Slack;
		Cache.ExpireTime = Now + GearCheckSeconds;
	};
	if (Clearance != FLT_MAX)
	{
		if (Clearance >= GearSolidAbove)
		{
			Remember(Clearance);
			return true;
		}
		// Closer than that the navmesh may read an obstacle nearer than it is: by a cell beside a mesh's face (#163), and by
		// up to (sqrt 2 - 1) of its erosion beside a box obstacle's corner. There the collision decides, and the ground under
		// the hull's corners where the navmesh ends without anything solid, such as at the edge of the map.
		const float Band = RTSHullClearance::GetNavUncertainty(*NavMesh) + (UE_SQRT_2 - 1.f) * RTSHullClearance::GetErosion(*NavMesh);
		if (Clearance >= -Band)
		{
			const float GroundZ = Location.Z - Unit->GetGroundOffset();
			const float Height = Shape.Hull.GetSize().Z;
			const bool bSkinClear = RTSHullClearance::IsSolidClear(*World, Footprint, GroundZ, Height, GearSolidSkin, Unit->GetGroundNormal());
			// Within the skin the hull may still drive on, up to touching, but is checked again every update: along a wall
			// the navmesh's reading wavers by a few millimetres, which must not stop a hull that touches nothing.
			if ((bSkinClear || RTSHullClearance::IsSolidClear(*World, Footprint, GroundZ, Height, 0.f, Unit->GetGroundNormal()))
				&& (Clearance >= 0.f || IsGroundUnder(Footprint, GroundZ)))
			{
				if (bSkinClear && Clearance >= 0.f)
				{
					Remember(GearSolidSkin);
				}
				return true;
			}
			// A hull grazing something may still move along or away from it, or turn, as long as it reaches no further in than
			// GrazeDepth; avoidance backs it out of the graze (URTSAvoidanceSubsystem).
			if (RTSHullClearance::IsSolidClear(*World, Footprint, GroundZ, Height, -GrazeDepth, Unit->GetGroundNormal()) && IsGroundUnder(Footprint, GroundZ))
			{
				if (FMath::IsNaN(InOutCurrent.Clearance))
				{
					InOutCurrent.Clearance = RTSHullClearance::GetClearance(*NavMesh, Unit->GetFootprint(), &InOutCurrent.Away, GearCheckExtraReach);
				}
				if (FVector2D::DotProduct(FVector2D(Location - UpdatedComponent->GetComponentLocation()), InOutCurrent.Away) >= -NavNoise)
				{
					return true;
				}
			}
		}
	}
	// Not clear, or off the navmesh: only a move that goes no deeper in than the hull already is and not toward what it is
	// in, such as along a wall it touches or out of a tight spot.
	if (FMath::IsNaN(InOutCurrent.Clearance))
	{
		InOutCurrent.Clearance = RTSHullClearance::GetClearance(*NavMesh, Unit->GetFootprint(), &InOutCurrent.Away, GearCheckExtraReach);
	}
	if (Clearance == FLT_MAX || InOutCurrent.Clearance == FLT_MAX)
	{
		return Clearance == FLT_MAX && InOutCurrent.Clearance == FLT_MAX;
	}
	const FVector2D Moved = FVector2D(Location - UpdatedComponent->GetComponentLocation());
	const bool bNotToward = FVector2D::DotProduct(Moved, InOutCurrent.Away) >= -NavNoise;
	// Driving straight, along or away from what the hull touches, cannot press it further in, whatever the navmesh's reading
	// does as the hull's closest point slides along; a turn swings its far end, and may not go deeper.
	const float Turned = FMath::Abs(FMath::FindDeltaAngleDegrees(UpdatedComponent->GetComponentRotation().Yaw, Yaw));
	const float Band = RTSHullClearance::GetNavUncertainty(*NavMesh) + (UE_SQRT_2 - 1.f) * RTSHullClearance::GetErosion(*NavMesh);
	return bNotToward && (Turned < StraightTurn ? Clearance >= FMath::Min(-Band, InOutCurrent.Clearance) : Clearance >= InOutCurrent.Clearance - NavNoise);
}

bool URTSUnitMovement::IsClearOfSolids(const FRTSFootprint& Footprint)
{
	ARTSUnit* Unit = GetUnit();
	UWorld* World = GetWorld();
	if (!Unit || !World)
	{
		return true;
	}
	const double Now = World->GetTimeSeconds();
	const float GroundOffset = Unit->GetGroundOffset();
	const float Height = Unit->GetShape().Hull.GetSize().Z;
	if (Solids.IsCurrent(Footprint, Now))
	{
		FRTSSolidStats::AddReuse();
	}
	else
	{
		const float Margin = FMath::Max(SolidMinMargin, FMath::Abs(Drive.GetSpeed(Limits)) * static_cast<float>(SolidLookSeconds));
		const double Lifetime = SolidLookSeconds * (0.75 + 0.5 * (Unit->GetUniqueID() % 16) / 16.0);
		Solids.Update(*World, Footprint, Footprint.Center.Z - GroundOffset, Height, Margin, Now, Lifetime);
	}
	if (Solids.Solids.IsEmpty())
	{
		return true;
	}

	const float StepHeight = GetNavAgentPropertiesRef().AgentStepHeight > 0.f ? GetNavAgentPropertiesRef().AgentStepHeight : SolidStepHeight;
	const FVector Normal = Unit->GetGroundNormal();
	auto BoxOf = [&](const FRTSFootprint& Pose)
	{
		return RTSHullClearance::MakeHullBox(Pose, Pose.Center.Z - GroundOffset, Normal, StepHeight, Height, 0.f);
	};
	const FRTSFootprint Here = Unit->GetFootprint();
	const RTSHullClearance::FHullBox Current = BoxOf(Here);
	// The box at the end of a step covers all the step's leading face swept as long as the step is shorter than the box is
	// narrow; a longer one, which would jump a thin wall, is looked at on the way too.
	const float Moved = FVector::Dist2D(Here.Center, Footprint.Center);
	const int32 Pieces = FMath::Max(1, FMath::CeilToInt32(Moved / FMath::Max(Footprint.Radius, 1.f)));
	for (int32 Piece = 1; Piece < Pieces; ++Piece)
	{
		FRTSFootprint Between = Footprint;
		Between.Center = FMath::Lerp(Here.Center, Footprint.Center, static_cast<float>(Piece) / Pieces);
		if (!Solids.IsClear(BoxOf(Between), Current, SolidTolerance))
		{
			return false;
		}
	}
	return Solids.IsClear(BoxOf(Footprint), Current, SolidTolerance);
}

bool URTSUnitMovement::IsGroundUnder(const FRTSFootprint& Footprint, float GroundZ) const
{
	const FVector2D Center(Footprint.Center);
	const FVector2D Along = FVector2D(Footprint.Axis) * (Footprint.HalfSegment + Footprint.Radius);
	const FVector2D Across = FVector2D(-Footprint.Axis.Y, Footprint.Axis.X) * Footprint.Radius;
	for (const FVector2D& Corner : { Center + Along + Across, Center + Along - Across, Center - Along + Across, Center - Along - Across })
	{
		float Z = 0.f;
		if (!RTSGround::Trace(*GetWorld(), FVector(Corner, GroundZ), GroundCheckUp, GroundCheckDown, Z))
		{
			return false;
		}
	}
	return true;
}

float URTSUnitMovement::CommitStep(const RTSLocomotion::FStep& Step, float DeltaTime, float& OutYaw)
{
	OutYaw = 0.f;
	ARTSUnit* Unit = GetUnit();
	const FTransform Pose = UpdatedComponent->GetComponentTransform();
	const FVector Location = Pose.GetLocation();
	const float Yaw = Pose.Rotator().Yaw;
	const FVector Forward = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector Right(-Forward.Y, Forward.X, 0.f);
	const float HalfDiagonal = Unit ? FVector2D(Unit->GetShape().Hull.GetExtent()).Size() : 0.f;
	const bool bWatchBarrel = Unit && Unit->IsBarrelNearObstacle();
	const bool bBarrelClear = bWatchBarrel && Unit->IsBarrelClearAt(Pose);
	FCurrentClearance Current;

	// Moves the hull along Share of an arc of Distance that turns StepYaw radians, when that is clear.
	auto TryArc = [&](float Distance, float StepYaw)
	{
		const FVector2D Offset = RTSLocomotion::ArcChord(Distance, StepYaw);
		const FVector Delta = Forward * Offset.X + Right * Offset.Y;
		const float NewYaw = Yaw + FMath::RadiansToDegrees(StepYaw);
		if (FMath::Abs(StepYaw) * HalfDiagonal > RotationCheckDistance)
		{
			const FVector2D HalfOffset = RTSLocomotion::ArcChord(0.5f * Distance, 0.5f * StepYaw);
			if (!IsPoseClear(Location + Forward * HalfOffset.X + Right * HalfOffset.Y, Yaw + FMath::RadiansToDegrees(0.5f * StepYaw), Current))
			{
				return false;
			}
		}
		if (!IsPoseClear(Location + Delta, NewYaw, Current))
		{
			return false;
		}
		// A barrel close to an obstacle (#162) turns or rises out of the way first; the hull does not drive or swing it in.
		// A barrel already in one (a building went up round it) may still drive straight out, but not turn.
		const FRotator NewRotation(0.f, NewYaw, 0.f);
		if (bWatchBarrel && (bBarrelClear || StepYaw != 0.f) && !Unit->IsBarrelClearAt(FTransform(NewRotation, Location + Delta)))
		{
			return false;
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(URTSUnitMovement::MoveHull);
			MoveUpdatedComponent(Delta, NewRotation.Quaternion(), /*bSweep*/ false);
		}
		return true;
	};

	if (TryArc(Step.Distance, Step.Yaw))
	{
		OutYaw = Step.Yaw;
		return 1.f;
	}

	// Tracks that cannot drive on turn on the spot instead, as far as one update's turn allows, and drive on the next; they
	// never slide sideways. Wheels cannot. First toward the path point they head for: arcing onto the path near something,
	// the hull's corners sweep wider than its straight way there.
	const bool bTurnsInPlace = Limits.IsTracked() && Limits.bAllowPivot && Unit && FMath::Abs(Step.Distance) > UE_KINDA_SMALL_NUMBER;
	const float MaxTurn = Limits.MaxYawRate * DeltaTime;
	if (bTurnsInPlace && !CommandOverride && PathPoints.IsValidIndex(Guidance.Target))
	{
		const FVector2D ToTarget = PathPoints[Guidance.Target] - FVector2D(Location);
		const FVector2D Heading = FVector2D(Forward) * FMath::Sign(Step.Distance);
		if (ToTarget.SizeSquared() > 1.f)
		{
			const FVector2D Want = ToTarget.GetSafeNormal();
			const float Turn = FMath::Atan2(Heading.X * Want.Y - Heading.Y * Want.X, FVector2D::DotProduct(Heading, Want));
			const float AlignYaw = FMath::Clamp(Turn, -MaxTurn, MaxTurn);
			if (FMath::Abs(Turn) > PathAlignAngle && TryArc(0.f, AlignYaw))
			{
				OutYaw = AlignYaw;
				return 0.f;
			}
		}
	}
	// Then parallel to a wall they drive into at a slant.
	if (bTurnsInPlace)
	{
		const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*GetWorld(), GetNavAgentPropertiesRef());
		FVector2D Away = FVector2D::ZeroVector;
		if (NavMesh)
		{
			RTSHullClearance::GetClearance(*NavMesh, Unit->GetFootprint(), &Away, GearCheckExtraReach);
		}
		const FVector2D Heading = FVector2D(Forward) * FMath::Sign(Step.Distance);
		if (!Away.IsNearlyZero() && FVector2D::DotProduct(Heading, Away) < 0.f)
		{
			// Of the two ways along the wall, the one closer to where the hull heads.
			FVector2D Along(-Away.Y, Away.X);
			Along *= FVector2D::DotProduct(Along, Heading) >= 0.f ? 1.f : -1.f;
			const float Turn = FMath::Atan2(Heading.X * Along.Y - Heading.Y * Along.X, FVector2D::DotProduct(Heading, Along));
			const float AlignYaw = FMath::Clamp(Turn, -MaxTurn, MaxTurn);
			if (FMath::Abs(Turn) < WallAlignAngle && FMath::Abs(AlignYaw) > UE_KINDA_SMALL_NUMBER && TryArc(0.f, AlignYaw))
			{
				OutYaw = AlignYaw;
				return 0.f;
			}
		}
	}

	// Then on along the heading without the turn: driving along a wall, the turn back onto the way swings the hull's far end
	// into it, where driving on straight takes the hull past it (#173). Guidance steers again next update.
	if (Step.Yaw != 0.f && FMath::Abs(Step.Distance) > UE_KINDA_SMALL_NUMBER && TryArc(Step.Distance, 0.f))
	{
		return 1.f;
	}

	for (const float Share : BlockedShares)
	{
		if (TryArc(Step.Distance * Share, Step.Yaw * Share))
		{
			OutYaw = Step.Yaw * Share;
			return Share;
		}
	}
	return 0.f;
}
