#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AIController.h"
#include "Commands/RTSCommand.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestGeometry.h"
#include "Tests/RTSTestPIE.h"
#include "Tests/RTSTestScale.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"

/**
 * Track-driven vehicles on L_TestMap (#138), with its navmesh and obstacles: driven flat out into a block they stop against
 * it without tunnelling, and stop driving the moment they touch; they climb the ridge, stop at the edge of the ground, pass
 * each other head on and pivot before they drive to a point behind them, never moving sideways; at 30, 60 and 120 game
 * updates a second. A blockage no path knows about stops a move within two seconds, without rocking back and forth after.
 */
namespace RTSVehicleMovementTests
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");

	/**
	 * The test's vehicles drive for the enemy's team, so the bases near the ridge and the east edge do not shoot them, and get
	 * their orders straight from the server.
	 */
	constexpr int32 DriveTeam = 1;
	constexpr double NavTimeout = 30.0;

	/** #138: no new penetration beyond 1 cm; sideways propulsion at most 50 cm/s; driven travel stops within one update. */
	constexpr float PenetrationTolerance = 1.f;
	constexpr float LateralLimit = 50.f;
	constexpr float BlockedTravel = 0.1f;

	/**
	 * A vehicle stuck against a blockage backs off or stops within BlockedHandling game seconds and gives its move up within
	 * GiveUpSeconds; over RestSeconds from when it first stopped it backs off at most once.
	 */
	constexpr double BlockedHandling = 2.0;
	constexpr double GiveUpSeconds = 5.0;
	constexpr double RestSeconds = 10.0;

	/** One drive of the test: the unit it spawns, what it checks every frame, when it is over and what it checks then. */
	struct FScenario
	{
		FString Name;
		/** The M20 unless set: the placeholder tank, whose gun lies inside its hull, drives where its barrel would get in the way. */
		const TCHAR* DataPath = nullptr;
		FVector Start = FVector::ZeroVector;
		float Yaw = 0.f;
		double Timeout = 30.0;
		/** Called once the unit stands, to order or command it. */
		TFunction<void(ARTSUnit&, ARTSPlayerController&)> Begin;
		/** Called every frame; returns true once the drive is over. */
		TFunction<bool(ARTSUnit&, double)> Tick;
		/** Checks the outcome. */
		TFunction<void(ARTSUnit&, FAutomationTestBase&)> End;
		/** The player's own unit, ordered through its controller; otherwise the enemy's, ordered on the server. */
		bool bPlayerTeam = false;
	};

	struct FState
	{
		TArray<FScenario> Scenarios;
		int32 Index = -1;
		TWeakObjectPtr<ARTSUnit> Unit;
		double ScenarioStart = 0.0;
		double PhaseStart = 0.0;
		double Rate = 30.0;
		TArray<FBox2D> Obstacles;
	};

	ARTSPlayerController* GetController()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	ARTSUnit* SpawnUnit(UWorld& World, URTSUnitData* Data, int32 TeamId, const FVector& Location, float Yaw)
	{
		FTransform Transform(FRotator(0.f, Yaw, 0.f), Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	void IssueMove(ARTSPlayerController& Controller, ARTSUnit& Unit, const FVector& Target)
	{
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = Target;
		if (Unit.GetTeamId() == Controller.GetTeamId())
		{
			Controller.Server_IssueCommand({ &Unit }, Move);
		}
		else
		{
			Unit.ExecuteCommand(Move);
		}
	}

	/** The smallest gap between the unit's hull and the map's walls and blocks. */
	float GetObstacleGap(const ARTSUnit& Unit, const TArray<FBox2D>& Obstacles)
	{
		float Gap = FLT_MAX;
		for (const FBox2D& Box : Obstacles)
		{
			Gap = FMath::Min(Gap, RTSTests::GetHullGap(Unit.GetFootprint(), Box));
		}
		return Gap;
	}

	bool IsMoving(const ARTSUnit& Unit)
	{
		const AAIController* AI = Unit.GetController<AAIController>();
		return AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVehicleWaitForPlay, FAutomationTestBase*, Test, TSharedRef<RTSVehicleMovementTests::FState>, State);
bool FRTSVehicleWaitForPlay::Update()
{
	using namespace RTSVehicleMovementTests;
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}
	UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	const ARTSPlayerController* Controller = GetController();
	if (World && World->HasBegunPlay() && Controller && Controller->GetTeamId() != INDEX_NONE && RTSTests::IsNavigationReady(*World))
	{
		// Every rate runs on a fixed step, as fast as the machine ticks.
		RTSTests::ApplyTestFixedStep(State->Rate);
		FApp::SetFixedDeltaTime(1.0 / State->Rate);
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const FString Label = It->GetActorLabel();
			if (Label.StartsWith(TEXT("Wall_")) || Label.StartsWith(TEXT("Block_")))
			{
				const FBox Box = It->GetComponentsBoundingBox();
				State->Obstacles.Add(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)));
			}
		}
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("PIE did not start or the navmeshes were not built in time"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVehicleRunScenarios, FAutomationTestBase*, Test, TSharedRef<RTSVehicleMovementTests::FState>, State);
bool FRTSVehicleRunScenarios::Update()
{
	using namespace RTSVehicleMovementTests;
	ARTSPlayerController* Controller = GetController();
	if (!Controller)
	{
		Test->AddError(TEXT("No ARTSPlayerController in the PIE world"));
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	const double Now = World.GetTimeSeconds();
	if (State->Index >= 0)
	{
		FScenario& Scenario = State->Scenarios[State->Index];
		ARTSUnit* Unit = State->Unit.Get();
		if (!Test->TestNotNull(*FString::Printf(TEXT("%s: the unit lives"), *Scenario.Name), Unit))
		{
			return true;
		}
		const double Elapsed = Now - State->ScenarioStart;
		const bool bOver = Scenario.Tick(*Unit, Elapsed);
		if (!bOver && Elapsed < Scenario.Timeout)
		{
			return false;
		}
		Test->TestTrue(*FString::Printf(TEXT("%s ends within %.0f s"), *Scenario.Name, Scenario.Timeout), bOver);
		Scenario.End(*Unit, *Test);
		Unit->Destroy();
	}
	++State->Index;
	if (!State->Scenarios.IsValidIndex(State->Index))
	{
		URTSUnitMovement::TestObstacles.Reset();
		return true;
	}
	FScenario& Next = State->Scenarios[State->Index];
	URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, Next.DataPath ? Next.DataPath : M20Path);
	if (!Test->TestNotNull(TEXT("The vehicle's data"), Data))
	{
		return true;
	}
	ARTSUnit* Unit = SpawnUnit(World, Data, Next.bPlayerTeam ? Controller->GetTeamId() : DriveTeam, Next.Start, Next.Yaw);
	State->Unit = Unit;
	State->ScenarioStart = Now;
	Next.Begin(*Unit, *Controller);
	return false;
}

namespace RTSVehicleMovementTests
{
	/** Adds the drives of the collision test, at the rate the test runs with. */
	void AddCollisionScenarios(const TSharedRef<FState>& State, const UWorld& EditorWorld)
	{
		const FBox2D Block = RTSTests::GetLabelledBox(EditorWorld, TEXT("Block_North"));
		const float HalfSize = RTSTests::GetMapHalfSize(EditorWorld);

		// Flat out into Block_North's north face, with no path to stop it: the gear alone must stop at the face.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Flat out into Block_North");
			Scenario.DataPath = TankPath;
			Scenario.Start = FVector(Block.GetCenter().X, Block.Max.Y + 6000.f, 0.f);
			Scenario.Yaw = -90.f;
			Scenario.Timeout = 15.0;
			struct FData { float MinGap = FLT_MAX; TOptional<double> BlockedTravel; float AfterBlocked = 0.f; bool bTouched = false; double TouchTime = 0.0; };
			TSharedRef<FData> Data = MakeShared<FData>();
			Scenario.Begin = [](ARTSUnit& Unit, ARTSPlayerController&)
			{
				RTSLocomotion::FCommand Command;
				Command.Speed = 1.e5f;
				Unit.GetUnitMovement()->SetCommandOverride(Command);
			};
			Scenario.Tick = [State, Data](ARTSUnit& Unit, double Elapsed)
			{
				Data->MinGap = FMath::Min(Data->MinGap, GetObstacleGap(Unit, State->Obstacles));
				const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
				if (Data->BlockedTravel)
				{
					// The first blocked update may still drive part of its step; every one after drives nothing.
					Data->AfterBlocked = FMath::Max(Data->AfterBlocked, static_cast<float>(FMath::Abs(Motion.DrivenDistance - *Data->BlockedTravel)));
				}
				else if (Motion.Status == ERTSLocomotionStatus::Blocked)
				{
					Data->BlockedTravel = Motion.DrivenDistance;
					Data->TouchTime = Elapsed;
				}
				return Data->BlockedTravel && Elapsed - Data->TouchTime > 2.0;
			};
			Scenario.End = [State, Data, Block](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(TEXT("Flat out into Block_North (the placeholder tank): the gear reports itself blocked"), Data->BlockedTravel.IsSet());
				Test.TestTrue(*FString::Printf(TEXT("its hull never goes more than %.0f cm into the block (closest %.2f cm)"), PenetrationTolerance, Data->MinGap),
					Data->MinGap >= -PenetrationTolerance);
				Test.TestTrue(*FString::Printf(TEXT("it stays on its side, not through (y %.0f)"), Unit.GetActorLocation().Y), Unit.GetActorLocation().Y > Block.Max.Y);
				Test.TestTrue(*FString::Printf(TEXT("and drives no further once blocked (%.3f cm)"), Data->AfterBlocked), Data->AfterBlocked <= BlockedTravel);
				Test.TestTrue(*FString::Printf(TEXT("stopping within 50 cm of the face (%.1f cm)"), GetObstacleGap(Unit, State->Obstacles)),
					GetObstacleGap(Unit, State->Obstacles) <= 50.f);
			};
		}

		// Toward the east edge of the ground, past the end of the navmesh.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Toward the edge of the ground");
			Scenario.Start = FVector(HalfSize - 3000.f, 12000.f, 0.f);
			Scenario.Yaw = 0.f;
			Scenario.Timeout = 15.0;
			TSharedRef<bool> bBlocked = MakeShared<bool>(false);
			Scenario.Begin = [](ARTSUnit& Unit, ARTSPlayerController&)
			{
				RTSLocomotion::FCommand Command;
				Command.Speed = 1.e5f;
				Unit.GetUnitMovement()->SetCommandOverride(Command);
			};
			Scenario.Tick = [bBlocked](ARTSUnit& Unit, double Elapsed)
			{
				*bBlocked |= Unit.GetRealizedMotion().Status == ERTSLocomotionStatus::Blocked;
				return *bBlocked && Elapsed > 6.0;
			};
			Scenario.End = [bBlocked, HalfSize](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Unit.GetWorld());
				FNavLocation Projected;
				const bool bSupported = NavSystem && NavSystem->ProjectPointToNavigation(Unit.GetNavAgentLocation(), Projected, FVector(10.f, 10.f, 200.f),
					NavSystem->GetNavDataForProps(Unit.GetNavAgentPropertiesRef(), Unit.GetNavAgentLocation()));
				Test.TestTrue(TEXT("Toward the edge: the gear stops where the ground it may drive on ends"), *bBlocked);
				Test.TestTrue(*FString::Printf(TEXT("standing on its navmesh (at x %.0f, the ground ends at %.0f)"), Unit.GetActorLocation().X, HalfSize), bSupported);
			};
		}

		// Over the ridge east of the bases: up a 15 degree ramp, across the top and down.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Over the ridge");
			Scenario.Start = FVector(11300.f, 3000.f, 0.f);
			Scenario.Timeout = 40.0;
			const FVector Target(16200.f, 3000.f, 0.f);
			TSharedRef<float> Highest = MakeShared<float>(-FLT_MAX);
			Scenario.Begin = [Target](ARTSUnit& Unit, ARTSPlayerController& Controller) { IssueMove(Controller, Unit, Target); };
			Scenario.Tick = [Highest](ARTSUnit& Unit, double Elapsed)
			{
				*Highest = FMath::Max(*Highest, static_cast<float>(Unit.GetNavAgentLocation().Z));
				return Elapsed > 1.0 && Unit.GetQueuedCommandCount() == 0;
			};
			Scenario.End = [Highest, Target](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(*FString::Printf(TEXT("Over the ridge: it climbs to the top (%.0f cm up)"), *Highest), *Highest > 250.f);
				Test.TestTrue(*FString::Printf(TEXT("and arrives on the far side (%.0f cm from the target)"), FVector::Dist2D(Unit.GetActorLocation(), Target)),
					FVector::Dist2D(Unit.GetActorLocation(), Target) <= 100.f);
			};
		}

		// Ordered to a point far behind: it pivots on the spot first, and never drives sideways.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Ordered behind");
			Scenario.Start = FVector(-8000.f, 15000.f, 0.f);
			Scenario.Yaw = 0.f;
			Scenario.Timeout = 40.0;
			const FVector Target(-11000.f, 15600.f, 0.f);
			struct FData { FTransform Last; int32 Recoveries = 0; float MaxLateral = 0.f; float MovedBeforeFacing = 0.f; bool bFacing = false; };
			TSharedRef<FData> Data = MakeShared<FData>();
			Scenario.Begin = [Target, Data](ARTSUnit& Unit, ARTSPlayerController& Controller)
			{
				Data->Last = Unit.GetActorTransform();
				IssueMove(Controller, Unit, Target);
			};
			Scenario.Tick = [Data, Target](ARTSUnit& Unit, double Elapsed)
			{
				const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
				const float Dt = Unit.GetWorld()->GetDeltaSeconds();
				// Frames something else moved the hull (avoidance, backing out) are recovery, reported apart.
				if (Motion.RecoveryCount == Data->Recoveries && Dt > 0.f)
				{
					const FVector Moved = Unit.GetActorLocation() - Data->Last.GetLocation();
					const FRotator Mid(0.f, Data->Last.Rotator().Yaw + 0.5f * FMath::FindDeltaAngleDegrees(Data->Last.Rotator().Yaw, Unit.GetActorRotation().Yaw), 0.f);
					const FVector Side = FRotationMatrix(Mid).GetUnitAxis(EAxis::Y);
					Data->MaxLateral = FMath::Max(Data->MaxLateral, FMath::Abs(static_cast<float>(FVector::DotProduct(Moved, Side))) / Dt);
				}
				Data->Recoveries = Motion.RecoveryCount;
				Data->Last = Unit.GetActorTransform();
				const FVector ToTarget = Target - Unit.GetActorLocation();
				Data->bFacing |= FMath::Abs(FMath::FindDeltaAngleDegrees(Unit.GetActorRotation().Yaw, ToTarget.Rotation().Yaw)) < 60.f;
				if (!Data->bFacing)
				{
					Data->MovedBeforeFacing = FMath::Max(Data->MovedBeforeFacing, static_cast<float>(FVector::Dist2D(Unit.GetActorLocation(), FVector(-8000.f, 15000.f, 0.f))));
				}
				return Elapsed > 1.0 && Unit.GetQueuedCommandCount() == 0;
			};
			Scenario.End = [Data, Target](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(*FString::Printf(TEXT("Ordered behind: it pivots before it drives (moved %.1f cm first)"), Data->MovedBeforeFacing), Data->MovedBeforeFacing <= 5.f);
				Test.TestTrue(*FString::Printf(TEXT("never driving sideways faster than %.0f cm/s (%.1f)"), LateralLimit, Data->MaxLateral), Data->MaxLateral <= LateralLimit);
				Test.TestTrue(*FString::Printf(TEXT("and arrives (%.0f cm from the target)"), FVector::Dist2D(Unit.GetActorLocation(), Target)),
					FVector::Dist2D(Unit.GetActorLocation(), Target) <= 100.f);
			};
		}

		// Short of a point a little behind: it reverses to it rather than turning round.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Reversing a short way");
			Scenario.Start = FVector(-4000.f, 15000.f, 0.f);
			Scenario.Yaw = 0.f;
			Scenario.Timeout = 30.0;
			const FVector Target(-4600.f, 15000.f, 0.f);
			TSharedRef<bool> bReversed = MakeShared<bool>(false);
			TSharedRef<float> MaxTurn = MakeShared<float>(0.f);
			Scenario.Begin = [Target](ARTSUnit& Unit, ARTSPlayerController& Controller) { IssueMove(Controller, Unit, Target); };
			Scenario.Tick = [bReversed, MaxTurn](ARTSUnit& Unit, double Elapsed)
			{
				*bReversed |= Unit.GetRealizedMotion().Status == ERTSLocomotionStatus::Reversing;
				*MaxTurn = FMath::Max(*MaxTurn, FMath::Abs(FMath::FindDeltaAngleDegrees(0.f, Unit.GetActorRotation().Yaw)));
				return Elapsed > 1.0 && Unit.GetQueuedCommandCount() == 0;
			};
			Scenario.End = [bReversed, MaxTurn, Target](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(TEXT("Reversing a short way: it reverses"), *bReversed);
				Test.TestTrue(*FString::Printf(TEXT("without turning round (at most %.1f degrees)"), *MaxTurn), *MaxTurn < 30.f);
				Test.TestTrue(*FString::Printf(TEXT("and arrives (%.0f cm from the target)"), FVector::Dist2D(Unit.GetActorLocation(), Target)),
					FVector::Dist2D(Unit.GetActorLocation(), Target) <= 100.f);
			};
		}
	}
}

/**
 * The M20 driven by its tracks on L_TestMap at 30, 60 and 120 game updates a second: flat out into Block_North it stops
 * at the face, no more than 1 cm in, and drives no further the update after it touched; toward the edge of the ground it
 * stops where its navmesh ends; it climbs over the ridge; ordered far behind itself it pivots first and never drives
 * sideways faster than 50 cm/s; a little behind itself it reverses. Narrow passages, pivots at corners and other vehicles
 * are driven by WorldDivide.Navigation.VehicleAgent, HullClearance.Drive and the avoidance tests.
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSVehicleCollisionTest, "WorldDivide.Navigation.VehicleCollision", RTSTests::TestFlags)

void FRTSVehicleCollisionTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Rate : { TEXT("30"), TEXT("60"), TEXT("120") })
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("%sHz"), Rate));
		OutTestCommands.Add(Rate);
	}
}

bool FRTSVehicleCollisionTest::RunTest(const FString& Parameters)
{
	using namespace RTSVehicleMovementTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	const UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	TSharedRef<FState> State = MakeShared<FState>();
	State->Rate = FCString::Atod(*Parameters);
	if (!EditorWorld || State->Rate <= 0.0)
	{
		AddError(TEXT("No editor world or rate"));
		return false;
	}
	AddCollisionScenarios(State, *EditorWorld);
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleWaitForPlay(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleRunScenarios(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/**
 * An M20 on a move order runs into a blockage no path knows about, across its whole way: within two seconds of first
 * stopping against it the M20 backs off to path again, and once stuck again gives the move up without arriving. Over ten
 * seconds it backs off no more than that once, and then stands where it stopped.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleBlockedTest, "WorldDivide.Navigation.VehicleBlocked", RTSTests::TestFlags)

bool FRTSVehicleBlockedTest::RunTest(const FString& Parameters)
{
	using namespace RTSVehicleMovementTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	TSharedRef<FState> State = MakeShared<FState>();
	FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
	Scenario.Name = TEXT("Into a blockage");
	Scenario.Start = FVector(-6000.f, 20000.f, 0.f);
	Scenario.Yaw = 0.f;
	Scenario.Timeout = 40.0;
	const FVector Target(2000.f, 20000.f, 0.f);
	struct FData
	{
		TOptional<double> FirstBlocked;
		/** The first time it did something about it: backed off, or gave the move up. */
		TOptional<double> Handled;
		TOptional<double> GaveUp;
		FVector RestLocation = FVector::ZeroVector;
		float RestDrift = 0.f;
		int32 SignChanges = 0;
		float LastSign = 0.f;
	};
	TSharedRef<FData> Data = MakeShared<FData>();
	Scenario.Begin = [Target](ARTSUnit& Unit, ARTSPlayerController& Controller)
	{
		// A wall of the test's own across the way, 15 m ahead and 40 m wide.
		URTSUnitMovement::TestObstacles = { FBox2D(FVector2D(-6000.f + 1500.f, 20000.f - 2000.f), FVector2D(-6000.f + 1700.f, 20000.f + 2000.f)) };
		IssueMove(Controller, Unit, Target);
	};
	Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed)
	{
		const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
		if (!Data->FirstBlocked && Motion.Status == ERTSLocomotionStatus::Blocked)
		{
			Data->FirstBlocked = Elapsed;
		}
		if (!Data->FirstBlocked)
		{
			return false;
		}
		if (!Data->Handled && (Motion.Status == ERTSLocomotionStatus::Reversing || !IsMoving(Unit)))
		{
			Data->Handled = Elapsed;
		}
		if (!Data->GaveUp && !IsMoving(Unit))
		{
			Data->GaveUp = Elapsed;
			Data->RestLocation = Unit.GetActorLocation();
		}
		if (Data->GaveUp)
		{
			Data->RestDrift = FMath::Max(Data->RestDrift, static_cast<float>(FVector::Dist2D(Unit.GetActorLocation(), Data->RestLocation)));
		}
		const float Sign = FMath::Abs(Motion.ForwardSpeed) > 1.f ? FMath::Sign(Motion.ForwardSpeed) : 0.f;
		if (Sign != 0.f && Data->LastSign != 0.f && Sign != Data->LastSign)
		{
			++Data->SignChanges;
		}
		Data->LastSign = Sign != 0.f ? Sign : Data->LastSign;
		return Elapsed - *Data->FirstBlocked >= RestSeconds && Data->GaveUp;
	};
	Scenario.End = [Data, Target](ARTSUnit& Unit, FAutomationTestBase& Test)
	{
		URTSUnitMovement::TestObstacles.Reset();
		if (!Test.TestTrue(TEXT("The M20 stops against the blockage"), Data->FirstBlocked.IsSet())
			|| !Test.TestTrue(TEXT("and gives its move up"), Data->GaveUp.IsSet()) || !Data->Handled)
		{
			return;
		}
		Test.TestTrue(*FString::Printf(TEXT("It backs off or stops within %.0f s of first stopping (%.2f s)"), BlockedHandling, *Data->Handled - *Data->FirstBlocked),
			*Data->Handled - *Data->FirstBlocked <= BlockedHandling);
		Test.TestTrue(*FString::Printf(TEXT("and gives the move up within %.0f s (%.2f s)"), GiveUpSeconds, *Data->GaveUp - *Data->FirstBlocked),
			*Data->GaveUp - *Data->FirstBlocked <= GiveUpSeconds);
		Test.TestEqual(TEXT("Its order is over"), Unit.GetQueuedCommandCount(), 0);
		Test.TestTrue(*FString::Printf(TEXT("without arriving (%.0f cm short)"), FVector::Dist2D(Unit.GetActorLocation(), Target)),
			FVector::Dist2D(Unit.GetActorLocation(), Target) > 1000.f);
		Test.TestTrue(*FString::Printf(TEXT("After that it stands (drifted %.1f cm)"), Data->RestDrift), Data->RestDrift <= 5.f);
		Test.TestTrue(*FString::Printf(TEXT("Over %.0f s it backs off at most once, never rocking back and forth (%d changes of direction)"), RestSeconds,
			Data->SignChanges), Data->SignChanges <= 2);
	};

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleWaitForPlay(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleRunScenarios(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
