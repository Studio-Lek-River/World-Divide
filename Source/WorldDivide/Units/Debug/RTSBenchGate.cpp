#include "Units/Debug/RTSBenchGate.h"

#if !UE_BUILD_SHIPPING

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace RTSBenchGate
{
	namespace
	{
		FString RowName(const FRow& Row)
		{
			return Row.Sample > 0 ? FString::Printf(TEXT("%d %s sample %d"), Row.Count, *Row.Scenario, Row.Sample)
				: FString::Printf(TEXT("%d %s"), Row.Count, *Row.Scenario);
		}

		const FRow* FindRow(const TArray<FRow>& Rows, const FRow& Like)
		{
			return Rows.FindByPredicate([&Like](const FRow& Row)
			{
				return Row.Count == Like.Count && Row.Scenario == Like.Scenario && Row.Sample == Like.Sample;
			});
		}

		/** The (count, scenario) pairs of Rows, in order. */
		TArray<TPair<int32, FString>> GetScenarios(const TArray<FRow>& Rows)
		{
			TArray<TPair<int32, FString>> Scenarios;
			for (const FRow& Row : Rows)
			{
				Scenarios.AddUnique(TPair<int32, FString>(Row.Count, Row.Scenario));
			}
			return Scenarios;
		}

		/** The mean of Field over the samples of one scenario; false without any. */
		bool MeanOf(const TArray<FRow>& Rows, int32 Count, const FString& Scenario, double FRow::*Field, double& OutMean)
		{
			double Sum = 0.0;
			int32 Num = 0;
			for (const FRow& Row : Rows)
			{
				if (Row.Count == Count && Row.Scenario == Scenario)
				{
					Sum += Row.*Field;
					++Num;
				}
			}
			OutMean = Num > 0 ? Sum / Num : 0.0;
			return Num > 0;
		}

		double MaxOf(const TArray<FRow>& Rows, int32 Count, const FString& Scenario, double FRow::*Field)
		{
			double Max = 0.0;
			for (const FRow& Row : Rows)
			{
				if (Row.Count == Count && Row.Scenario == Scenario)
				{
					Max = FMath::Max(Max, Row.*Field);
				}
			}
			return Max;
		}

		/** The flags that make Run unfit, whatever its numbers: its own validity record, a trace, the build, the machine. */
		TArray<FString> GetRunFlags(const FRun& Run)
		{
			TArray<FString> Flags = Run.Flags;
			if (!Run.bHasValidity)
			{
				Flags.Add(TEXT("no validity record (a run from before #171)"));
			}
			if (Run.bTrace)
			{
				Flags.Add(TEXT("an Unreal Insights trace was recording"));
			}
			if (!Run.Build.IsEmpty() && Run.Build != TEXT("Development"))
			{
				Flags.Add(FString::Printf(TEXT("a %s build, not Development"), *Run.Build));
			}
			if (Run.bHasEnvironment)
			{
				Flags.Append(CheckEnvironment(Run.OtherCores, Run.bShaderWorkers));
			}
			else
			{
				Flags.Add(TEXT("no record of the machine's other load"));
			}
			return Flags;
		}

		/** Why Base cannot be read against, empty when it can. */
		TArray<FString> GetBaselineProblems(const FRun* Base, const TArray<FString>& Incompatible)
		{
			TArray<FString> Problems;
			if (!Base)
			{
				Problems.Add(TEXT("no baseline"));
				return Problems;
			}
			for (const FString& Difference : Incompatible)
			{
				Problems.Add(TEXT("incompatible baseline: ") + Difference);
			}
			if (!Base->bComplete)
			{
				Problems.Add(TEXT("the baseline has no result"));
			}
			for (const FString& Flag : GetRunFlags(*Base))
			{
				Problems.Add(TEXT("the baseline is not valid: ") + Flag);
			}
			return Problems;
		}

		/** The count the budgets are read at: 240, or the largest count of a run made with other counts. */
		int32 GetBudgetCount(const TArray<FRow>& Rows)
		{
			int32 Largest = 0;
			for (const FRow& Row : Rows)
			{
				if (Row.Count == Budgets::GateCount)
				{
					return Budgets::GateCount;
				}
				Largest = FMath::Max(Largest, Row.Count);
			}
			return Largest;
		}

		FString Ms(double Value)
		{
			return FString::Printf(TEXT("%.2f ms"), Value);
		}

		/** The budget checks of a valid run, as failures (or, for the screen, reasons to escalate) and passed notes. */
		void CheckBudgets(const FRun& Run, double Share, TArray<FString>& OutOver, TArray<FString>& OutNotes)
		{
			const int32 Count = GetBudgetCount(Run.Rows);
			if (Count != Budgets::GateCount)
			{
				OutOver.Add(FString::Printf(TEXT("no %d-unit rows, so the budgets cannot be read"), Budgets::GateCount));
				return;
			}
			auto Check = [&](const FString& What, double Value, double Limit)
			{
				const FString Line = FString::Printf(TEXT("%s %s against %s%s"), *What, *Ms(Value), *Ms(Limit * Share),
					Share < 1.0 ? *FString::Printf(TEXT(" (%.0f%% of the %s budget)"), Share * 100.0, *Ms(Limit)) : TEXT(""));
				(Value > Limit * Share ? OutOver : OutNotes).Add(Line);
			};
			const FString Move = TEXT("move");
			double Mean = 0.0;
			if (MeanOf(Run.Rows, Count, Move, &FRow::GameMs, Mean))
			{
				// The order's own frame is in the move's slowest frame.
				Check(FString::Printf(TEXT("%d move: slowest game-thread frame"), Count), MaxOf(Run.Rows, Count, Move, &FRow::MaxGameMs),
					Budgets::MoveOrderFrameMs);
				if (Run.bVehicles)
				{
					MeanOf(Run.Rows, Count, Move, &FRow::LocomotionMs, Mean);
					Check(FString::Printf(TEXT("%d move: locomotion mean"), Count), Mean, Budgets::LocomotionMeanMs);
					MeanOf(Run.Rows, Count, Move, &FRow::P95LocomotionMs, Mean);
					Check(FString::Printf(TEXT("%d move: locomotion p95"), Count), Mean, Budgets::LocomotionP95Ms);
				}
			}
			else
			{
				OutOver.Add(FString::Printf(TEXT("no %d move rows, so the budgets cannot be read"), Count));
			}
			for (const TPair<int32, FString>& Scenario : GetScenarios(Run.Rows))
			{
				if (Scenario.Key == Count && MeanOf(Run.Rows, Count, Scenario.Value, &FRow::AvoidanceMs, Mean))
				{
					Check(FString::Printf(TEXT("%d %s: avoidance mean"), Count, *Scenario.Value), Mean, Budgets::AvoidanceMeanMs);
				}
			}
		}

		/** Each scenario's game-thread mean against the baseline's, as lines beyond Threshold and lines within it. */
		void CheckGameThread(const FRun& Now, const FRun& Base, double Threshold, TArray<FString>& OutOver, TArray<FString>& OutNotes)
		{
			for (const TPair<int32, FString>& Scenario : GetScenarios(Now.Rows))
			{
				double After = 0.0;
				double Before = 0.0;
				MeanOf(Now.Rows, Scenario.Key, Scenario.Value, &FRow::GameMs, After);
				if (!MeanOf(Base.Rows, Scenario.Key, Scenario.Value, &FRow::GameMs, Before))
				{
					OutOver.Add(FString::Printf(TEXT("%d %s: the baseline has no such rows"), Scenario.Key, *Scenario.Value));
					continue;
				}
				const FString Line = FString::Printf(TEXT("%d %s: game thread %.2f -> %.2f ms (%+.2f, limit +%.2f)"), Scenario.Key,
					*Scenario.Value, Before, After, After - Before, Threshold);
				(After - Before > Threshold ? OutOver : OutNotes).Add(Line);
			}
		}
	}

	const TCHAR* LexVerdict(EVerdict Verdict)
	{
		switch (Verdict)
		{
		case EVerdict::Pass:
			return TEXT("PASS");
		case EVerdict::Fail:
			return TEXT("FAIL");
		default:
			return TEXT("INCONCLUSIVE");
		}
	}

	bool FRow::ParseCsv(const FString& Csv, TArray<FRow>& OutRows)
	{
		OutRows.Reset();
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		if (Lines.IsEmpty())
		{
			return false;
		}
		TArray<FString> Header;
		Lines[0].TrimStartAndEnd().ParseIntoArray(Header, TEXT(","), false);
		auto Column = [&Header](const TCHAR* Name) { return Header.IndexOfByKey(FString(Name)); };
		const TCHAR* Required[] = { TEXT("count"), TEXT("scenario"), TEXT("sample"), TEXT("frames"), TEXT("frame_ms"), TEXT("game_ms"),
			TEXT("p95_game_ms"), TEXT("max_game_ms"), TEXT("render_ms"), TEXT("rhi_ms"), TEXT("gpu_ms"), TEXT("avoidance_ms"),
			TEXT("locomotion_ms"), TEXT("p95_locomotion_ms") };
		for (const TCHAR* Name : Required)
		{
			if (Column(Name) == INDEX_NONE)
			{
				return false;
			}
		}
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			TArray<FString> Cells;
			Lines[Index].TrimStartAndEnd().ParseIntoArray(Cells, TEXT(","), false);
			if (Cells.Num() < Header.Num())
			{
				continue;
			}
			auto Number = [&](const TCHAR* Name, double Default = 0.0)
			{
				const int32 At = Column(Name);
				return At == INDEX_NONE ? Default : FCString::Atod(*Cells[At]);
			};
			FRow Row;
			Row.Count = FMath::RoundToInt(Number(TEXT("count")));
			Row.Scenario = Cells[Column(TEXT("scenario"))];
			Row.Sample = FMath::RoundToInt(Number(TEXT("sample")));
			Row.Frames = FMath::RoundToInt(Number(TEXT("frames")));
			Row.FrameMs = Number(TEXT("frame_ms"));
			Row.GameMs = Number(TEXT("game_ms"));
			Row.P95GameMs = Number(TEXT("p95_game_ms"));
			Row.MaxGameMs = Number(TEXT("max_game_ms"));
			Row.RenderMs = Number(TEXT("render_ms"));
			Row.RHIMs = Number(TEXT("rhi_ms"));
			Row.GPUMs = Number(TEXT("gpu_ms"));
			Row.AvoidanceMs = Number(TEXT("avoidance_ms"));
			Row.LocomotionMs = Number(TEXT("locomotion_ms"));
			Row.P95LocomotionMs = Number(TEXT("p95_locomotion_ms"));
			Row.MaxLocomotionMs = Number(TEXT("max_locomotion_ms"));
			Row.SampleSeconds = Number(TEXT("sample_s"), -1.0);
			Row.MaxFrameMs = Number(TEXT("max_frame_ms"), -1.0);
			Row.StallFrames = FMath::RoundToInt(Number(TEXT("stall_frames"), -1.0));
			Row.FreezeFrames = FMath::RoundToInt(Number(TEXT("freeze_frames"), -1.0));
			OutRows.Add(MoveTemp(Row));
		}
		return true;
	}

	FRun ReadRun(const FString& Set, const FString& Csv, const FString& Json)
	{
		FRun Run;
		Run.Set = Set;
		TSharedPtr<FJsonObject> Root;
		if (!FRow::ParseCsv(Csv, Run.Rows) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			return Run;
		}
		Run.bComplete = !Run.Rows.IsEmpty();
		const TSharedPtr<FJsonObject>* Method = nullptr;
		if (Root->TryGetObjectField(TEXT("method"), Method))
		{
			(*Method)->TryGetStringField(TEXT("name"), Run.Method);
			(*Method)->TryGetNumberField(TEXT("sample_s"), Run.PlannedSampleSeconds);
		}
		const TSharedPtr<FJsonObject>* Machine = nullptr;
		if (Root->TryGetObjectField(TEXT("machine"), Machine))
		{
			(*Machine)->TryGetStringField(TEXT("build"), Run.Build);
		}
		const TSharedPtr<FJsonObject>* RunSection = nullptr;
		if (Root->TryGetObjectField(TEXT("run"), RunSection))
		{
			Run.bHasValidity = true;
			(*RunSection)->TryGetStringArrayField(TEXT("flags"), Run.Flags);
			bool bValid = true;
			if ((*RunSection)->TryGetBoolField(TEXT("valid"), bValid) && !bValid && Run.Flags.IsEmpty())
			{
				Run.Flags.Add(TEXT("marked invalid"));
			}
			(*RunSection)->TryGetBoolField(TEXT("trace"), Run.bTrace);
			(*RunSection)->TryGetBoolField(TEXT("vehicles"), Run.bVehicles);
		}
		const TSharedPtr<FJsonObject>* Environment = nullptr;
		if (Root->TryGetObjectField(TEXT("environment"), Environment))
		{
			Run.bHasEnvironment = true;
			(*Environment)->TryGetNumberField(TEXT("other_cores"), Run.OtherCores);
			(*Environment)->TryGetBoolField(TEXT("shader_workers"), Run.bShaderWorkers);
		}
		return Run;
	}

	FRun LoadRun(const FString& Set, const FString& Dir)
	{
		FString Csv;
		FString Json;
		if (!FFileHelper::LoadFileToString(Csv, *FPaths::Combine(Dir, TEXT("bench.csv")))
			|| !FFileHelper::LoadFileToString(Json, *FPaths::Combine(Dir, TEXT("bench.json"))))
		{
			FRun Missing;
			Missing.Set = Set;
			return Missing;
		}
		return ReadRun(Set, Csv, Json);
	}

	TArray<FString> CompareMetadata(const FString& BaseJson, const FString& NowJson)
	{
		TArray<FString> Differences;
		TSharedPtr<FJsonObject> Base;
		TSharedPtr<FJsonObject> Now;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BaseJson), Base) || !Base.IsValid()
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NowJson), Now) || !Now.IsValid())
		{
			Differences.Add(TEXT("bench.json does not parse"));
			return Differences;
		}
		auto Text = [](const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return FString(TEXT("(none)"));
			}
			FString Out;
			FJsonSerializer::Serialize(Value, FString(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
			return Out;
		};
		for (const TCHAR* Section : { TEXT("method"), TEXT("scene"), TEXT("machine") })
		{
			const TSharedPtr<FJsonObject>* Before = nullptr;
			const TSharedPtr<FJsonObject>* After = nullptr;
			if (!Base->TryGetObjectField(Section, Before) || !Now->TryGetObjectField(Section, After))
			{
				Differences.Add(FString::Printf(TEXT("%s: missing"), Section));
				continue;
			}
			TArray<FString> Fields;
			for (const TSharedPtr<FJsonObject>* Object : { Before, After })
			{
				for (const auto& Pair : (*Object)->Values)
				{
					Fields.AddUnique(FString(Pair.Key));
				}
			}
			for (const FString& Field : Fields)
			{
				const FString B = Text((*Before)->TryGetField(Field));
				const FString N = Text((*After)->TryGetField(Field));
				if (B != N)
				{
					Differences.Add(FString::Printf(TEXT("%s.%s: %s -> %s"), Section, *Field, *B, *N));
				}
			}
		}
		const TSharedPtr<FJsonObject>* BaseRun = nullptr;
		const TSharedPtr<FJsonObject>* NowRun = nullptr;
		FString BaseEngine;
		FString NowEngine;
		if (Base->TryGetObjectField(TEXT("run"), BaseRun) && Now->TryGetObjectField(TEXT("run"), NowRun)
			&& (*BaseRun)->TryGetStringField(TEXT("engine"), BaseEngine) && (*NowRun)->TryGetStringField(TEXT("engine"), NowEngine)
			&& BaseEngine != NowEngine)
		{
			Differences.Add(FString::Printf(TEXT("run.engine: %s -> %s"), *BaseEngine, *NowEngine));
		}
		return Differences;
	}

	TArray<FString> CheckRow(const FRow& Row, double PlannedSeconds)
	{
		TArray<FString> Flags;
		const FString Name = RowName(Row);
		if (Row.Frames <= 0)
		{
			Flags.Add(Name + TEXT(": no frames"));
			return Flags;
		}
		if (PlannedSeconds > 0.0 && Row.SampleSeconds >= 0.0 && Row.SampleSeconds < PlannedSeconds * Validity::MinSampleShare)
		{
			Flags.Add(FString::Printf(TEXT("%s: incomplete, %.1f of %.0f s"), *Name, Row.SampleSeconds, PlannedSeconds));
		}
		const double Seconds = Row.SampleSeconds > 0.0 ? Row.SampleSeconds : Row.Frames * Row.FrameMs / 1000.0;
		if (Seconds > 0.0 && Row.Frames / Seconds < Validity::MinFramesPerSecond)
		{
			Flags.Add(FString::Printf(TEXT("%s: only %d frames in %.1f s (%.1f fps, at least %.0f)"), *Name, Row.Frames, Seconds,
				Row.Frames / Seconds, Validity::MinFramesPerSecond));
		}
		if (Row.FreezeFrames > 0)
		{
			Flags.Add(FString::Printf(TEXT("%s: %d frame(s) over %.0f ms, the longest %.0f ms"), *Name, Row.FreezeFrames,
				Validity::FreezeFrameMs, Row.MaxFrameMs));
		}
		else if (Row.StallFrames > Validity::MaxStalls)
		{
			Flags.Add(FString::Printf(TEXT("%s: %d stalls over %.0f ms (at most %d)"), *Name, Row.StallFrames, Validity::StallFrameMs,
				Validity::MaxStalls));
		}
		return Flags;
	}

	TArray<FString> CheckRun(const TArray<FRow>& Rows, double PlannedSeconds)
	{
		TArray<FString> Flags;
		if (Rows.IsEmpty())
		{
			Flags.Add(TEXT("no rows"));
			return Flags;
		}
		for (const FRow& Row : Rows)
		{
			Flags.Append(CheckRow(Row, PlannedSeconds));
		}
		for (const TPair<int32, FString>& Scenario : GetScenarios(Rows))
		{
			double Low = TNumericLimits<double>::Max();
			double High = TNumericLimits<double>::Lowest();
			int32 Samples = 0;
			for (const FRow& Row : Rows)
			{
				if (Row.Count == Scenario.Key && Row.Scenario == Scenario.Value)
				{
					Low = FMath::Min(Low, Row.GameMs);
					High = FMath::Max(High, Row.GameMs);
					++Samples;
				}
			}
			if (Samples > 1 && High - Low > Validity::MaxSampleSpreadMs)
			{
				Flags.Add(FString::Printf(TEXT("%d %s: the samples disagree by %.2f ms game thread (%.2f to %.2f, at most %.2f)"),
					Scenario.Key, *Scenario.Value, High - Low, Low, High, Validity::MaxSampleSpreadMs));
			}
		}
		return Flags;
	}

	TArray<FString> CheckEnvironment(double OtherCores, bool bShaderWorkers)
	{
		TArray<FString> Flags;
		if (OtherCores > Validity::MaxOtherCores)
		{
			Flags.Add(FString::Printf(TEXT("other processes used %.2f cores while it ran (at most %.2f)"), OtherCores,
				Validity::MaxOtherCores));
		}
		if (bShaderWorkers)
		{
			Flags.Add(TEXT("shaders were compiling while it ran"));
		}
		return Flags;
	}

	TArray<FString> CheckAgainstBaseline(const TArray<FRow>& Now, const TArray<FRow>& Base)
	{
		TArray<FString> Flags;
		for (const FRow& Row : Now)
		{
			const FRow* Before = FindRow(Base, Row);
			if (!Before || Before->FrameMs <= 0.0 || Row.FrameMs < Before->FrameMs * Validity::SlowdownFactor)
			{
				continue;
			}
			const double Growth = Row.FrameMs - Before->FrameMs;
			const double Explained = FMath::Max(FMath::Max(Row.GameMs - Before->GameMs, Row.RenderMs - Before->RenderMs),
				FMath::Max(Row.RHIMs - Before->RHIMs, Row.GPUMs - Before->GPUMs));
			if (Explained < Growth * Validity::MinExplainedShare)
			{
				Flags.Add(FString::Printf(TEXT("%s: the frame took %.2f ms against %.2f ms, but the game, render, RHI and GPU times explain %.2f ms of it"),
					*RowName(Row), Row.FrameMs, Before->FrameMs, FMath::Max(0.0, Explained)));
			}
		}
		return Flags;
	}

	FSetVerdict JudgeScreen(const FRun& Now, const FRun* Base, const TArray<FString>& Incompatible)
	{
		FSetVerdict Verdict;
		Verdict.Set = Now.Set;
		Verdict.Method = TEXT("screen");
		const FString Again = FString::Printf(TEXT("run the screen of %s again on an idle machine (ue.ps1 -Task gate); if it reproduces there, read it as the code"), *Now.Set);
		if (!Now.bComplete)
		{
			Verdict.Reasons.Add(TEXT("no result: the run failed or was interrupted"));
			Verdict.Next = Again;
			return Verdict;
		}
		Verdict.Reasons = GetRunFlags(Now);
		if (Base && Incompatible.IsEmpty())
		{
			Verdict.Reasons.Append(CheckAgainstBaseline(Now.Rows, Base->Rows));
		}
		if (!Verdict.Reasons.IsEmpty())
		{
			Verdict.Next = Again;
			return Verdict;
		}

		TArray<FString> Escalate;
		CheckBudgets(Now, ScreenBudgetShare, Escalate, Verdict.Notes);
		const TArray<FString> BaseProblems = GetBaselineProblems(Base, Incompatible);
		Escalate.Append(BaseProblems);
		if (BaseProblems.IsEmpty())
		{
			CheckGameThread(Now, *Base, ScreenThresholdMs, Escalate, Verdict.Notes);
		}
		if (Escalate.IsEmpty())
		{
			Verdict.Verdict = EVerdict::Pass;
			Verdict.Notes.Add(TEXT("screen: diagnostic, clear; not gate evidence on its own"));
			return Verdict;
		}
		Verdict.bEscalate = true;
		Verdict.Reasons = Escalate;
		Verdict.Next = FString::Printf(TEXT("the long method of %s"), *Now.Set);
		return Verdict;
	}

	FSetVerdict JudgeLong(const FRun& Now, const FRun* Base, const TArray<FString>& Incompatible)
	{
		FSetVerdict Verdict;
		Verdict.Set = Now.Set;
		Verdict.Method = TEXT("long");
		const FString Again = FString::Printf(TEXT("run the long method of %s again on an idle machine; if it reproduces there, read it as the code"), *Now.Set);
		if (!Now.bComplete)
		{
			Verdict.Reasons.Add(TEXT("no result: the run failed or was interrupted"));
			Verdict.Next = Again;
			return Verdict;
		}
		Verdict.Reasons = GetRunFlags(Now);
		const TArray<FString> BaseProblems = GetBaselineProblems(Base, Incompatible);
		if (BaseProblems.IsEmpty())
		{
			Verdict.Reasons.Append(CheckAgainstBaseline(Now.Rows, Base->Rows));
		}
		if (!Verdict.Reasons.IsEmpty())
		{
			// Contaminated: not a measurement, so neither its budgets nor its baseline difference are read.
			Verdict.Next = Again;
			return Verdict;
		}

		TArray<FString> Failures;
		CheckBudgets(Now, 1.0, Failures, Verdict.Notes);
		if (BaseProblems.IsEmpty())
		{
			CheckGameThread(Now, *Base, Budgets::GameThreadSpreadMs, Failures, Verdict.Notes);
		}
		if (!Failures.IsEmpty())
		{
			// A valid run over a budget fails even without a baseline.
			Verdict.Verdict = EVerdict::Fail;
			Verdict.Reasons = Failures;
			Verdict.Next = FString::Printf(TEXT("find the cost in a trace: ue.ps1 -Task bench -Unit %s -Counts 240 -Trace"), *Now.Set);
			return Verdict;
		}
		if (!BaseProblems.IsEmpty())
		{
			Verdict.Reasons = BaseProblems;
			Verdict.Next = FString::Printf(TEXT("store the long baseline on a clean checkout of the baseline commit: ue.ps1 -Task bench -Long -Unit %s -SaveBaseline"), *Now.Set);
			return Verdict;
		}
		Verdict.Verdict = EVerdict::Pass;
		return Verdict;
	}

	EVerdict Combine(const TArray<FSetVerdict>& Verdicts)
	{
		EVerdict Result = EVerdict::Pass;
		for (const FSetVerdict& Verdict : Verdicts)
		{
			if (Verdict.Verdict == EVerdict::Fail)
			{
				return EVerdict::Fail;
			}
			if (Verdict.Verdict == EVerdict::Inconclusive)
			{
				Result = EVerdict::Inconclusive;
			}
		}
		return Result;
	}

	FChange ClassifyPath(const FString& RepoPath)
	{
		const FString Path = RepoPath.Replace(TEXT("\\"), TEXT("/"));
		FChange Change;
		auto Broad = [&Change](bool bBenchMethod)
		{
			Change.Scope = EScope::Broad;
			Change.bBenchMethod = bBenchMethod;
			return Change;
		};
		if (Path.StartsWith(TEXT("docs/")) || Path.EndsWith(TEXT(".md")) || Path.StartsWith(TEXT("Source/WorldDivide/Tests/"))
			|| Path.StartsWith(TEXT("Source/WorldDivide/Units/Debug/RTSBenchGate")))
		{
			return Change;
		}
		if (Path == TEXT("Source/WorldDivide/Units/Debug/RTSBenchCommand.cpp") || Path == TEXT("Source/WorldDivide/Units/Debug/RTSBenchPlan.h")
			|| Path.StartsWith(TEXT("Content/Maps/L_Bench.")) || Path.StartsWith(TEXT("Content/Maps/L_Bench_BuiltData")))
		{
			return Broad(true);
		}
		if (Path.StartsWith(TEXT("Content/Maps/")))
		{
			return Change;
		}
		const bool bPackage = Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap"));
		if (Path.StartsWith(TEXT("Content/")) && bPackage)
		{
			const FString Package = TEXT("/Game/") + FPaths::GetBaseFilename(Path.RightChop(8), false);
			if (Path.StartsWith(TEXT("Content/Data/Units/")) && !Path.RightChop(19).Contains(TEXT("/")))
			{
				Change.Scope = EScope::Unit;
				Change.Name = FPaths::GetBaseFilename(Path);
				return Change;
			}
			Change.Scope = EScope::Content;
			Change.Name = Package;
			return Change;
		}
		if (Path.StartsWith(TEXT("Source/")) || Path.StartsWith(TEXT("Config/")) || Path.StartsWith(TEXT("Plugins/"))
			|| Path.StartsWith(TEXT("UI/")) || Path.StartsWith(TEXT("Content/")) || Path.EndsWith(TEXT(".uproject")))
		{
			return Broad(false);
		}
		return Change;
	}

	const TCHAR* LexUnitClass(EUnitClass Class)
	{
		switch (Class)
		{
		case EUnitClass::Tracked:
			return TEXT("tracked");
		case EUnitClass::Wheeled:
			return TEXT("wheeled");
		case EUnitClass::Air:
			return TEXT("air");
		default:
			return TEXT("infantry");
		}
	}

	const TArray<FString>& GetPinned()
	{
		static const TArray<FString> Pinned = { TEXT("DA_TestM20Guardian"), TEXT("DA_TestTank") };
		return Pinned;
	}

	int32 GetSizeBucket(double FootprintM2)
	{
		return FootprintM2 < 4.0 ? 0 : FootprintM2 <= 20.0 ? 1 : 2;
	}

	TArray<FString> SelectRepresentatives(const TArray<FUnitInfo>& Roster, TMap<FString, FString>* OutReasons)
	{
		TSet<FString> Chosen;
		auto Choose = [&Chosen, OutReasons](const FUnitInfo& Unit, const FString& Why)
		{
			Chosen.Add(Unit.Name);
			if (OutReasons)
			{
				FString& Reason = OutReasons->FindOrAdd(Unit.Name);
				Reason += Reason.IsEmpty() ? Why : TEXT("; ") + Why;
			}
		};
		for (const EUnitClass Class : { EUnitClass::Tracked, EUnitClass::Wheeled, EUnitClass::Infantry, EUnitClass::Air })
		{
			TArray<const FUnitInfo*> Members;
			for (const FUnitInfo& Unit : Roster)
			{
				if (Unit.Class == Class)
				{
					Members.Add(&Unit);
				}
			}
			if (Members.IsEmpty())
			{
				continue;
			}
			// The screen ranks a class only when it measured all of it; otherwise the meshes do.
			const bool bScreened = !Members.ContainsByPredicate([](const FUnitInfo* Unit) { return Unit->ScreenGameMs < 0.0; });
			auto Cost = [bScreened](const FUnitInfo* Unit)
			{
				return bScreened ? Unit->ScreenGameMs : static_cast<double>(Unit->Triangles) * FMath::Max(1, Unit->Parts);
			};
			const FString Measure = bScreened ? TEXT("by the screen") : TEXT("by LOD0 triangles times parts");
			const FString ClassName = LexUnitClass(Class);
			for (const FUnitInfo* Unit : Members)
			{
				if (GetPinned().Contains(Unit->Name))
				{
					Choose(*Unit, TEXT("pinned for history"));
				}
			}
			const FUnitInfo* Costliest = Members[0];
			TMap<int32, const FUnitInfo*> BySize;
			for (const FUnitInfo* Unit : Members)
			{
				Costliest = Cost(Unit) > Cost(Costliest) ? Unit : Costliest;
				const FUnitInfo*& InBucket = BySize.FindOrAdd(GetSizeBucket(Unit->FootprintM2), Unit);
				InBucket = Cost(Unit) > Cost(InBucket) ? Unit : InBucket;
			}
			// A unit with a long baseline stays the pick while it is within the margin of the costliest, so a near tie in
			// the screen does not swap a representative for one without a baseline on every run.
			auto Keep = [&Members, &Cost, bScreened](const FUnitInfo* Pick, TFunctionRef<bool(const FUnitInfo*)> InGroup)
			{
				const double Margin = bScreened ? ScreenThresholdMs : Cost(Pick) * RepresentativeMarginShare;
				const FUnitInfo* Kept = Pick;
				for (const FUnitInfo* Unit : Members)
				{
					if (!Kept->bHasLongBaseline && Unit->bHasLongBaseline && InGroup(Unit) && Cost(Unit) >= Cost(Pick) - Margin)
					{
						Kept = Unit;
					}
				}
				return Kept;
			};
			auto Describe = [](const FUnitInfo* Kept, const FUnitInfo* Pick, const FString& What)
			{
				return Kept == Pick ? What : FString::Printf(TEXT("%s within the margin of %s, kept for its long baseline"), *What, *Pick->Name);
			};
			const FUnitInfo* Kept = Keep(Costliest, [](const FUnitInfo*) { return true; });
			Choose(*Kept, Describe(Kept, Costliest, FString::Printf(TEXT("costliest %s unit %s"), *ClassName, *Measure)));
			if (BySize.Num() > 1)
			{
				static const TCHAR* Sizes[] = { TEXT("under 4 m2"), TEXT("4 to 20 m2"), TEXT("over 20 m2") };
				for (const TPair<int32, const FUnitInfo*>& Bucket : BySize)
				{
					const int32 Size = Bucket.Key;
					const FUnitInfo* InSize = Keep(Bucket.Value, [Size](const FUnitInfo* Unit) { return GetSizeBucket(Unit->FootprintM2) == Size; });
					Choose(*InSize, Describe(InSize, Bucket.Value, FString::Printf(TEXT("costliest %s unit %s"), *ClassName, Sizes[Size])));
				}
			}
		}
		TArray<FString> Representatives;
		for (const FUnitInfo& Unit : Roster)
		{
			if (Chosen.Contains(Unit.Name))
			{
				Representatives.Add(Unit.Name);
			}
		}
		return Representatives;
	}

	FString MakeMixedSet(const TArray<FUnitInfo>& Roster, const TArray<FString>& Representatives)
	{
		TArray<FString> Members;
		for (const EUnitClass Class : { EUnitClass::Tracked, EUnitClass::Wheeled, EUnitClass::Infantry, EUnitClass::Air })
		{
			const FUnitInfo* Pick = nullptr;
			for (const FUnitInfo& Unit : Roster)
			{
				// A pinned unit first, then one with a long baseline, so the mixed set keeps its name and its baseline.
				auto Rank = [](const FUnitInfo& Candidate) { return GetPinned().Contains(Candidate.Name) ? 2 : Candidate.bHasLongBaseline ? 1 : 0; };
				if (Unit.Class == Class && Representatives.Contains(Unit.Name) && (!Pick || Rank(Unit) > Rank(*Pick)))
				{
					Pick = &Unit;
				}
			}
			if (Pick)
			{
				Members.Add(Pick->Name);
			}
		}
		return Members.Num() > 1 ? FString::Join(Members, TEXT("+")) : FString();
	}

	FPlan MakePlan(const TArray<FUnitInfo>& Roster, const TArray<FString>& ChangedPaths, bool bForceBroad)
	{
		FPlan Plan;
		TMap<FString, FString> RepresentativeReasons;
		Plan.Representatives = SelectRepresentatives(Roster, &RepresentativeReasons);
		Plan.Mixed = MakeMixedSet(Roster, Plan.Representatives);
		Plan.bBroad = bForceBroad;

		TMap<FString, FString> Affected;
		for (const FString& Path : ChangedPaths)
		{
			const FChange Change = ClassifyPath(Path);
			Plan.bBenchMethod |= Change.bBenchMethod;
			switch (Change.Scope)
			{
			case EScope::None:
				Plan.Changes.Add(Path + TEXT(": nothing the bench runs"));
				break;
			case EScope::Broad:
				Plan.bBroad = true;
				Plan.Changes.Add(Path + (Change.bBenchMethod ? TEXT(": the bench's own method or scene, every unit") : TEXT(": shared, every unit")));
				break;
			case EScope::Unit:
				if (Roster.ContainsByPredicate([&Change](const FUnitInfo& Unit) { return Unit.Name == Change.Name; }))
				{
					Affected.FindOrAdd(Change.Name) = TEXT("its data changed");
					Plan.Changes.Add(Path + TEXT(": ") + Change.Name);
				}
				else
				{
					Plan.Changes.Add(Path + TEXT(": no unit of the roster (removed?)"));
				}
				break;
			case EScope::Content:
			{
				TArray<FString> Users;
				for (const FUnitInfo& Unit : Roster)
				{
					if (Unit.Packages.Contains(Change.Name))
					{
						Users.Add(Unit.Name);
						Affected.FindOrAdd(Unit.Name) = FString::Printf(TEXT("it uses %s"), *Change.Name);
					}
				}
				if (Users.IsEmpty())
				{
					// Not part of any unit: code may load it (a game mode, the HUD, a shared material), so it is shared.
					Plan.bBroad = true;
					Plan.Changes.Add(Path + TEXT(": no unit uses it, so it is shared, every unit"));
				}
				else
				{
					Plan.Changes.Add(Path + TEXT(": ") + FString::Join(Users, TEXT(", ")));
				}
				break;
			}
			}
		}

		const TArray<FString> MixMembers = [&Plan]()
		{
			TArray<FString> Members;
			Plan.Mixed.ParseIntoArray(Members, TEXT("+"));
			return Members;
		}();
		for (const FUnitInfo& Unit : Roster)
		{
			const bool bRepresentative = Plan.Representatives.Contains(Unit.Name);
			if (Plan.bBroad)
			{
				Plan.Screen.Add(Unit.Name);
				Plan.Reasons.Add(Unit.Name, bRepresentative ? TEXT("shared change; representative: ") + RepresentativeReasons[Unit.Name]
					: TEXT("shared change: screened"));
				if (bRepresentative)
				{
					Plan.Long.Add(Unit.Name);
				}
			}
			else if (const FString* Why = Affected.Find(Unit.Name))
			{
				Plan.Screen.Add(Unit.Name);
				Plan.Reasons.Add(Unit.Name, *Why + (bRepresentative ? TEXT("; representative: ") + RepresentativeReasons[Unit.Name] : FString()));
				if (bRepresentative)
				{
					Plan.Long.Add(Unit.Name);
				}
			}
		}
		const bool bMixAffected = Plan.bBroad || MixMembers.ContainsByPredicate([&Affected](const FString& Member) { return Affected.Contains(Member); });
		if (!Plan.Mixed.IsEmpty() && bMixAffected)
		{
			Plan.Long.Add(Plan.Mixed);
			Plan.Reasons.Add(Plan.Mixed, TEXT("the mixed set: one representative of each class"));
		}
		return Plan;
	}
}

#endif
