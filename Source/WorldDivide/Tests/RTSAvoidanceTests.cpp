#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSSupplySource.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestFormation.h"
#include "Tests/RTSTestPIE.h"
#include "Tests/RTSTestScale.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

/**
 * Local avoidance on L_TestMap, in the empty strip north of the bases. The crowd test sends 50 tanks and infantry, each on
 * its own, to one point and checks that they end apart. The gap test walls the strip off with power plants but for a gap
 * one cell wide and checks that a squad of infantry goes through it. The frame-time test walks 200 units across the strip
 * with avoidance on and off and reports what it costs; the mixed test does the same with 20 M20s among them. The tank-pair
 * tests stand two M20s side by side, which must stay put, and end to end overlapping, which must part no faster than half
 * their speed.
 */
namespace RTSAvoidanceTests
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");
	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* PowerPlantPath = TEXT("/Game/Data/Buildings/DA_TestPowerPlant.DA_TestPowerPlant");
	constexpr double NavTimeout = 30.0;

	/**
	 * Where the tests put things on L_TestMap. Every spot comes from the map and the game's data once the session is up
	 * (InitLayout, called by FRTSAvoidanceWaitForNavigation), so it moves with the scale (#136): the strip lies midway between
	 * Block_North and the ground's north edge, and blocks of units stand a tank's hull plus a gap apart.
	 */
	float StripY = 0.f;
	/** Blocks of tanks and infantry: a tank's length plus BlockGap along X, its width plus BlockGap along Y. */
	FVector2D BlockSpacing = FVector2D::ZeroVector;
	constexpr float BlockGap = 250.f;

	/** The crowd test: 50 units in a block west of the strip's centre, sent to one point east of it, three hulls clear of it. */
	constexpr int32 CrowdCount = 50;
	constexpr int32 CrowdColumns = 10;
	FVector CrowdOrigin;
	FVector CrowdTarget;
	/** Pairs may end overlapping by no more than this share of the sum of their footprints' radii. */
	constexpr float CrowdMaxOverlap = 0.1f;
	/** Game seconds the crowd stands once every order is done, before it is measured. */
	constexpr double CrowdSettleSeconds = 1.0;
	constexpr double CrowdTimeout = 60.0;

	/**
	 * The gap test: four power plants side by side on either side of the strip's lane along x = 0, but for a gap of
	 * 2 * GapHalfWidth across the lane, one grid cell wider than the infantry nav agent on each side. Going around either end
	 * is far longer than going through. GapPlantYs run south to north.
	 */
	float GapPlantYs[8];
	float GapCenterY = 0.f;
	constexpr float GapHalfWidth = 100.f;
	constexpr int32 GapSquadCount = 12;
	constexpr int32 GapSquadColumns = 3;
	FVector GapSquadOrigin;
	constexpr float GapSquadSpacing = 200.f;
	FVector GapTarget;
	constexpr double GapTimeout = 60.0;

	/** The frame-time test: 200 units, half tanks and half infantry, walk across the strip and back. */
	constexpr int32 FrameTimeCount = 200;
	constexpr int32 FrameTimeColumns = 20;
	FVector FrameTimeOrigin;
	FVector FrameTimeEast;
	FVector FrameTimeWest;
	/**
	 * The empty strip the walks stay in: north of Block_North and the bases' supply sources, StripMargin inside the map's
	 * edge. A formation that would reach past it is moved in, so its slots never land in a wall or off the navmesh.
	 */
	FBox2D Strip(ForceInit);
	constexpr float StripMargin = 500.f;
	/** Real seconds sampled in each half. */
	constexpr double FrameTimeSampleSeconds = 4.0;
	/** Mean time of the avoidance pass over 200 units that would start to show in a 60 fps frame. */
	constexpr double FrameTimeBudgetMs = 1.0;

	/** The mixed test: 180 tanks and infantry as in the frame-time test, and 20 M20s in a block of their own to the south. */
	constexpr int32 MixedM20Count = 20;
	constexpr int32 MixedM20Columns = 5;
	FVector MixedM20Origin;
	/** Clearance between the M20s' footprints in the block. */
	constexpr float MixedM20Clearance = 300.f;
	/** Provisional: the slowest pass in the mixed test (plan section 6, item 7). */
	constexpr double MixedMaxPassBudgetMs = 2.0;

	/** The tank-pair tests: two M20s facing east in the strip, watched for this many game seconds. */
	FVector PairOrigin;
	constexpr float PairSideBySide = 450.f;
	constexpr float PairEndToEndOverlap = 200.f;
	constexpr double PairWatchSeconds = 2.0;
	/** Standing clear, neither may drift more than this. */
	constexpr float PairStillTolerance = 1.f;
	/** The grid must return every unit within QueryReach of these points: the strip, both bases and the middle. */
	FVector QueryPoints[4];
	float QueryReach = 0.f;

	/** Lays the spots out from the map in World and the test data. False, with the reason in OutError, when something is missing. */
	bool InitLayout(const UWorld& World, FString& OutError)
	{
		const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
		const URTSBuildingData* Plant = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		const float HalfSize = RTSTests::GetMapHalfSize(World);
		const FBox2D Block = RTSTests::GetLabelledBox(World, TEXT("Block_North"));
		float SourcesMaxY = Block.Max.Y;
		for (TActorIterator<ARTSSupplySource> It(&World); It; ++It)
		{
			SourcesMaxY = FMath::Max(SourcesMaxY, It->GetActorLocation().Y + RTSBuildPlacement::GetFootprintExtent(It->Footprint).Y);
		}
		if (!Tank || !Plant || HalfSize <= 0.f || !Block.bIsValid)
		{
			OutError = TEXT("Missing test data, or L_TestMap lacks its NavMeshBoundsVolume or Block_North");
			return false;
		}
		const FVector2D TankHull = RTSTests::GetHullSize(*Tank);
		const FVector2D PlantExtent = RTSTests::GetFootprintExtent(*Plant);

		StripY = 0.5f * (Block.Max.Y + HalfSize);
		Strip = FBox2D(FVector2D(-HalfSize + StripMargin, SourcesMaxY + StripMargin), FVector2D(HalfSize - StripMargin, HalfSize - StripMargin));
		BlockSpacing = TankHull + FVector2D(BlockGap);

		CrowdTarget = FVector(3.f * TankHull.X, StripY, 0.f);
		const int32 CrowdRows = FMath::DivideAndRoundUp(CrowdCount, CrowdColumns);
		CrowdOrigin = FVector(CrowdTarget.X - 6.f * TankHull.X - CrowdColumns * BlockSpacing.X, StripY - 0.5f * (CrowdRows - 1) * BlockSpacing.Y, 0.f);

		// On a grid line, so the plants' even footprints centre where the gap needs them.
		GapCenterY = FMath::RoundToFloat(StripY / RTSBuildPlacement::GridSize) * RTSBuildPlacement::GridSize;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float Offset = GapHalfWidth + PlantExtent.Y + Index * 2.f * PlantExtent.Y;
			GapPlantYs[3 - Index] = GapCenterY - Offset;
			GapPlantYs[4 + Index] = GapCenterY + Offset;
		}
		GapSquadOrigin = FVector(-(PlantExtent.X + 2000.f), GapCenterY - 400.f, 0.f);
		GapTarget = FVector(PlantExtent.X + 1500.f, GapCenterY, 0.f);

		const int32 FrameTimeRows = FMath::DivideAndRoundUp(FrameTimeCount, FrameTimeColumns);
		FrameTimeOrigin = FVector(-0.5f * (FrameTimeColumns - 1) * BlockSpacing.X, StripY - 0.5f * (FrameTimeRows - 1) * BlockSpacing.Y, 0.f);
		FrameTimeEast = FVector(0.3f * HalfSize, StripY, 0.f);
		FrameTimeWest = FVector(-0.3f * HalfSize, StripY, 0.f);
		// South of the frame-time block, inside the strip.
		MixedM20Origin = FVector(FrameTimeOrigin.X, Strip.Min.Y + 1000.f, 0.f);

		PairOrigin = FVector(-0.3f * HalfSize, StripY, 0.f);
		QueryPoints[0] = PairOrigin;
		QueryPoints[1] = FVector(-0.6f * HalfSize, 0.f, 0.f);
		QueryPoints[2] = FVector(0.6f * HalfSize, 0.f, 0.f);
		QueryPoints[3] = FVector::ZeroVector;
		QueryReach = 0.25f * HalfSize;
		return true;
	}

	struct FAvoidanceState
	{
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		double PhaseStart = 0.0;
		/** How close to its target the last formation move ends (RTSTests::GetFormationArrivalRadius). */
		float ArrivalRadius = 0.f;
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
		int64 WalkingSum = 0;
		bool bMixed = false;

		// Tank-pair tests only.
		double WatchStart = -1.0;
		TArray<FVector> StartLocations;
		float WorstStepRatio = 0.f;
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
		const FVector2D& Spacing, bool bTanks)
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
			const FVector Location = Origin + FVector((Index % Columns) * Spacing.X, (Index / Columns) * Spacing.Y, 0.f);
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

	/**
	 * Target, moved so that the formation Units take there stays inside Strip. Fails the test when the formation is larger
	 * than the strip, rather than sending units into the walls around it.
	 */
	FVector FitInStrip(FAutomationTestBase& Test, TConstArrayView<ARTSUnit*> Units, FVector Target)
	{
		// Moving the target turns the formation a little, so check again after each move.
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
		{
			const FBox2D Bounds = RTSTests::GetFormationBounds(Units, Target);
			if (Bounds.GetSize().X > Strip.GetSize().X || Bounds.GetSize().Y > Strip.GetSize().Y)
			{
				Test.AddError(FString::Printf(TEXT("The formation (%.0f x %.0f cm) does not fit the strip (%.0f x %.0f cm)"),
					Bounds.GetSize().X, Bounds.GetSize().Y, Strip.GetSize().X, Strip.GetSize().Y));
				return Target;
			}
			const FVector2D Shift(FMath::Max(Strip.Min.X - Bounds.Min.X, 0.0) + FMath::Min(Strip.Max.X - Bounds.Max.X, 0.0),
				FMath::Max(Strip.Min.Y - Bounds.Min.Y, 0.0) + FMath::Min(Strip.Max.Y - Bounds.Max.Y, 0.0));
			if (Shift.IsNearlyZero(1.0))
			{
				return Target;
			}
			Target += FVector(Shift, 0.f);
		}
		const FBox2D Bounds = RTSTests::GetFormationBounds(Units, Target);
		Test.TestTrue(TEXT("The formation fits the strip"), Strip.ExpandBy(1.0).IsInside(Bounds));
		return Target;
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
		FString Error;
		if (!InitLayout(*World, Error))
		{
			Test->AddError(Error);
		}
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
	if (!SpawnBlock(*Test, *State, CrowdCount, CrowdColumns, CrowdOrigin, BlockSpacing, /*bTanks*/ true))
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
	float WorstOverlap = 0.f;
	float FarthestFromTarget = 0.f;
	int32 TooClose = 0;
	for (int32 A = 0; A < Units.Num(); ++A)
	{
		FarthestFromTarget = FMath::Max(FarthestFromTarget, FVector::Dist2D(Units[A]->GetActorLocation(), CrowdTarget));
		const FRTSFootprint FootprintA = Units[A]->GetFootprint();
		for (int32 B = A + 1; B < Units.Num(); ++B)
		{
			const FRTSFootprint FootprintB = Units[B]->GetFootprint();
			const float Overlap = -RTSUnitShape::GetGap(FootprintA, FootprintB) / (FootprintA.Radius + FootprintB.Radius);
			WorstOverlap = FMath::Max(WorstOverlap, Overlap);
			if (Overlap > CrowdMaxOverlap)
			{
				++TooClose;
				Test->AddInfo(FString::Printf(TEXT("%s and %s overlap by %.2f of their radii"), *Units[A]->GetName(),
					*Units[B]->GetName(), Overlap));
			}
		}
	}
	Test->TestEqual(TEXT("Pairs overlapping by more than 0.1 x the sum of their footprints' radii"), TooClose, 0);
	Test->AddInfo(FString::Printf(TEXT("Crowd settled after %.1f s: deepest overlap %.2f of the radii, farthest unit %.0f cm from the target"),
		Elapsed, WorstOverlap, FarthestFromTarget));
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
	if (!SpawnBlock(*Test, *State, GapSquadCount, GapSquadColumns, GapSquadOrigin, FVector2D(GapSquadSpacing), /*bTanks*/ false))
	{
		return true;
	}
	for (const TWeakObjectPtr<ARTSUnit>& Unit : State->Units)
	{
		State->LastLocations.Add(Unit->GetActorLocation());
		State->CrossingYs.AddDefaulted();
	}
	// One order for the squad, as the player gives it: the formation spreads them over the far side.
	State->ArrivalRadius = RTSTests::GetFormationArrivalRadius(GetLiveUnits(*State), GapTarget);
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
		Arrived += Unit && FVector::Dist2D(Unit->GetActorLocation(), GapTarget) <= State->ArrivalRadius ? 1 : 0;
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
		&& !SpawnBlock(*Test, *State, FrameTimeCount, FrameTimeColumns, FrameTimeOrigin, BlockSpacing, /*bTanks*/ true))
	{
		return true;
	}
	GetAvoidanceVariable()->Set(bAvoidance, ECVF_SetByConsole);
	const TArray<ARTSUnit*> Units = GetLiveUnits(*State);
	const FVector Target = FitInStrip(*Test, Units, bAvoidance ? FrameTimeEast : FrameTimeWest);
	const FBox2D Bounds = RTSTests::GetFormationBounds(Units, Target);
	Test->AddInfo(FString::Printf(TEXT("Walking to %s, the formation %.0f x %.0f cm"), *Target.ToCompactString(), Bounds.GetSize().X, Bounds.GetSize().Y));
	GetController(*Test)->Server_IssueCommand(Units, MakeMove(Target));

	State->PhaseStart = FPlatformTime::Seconds();
	State->LastFrame = State->PhaseStart;
	State->Frames = 0;
	State->FrameSeconds = 0.0;
	State->Passes = 0;
	State->PassSeconds = 0.0;
	State->MaxPassSeconds = 0.0;
	State->WalkingSum = 0;
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
	// Units walking, on average over the sample: the first ones may arrive before it ends.
	for (const ARTSUnit* Unit : GetLiveUnits(*State))
	{
		State->WalkingSum += Unit->GetVelocity().SizeSquared2D() > 1.f ? 1 : 0;
	}
	if (Now - State->PhaseStart < FrameTimeSampleSeconds)
	{
		return false;
	}

	const int32 Walking = static_cast<int32>(State->WalkingSum / State->Frames);
	const double MeanFrameMs = State->FrameSeconds / State->Frames * 1000.0;
	if (bAvoidance)
	{
		const double MeanPassMs = State->PassSeconds / FMath::Max(State->Passes, 1) * 1000.0;
		const double MaxPassMs = State->MaxPassSeconds * 1000.0;
		State->MeanFrameMsOn = MeanFrameMs;
		Test->AddInfo(FString::Printf(TEXT("Avoidance on%s, %d of %d units walking on average: frame %.2f ms mean over %d frames, avoidance pass %.3f ms mean, %.3f ms max"),
			State->bMixed ? TEXT(" (mixed, 20 M20s)") : TEXT(""), Walking, FrameTimeCount, MeanFrameMs, State->Frames, MeanPassMs, MaxPassMs));
		Test->TestTrue(TEXT("Units walk while the frame time is sampled"), Walking > FrameTimeCount / 2);
		Test->TestTrue(FString::Printf(TEXT("Avoidance pass under %.1f ms on average"), FrameTimeBudgetMs), MeanPassMs < FrameTimeBudgetMs);
		if (State->bMixed)
		{
			Test->TestTrue(FString::Printf(TEXT("Slowest avoidance pass under %.1f ms"), MixedMaxPassBudgetMs), MaxPassMs < MixedMaxPassBudgetMs);
		}
	}
	else
	{
		Test->AddInfo(FString::Printf(TEXT("Avoidance off, %d of %d units walking on average: frame %.2f ms mean over %d frames (%+.2f ms with avoidance)"),
			Walking, FrameTimeCount, MeanFrameMs, State->Frames, State->MeanFrameMsOn - MeanFrameMs));
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSAvoidanceSpawnMixed, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State);
bool FRTSAvoidanceSpawnMixed::Update()
{
	ARTSPlayerController* Controller = GetController(*Test);
	URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
	if (!Controller || !M20)
	{
		Test->AddError(TEXT("No controller or M20 data"));
		return true;
	}
	State->bMixed = true;
	if (!SpawnBlock(*Test, *State, FrameTimeCount - MixedM20Count, FrameTimeColumns, FrameTimeOrigin, BlockSpacing, /*bTanks*/ true))
	{
		return true;
	}
	// Spaced by the M20's own footprint, so the block stands clear before it moves.
	FVector2D Pitch = FVector2D::ZeroVector;
	for (int32 Index = 0; Index < MixedM20Count; ++Index)
	{
		const FVector Location = MixedM20Origin + FVector((Index % MixedM20Columns) * Pitch.X, (Index / MixedM20Columns) * Pitch.Y, 0.f);
		ARTSUnit* Unit = SpawnUnit(Controller->GetWorld(), M20, Controller->GetTeamId(), Location);
		State->Units.Add(Unit);
		if (Index == 0)
		{
			const FRTSFootprint Footprint = Unit->GetFootprint();
			Pitch = FVector2D(2.f * (Footprint.HalfSegment + Footprint.Radius), 2.f * Footprint.Radius) + FVector2D(MixedM20Clearance);
		}
	}
	return true;
}

/** Two M20s facing east in the strip: side by side at PairSideBySide, or end to end overlapping by PairEndToEndOverlap. */
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FRTSAvoidanceSpawnPair, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State, bool, bEndToEnd);
bool FRTSAvoidanceSpawnPair::Update()
{
	ARTSPlayerController* Controller = GetController(*Test);
	URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
	if (!Controller || !M20)
	{
		Test->AddError(TEXT("No controller or M20 data"));
		return true;
	}
	ARTSUnit* First = SpawnUnit(Controller->GetWorld(), M20, Controller->GetTeamId(), PairOrigin);
	const FRTSFootprint Footprint = First->GetFootprint();
	const FVector Offset = bEndToEnd
		? FVector(2.f * (Footprint.HalfSegment + Footprint.Radius) - PairEndToEndOverlap, 0.f, 0.f)
		: FVector(0.f, PairSideBySide, 0.f);
	ARTSUnit* Second = SpawnUnit(Controller->GetWorld(), M20, Controller->GetTeamId(), PairOrigin + Offset);
	State->Units = { First, Second };
	for (const ARTSUnit* Unit : { First, Second })
	{
		State->StartLocations.Add(Unit->GetActorLocation());
		State->LastLocations.Add(Unit->GetActorLocation());
	}
	const float Gap = RTSUnitShape::GetGap(First->GetFootprint(), Second->GetFootprint());
	Test->AddInfo(FString::Printf(TEXT("M20 footprint: %.0f cm long, %.0f cm wide; the pair starts %.0f cm apart at the edges"),
		2.f * (Footprint.HalfSegment + Footprint.Radius), 2.f * Footprint.Radius, Gap));
	if (!bEndToEnd)
	{
		Test->TestTrue(TEXT("Side by side at 4.5 m, the hulls do not touch"), Gap > 0.f);
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FRTSAvoidanceWatchPair, FAutomationTestBase*, Test, TSharedRef<FAvoidanceState>, State, bool, bEndToEnd);
bool FRTSAvoidanceWatchPair::Update()
{
	const TArray<ARTSUnit*> Units = GetLiveUnits(*State);
	if (Units.Num() != 2)
	{
		Test->AddError(TEXT("The pair is gone"));
		return true;
	}
	UWorld* World = Units[0]->GetWorld();
	if (State->WatchStart < 0.0)
	{
		State->WatchStart = World->GetTimeSeconds();
	}

	// The push of the frame just ended, against the most it may be: half the unit's speed over that frame.
	const float DeltaSeconds = World->GetDeltaSeconds();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector Location = Units[Index]->GetActorLocation();
		const float Step = FVector::Dist2D(Location, State->LastLocations[Index]);
		const float Allowed = Units[Index]->GetUnitData()->MoveSpeed * 0.5f * DeltaSeconds;
		if (Step > 0.01f && Allowed > 0.f)
		{
			State->WorstStepRatio = FMath::Max(State->WorstStepRatio, Step / Allowed);
		}
		State->LastLocations[Index] = Location;
	}
	if (World->GetTimeSeconds() - State->WatchStart < PairWatchSeconds)
	{
		return false;
	}

	const float Gap = RTSUnitShape::GetGap(Units[0]->GetFootprint(), Units[1]->GetFootprint());
	const float Drift = FMath::Max(FVector::Dist2D(Units[0]->GetActorLocation(), State->StartLocations[0]),
		FVector::Dist2D(Units[1]->GetActorLocation(), State->StartLocations[1]));
	Test->AddInfo(FString::Printf(TEXT("After %.1f s: %.1f cm apart at the edges, moved up to %.1f cm, fastest push %.2f of half speed"),
		PairWatchSeconds, Gap, Drift, State->WorstStepRatio));
	if (bEndToEnd)
	{
		// The margin is float error on a full step.
		Test->TestTrue(TEXT("No push is faster than half the tank's speed"), State->WorstStepRatio <= 1.01f);
		Test->TestTrue(TEXT("The overlapping tanks end clear of each other"), Gap >= -1.f);
		return true;
	}
	Test->TestTrue(TEXT("Tanks side by side at 4.5 m do not push each other"), Drift < PairStillTolerance);

	// The grid returns every unit whose footprint reaches within the radius.
	const URTSAvoidanceSubsystem* Avoidance = World->GetSubsystem<URTSAvoidanceSubsystem>();
	int32 Missing = 0;
	int32 Returned = 0;
	for (const FVector& Point : QueryPoints)
	{
		TArray<ARTSUnit*> Found;
		Avoidance->QueryRadius(Point, QueryReach, Found);
		Returned += Found.Num();
		for (TActorIterator<ARTSUnit> It(World); It; ++It)
		{
			const FRTSFootprint Footprint = It->GetFootprint();
			const float Reach = FVector::Dist2D(Point, Footprint.GetClosestSegmentPoint(Point)) - Footprint.Radius;
			if (Reach <= QueryReach && !It->GetHealthComponent()->IsDead() && !Found.Contains(*It))
			{
				++Missing;
				Test->AddInfo(FString::Printf(TEXT("QueryRadius at %s missed %s"), *Point.ToCompactString(), *It->GetName()));
			}
		}
	}
	Test->TestEqual(TEXT("QueryRadius misses no unit in reach"), Missing, 0);
	Test->AddInfo(FString::Printf(TEXT("QueryRadius returned %d units over %d points"), Returned, static_cast<int32>(UE_ARRAY_COUNT(QueryPoints))));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAvoidanceMixedTest, "WorldDivide.Avoidance.Mixed", RTSTests::TestFlags)

bool FRTSAvoidanceMixedTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSAvoidanceTests::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSAvoidanceTests::TestMap));
		return false;
	}

	const TSharedRef<RTSAvoidanceTests::FAvoidanceState> State = MakeShared<RTSAvoidanceTests::FAvoidanceState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceSpawnMixed(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceIssueFrameTimeMove(this, State, true));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceSampleFrameTime(this, State, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceRestore());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAvoidanceParallelTanksTest, "WorldDivide.Avoidance.ParallelTanks", RTSTests::TestFlags)

bool FRTSAvoidanceParallelTanksTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSAvoidanceTests::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSAvoidanceTests::TestMap));
		return false;
	}

	const TSharedRef<RTSAvoidanceTests::FAvoidanceState> State = MakeShared<RTSAvoidanceTests::FAvoidanceState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceSpawnPair(this, State, false));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWatchPair(this, State, false));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAvoidanceEndToEndTest, "WorldDivide.Avoidance.EndToEnd", RTSTests::TestFlags)

bool FRTSAvoidanceEndToEndTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSAvoidanceTests::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSAvoidanceTests::TestMap));
		return false;
	}

	const TSharedRef<RTSAvoidanceTests::FAvoidanceState> State = MakeShared<RTSAvoidanceTests::FAvoidanceState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceSpawnPair(this, State, true));
	ADD_LATENT_AUTOMATION_COMMAND(RTSAvoidanceTests::FRTSAvoidanceWatchPair(this, State, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
