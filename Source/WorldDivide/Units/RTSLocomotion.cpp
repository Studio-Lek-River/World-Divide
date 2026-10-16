#include "Units/RTSLocomotion.h"

namespace
{
	/** Below this the arc is treated as straight, to keep its chord exact. */
	constexpr float SmallAngle = 1.e-4f;

	/** Guidance passes a path point once within this many cm of it; a sharp corner a track stops and pivots at, within the second. */
	constexpr float WaypointReach = 50.f;
	constexpr float CornerReach = 30.f;

	/** Tracks stop and pivot at a path corner that turns more than this; the hull clears every heading there (AdjustPath). */
	constexpr float SharpCornerAngle = UE_PI / 6.f;

	/** A corner gentler than this needs no slowing. */
	constexpr float GentleCornerAngle = UE_PI / 36.f;

	/** How many corners ahead guidance looks at to brake for them in time. */
	constexpr int32 CornerLookAhead = 4;

	/** Tracks start a pivot once slower than this, in cm/s; a pivot ends within this many radians of its heading (at most). */
	constexpr float PivotStartSpeed = 20.f;
	constexpr float PivotExitAngle = UE_PI / 22.5f;

	/** A pivot to a facing asked for while standing ends within this many radians of it. */
	constexpr float FacingTolerance = UE_PI / 180.f;

	/** Pivots brake their turn at this share of what the tracks' braking allows, so they stop on their heading. */
	constexpr float PivotBrakeShare = 0.5f;

	/** The point pursued lies this many seconds of the current speed plus the base ahead, between the minimum and maximum, in cm. */
	constexpr float PursuitSeconds = 0.5f;
	constexpr float PursuitBase = 100.f;
	constexpr float PursuitMin = 150.f;
	constexpr float PursuitMax = 1500.f;
	/** Wheels look at least this share of their turning radius ahead, so they follow its circle round a corner. */
	constexpr float WheeledLookAheadRadii = 0.75f;

	/** A goal at most this many hull lengths behind, along the path, is reversed to when the profile reverses. */
	constexpr float ReverseHullLengths = 1.5f;

	/** Wheeled: the goal counts as inside a turning circle within this share of its radius, and outside again beyond the second. */
	constexpr float CircleInside = 0.98f;
	constexpr float CircleOutside = 1.05f;
	/** Wheeled: how fast it backs or swings out of a circle, in cm/s, and how often before it gives up. */
	constexpr float ManoeuvreSpeed = 300.f;
	constexpr int32 MaxManoeuvres = 3;

	float WrapAngle(float Radians)
	{
		return FMath::UnwindRadians(Radians);
	}

	/**
	 * Moves Speed toward Target at Rate for up to Time; returns the time it took, at most Time, and adds the distance covered
	 * to Distance.
	 */
	float RampToward(float& Speed, float Target, float Rate, float Time, float& Distance)
	{
		const float Gap = Target - Speed;
		const float Needed = FMath::Abs(Gap) / Rate;
		if (Needed <= Time)
		{
			Distance += 0.5f * (Speed + Target) * Needed;
			Speed = Target;
			return Needed;
		}
		const float End = Speed + FMath::Sign(Gap) * Rate * Time;
		Distance += 0.5f * (Speed + End) * Time;
		Speed = End;
		return Time;
	}

	/** The yaw rate a pivot runs at to turn Error radians and stop there. */
	float GetPivotRate(const RTSLocomotion::FLimits& Limits, float Error)
	{
		const float YawBraking = PivotBrakeShare * 2.f * Limits.Braking / Limits.TrackGauge;
		return FMath::Sign(Error) * FMath::Min(Limits.MaxYawRate, FMath::Sqrt(2.f * YawBraking * FMath::Abs(Error)));
	}

	/** How far the path turns at its point Index, in radians. */
	float GetCornerAngle(TConstArrayView<FVector2D> Path, int32 Index)
	{
		const FVector2D In = (Path[Index] - Path[Index - 1]).GetSafeNormal();
		const FVector2D Out = (Path[Index + 1] - Path[Index]).GetSafeNormal();
		if (In.IsNearlyZero() || Out.IsNearlyZero())
		{
			return 0.f;
		}
		return FMath::Acos(FMath::Clamp(FVector2D::DotProduct(In, Out), -1.f, 1.f));
	}
}

namespace RTSLocomotion
{
	FLimits FLimits::FromData(const URTSUnitData& Data)
	{
		const FRTSLocomotionProfile& Profile = Data.Locomotion;
		FLimits Limits;
		Limits.Type = Profile.Type;
		Limits.ForwardSpeed = Data.MoveSpeed;
		Limits.ReverseSpeed = Profile.ReverseSpeed;
		Limits.Acceleration = Profile.Acceleration;
		Limits.Braking = Profile.Braking;
		Limits.MaxYawRate = FMath::DegreesToRadians(Data.TurnRate);
		Limits.MaxLateralAcceleration = Profile.MaxLateralAcceleration;
		Limits.TrackGauge = Profile.TrackGauge;
		Limits.bAllowPivot = Profile.bAllowPivot;
		Limits.PivotAngle = FMath::DegreesToRadians(Profile.PivotAngle);
		Limits.Wheelbase = Profile.Wheelbase;
		Limits.AxleTrack = Profile.AxleTrack;
		Limits.MinTurnRadius = Profile.GetMinTurnRadius();
		// The tighter of the steering lock and the turning circle.
		Limits.MaxSteer = FMath::Min(FMath::DegreesToRadians(Profile.MaxSteerAngle), FMath::Atan(Profile.Wheelbase / Limits.MinTurnRadius));
		Limits.SteerRate = FMath::DegreesToRadians(Profile.SteerRate);
		return Limits;
	}

	float FLimits::GetTrackReverseLimit() const
	{
		return FMath::Max(ReverseSpeed, bAllowPivot ? MaxYawRate * TrackGauge * 0.5f : 0.f);
	}

	float FDrive::GetYawRate(const FLimits& Limits) const
	{
		return Limits.IsTracked() ? (Left - Right) / Limits.TrackGauge : Speed * FMath::Tan(Steer) / Limits.Wheelbase;
	}

	FCommand FCommand::FromTracks(const FLimits& Limits, float Left, float Right)
	{
		FCommand Command;
		const float Speed = 0.5f * (Left + Right);
		const float YawRate = (Left - Right) / Limits.TrackGauge;
		if (FMath::Abs(Speed) < UE_KINDA_SMALL_NUMBER)
		{
			Command.bPivot = YawRate != 0.f;
			Command.PivotYawRate = YawRate;
			return Command;
		}
		Command.Speed = Speed;
		Command.Curvature = YawRate / Speed;
		return Command;
	}

	float Ramp(float& Speed, float Target, float Accel, float Brake, float Dt)
	{
		float Distance = 0.f;
		float Left = Dt;
		// Changing direction brakes to a standstill first.
		if (Speed * Target < 0.f)
		{
			Left -= RampToward(Speed, 0.f, Brake, Left, Distance);
			if (Left <= 0.f)
			{
				return Distance;
			}
		}
		const bool bFaster = FMath::Abs(Target) > FMath::Abs(Speed);
		Left -= RampToward(Speed, Target, bFaster ? Accel : Brake, Left, Distance);
		return Distance + Speed * FMath::Max(Left, 0.f);
	}

	FVector2D ArcChord(float Distance, float Yaw)
	{
		if (FMath::Abs(Yaw) < SmallAngle)
		{
			return FVector2D(Distance * (1.f - Yaw * Yaw / 6.f), Distance * Yaw * 0.5f);
		}
		return FVector2D(Distance * FMath::Sin(Yaw) / Yaw, Distance * (1.f - FMath::Cos(Yaw)) / Yaw);
	}

	float StoppingDistance(float Speed, float Braking)
	{
		return Speed * Speed / (2.f * Braking);
	}

	void Step(const FLimits& Limits, const FCommand& Command, FDrive& Drive, float Dt, FStep& Out)
	{
		Out = FStep();
		if (Dt <= 0.f)
		{
			return;
		}
		if (Limits.IsTracked())
		{
			const float Half = 0.5f * Limits.TrackGauge;
			const float Speed = Command.bPivot ? 0.f : FMath::Clamp(Command.Speed, -Limits.ReverseSpeed, Limits.ForwardSpeed);
			const float YawRate = FMath::Clamp(Command.bPivot ? Command.PivotYawRate : Speed * Command.Curvature,
				-Limits.MaxYawRate, Limits.MaxYawRate);
			float Left = Speed + YawRate * Half;
			float Right = Speed - YawRate * Half;
			// Neither track runs faster than the hull may: both slow together, so the turn keeps its curvature.
			const float ForwardLimit = Limits.ForwardSpeed;
			const float ReverseLimit = Limits.GetTrackReverseLimit();
			float Scale = 1.f;
			for (const float Track : { Left, Right })
			{
				if (Track > ForwardLimit)
				{
					Scale = FMath::Min(Scale, ForwardLimit / Track);
				}
				else if (Track < -ReverseLimit)
				{
					Scale = FMath::Min(Scale, ReverseLimit / -Track);
				}
			}
			Left *= Scale;
			Right *= Scale;

			const float LeftDistance = Ramp(Drive.Left, Left, Limits.Acceleration, Limits.Braking, Dt);
			const float RightDistance = Ramp(Drive.Right, Right, Limits.Acceleration, Limits.Braking, Dt);
			Out.Distance = 0.5f * (LeftDistance + RightDistance);
			Out.Yaw = (LeftDistance - RightDistance) / Limits.TrackGauge;

			// Tracks that ramp at different rates may turn faster on the way than either end: never faster than the limit.
			const float MaxYaw = Limits.MaxYawRate * Dt;
			if (FMath::Abs(Out.Yaw) > MaxYaw)
			{
				Out.Yaw = FMath::Sign(Out.Yaw) * MaxYaw;
			}
			const float EndYawRate = (Drive.Left - Drive.Right) / Limits.TrackGauge;
			if (FMath::Abs(EndYawRate) > Limits.MaxYawRate)
			{
				const float Mean = 0.5f * (Drive.Left + Drive.Right);
				Drive.Left = Mean + FMath::Sign(EndYawRate) * Limits.MaxYawRate * Half;
				Drive.Right = Mean - FMath::Sign(EndYawRate) * Limits.MaxYawRate * Half;
			}
		}
		else
		{
			const float Speed = FMath::Clamp(Command.Speed, -Limits.ReverseSpeed, Limits.ForwardSpeed);
			const float SteerGoal = FMath::Clamp(FMath::Atan(Command.Curvature * Limits.Wheelbase), -Limits.MaxSteer, Limits.MaxSteer);
			const float SteerBefore = Drive.Steer;
			Drive.Steer = SteerBefore + FMath::Clamp(SteerGoal - SteerBefore, -Limits.SteerRate * Dt, Limits.SteerRate * Dt);
			const float SpeedBefore = Drive.Speed;
			Out.Distance = Ramp(Drive.Speed, Speed, Limits.Acceleration, Limits.Braking, Dt);

			// No faster a turn than the yaw limit: at speed the steering eases off, which only widens the turn.
			const float Fastest = FMath::Max(FMath::Abs(SpeedBefore), FMath::Abs(Drive.Speed));
			float Steer = 0.5f * (SteerBefore + Drive.Steer);
			if (Fastest > UE_KINDA_SMALL_NUMBER)
			{
				const float YawLimited = FMath::Atan(Limits.MaxYawRate * Limits.Wheelbase / Fastest);
				Drive.Steer = FMath::Clamp(Drive.Steer, -YawLimited, YawLimited);
				Steer = FMath::Clamp(Steer, -YawLimited, YawLimited);
			}
			Out.Yaw = Out.Distance * FMath::Tan(Steer) / Limits.Wheelbase;
		}
		Out.Offset = ArcChord(Out.Distance, Out.Yaw);
	}

	FCommand Guide(const FLimits& Limits, const FGuidanceInput& Input, FGuidanceState& State)
	{
		FCommand Command;
		const TConstArrayView<FVector2D> Path = Input.Path;
		const int32 Count = Path.Num();
		const bool bTracked = Limits.IsTracked();
		const bool bPivots = bTracked && Limits.bAllowPivot;

		// Standing without a path: tracks pivot to a facing asked for; anything else brakes to a stop.
		if (Count < 2 || State.bFailed)
		{
			if (Input.FacingYaw && bPivots && Count < 2)
			{
				const float Error = WrapAngle(*Input.FacingYaw - Input.Yaw);
				if (FMath::Abs(Error) > FacingTolerance && FMath::Abs(Input.Speed) <= PivotStartSpeed)
				{
					Command.bPivot = true;
					Command.PivotYawRate = GetPivotRate(Limits, Error);
				}
			}
			return Command;
		}

		auto IsStopCorner = [&](int32 Index)
		{
			return bTracked && Index > 0 && Index < Count - 1 && GetCornerAngle(Path, Index) > SharpCornerAngle;
		};

		// Past the points already reached.
		State.Target = FMath::Clamp(State.Target, 1, Count - 1);
		while (State.Target < Count - 1)
		{
			const FVector2D& From = Path[State.Target - 1];
			const FVector2D& To = Path[State.Target];
			const FVector2D Segment = To - From;
			const float LengthSquared = Segment.SizeSquared();
			const float Along = LengthSquared > UE_KINDA_SMALL_NUMBER ? FVector2D::DotProduct(Input.Location - From, Segment) / LengthSquared : 1.f;
			const float Reach = IsStopCorner(State.Target) ? CornerReach : WaypointReach;
			if (Along < 1.f && FVector2D::DistSquared(Input.Location, To) >= FMath::Square(Reach))
			{
				break;
			}
			++State.Target;
		}
		const bool bFinal = State.Target == Count - 1;

		// Where the unit next has to stop: the goal, or a sharp corner a track pivots at.
		int32 StopIndex = Count - 1;
		for (int32 Index = State.Target; Index < Count - 1; ++Index)
		{
			if (IsStopCorner(Index))
			{
				StopIndex = Index;
				break;
			}
		}
		float StopDistance = FVector2D::Distance(Input.Location, Path[State.Target]);
		float Remaining = StopDistance;
		for (int32 Index = State.Target; Index < Count - 1; ++Index)
		{
			const float Leg = FVector2D::Distance(Path[Index], Path[Index + 1]);
			StopDistance += Index < StopIndex ? Leg : 0.f;
			Remaining += Leg;
		}

		// The point pursued: a look-ahead distance toward the next path point, never past it. Cutting a corner would take the
		// hull inside it, toward the very obstacle AdjustPath moved the corner away from; heading for each point in turn, a
		// vehicle can only swing wide of a corner, away from what it turns round.
		float LookAhead = FMath::Clamp(PursuitSeconds * FMath::Abs(Input.Speed) + PursuitBase, PursuitMin, PursuitMax);
		if (Limits.IsWheeled())
		{
			LookAhead = FMath::Max(LookAhead, WheeledLookAheadRadii * Limits.MinTurnRadius);
		}
		const FVector2D ToTarget = Path[State.Target] - Input.Location;
		const float TargetDistance = ToTarget.Size();
		const FVector2D Point = TargetDistance > LookAhead ? Input.Location + ToTarget * (LookAhead / TargetDistance) : Path[State.Target];
		const FVector2D Forward(FMath::Cos(Input.Yaw), FMath::Sin(Input.Yaw));
		const FVector2D Right(-Forward.Y, Forward.X);
		const FVector2D ToPoint = Point - Input.Location;
		const float PointDistance = ToPoint.Size();
		if (PointDistance < UE_KINDA_SMALL_NUMBER)
		{
			return Command;
		}
		const float AheadOf = FVector2D::DotProduct(ToPoint, Forward);
		const float SideOf = FVector2D::DotProduct(ToPoint, Right);
		const float Heading = FMath::Atan2(SideOf, AheadOf);
		// The arc tangent to the hull through the point; it holds driving forward and backward alike.
		const float ArcCurvature = 2.f * SideOf / FMath::Square(PointDistance);
		const float MaxCurvature = Limits.IsWheeled() ? 1.f / Limits.MinTurnRadius : FLT_MAX;
		const float Brake = Limits.Braking;
		const float Lag = FMath::Abs(Input.Speed) * Input.Dt;

		// Reverses to a goal a short way behind, when the profile reverses.
		const float ReverseDistance = ReverseHullLengths * Input.HullLength;
		const bool bBehind = FMath::Abs(Heading) > UE_HALF_PI;
		if (Limits.ReverseSpeed > 0.f && bFinal && !State.bManoeuvring)
		{
			if (!State.bReversing && bBehind && Remaining < ReverseDistance)
			{
				State.bReversing = true;
				State.bPivoting = false;
			}
			else if (State.bReversing && (!bBehind || Remaining > 1.5f * ReverseDistance))
			{
				State.bReversing = false;
			}
		}
		else
		{
			State.bReversing = false;
		}
		if (State.bReversing)
		{
			const float Behind = WrapAngle(Heading - UE_PI);
			const float Speed = FMath::Min3(Limits.ReverseSpeed * Input.SpeedScale, FMath::Sqrt(2.f * Brake * FMath::Max(Remaining - Lag, 0.f)),
				FMath::Abs(ArcCurvature) > UE_KINDA_SMALL_NUMBER ? FMath::Sqrt(Limits.MaxLateralAcceleration / FMath::Abs(ArcCurvature)) : FLT_MAX);
			Command.Speed = -Speed * FMath::Max(FMath::Cos(Behind), 0.f);
			Command.Curvature = FMath::Clamp(ArcCurvature, -MaxCurvature, MaxCurvature);
			return Command;
		}

		// Tracks pivot toward a way that lies far off their heading, once they have stopped.
		if (bPivots)
		{
			if (!State.bPivoting && FMath::Abs(Heading) > Limits.PivotAngle)
			{
				State.bPivoting = true;
			}
			else if (State.bPivoting && FMath::Abs(Heading) < FMath::Min(PivotExitAngle, 0.5f * Limits.PivotAngle))
			{
				State.bPivoting = false;
			}
			if (State.bPivoting)
			{
				if (FMath::Abs(Input.Speed) <= PivotStartSpeed)
				{
					Command.bPivot = true;
					Command.PivotYawRate = GetPivotRate(Limits, Heading);
				}
				return Command;
			}
		}

		// Wheels cannot reach a goal inside their turning circle: back (or swing) out of it first, a few times at most.
		if (Limits.IsWheeled() && bFinal)
		{
			const float Radius = Limits.MinTurnRadius;
			const FVector2D& Goal = Path[Count - 1];
			const float ToRightCentre = FVector2D::Distance(Goal, Input.Location + Right * Radius);
			const float ToLeftCentre = FVector2D::Distance(Goal, Input.Location - Right * Radius);
			const bool bInside = FMath::Min(ToRightCentre, ToLeftCentre) < CircleInside * Radius;
			const bool bOutside = FMath::Min(ToRightCentre, ToLeftCentre) > CircleOutside * Radius;
			if (!State.bManoeuvring && bInside && Remaining > WaypointReach)
			{
				State.bManoeuvring = true;
				State.bFailed = ++State.Manoeuvres > MaxManoeuvres;
				if (State.bFailed)
				{
					return Command;
				}
			}
			else if (State.bManoeuvring && bOutside)
			{
				State.bManoeuvring = false;
			}
			if (State.bManoeuvring)
			{
				// Backing with the wheels turned away from the goal's side swings the nose toward it; without reverse, drive away from it.
				const float Side = ToRightCentre < ToLeftCentre ? 1.f : -1.f;
				Command.Speed = Limits.ReverseSpeed > 0.f ? -FMath::Min(Limits.ReverseSpeed, ManoeuvreSpeed) : FMath::Min(Limits.ForwardSpeed, ManoeuvreSpeed);
				Command.Curvature = -Side * MaxCurvature;
				return Command;
			}
		}

		// Pursue the point: on the arc through it, or turning hard toward it when it lies behind.
		float Curvature = bBehind ? (SideOf >= 0.f ? 1.f : -1.f) * (Limits.IsWheeled() ? MaxCurvature : 2.f / FMath::Max(PointDistance, 1.f)) : ArcCurvature;
		Curvature = FMath::Clamp(Curvature + Input.CurvatureBias, -MaxCurvature, MaxCurvature);

		float Speed = Limits.ForwardSpeed * Input.SpeedScale;
		Speed = FMath::Min(Speed, FMath::Sqrt(2.f * Brake * FMath::Max(StopDistance - Lag, 0.f)));
		if (FMath::Abs(Curvature) > UE_KINDA_SMALL_NUMBER)
		{
			Speed = FMath::Min3(Speed, FMath::Sqrt(Limits.MaxLateralAcceleration / FMath::Abs(Curvature)), Limits.MaxYawRate / FMath::Abs(Curvature));
		}
		// Slow in time for the corners ahead that a track does not stop at: to what its grip allows round them.
		{
			float Distance = FVector2D::Distance(Input.Location, Path[State.Target]);
			for (int32 Index = State.Target; Index < FMath::Min(StopIndex, State.Target + CornerLookAhead); ++Index)
			{
				const float Angle = GetCornerAngle(Path, Index);
				if (Angle > GentleCornerAngle)
				{
					const float CornerRadius = Limits.IsWheeled() ? Limits.MinTurnRadius : FMath::Max(Input.HullLength, 1.f);
					const float CornerSpeed = FMath::Sqrt(Limits.MaxLateralAcceleration * CornerRadius);
					Speed = FMath::Min(Speed, FMath::Sqrt(FMath::Square(CornerSpeed) + 2.f * Brake * FMath::Max(Distance - Lag, 0.f)));
				}
				Distance += FVector2D::Distance(Path[Index], Path[Index + 1]);
			}
		}
		// Tracks ease off while their heading is still swinging onto the way.
		if (bTracked)
		{
			Speed *= FMath::Max(FMath::Cos(Heading), 0.f);
		}
		Command.Speed = Speed;
		Command.Curvature = Curvature;
		return Command;
	}
}
