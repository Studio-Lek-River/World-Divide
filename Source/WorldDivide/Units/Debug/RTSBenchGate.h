#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

/**
 * The performance gate for a full unit roster (#171): what makes a bench run valid, how a run is judged against its
 * baseline and the approved budgets, and which unit sets a change has to bench. Pure logic, so the tests can feed it
 * synthetic runs; rts.Debug.Bench checks its own rows with it, and the RTSBenchGate commandlet (ue.ps1 -Task gate) plans
 * and judges with it. See docs/tech/scale/PERFORMANCE_RESULTS.md, "Performance gate for a full roster".
 */
namespace RTSBenchGate
{
	enum class EVerdict : uint8
	{
		/** Every required measurement is valid and within its approved limits. */
		Pass,
		/** A valid measurement violates a limit. */
		Fail,
		/** Not enough evidence: a contaminated, incomplete or missing run, or no compatible baseline. Never a pass. */
		Inconclusive,
	};

	const TCHAR* LexVerdict(EVerdict Verdict);

	/** The approved budgets the gate reads, unchanged from PERFORMANCE_RESULTS.md. */
	namespace Budgets
	{
		/** A game-thread difference to the baseline beyond the run-to-run spread is a regression. */
		constexpr double GameThreadSpreadMs = 0.6;
		/** #138: 240 moving vehicles. */
		constexpr double LocomotionMeanMs = 1.0;
		constexpr double LocomotionP95Ms = 2.0;
		/** No server frame over 33 ms on a 240-unit move order. */
		constexpr double MoveOrderFrameMs = 33.0;
		/** Avoidance mean under 1.0 ms. */
		constexpr double AvoidanceMeanMs = 1.0;
		/** The count the budgets are set for. */
		constexpr int32 GateCount = 240;
	}

	/** What makes a run unfit to measure with (contamination), not what makes it slow. */
	namespace Validity
	{
		/** A timed sample must last this share of its planned length. */
		constexpr double MinSampleShare = 0.98;
		/** A sample's mean frame rate must reach this; #167's disturbed run made 66 frames in 30 s (2.2 fps). */
		constexpr double MinFramesPerSecond = 10.0;
		/** A frame this long is a stall; more than MaxStalls of them in a sample, or any freeze, mark it contaminated. */
		constexpr double StallFrameMs = 100.0;
		constexpr int32 MaxStalls = 3;
		constexpr double FreezeFrameMs = 250.0;
		/** The samples of one scenario must agree this well: a code change moves them all alike, outside load does not. */
		constexpr double MaxSampleSpreadMs = 0.6;
		/** Against the baseline: a frame this many times slower, ... */
		constexpr double SlowdownFactor = 1.5;
		/** ... with less than this share of the slowdown in the game, render, RHI or GPU time, was lost outside the game. */
		constexpr double MinExplainedShare = 0.5;
		/** Mean cores other processes used while a set ran (ue.ps1 samples them). Kept equal in ue.ps1 (-SaveBaseline). */
		constexpr double MaxOtherCores = 1.0;
	}

	/**
	 * The screen escalates a unit to the long method when its game thread is this much over its screen baseline: half the
	 * gate's 0.6 ms. The screen's measured run-to-run spread is 0.05 ms over five sessions (PERFORMANCE_RESULTS.md, #171); the
	 * margin above it covers what one day's series cannot show, such as the machine's state on another day.
	 */
	constexpr double ScreenThresholdMs = 0.3;
	/** ... or when a locomotion figure reaches this share of its budget. */
	constexpr double ScreenBudgetShare = 0.8;

	/** One row of bench.csv. Columns a run of before #171 lacks read as -1. */
	struct FRow
	{
		int32 Count = 0;
		FString Scenario;
		int32 Sample = 0;
		int32 Frames = 0;
		double FrameMs = 0.0;
		double GameMs = 0.0;
		double P95GameMs = 0.0;
		double MaxGameMs = 0.0;
		double RenderMs = 0.0;
		double RHIMs = 0.0;
		double GPUMs = 0.0;
		double AvoidanceMs = 0.0;
		double LocomotionMs = 0.0;
		double P95LocomotionMs = 0.0;
		double MaxLocomotionMs = 0.0;
		double SampleSeconds = -1.0;
		double MaxFrameMs = -1.0;
		int32 StallFrames = -1;
		int32 FreezeFrames = -1;

		/** The rows of a bench.csv; false when its header lacks a column every run has. */
		static bool ParseCsv(const FString& Csv, TArray<FRow>& OutRows);
	};

	/** The flags that make one sample row unfit: incomplete, too few frames, stalls. PlannedSeconds is 0 for a frame-count sample. */
	TArray<FString> CheckRow(const FRow& Row, double PlannedSeconds);

	/** CheckRow of every row, and the agreement of each scenario's samples. */
	TArray<FString> CheckRun(const TArray<FRow>& Rows, double PlannedSeconds);

	/** The flags of the machine while the set ran. */
	TArray<FString> CheckEnvironment(double OtherCores, bool bShaderWorkers);

	/** Rows whose frame slowed down far more than the game's own times explain, against the baseline's same row. */
	TArray<FString> CheckAgainstBaseline(const TArray<FRow>& Now, const TArray<FRow>& Base);

	/** One set's run, as the judge reads it back from bench.csv and bench.json. */
	struct FRun
	{
		FString Set;
		FString Method;
		/** Whether the run gave a result at all. */
		bool bComplete = false;
		double PlannedSampleSeconds = 0.0;
		/** The run section of bench.json (#171); a run or baseline without it has no validity record. */
		bool bHasValidity = false;
		TArray<FString> Flags;
		bool bTrace = false;
		FString Build;
		bool bHasEnvironment = false;
		double OtherCores = 0.0;
		bool bShaderWorkers = false;
		/** Whether the set holds a tracked or wheeled unit, which the locomotion budget is for. */
		bool bVehicles = false;
		TArray<FRow> Rows;
	};

	/** A run from the text of its bench.csv and bench.json (and the environment section ue.ps1 adds); bComplete when both parse. */
	FRun ReadRun(const FString& Set, const FString& Csv, const FString& Json);

	/** The run in Dir (bench.csv and bench.json), incomplete when either is missing. */
	FRun LoadRun(const FString& Set, const FString& Dir);

	/**
	 * How two bench.json differ where a baseline must match its run, as "section.field: baseline -> now": the method, the
	 * scene, the machine, and the engine both ran on.
	 */
	TArray<FString> CompareMetadata(const FString& BaseJson, const FString& NowJson);

	struct FSetVerdict
	{
		FString Set;
		FString Method;
		EVerdict Verdict = EVerdict::Inconclusive;
		/** The screen sends the set to the long method. */
		bool bEscalate = false;
		/** Why it is not a pass; empty for a pass. */
		TArray<FString> Reasons;
		/** The checks it passed and what it was read against. */
		TArray<FString> Notes;
		/** What to run next. */
		FString Next;
	};

	/**
	 * The screen of one set: Pass when valid and clear, Inconclusive with bEscalate when it needs the long method,
	 * Inconclusive without when it must be run again. Never Fail: the screen method is no gate evidence.
	 * Incompatible lists how its baseline differs (method, scene, machine, engine), empty when compatible or when Base is null.
	 */
	FSetVerdict JudgeScreen(const FRun& Now, const FRun* Base, const TArray<FString>& Incompatible);

	/** The long method of one set against the budgets and its baseline. */
	FSetVerdict JudgeLong(const FRun& Now, const FRun* Base, const TArray<FString>& Incompatible);

	/** Fail when any set fails, else Inconclusive when any is or a required set is missing, else Pass. */
	EVerdict Combine(const TArray<FSetVerdict>& Verdicts);

	/** What a changed file asks the bench to measure. */
	enum class EScope : uint8
	{
		/** Nothing the bench runs: docs, tests, tools, other maps. */
		None,
		/** One unit's data asset. */
		Unit,
		/** A content package: the units that depend on it, every unit when none does. */
		Content,
		/** Code, config, plugins, UI, or the bench itself: every unit. */
		Broad,
	};

	struct FChange
	{
		EScope Scope = EScope::None;
		/** The unit of a Unit change, the package (/Game/...) of a Content change. */
		FString Name;
		/** The bench's own method or scene changed: baselines need regenerating. */
		bool bBenchMethod = false;
	};

	/** The scope of one changed file, by its path from the repository root with '/' separators. */
	FChange ClassifyPath(const FString& RepoPath);

	enum class EUnitClass : uint8
	{
		Tracked,
		Wheeled,
		Infantry,
		Air,
	};

	const TCHAR* LexUnitClass(EUnitClass Class);

	/** A roster entry and what makes it cost: the RTSBenchGate commandlet reads them from the unit data. */
	struct FUnitInfo
	{
		FString Name;
		EUnitClass Class = EUnitClass::Infantry;
		/** Meshes the unit is built from: hull, turret, gun and parts. */
		int32 Parts = 0;
		int32 Wheels = 0;
		/** Triangles of those meshes' LOD0. */
		int64 Triangles = 0;
		double FootprintM2 = 0.0;
		/** The /Game packages it is made of, its own data asset included. */
		TSet<FString> Packages;
		/** The game thread of its last valid screen, or a negative value without one. */
		double ScreenGameMs = -1.0;
	};

	/** The units benched with the long method so history stays comparable. */
	const TArray<FString>& GetPinned();

	/** The size bucket of a footprint: 0 below 4 m2, 1 up to 20 m2, 2 above. */
	int32 GetSizeBucket(double FootprintM2);

	/**
	 * The representatives benched with the long method, in roster order: for each class its pinned units, its costliest by
	 * the screen (by LOD0 triangles times parts without one), and the costliest of each
	 * size bucket. A heuristic cover of the kinds of cost, not a guarantee for any single unit. OutReasons says why each is in.
	 */
	TArray<FString> SelectRepresentatives(const TArray<FUnitInfo>& Roster, TMap<FString, FString>* OutReasons = nullptr);

	/** One representative of each class (pinned first), joined by '+', the mixed set; empty with fewer than two classes. */
	FString MakeMixedSet(const TArray<FUnitInfo>& Roster, const TArray<FString>& Representatives);

	struct FPlan
	{
		/** A change needs every unit screened and every representative benched with the long method. */
		bool bBroad = false;
		bool bBenchMethod = false;
		TArray<FString> Representatives;
		FString Mixed;
		/** The sets of the screen and of the long method, each in roster order. */
		TArray<FString> Screen;
		TArray<FString> Long;
		/** Why each set is in, by set. */
		TMap<FString, FString> Reasons;
		/** The changed files and their scope, for the report. */
		TArray<FString> Changes;
	};

	/** The sets ChangedPaths require, or every set when bForceBroad (a milestone or nightly run). */
	FPlan MakePlan(const TArray<FUnitInfo>& Roster, const TArray<FString>& ChangedPaths, bool bForceBroad);
}

#endif
