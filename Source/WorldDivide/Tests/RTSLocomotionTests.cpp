#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSLocomotion.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Track- and wheel-driven locomotion (#138) on flat, open ground: a world without a navmesh or obstacles, where the gear's
 * moves are never cut short, stepped by hand at 30, 60 and 120 updates a second. The hull must follow the kinematics of
 * its tracks or wheels, keep its speed, acceleration, braking, steering and yaw limits, report what it drove apart from
 * what moved it otherwise, and come out the same at every rate.
 */
namespace RTSLocomotionTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const double Rates[] = { 30.0, 60.0, 120.0 };

	/** A 7 x 3 m hull of engine cube, tracked like the M20. */
	constexpr float TrackGauge = 264.f;
	constexpr float TrackedTopSpeed = 1250.f;
	constexpr float TrackedReverse = 400.f;
	constexpr float TrackedTurnRate = 100.f;
	constexpr float TrackedAcceleration = 400.f;
	constexpr float TrackedBraking = 1000.f;

	/** A 5.4 x 2.5 m truck on wheels, like the flak truck. */
	constexpr float Wheelbase = 330.f;
	constexpr float WheeledTopSpeed = 1400.f;
	constexpr float WheeledReverse = 500.f;
	constexpr float WheeledTurnRate = 75.f;
	constexpr float WheeledAcceleration = 350.f;
	constexpr float WheeledBraking = 900.f;
	constexpr float MaxSteer = 35.f;
	constexpr float SteerRate = 60.f;

	/** #138's tolerances: 1 % of the distance driven, at least 1 cm, and 1 degree; limits within 1 %. */
	constexpr float DistanceShare = 0.01f;
	constexpr float MinDistanceTolerance = 1.f;
	constexpr float YawTolerance = 1.f;
	constexpr float LimitShare = 1.01f;

	URTSUnitData* MakeData(ERTSLocomotionType Type)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Data->MaxHealth = 100.f;
		FRTSLocomotionProfile& Profile = Data->Locomotion;
		Profile.Type = Type;
		Profile.MaxLateralAcceleration = 400.f;
		if (Type == ERTSLocomotionType::Tracked)
		{
			Data->MeshScale = FVector(7.f, 3.f, 2.f);
			Data->MoveSpeed = TrackedTopSpeed;
			Data->TurnRate = TrackedTurnRate;
			Profile.ReverseSpeed = TrackedReverse;
			Profile.Acceleration = TrackedAcceleration;
			Profile.Braking = TrackedBraking;
			Profile.TrackGauge = TrackGauge;
		}
		else
		{
			Data->MeshScale = FVector(5.4f, 2.5f, 2.5f);
			Data->MoveSpeed = WheeledTopSpeed;
			Data->TurnRate = WheeledTurnRate;
			Profile.ReverseSpeed = WheeledReverse;
			Profile.Acceleration = WheeledAcceleration;
			Profile.Braking = WheeledBraking;
			Profile.Wheelbase = Wheelbase;
			Profile.AxleTrack = 210.f;
			Profile.MaxSteerAngle = MaxSteer;
			Profile.SteerRate = SteerRate;
		}
		return Data;
	}

	/** One update of the unit's movement, as the engine ticks it, with the world's clock moved on. */
	void StepMovement(ARTSUnit& Unit, float Dt)
	{
		Unit.GetWorld()->TimeSeconds += Dt;
		Unit.GetUnitMovement()->TickComponent(Dt, LEVELTICK_All, nullptr);
	}

	/** Runs the unit's gear on Command for Seconds at Rate updates a second; Each, when given, after every update. */
	void Run(ARTSUnit& Unit, const RTSLocomotion::FCommand& Command, double Seconds, double Rate,
		TFunctionRef<void(float)> Each = [](float) {})
	{
		URTSUnitMovement& Movement = *Unit.GetUnitMovement();
		Movement.SetCommandOverride(Command);
		const int32 Steps = FMath::RoundToInt32(Seconds * Rate);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			StepMovement(Unit, 1.f / Rate);
			Each(1.f / Rate);
		}
	}

	/**
	 * The kinematic reference of two tracks ramping from a standstill to Left and Right, at Accel, integrated finely and
	 * independently of RTSLocomotion: where the hull ends, its yaw (radians), the distance driven and each track's travel.
	 */
	struct FTrackReference
	{
		FVector2D Location = FVector2D::ZeroVector;
		double Yaw = 0.0;
		double Distance = 0.0;
		double LeftTravel = 0.0;
		double RightTravel = 0.0;
	};

	FTrackReference IntegrateTracks(double Left, double Right, double Accel, double Gauge, double Seconds)
	{
		FTrackReference Reference;
		const int32 Steps = FMath::RoundToInt32(Seconds * 20000.0);
		const double Dt = Seconds / Steps;
		double LeftSpeed = 0.0;
		double RightSpeed = 0.0;
		double X = 0.0;
		double Y = 0.0;
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			const double LeftBefore = LeftSpeed;
			const double RightBefore = RightSpeed;
			LeftSpeed = FMath::Sign(Left) * FMath::Min(FMath::Abs(Left), FMath::Abs(LeftSpeed) + Accel * Dt);
			RightSpeed = FMath::Sign(Right) * FMath::Min(FMath::Abs(Right), FMath::Abs(RightSpeed) + Accel * Dt);
			const double LeftStep = 0.5 * (LeftBefore + LeftSpeed) * Dt;
			const double RightStep = 0.5 * (RightBefore + RightSpeed) * Dt;
			const double Forward = 0.5 * (LeftStep + RightStep);
			const double Turn = (LeftStep - RightStep) / Gauge;
			const double Mid = Reference.Yaw + 0.5 * Turn;
			X += Forward * FMath::Cos(Mid);
			Y += Forward * FMath::Sin(Mid);
			Reference.Yaw += Turn;
			Reference.Distance += FMath::Abs(Forward);
			Reference.LeftTravel += LeftStep;
			Reference.RightTravel += RightStep;
		}
		Reference.Location = FVector2D(X, Y);
		return Reference;
	}

	float YawOf(const ARTSUnit& Unit)
	{
		return Unit.GetActorRotation().Yaw;
	}
}

/**
 * Tracks at equal speeds drive straight, at unequal ones on an arc, both backward in reverse and opposite ways in a pivot
 * either way. Over 10 s from a standstill the hull ends where the differential-drive kinematics of its tracks put it,
 * within 1 % of the distance driven (at least 1 cm) and 1 degree, and its tracks report that travel; at 30, 60 and 120
 * updates a second alike.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleTurningTest, "WorldDivide.Units.VehicleTurning", RTSTests::TestFlags)

bool FRTSVehicleTurningTest::RunTest(const FString& Parameters)
{
	using namespace RTSLocomotionTests;
	struct FCase
	{
		const TCHAR* Name;
		float Left;
		float Right;
	};
	const FCase Cases[] = {
		{ TEXT("straight"), 800.f, 800.f },
		{ TEXT("arc right"), 900.f, 600.f },
		{ TEXT("arc left"), 600.f, 1000.f },
		{ TEXT("reverse"), -300.f, -300.f },
		{ TEXT("reverse arc"), -350.f, -200.f },
		{ TEXT("pivot right"), 200.f, -200.f },
		{ TEXT("pivot left"), -200.f, 200.f },
	};
	constexpr double Seconds = 10.0;
	URTSUnitData* Data = MakeData(ERTSLocomotionType::Tracked);

	for (const FCase& Case : Cases)
	{
		const FTrackReference Reference = IntegrateTracks(Case.Left, Case.Right, TrackedAcceleration, TrackGauge, Seconds);
		const float Tolerance = FMath::Max(DistanceShare * static_cast<float>(Reference.Distance), MinDistanceTolerance);
		TArray<FVector2D> Ends;
		TArray<float> EndYaws;
		for (const double Rate : Rates)
		{
			const FRTSTestWorld TestWorld;
			ARTSUnit* Unit = TestWorld.SpawnUnit(Data, 0, FVector::ZeroVector);
			if (!TestTrue(TEXT("The tracked test hull is driven by its gear"), Unit->GetUnitMovement()->UsesLocomotion()))
			{
				return false;
			}
			const RTSLocomotion::FCommand Command = RTSLocomotion::FCommand::FromTracks(Unit->GetUnitMovement()->GetLimits(), Case.Left, Case.Right);
			Run(*Unit, Command, Seconds, Rate);

			const FVector2D End(Unit->GetActorLocation());
			const float Yaw = YawOf(*Unit);
			const float ReferenceYaw = FMath::RadiansToDegrees(static_cast<float>(Reference.Yaw));
			const FString What = FString::Printf(TEXT("%s at %.0f Hz"), Case.Name, Rate);
			TestTrue(FString::Printf(TEXT("%s ends at the kinematic reference (%.2f cm off, tolerance %.2f cm, after %.0f cm)"), *What,
				FVector2D::Distance(End, Reference.Location), Tolerance, Reference.Distance), FVector2D::Distance(End, Reference.Location) <= Tolerance);
			TestTrue(FString::Printf(TEXT("%s ends on the reference heading (%.3f against %.3f degrees)"), *What, Yaw, ReferenceYaw),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, ReferenceYaw)) <= YawTolerance);
			const FRTSRealizedMotion& Motion = Unit->GetRealizedMotion();
			const float TravelTolerance = FMath::Max(DistanceShare * static_cast<float>(FMath::Abs(Reference.LeftTravel) + FMath::Abs(Reference.RightTravel)), MinDistanceTolerance);
			TestTrue(FString::Printf(TEXT("%s: the left track reports its travel (%.1f against %.1f cm)"), *What, Motion.LeftTravel, Reference.LeftTravel),
				FMath::Abs(Motion.LeftTravel - Reference.LeftTravel) <= TravelTolerance);
			TestTrue(FString::Printf(TEXT("%s: the right track reports its travel (%.1f against %.1f cm)"), *What, Motion.RightTravel, Reference.RightTravel),
				FMath::Abs(Motion.RightTravel - Reference.RightTravel) <= TravelTolerance);
			Ends.Add(End);
			EndYaws.Add(Yaw);
		}
		for (int32 Index = 1; Index < Ends.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s: %.0f Hz ends where 30 Hz does (%.2f cm, %.3f degrees apart)"), Case.Name, Rates[Index],
				FVector2D::Distance(Ends[Index], Ends[0]), FMath::FindDeltaAngleDegrees(EndYaws[Index], EndYaws[0])),
				FVector2D::Distance(Ends[Index], Ends[0]) <= Tolerance && FMath::Abs(FMath::FindDeltaAngleDegrees(EndYaws[Index], EndYaws[0])) <= YawTolerance);
		}
	}
	return true;
}

/**
 * The gear keeps its limits within 1 % at every rate: top speed forward and back, acceleration, braking, the yaw rate of
 * tracks and the steering rate of wheels. Changing from forward to reverse brakes through a standstill, and a hull
 * braking from top speed stops within 5 % of v² / 2b, plus one update's travel.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleLimitsTest, "WorldDivide.Units.VehicleLimits", RTSTests::TestFlags)

bool FRTSVehicleLimitsTest::RunTest(const FString& Parameters)
{
	using namespace RTSLocomotionTests;
	for (const double Rate : Rates)
	{
		for (const ERTSLocomotionType Type : { ERTSLocomotionType::Tracked, ERTSLocomotionType::Wheeled })
		{
			const bool bTracked = Type == ERTSLocomotionType::Tracked;
			const FString What = FString::Printf(TEXT("%s at %.0f Hz"), bTracked ? TEXT("Tracks") : TEXT("Wheels"), Rate);
			const float TopSpeed = bTracked ? TrackedTopSpeed : WheeledTopSpeed;
			const float Reverse = bTracked ? TrackedReverse : WheeledReverse;
			const float Accel = bTracked ? TrackedAcceleration : WheeledAcceleration;
			const float Brake = bTracked ? TrackedBraking : WheeledBraking;
			const FRTSTestWorld TestWorld;
			ARTSUnit* Unit = TestWorld.SpawnUnit(MakeData(Type), 0, FVector::ZeroVector);
			const URTSUnitMovement& Movement = *Unit->GetUnitMovement();
			auto Speed = [&Movement]() { return Movement.GetDrive().GetSpeed(Movement.GetLimits()); };
			const float Dt = 1.f / Rate;

			// Flat out from a standstill.
			RTSLocomotion::FCommand Ahead;
			Ahead.Speed = 2.f * TopSpeed;
			float MaxGain = 0.f;
			float MaxSpeed = 0.f;
			float Previous = 0.f;
			Run(*Unit, Ahead, 6.0, Rate, [&](float)
			{
				MaxGain = FMath::Max(MaxGain, (Speed() - Previous) / Dt);
				MaxSpeed = FMath::Max(MaxSpeed, Speed());
				Previous = Speed();
			});
			TestTrue(FString::Printf(TEXT("%s accelerate at most at %.0f cm/s2 (%.1f)"), *What, Accel, MaxGain), MaxGain <= Accel * LimitShare);
			TestTrue(FString::Printf(TEXT("%s reach and keep their top speed of %.0f cm/s (%.1f, now %.1f)"), *What, TopSpeed, MaxSpeed, Speed()),
				MaxSpeed <= TopSpeed * LimitShare && FMath::Abs(Speed() - TopSpeed) <= TopSpeed * (LimitShare - 1.f));

			// Braking to a stop.
			const double DrivenBefore = Unit->GetRealizedMotion().DrivenDistance;
			float MaxLoss = 0.f;
			Previous = Speed();
			Run(*Unit, RTSLocomotion::FCommand(), 3.0, Rate, [&](float)
			{
				MaxLoss = FMath::Max(MaxLoss, (Previous - Speed()) / Dt);
				Previous = Speed();
			});
			const float Stopping = static_cast<float>(Unit->GetRealizedMotion().DrivenDistance - DrivenBefore);
			const float Reference = RTSLocomotion::StoppingDistance(TopSpeed, Brake);
			TestTrue(FString::Printf(TEXT("%s brake at most at %.0f cm/s2 (%.1f)"), *What, Brake, MaxLoss), MaxLoss <= Brake * LimitShare);
			TestTrue(FString::Printf(TEXT("%s stop in %.1f cm against v2/2b = %.1f cm"), *What, Stopping, Reference),
				FMath::Abs(Stopping - Reference) <= 0.05f * Reference + TopSpeed * Dt);
			TestTrue(FString::Printf(TEXT("%s stand once stopped (%.2f cm/s)"), *What, Speed()), FMath::IsNearlyZero(Speed(), 0.01f));

			// Forward to reverse: through a standstill, never faster than reverse allows.
			RTSLocomotion::FCommand Forward;
			Forward.Speed = 0.5f * TopSpeed;
			Run(*Unit, Forward, 4.0, Rate);
			RTSLocomotion::FCommand Back;
			Back.Speed = -2.f * TopSpeed;
			float Slowest = FLT_MAX;
			float MinSpeed = 0.f;
			bool bMonotonic = true;
			Previous = Speed();
			Run(*Unit, Back, 6.0, Rate, [&](float)
			{
				Slowest = FMath::Min(Slowest, FMath::Abs(Speed()));
				MinSpeed = FMath::Min(MinSpeed, Speed());
				bMonotonic &= Speed() <= Previous + UE_KINDA_SMALL_NUMBER;
				Previous = Speed();
			});
			TestTrue(FString::Printf(TEXT("%s slow down steadily into reverse"), *What), bMonotonic);
			TestTrue(FString::Printf(TEXT("%s pass through a standstill on the way (slowest %.2f cm/s)"), *What, Slowest),
				Slowest <= FMath::Max(Brake, Accel) * Dt * LimitShare);
			TestTrue(FString::Printf(TEXT("%s reverse no faster than %.0f cm/s (%.1f)"), *What, Reverse, -MinSpeed), -MinSpeed <= Reverse * LimitShare);

			// The tightest turn the gear gives at speed: tracks hold their yaw rate, wheels steer no faster than their rate.
			Run(*Unit, Ahead, 6.0, Rate);
			RTSLocomotion::FCommand Hard = Ahead;
			Hard.Curvature = 1.f;
			float MaxYawRate = 0.f;
			float MaxSteerChange = 0.f;
			float PreviousSteer = Movement.GetDrive().Steer;
			Run(*Unit, Hard, 4.0, Rate, [&](float)
			{
				MaxYawRate = FMath::Max(MaxYawRate, FMath::Abs(Unit->GetRealizedMotion().YawRate));
				MaxSteerChange = FMath::Max(MaxSteerChange, FMath::RadiansToDegrees(FMath::Abs(Movement.GetDrive().Steer - PreviousSteer)) / Dt);
				PreviousSteer = Movement.GetDrive().Steer;
			});
			const float TurnRate = bTracked ? TrackedTurnRate : WheeledTurnRate;
			TestTrue(FString::Printf(TEXT("%s turn no faster than %.0f degrees/s (%.2f)"), *What, TurnRate, MaxYawRate), MaxYawRate <= TurnRate * LimitShare);
			if (!bTracked)
			{
				TestTrue(FString::Printf(TEXT("%s steer no faster than %.0f degrees/s (%.2f)"), *What, SteerRate, MaxSteerChange),
					MaxSteerChange <= SteerRate * LimitShare);
			}
		}
	}
	return true;
}

/**
 * Wheels turn by the bicycle model, yaw rate = v tan(steer) / wheelbase, within 1 %, and never tighter than their turning
 * circle; at speed the yaw limit widens the turn rather than tightening it. Reversing turns the other way, and standing
 * with full lock the hull does not turn at all (at most 0.1 degree in 5 s).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleWheeledTest, "WorldDivide.Units.VehicleWheeled", RTSTests::TestFlags)

bool FRTSVehicleWheeledTest::RunTest(const FString& Parameters)
{
	using namespace RTSLocomotionTests;
	const float MinRadius = Wheelbase / FMath::Tan(FMath::DegreesToRadians(MaxSteer));
	for (const double Rate : Rates)
	{
		const FRTSTestWorld TestWorld;
		ARTSUnit* Unit = TestWorld.SpawnUnit(MakeData(ERTSLocomotionType::Wheeled), 0, FVector::ZeroVector);
		const URTSUnitMovement& Movement = *Unit->GetUnitMovement();
		const FRTSRealizedMotion& Motion = Unit->GetRealizedMotion();
		const FString At = FString::Printf(TEXT("at %.0f Hz"), Rate);

		// Standing with full lock.
		RTSLocomotion::FCommand Lock;
		Lock.Curvature = 1.f;
		const float YawBefore = YawOf(*Unit);
		Run(*Unit, Lock, 5.0, Rate);
		TestTrue(FString::Printf(TEXT("Standing with full lock the hull does not turn %s (%.4f degrees)"), *At,
			FMath::FindDeltaAngleDegrees(YawBefore, YawOf(*Unit))), FMath::Abs(FMath::FindDeltaAngleDegrees(YawBefore, YawOf(*Unit))) <= 0.1f);

		// A steady turn at 20 degrees of steering.
		RTSLocomotion::FCommand Turn;
		Turn.Speed = 600.f;
		Turn.Curvature = FMath::Tan(FMath::DegreesToRadians(20.f)) / Wheelbase;
		Run(*Unit, Turn, 4.0, Rate);
		float WorstModel = 0.f;
		Run(*Unit, Turn, 1.0, Rate, [&](float)
		{
			const float Model = FMath::RadiansToDegrees(Motion.ForwardSpeed * FMath::Tan(Movement.GetDrive().Steer) / Wheelbase);
			WorstModel = FMath::Max(FMath::Abs(Motion.YawRate - Model) / FMath::Max(FMath::Abs(Model), UE_KINDA_SMALL_NUMBER), WorstModel);
		});
		TestTrue(FString::Printf(TEXT("A steady turn follows v tan(steer) / wheelbase %s (off by %.3f %%)"), *At, 100.f * WorstModel), WorstModel <= 0.01f);
		TestTrue(FString::Printf(TEXT("and steers what it was asked to (%.2f degrees)"), FMath::RadiansToDegrees(Movement.GetDrive().Steer)),
			FMath::IsNearlyEqual(FMath::RadiansToDegrees(Movement.GetDrive().Steer), 20.f, 0.2f));

		// Full lock, slow and at speed: never tighter than the turning circle.
		for (const float Speed : { 400.f, WheeledTopSpeed })
		{
			RTSLocomotion::FCommand Tight;
			Tight.Speed = Speed;
			Tight.Curvature = 1.f;
			Run(*Unit, Tight, 6.0, Rate);
			float Tightest = FLT_MAX;
			float FastestYaw = 0.f;
			Run(*Unit, Tight, 2.0, Rate, [&](float)
			{
				if (FMath::Abs(Motion.YawRate) > UE_KINDA_SMALL_NUMBER)
				{
					Tightest = FMath::Min(Tightest, FMath::Abs(Motion.ForwardSpeed / FMath::DegreesToRadians(Motion.YawRate)));
				}
				FastestYaw = FMath::Max(FastestYaw, FMath::Abs(Motion.YawRate));
			});
			TestTrue(FString::Printf(TEXT("At %.0f cm/s with full lock %s the turn is no tighter than %.1f cm (%.1f)"), Speed, *At, MinRadius, Tightest),
				Tightest >= MinRadius * (2.f - LimitShare));
			TestTrue(FString::Printf(TEXT("and turns no faster than %.0f degrees/s (%.2f)"), WheeledTurnRate, FastestYaw), FastestYaw <= WheeledTurnRate * LimitShare);
		}

		// Reversing with the wheels turned right swings the nose left.
		RTSLocomotion::FCommand Back;
		Back.Speed = -300.f;
		Back.Curvature = 1.f / 1000.f;
		Run(*Unit, Back, 4.0, Rate);
		TestTrue(FString::Printf(TEXT("Reversing with the wheels turned right turns the hull left %s (%.2f degrees/s)"), *At, Motion.YawRate),
			Motion.ForwardSpeed < 0.f && Motion.YawRate < 0.f);
	}
	return true;
}

/**
 * Route guidance of a tracked hull ordered to a point far behind it: it stops and pivots on the spot before it drives, never
 * moving sideways, and drives on to the goal; a wheeled hull, which cannot pivot, swings round on its turning circle instead
 * and still never moves sideways. Pure kinematics, at 30, 60 and 120 updates a second.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehiclePivotFirstTest, "WorldDivide.Units.VehiclePivotFirst", RTSTests::TestFlags)

bool FRTSVehiclePivotFirstTest::RunTest(const FString& Parameters)
{
	using namespace RTSLocomotionTests;
	constexpr float LateralLimit = 50.f;
	for (const ERTSLocomotionType Type : { ERTSLocomotionType::Tracked, ERTSLocomotionType::Wheeled })
	{
		const RTSLocomotion::FLimits Limits = RTSLocomotion::FLimits::FromData(*MakeData(Type));
		for (const double Rate : Rates)
		{
			const FString What = FString::Printf(TEXT("%s at %.0f Hz"), Type == ERTSLocomotionType::Tracked ? TEXT("Tracks") : TEXT("Wheels"), Rate);
			const float Dt = 1.f / Rate;
			const TArray<FVector2D> Path = { FVector2D::ZeroVector, FVector2D(-3000.f, 400.f) };
			RTSLocomotion::FGuidanceState State;
			RTSLocomotion::FDrive Drive;
			FVector2D Location = FVector2D::ZeroVector;
			float Yaw = 0.f;
			float MaxLateral = 0.f;
			float MovedBeforeFacing = 0.f;
			bool bFacing = false;
			bool bArrived = false;
			for (int32 Step = 0; Step < FMath::RoundToInt32(30.0 * Rate) && !bArrived; ++Step)
			{
				RTSLocomotion::FGuidanceInput Input;
				Input.Location = Location;
				Input.Yaw = Yaw;
				Input.Speed = Drive.GetSpeed(Limits);
				Input.Dt = Dt;
				Input.Path = Path;
				Input.HullLength = 700.f;
				const RTSLocomotion::FCommand Command = RTSLocomotion::Guide(Limits, Input, State);
				RTSLocomotion::FStep Out;
				RTSLocomotion::Step(Limits, Command, Drive, Dt, Out);
				const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
				const FVector2D Right(-Forward.Y, Forward.X);
				const FVector2D Moved = Forward * Out.Offset.X + Right * Out.Offset.Y;
				// Sideways to the heading halfway through the step: the chord of an arc lies along it.
				const float Mid = Yaw + 0.5f * Out.Yaw;
				MaxLateral = FMath::Max(MaxLateral, FMath::Abs(FVector2D::DotProduct(Moved, FVector2D(-FMath::Sin(Mid), FMath::Cos(Mid)))) / Dt);
				Location += Moved;
				Yaw += Out.Yaw;
				const FVector2D ToGoal = Path.Last() - Location;
				bFacing |= FMath::Abs(FMath::UnwindRadians(FMath::Atan2(ToGoal.Y, ToGoal.X) - Yaw)) < Limits.PivotAngle;
				if (!bFacing)
				{
					MovedBeforeFacing = FMath::Max(MovedBeforeFacing, static_cast<float>(Location.Size()));
				}
				bArrived = ToGoal.Size() < 25.f;
			}
			TestTrue(FString::Printf(TEXT("%s never move sideways faster than %.0f cm/s (%.2f)"), *What, LateralLimit, MaxLateral), MaxLateral <= LateralLimit);
			TestTrue(FString::Printf(TEXT("%s reach the goal behind them"), *What), bArrived);
			if (Type == ERTSLocomotionType::Tracked)
			{
				TestTrue(FString::Printf(TEXT("%s pivot toward it before they drive (moved %.1f cm first)"), *What, MovedBeforeFacing), MovedBeforeFacing <= 1.f);
			}
		}
	}
	return true;
}

/**
 * The placeholder vehicles get their tracks and wheels from their data, the same movement component drives all of them,
 * the Abrams drives forward, backs up and pivots on its own profile, infantry keeps its legs, and a profile with
 * impossible values is refused and falls back to legs.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleProfilesTest, "WorldDivide.Units.VehicleProfiles", RTSTests::TestFlags)

bool FRTSVehicleProfilesTest::RunTest(const FString& Parameters)
{
	using namespace RTSLocomotionTests;
	const TPair<const TCHAR*, ERTSLocomotionType> Expected[] = {
		{ TEXT("DA_TestM20Guardian"), ERTSLocomotionType::Tracked },
		{ TEXT("DA_TestTank"), ERTSLocomotionType::Tracked },
		{ TEXT("DA_TestAbrams"), ERTSLocomotionType::Tracked },
		{ TEXT("DA_TestFlakTruck"), ERTSLocomotionType::Wheeled },
		{ TEXT("DA_TestHarvester"), ERTSLocomotionType::Wheeled },
		{ TEXT("DA_TestBuilder"), ERTSLocomotionType::Wheeled },
		{ TEXT("DA_TestInfantry"), ERTSLocomotionType::Legs },
		{ TEXT("DA_TestJet"), ERTSLocomotionType::Legs },
	};
	const FRTSTestWorld TestWorld;
	for (const TPair<const TCHAR*, ERTSLocomotionType>& Entry : Expected)
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, *FString::Printf(TEXT("/Game/Data/Units/%s.%s"), Entry.Key, Entry.Key));
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Entry.Key), Data))
		{
			continue;
		}
		FString Error;
		TestEqual(*FString::Printf(TEXT("%s has its locomotion"), Entry.Key), Data->Locomotion.Type, Entry.Value);
		TestTrue(*FString::Printf(TEXT("%s's profile is valid (%s)"), Entry.Key, *Error), Data->Locomotion.Validate(&Error));
		ARTSUnit* Unit = TestWorld.SpawnUnit(Data, 0, FVector(0.f, 0.f, 10000.f));
		TestEqual(*FString::Printf(TEXT("%s is driven by its gear only on tracks or wheels"), Entry.Key),
			Unit->GetUnitMovement()->UsesLocomotion(), Entry.Value != ERTSLocomotionType::Legs);
	}

	// The Abrams on the profile setup_testmap.py gives it: forward and back along its hull, and a pivot on the spot.
	if (URTSUnitData* Abrams = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestAbrams.DA_TestAbrams")))
	{
		ARTSUnit* Unit = TestWorld.SpawnUnit(Abrams, 0, FVector(0.f, 20000.f, 0.f));
		const RTSLocomotion::FLimits& Limits = Unit->GetUnitMovement()->GetLimits();
		constexpr double Rate = 60.0;
		auto Along = [Unit]() { return Unit->GetActorLocation().X; };

		double Start = Along();
		Run(*Unit, RTSLocomotion::FCommand::FromTracks(Limits, 200.f, 200.f), 2.0, Rate);
		TestTrue(FString::Printf(TEXT("The Abrams drives forward (%.1f cm)"), Along() - Start), Along() - Start > 100.0);
		Run(*Unit, RTSLocomotion::FCommand(), 2.0, Rate);

		Start = Along();
		Run(*Unit, RTSLocomotion::FCommand::FromTracks(Limits, -200.f, -200.f), 2.0, Rate);
		TestTrue(FString::Printf(TEXT("and backs up (%.1f cm)"), Along() - Start), Along() - Start < -100.0);
		Run(*Unit, RTSLocomotion::FCommand(), 2.0, Rate);

		const FVector2D Before(Unit->GetActorLocation());
		const float YawBefore = Unit->GetActorRotation().Yaw;
		Run(*Unit, RTSLocomotion::FCommand::FromTracks(Limits, 200.f, -200.f), 1.0, Rate);
		const float Turned = FMath::Abs(FMath::FindDeltaAngleDegrees(YawBefore, Unit->GetActorRotation().Yaw));
		const float Drifted = FVector2D::Distance(Before, FVector2D(Unit->GetActorLocation()));
		TestTrue(FString::Printf(TEXT("and pivots on the spot (%.1f degrees, %.2f cm off)"), Turned, Drifted), Turned > 10.f && Drifted < 1.f);
	}

	auto Refused = [this, &TestWorld](const TCHAR* What, TFunctionRef<void(FRTSLocomotionProfile&)> Spoil, ERTSLocomotionType Type)
	{
		URTSUnitData* Data = MakeData(Type);
		Spoil(Data->Locomotion);
		TestFalse(*FString::Printf(TEXT("A profile with %s is refused"), What), Data->Locomotion.Validate());
		TestFalse(TEXT("and a unit with it walks instead"),
			TestWorld.SpawnUnit(Data, 0, FVector::ZeroVector)->GetUnitMovement()->UsesLocomotion());
	};
	AddExpectedMessage(TEXT("locomotion profile not valid"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 5, false);
	Refused(TEXT("no track gauge"), [](FRTSLocomotionProfile& Profile) { Profile.TrackGauge = 0.f; }, ERTSLocomotionType::Tracked);
	Refused(TEXT("an acceleration that is not a number"), [](FRTSLocomotionProfile& Profile) { Profile.Acceleration = NAN; }, ERTSLocomotionType::Tracked);
	Refused(TEXT("a negative reverse speed"), [](FRTSLocomotionProfile& Profile) { Profile.ReverseSpeed = -1.f; }, ERTSLocomotionType::Tracked);
	Refused(TEXT("steering past 80 degrees"), [](FRTSLocomotionProfile& Profile) { Profile.MaxSteerAngle = 90.f; }, ERTSLocomotionType::Wheeled);
	Refused(TEXT("an infinite wheelbase"), [](FRTSLocomotionProfile& Profile) { Profile.Wheelbase = INFINITY; }, ERTSLocomotionType::Wheeled);
	return true;
}

/**
 * What the gear reports is what it drove: a pivot runs the tracks opposite ways by gauge x turn / 2 each and drives the hull
 * nowhere, while a teleport or a push moves the hull without any travel, reported as recovery instead.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleRealizedMotionTest, "WorldDivide.Units.VehicleRealizedMotion", RTSTests::TestFlags)

bool FRTSVehicleRealizedMotionTest::RunTest(const FString& Parameters)
{
	using namespace RTSLocomotionTests;
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeData(ERTSLocomotionType::Tracked), 0, FVector::ZeroVector);
	const FRTSRealizedMotion& Motion = Unit->GetRealizedMotion();
	const RTSLocomotion::FLimits& Limits = Unit->GetUnitMovement()->GetLimits();
	constexpr double Rate = 60.0;

	Run(*Unit, RTSLocomotion::FCommand::FromTracks(Limits, 200.f, -200.f), 2.0, Rate);
	const float Expected = 0.5f * TrackGauge * static_cast<float>(Motion.TurnedRadians);
	TestTrue(FString::Printf(TEXT("A pivot runs the left track forward by gauge x turn / 2 (%.2f against %.2f cm)"), Motion.LeftTravel, Expected),
		FMath::IsNearlyEqual(static_cast<float>(Motion.LeftTravel), Expected, 0.01f * FMath::Abs(Expected)));
	TestTrue(FString::Printf(TEXT("and the right one back as far (%.2f cm)"), Motion.RightTravel),
		FMath::IsNearlyEqual(static_cast<float>(Motion.RightTravel), -Expected, 0.01f * FMath::Abs(Expected)));
	TestTrue(FString::Printf(TEXT("and drives the hull nowhere (%.3f cm, at %s)"), Motion.DrivenDistance, *Unit->GetActorLocation().ToCompactString()),
		FMath::Abs(Motion.DrivenDistance) < 0.01 && FVector2D(Unit->GetActorLocation()).Size() < 0.01f);
	TestEqual(TEXT("A pivot reports itself"), Motion.Status, ERTSLocomotionStatus::Pivoting);
	Run(*Unit, RTSLocomotion::FCommand(), 1.0, Rate);

	// Teleported while standing, then pushed while driving.
	const FRTSRealizedMotion Before = Motion;
	Unit->SetActorLocation(Unit->GetActorLocation() + FVector(500.f, 0.f, 0.f));
	Run(*Unit, RTSLocomotion::FCommand(), 0.5, Rate);
	TestEqual(TEXT("A teleport counts as one recovery"), Motion.RecoveryCount, Before.RecoveryCount + 1);
	TestTrue(FString::Printf(TEXT("of its displacement (%s)"), *Motion.RecoveryDisplacement.ToString()),
		Motion.RecoveryDisplacement.Equals(Before.RecoveryDisplacement + FVector2D(500.f, 0.f), 0.01f));
	TestTrue(TEXT("and advances neither the hull's travel nor its tracks"), Motion.DrivenDistance == Before.DrivenDistance
		&& Motion.LeftTravel == Before.LeftTravel && Motion.RightTravel == Before.RightTravel);

	RTSLocomotion::FCommand Ahead;
	Ahead.Speed = 600.f;
	const double DrivenBefore = Motion.DrivenDistance;
	const FVector Start = Unit->GetActorLocation();
	Run(*Unit, Ahead, 1.0, Rate);
	Unit->SetActorLocation(Unit->GetActorLocation() + Unit->GetActorRightVector() * 40.f);
	Run(*Unit, Ahead, 1.0, Rate);
	const float Driven = static_cast<float>(Motion.DrivenDistance - DrivenBefore);
	const float Along = FVector::DotProduct(Unit->GetActorLocation() - Start, Unit->GetActorForwardVector());
	TestTrue(FString::Printf(TEXT("A push sideways while driving is no travel: driven %.2f cm, %.2f cm along the hull"), Driven, Along),
		FMath::IsNearlyEqual(Driven, Along, 0.1f));
	TestEqual(TEXT("and counts as one recovery more"), Motion.RecoveryCount, Before.RecoveryCount + 2);
	return true;
}

#endif
