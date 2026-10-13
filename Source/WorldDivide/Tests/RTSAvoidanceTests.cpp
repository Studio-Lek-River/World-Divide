#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuilding.h"
#include "Commands/RTSCommand.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestPIE.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"

/**
 * Local avoidance on L_TestMap, in the empty strip north of the bases. The crowd test sends 50 tanks and infantry, each on
 * its own, to one point and checks that they end apart. The gap test walls the strip off with power plants but for a gap
 * one cell wide and checks that a squad of infantry goes through it. The frame-time test walks 200 units across the strip
 * with avoidance on and off and reports what it costs.
 */
namespace RTSAvoidanceTests
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");
	const TCHAR* PowerPlantPath = TEXT("/Game/Data/Buildings/DA_TestPowerPlant.DA_TestPowerPlant");
	constexpr double NavTimeout = 30.0;

	/** The crowd test: 50 units in a block west of the strip's centre, sent to one point east of it. */
	constexpr int32 CrowdCount = 50;
	constexpr int32 CrowdColumns = 10;
	const FVector CrowdOrigin(-4500.f, 6500.f, 0.f);
	constexpr float CrowdSpacing = 250.f;
	const FVector CrowdTarget(1500.f, 7000.f, 0.f);
	/** Pairs may end no closer than this share of the sum of their radii. */
	constexpr float CrowdMinSeparation = 0.9f;
	/** Game seconds the crowd stands once every order is done, before it is measured. */
	constexpr double CrowdSettleSeconds = 1.0;
	constexpr double CrowdTimeout = 60.0;

	/**
	 * The gap test: 4x4 power plants stand along x = 0 from y = 5300 to 8700, but for a gap from 6900 to 7100, one grid cell
	 * wider than a navmesh agent on each side. Going around either end is half as long again as going through.
	 */
	const float GapPlantYs[] = { 5500.f, 5900.f, 6300.f, 6700.f, 7300.f, 7700.f, 8100.f, 8500.f };
	constexpr float GapCenterY = 7000.f;
	constexpr float GapHalfWidth = 100.f;
	constexpr int32 GapSquadCount = 12;
	constexpr int32 GapSquadColumns = 3;
	const FVector GapSquadOrigin(-2000.f, 6600.f, 0.f);
	constexpr float GapSquadSpacing = 200.f;
	const FVector GapTarget(1500.f, 7000.f, 0.f);
	constexpr float GapArrivalRadius = 600.f;
	constexpr double GapTimeout = 60.0;

	/** The frame-time test: 200 units, half tanks and half infantry, walk across the strip and back. */
	constexpr int32 FrameTimeCount = 200;
	constexpr int32 FrameTimeColumns = 20;
	const FVector FrameTimeOrigin(-5000.f, 6000.f, 0.f);
	constexpr float FrameTimeSpacing = 220.f;
	const FVector FrameTimeEast(3000.f, 7000.f, 0.f);
	const FVector FrameTimeWest(-3000.f, 7000.f, 0.f);
	/** Real seconds sampled in each half. */
	constexpr double FrameTimeSampleSeconds = 4.0;
	/** Mean time of the avoidance pass over 200 units that would start to show in a 60 fps frame. */
	constexpr double FrameTimeBudgetMs = 1.0;

	struct FAvoidanceState
	{
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		double PhaseStart = 0.0;
		double SettleStart = -1.0;

		// Gap test only: where each unit crossed x = 0, or unset.
		TArray<FVector> LastLocations;
		TArray<TOptional<float>> CrossingYs;

		// Frame-time test only.
		double LastFrame = 0.0;
		int32 Frames = 0;
		double FrameSeconds = 0.0;
		int32 Passes = 0;
		double PassSeconds = 0.0;
		double MaxPassSeconds = 0.0;
		double MeanFrameMsOn = 0.0;
	};

	ARTSPlayerController* GetController(FAutomationTestBase& Test)
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller)
		{
			Test.AddError(TEXT("No ARTSPlayerController in the PIE world"));
		}
		return Controller;
	}

	ARTSUnit* SpawnUnit(UWorld* World, URTSUnitData* Data, int32 TeamId, const FVector& Location)
	{
		FTransform Transform(Location);
		ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(
			ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	/** Count units of the player's team in a block of Columns columns from Origin, tanks and infantry taking turns. */
	bool SpawnBlock(FAutomationTestBase& Test, FAvoidanceState& State, int32 Count, int32 Columns, const FVector& Origin,
		float Spacing, bool bTanks)
	{
		ARTSPlayerController* Controller = GetController(Test);
		URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
		URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
		if (!Controller || !Tank || !Infantry)
		{
			Test.AddError(TEXT("No controller or unit data"));
			return false;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector Location = Origin + FVector((Index % Columns) * Spacing, (Index / Columns) * Spacing, 0.f);
			URTSUnitData* Data = bTanks && Index % 2 == 0 ? Tank : Infantry;
			State.Units.Add(SpawnUnit(Controller->GetWorld(), Data, Controller->GetTeamId(), Location));
		}
		return true;
	}

	FRTSCommand MakeMove(const FVector& Target)
	{
		FRTSCommand Command;
		Command.Type = ERTSCommandType::Move;
		Command.TargetLocation = Target;
		return Command;
	}

	TArray<ARTSUnit*> GetLiveUnits(const FAvoidanceState& State)
	{
		TArray<ARTSUnit*> Units;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : State.Units)
		{
			if (Unit.IsValid())
			{
				Units.Add(Unit.Get());
			}
		}
		return Units;
	}

	bool AllOrdersDone(const FAvoidanceState& State)
	{
		for (const TWeakObjectPtr<ARTSUnit>& Unit : State.Units)
		{
			if (Unit.IsValid() && Unit->GetQueuedCommandCount() > 0)
			{
				return false;
			}
		}
		return true;
	}

	IConsoleVariable* GetAvoidanceVariable()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Avoidance"));
	}

	/** Turns avoidance back on once the test ends, however it ends. Add it as the test's last latent command. */
	class FRTSAvoidanceRestore : public IAutomationLatentCommand
	{
	public:
		virtual ~FRTSAvoidanceRestore() override
		{
			GetAvoidanceVariable()->Set(true, ECVF_SetByConsole);
		}

		virtual bool Update() override { return true; }
	};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidanceWaitForNavigation, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidanceWaitForNavigation::Update()
{
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}

	// Wait for the session FRTSStartStandalonePIE asked for, with the player's team and credits handed out.
	UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	const ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const bool bPlaying = World && World->HasBegunPlay() && Controller && Controller->GetTeamId() != INDEX_NONE
		&& Controller->GetFaction() && Controller->GetCredits() > 0;
	if (bPlaying && RTSTests::IsNavigationReady(*World))
	{
		RTSTests::ApplyTestTimeDilation(*World);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("PIE did not start or the navmesh was not built in time"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidanceIssueCrowd, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidanceIssueCrowd::Update()
{
	if (!SpawnBlock(*Test, *State, CrowdCount, CrowdColumns, CrowdOrigin, CrowdSpacing, /*bTanks*/ true))
	{
		return true;
	}
	// One order per unit, so no formation spreads them: avoidance alone keeps them apart at the target.
	ARTSPlayerController* Controller = GetController(*Test);
	for (ARTSUnit* Unit : GetLiveUnits(*State))
	{
		Controller->Server_IssueCommand({ Unit }, MakeMove(CrowdTarget));
	}
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidanceVerifyCrowd, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidanceVerifyCrowd::Update()
{
	const TArray<ARTSUnit*> Units = GetLiveUnits(*State);
	UWorld* World = Units.IsEmpty() ? nullptr : Units[0]->GetWorld();
	if (!World)
	{
		Test->AddError(TEXT("The crowd is gone"));
		return true;
	}

	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (State->SettleStart < 0.0)
	{
		if (AllOrdersDone(*State))
		{
			State->SettleStart = World->GetTimeSeconds();
		}
		else if (Elapsed < CrowdTimeout)
		{
			return false;
		}
		else
		{
			Test->AddError(FString::Printf(TEXT("The crowd still had orders after %.0f s"), CrowdTimeout));
			return true;
		}
	}
	if (World->GetTimeSeconds() - State->SettleStart < CrowdSettleSeconds)
	{
		return false;
	}

	Test->TestEqual(TEXT("Every unit of the crowd is alive"), Units.Num(), CrowdCount);
	float WorstRatio = TNumericLimits<float>::Max();
	float FarthestFromTarget = 0.f;
	int32 TooClose = 0;
	for (int32 A = 0; A < Units.Num(); ++A)
	{
		FarthestFromTarget = FMath::Max(FarthestFromTarget, FVector::Dist2D(Units[A]->GetActorLocation(), CrowdTarget));
		for (int32 B = A + 1; B < Units.Num(); ++B)
		{
			const float Ratio = FVector::Dist2D(Units[A]->GetActorLocation(), Units[B]->GetActorLocation())
				/ (Units[A]->GetSelectionRadius() + Units[B]->GetSelectionRadius());
			WorstRatio = FMath::Min(WorstRatio, Ratio);
			if (Ratio < CrowdMinSeparation)
			{
				++TooClose;
				Test->AddInfo(FString::Printf(TEXT("%s and %s at %.2f of their radii"), *Units[A]->GetName(),
					*Units[B]->GetName(), Ratio));
			}
		}
	}
	Test->TestEqual(TEXT("Pairs closer than 0.9 x the sum of their radii"), TooClose, 0);
	Test->AddInfo(FString::Printf(TEXT("Crowd settled after %.1f s: closest pair at %.2f of their radii, farthest unit %.0f cm from the target"),
		Elapsed, WorstRatio, FarthestFromTarget));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidancePlaceGap, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidancePlaceGap::Update()
{
	ARTSPlayerController* Controller = GetController(*Test);
	URTSBuildingData* PowerPlant = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	IConsoleVariable* DebugBuild = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.DebugBuild"));
	if (!Controller || !PowerPlant || !DebugBuild)
	{
		Test->AddError(TEXT("No controller, power plant or rts.DebugBuild"));
		return true;
	}

	// The debug build menu's way in: the server places each plant without a builder.
	const bool bWasDebugBuild = DebugBuild->GetBool();
	DebugBuild->Set(true, ECVF_SetByConsole);
	for (const float Y : GapPlantYs)
	{
		FRTSCommand Build;
		Build.Type = ERTSCommandType::Build;
		Build.EntityData = PowerPlant;
		Build.TargetLocation = FVector(0.f, Y, 0.f);
		Controller->Server_IssueCommand(TArray<ARTSUnit*>(), Build);
	}
	DebugBuild->Set(bWasDebugBuild, ECVF_SetByConsole);

	int32 Placed = 0;
	for (TActorIterator<ARTSBuilding> It(Controller->GetWorld()); It; ++It)
	{
		Placed += It->GetBuildingData() == PowerPlant && FMath::IsNearlyZero(It->GetActorLocation().X, 1.f) ? 1 : 0;
	}
	Test->TestEqual(TEXT("Every power plant of the wall was placed"), Placed, static_cast<int32>(UE_ARRAY_COUNT(GapPlantYs)));
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidanceWaitForGap, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidanceWaitForGap::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSystem)
	{
		Test->AddError(TEXT("No navigation system"));
		return true;
	}

	// Cut where the plants stand, open in the middle of the gap.
	FNavLocation Projected;
	const FVector QueryExtent(10.f, 10.f, 250.f);
	const bool bCut = !NavSystem->ProjectPointToNavigation(FVector(0.f, GapPlantYs[3], 0.f), Projected, QueryExtent)
		&& !NavSystem->ProjectPointToNavigation(FVector(0.f, GapPlantYs[4], 0.f), Projected, QueryExtent);
	if (bCut && !NavSystem->IsNavigationBuildInProgress())
	{
		Test->TestTrue(TEXT("The gap keeps its navmesh"),
			NavSystem->ProjectPointToNavigation(FVector(0.f, GapCenterY, 0.f), Projected, QueryExtent));
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("The power plants never cut the navmesh"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidanceIssueGapMove, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidanceIssueGapMove::Update()
{
	if (!SpawnBlock(*Test, *State, GapSquadCount, GapSquadColumns, GapSquadOrigin, GapSquadSpacing, /*bTanks*/ false))
	{
		return true;
	}
	for (const TWeakObjectPtr<ARTSUnit>& Unit : State->Units)
	{
		State->LastLocations.Add(Unit->GetActorLocation());
		State->CrossingYs.AddDefaulted();
	}
	// One order for the squad, as the player gives it: the formation spreads them over the far side.
	GetController(*Test)->Server_IssueCommand(GetLiveUnits(*State), MakeMove(GapTarget));
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidanceVerifyGap, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidanceVerifyGap::Update()
{
	for (int32 Index = 0; Index < State->Units.Num(); ++Index)
	{
		const ARTSUnit* Unit = State->Units[Index].Get();
		if (!Unit)
		{
			continue;
		}
		const FVector Location = Unit->GetActorLocation();
		const FVector& Last = State->LastLocations[Index];
		if (!State->CrossingYs[Index] && Last.X < 0.f && Location.X >= 0.f)
		{
			const float Alpha = static_cast<float>(-Last.X / (Location.X - Last.X));
			State->CrossingYs[Index] = FMath::Lerp(Last.Y, Location.Y, Alpha);
		}
		State->LastLocations[Index] = Location;
	}

	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!AllOrdersDone(*State) && Elapsed < GapTimeout)
	{
		return false;
	}

	int32 ThroughGap = 0;
	int32 Arrived = 0;
	for (int32 Index = 0; Index < State->Units.Num(); ++Index)
	{
		const ARTSUnit* Unit = State->Units[Index].Get();
		const TOptional<float>& CrossingY = State->CrossingYs[Index];
		if (CrossingY && FMath::Abs(*CrossingY - GapCenterY) <= GapHalfWidth)
		{
			++ThroughGap;
		}
		else
		{
			Test->AddInfo(FString::Printf(TEXT("Unit %d crossed x = 0 at %s"), Index,
				CrossingY ? *FString::Printf(TEXT("y = %.0f"), *CrossingY) : TEXT("no point")));
		}
		Arrived += Unit && FVector::Dist2D(Unit->GetActorLocation(), GapTarget) <= GapArrivalRadius ? 1 : 0;
	}
	Test->TestEqual(TEXT("Every unit went through the gap"), ThroughGap, GapSquadCount);
	Test->TestEqual(TEXT("Every unit reached the far side"), Arrived, GapSquadCount);
	Test->AddInfo(FString::Printf(TEXT("Squad through the gap after %.1f s"), Elapsed));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FRTSAvoidanceIssueFrameTimeMove, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State, bool, bAvoidance);
bool FRTSAvoidanceIssueFrameTimeMove::Update()
{
	if (State->Units.IsEmpty()
		&& !SpawnBlock(*Test, *State, FrameTimeCount, FrameTimeColumns, FrameTimeOrigin, FrameTimeSpacing, /*bTanks*/ true))
	{
		return true;
	}
	GetAvoidanceVariable()->Set(bAvoidance, ECVF_SetByConsole);
	GetController(*Test)->Server_IssueCommand(GetLiveUnits(*State), MakeMove(bAvoidance ? FrameTimeEast : FrameTimeWest));

	State->PhaseStart = FPlatformTime::Seconds();
	State->LastFrame = State->PhaseStart;
	State->Frames = 0;
	State->FrameSeconds = 0.0;
	State->Passes = 0;
	State->PassSeconds = 0.0;
	State->MaxPassSeconds = 0.0;
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FRTSAvoidanceSampleFrameTime, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State, bool, bAvoidance);
bool FRTSAvoidanceSampleFrameTime::Update()
{
	const double Now = FPlatformTime::Seconds();
	++State->Frames;
	State->FrameSeconds += Now - State->LastFrame;
	State->LastFrame = Now;

	UWorld* World = AutomationCommon::GetAnyGameWorld();
	const URTSAvoidanceSubsystem* Avoidance = World ? World->GetSubsystem<URTSAvoidanceSubsystem>() : nullptr;
	if (!Avoidance)
	{
		Test->AddError(TEXT("No avoidance subsystem in the PIE world"));
		return true;
	}
	if (bAvoidance)
	{
		++State->Passes;
		State->PassSeconds += Avoidance->GetLastSeparateSeconds();
		State->MaxPassSeconds = FMath::Max(State->MaxPassSeconds, Avoidance->GetLastSeparateSeconds());
	}
	if (Now - State->PhaseStart < FrameTimeSampleSeconds)
	{
		return false;
	}

	int32 Walking = 0;
	for (const ARTSUnit* Unit : GetLiveUnits(*State))
	{
		Walking += Unit->GetVelocity().SizeSquared2D() > 1.f ? 1 : 0;
	}
	const double MeanFrameMs = State->FrameSeconds / State->Frames * 1000.0;
	if (bAvoidance)
	{
		const double MeanPassMs = State->PassSeconds / FMath::Max(State->Passes, 1) * 1000.0;
		State->MeanFrameMsOn = MeanFrameMs;
		Test->AddInfo(FString::Printf(TEXT("Avoidance on, %d of %d units walking: frame %.2f ms mean over %d frames, avoidance pass %.3f ms mean, %.3f ms max"),
			Walking, FrameTimeCount, MeanFrameMs, State->Frames, MeanPassMs, State->MaxPassSeconds * 1000.0));
		Test->TestTrue(TEXT("Units walk while the frame time is sampled"), Walking > FrameTimeCount / 2);
		Test->TestTrue(FString::Printf(TEXT("Avoidance pass under %.1f ms on average"), FrameTimeBudgetMs), MeanPassMs < FrameTimeBudgetMs);
	}
	else
	{
		Test->AddInfo(FString::Printf(TEXT("Avoidance off, %d of %d units walking: frame %.2f ms mean over %d frames (%+.2f ms with avoidance)"),
			Walking, FrameTimeCount, MeanFrameMs, State->Frames, State->MeanFrameMsOn - MeanFrameMs));
	}
	return true;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAvoidanceCrowdTest, "WorldDivide.Avoidance.Crowd", RTSTests::TestFlags)

bool FRTSAvoidanceCrowdTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSAvoidanceTests::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSAvoidanceTests::TestMap));
		return false;
	}

	const TSharedRef<RTSAvoidanceTests::FAvoidanceState> State = MakeShared<RTSAvoidanceTests::FAvoidanceState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceIssueCrowd(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceVerifyCrowd(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAvoidanceGapTest, "WorldDivide.Avoidance.Gap", RTSTests::TestFlags)

bool FRTSAvoidanceGapTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSAvoidanceTests::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSAvoidanceTests::TestMap));
		return false;
	}

	const TSharedRef<RTSAvoidanceTests::FAvoidanceState> State = MakeShared<RTSAvoidanceTests::FAvoidanceState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidancePlaceGap(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWaitForGap(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceIssueGapMove(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceVerifyGap(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAvoidanceFrameTimeTest, "WorldDivide.Avoidance.FrameTime", RTSTests::TestFlags)

bool FRTSAvoidanceFrameTimeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSAvoidanceTests::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSAvoidanceTests::TestMap));
		return false;
	}

	const TSharedRef<RTSAvoidanceTests::FAvoidanceState> State = MakeShared<RTSAvoidanceTests::FAvoidanceState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceIssueFrameTimeMove(this, State, true));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceSampleFrameTime(this, State, true));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceIssueFrameTimeMove(this, State, false));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceSampleFrameTime(this, State, false));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceRestore());
	return true;
}

#endif
