#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Units/Debug/RTSBenchPlan.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Count.h"
#include "Units/Debug/RTSBenchScene.h"
#if WITH_EDITOR
#include "Commands/RTSCommand.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Navigation/RTSHullClearance.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestGeometry.h"
#include "Tests/RTSTestPIE.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#endif

namespace
{
	using RTSTests::TestFlags;
}

/**
 * The bench methods measure what PERFORMANCE_RESULTS.md says they do (#167): the long method is the #138 gate, unchanged; the
 * short method is the step 0 method; the quick method is 240 moving units, a 10 s warm-up and one 30 s sample, in the long scene.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchPlanTest, "WorldDivide.Debug.Bench.Plan", TestFlags)

bool FRTSBenchPlanTest::RunTest(const FString& Parameters)
{
	const TArray<int32> GateCounts = { 1, 30, 120, 240 };

	const FRTSBenchPlan Long = FRTSBenchPlan::Make(ERTSBenchMethod::Long, {});
	TestTrue(TEXT("long: counts"), Long.Counts == GateCounts);
	TestTrue(TEXT("long: measures idle"), Long.bIdle);
	TestEqual(TEXT("long: idle warm-up"), Long.IdleWarmupSeconds, 10.0);
	TestEqual(TEXT("long: move warm-up"), Long.MoveWarmupSeconds, 10.0);
	TestEqual(TEXT("long: sample length"), Long.SampleSeconds, 60.0);
	TestEqual(TEXT("long: samples at 240"), Long.GetSamples(240), 3);
	TestEqual(TEXT("long: samples at 120"), Long.GetSamples(120), 1);
	TestEqual(TEXT("long: samples at 1"), Long.GetSamples(1), 1);
	TestTrue(TEXT("long: long scene"), Long.bLongScene);

	const FRTSBenchPlan Short = FRTSBenchPlan::Make(ERTSBenchMethod::Short, {});
	TestTrue(TEXT("short: counts"), Short.Counts == GateCounts);
	TestTrue(TEXT("short: measures idle"), Short.bIdle);
	TestEqual(TEXT("short: settle"), Short.IdleWarmupSeconds, 3.0);
	TestEqual(TEXT("short: move lead"), Short.MoveWarmupSeconds, 1.0);
	TestFalse(TEXT("short: frame sample"), Short.IsTimed());
	TestEqual(TEXT("short: frames"), Short.SampleFrames, 120);
	TestFalse(TEXT("short: step 0 scene"), Short.bLongScene);

	const FRTSBenchPlan Quick = FRTSBenchPlan::Make(ERTSBenchMethod::Quick, {});
	TestTrue(TEXT("quick: counts"), Quick.Counts == TArray<int32>{ 240 });
	TestFalse(TEXT("quick: moving only"), Quick.bIdle);
	TestEqual(TEXT("quick: move warm-up"), Quick.MoveWarmupSeconds, 10.0);
	TestEqual(TEXT("quick: sample length"), Quick.SampleSeconds, 30.0);
	TestEqual(TEXT("quick: one sample"), Quick.GetSamples(240), 1);
	TestTrue(TEXT("quick: long scene, so it compares with the gate's move rows"), Quick.bLongScene);

	const FRTSBenchPlan Screen = FRTSBenchPlan::Make(ERTSBenchMethod::Screen, {});
	TestTrue(TEXT("screen: counts"), Screen.Counts == TArray<int32>{ 240 });
	TestFalse(TEXT("screen: moving only"), Screen.bIdle);
	TestEqual(TEXT("screen: move warm-up"), Screen.MoveWarmupSeconds, 10.0);
	TestEqual(TEXT("screen: sample length (#171)"), Screen.SampleSeconds, 15.0);
	TestEqual(TEXT("screen: one sample"), Screen.GetSamples(240), 1);
	TestTrue(TEXT("screen: the long scene"), Screen.bLongScene);

	const FRTSBenchPlan QuickCounts = FRTSBenchPlan::Make(ERTSBenchMethod::Quick, { 30, 120 });
	TestTrue(TEXT("quick: given counts"), QuickCounts.Counts == TArray<int32>{ 30, 120 });
	TestEqual(TEXT("quick: one sample at the largest given count"), QuickCounts.GetSamples(120), 1);
	return true;
}

/** rts.Debug.Bench's arguments: counts joined by ',' or '+', and the method words. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchArgsTest, "WorldDivide.Debug.Bench.Args", TestFlags)

bool FRTSBenchArgsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("counts with '+'"), FRTSBenchPlan::ParseCounts(TEXT("1+30+120+240")) == TArray<int32>{ 1, 30, 120, 240 });
	TestTrue(TEXT("counts with ','"), FRTSBenchPlan::ParseCounts(TEXT("240,30")) == TArray<int32>{ 240, 30 });
	TestTrue(TEXT("0 is no count"), FRTSBenchPlan::ParseCounts(TEXT("0")).IsEmpty());
	TestTrue(TEXT("words are no counts"), FRTSBenchPlan::ParseCounts(TEXT("quick")).IsEmpty());

	for (const ERTSBenchMethod Method : { ERTSBenchMethod::Short, ERTSBenchMethod::Long, ERTSBenchMethod::Quick, ERTSBenchMethod::Screen })
	{
		ERTSBenchMethod Parsed = ERTSBenchMethod::Short;
		TestTrue(FString::Printf(TEXT("%s parses"), FRTSBenchPlan::GetMethodName(Method)),
			FRTSBenchPlan::ParseMethod(FRTSBenchPlan::GetMethodName(Method), Parsed) && Parsed == Method);
	}
	ERTSBenchMethod Unknown = ERTSBenchMethod::Short;
	TestFalse(TEXT("an unknown method is refused"), FRTSBenchPlan::ParseMethod(TEXT("fast"), Unknown));
	return true;
}

namespace RTSBenchTests
{
	using namespace RTSBenchScene;

	/** The hulls of a grid of Count hulls Size big, spaced as the bench spaces them, round LongGridCentre. */
	TArray<FBox2D> MakeHulls(int32 Count, const FVector2D& Size)
	{
		int32 Rows = 0;
		int32 Columns = 0;
		TArray<FBox2D> Hulls;
		for (const FVector& Place : MakeGrid(Count, Size + FVector2D(RowGap, ColumnGap), LongGridCentre, Rows, Columns))
		{
			Hulls.Add(FBox2D(FVector2D(Place) - Size / 2.f, FVector2D(Place) + Size / 2.f));
		}
		return Hulls;
	}

	int32 CountKind(TConstArrayView<FSolid> Layout, ESolidKind Kind)
	{
		return Algo::CountIf(Layout, [Kind](const FSolid& Solid) { return Solid.Kind == Kind; });
	}
}

/**
 * The obstacle scene of rts.Debug.Bench (#172): the same grid gives the same solids; every kind stands there, walls both along
 * and across the drive; none stands on a hull where the grid spawns or beside another solid, all of them on the area round
 * the grid and its drive, about as dense as RTSBenchScene::Solids says.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchSolidLayoutTest, "WorldDivide.Debug.Bench.SolidLayout", TestFlags)

bool FRTSBenchSolidLayoutTest::RunTest(const FString& Parameters)
{
	using namespace RTSBenchTests;
	// About an M20's hull: the grid of 240 covers some 120 x 120 m.
	const TArray<FBox2D> Hulls = MakeHulls(240, FVector2D(800.f, 350.f));
	const FBox2D Area = GetSolidsArea(Hulls, LongMoveDistance);
	const TArray<FSolid> Layout = MakeSolids(Area, Hulls);
	TestTrue(TEXT("the same grid gives the same solids"), Layout == MakeSolids(Area, Hulls));
	TestTrue(TEXT("the area reaches past the drive"), Area.Min.X < Hulls[0].Min.X + LongMoveDistance);

	const int32 Trunks = CountKind(Layout, ESolidKind::Trunk);
	const int32 Boulders = CountKind(Layout, ESolidKind::Boulder);
	const int32 Walls = CountKind(Layout, ESolidKind::Wall);
	const double SquareMetres = Area.GetArea() / 1.0e4;
	AddInfo(FString::Printf(TEXT("%d trunks, %d boulders, %d walls over %.0f m2: one per %.0f m2"), Trunks, Boulders, Walls,
		SquareMetres, SquareMetres / FMath::Max(1, Layout.Num())));
	TestTrue(TEXT("trunks stand there"), Trunks >= 20);
	TestTrue(TEXT("boulders stand there"), Boulders >= 10);
	TestTrue(TEXT("walls stand there"), Walls >= 3);
	TestTrue(TEXT("no kind stands denser than planned"), Boulders <= FMath::RoundToInt(SquareMetres * Solids::BouldersPerM2)
		&& Walls <= FMath::RoundToInt(SquareMetres * Solids::WallsPerM2));
	TestTrue(TEXT("a wall runs along the drive"), Layout.ContainsByPredicate([](const FSolid& Solid)
		{ return Solid.Kind == ESolidKind::Wall && Solid.Size.X > Solid.Size.Y; }));
	TestTrue(TEXT("a wall runs across the drive"), Layout.ContainsByPredicate([](const FSolid& Solid)
		{ return Solid.Kind == ESolidKind::Wall && Solid.Size.Y > Solid.Size.X; }));

	for (int32 Index = 0; Index < Layout.Num(); ++Index)
	{
		const FBox2D Box = Layout[Index].GetBox();
		TestTrue(TEXT("every solid stands on the area"), Area.IsInside(Box.Min) && Area.IsInside(Box.Max));
		const FBox2D Clear = Box.ExpandBy(Solids::KeepClear - 1.f);
		if (Hulls.ContainsByPredicate([&Clear](const FBox2D& Hull) { return Clear.Intersect(Hull); }))
		{
			AddError(FString::Printf(TEXT("solid %d stands within %.0f cm of a hull"), Index, Solids::KeepClear));
		}
		for (int32 Other = Index + 1; Other < Layout.Num(); ++Other)
		{
			if (Box.ExpandBy(Solids::Spacing - 1.f).Intersect(Layout[Other].GetBox()))
			{
				AddError(FString::Printf(TEXT("solids %d and %d stand within %.0f cm"), Index, Other, Solids::Spacing));
			}
		}
	}
	return true;
}

#if WITH_EDITOR

namespace RTSBenchTests
{
	const TCHAR* BenchMap = TEXT("/Game/Maps/L_Bench");
	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	constexpr int32 UnitCount = 240;
	constexpr double TestRate = 30.0;
	constexpr double NavTimeout = 30.0;
	constexpr double TestTimeout = 180.0;
	/** Game seconds the grid stands before its first order, and each leg of the drive, as the long scene's. */
	constexpr double SettleSeconds = 1.0;
	constexpr double LegSeconds = 15.0;

	/** #168: no hull more than 1 cm into a solid. A hull within ContactGap of one has met it. */
	constexpr float PenetrationTolerance = 1.f;
	constexpr float ContactGap = 10.f;
	/** A unit that drove MovedDistance on a leg has moved, not stuck; at least MovedShare of them do on every leg. */
	constexpr float MovedDistance = 2000.f;
	constexpr float MovedShare = 0.75f;
	/** At least PassedShare of the units drive past a solid in their neighbourhood without meeting it. */
	constexpr float PassedShare = 0.1f;

	/** Ground distance from the hull box of Footprint to Solid: the gap, or minus how deep the hull is in it. */
	float GetSolidGap(const FRTSFootprint& Footprint, const FSolid& Solid)
	{
		if (Solid.Kind != ESolidKind::Trunk)
		{
			return RTSTests::GetHullGap(Footprint, Solid.GetBox());
		}
		// A trunk is a disc: its centre in the hull's frame, against the hull's half extents.
		const FVector2D Axis = FVector2D(Footprint.Axis).GetSafeNormal();
		const FVector2D Offset = FVector2D(Solid.Center) - FVector2D(Footprint.Center);
		const FVector2D Local(FVector2D::DotProduct(Offset, Axis), FVector2D::DotProduct(Offset, FVector2D(-Axis.Y, Axis.X)));
		const FVector2D Half(Footprint.HalfSegment + Footprint.Radius, Footprint.Radius);
		const FVector2D Outside(FMath::Max(FMath::Abs(Local.X) - Half.X, 0.f), FMath::Max(FMath::Abs(Local.Y) - Half.Y, 0.f));
		const float Distance = Outside.IsZero() ? -FMath::Min(Half.X - FMath::Abs(Local.X), Half.Y - FMath::Abs(Local.Y)) : Outside.Size();
		return Distance - Solid.Size.X / 2.f;
	}

	struct FUnitTrack
	{
		TWeakObjectPtr<ARTSUnit> Unit;
		FVector LegStart = FVector::ZeroVector;
		float MinGap = FLT_MAX;
		bool bNear = false;
		bool bContact = false;
		bool bPassed = false;
	};

	struct FSolidsState
	{
		double PhaseStart = 0.0;
		double LegStart = 0.0;
		int32 Leg = -1;
		TArray<FUnitTrack> Tracks;
		TArray<FSolid> Layout;
		float MaxDepth = 0.f;
		FRTSSolidStats Stats;
		int32 Frames = 0;
	};

	ARTSPlayerController* GetController()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	/** The bench's grid of 240 M20s for the player, and its solids, as rts.Debug.Bench DA_TestM20Guardian@Solids makes them. */
	bool SpawnScene(UWorld& World, ARTSPlayerController& Controller, FSolidsState& State, FAutomationTestBase& Test)
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, M20Path);
		if (!Test.TestNotNull(TEXT("M20 data"), Data))
		{
			return false;
		}
		auto Spawn = [&World, &Controller, Data](const FVector& Location)
		{
			const FTransform Transform(FRotator::ZeroRotator, Location);
			ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Unit->InitFromData(Data, Controller.GetTeamId());
			Unit->FinishSpawning(Transform);
			Unit->SetActorLocation(Location + FVector(0.f, 0.f, Unit->GetGroundOffset()));
			return Unit;
		};
		ARTSUnit* First = Spawn(LongGridCentre + FVector(0.f, 0.f, 10000.f));
		const FVector Size = First->CalculateComponentsBoundingBoxInLocalSpace(/*bNonColliding*/ true).GetSize();
		int32 Rows = 0;
		int32 Columns = 0;
		const TArray<FVector> Places = MakeGrid(UnitCount, FVector2D(Size.X + RowGap, Size.Y + ColumnGap), LongGridCentre, Rows, Columns);
		TArray<FBox2D> Hulls;
		for (int32 Index = 0; Index < Places.Num(); ++Index)
		{
			ARTSUnit* Unit = Index == 0 ? First : Spawn(Places[Index]);
			if (Index == 0)
			{
				Unit->SetActorLocation(Places[0] + FVector(0.f, 0.f, Unit->GetGroundOffset()));
			}
			FUnitTrack& Track = State.Tracks.AddDefaulted_GetRef();
			Track.Unit = Unit;
			Hulls.Add(GetHullBox(Unit->GetFootprint()));
		}
		State.Layout = MakeSolids(GetSolidsArea(Hulls, LongMoveDistance), Hulls);
		return Test.TestEqual(TEXT("every solid spawns"), SpawnSolids(World, State.Layout, LongGridCentre.Z).Num(), State.Layout.Num());
	}

	/** The order of the long scene's leg: to the point west of the grid, then back to its place. */
	void IssueLeg(ARTSPlayerController& Controller, FSolidsState& State)
	{
		TArray<ARTSUnit*> Units;
		for (FUnitTrack& Track : State.Tracks)
		{
			if (ARTSUnit* Unit = Track.Unit.Get())
			{
				Units.Add(Unit);
				Track.LegStart = Unit->GetActorLocation();
			}
		}
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = LongGridCentre + FVector(State.Leg % 2 == 0 ? LongMoveDistance : 0.f, 0.f, 0.f);
		Controller.Server_IssueCommand(Units, Move);
	}

	/** Every hull against every solid near it, this frame. */
	void TrackFrame(FSolidsState& State)
	{
		++State.Frames;
		for (FUnitTrack& Track : State.Tracks)
		{
			const ARTSUnit* Unit = Track.Unit.Get();
			if (!Unit)
			{
				continue;
			}
			const FRTSFootprint Footprint = Unit->GetFootprint();
			const float Reach = Footprint.HalfSegment + 2.f * Footprint.Radius + 500.f;
			for (const FSolid& Solid : State.Layout)
			{
				if (FVector2D::Distance(FVector2D(Solid.Center), FVector2D(Footprint.Center)) > Reach + FVector2D(Solid.Size).Size() / 2.f)
				{
					continue;
				}
				const float Gap = GetSolidGap(Footprint, Solid);
				Track.MinGap = FMath::Min(Track.MinGap, Gap);
				State.MaxDepth = FMath::Max(State.MaxDepth, -Gap);
			}
			Track.bNear |= Unit->GetUnitMovement() && !Unit->GetUnitMovement()->GetSolidNeighbourhood().Solids.IsEmpty();
		}
	}

	void AddStats(FSolidsState& State)
	{
		const FRTSSolidStats Frame = FRTSSolidStats::Take();
		State.Stats.Updates += Frame.Updates;
		State.Stats.Reuses += Frame.Reuses;
		State.Stats.Checks += Frame.Checks;
		State.Stats.ComponentQueries += Frame.ComponentQueries;
		State.Stats.BroadSeconds += Frame.BroadSeconds;
		State.Stats.NarrowSeconds += Frame.NarrowSeconds;
	}

	/** Checks a finished leg: enough units moved; then starts tracking the next one afresh. */
	void EndLeg(FSolidsState& State, FAutomationTestBase& Test)
	{
		int32 Moved = 0;
		int32 Near = 0;
		for (FUnitTrack& Track : State.Tracks)
		{
			const ARTSUnit* Unit = Track.Unit.Get();
			if (Unit && FVector2D::Distance(FVector2D(Unit->GetActorLocation()), FVector2D(Track.LegStart)) >= MovedDistance)
			{
				++Moved;
			}
			const bool bContact = Track.MinGap < ContactGap;
			Track.bContact |= bContact;
			Track.bPassed |= Track.bNear && !bContact;
			Near += Track.bNear ? 1 : 0;
			Track.MinGap = FLT_MAX;
			Track.bNear = false;
		}
		Test.AddInfo(FString::Printf(TEXT("leg %d: %d of %d units drove %.0f m or more, %d had solids in their neighbourhood"),
			State.Leg + 1, Moved, State.Tracks.Num(), MovedDistance / 100.f, Near));
		Test.TestTrue(FString::Printf(TEXT("leg %d: the units drive rather than pile up"), State.Leg + 1),
			Moved >= FMath::CeilToInt(MovedShare * State.Tracks.Num()));
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSBenchRunSolids, FAutomationTestBase*, Test, TSharedRef<RTSBenchTests::FSolidsState>, State);
bool FRTSBenchRunSolids::Update()
{
	using namespace RTSBenchTests;
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > TestTimeout)
	{
		Test->AddError(TEXT("The drive did not finish in time"));
		return true;
	}
	UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	ARTSPlayerController* Controller = GetController();
	if (State->Tracks.IsEmpty())
	{
		if (!World || !World->HasBegunPlay() || !Controller || Controller->GetTeamId() == INDEX_NONE || !RTSTests::IsNavigationReady(*World))
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
			{
				Test->AddError(TEXT("PIE did not start or the navmeshes were not built in time"));
				return true;
			}
			return false;
		}
		RTSTests::ApplyTestFixedStep(TestRate);
		FApp::SetFixedDeltaTime(1.0 / TestRate);
		if (!SpawnScene(*World, *Controller, *State, *Test))
		{
			return true;
		}
		State->LegStart = World->GetTimeSeconds();
		return false;
	}
	if (!World || !Controller)
	{
		Test->AddError(TEXT("The play session ended"));
		return true;
	}

	const double Now = World->GetTimeSeconds();
	if (State->Leg < 0)
	{
		if (Now - State->LegStart >= SettleSeconds)
		{
			FRTSSolidStats::Take();
			State->Leg = 0;
			State->LegStart = Now;
			IssueLeg(*Controller, *State);
		}
		return false;
	}
	TrackFrame(*State);
	AddStats(*State);
	if (Now - State->LegStart < LegSeconds)
	{
		return false;
	}
	EndLeg(*State, *Test);
	if (++State->Leg < 2)
	{
		State->LegStart = Now;
		IssueLeg(*Controller, *State);
		return false;
	}

	const int32 Units = State->Tracks.Num();
	const int32 Contacts = Algo::CountIf(State->Tracks, [](const FUnitTrack& Track) { return Track.bContact; });
	const int32 Passed = Algo::CountIf(State->Tracks, [](const FUnitTrack& Track) { return Track.bPassed; });
	const FRTSSolidStats& Stats = State->Stats;
	const int32 Frames = FMath::Max(1, State->Frames);
	Test->AddInfo(FString::Printf(TEXT("%d solids (%d trunks, %d boulders, %d walls); %d units met one, %d passed one; deepest %.2f cm"),
		State->Layout.Num(), CountKind(State->Layout, ESolidKind::Trunk), CountKind(State->Layout, ESolidKind::Boulder),
		CountKind(State->Layout, ESolidKind::Wall), Contacts, Passed, State->MaxDepth));
	Test->AddInfo(FString::Printf(TEXT("per frame: %.2f broad overlaps, %.1f%% reused, %.1f hull checks, %.1f component queries, broad %.4f ms, narrow %.4f ms"),
		static_cast<double>(Stats.Updates) / Frames, 100.0 * Stats.Reuses / FMath::Max(1, Stats.Updates + Stats.Reuses),
		static_cast<double>(Stats.Checks) / Frames, static_cast<double>(Stats.ComponentQueries) / Frames,
		Stats.BroadSeconds * 1000.0 / Frames, Stats.NarrowSeconds * 1000.0 / Frames));
	Test->TestTrue(TEXT("the hulls are checked against solids"), Stats.Checks > 0 && Stats.ComponentQueries > 0);
	Test->TestTrue(TEXT("neighbourhoods are reused"), Stats.Reuses > Stats.Updates);
	Test->TestTrue(FString::Printf(TEXT("no hull goes more than %.0f cm into a solid (deepest %.2f cm)"), PenetrationTolerance, State->MaxDepth),
		State->MaxDepth <= PenetrationTolerance);
	Test->TestTrue(TEXT("units meet solids"), Contacts > 0);
	Test->TestTrue(TEXT("units drive past solids"), Passed >= FMath::CeilToInt(PassedShare * Units));
	return true;
}

/**
 * The obstacle scene of rts.Debug.Bench (#172) on L_Bench at a fixed 30 Hz: 240 M20s, as the bench spawns them among its
 * solids, drive the long scene's two legs, west and back. The narrow phase runs, units meet solids and pass them, most of
 * them keep driving rather than pile up, and no hull goes more than 1 cm into a solid.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchSolidsTest, "WorldDivide.Debug.Bench.Solids", TestFlags)

bool FRTSBenchSolidsTest::RunTest(const FString& Parameters)
{
	using namespace RTSBenchTests;
	if (!AutomationOpenMap(BenchMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), BenchMap));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSBenchRunSolids(this, MakeShared<FSolidsState>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

#endif
