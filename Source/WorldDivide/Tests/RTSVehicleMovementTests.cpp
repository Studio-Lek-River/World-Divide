#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AIController.h"
#include "Commands/RTSCommand.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSSupplySource.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/RTSHullClearance.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestGeometry.h"
#include "Tests/RTSTestPIE.h"
#include "Tests/RTSTestScale.h"
#include "Units/RTSGround.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#include "Units/RTSUnitShape.h"

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

	/** A solid engine cube filling Box that navigation leaves out (#168): the navmesh runs on under it. */
	AActor* SpawnSolid(UWorld& World, const FBox& Box)
	{
		const FTransform Transform(FRotator::ZeroRotator, Box.GetCenter(), Box.GetSize() / 100.f);
		AStaticMeshActor* Actor = World.SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetCanEverAffectNavigation(false);
		Actor->FinishSpawning(Transform);
		return Actor;
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
		TWeakObjectPtr<AActor> Wall;
	};
	TSharedRef<FData> Data = MakeShared<FData>();
	Scenario.Begin = [Target, Data](ARTSUnit& Unit, ARTSPlayerController& Controller)
	{
		// A wall of the test's own across the way, 15 m ahead and 40 m wide, that navigation leaves out (#168).
		Data->Wall = SpawnSolid(*Unit.GetWorld(), FBox(FVector(-6000.f + 1500.f, 20000.f - 2000.f, 0.f), FVector(-6000.f + 1700.f, 20000.f + 2000.f, 300.f)));
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
		if (Data->Wall.IsValid())
		{
			Data->Wall->Destroy();
		}
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

namespace RTSVehicleMovementTests
{
	const TCHAR* HarvesterPath = TEXT("/Game/Data/Units/DA_TestHarvester.DA_TestHarvester");

	/** #170: where the drives into a blockage start, facing east, and their target 80 m on, behind the blockage. */
	const FVector FeedbackStart(-6000.f, 20000.f, 0.f);
	const FVector FeedbackTarget(2000.f, 20000.f, 0.f);

	/** A wall the navmesh leaves out (#168) across the way, 15 m ahead of FeedbackStart and 40 m wide. */
	const FBox FeedbackWall(FVector(-4500.f, 18000.f, 0.f), FVector(-4300.f, 22000.f, 300.f));

	/**
	 * Once every unit stands, a drive runs on this long: past the refusal cooldown and the span in which a stuck unit gives
	 * up, so a second refusal would arrive within it.
	 */
	constexpr double FeedbackQuietSeconds = 4.0;

	/** The drives that keep their order run this long against the blockage. */
	constexpr double KeptOrderSeconds = 15.0;

	/** What a drive into the blockage saw of the player's refusals. */
	struct FFeedback
	{
		TWeakObjectPtr<ARTSPlayerController> Controller;
		int32 RefusalsAtStart = 0;
		/** The drive's units, the scenario's own first. */
		TArray<TWeakObjectPtr<ARTSUnit>> Group;
		/** Spawned for the drive and destroyed after it: the wall, the group's other units, targets. */
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TWeakObjectPtr<AActor> Wall;
		/** Refusals the player had when the unit first backed off. */
		TOptional<int32> RefusalsAtBackOff;
		/** Where the unit stood when the first refusal arrived. */
		TOptional<FVector> RefusedAt;
		TOptional<double> AllStopped;

		int32 GetRefusals() const { return Controller.IsValid() ? Controller->GetOrderRefusalCount() - RefusalsAtStart : 0; }
	};

	void BeginFeedback(FFeedback& Data, ARTSUnit& Unit, ARTSPlayerController& Controller, bool bWall)
	{
		Data.Controller = &Controller;
		Data.RefusalsAtStart = Controller.GetOrderRefusalCount();
		Data.Group.Add(&Unit);
		if (bWall)
		{
			Data.Wall = SpawnSolid(*Unit.GetWorld(), FeedbackWall);
			Data.Spawned.Add(Data.Wall);
		}
	}

	ARTSUnit* SpawnForFeedback(FFeedback& Data, UWorld& World, const TCHAR* Path, int32 TeamId, const FVector& Location, float Yaw)
	{
		ARTSUnit* Unit = SpawnUnit(World, LoadObject<URTSUnitData>(nullptr, Path), TeamId, Location, Yaw);
		Data.Spawned.Add(Unit);
		return Unit;
	}

	void Issue(ARTSPlayerController& Controller, const FFeedback& Data, ERTSCommandType Type, const FVector& Target, bool bQueue = false)
	{
		TArray<ARTSUnit*> Units;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Data.Group)
		{
			Units.Add(Unit.Get());
		}
		FRTSCommand Command;
		Command.Type = Type;
		Command.TargetLocation = Target;
		Command.bQueue = bQueue;
		Controller.Server_IssueCommand(Units, Command);
	}

	/** Follows the drive every frame; true once every unit of the group has stood for FeedbackQuietSeconds. */
	bool TickFeedback(FFeedback& Data, const ARTSUnit& Unit, double Elapsed)
	{
		if (!Data.RefusalsAtBackOff && Unit.GetRealizedMotion().Status == ERTSLocomotionStatus::Reversing)
		{
			Data.RefusalsAtBackOff = Data.GetRefusals();
		}
		if (!Data.RefusedAt && Data.GetRefusals() > 0)
		{
			Data.RefusedAt = Unit.GetActorLocation();
		}
		const bool bMoving = Data.Group.ContainsByPredicate([](const TWeakObjectPtr<ARTSUnit>& Member) { return Member.IsValid() && IsMoving(*Member); });
		if (bMoving || Elapsed < 1.0)
		{
			Data.AllStopped.Reset();
			return false;
		}
		if (!Data.AllStopped)
		{
			Data.AllStopped = Elapsed;
		}
		return Elapsed - *Data.AllStopped >= FeedbackQuietSeconds;
	}

	void EndFeedback(FFeedback& Data)
	{
		for (const TWeakObjectPtr<AActor>& Actor : Data.Spawned)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}
	}
}

/**
 * The player hears of a move a stuck vehicle gives up (#170), through the refusal of #164. An M20 the player sends by move or
 * attack-move into a blockage no path knows about backs off once without a word, then gives the order up: its queue empties
 * and the player gets exactly one refusal, marked where the M20 stands, and no second one after. A queued move after it
 * starts once it gives up. A move whose guidance finds its goal out of reach gives one refusal too. Three M20s sent into it
 * together give one refusal. A blockage that clears while the M20 backs off gives none, and it arrives. An attack on an enemy
 * behind the blockage and a harvest of a source behind it keep their orders and give none; nor does a unit the player did
 * not order.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleBlockedFeedbackTest, "WorldDivide.Navigation.VehicleBlockedFeedback", RTSTests::TestFlags)

bool FRTSVehicleBlockedFeedbackTest::RunTest(const FString& Parameters)
{
	using namespace RTSVehicleMovementTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	TSharedRef<FState> State = MakeShared<FState>();
	auto AddScenario = [&State](const TCHAR* Name, double Timeout) -> FScenario&
	{
		FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
		Scenario.Name = Name;
		Scenario.Start = FeedbackStart;
		Scenario.Yaw = 0.f;
		Scenario.Timeout = Timeout;
		Scenario.bPlayerTeam = true;
		return Scenario;
	};

	// A move, then an attack-move, into the blockage.
	for (const ERTSCommandType Order : { ERTSCommandType::Move, ERTSCommandType::AttackMove })
	{
		const FString Name = Order == ERTSCommandType::Move ? TEXT("A move into the blockage") : TEXT("An attack-move into the blockage");
		FScenario& Scenario = AddScenario(*Name, 40.0);
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		Scenario.Begin = [Data, Order](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, true);
			Issue(Controller, *Data, Order, FeedbackTarget);
		};
		Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed) { return TickFeedback(*Data, Unit, Elapsed); };
		Scenario.End = [Data, Name](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			EndFeedback(*Data);
			Test.TestTrue(*FString::Printf(TEXT("%s: the M20 backs off first"), *Name), Data->RefusalsAtBackOff.IsSet());
			Test.TestEqual(*FString::Printf(TEXT("%s: backing off is no refusal"), *Name), Data->RefusalsAtBackOff.Get(-1), 0);
			Test.TestEqual(*FString::Printf(TEXT("%s: stuck again, it gives the order up"), *Name), Unit.GetQueuedCommandCount(), 0);
			Test.TestTrue(*FString::Printf(TEXT("%s: without arriving (%.0f cm short)"), *Name, FVector::Dist2D(Unit.GetActorLocation(), FeedbackTarget)),
				FVector::Dist2D(Unit.GetActorLocation(), FeedbackTarget) > 1000.f);
			Test.TestEqual(*FString::Printf(TEXT("%s: the player gets one refusal, and no more after"), *Name), Data->GetRefusals(), 1);
			FVector Marked;
			float Age = 0.f;
			if (Data->Controller.IsValid() && Data->RefusedAt && Data->Controller->GetLastOrderRefusal(Marked, Age))
			{
				Test.TestTrue(*FString::Printf(TEXT("%s: marked where the M20 stands (%.0f cm off)"), *Name, FVector::Dist2D(Marked, *Data->RefusedAt)),
					FVector::Dist2D(Marked, *Data->RefusedAt) <= 100.f);
			}
		};
	}

	// A move into the blockage with a second move queued behind it, back west.
	{
		FScenario& Scenario = AddScenario(TEXT("A queued move after the blockage"), 50.0);
		const FVector Back(-8000.f, 20000.f, 0.f);
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		Scenario.Begin = [Data, Back](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, true);
			Issue(Controller, *Data, ERTSCommandType::Move, FeedbackTarget);
			Issue(Controller, *Data, ERTSCommandType::Move, Back, true);
		};
		Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed) { return TickFeedback(*Data, Unit, Elapsed); };
		Scenario.End = [Data, Back](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			EndFeedback(*Data);
			Test.TestEqual(TEXT("A queued move after the blockage: one refusal"), Data->GetRefusals(), 1);
			Test.TestEqual(TEXT("both orders are over"), Unit.GetQueuedCommandCount(), 0);
			Test.TestTrue(*FString::Printf(TEXT("and the queued one was carried out (%.0f cm from its target)"), FVector::Dist2D(Unit.GetActorLocation(), Back)),
				FVector::Dist2D(Unit.GetActorLocation(), Back) <= 100.f);
		};
	}

	// A move whose guidance gives the goal up as out of reach, two seconds in, on open ground.
	{
		FScenario& Scenario = AddScenario(TEXT("A move out of reach"), 30.0);
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		TSharedRef<bool> bFailed = MakeShared<bool>(false);
		Scenario.Begin = [Data](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, false);
			Issue(Controller, *Data, ERTSCommandType::Move, FeedbackTarget);
		};
		Scenario.Tick = [Data, bFailed](ARTSUnit& Unit, double Elapsed)
		{
			if (!*bFailed && Elapsed >= 2.0 && IsMoving(Unit))
			{
				Unit.GetUnitMovement()->FailGuidance();
				*bFailed = true;
			}
			return TickFeedback(*Data, Unit, Elapsed);
		};
		Scenario.End = [Data, bFailed](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			EndFeedback(*Data);
			Test.TestTrue(TEXT("A move out of reach: guidance fails under way"), *bFailed);
			Test.TestEqual(TEXT("the order is given up"), Unit.GetQueuedCommandCount(), 0);
			Test.TestTrue(*FString::Printf(TEXT("short of its target (%.0f cm)"), FVector::Dist2D(Unit.GetActorLocation(), FeedbackTarget)),
				FVector::Dist2D(Unit.GetActorLocation(), FeedbackTarget) > 1000.f);
			Test.TestEqual(TEXT("and the player gets one refusal"), Data->GetRefusals(), 1);
		};
	}

	// Three M20s abreast sent into the blockage by one order.
	{
		FScenario& Scenario = AddScenario(TEXT("A group into the blockage"), 50.0);
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		Scenario.Begin = [Data](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, true);
			for (const float Side : { -700.f, 700.f })
			{
				Data->Group.Add(SpawnForFeedback(*Data, *Unit.GetWorld(), M20Path, Controller.GetTeamId(), FeedbackStart + FVector(0.f, Side, 0.f), 0.f));
			}
			Issue(Controller, *Data, ERTSCommandType::Move, FeedbackTarget);
		};
		Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed) { return TickFeedback(*Data, Unit, Elapsed); };
		Scenario.End = [Data](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			for (int32 Index = 0; Index < Data->Group.Num(); ++Index)
			{
				const ARTSUnit* Member = Data->Group[Index].Get();
				Test.TestTrue(*FString::Printf(TEXT("A group into the blockage: M20 %d gives its move up"), Index + 1),
					Member && Member->GetQueuedCommandCount() == 0 && FVector::Dist2D(Member->GetActorLocation(), FeedbackTarget) > 1000.f);
			}
			EndFeedback(*Data);
			Test.TestEqual(TEXT("A group into the blockage: one refusal for the group"), Data->GetRefusals(), 1);
		};
	}

	// The blockage clears while the M20 backs off from it.
	{
		FScenario& Scenario = AddScenario(TEXT("A blockage that clears"), 40.0);
		const FVector Target(-2000.f, 20000.f, 0.f);
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		Scenario.Begin = [Data, Target](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, true);
			Issue(Controller, *Data, ERTSCommandType::Move, Target);
		};
		Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed)
		{
			const bool bOver = TickFeedback(*Data, Unit, Elapsed);
			if (Data->RefusalsAtBackOff && Data->Wall.IsValid())
			{
				Data->Wall->Destroy();
			}
			return bOver;
		};
		Scenario.End = [Data, Target](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			EndFeedback(*Data);
			Test.TestTrue(TEXT("A blockage that clears: the M20 backs off"), Data->RefusalsAtBackOff.IsSet());
			Test.TestTrue(*FString::Printf(TEXT("then arrives (%.0f cm from its target)"), FVector::Dist2D(Unit.GetActorLocation(), Target)),
				FVector::Dist2D(Unit.GetActorLocation(), Target) <= 100.f);
			Test.TestEqual(TEXT("its order is over"), Unit.GetQueuedCommandCount(), 0);
			Test.TestEqual(TEXT("and the player gets no refusal"), Data->GetRefusals(), 0);
		};
	}

	// An attack on an enemy M20 behind the blockage, out of range of it.
	{
		FScenario& Scenario = AddScenario(TEXT("An attack across the blockage"), KeptOrderSeconds + 5.0);
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		Scenario.Begin = [Data](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, true);
			FRTSCommand Attack;
			Attack.Type = ERTSCommandType::Attack;
			Attack.TargetActor = SpawnForFeedback(*Data, *Unit.GetWorld(), M20Path, DriveTeam, FeedbackStart + FVector(10000.f, 0.f, 0.f), 180.f);
			Controller.Server_IssueCommand({ &Unit }, Attack);
		};
		Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed)
		{
			TickFeedback(*Data, Unit, Elapsed);
			return Elapsed >= KeptOrderSeconds;
		};
		Scenario.End = [Data](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			EndFeedback(*Data);
			Test.TestTrue(*FString::Printf(TEXT("An attack across the blockage: the M20 stays on its side (x %.0f)"), Unit.GetActorLocation().X),
				Unit.GetActorLocation().X < FeedbackWall.Min.X);
			Test.TestEqual(TEXT("keeps its attack"), Unit.GetQueuedCommandCount(), 1);
			Test.TestEqual(TEXT("and the player gets no refusal"), Data->GetRefusals(), 0);
		};
	}

	// A harvest of a source behind the blockage.
	{
		FScenario& Scenario = AddScenario(TEXT("A harvest across the blockage"), KeptOrderSeconds + 5.0);
		Scenario.DataPath = HarvesterPath;
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		Scenario.Begin = [Data](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, true);
			const FTransform Transform(FeedbackStart + FVector(6000.f, 0.f, 0.f));
			ARTSSupplySource* Source = Unit.GetWorld()->SpawnActorDeferred<ARTSSupplySource>(ARTSSupplySource::StaticClass(), Transform);
			Source->Amount = 5000;
			Source->FinishSpawning(Transform);
			Data->Spawned.Add(Source);
			FRTSCommand Harvest;
			Harvest.Type = ERTSCommandType::Harvest;
			Harvest.TargetActor = Source;
			Controller.Server_IssueCommand({ &Unit }, Harvest);
		};
		Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed)
		{
			TickFeedback(*Data, Unit, Elapsed);
			return Elapsed >= KeptOrderSeconds;
		};
		Scenario.End = [Data](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			EndFeedback(*Data);
			Test.TestTrue(*FString::Printf(TEXT("A harvest across the blockage: the harvester stays on its side (x %.0f)"), Unit.GetActorLocation().X),
				Unit.GetActorLocation().X < FeedbackWall.Min.X);
			Test.TestEqual(TEXT("keeps its harvest"), Unit.GetQueuedCommandCount(), 1);
			Test.TestEqual(TEXT("and the player gets no refusal"), Data->GetRefusals(), 0);
		};
	}

	// The enemy's M20, ordered on the server with no player behind the order.
	{
		FScenario& Scenario = AddScenario(TEXT("An order no player gave"), 40.0);
		Scenario.bPlayerTeam = false;
		TSharedRef<FFeedback> Data = MakeShared<FFeedback>();
		Scenario.Begin = [Data](ARTSUnit& Unit, ARTSPlayerController& Controller)
		{
			BeginFeedback(*Data, Unit, Controller, true);
			IssueMove(Controller, Unit, FeedbackTarget);
		};
		Scenario.Tick = [Data](ARTSUnit& Unit, double Elapsed) { return TickFeedback(*Data, Unit, Elapsed); };
		Scenario.End = [Data](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			EndFeedback(*Data);
			Test.TestEqual(TEXT("An order no player gave: the enemy's M20 gives its move up"), Unit.GetQueuedCommandCount(), 0);
			Test.TestEqual(TEXT("and the player hears nothing of it"), Data->GetRefusals(), 0);
		};
	}

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleWaitForPlay(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleRunScenarios(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace RTSVehicleMovementTests
{
	const TCHAR* AbramsPath = TEXT("/Game/Data/Units/DA_TestAbrams.DA_TestAbrams");
	const TCHAR* TruckPath = TEXT("/Game/Data/Units/DA_TestFlakTruck.DA_TestFlakTruck");

	/** Open, flat ground west of the middle, where the drives at solids the navmesh leaves out (#168) take place. */
	const FVector OpenGround(-6000.f, 20000.f, 0.f);

	/** #168: a hull stops within this (cm) of such a solid. */
	constexpr float SolidStopGap = 50.f;

	/** A wall across the way, this far ahead of the drive's start, this wide and this high; and the thin one, as thick. */
	constexpr float WallAhead = 2500.f;
	constexpr float WallHalfWidth = 1500.f;
	constexpr float WallThickness = 100.f;
	constexpr float WallHeight = 300.f;
	constexpr float ThinWallThickness = 10.f;

	/** What a drive at a solid saw: its closest gap to the solids, and the gear once blocked. */
	struct FSolidDrive
	{
		TArray<TWeakObjectPtr<AActor>> Solids;
		TArray<FBox2D> Boxes;
		TArray<TWeakObjectPtr<ARTSUnit>> Others;
		float MinGap = FLT_MAX;
		TOptional<double> BlockedAt;
		double BlockedTravel = 0.0;
		float AfterBlocked = 0.f;
		int32 SignChanges = 0;
		float LastSign = 0.f;
		bool bDriving = false;
	};

	void AddSolid(UWorld& World, FSolidDrive& Drive, const FBox& Box)
	{
		Drive.Solids.Add(SpawnSolid(World, Box));
		Drive.Boxes.Add(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)));
	}

	void Clear(FSolidDrive& Drive)
	{
		for (const TWeakObjectPtr<AActor>& Solid : Drive.Solids)
		{
			if (Solid.IsValid())
			{
				Solid->Destroy();
			}
		}
		for (const TWeakObjectPtr<ARTSUnit>& Other : Drive.Others)
		{
			if (Other.IsValid())
			{
				Other->Destroy();
			}
		}
		Drive.Solids.Reset();
		Drive.Others.Reset();
	}

	/** Adds Unit's closest gap to the drive's solids and what its gear did this frame. */
	void Watch(FSolidDrive& Drive, const ARTSUnit& Unit, double Elapsed)
	{
		for (const FBox2D& Box : Drive.Boxes)
		{
			Drive.MinGap = FMath::Min(Drive.MinGap, RTSTests::GetHullGap(Unit.GetFootprint(), Box));
		}
		const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
		if (Drive.BlockedAt)
		{
			Drive.AfterBlocked = FMath::Max(Drive.AfterBlocked, static_cast<float>(FMath::Abs(Motion.DrivenDistance - Drive.BlockedTravel)));
			const float Sign = FMath::Abs(Motion.ForwardSpeed) > 1.f ? FMath::Sign(Motion.ForwardSpeed) : 0.f;
			if (Sign != 0.f && Drive.LastSign != 0.f && Sign != Drive.LastSign)
			{
				++Drive.SignChanges;
			}
			Drive.LastSign = Sign != 0.f ? Sign : Drive.LastSign;
		}
		else if (Motion.Status == ERTSLocomotionStatus::Blocked)
		{
			Drive.BlockedAt = Elapsed;
			Drive.BlockedTravel = Motion.DrivenDistance;
		}
	}

	float GetGap(const FSolidDrive& Drive, const ARTSUnit& Unit)
	{
		float Gap = FLT_MAX;
		for (const FBox2D& Box : Drive.Boxes)
		{
			Gap = FMath::Min(Gap, RTSTests::GetHullGap(Unit.GetFootprint(), Box));
		}
		return Gap;
	}

	void DriveFlatOut(ARTSUnit& Unit)
	{
		RTSLocomotion::FCommand Command;
		Command.Speed = 1.e5f;
		Unit.GetUnitMovement()->SetCommandOverride(Command);
	}

	/**
	 * Flat out from Start, facing Yaw, at Wall, which navigation leaves out: the gear stops the hull at its face, no more
	 * than 1 cm in and no further than 50 cm short, on the side it came from, and drives no further once blocked. A turret
	 * is turned round first: a gun over the front would stop the hull at the barrel's reach (#162), not at its own face.
	 */
	void AddWallDrive(const TSharedRef<FState>& State, const FString& Name, const TCHAR* DataPath, const FVector& Start, float Yaw, const FBox& Wall)
	{
		FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
		Scenario.Name = Name;
		Scenario.DataPath = DataPath;
		Scenario.Start = Start;
		Scenario.Yaw = Yaw;
		Scenario.Timeout = 35.0;
		TSharedRef<FSolidDrive> Drive = MakeShared<FSolidDrive>();
		Scenario.Begin = [Drive, Wall](ARTSUnit& Unit, ARTSPlayerController&)
		{
			AddSolid(*Unit.GetWorld(), *Drive, Wall);
			Unit.SetAimOverride(FVector2D(180.f, 0.f));
		};
		Scenario.Tick = [Drive](ARTSUnit& Unit, double Elapsed)
		{
			if (!Drive->bDriving)
			{
				Drive->bDriving = !Unit.GetShape().Barrel || FMath::Abs(FMath::FindDeltaAngleDegrees(Unit.GetTurretYaw(), 180.f)) < 1.f;
				if (Drive->bDriving)
				{
					DriveFlatOut(Unit);
				}
				return false;
			}
			Watch(*Drive, Unit, Elapsed);
			return Drive->BlockedAt && Elapsed - *Drive->BlockedAt > 2.0;
		};
		Scenario.End = [Drive, Name, Start, Wall](ARTSUnit& Unit, FAutomationTestBase& Test)
		{
			const float Gap = GetGap(*Drive, Unit);
			const FVector2D Toward = FVector2D(Wall.GetCenter() - Start);
			Test.AddInfo(FString::Printf(TEXT("%s: stopped %.2f cm short, closest %.2f cm"), *Name, Gap, Drive->MinGap));
			Test.TestTrue(*FString::Printf(TEXT("%s: the gear reports itself blocked"), *Name), Drive->BlockedAt.IsSet());
			Test.TestTrue(*FString::Printf(TEXT("%s: the hull never goes more than %.0f cm into the wall (closest %.2f cm)"), *Name, PenetrationTolerance,
				Drive->MinGap), Drive->MinGap >= -PenetrationTolerance);
			Test.TestTrue(*FString::Printf(TEXT("%s: it stays on its side, not through"), *Name),
				FVector2D::DotProduct(FVector2D(Unit.GetActorLocation() - Wall.GetCenter()), Toward) < 0.f);
			Test.TestTrue(*FString::Printf(TEXT("%s: it stops within %.0f cm of the face (%.1f cm)"), *Name, SolidStopGap, Gap),
				Gap >= -PenetrationTolerance && Gap <= SolidStopGap);
			Test.TestTrue(*FString::Printf(TEXT("%s: and drives no further once blocked (%.3f cm)"), *Name, Drive->AfterBlocked),
				Drive->AfterBlocked <= BlockedTravel);
			Test.TestTrue(*FString::Printf(TEXT("%s: without rocking back and forth (%d changes of direction)"), *Name, Drive->SignChanges),
				Drive->SignChanges <= 2);
			Clear(*Drive);
		};
	}

	/** A wall across the way at X, of Thickness, centred on Y. */
	FBox WallAcross(float X, float Y, float Thickness)
	{
		return FBox(FVector(X, Y - WallHalfWidth, 0.f), FVector(X + Thickness, Y + WallHalfWidth, WallHeight));
	}

	/** Every drive of the off-navmesh test, at the rate the test runs with. */
	void AddOffNavmeshScenarios(const TSharedRef<FState>& State, const UWorld& EditorWorld, FAutomationTestBase& Test)
	{
		const FBox2D Block = RTSTests::GetLabelledBox(EditorWorld, TEXT("Block_North"));
		const float WallX = OpenGround.X + WallAhead;
		const FBox Wall = WallAcross(WallX, OpenGround.Y, WallThickness);

		// The neighbourhood itself: what it finds, what it leaves to the navmesh, when it looks again, and the hull's box on a slope.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Solid neighbourhood");
			Scenario.DataPath = AbramsPath;
			Scenario.Start = OpenGround;
			Scenario.Timeout = 5.0;
			TSharedRef<FSolidDrive> Drive = MakeShared<FSolidDrive>();
			Scenario.Begin = [Drive, Block, &Test](ARTSUnit& Unit, ARTSPlayerController&)
			{
				UWorld& World = *Unit.GetWorld();
				const double Now = World.GetTimeSeconds();
				const FRTSUnitShape& Shape = Unit.GetShape();
				const float Height = Shape.Hull.GetSize().Z;
				const FRTSFootprint Here = Unit.GetFootprint();
				const float GroundZ = Here.Center.Z - Unit.GetGroundOffset();
				auto Look = [&](const FRTSFootprint& Footprint)
				{
					FRTSSolidNeighbourhood Neighbourhood;
					Neighbourhood.Update(World, Footprint, Footprint.Center.Z - Unit.GetGroundOffset(), Height, 200.f, Now, 1.0);
					return Neighbourhood;
				};
				Test.TestEqual(TEXT("On open ground the neighbourhood holds no solid"), Look(Here).Solids.Num(), 0);

				// Beside Block_North, which cuts the navmesh: the navmesh's to keep the hull out of, not the collision's.
				FRTSFootprint ByBlock = Here;
				ByBlock.Center = FVector(Block.GetCenter().X, Block.Max.Y + Here.Radius + 100.f, Here.Center.Z);
				Test.TestEqual(TEXT("Beside a block the navmesh knows, it holds no solid"), Look(ByBlock).Solids.Num(), 0);

				// A prop navigation leaves out, 1 m ahead of the hull.
				const float Front = Here.Center.X + Here.HalfSegment + Here.Radius;
				AddSolid(World, *Drive, FBox(FVector(Front + 100.f, Here.Center.Y - 200.f, GroundZ), FVector(Front + 300.f, Here.Center.Y + 200.f, GroundZ + 300.f)));
				const FRTSSolidNeighbourhood Neighbourhood = Look(Here);
				Test.TestEqual(TEXT("A prop navigation leaves out is in it"), Neighbourhood.Solids.Num(), 1);
				Test.TestTrue(TEXT("Where it was made it is current"), Neighbourhood.IsCurrent(Here, Now));
				FRTSFootprint Moved = Here;
				Moved.Center.X += 190.f;
				Test.TestTrue(TEXT("and within its margin"), Neighbourhood.IsCurrent(Moved, Now));
				Moved.Center.X += 20.f;
				Test.TestFalse(TEXT("but not beyond it"), Neighbourhood.IsCurrent(Moved, Now));
				Test.TestFalse(TEXT("nor once it expired"), Neighbourhood.IsCurrent(Here, Now + 1.5));

				const FVector Up = FVector::UpVector;
				auto BoxAt = [&](float Ahead)
				{
					FRTSFootprint At = Here;
					At.Center.X += Ahead;
					return RTSHullClearance::MakeHullBox(At, GroundZ, Up, 35.f, Height, 0.f);
				};
				Test.TestTrue(TEXT("The hull where it stands is clear of the prop"), Neighbourhood.IsClear(BoxAt(0.f), BoxAt(0.f), 0.5f));
				Test.TestTrue(TEXT("and 99 cm on"), Neighbourhood.IsClear(BoxAt(99.f), BoxAt(0.f), 0.5f));
				Test.TestFalse(TEXT("but not 102 cm on, 2 cm into it"), Neighbourhood.IsClear(BoxAt(102.f), BoxAt(0.f), 0.5f));
				Test.TestFalse(TEXT("Inside it, deeper is not clear"), Neighbourhood.IsClear(BoxAt(150.f), BoxAt(120.f), 0.5f));
				Test.TestTrue(TEXT("but back out is"), Neighbourhood.IsClear(BoxAt(110.f), BoxAt(120.f), 0.5f));

				// Up the ridge's ramp: the box follows the slope and starts a step above it, so the ramp is no wall.
				TOptional<FVector> Ramp;
				FVector Normal = Up;
				for (float X = 11300.f; X < 16200.f && !Ramp; X += 50.f)
				{
					float Z = 0.f;
					FVector Back = Up, Middle = Up, Ahead = Up;
					float Ignored = 0.f;
					if (RTSGround::Trace(World, FVector(X, 3000.f, 0.f), 1000.f, 1000.f, Z, &Middle) && Middle.Z < 0.99f
						&& RTSGround::Trace(World, FVector(X - Here.HalfSegment - Here.Radius, 3000.f, 0.f), 1000.f, 1000.f, Ignored, &Back)
						&& RTSGround::Trace(World, FVector(X + Here.HalfSegment + Here.Radius, 3000.f, 0.f), 1000.f, 1000.f, Ignored, &Ahead)
						&& Back.Equals(Middle, 0.01f) && Ahead.Equals(Middle, 0.01f))
					{
						Ramp = FVector(X, 3000.f, Z);
						Normal = Middle;
					}
				}
				if (Test.TestTrue(TEXT("The ridge has a ramp as long as the hull"), Ramp.IsSet()))
				{
					FRTSFootprint OnRamp = Here;
					OnRamp.Center = *Ramp + FVector(0.f, 0.f, Unit.GetGroundOffset());
					OnRamp.Axis = FVector::ForwardVector;
					Test.TestTrue(TEXT("On it the hull's box, tilted to the ground, overlaps nothing"),
						RTSHullClearance::IsSolidClear(World, OnRamp, Ramp->Z, Height, 0.f, Normal));
					Test.TestFalse(TEXT("where a level box would cut into the ramp"), RTSHullClearance::IsSolidClear(World, OnRamp, Ramp->Z, Height, 0.f, Up));
					Test.TestEqual(TEXT("and the ramp is no solid of the neighbourhood"), Look(OnRamp).Solids.Num(), 0);
				}
				Clear(*Drive);
			};
			Scenario.Tick = [](ARTSUnit&, double) { return true; };
			Scenario.End = [](ARTSUnit&, FAutomationTestBase&) {};
		}

		// At a wall from in front, at a slant and from behind; the placeholder tank, the long Abrams and the wheeled truck.
		AddWallDrive(State, TEXT("M20 flat out at a wall left out of navigation"), M20Path, OpenGround, 0.f, Wall);
		AddWallDrive(State, TEXT("M20 at the wall at 30 degrees"), M20Path, OpenGround - FVector(0.f, 1000.f, 0.f), 30.f, Wall);
		AddWallDrive(State, TEXT("M20 at the wall from the far side"), M20Path, FVector(WallX + WallThickness + WallAhead, OpenGround.Y, 0.f), 180.f, Wall);
		AddWallDrive(State, TEXT("Placeholder tank at the wall"), TankPath, OpenGround, 0.f, Wall);
		AddWallDrive(State, TEXT("Abrams at the wall"), AbramsPath, OpenGround, 0.f, Wall);
		AddWallDrive(State, TEXT("Abrams at the wall at -30 degrees"), AbramsPath, OpenGround + FVector(0.f, 1000.f, 0.f), -30.f, Wall);
		AddWallDrive(State, TEXT("Flak truck at the wall"), TruckPath, OpenGround, 0.f, Wall);

		// A 10 cm wall at top speed, from far enough to reach it.
		const FBox Thin = WallAcross(OpenGround.X + 2.f * WallAhead, OpenGround.Y, ThinWallThickness);
		AddWallDrive(State, TEXT("M20 at a thin wall at top speed"), M20Path, OpenGround, 0.f, Thin);
		AddWallDrive(State, TEXT("Flak truck at a thin wall at top speed"), TruckPath, OpenGround, 0.f, Thin);

		// Pivoting beside a wall 15 cm off its side: it turns until a corner meets the wall, and no further in.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Pivot beside a wall");
			Scenario.Start = OpenGround;
			Scenario.Timeout = 10.0;
			TSharedRef<FSolidDrive> Drive = MakeShared<FSolidDrive>();
			TSharedRef<float> MaxTurn = MakeShared<float>(0.f);
			TSharedRef<float> Turned = MakeShared<float>(0.f);
			Scenario.Begin = [Drive](ARTSUnit& Unit, ARTSPlayerController&)
			{
				const FRTSFootprint Footprint = Unit.GetFootprint();
				const float Side = Footprint.Center.Y + Footprint.Radius + 15.f;
				AddSolid(*Unit.GetWorld(), *Drive, FBox(FVector(Footprint.Center.X - 1000.f, Side, 0.f), FVector(Footprint.Center.X + 1000.f, Side + WallThickness, WallHeight)));
				RTSLocomotion::FCommand Command;
				Command.bPivot = true;
				Command.PivotYawRate = Unit.GetUnitMovement()->GetLimits().MaxYawRate;
				Unit.GetUnitMovement()->SetCommandOverride(Command);
			};
			Scenario.Tick = [Drive, MaxTurn, Turned](ARTSUnit& Unit, double Elapsed)
			{
				Watch(*Drive, Unit, Elapsed);
				*Turned = FMath::Abs(FMath::FindDeltaAngleDegrees(0.f, Unit.GetActorRotation().Yaw));
				*MaxTurn = FMath::Max(*MaxTurn, *Turned);
				return Elapsed > 4.0;
			};
			Scenario.End = [Drive, MaxTurn, Turned](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(*FString::Printf(TEXT("Pivot beside a wall: it turns (%.2f degrees)"), *MaxTurn), *MaxTurn > 0.5f);
				Test.TestTrue(*FString::Printf(TEXT("until a corner meets the wall, short of a quarter turn (%.2f degrees)"), *MaxTurn), *MaxTurn < 45.f);
				Test.TestTrue(*FString::Printf(TEXT("no more than %.0f cm into it (closest %.2f cm)"), PenetrationTolerance, Drive->MinGap),
					Drive->MinGap >= -PenetrationTolerance);
				Test.TestTrue(*FString::Printf(TEXT("and holds there (%.2f of %.2f degrees)"), *Turned, *MaxTurn), *MaxTurn - *Turned < 0.5f);
				Clear(*Drive);
			};
		}

		// Along a wall 30 cm off its side, to a point past its far end.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("Along a wall");
			Scenario.Start = OpenGround;
			Scenario.Timeout = 30.0;
			const FVector Target = OpenGround + FVector(3500.f, 0.f, 0.f);
			TSharedRef<FSolidDrive> Drive = MakeShared<FSolidDrive>();
			Scenario.Begin = [Drive, Target](ARTSUnit& Unit, ARTSPlayerController& Controller)
			{
				const FRTSFootprint Footprint = Unit.GetFootprint();
				const float Side = Footprint.Center.Y + Footprint.Radius + 30.f;
				AddSolid(*Unit.GetWorld(), *Drive, FBox(FVector(Footprint.Center.X - 500.f, Side, 0.f), FVector(Footprint.Center.X + 3000.f, Side + WallThickness, WallHeight)));
				IssueMove(Controller, Unit, Target);
			};
			Scenario.Tick = [Drive](ARTSUnit& Unit, double Elapsed)
			{
				Watch(*Drive, Unit, Elapsed);
				return Elapsed > 1.0 && Unit.GetQueuedCommandCount() == 0;
			};
			Scenario.End = [Drive, Target](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(*FString::Printf(TEXT("Along a wall: never more than %.0f cm into it (closest %.2f cm)"), PenetrationTolerance, Drive->MinGap),
					Drive->MinGap >= -PenetrationTolerance);
				Test.TestTrue(*FString::Printf(TEXT("and arrives (%.0f cm from the target)"), FVector::Dist2D(Unit.GetActorLocation(), Target)),
					FVector::Dist2D(Unit.GetActorLocation(), Target) <= 100.f);
				Clear(*Drive);
			};
		}

		// Six M20s ordered past a pillar that navigation leaves out, in their way: pushing each other, none goes into it.
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = TEXT("A group past a pillar");
			Scenario.Start = OpenGround;
			Scenario.Timeout = 60.0;
			const FVector Target = OpenGround + FVector(5000.f, 0.f, 0.f);
			TSharedRef<FSolidDrive> Drive = MakeShared<FSolidDrive>();
			Scenario.Begin = [Drive, Target](ARTSUnit& Unit, ARTSPlayerController& Controller)
			{
				UWorld& World = *Unit.GetWorld();
				const FVector Pillar = OpenGround + FVector(2500.f, 0.f, 0.f);
				AddSolid(World, *Drive, FBox(Pillar + FVector(-150.f, -150.f, 0.f), Pillar + FVector(150.f, 150.f, WallHeight)));
				IssueMove(Controller, Unit, Target);
				for (int32 Index = 1; Index < 6; ++Index)
				{
					const FVector Offset((Index / 3) * -1000.f, (Index % 3 - 1) * 600.f, 0.f);
					ARTSUnit* Other = SpawnUnit(World, Unit.GetUnitData(), Unit.GetTeamId(), OpenGround + Offset, 0.f);
					Drive->Others.Add(Other);
					IssueMove(Controller, *Other, Target + Offset);
				}
			};
			Scenario.Tick = [Drive](ARTSUnit& Unit, double Elapsed)
			{
				Watch(*Drive, Unit, Elapsed);
				bool bMoving = Unit.GetQueuedCommandCount() > 0;
				for (const TWeakObjectPtr<ARTSUnit>& Other : Drive->Others)
				{
					if (Other.IsValid())
					{
						for (const FBox2D& Box : Drive->Boxes)
						{
							Drive->MinGap = FMath::Min(Drive->MinGap, RTSTests::GetHullGap(Other->GetFootprint(), Box));
						}
						bMoving |= Other->GetQueuedCommandCount() > 0;
					}
				}
				return Elapsed > 1.0 && !bMoving;
			};
			Scenario.End = [Drive](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(*FString::Printf(TEXT("A group past a pillar: no hull more than %.0f cm into it (closest %.2f cm)"), PenetrationTolerance,
					Drive->MinGap), Drive->MinGap >= -PenetrationTolerance);
				Clear(*Drive);
			};
		}

		// The long hull and the wheels over the ridge: slopes and its crest are ground, not walls.
		for (const TCHAR* DataPath : { AbramsPath, TruckPath })
		{
			FScenario& Scenario = State->Scenarios.AddDefaulted_GetRef();
			Scenario.Name = FString::Printf(TEXT("Over the ridge (%s)"), DataPath == AbramsPath ? TEXT("Abrams") : TEXT("flak truck"));
			Scenario.DataPath = DataPath;
			Scenario.Start = FVector(11300.f, 3000.f, 0.f);
			Scenario.Timeout = 50.0;
			const FVector Target(16200.f, 3000.f, 0.f);
			TSharedRef<float> Highest = MakeShared<float>(-FLT_MAX);
			const FString Name = Scenario.Name;
			Scenario.Begin = [Target](ARTSUnit& Unit, ARTSPlayerController& Controller) { IssueMove(Controller, Unit, Target); };
			Scenario.Tick = [Highest](ARTSUnit& Unit, double Elapsed)
			{
				*Highest = FMath::Max(*Highest, static_cast<float>(Unit.GetNavAgentLocation().Z));
				return Elapsed > 1.0 && Unit.GetQueuedCommandCount() == 0;
			};
			Scenario.End = [Highest, Target, Name](ARTSUnit& Unit, FAutomationTestBase& Test)
			{
				Test.TestTrue(*FString::Printf(TEXT("%s: it climbs to the top (%.0f cm up)"), *Name, *Highest), *Highest > 250.f);
				Test.TestTrue(*FString::Printf(TEXT("%s: and arrives on the far side (%.0f cm from the target)"), *Name, FVector::Dist2D(Unit.GetActorLocation(), Target)),
					FVector::Dist2D(Unit.GetActorLocation(), Target) <= 100.f);
			};
		}
	}
}

/**
 * Solids that navigation leaves out (#168), on L_TestMap at 30, 60 and 120 game updates a second: what the hull's
 * neighbourhood finds and leaves to the navmesh, and its box on a slope; the M20, the placeholder tank, the long Abrams and
 * the wheeled flak truck driven flat out at such a wall, from in front, at a slant and from behind, and at a thin one at top
 * speed, stop 0-50 cm short of it and no more than 1 cm in, without rocking; an M20 pivots beside one and drives along one
 * without going in; a group ordered past such a pillar keeps out of it; the Abrams and the truck still cross the ridge.
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSVehicleOffNavmeshTest, "WorldDivide.Navigation.VehicleOffNavmesh", RTSTests::TestFlags)

void FRTSVehicleOffNavmeshTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Rate : { TEXT("30"), TEXT("60"), TEXT("120") })
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("%sHz"), Rate));
		OutTestCommands.Add(Rate);
	}
}

bool FRTSVehicleOffNavmeshTest::RunTest(const FString& Parameters)
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
	AddOffNavmeshScenarios(State, *EditorWorld, *this);
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleWaitForPlay(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleRunScenarios(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
