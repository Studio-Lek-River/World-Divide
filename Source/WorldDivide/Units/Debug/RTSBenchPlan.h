#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

/** The methods of rts.Debug.Bench (#132, #138, #167). */
enum class ERTSBenchMethod : uint8
{
	/** 3 s settle, 120 frames idle, then 120 frames 1 s after one move order. */
	Short,
	/** The performance gate of #138: 10 s warm-ups, 60 s samples (three at the largest count), the units driving back and forth. */
	Long,
	/** For iterating (#167): the long scene, moving only, a 10 s warm-up and one 30 s sample. Diagnostic, never gate evidence. */
	Quick,
};

/** What one bench method measures: its counts, scenarios, warm-ups and samples. */
struct FRTSBenchPlan
{
	/** Bumped whenever a method's numbers or scene change, so a baseline stored with the old method is never compared. */
	static constexpr int32 Version = 1;

	ERTSBenchMethod Method = ERTSBenchMethod::Short;
	TArray<int32> Counts;
	/** Whether the units are measured standing before the move order. */
	bool bIdle = true;
	/** Real seconds from spawning to the idle measurement (or to the move order without it), and from the order to the move measurement. */
	double IdleWarmupSeconds = 3.0;
	double MoveWarmupSeconds = 1.0;
	/** A timed sample's real seconds; 0 measures SampleFrames frames instead. */
	double SampleSeconds = 0.0;
	int32 SampleFrames = 120;
	/** Timed samples per scenario at the largest count and below it. */
	int32 SamplesAtLargest = 1;
	int32 SamplesBelow = 1;
	/** The long scene: the grid south-west of the middle, out of the headquarters' range, its units driving back and forth. */
	bool bLongScene = false;

	bool IsTimed() const { return SampleSeconds > 0.0; }

	int32 GetSamples(int32 Count) const
	{
		return Counts.Num() > 0 && Count == Counts.Last() ? SamplesAtLargest : SamplesBelow;
	}

	static const TCHAR* GetMethodName(ERTSBenchMethod InMethod)
	{
		switch (InMethod)
		{
		case ERTSBenchMethod::Long:
			return TEXT("long");
		case ERTSBenchMethod::Quick:
			return TEXT("quick");
		default:
			return TEXT("short");
		}
	}

	/** "short", "long" or "quick"; false for anything else. */
	static bool ParseMethod(const FString& Word, ERTSBenchMethod& OutMethod)
	{
		for (const ERTSBenchMethod Candidate : { ERTSBenchMethod::Short, ERTSBenchMethod::Long, ERTSBenchMethod::Quick })
		{
			if (Word == GetMethodName(Candidate))
			{
				OutMethod = Candidate;
				return true;
			}
		}
		return false;
	}

	/** The positive counts of "1,30,120" or "1+30+120" (ue.ps1 joins them with '+', since -ExecCmds splits at commas). */
	static TArray<int32> ParseCounts(const FString& Arg)
	{
		TArray<FString> Parts;
		Arg.Replace(TEXT("+"), TEXT(",")).ParseIntoArray(Parts, TEXT(","));
		TArray<int32> Counts;
		for (const FString& Part : Parts)
		{
			if (const int32 Count = FCString::Atoi(*Part); Count > 0)
			{
				Counts.Add(Count);
			}
		}
		return Counts;
	}

	/** The plan of InMethod for InCounts, or for the method's default counts when InCounts is empty. */
	static FRTSBenchPlan Make(ERTSBenchMethod InMethod, const TArray<int32>& InCounts)
	{
		FRTSBenchPlan Plan;
		Plan.Method = InMethod;
		switch (InMethod)
		{
		case ERTSBenchMethod::Long:
			Plan.IdleWarmupSeconds = 10.0;
			Plan.MoveWarmupSeconds = 10.0;
			Plan.SampleSeconds = 60.0;
			Plan.SamplesAtLargest = 3;
			Plan.bLongScene = true;
			break;
		case ERTSBenchMethod::Quick:
			Plan.bIdle = false;
			Plan.MoveWarmupSeconds = 10.0;
			Plan.SampleSeconds = 30.0;
			Plan.bLongScene = true;
			break;
		default:
			break;
		}
		Plan.Counts = InCounts;
		if (Plan.Counts.IsEmpty())
		{
			Plan.Counts = InMethod == ERTSBenchMethod::Quick ? TArray<int32>{ 240 } : TArray<int32>{ 1, 30, 120, 240 };
		}
		return Plan;
	}
};

#endif
