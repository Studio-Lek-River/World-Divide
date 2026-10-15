#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Containers/Ticker.h"
#include "Data/RTSUnitData.h"
#include "DynamicRHI.h"
#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSPlayerController.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

/**
 * rts.Debug.Bench <UnitData> [Count,Count,...]: the performance benchmark of one unit type (ue.ps1 -Task bench), on
 * L_Bench, or of a mix of sizes named UnitA+UnitB+..., which the grid takes in turn and spaces for the largest.
 * For each count (by default 1, 30, 120 and 240) that many of the unit stand in a grid spaced like a formation,
 * and are measured standing still, then driving on one move order issued as the player issues it. Every scenario is seen
 * from the same fixed camera, at a fixed 1920x1080 with dynamic resolution off and the screen percentage at 100, without a
 * frame rate cap. Logged and written to Saved/Bench/<UnitData>/bench.csv and bench.md per scenario: the frame, game thread,
 * render thread, RHI thread and GPU times, the slowest game thread frame, draw calls, triangles drawn, the avoidance pass
 * and the weapons' target search, the memory in use and its peak, and the internal render resolution. The table's header
 * names the CPU, GPU, driver, RHI and build it was measured on. With -RTSBenchQuit on the command
 * line the game quits when done, with exit code 1 on failure.
 */
namespace RTSBenchCommand
{
	const TCHAR* QuitParam = TEXT("RTSBenchQuit");
	const TCHAR* UnitDataPath = TEXT("/Game/Data/Units");

	constexpr double ReadyTimeout = 60.0;
	constexpr double ResizeTimeout = 10.0;
	const FIntPoint ScreenSize(1920, 1080);

	const TArray<int32> DefaultCounts = { 1, 30, 120, 240 };
	/** Real seconds for spawned units to settle and for a move order to get going, and the frames measured. */
	constexpr double SettleSeconds = 3.0;
	constexpr double MoveLeadSeconds = 1.0;
	constexpr double ClearSeconds = 1.0;
	constexpr int32 MeasureFrames = 120;

	/**
	 * The grid: units face +X in rows across Y, as a formation would stand: each unit's length plus RowGap apart along X and
	 * its width plus ColumnGap apart along Y, about square overall, centred on GridCentre. The move order sends them MoveDistance
	 * along +X.
	 */
	const FVector GridCentre(0.f, 0.f, 0.f);
	constexpr float RowGap = 300.f;
	constexpr float ColumnGap = 250.f;
	constexpr float MoveDistance = 6000.f;

	/** The fixed camera: looking down at the RTS pitch over the middle of the grid and the move, far enough to see both. */
	const FVector CameraLookAt(MoveDistance / 2.f, 0.f, 0.f);
	constexpr float CameraPitch = -55.f;
	constexpr float CameraDistance = 17000.f;
	constexpr float CameraFOV = 60.f;

	/** The settings every scenario runs with, restored afterwards. */
	const TArray<TPair<const TCHAR*, const TCHAR*>> BenchCVars = {
		{ TEXT("r.DynamicRes.OperationMode"), TEXT("0") },
		{ TEXT("r.ScreenPercentage"), TEXT("100") },
		{ TEXT("r.VSync"), TEXT("0") },
		{ TEXT("t.MaxFPS"), TEXT("0") },
	};

	struct FScenarioStats
	{
		FString Scenario;
		int32 Count = 0;
		int32 Units = 0;
		int32 Frames = 0;
		double FrameMs = 0.0;
		double GameMs = 0.0;
		double MaxGameMs = 0.0;
		double RenderMs = 0.0;
		double RHIMs = 0.0;
		double GPUMs = 0.0;
		double DrawCalls = 0.0;
		double Triangles = 0.0;
		double AvoidanceMs = 0.0;
		double MaxAvoidanceMs = 0.0;
		double SearchMs = 0.0;
		double UsedMemoryMB = 0.0;
		double PeakMemoryMB = 0.0;
		FIntPoint RenderSize = FIntPoint::ZeroValue;

		void Add(double InFrameMs, double InGameMs, double InRenderMs, double InRHIMs, double InGPUMs, double InDrawCalls,
			double InTriangles, double InAvoidanceMs, double InSearchMs)
		{
			++Frames;
			FrameMs += InFrameMs;
			GameMs += InGameMs;
			MaxGameMs = FMath::Max(MaxGameMs, InGameMs);
			RenderMs += InRenderMs;
			RHIMs += InRHIMs;
			GPUMs += InGPUMs;
			DrawCalls += InDrawCalls;
			Triangles += InTriangles;
			AvoidanceMs += InAvoidanceMs;
			MaxAvoidanceMs = FMath::Max(MaxAvoidanceMs, InAvoidanceMs);
			SearchMs += InSearchMs;
		}

		double Mean(double Sum) const { return Frames > 0 ? Sum / Frames : 0.0; }

		static FString CsvHeader()
		{
			return TEXT("count,scenario,units,frame_ms,game_ms,max_game_ms,render_ms,rhi_ms,gpu_ms,draw_calls,triangles,")
				TEXT("avoidance_ms,max_avoidance_ms,search_ms,used_memory_mb,peak_memory_mb,render_width,render_height");
		}

		FString ToCsv() const
		{
			return FString::Printf(TEXT("%d,%s,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%.0f,%.3f,%.3f,%.3f,%.0f,%.0f,%d,%d"),
				Count, *Scenario, Units, Mean(FrameMs), Mean(GameMs), MaxGameMs, Mean(RenderMs), Mean(RHIMs), Mean(GPUMs),
				Mean(DrawCalls), Mean(Triangles), Mean(AvoidanceMs), MaxAvoidanceMs, Mean(SearchMs), UsedMemoryMB,
				PeakMemoryMB, RenderSize.X, RenderSize.Y);
		}

		static FString MarkdownHeader()
		{
			return TEXT("| Units | Scenario | Frame ms | Game ms (max) | Render ms | RHI ms | GPU ms | Draw calls | Triangles | ")
				TEXT("Avoidance ms (max) | Target search ms | Memory MB (peak) | Render res |\n")
				TEXT("|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|\n");
		}

		FString ToMarkdown() const
		{
			return FString::Printf(TEXT("| %d | %s | %.2f | %.2f (%.2f) | %.2f | %.2f | %.2f | %.0f | %.2f M | %.3f (%.3f) | %.3f | %.0f (%.0f) | %dx%d |\n"),
				Count, *Scenario, Mean(FrameMs), Mean(GameMs), MaxGameMs, Mean(RenderMs), Mean(RHIMs), Mean(GPUMs),
				Mean(DrawCalls), Mean(Triangles) / 1.0e6, Mean(AvoidanceMs), MaxAvoidanceMs, Mean(SearchMs), UsedMemoryMB,
				PeakMemoryMB, RenderSize.X, RenderSize.Y);
		}
	};

	class FBenchRun : public TSharedFromThis<FBenchRun>
	{
	public:
		FBenchRun(UWorld& InWorld, const FString& InUnitName, const TArray<int32>& InCounts)
			: World(&InWorld), UnitName(InUnitName), Counts(InCounts)
		{
		}

		static bool IsRunning() { return Active.IsValid(); }

		static void Start(const TSharedRef<FBenchRun>& Run)
		{
			Active = Run;
			Run->StepStart = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FBenchRun::Tick));
		}

		static FString GetOutputDir(const FString& InUnitName)
		{
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Bench"), InUnitName);
		}

	private:
		enum class EStep : uint8
		{
			WaitForPlayer,
			Resize,
			SettleIdle,
			MeasureIdle,
			LeadMove,
			MeasureMove,
			Clear,
		};

		bool Tick(float)
		{
			if (!World.IsValid())
			{
				return Finish(TEXT("the world ended"));
			}
			++StepFrames;
			if (ARTSPlayerController* Controller = GetController(); Controller && GetViewportSize().X > 0)
			{
				// The cursor rests wherever the offscreen window put it; at an edge it would scroll the camera.
				Controller->SetMouseLocation(GetViewportSize().X / 2, GetViewportSize().Y / 2);
			}
			switch (Step)
			{
			case EStep::WaitForPlayer:
				return WaitForPlayer();
			case EStep::Resize:
				return Resize();
			case EStep::SettleIdle:
				if (FPlatformTime::Seconds() - StepStart >= SettleSeconds)
				{
					BeginMeasure(TEXT("idle"), EStep::MeasureIdle);
				}
				return true;
			case EStep::MeasureIdle:
				if (Measure())
				{
					Lead = FScenarioStats();
					IssueMove();
					GoTo(EStep::LeadMove);
				}
				return true;
			case EStep::LeadMove:
				// The order's own frame is the dispatch; its cost shows in the slowest game thread frame of the move.
				TakeFrame(Lead);
				if (FPlatformTime::Seconds() - StepStart >= MoveLeadSeconds)
				{
					BeginMeasure(TEXT("move"), EStep::MeasureMove);
				}
				return true;
			case EStep::MeasureMove:
				if (Measure())
				{
					ClearUnits();
					GoTo(EStep::Clear);
				}
				return true;
			case EStep::Clear:
				if (FPlatformTime::Seconds() - StepStart >= ClearSeconds)
				{
					return StartCount();
				}
				return true;
			}
			return true;
		}

		void GoTo(EStep Next)
		{
			Step = Next;
			StepFrames = 0;
			StepStart = FPlatformTime::Seconds();
		}

		ARTSPlayerController* GetController() const
		{
			return World.IsValid() ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		}

		FIntPoint GetViewportSize() const
		{
			const UGameViewportClient* Client = World->GetGameViewport();
			return Client && Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		}

		bool WaitForPlayer()
		{
			ARTSPlayerController* Controller = GetController();
			if (!World->HasBegunPlay() || !Controller || !Controller->GetPawn())
			{
				return FPlatformTime::Seconds() - StepStart > ReadyTimeout ? Finish(TEXT("no player in time")) : true;
			}
			if (!Controller->HasAuthority())
			{
				return Finish(TEXT("the game is a client; the benchmark runs on the server"));
			}
			// A mix of sizes is several names joined by '+'; the grid takes them in turn.
			TArray<FString> Names;
			UnitName.ParseIntoArray(Names, TEXT("+"));
			for (const FString& Name : Names)
			{
				const FString Path = FString::Printf(TEXT("%s/%s.%s"), UnitDataPath, *Name, *Name);
				URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, *Path);
				if (!Data)
				{
					return Finish(FString::Printf(TEXT("no unit data at %s"), *Path));
				}
				UnitData.Emplace(Data);
			}
			if (UnitData.IsEmpty())
			{
				return Finish(TEXT("no unit named"));
			}
			TeamId = Controller->GetTeamId();
			ApplySettings();
			UseBenchCamera();
			GoTo(EStep::Resize);
			return true;
		}

		bool Resize()
		{
			if (World->WorldType == EWorldType::PIE || GetViewportSize() == ScreenSize)
			{
				return StartCount();
			}
			if (StepFrames == 1)
			{
				FSystemResolution::RequestResolutionChange(ScreenSize.X, ScreenSize.Y, EWindowMode::Windowed);
			}
			if (FPlatformTime::Seconds() - StepStart > ResizeTimeout)
			{
				const FIntPoint Size = GetViewportSize();
				return Finish(FString::Printf(TEXT("the screen stayed %dx%d instead of %dx%d"), Size.X, Size.Y, ScreenSize.X, ScreenSize.Y));
			}
			return true;
		}

		void ApplySettings()
		{
			for (const TPair<const TCHAR*, const TCHAR*>& Setting : BenchCVars)
			{
				if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Setting.Key))
				{
					SavedCVars.Add(Setting.Key, CVar->GetString());
					CVar->Set(Setting.Value, ECVF_SetByConsole);
				}
			}
			bSavedSmoothFrameRate = GEngine->bSmoothFrameRate;
			GEngine->bSmoothFrameRate = false;
		}

		void RestoreSettings()
		{
			for (const TPair<FString, FString>& Setting : SavedCVars)
			{
				if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Setting.Key))
				{
					CVar->Set(*Setting.Value, ECVF_SetByConsole);
				}
			}
			SavedCVars.Reset();
			GEngine->bSmoothFrameRate = bSavedSmoothFrameRate;
		}

		void UseBenchCamera()
		{
			Camera = World->SpawnActor<ACameraActor>();
			Camera->GetCameraComponent()->SetFieldOfView(CameraFOV);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			const FRotator Rotation(CameraPitch, 0.f, 0.f);
			Camera->SetActorLocationAndRotation(CameraLookAt - Rotation.Vector() * CameraDistance, Rotation);
			GetController()->SetViewTarget(Camera.Get());
		}

		/** Spawns the next count's grid, or finishes after the last. */
		bool StartCount()
		{
			if (CountIndex >= Counts.Num())
			{
				return Finish(FString());
			}
			if (!SpawnGrid(Counts[CountIndex]))
			{
				return Finish(TEXT("could not spawn the units"));
			}
			GoTo(EStep::SettleIdle);
			return true;
		}

		/** A unit of the Index-th kind in the mix, taking them in turn. */
		ARTSUnit* SpawnUnit(const FVector& Location, int32 Index)
		{
			const FTransform Transform(FRotator::ZeroRotator, Location);
			ARTSUnit* Spawned = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Spawned)
			{
				Spawned->InitFromData(UnitData[Index % UnitData.Num()].Get(), TeamId);
				Spawned->FinishSpawning(Transform);
				Units.Add(Spawned);
			}
			return Spawned;
		}

		bool SpawnGrid(int32 Count)
		{
			// The first unit of each kind gives the size of the grid's cells, the largest of them, from its parts at rest,
			// before it moves to its place.
			FVector2D Largest = FVector2D::ZeroVector;
			const int32 Kinds = FMath::Min(Count, UnitData.Num());
			for (int32 Index = 0; Index < Kinds; ++Index)
			{
				ARTSUnit* First = SpawnUnit(GridCentre + FVector(0.f, 0.f, 10000.f * (Index + 1)), Index);
				if (!First)
				{
					return false;
				}
				const FBox Local = First->CalculateComponentsBoundingBoxInLocalSpace(/*bNonColliding*/ true);
				Largest = FVector2D::Max(Largest, FVector2D(Local.GetSize().X, Local.GetSize().Y));
			}
			const FVector2D Spacing(Largest.X + RowGap, Largest.Y + ColumnGap);
			const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(Count * Spacing.X / Spacing.Y)));
			const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);
			const FVector2D Size((Rows - 1) * Spacing.X, (Columns - 1) * Spacing.Y);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector Location = GridCentre + FVector(Index / Columns * Spacing.X - Size.X / 2.f,
					Index % Columns * Spacing.Y - Size.Y / 2.f, 0.f);
				if (Index < Kinds)
				{
					ARTSUnit* First = Units[Units.Num() - Kinds + Index].Get();
					First->SetActorLocation(Location + FVector(0.f, 0.f, First->GetGroundOffset()));
				}
				else if (ARTSUnit* Spawned = SpawnUnit(Location, Index))
				{
					Spawned->SetActorLocation(Location + FVector(0.f, 0.f, Spawned->GetGroundOffset()));
				}
				else
				{
					return false;
				}
			}
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: %d x %s in %d rows of %d, %.0f x %.0f cm apart"),
				Count, *UnitName, Rows, Columns, Spacing.X, Spacing.Y);
			return true;
		}

		void IssueMove()
		{
			TArray<ARTSUnit*> Ordered;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
			{
				if (Unit.IsValid())
				{
					Ordered.Add(Unit.Get());
				}
			}
			FRTSCommand Move;
			Move.Type = ERTSCommandType::Move;
			Move.TargetLocation = GridCentre + FVector(MoveDistance, 0.f, 0.f);
			GetController()->Server_IssueCommand(Ordered, Move);
		}

		void ClearUnits()
		{
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
			{
				if (Unit.IsValid())
				{
					if (AController* Controller = Unit->GetController())
					{
						Controller->Destroy();
					}
					Unit->Destroy();
				}
			}
			Units.Reset();
			GEngine->ForceGarbageCollection(true);
		}

		void BeginMeasure(const TCHAR* Scenario, EStep Next)
		{
			Current = FScenarioStats();
			Current.Scenario = Scenario;
			Current.Count = Counts[CountIndex];
			// The move's slowest frame includes the order's own frame, before the measured ones.
			Current.MaxGameMs = Next == EStep::MeasureMove ? Lead.MaxGameMs : 0.0;
			URTSWeaponComponent::TakeSearchSeconds();
			TRACE_BEGIN_REGION(*RegionName());
			GoTo(Next);
		}

		FString RegionName() const
		{
			return FString::Printf(TEXT("%s_%d_%s"), *UnitName, Current.Count, *Current.Scenario);
		}

		/** Adds this frame's numbers to Stats. */
		void TakeFrame(FScenarioStats& Stats) const
		{
			const URTSAvoidanceSubsystem* Avoidance = World->GetSubsystem<URTSAvoidanceSubsystem>();
			Stats.Add(FApp::GetDeltaTime() * 1000.0, FPlatformTime::ToMilliseconds(GGameThreadTime),
				FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(GRHIThreadTime),
				FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)), GNumDrawCallsRHI[0], GNumPrimitivesDrawnRHI[0],
				Avoidance ? Avoidance->GetLastSeparateSeconds() * 1000.0 : 0.0, URTSWeaponComponent::TakeSearchSeconds() * 1000.0);
		}

		/** Takes one frame of the scenario; true once it has all of them, logged and kept. */
		bool Measure()
		{
			TakeFrame(Current);
			if (StepFrames < MeasureFrames)
			{
				return false;
			}
			TRACE_END_REGION(*RegionName());
			Current.Units = Units.Num();
			const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
			Current.UsedMemoryMB = Memory.UsedPhysical / (1024.0 * 1024.0);
			Current.PeakMemoryMB = Memory.PeakUsedPhysical / (1024.0 * 1024.0);
			Current.RenderSize = GetRenderSize();
			Results.Add(Current);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: %s"), *Current.ToCsv());
			if (Current.Scenario == TEXT("move"))
			{
				++CountIndex;
			}
			return true;
		}

		/** The resolution the scene renders at before any upscale. */
		FIntPoint GetRenderSize() const
		{
			float Fraction = 1.f;
			FDynamicResolutionStateInfos Infos;
			GEngine->GetDynamicResolutionCurrentStateInfos(Infos);
			if (Infos.Status == EDynamicResolutionStatus::Enabled || Infos.Status == EDynamicResolutionStatus::DebugForceEnabled)
			{
				Fraction = Infos.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction];
			}
			else if (const IConsoleVariable* Percentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
				Percentage && Percentage->GetFloat() > 0.f)
			{
				Fraction = Percentage->GetFloat() / 100.f;
			}
			const FIntPoint Size = GetViewportSize();
			return FIntPoint(FMath::RoundToInt(Size.X * Fraction), FMath::RoundToInt(Size.Y * Fraction));
		}

		bool WriteResults() const
		{
			const FString Dir = GetOutputDir(UnitName);
			FString Csv = FScenarioStats::CsvHeader() + TEXT("\n");
			FString Markdown = FString::Printf(TEXT("%s on %s, %s\n\n- CPU: %s\n- GPU: %s, driver %s, %s\n- Build: %s\n\n"),
				*UnitName, *World->GetMapName(), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")),
				*FPlatformMisc::GetCPUBrand().TrimStartAndEnd(), *GRHIAdapterName, *GRHIAdapterUserDriverVersion,
				GDynamicRHI ? GDynamicRHI->GetName() : TEXT("no RHI"), LexToString(FApp::GetBuildConfiguration()));
			Markdown += FScenarioStats::MarkdownHeader();
			for (const FScenarioStats& Result : Results)
			{
				Csv += Result.ToCsv() + TEXT("\n");
				Markdown += Result.ToMarkdown();
			}
			return FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Dir, TEXT("bench.csv")))
				&& FFileHelper::SaveStringToFile(Markdown, *FPaths::Combine(Dir, TEXT("bench.md")));
		}

		bool Finish(FString Error)
		{
			if (World.IsValid())
			{
				ClearUnits();
				if (Camera.IsValid())
				{
					Camera->Destroy();
				}
				if (ARTSPlayerController* Controller = GetController(); Controller && Controller->GetPawn())
				{
					Controller->SetViewTarget(Controller->GetPawn());
				}
			}
			if (!SavedCVars.IsEmpty())
			{
				RestoreSettings();
			}
			if (Error.IsEmpty() && !WriteResults())
			{
				Error = FString::Printf(TEXT("could not write %s"), *GetOutputDir(UnitName));
			}
			if (Error.IsEmpty())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: %d scenarios to %s"), Results.Num(),
					*FPaths::ConvertRelativePathToFull(GetOutputDir(UnitName)));
			}
			else
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.Bench failed: %s"), *Error);
			}
			if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() ? 0 : 1);
			}
			Active.Reset();
			return false;
		}

		static TSharedPtr<FBenchRun> Active;

		TWeakObjectPtr<UWorld> World;
		FString UnitName;
		TArray<int32> Counts;
		/** The mix, held through the garbage collection between counts, when no unit refers to it. */
		TArray<TStrongObjectPtr<URTSUnitData>> UnitData;
		int32 TeamId = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<TWeakObjectPtr<ARTSUnit>> Units;

		EStep Step = EStep::WaitForPlayer;
		int32 StepFrames = 0;
		double StepStart = 0.0;
		int32 CountIndex = 0;
		FScenarioStats Current;
		/** The frames between the move order and the measured ones. */
		FScenarioStats Lead;
		TArray<FScenarioStats> Results;

		TMap<FString, FString> SavedCVars;
		bool bSavedSmoothFrameRate = false;
	};

	TSharedPtr<FBenchRun> FBenchRun::Active;

	void RunBenchCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld() || Args.IsEmpty())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.Bench <UnitData> [Count,Count,...] needs a running game"));
			return;
		}
		if (FBenchRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.Bench is already running"));
			return;
		}
		TArray<int32> Counts;
		if (Args.Num() > 1)
		{
			TArray<FString> Parts;
			// Counts are separated by ',' or by '+', which ue.ps1 uses because -ExecCmds splits commands at commas.
			Args[1].Replace(TEXT("+"), TEXT(",")).ParseIntoArray(Parts, TEXT(","));
			for (const FString& Part : Parts)
			{
				if (const int32 Count = FCString::Atoi(*Part); Count > 0)
				{
					Counts.Add(Count);
				}
			}
		}
		if (Counts.IsEmpty())
		{
			Counts = DefaultCounts;
		}
		Output.Logf(TEXT("rts.Debug.Bench: %s to %s"), *Args[0], *FPaths::ConvertRelativePathToFull(FBenchRun::GetOutputDir(Args[0])));
		FBenchRun::Start(MakeShared<FBenchRun>(*World, Args[0], Counts));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice BenchCommand(
		TEXT("rts.Debug.Bench"),
		TEXT("rts.Debug.Bench <UnitData>[+UnitData...] [Count,Count,...]: measures that many of the unit, or of the mix in turn (by default 1, 30, 120 and 240) ")
		TEXT("standing in a grid and driving on a move order, from a fixed camera, to Saved/Bench/<UnitData>."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunBenchCommand));
}

#endif
