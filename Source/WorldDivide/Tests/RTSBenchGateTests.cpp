#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Units/Debug/RTSBenchGate.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using namespace RTSBenchGate;

	/** A clean 30 s or 60 s sample of 240 moving units, as the bench writes it. */
	FRow MakeRow(int32 Count, const TCHAR* Scenario, int32 Sample, double GameMs, double Seconds = 60.0)
	{
		FRow Row;
		Row.Count = Count;
		Row.Scenario = Scenario;
		Row.Sample = Sample;
		Row.GameMs = GameMs;
		Row.P95GameMs = GameMs * 1.8;
		Row.MaxGameMs = 18.0;
		Row.FrameMs = GameMs + 1.3;
		Row.RenderMs = 4.2;
		Row.RHIMs = 0.8;
		Row.GPUMs = 4.3;
		Row.AvoidanceMs = 0.4;
		Row.LocomotionMs = 0.44;
		Row.P95LocomotionMs = 1.15;
		Row.SampleSeconds = Seconds;
		Row.Frames = FMath::RoundToInt(Seconds * 1000.0 / Row.FrameMs);
		Row.MaxFrameMs = 19.0;
		Row.StallFrames = 0;
		Row.FreezeFrames = 0;
		return Row;
	}

	/** A valid long run of a vehicle set: one sample at 1, 30 and 120, three at 240, idle and moving. */
	FRun MakeLongRun(double MoveGameMs, double IdleGameMs = 1.94)
	{
		FRun Run;
		Run.Set = TEXT("DA_TestM20Guardian");
		Run.Method = TEXT("long");
		Run.bComplete = true;
		Run.PlannedSampleSeconds = 60.0;
		Run.bHasValidity = true;
		Run.Build = TEXT("Development");
		Run.bHasEnvironment = true;
		Run.OtherCores = 0.2;
		Run.bVehicles = true;
		for (const int32 Count : { 1, 30, 120 })
		{
			Run.Rows.Add(MakeRow(Count, TEXT("idle"), 1, 1.1 + Count / 200.0));
			Run.Rows.Add(MakeRow(Count, TEXT("move"), 1, 1.2 + Count / 100.0));
		}
		for (int32 Sample = 1; Sample <= 3; ++Sample)
		{
			Run.Rows.Add(MakeRow(240, TEXT("idle"), Sample, IdleGameMs));
		}
		for (int32 Sample = 1; Sample <= 3; ++Sample)
		{
			Run.Rows.Add(MakeRow(240, TEXT("move"), Sample, MoveGameMs + 0.03 * Sample));
		}
		return Run;
	}

	/** A valid screen run: one 15 s sample of 240 moving units. */
	FRun MakeScreenRun(double GameMs)
	{
		FRun Run = MakeLongRun(GameMs);
		Run.Method = TEXT("screen");
		Run.PlannedSampleSeconds = 15.0;
		Run.Rows = { MakeRow(240, TEXT("move"), 1, GameMs, 15.0) };
		return Run;
	}

	FUnitInfo MakeUnit(const TCHAR* Name, EUnitClass Class, int64 Triangles, double FootprintM2, std::initializer_list<const TCHAR*> Packages = {})
	{
		FUnitInfo Unit;
		Unit.Name = Name;
		Unit.Class = Class;
		Unit.Parts = 3;
		Unit.Triangles = Triangles;
		Unit.FootprintM2 = FootprintM2;
		Unit.Packages.Add(FString::Printf(TEXT("/Game/Data/Units/%s"), Name));
		for (const TCHAR* Package : Packages)
		{
			Unit.Packages.Add(Package);
		}
		return Unit;
	}

	/** Today's roster, with made-up mesh figures. */
	TArray<FUnitInfo> MakeRoster()
	{
		return {
			MakeUnit(TEXT("DA_TestAbrams"), EUnitClass::Tracked, 60000, 37.0, { TEXT("/Game/Art/Abrams/SM_Abrams") }),
			MakeUnit(TEXT("DA_TestBuilder"), EUnitClass::Wheeled, 4000, 10.0),
			MakeUnit(TEXT("DA_TestFlakTruck"), EUnitClass::Wheeled, 9000, 11.0, { TEXT("/Game/Art/Truck/SM_Truck") }),
			MakeUnit(TEXT("DA_TestGrenadier"), EUnitClass::Infantry, 1200, 0.3, { TEXT("/Game/Art/Infantry/SM_Soldier") }),
			MakeUnit(TEXT("DA_TestHarvester"), EUnitClass::Wheeled, 9000, 25.0, { TEXT("/Game/Art/Truck/SM_Truck") }),
			MakeUnit(TEXT("DA_TestInfantry"), EUnitClass::Infantry, 1200, 0.3, { TEXT("/Game/Art/Infantry/SM_Soldier") }),
			MakeUnit(TEXT("DA_TestJet"), EUnitClass::Air, 4000, 70.0),
			MakeUnit(TEXT("DA_TestM20Guardian"), EUnitClass::Tracked, 89000, 25.0, { TEXT("/Game/Art/M20/SM_M20Hull") }),
			MakeUnit(TEXT("DA_TestTank"), EUnitClass::Tracked, 500, 38.0),
		};
	}

	bool HasFlag(const TArray<FString>& Flags, const TCHAR* Part)
	{
		return Flags.ContainsByPredicate([Part](const FString& Flag) { return Flag.Contains(Part); });
	}
}

/**
 * #171: a run disturbed by other load on the machine, or cut short, is not a measurement. #167's case, 66 frames in a 30 s
 * sample, must be caught; a real regression whose game thread explains the slower frames must not be taken for contamination.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchGateContaminationTest, "WorldDivide.Debug.Bench.Gate.Contamination", TestFlags)

bool FRTSBenchGateContaminationTest::RunTest(const FString& Parameters)
{
	const FRow Clean = MakeRow(240, TEXT("move"), 1, 4.2, 30.0);
	TestTrue(TEXT("a clean sample is valid"), CheckRow(Clean, 30.0).IsEmpty());

	FRow Starved = Clean;
	Starved.Frames = 66;
	Starved.FrameMs = 30000.0 / 66;
	TestTrue(TEXT("66 frames in 30 s are too few"), HasFlag(CheckRow(Starved, 30.0), TEXT("only 66 frames")));

	FRow Short = Clean;
	Short.SampleSeconds = 20.0;
	TestTrue(TEXT("a sample that stopped after 20 of 30 s is incomplete"), HasFlag(CheckRow(Short, 30.0), TEXT("incomplete")));
	TestTrue(TEXT("29.6 of 30 s is complete"), [&] { FRow Near = Clean; Near.SampleSeconds = 29.6; return CheckRow(Near, 30.0).IsEmpty(); }());

	FRow Empty = Clean;
	Empty.Frames = 0;
	TestTrue(TEXT("a sample without frames is invalid"), HasFlag(CheckRow(Empty, 30.0), TEXT("no frames")));

	FRow Frozen = Clean;
	Frozen.FreezeFrames = 1;
	Frozen.StallFrames = 1;
	Frozen.MaxFrameMs = 900.0;
	TestTrue(TEXT("one 900 ms freeze marks the sample"), HasFlag(CheckRow(Frozen, 30.0), TEXT("over 250 ms")));
	FRow Stalled = Clean;
	Stalled.StallFrames = Validity::MaxStalls + 1;
	TestTrue(TEXT("more stalls than allowed mark the sample"), HasFlag(CheckRow(Stalled, 30.0), TEXT("stalls")));
	Stalled.StallFrames = Validity::MaxStalls;
	TestTrue(TEXT("a few stalls are allowed"), CheckRow(Stalled, 30.0).IsEmpty());

	FRow Old = Clean;
	Old.SampleSeconds = -1.0;
	Old.StallFrames = -1;
	Old.FreezeFrames = -1;
	Old.MaxFrameMs = -1.0;
	TestTrue(TEXT("a row from before #171 checks what it has"), CheckRow(Old, 30.0).IsEmpty());

	TArray<FRow> Agreeing = MakeLongRun(4.2).Rows;
	TestTrue(TEXT("a clean long run is valid"), CheckRun(Agreeing, 60.0).IsEmpty());
	TArray<FRow> Disagreeing = Agreeing;
	Disagreeing.Last().GameMs += 0.9;
	TestTrue(TEXT("samples of one scenario 0.9 ms apart mark the run"), HasFlag(CheckRun(Disagreeing, 60.0), TEXT("samples disagree")));
	TestTrue(TEXT("a run without rows is invalid"), HasFlag(CheckRun({}, 60.0), TEXT("no rows")));

	TestTrue(TEXT("a quiet machine is fine"), CheckEnvironment(0.3, false).IsEmpty());
	TestTrue(TEXT("another game beside the bench marks it"), HasFlag(CheckEnvironment(3.5, false), TEXT("other processes")));
	TestTrue(TEXT("shader compilation marks it"), HasFlag(CheckEnvironment(0.1, true), TEXT("shaders")));

	// GPU contention: the frame doubles, but none of the game's own times does.
	const TArray<FRow> Base = { MakeRow(240, TEXT("move"), 1, 4.2) };
	FRow Contended = Base[0];
	Contended.FrameMs = Base[0].FrameMs * 2.5;
	Contended.Frames /= 2;
	TestTrue(TEXT("a slowdown the game's times do not explain is contamination"),
		HasFlag(CheckAgainstBaseline({ Contended }, Base), TEXT("explain")));
	FRow Regressed = Base[0];
	Regressed.GameMs = 9.0;
	Regressed.FrameMs = 10.3;
	Regressed.Frames /= 2;
	TestTrue(TEXT("a regression the game thread explains is not contamination"), CheckAgainstBaseline({ Regressed }, Base).IsEmpty());
	return true;
}

/** #171: PASS, FAIL and INCONCLUSIVE of the long method, the screen's escalation, and the overall verdict. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchGateVerdictTest, "WorldDivide.Debug.Bench.Gate.Verdict", TestFlags)

bool FRTSBenchGateVerdictTest::RunTest(const FString& Parameters)
{
	const FRun Base = MakeLongRun(4.17);

	const FSetVerdict Pass = JudgeLong(MakeLongRun(4.21), &Base, {});
	TestTrue(TEXT("within the spread and the budgets: pass"), Pass.Verdict == EVerdict::Pass);
	TestTrue(TEXT("a pass names its checks"), HasFlag(Pass.Notes, TEXT("locomotion p95")));

	const FSetVerdict Regressed = JudgeLong(MakeLongRun(4.9), &Base, {});
	TestTrue(TEXT("+0.7 ms game thread on a valid run: fail"), Regressed.Verdict == EVerdict::Fail);
	TestTrue(TEXT("the failure names the scenario"), HasFlag(Regressed.Reasons, TEXT("240 move: game thread")));
	TestFalse(TEXT("a fail says what to run next"), Regressed.Next.IsEmpty());

	FRun OverBudget = MakeLongRun(4.2);
	for (FRow& Row : OverBudget.Rows)
	{
		Row.P95LocomotionMs = Row.Count == 240 && Row.Scenario == TEXT("move") ? 2.3 : Row.P95LocomotionMs;
	}
	TestTrue(TEXT("locomotion p95 over 2.0 ms fails even without a baseline"), JudgeLong(OverBudget, nullptr, {}).Verdict == EVerdict::Fail);
	FRun Infantry = OverBudget;
	Infantry.bVehicles = false;
	TestTrue(TEXT("the locomotion budget is for vehicles"), JudgeLong(Infantry, &Base, {}).Verdict == EVerdict::Pass);
	FRun Hitch = MakeLongRun(4.2);
	Hitch.Rows.Last().MaxGameMs = 35.0;
	TestTrue(TEXT("a 35 ms frame on the move order fails"), HasFlag(JudgeLong(Hitch, &Base, {}).Reasons, TEXT("slowest game-thread frame")));
	FRun Crowded = MakeLongRun(4.2);
	for (FRow& Row : Crowded.Rows)
	{
		Row.AvoidanceMs = Row.Count == 240 ? 1.2 : Row.AvoidanceMs;
	}
	TestTrue(TEXT("avoidance over 1.0 ms fails"), JudgeLong(Crowded, &Base, {}).Verdict == EVerdict::Fail);

	FRun Contaminated = MakeLongRun(9.0);
	Contaminated.OtherCores = 3.0;
	const FSetVerdict Busy = JudgeLong(Contaminated, &Base, {});
	TestTrue(TEXT("a slow run on a busy machine is inconclusive, not a fail"), Busy.Verdict == EVerdict::Inconclusive);
	TestTrue(TEXT("it says to run again"), Busy.Next.Contains(TEXT("again")));

	FRun Traced = MakeLongRun(4.2);
	Traced.bTrace = true;
	TestTrue(TEXT("a traced run is inconclusive"), JudgeLong(Traced, &Base, {}).Verdict == EVerdict::Inconclusive);
	FRun Debug = MakeLongRun(4.2);
	Debug.Build = TEXT("Debug");
	TestTrue(TEXT("a Debug build is inconclusive"), JudgeLong(Debug, &Base, {}).Verdict == EVerdict::Inconclusive);
	FRun Unwatched = MakeLongRun(4.2);
	Unwatched.bHasEnvironment = false;
	TestTrue(TEXT("a run without the machine's load is inconclusive"), JudgeLong(Unwatched, &Base, {}).Verdict == EVerdict::Inconclusive);
	FRun Interrupted = MakeLongRun(4.2);
	Interrupted.bComplete = false;
	TestTrue(TEXT("an interrupted run is inconclusive"), JudgeLong(Interrupted, &Base, {}).Verdict == EVerdict::Inconclusive);
	FRun Flagged = MakeLongRun(4.2);
	Flagged.Flags.Add(TEXT("240 move sample 2: only 66 frames in 30.0 s"));
	TestTrue(TEXT("a run with its own flags is inconclusive"), JudgeLong(Flagged, &Base, {}).Verdict == EVerdict::Inconclusive);

	TestTrue(TEXT("no baseline: inconclusive"), JudgeLong(MakeLongRun(4.2), nullptr, {}).Verdict == EVerdict::Inconclusive);
	TestTrue(TEXT("an incompatible baseline: inconclusive"),
		JudgeLong(MakeLongRun(4.2), &Base, { TEXT("machine.driver: 617.42 -> 620.10") }).Verdict == EVerdict::Inconclusive);
	FRun OldBase = Base;
	OldBase.bHasValidity = false;
	TestTrue(TEXT("a baseline without a validity record: inconclusive"), JudgeLong(MakeLongRun(4.2), &OldBase, {}).Verdict == EVerdict::Inconclusive);
	FRun DirtyBase = Base;
	DirtyBase.OtherCores = 4.0;
	TestTrue(TEXT("a contaminated baseline is never read against"), JudgeLong(MakeLongRun(4.2), &DirtyBase, {}).Verdict == EVerdict::Inconclusive);

	const FRun ScreenBase = MakeScreenRun(4.4);
	TestTrue(TEXT("screen within the threshold: clear"), JudgeScreen(MakeScreenRun(4.5), &ScreenBase, {}).Verdict == EVerdict::Pass);
	const FSetVerdict Suspicious = JudgeScreen(MakeScreenRun(5.2), &ScreenBase, {});
	TestTrue(TEXT("screen +0.8 ms escalates"), Suspicious.bEscalate && Suspicious.Verdict == EVerdict::Inconclusive);
	TestTrue(TEXT("to the long method"), Suspicious.Next.Contains(TEXT("long method")));
	TestTrue(TEXT("the screen never fails, however slow"), JudgeScreen(MakeScreenRun(40.0), &ScreenBase, {}).bEscalate);
	TestTrue(TEXT("a screen without a baseline escalates"), JudgeScreen(MakeScreenRun(4.4), nullptr, {}).bEscalate);
	FRun NearBudget = MakeScreenRun(4.4);
	NearBudget.Rows[0].P95LocomotionMs = 1.7;
	TestTrue(TEXT("screen locomotion p95 at 85 % of the budget escalates"), JudgeScreen(NearBudget, &ScreenBase, {}).bEscalate);
	FRun BusyScreen = MakeScreenRun(4.4);
	BusyScreen.bShaderWorkers = true;
	const FSetVerdict Rerun = JudgeScreen(BusyScreen, &ScreenBase, {});
	TestTrue(TEXT("a contaminated screen runs again instead of escalating"), !Rerun.bEscalate && Rerun.Verdict == EVerdict::Inconclusive);

	FSetVerdict P;
	P.Verdict = EVerdict::Pass;
	FSetVerdict I;
	FSetVerdict F;
	F.Verdict = EVerdict::Fail;
	TestTrue(TEXT("nothing to measure passes"), Combine({}) == EVerdict::Pass);
	TestTrue(TEXT("an inconclusive set is never a pass"), Combine({ P, I, P }) == EVerdict::Inconclusive);
	TestTrue(TEXT("a fail wins"), Combine({ P, I, F }) == EVerdict::Fail);
	TestTrue(TEXT("all pass"), Combine({ P, P }) == EVerdict::Pass);
	return true;
}

/** #171: which changes ask for which sets, a shared change for more than one unit's data. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchGateChangesTest, "WorldDivide.Debug.Bench.Gate.Changes", TestFlags)

bool FRTSBenchGateChangesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("docs"), ClassifyPath(TEXT("docs/tech/scale/PERFORMANCE_RESULTS.md")).Scope == EScope::None);
	TestTrue(TEXT("tests"), ClassifyPath(TEXT("Source/WorldDivide/Tests/RTSBenchTests.cpp")).Scope == EScope::None);
	TestTrue(TEXT("the gate's own judge"), ClassifyPath(TEXT("Source/WorldDivide/Units/Debug/RTSBenchGate.cpp")).Scope == EScope::None);
	TestTrue(TEXT("another map"), ClassifyPath(TEXT("Content/Maps/L_TestMap.umap")).Scope == EScope::None);
	TestTrue(TEXT("tools"), ClassifyPath(TEXT("Tools/Editor/setup_testmap.py")).Scope == EScope::None);
	const FChange Unit = ClassifyPath(TEXT("Content/Data/Units/DA_TestTank.uasset"));
	TestTrue(TEXT("a unit's data"), Unit.Scope == EScope::Unit && Unit.Name == TEXT("DA_TestTank"));
	const FChange Mesh = ClassifyPath(TEXT("Content\\Art\\M20\\SM_M20Hull.uasset"));
	TestTrue(TEXT("a mesh is content by package"), Mesh.Scope == EScope::Content && Mesh.Name == TEXT("/Game/Art/M20/SM_M20Hull"));
	TestTrue(TEXT("movement code"), ClassifyPath(TEXT("Source/WorldDivide/Units/RTSUnitMovement.cpp")).Scope == EScope::Broad);
	TestTrue(TEXT("navigation code"), ClassifyPath(TEXT("Source/WorldDivide/Navigation/RTSHullClearance.cpp")).Scope == EScope::Broad);
	TestTrue(TEXT("rendering config"), ClassifyPath(TEXT("Config/DefaultEngine.ini")).Scope == EScope::Broad);
	TestTrue(TEXT("the HUD's layouts"), ClassifyPath(TEXT("UI/Layouts/HUD.layout.json")).Scope == EScope::Broad);
	TestTrue(TEXT("a plugin"), ClassifyPath(TEXT("Plugins/WorldDivideMCP/Source/X.cpp")).Scope == EScope::Broad);
	TestTrue(TEXT("the bench's own code"), ClassifyPath(TEXT("Source/WorldDivide/Units/Debug/RTSBenchCommand.cpp")).bBenchMethod);
	TestTrue(TEXT("the bench's map"), ClassifyPath(TEXT("Content/Maps/L_Bench.umap")).bBenchMethod);

	const TArray<FUnitInfo> Roster = MakeRoster();
	const FPlan Nothing = MakePlan(Roster, { TEXT("docs/x.md"), TEXT("Source/WorldDivide/Tests/X.cpp") }, false);
	TestTrue(TEXT("docs and tests bench nothing"), Nothing.Screen.IsEmpty() && Nothing.Long.IsEmpty() && !Nothing.bBroad);

	const FPlan Tank = MakePlan(Roster, { TEXT("Content/Data/Units/DA_TestTank.uasset") }, false);
	TestTrue(TEXT("a representative's data: its screen"), Tank.Screen == TArray<FString>{ TEXT("DA_TestTank") });
	TestTrue(TEXT("its long run"), Tank.Long.Contains(TEXT("DA_TestTank")));
	TestTrue(TEXT("and the mixed set it is in"), Tank.Long.Contains(Tank.Mixed) == Tank.Mixed.Contains(TEXT("DA_TestTank")));
	TestFalse(TEXT("one unit's data is not broad"), Tank.bBroad);

	const FPlan Builder = MakePlan(Roster, { TEXT("Content/Data/Units/DA_TestBuilder.uasset") }, false);
	TestTrue(TEXT("another unit's data: only its screen, unless it escalates"),
		Builder.Screen == TArray<FString>{ TEXT("DA_TestBuilder") } && !Builder.Long.Contains(TEXT("DA_TestBuilder")));

	const FPlan Truck = MakePlan(Roster, { TEXT("Content/Art/Truck/SM_Truck.uasset") }, false);
	TestTrue(TEXT("a shared mesh: every unit made of it"),
		Truck.Screen == TArray<FString>({ TEXT("DA_TestFlakTruck"), TEXT("DA_TestHarvester") }));

	const FPlan Unused = MakePlan(Roster, { TEXT("Content/Materials/M_TeamColour.uasset") }, false);
	TestTrue(TEXT("content no unit lists could be loaded by code: broad"), Unused.bBroad);

	const FPlan Code = MakePlan(Roster, { TEXT("docs/x.md"), TEXT("Source/WorldDivide/Units/RTSUnitMovement.cpp") }, false);
	TestTrue(TEXT("shared code: broad"), Code.bBroad);
	TestEqual(TEXT("every unit screened"), Code.Screen.Num(), Roster.Num());
	TestEqual(TEXT("every representative and the mix benched long"), Code.Long.Num(), Code.Representatives.Num() + 1);
	TestTrue(TEXT("broader than one unit's data"), Code.Screen.Num() > Tank.Screen.Num() && Code.Long.Num() > Tank.Long.Num());
	TestFalse(TEXT("movement code is not the bench's method"), Code.bBenchMethod);
	TestTrue(TEXT("every change is named"), Code.Changes.Num() == 2);

	TestTrue(TEXT("a nightly run is broad without changes"), MakePlan(Roster, {}, true).bBroad);
	TestTrue(TEXT("the bench's own code asks for new baselines"), MakePlan(Roster, { TEXT("Source/WorldDivide/Units/Debug/RTSBenchPlan.h") }, false).bBenchMethod);
	return true;
}

/** #171: the representatives cover each class, its costliest unit and its sizes; the pinned units stay for history. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchGateRepresentativesTest, "WorldDivide.Debug.Bench.Gate.Representatives", TestFlags)

bool FRTSBenchGateRepresentativesTest::RunTest(const FString& Parameters)
{
	TArray<FUnitInfo> Roster = MakeRoster();
	TMap<FString, FString> Reasons;
	const TArray<FString> Representatives = SelectRepresentatives(Roster, &Reasons);
	for (const TCHAR* Pinned : { TEXT("DA_TestM20Guardian"), TEXT("DA_TestTank") })
	{
		TestTrue(FString::Printf(TEXT("%s is pinned"), Pinned), Representatives.Contains(Pinned));
	}
	for (const EUnitClass Class : { EUnitClass::Tracked, EUnitClass::Wheeled, EUnitClass::Infantry, EUnitClass::Air })
	{
		TestTrue(FString::Printf(TEXT("a %s representative"), LexUnitClass(Class)), Roster.ContainsByPredicate([&](const FUnitInfo& Unit)
		{
			return Unit.Class == Class && Representatives.Contains(Unit.Name);
		}));
	}
	TestTrue(TEXT("each has a reason"), Reasons.Num() == Representatives.Num());
	TestTrue(TEXT("the wheeled sizes: the 11 m2 truck and the 25 m2 harvester"),
		Representatives.Contains(TEXT("DA_TestFlakTruck")) && Representatives.Contains(TEXT("DA_TestHarvester")));
	TestFalse(TEXT("the smaller twin of a size is left out"), Representatives.Contains(TEXT("DA_TestBuilder")));
	TestTrue(TEXT("fewer than the roster"), Representatives.Num() < Roster.Num());

	// With every unit screened, the screen ranks a class instead of its meshes.
	for (FUnitInfo& Unit : Roster)
	{
		Unit.ScreenGameMs = Unit.Name == TEXT("DA_TestBuilder") ? 9.0 : 3.0;
	}
	TestTrue(TEXT("the screen's costliest wheeled unit is in"), SelectRepresentatives(Roster).Contains(TEXT("DA_TestBuilder")));

	const FString Mixed = MakeMixedSet(Roster, Representatives);
	TArray<FString> Members;
	Mixed.ParseIntoArray(Members, TEXT("+"));
	TestEqual(TEXT("the mix has one unit of each class"), Members.Num(), 4);
	TestTrue(TEXT("its tracked unit is pinned"), Members[0] == TEXT("DA_TestM20Guardian"));
	TestTrue(TEXT("no mix of one class"), MakeMixedSet({ MakeUnit(TEXT("A"), EUnitClass::Tracked, 1, 1.0) }, { TEXT("A") }).IsEmpty());

	// A roster of over a hundred: the representatives stay a few per class.
	TArray<FUnitInfo> Large;
	for (int32 Index = 0; Index < 120; ++Index)
	{
		Large.Add(MakeUnit(*FString::Printf(TEXT("DA_Unit%03d"), Index), static_cast<EUnitClass>(Index % 4), 1000 + Index * 37 % 500, (Index * 7) % 40 + 0.5));
	}
	const int32 Count = SelectRepresentatives(Large).Num();
	TestTrue(FString::Printf(TEXT("120 units give %d representatives, at most one per class and size"), Count), Count >= 4 && Count <= 12);
	return true;
}

/** #171: bench.csv and bench.json read back, old and new, and what a baseline must match. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchGateReadTest, "WorldDivide.Debug.Bench.Gate.Read", TestFlags)

bool FRTSBenchGateReadTest::RunTest(const FString& Parameters)
{
	const FString OldCsv = TEXT("count,scenario,sample,frames,units,frame_ms,game_ms,p95_game_ms,max_game_ms,render_ms,rhi_ms,gpu_ms,draw_calls,triangles,avoidance_ms,max_avoidance_ms,search_ms,locomotion_ms,p95_locomotion_ms,max_locomotion_ms\n")
		TEXT("240,move,1,10958,240,5.48,4.10,7.83,18.03,4.19,0.81,4.30,3641,45817407,0.342,1.738,0.039,0.435,1.152,2.048\n");
	TArray<FRow> Rows;
	TestTrue(TEXT("a bench.csv from before #171 parses"), FRow::ParseCsv(OldCsv, Rows) && Rows.Num() == 1);
	TestEqual(TEXT("its game thread"), Rows[0].GameMs, 4.10);
	TestEqual(TEXT("no stall column reads -1"), Rows[0].StallFrames, -1);
	TestFalse(TEXT("a CSV without the columns is refused"), FRow::ParseCsv(TEXT("a,b\n1,2\n"), Rows));

	const FString NewCsv = TEXT("count,scenario,sample,frames,frame_ms,game_ms,p95_game_ms,max_game_ms,render_ms,rhi_ms,gpu_ms,avoidance_ms,locomotion_ms,p95_locomotion_ms,sample_s,max_frame_ms,stall_frames,freeze_frames\n")
		TEXT("240,move,1,66,454.5,4.1,7.8,18,4.2,0.8,4.3,0.3,0.4,1.1,30.01,900,5,2\n");
	const FString Json = TEXT("{\"method\":{\"name\":\"quick\",\"sample_s\":30},\"machine\":{\"build\":\"Development\"},")
		TEXT("\"run\":{\"valid\":false,\"flags\":[\"240 move sample 1: only 66 frames\"],\"trace\":false,\"vehicles\":true},")
		TEXT("\"environment\":{\"other_cores\":2.5,\"shader_workers\":false}}");
	const FRun Run = ReadRun(TEXT("DA_TestTank"), NewCsv, Json);
	TestTrue(TEXT("complete"), Run.bComplete);
	TestEqual(TEXT("the planned sample"), Run.PlannedSampleSeconds, 30.0);
	TestTrue(TEXT("its flags"), Run.bHasValidity && Run.Flags.Num() == 1);
	TestTrue(TEXT("its environment"), Run.bHasEnvironment && Run.OtherCores == 2.5);
	TestEqual(TEXT("its freezes"), Run.Rows[0].FreezeFrames, 2);
	TestFalse(TEXT("a bench.json that does not parse is no result"), ReadRun(TEXT("X"), NewCsv, TEXT("{")).bComplete);
	TestTrue(TEXT("and the judge says so"), JudgeLong(ReadRun(TEXT("X"), NewCsv, TEXT("{")), nullptr, {}).Verdict == EVerdict::Inconclusive);

	const FString Base = TEXT("{\"method\":{\"name\":\"long\",\"version\":1},\"scene\":{\"map\":\"L_Bench\"},\"machine\":{\"gpu\":\"RTX 4090\",\"driver\":\"617.42\"},\"run\":{\"engine\":\"5.8.0\"}}");
	TestTrue(TEXT("the same run matches"), CompareMetadata(Base, Base).IsEmpty());
	const FString Driver = Base.Replace(TEXT("617.42"), TEXT("620.10"));
	TestTrue(TEXT("a new driver does not"), HasFlag(CompareMetadata(Base, Driver), TEXT("machine.driver")));
	const FString Engine = Base.Replace(TEXT("5.8.0"), TEXT("5.8.1"));
	TestTrue(TEXT("a new engine does not"), HasFlag(CompareMetadata(Base, Engine), TEXT("run.engine")));
	const FString Shorter = Base.Replace(TEXT("\"version\":1"), TEXT("\"version\":1,\"sample_s\":15"));
	TestTrue(TEXT("a study's sample length does not"), HasFlag(CompareMetadata(Base, Shorter), TEXT("method.sample_s")));
	const FString Old = TEXT("{\"method\":{\"name\":\"long\",\"version\":1},\"scene\":{\"map\":\"L_Bench\"},\"machine\":{\"gpu\":\"RTX 4090\",\"driver\":\"617.42\"}}");
	TestTrue(TEXT("a baseline from before #171 is not refused for its missing run section"), CompareMetadata(Old, Base).IsEmpty());
	return true;
}

#endif
