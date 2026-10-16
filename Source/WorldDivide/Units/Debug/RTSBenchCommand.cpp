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
#include "Dom/JsonObject.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "Units/Debug/RTSBenchPlan.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#include "WorldDivide.h"

/**
 * rts.Debug.Bench <UnitData> [Count,Count,...]: the performance benchmark of one unit type (ue.ps1 -Task bench), on
 * L_Bench, or of a mix of sizes named UnitA+UnitB+..., which the grid takes in turn and spaces for the largest.
 * For each count (by default 1, 30, 120 and 240) that many of the unit stand in a grid spaced like a formation,
 * and are measured standing still, then driving on one move order issued as the player issues it. Every scenario is seen
 * from the same fixed camera, at a fixed 1920x1080 with dynamic resolution off and the screen percentage at 100, without a
 * frame rate cap. Logged and written to Saved/Bench/<UnitData>/bench.csv and bench.md per scenario: the frame, game thread,
 * render thread, RHI thread and GPU times, the slowest game thread frame, draw calls, triangles drawn, the avoidance pass
 * and the weapons' target search, the vehicles' locomotion (#138), the memory in use and its peak, and the internal render resolution. The table's header
 * names the CPU, GPU, driver, RHI and build it was measured on. With -RTSBenchQuit on the command
 * line the game quits when done, with exit code 1 on failure.
 *
 * The long method (a third argument "long", ue.ps1 -Long; #138) warms each scenario up for 10 s and then measures 60 s samples,
 * three at the largest count and one below it, each its own row with the 95th percentile of the game thread next to the mean.
 * Its units keep driving: the move order goes back and forth between the grid's place and a point 30 m west every 15 s.
 *
 * The quick method (a third argument "quick", ue.ps1 -Quick; #167) is the long scene for iterating: 240 units by default,
 * moving only, a 10 s warm-up and one 30 s sample, the same columns. It is diagnostic and never the gate. The numbers of each
 * method are FRTSBenchPlan's. Beside bench.csv and bench.md, bench.json records the method, the scene and the machine, so a
 * stored baseline is only compared with a run of the same kind (ue.ps1 -Compare).
 *
 * The command given while a run is going is queued: ue.ps1 runs several unit sets one after another in one game session, each
 * its own run with its own camera, settings, warm-up, samples and output folder, its units cleared before the next starts.
 * -RTSBenchQuit quits after the last.
 */
namespace RTSBenchCommand
{
	const TCHAR* QuitParam = TEXT("RTSBenchQuit");
	const TCHAR* UnitDataPath = TEXT("/Game/Data/Units");

	constexpr double ReadyTimeout = 60.0;
	constexpr double ResizeTimeout = 10.0;
	const FIntPoint ScreenSize(1920, 1080);

	/** Real seconds the units of a count get to clear, and that a queued run waits before it starts. */
	constexpr double ClearSeconds = 1.0;

	/** Real seconds between the long scene's move orders, which alternate between the point ahead and the grid's place. */
	constexpr double LongMoveSeconds = 15.0;
	/** Frames a timed sample keeps room for without growing: 60 s above 500 frames per second. */
	constexpr int32 LongFrameCapacity = 32768;

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

	/**
	 * The long method's grid and drive: its units shoot at whatever comes into range while they stand, and over minutes 240
	 * of them would destroy the enemy's headquarters and end the match. So the grid stands south-west of the middle, every
	 * unit out of range of the headquarters in the north-east corner, and drives the other way: half as far, to the west.
	 */
	const FVector LongGridCentre(-3000.f, -1000.f, 0.f);
	constexpr float LongMoveDistance = -3000.f;
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
		/** The long method's sample, from 1; 0 for the short method. */
		int32 Sample = 0;
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
		double LocomotionMs = 0.0;
		double MaxLocomotionMs = 0.0;
		/** Each frame's game thread and locomotion time, kept for the percentiles; reserved up front so measuring never allocates. */
		TArray<float> GameFrames;
		TArray<float> LocomotionFrames;
		double P95GameMs = 0.0;
		double P95LocomotionMs = 0.0;

		void Add(double InFrameMs, double InGameMs, double InRenderMs, double InRHIMs, double InGPUMs, double InDrawCalls,
			double InTriangles, double InAvoidanceMs, double InSearchMs, double InLocomotionMs)
		{
			LocomotionMs += InLocomotionMs;
			MaxLocomotionMs = FMath::Max(MaxLocomotionMs, InLocomotionMs);
			if (LocomotionFrames.Num() < LocomotionFrames.Max())
			{
				LocomotionFrames.Add(InLocomotionMs);
			}
			++Frames;
			FrameMs += InFrameMs;
			GameMs += InGameMs;
			MaxGameMs = FMath::Max(MaxGameMs, InGameMs);
			if (GameFrames.Num() < GameFrames.Max())
			{
				GameFrames.Add(InGameMs);
			}
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

		/** The 95th percentiles of the kept frames, once the sample is complete. */
		void TakePercentiles()
		{
			auto P95 = [](TArray<float>& Frames)
			{
				if (Frames.IsEmpty())
				{
					return 0.0;
				}
				Frames.Sort();
				const double Value = Frames[FMath::Min(Frames.Num() - 1, FMath::CeilToInt(0.95 * Frames.Num()) - 1)];
				Frames.Empty();
				return Value;
			};
			P95GameMs = P95(GameFrames);
			P95LocomotionMs = P95(LocomotionFrames);
		}

		static FString CsvHeader()
		{
			return TEXT("count,scenario,sample,frames,units,frame_ms,game_ms,p95_game_ms,max_game_ms,render_ms,rhi_ms,gpu_ms,draw_calls,triangles,")
				TEXT("avoidance_ms,max_avoidance_ms,search_ms,locomotion_ms,p95_locomotion_ms,max_locomotion_ms,used_memory_mb,peak_memory_mb,render_width,render_height");
		}

		FString ToCsv() const
		{
			return FString::Printf(TEXT("%d,%s,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%.0f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%d,%d"),
				Count, *Scenario, Sample, Frames, Units, Mean(FrameMs), Mean(GameMs), P95GameMs, MaxGameMs, Mean(RenderMs), Mean(RHIMs), Mean(GPUMs),
				Mean(DrawCalls), Mean(Triangles), Mean(AvoidanceMs), MaxAvoidanceMs, Mean(SearchMs), Mean(LocomotionMs), P95LocomotionMs,
				MaxLocomotionMs, UsedMemoryMB, PeakMemoryMB, RenderSize.X, RenderSize.Y);
		}

		static FString MarkdownHeader()
		{
			return TEXT("| Units | Scenario | Sample | Frame ms | Game ms (p95, max) | Render ms | RHI ms | GPU ms | Draw calls | Triangles | ")
				TEXT("Avoidance ms (max) | Target search ms | Locomotion ms (p95, max) | Memory MB (peak) | Render res |\n")
				TEXT("|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|\n");
		}

		FString ToMarkdown() const
		{
			return FString::Printf(TEXT("| %d | %s | %s | %.2f | %.2f (%.2f, %.2f) | %.2f | %.2f | %.2f | %.0f | %.2f M | %.3f (%.3f) | %.3f | %.3f (%.3f, %.3f) | %.0f (%.0f) | %dx%d |\n"),
				Count, *Scenario, Sample > 0 ? *FString::Printf(TEXT("%d (%d frames)"), Sample, Frames) : TEXT("-"), Mean(FrameMs),
				Mean(GameMs), P95GameMs, MaxGameMs, Mean(RenderMs), Mean(RHIMs), Mean(GPUMs), Mean(DrawCalls), Mean(Triangles) / 1.0e6,
				Mean(AvoidanceMs), MaxAvoidanceMs, Mean(SearchMs), Mean(LocomotionMs), P95LocomotionMs, MaxLocomotionMs, UsedMemoryMB,
				PeakMemoryMB, RenderSize.X, RenderSize.Y);
		}
	};

	class FBenchRun : public TSharedFromThis<FBenchRun>
	{
	public:
		FBenchRun(UWorld& InWorld, const FString& InUnitName, const FRTSBenchPlan& InPlan)
			: World(&InWorld), UnitName(InUnitName), Counts(InPlan.Counts), Plan(InPlan)
		{
		}

		static bool IsRunning() { return Active.IsValid(); }

		/** Starts Run, or queues it behind the run going on; returns its place in the queue, 0 when it starts now. */
		static int32 StartOrQueue(const TSharedRef<FBenchRun>& Run)
		{
			if (IsRunning())
			{
				Queued.Add(Run);
				return Queued.Num();
			}
			bAnyFailed = false;
			Start(Run, 0.0);
			return 0;
		}

		static void Start(const TSharedRef<FBenchRun>& Run, double Delay)
		{
			Active = Run;
			Run->StartDelay = Delay;
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
				if (FPlatformTime::Seconds() - StepStart >= Plan.IdleWarmupSeconds)
				{
					if (Plan.bIdle)
					{
						BeginMeasure(TEXT("idle"), EStep::MeasureIdle);
					}
					else
					{
						StartMove();
					}
				}
				return true;
			case EStep::MeasureIdle:
				if (Measure() && !NextSample(EStep::MeasureIdle))
				{
					StartMove();
				}
				return true;
			case EStep::LeadMove:
				// The order's own frame is the dispatch; its cost shows in the slowest game thread frame of the move.
				TakeFrame(Lead);
				KeepMoving();
				if (FPlatformTime::Seconds() - StepStart >= Plan.MoveWarmupSeconds)
				{
					BeginMeasure(TEXT("move"), EStep::MeasureMove);
				}
				return true;
			case EStep::MeasureMove:
				KeepMoving();
				if (Measure() && !NextSample(EStep::MeasureMove))
				{
					++CountIndex;
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

		void StartMove()
		{
			Lead = FScenarioStats();
			IssueMove();
			GoTo(EStep::LeadMove);
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
			if (FPlatformTime::Seconds() - StepStart < StartDelay)
			{
				return true;
			}
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
			const FVector LookAt = GetCameraLookAt();
			Camera->SetActorLocationAndRotation(LookAt - Rotation.Vector() * CameraDistance, Rotation);
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
				ARTSUnit* First = SpawnUnit(GetGridCentre() + FVector(0.f, 0.f, 10000.f * (Index + 1)), Index);
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
				const FVector Location = GetGridCentre() + FVector(Index / Columns * Spacing.X - Size.X / 2.f,
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

		FVector GetGridCentre() const { return Plan.bLongScene ? LongGridCentre : GridCentre; }

		FVector GetCameraLookAt() const
		{
			return Plan.bLongScene ? LongGridCentre + FVector(LongMoveDistance / 2.f, 0.f, 0.f) : CameraLookAt;
		}

		/** The long scene's units keep driving: back to the grid's place, then ahead again, each LongMoveSeconds. */
		void KeepMoving()
		{
			if (Plan.bLongScene && FPlatformTime::Seconds() - LastMoveTime >= LongMoveSeconds)
			{
				IssueMove();
			}
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
			Move.TargetLocation = GetGridCentre() + FVector(bMoveBack ? 0.f : Plan.bLongScene ? LongMoveDistance : MoveDistance, 0.f, 0.f);
			GetController()->Server_IssueCommand(Ordered, Move);
			bMoveBack = !bMoveBack;
			LastMoveTime = FPlatformTime::Seconds();
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
			bMoveBack = false;
			GEngine->ForceGarbageCollection(true);
		}

		void BeginMeasure(const TCHAR* Scenario, EStep Next)
		{
			Current = FScenarioStats();
			Current.Scenario = Scenario;
			Current.Count = Counts[CountIndex];
			Current.Sample = Plan.IsTimed() ? 1 : 0;
			Current.GameFrames.Reserve(Plan.IsTimed() ? LongFrameCapacity : Plan.SampleFrames);
			Current.LocomotionFrames.Reserve(Plan.IsTimed() ? LongFrameCapacity : Plan.SampleFrames);
			// The move's slowest frame includes the order's own frame, before the measured ones.
			Current.MaxGameMs = Next == EStep::MeasureMove ? Lead.MaxGameMs : 0.0;
			URTSWeaponComponent::TakeSearchSeconds();
			URTSUnitMovement::TakeLocomotionSeconds();
			TRACE_BEGIN_REGION(*RegionName());
			GoTo(Next);
		}

		FString RegionName() const
		{
			return Current.Sample > 0 ? FString::Printf(TEXT("%s_%d_%s_%d"), *UnitName, Current.Count, *Current.Scenario, Current.Sample)
				: FString::Printf(TEXT("%s_%d_%s"), *UnitName, Current.Count, *Current.Scenario);
		}

		/** Starts a timed method's next sample of the same scenario; false once the scenario has all of them. */
		bool NextSample(EStep MeasureStep)
		{
			if (!Plan.IsTimed() || Current.Sample >= Plan.GetSamples(Counts[CountIndex]))
			{
				return false;
			}
			const int32 Sample = Current.Sample + 1;
			const FString Scenario = Current.Scenario;
			Current = FScenarioStats();
			Current.Scenario = Scenario;
			Current.Count = Counts[CountIndex];
			Current.Sample = Sample;
			Current.GameFrames.Reserve(LongFrameCapacity);
			Current.LocomotionFrames.Reserve(LongFrameCapacity);
			TRACE_BEGIN_REGION(*RegionName());
			GoTo(MeasureStep);
			return true;
		}

		/** Adds this frame's numbers to Stats. */
		void TakeFrame(FScenarioStats& Stats) const
		{
			const URTSAvoidanceSubsystem* Avoidance = World->GetSubsystem<URTSAvoidanceSubsystem>();
			Stats.Add(FApp::GetDeltaTime() * 1000.0, FPlatformTime::ToMilliseconds(GGameThreadTime),
				FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(GRHIThreadTime),
				FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)), GNumDrawCallsRHI[0], GNumPrimitivesDrawnRHI[0],
				Avoidance ? Avoidance->GetLastSeparateSeconds() * 1000.0 : 0.0, URTSWeaponComponent::TakeSearchSeconds() * 1000.0,
				URTSUnitMovement::TakeLocomotionSeconds() * 1000.0);
		}

		/** Takes one frame of the scenario; true once it has all of them, logged and kept. */
		bool Measure()
		{
			TakeFrame(Current);
			if (Plan.IsTimed() ? FPlatformTime::Seconds() - StepStart < Plan.SampleSeconds : StepFrames < Plan.SampleFrames)
			{
				return false;
			}
			TRACE_END_REGION(*RegionName());
			Current.TakePercentiles();
			Current.Units = Units.Num();
			const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
			Current.UsedMemoryMB = Memory.UsedPhysical / (1024.0 * 1024.0);
			Current.PeakMemoryMB = Memory.PeakUsedPhysical / (1024.0 * 1024.0);
			Current.RenderSize = GetRenderSize();
			Results.Add(Current);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: %s"), *Current.ToCsv());
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
			FString Markdown = FString::Printf(TEXT("%s on %s, %s, %s method%s\n\n- CPU: %s\n- GPU: %s, driver %s, %s\n- Build: %s\n\n"),
				*UnitName, *World->GetMapName(), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")), FRTSBenchPlan::GetMethodName(Plan.Method),
				Plan.Method == ERTSBenchMethod::Quick ? TEXT(" (diagnostic, not gate evidence)") : TEXT(""),
				*GetCPUName(), *GRHIAdapterName, *GRHIAdapterUserDriverVersion, GetRHIName(), LexToString(FApp::GetBuildConfiguration()));
			Markdown += FScenarioStats::MarkdownHeader();
			for (const FScenarioStats& Result : Results)
			{
				Csv += Result.ToCsv() + TEXT("\n");
				Markdown += Result.ToMarkdown();
			}
			return FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Dir, TEXT("bench.csv")))
				&& FFileHelper::SaveStringToFile(Markdown, *FPaths::Combine(Dir, TEXT("bench.md")))
				&& FFileHelper::SaveStringToFile(DescribeRun(), *FPaths::Combine(Dir, TEXT("bench.json")));
		}

		static FString GetCPUName() { return FPlatformMisc::GetCPUBrand().TrimStartAndEnd(); }
		static const TCHAR* GetRHIName() { return GDynamicRHI ? GDynamicRHI->GetName() : TEXT("no RHI"); }

		/**
		 * bench.json: what a stored baseline must share with a run to be compared with it (ue.ps1 -Compare): the method, the
		 * scene and the machine. ue.ps1 adds the commit and the content's hashes.
		 */
		FString DescribeRun() const
		{
			auto Numbers = [](const TArray<int32>& Values)
			{
				TArray<TSharedPtr<FJsonValue>> Array;
				for (const int32 Value : Values)
				{
					Array.Add(MakeShared<FJsonValueNumber>(Value));
				}
				return Array;
			};
			auto Vector = [](const FVector& Value)
			{
				return TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y),
					MakeShared<FJsonValueNumber>(Value.Z) };
			};

			const TSharedRef<FJsonObject> Method = MakeShared<FJsonObject>();
			Method->SetStringField(TEXT("name"), FRTSBenchPlan::GetMethodName(Plan.Method));
			Method->SetNumberField(TEXT("version"), FRTSBenchPlan::Version);
			Method->SetBoolField(TEXT("gate"), Plan.Method == ERTSBenchMethod::Long);
			Method->SetArrayField(TEXT("counts"), Numbers(Counts));
			TArray<TSharedPtr<FJsonValue>> Scenarios;
			if (Plan.bIdle)
			{
				Scenarios.Add(MakeShared<FJsonValueString>(TEXT("idle")));
			}
			Scenarios.Add(MakeShared<FJsonValueString>(TEXT("move")));
			Method->SetArrayField(TEXT("scenarios"), Scenarios);
			Method->SetNumberField(TEXT("idle_warmup_s"), Plan.IdleWarmupSeconds);
			Method->SetNumberField(TEXT("move_warmup_s"), Plan.MoveWarmupSeconds);
			Method->SetNumberField(TEXT("sample_s"), Plan.SampleSeconds);
			Method->SetNumberField(TEXT("sample_frames"), Plan.IsTimed() ? 0 : Plan.SampleFrames);
			Method->SetNumberField(TEXT("samples_at_largest"), Plan.IsTimed() ? Plan.SamplesAtLargest : 1);
			Method->SetNumberField(TEXT("samples_below"), Plan.IsTimed() ? Plan.SamplesBelow : 1);
			Method->SetBoolField(TEXT("long_scene"), Plan.bLongScene);

			const TSharedRef<FJsonObject> Scene = MakeShared<FJsonObject>();
			Scene->SetStringField(TEXT("map"), World->GetMapName());
			Scene->SetStringField(TEXT("units"), UnitName);
			Scene->SetArrayField(TEXT("screen"), Numbers({ ScreenSize.X, ScreenSize.Y }));
			const FIntPoint Render = Results.IsEmpty() ? FIntPoint::ZeroValue : Results.Last().RenderSize;
			Scene->SetArrayField(TEXT("render"), Numbers({ Render.X, Render.Y }));
			Scene->SetArrayField(TEXT("grid_centre"), Vector(GetGridCentre()));
			Scene->SetArrayField(TEXT("camera_look_at"), Vector(GetCameraLookAt()));
			Scene->SetNumberField(TEXT("camera_pitch"), CameraPitch);
			Scene->SetNumberField(TEXT("camera_distance"), CameraDistance);
			Scene->SetNumberField(TEXT("camera_fov"), CameraFOV);

			const TSharedRef<FJsonObject> Machine = MakeShared<FJsonObject>();
			Machine->SetStringField(TEXT("cpu"), GetCPUName());
			Machine->SetStringField(TEXT("gpu"), GRHIAdapterName);
			Machine->SetStringField(TEXT("driver"), GRHIAdapterUserDriverVersion);
			Machine->SetStringField(TEXT("rhi"), GetRHIName());
			Machine->SetStringField(TEXT("build"), LexToString(FApp::GetBuildConfiguration()));

			const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetStringField(TEXT("date"), FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")));
			Root->SetObjectField(TEXT("method"), Method);
			Root->SetObjectField(TEXT("scene"), Scene);
			Root->SetObjectField(TEXT("machine"), Machine);
			FString Json;
			FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json));
			return Json;
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
				UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.Bench failed: %s: %s"), *UnitName, *Error);
				bAnyFailed = true;
			}
			Active.Reset();
			// The next set starts after its units' clear time, on a world with none of this set's units left.
			if (!Queued.IsEmpty())
			{
				const TSharedRef<FBenchRun> Next = Queued[0];
				Queued.RemoveAt(0);
				Start(Next, ClearSeconds);
			}
			else if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, bAnyFailed ? 1 : 0);
			}
			return false;
		}

		static TSharedPtr<FBenchRun> Active;
		/** The runs given while one was going, started in turn. */
		static TArray<TSharedRef<FBenchRun>> Queued;
		/** Whether any run of the current queue failed. */
		static bool bAnyFailed;

		TWeakObjectPtr<UWorld> World;
		FString UnitName;
		TArray<int32> Counts;
		FRTSBenchPlan Plan;
		/** Real seconds the run waits before it looks for the player: a queued run's clear time. */
		double StartDelay = 0.0;
		/** The mix, held through the garbage collection between counts, when no unit refers to it. */
		TArray<TStrongObjectPtr<URTSUnitData>> UnitData;
		int32 TeamId = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<TWeakObjectPtr<ARTSUnit>> Units;

		EStep Step = EStep::WaitForPlayer;
		int32 StepFrames = 0;
		double StepStart = 0.0;
		int32 CountIndex = 0;
		bool bMoveBack = false;
		double LastMoveTime = 0.0;
		FScenarioStats Current;
		/** The frames between the move order and the measured ones. */
		FScenarioStats Lead;
		TArray<FScenarioStats> Results;

		TMap<FString, FString> SavedCVars;
		bool bSavedSmoothFrameRate = false;
	};

	TSharedPtr<FBenchRun> FBenchRun::Active;
	TArray<TSharedRef<FBenchRun>> FBenchRun::Queued;
	bool FBenchRun::bAnyFailed = false;

	void RunBenchCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld() || Args.IsEmpty())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.Bench <UnitData> [Count,Count,...] [short|long|quick] needs a running game"));
			return;
		}
		ERTSBenchMethod Method = ERTSBenchMethod::Short;
		if (Args.Num() > 2 && !FRTSBenchPlan::ParseMethod(Args[2], Method))
		{
			Output.Logf(ELogVerbosity::Error, TEXT("rts.Debug.Bench: no method named %s (short, long or quick)"), *Args[2]);
			return;
		}
		// A count of 0 (or none) takes the method's default counts, so "quick" can follow without them.
		const FRTSBenchPlan Plan = FRTSBenchPlan::Make(Method, Args.Num() > 1 ? FRTSBenchPlan::ParseCounts(Args[1]) : TArray<int32>());
		const int32 Place = FBenchRun::StartOrQueue(MakeShared<FBenchRun>(*World, Args[0], Plan));
		Output.Logf(TEXT("rts.Debug.Bench: %s, %s method, to %s%s"), *Args[0], FRTSBenchPlan::GetMethodName(Method),
			*FPaths::ConvertRelativePathToFull(FBenchRun::GetOutputDir(Args[0])),
			Place > 0 ? *FString::Printf(TEXT(", queued %d"), Place) : TEXT(""));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice BenchCommand(
		TEXT("rts.Debug.Bench"),
		TEXT("rts.Debug.Bench <UnitData>[+UnitData...] [Count,Count,...|0] [short|long|quick]: measures that many of the unit, or of the mix in turn (by default 1, 30, 120 and 240; ")
		TEXT("quick: 240) standing in a grid and driving on a move order, from a fixed camera, to Saved/Bench/<UnitData>. Given while a run is going, it is queued."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunBenchCommand));
}

#endif
