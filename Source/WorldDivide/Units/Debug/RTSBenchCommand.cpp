#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Algo/Count.h"
#include "Buildings/RTSBuilding.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Containers/Ticker.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSUnitData.h"
#include "DynamicRHI.h"
#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSPlayerController.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RHI.h"
#include "StaticMeshResources.h"
#include "RenderTimer.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "Units/Debug/RTSBenchGate.h"
#include "Units/Debug/RTSBenchPlan.h"
#include "Units/Debug/RTSBenchScene.h"
#include "Units/Debug/RTSUnitLOD.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSRunningGearSubsystem.h"
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
 * method are FRTSBenchPlan's. The screen method (a third argument "screen", ue.ps1 -Screen; #171) is the quick scene with one
 * 15 s sample, the first tier of the full-roster gate (ue.ps1 -Task gate). Beside bench.csv and bench.md, bench.json records the method, the scene and the machine, so a
 * stored baseline is only compared with a run of the same kind (ue.ps1 -Compare).
 *
 * Every run checks its own rows (#171, RTSBenchGate::CheckRun): a sample cut short, too few frames, stalls, or samples of one
 * scenario that disagree mark it invalid in bench.json's run section. That section also records when it ran, whether a trace
 * was recording, the engine and what the set is made of, so a contaminated run is never stored or judged as a measurement.
 * -RTSBenchFrames also writes every measured frame to frames.csv; -RTSBenchSampleSeconds=N changes the timed samples' length
 * for the method studies of #171 (recorded in the method, so such a run never matches a baseline).
 *
 * The command given while a run is going is queued: ue.ps1 runs several unit sets one after another in one game session, each
 * its own run with its own camera, settings, warm-up, samples and output folder, its units cleared before the next starts.
 * -RTSBenchQuit quits after the last.
 *
 * A set named with RTSBenchScene::SolidsSuffix (DA_TestM20Guardian@Solids; #172) benches among solids the navmesh leaves
 * out, which RTSBenchScene lays out over the ground each count's grid covers and drives over. The suffix is part of the set's
 * name, so its output and baselines have their own folder, and bench.json's scene names the layout.
 *
 * A set named with RTSBenchScene::RigidTracksSuffix (DA_TestM20Guardian@RigidTracks; #151) benches its units with tracks that
 * do not move their links (rts.Vehicle.TrackAnimation 0 while it runs), against the animated set without the suffix.
 * RTSBenchScene::AllTracksSuffix (@AllTracks; #212) animates every track instead, however far from the camera
 * (rts.Vehicle.TrackAnimationDistance 0 while it runs). Every row counts the tracks' phases sent to their materials per
 * frame, and at the sample's end the units' tracks, those drawn on screen and those of them the renderer moves the links of.
 *
 * A set named with RTSBenchScene::MixedTeamsSuffix (DA_TestM20Guardian@MixedTeams; #150) spreads its units over
 * RTSBenchScene::MixedTeams teams in turn, with their weapons idled, and orders them straight through RTSCommands::Dispatch,
 * since the player's own path only orders the player's units. It shows whether units of one kind in different team colours
 * still share their draws. The suffixes combine in any order.
 *
 * Every row also counts what the units draw with (#150): their primitive components, the material slots of those that
 * are dynamic instances, and the distinct materials over all of them.
 *
 * A set named with RTSBenchScene::ForceLOD0Suffix (DA_TestM20Guardian@LOD0; #149) draws every mesh at LOD0 (r.ForceLOD 0
 * while it runs), against the LODs the set without it picks; RTSBenchScene::NearSuffix and FarSuffix move the camera to
 * NearCameraDistance or FarCameraDistance. Every row also counts the units on screen at the sample's end by the LOD their
 * hull is drawn at, and how many of them have a part at another LOD (RTSUnitLOD).
 *
 * The occlusion suffixes (#214) change the occlusion queries of the units' parts: RTSBenchScene::ExactOcclusionSuffix gives
 * each its own query in every frame (rts.Render.GroupPartOcclusion 0), NoOcclusionSuffix turns hardware occlusion off and
 * HZBOcclusionSuffix tests the hierarchical Z-buffer instead, against the grouped occlusion the set without them has.
 * OccludedSuffix roofs the southern two thirds of the grid, so occlusion culling has units to leave out, and CloseSuffix
 * takes the camera to the RTS camera's close zoom and pitch. A set with one of RTSBenchScene's L_TestMap viewpoints
 * (DA_TestM20Guardian@TestMapBase, ue.ps1 -Map L_TestMap) clears the map's own units and stands its grid there. Every row
 * also counts the units' parts drawn on screen at the sample's end, so a hidden unit that is drawn anyway shows.
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

	const TCHAR* FramesParam = TEXT("RTSBenchFrames");
	const TCHAR* SampleSecondsParam = TEXT("RTSBenchSampleSeconds=");

	using RTSBenchScene::GridCentre;
	using RTSBenchScene::LongGridCentre;
	using RTSBenchScene::LongMoveDistance;
	using RTSBenchScene::MoveDistance;

	/** The fixed camera: looking down at the RTS pitch over the middle of the grid and the move, far enough to see both. */
	const FVector CameraLookAt(MoveDistance / 2.f, 0.f, 0.f);

	constexpr float CameraPitch = -55.f;
	constexpr float DefaultCameraDistance = 17000.f;
	constexpr float CameraFOV = 60.f;

	/** A part counts as drawn on screen when it was within this many seconds of the sample's end (#214). */
	constexpr float DrawnSeconds = 0.1f;

	/** The settings every scenario runs with, restored afterwards. */
	const TArray<TPair<const TCHAR*, const TCHAR*>> BenchCVars = {
		{ TEXT("r.DynamicRes.OperationMode"), TEXT("0") },
		{ TEXT("r.ScreenPercentage"), TEXT("100") },
		{ TEXT("r.VSync"), TEXT("0") },
		{ TEXT("t.MaxFPS"), TEXT("0") },
	};

	/** The settings a set's suffix changes while it runs, restored afterwards; set before its first count spawns. */
	const TArray<TTuple<const TCHAR*, const TCHAR*, const TCHAR*>> SuffixCVars = {
		{ RTSBenchScene::RigidTracksSuffix, TEXT("rts.Vehicle.TrackAnimation"), TEXT("0") },
		{ RTSBenchScene::AllTracksSuffix, TEXT("rts.Vehicle.TrackAnimationDistance"), TEXT("0") },
		{ RTSBenchScene::ForceLOD0Suffix, TEXT("r.ForceLOD"), TEXT("0") },
		{ RTSBenchScene::ExactOcclusionSuffix, TEXT("rts.Render.GroupPartOcclusion"), TEXT("0") },
		{ RTSBenchScene::NoOcclusionSuffix, TEXT("r.AllowOcclusionQueries"), TEXT("0") },
		{ RTSBenchScene::HZBOcclusionSuffix, TEXT("r.HZBOcclusion"), TEXT("1") },
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
		/** The running gear pass: wheels and tracks (URTSRunningGearSubsystem, #151). */
		double RunningGearMs = 0.0;
		/** What the units draw with at the sample's end (#150): primitive components, dynamic instance slots, materials. */
		int32 Primitives = 0;
		int32 DynamicMaterials = 0;
		int32 DistinctMaterials = 0;
		/** The units on screen at the sample's end by their hull's LOD (#149), and those with a part at another LOD. */
		TArray<int32, TInlineAllocator<4>> HullLODs;
		int32 MixedLODUnits = 0;
		/** The units' primitive components drawn on screen at the sample's end (#214): hidden ones only when nothing culls them. */
		int32 DrawnPrimitives = 0;
		/** The tracks' phases sent to their materials over the sample (#212), each one primitive data update. */
		int64 TrackUpdates = 0;
		/**
		 * At the sample's end (#212): the units' tracks, those drawn on screen, and those of them whose links the renderer
		 * moves: within their link shift's distance of the view and at an LOD drawn with the animated material.
		 */
		int32 Tracks = 0;
		int32 DrawnTracks = 0;
		int32 AnimatedTracks = 0;
		/**
		 * Each frame's game thread, locomotion, render thread, GPU and running gear time, kept for the percentiles; reserved
		 * up front so measuring never allocates.
		 */
		TArray<float> GameFrames;
		TArray<float> LocomotionFrames;
		TArray<float> RenderFrames;
		TArray<float> GPUFrames;
		TArray<float> RunningGearFrames;
		TArray<float> RHIFrames;
		double P95GameMs = 0.0;
		double P95LocomotionMs = 0.0;
		double P95RenderMs = 0.0;
		double P95GPUMs = 0.0;
		double P95RunningGearMs = 0.0;
		double P95RHIMs = 0.0;

		void Reserve(int32 Capacity)
		{
			for (TArray<float>* Kept : { &GameFrames, &LocomotionFrames, &RenderFrames, &GPUFrames, &RunningGearFrames, &RHIFrames })
			{
				Kept->Reserve(Capacity);
			}
		}
		/** The solids the navmesh leaves out (#168), summed over the frames: see FRTSSolidStats. */
		FRTSSolidStats Solids;
		/** Real seconds the sample lasted, its longest frame, and its frames over the stall and freeze limits (#171). */
		double SampleSeconds = 0.0;
		double MaxFrameMs = 0.0;
		int32 StallFrames = 0;
		int32 FreezeFrames = 0;
		double LastFrameMs = 0.0;
		double LastGameMs = 0.0;
		/** With -RTSBenchFrames: each frame's real seconds into the sample, frame time and game thread time. */
		TArray<FVector3f> KeptFrames;

		void AddSolids(const FRTSSolidStats& Frame)
		{
			Solids.Updates += Frame.Updates;
			Solids.Reuses += Frame.Reuses;
			Solids.Checks += Frame.Checks;
			Solids.ComponentQueries += Frame.ComponentQueries;
			Solids.BroadSeconds += Frame.BroadSeconds;
			Solids.NarrowSeconds += Frame.NarrowSeconds;
		}

		/** The share of solid checks that reused their neighbourhood instead of looking again. */
		double SolidReuseShare() const
		{
			const int32 Looks = Solids.Updates + Solids.Reuses;
			return Looks > 0 ? static_cast<double>(Solids.Reuses) / Looks : 0.0;
		}

		void Add(double InFrameMs, double InGameMs, double InRenderMs, double InRHIMs, double InGPUMs, double InDrawCalls,
			double InTriangles, double InAvoidanceMs, double InSearchMs, double InLocomotionMs, double InRunningGearMs)
		{
			auto Keep = [](TArray<float>& Kept, double Value)
			{
				if (Kept.Num() < Kept.Max())
				{
					Kept.Add(Value);
				}
			};
			LocomotionMs += InLocomotionMs;
			MaxLocomotionMs = FMath::Max(MaxLocomotionMs, InLocomotionMs);
			Keep(LocomotionFrames, InLocomotionMs);
			RunningGearMs += InRunningGearMs;
			Keep(RunningGearFrames, InRunningGearMs);
			Keep(RenderFrames, InRenderMs);
			Keep(GPUFrames, InGPUMs);
			Keep(RHIFrames, InRHIMs);
			++Frames;
			FrameMs += InFrameMs;
			MaxFrameMs = FMath::Max(MaxFrameMs, InFrameMs);
			StallFrames += InFrameMs > RTSBenchGate::Validity::StallFrameMs ? 1 : 0;
			FreezeFrames += InFrameMs > RTSBenchGate::Validity::FreezeFrameMs ? 1 : 0;
			LastFrameMs = InFrameMs;
			LastGameMs = InGameMs;
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
			P95RenderMs = P95(RenderFrames);
			P95GPUMs = P95(GPUFrames);
			P95RunningGearMs = P95(RunningGearFrames);
			P95RHIMs = P95(RHIFrames);
		}

		static FString CsvHeader()
		{
			return TEXT("count,scenario,sample,frames,units,frame_ms,game_ms,p95_game_ms,max_game_ms,render_ms,rhi_ms,gpu_ms,draw_calls,triangles,")
				TEXT("avoidance_ms,max_avoidance_ms,search_ms,locomotion_ms,p95_locomotion_ms,max_locomotion_ms,used_memory_mb,peak_memory_mb,render_width,render_height,")
				TEXT("solid_updates,solid_reuse_share,solid_checks,solid_queries,solid_broad_ms,solid_narrow_ms,")
				TEXT("sample_s,max_frame_ms,stall_frames,freeze_frames,p95_render_ms,p95_gpu_ms,running_gear_ms,p95_running_gear_ms,")
				TEXT("primitives,dynamic_materials,distinct_materials,lod0_units,lod1_units,lod2_units,lod3_units,mixed_lod_units,p95_rhi_ms,drawn_primitives,")
				TEXT("track_updates,tracks,drawn_tracks,animated_tracks");
		}

		FString ToCsv() const
		{
			return FString::Printf(TEXT("%d,%s,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%.0f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%d,%d,%.2f,%.3f,%.2f,%.2f,%.4f,%.4f,%.2f,%.2f,%d,%d,%.2f,%.2f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%d,%.1f,%d,%d,%d"),
				Count, *Scenario, Sample, Frames, Units, Mean(FrameMs), Mean(GameMs), P95GameMs, MaxGameMs, Mean(RenderMs), Mean(RHIMs), Mean(GPUMs),
				Mean(DrawCalls), Mean(Triangles), Mean(AvoidanceMs), MaxAvoidanceMs, Mean(SearchMs), Mean(LocomotionMs), P95LocomotionMs,
				MaxLocomotionMs, UsedMemoryMB, PeakMemoryMB, RenderSize.X, RenderSize.Y, Mean(Solids.Updates), SolidReuseShare(),
				Mean(Solids.Checks), Mean(Solids.ComponentQueries), Mean(Solids.BroadSeconds * 1000.0), Mean(Solids.NarrowSeconds * 1000.0),
				SampleSeconds, MaxFrameMs, StallFrames, FreezeFrames, P95RenderMs, P95GPUMs, Mean(RunningGearMs), P95RunningGearMs,
				Primitives, DynamicMaterials, DistinctMaterials, HullLOD(0), HullLOD(1), HullLOD(2), HullLOD(3), MixedLODUnits, P95RHIMs, DrawnPrimitives,
				Mean(TrackUpdates), Tracks, DrawnTracks, AnimatedTracks);
		}

		int32 HullLOD(int32 LOD) const { return HullLODs.IsValidIndex(LOD) ? HullLODs[LOD] : 0; }

		/** The row as the gate reads it back from bench.csv. */
		RTSBenchGate::FRow ToGateRow() const
		{
			RTSBenchGate::FRow Row;
			Row.Count = Count;
			Row.Scenario = Scenario;
			Row.Sample = Sample;
			Row.Frames = Frames;
			Row.FrameMs = Mean(FrameMs);
			Row.GameMs = Mean(GameMs);
			Row.P95GameMs = P95GameMs;
			Row.MaxGameMs = MaxGameMs;
			Row.RenderMs = Mean(RenderMs);
			Row.RHIMs = Mean(RHIMs);
			Row.GPUMs = Mean(GPUMs);
			Row.AvoidanceMs = Mean(AvoidanceMs);
			Row.LocomotionMs = Mean(LocomotionMs);
			Row.P95LocomotionMs = P95LocomotionMs;
			Row.MaxLocomotionMs = MaxLocomotionMs;
			Row.SampleSeconds = SampleSeconds;
			Row.MaxFrameMs = MaxFrameMs;
			Row.StallFrames = StallFrames;
			Row.FreezeFrames = FreezeFrames;
			return Row;
		}

		static FString MarkdownHeader()
		{
			return TEXT("| Units | Scenario | Sample | Frame ms | Game ms (p95, max) | Render ms (p95) | RHI ms (p95) | GPU ms (p95) | Draw calls | Triangles | ")
				TEXT("Avoidance ms (max) | Target search ms | Locomotion ms (p95, max) | Running gear ms (p95) | Memory MB (peak) | Render res |\n")
				TEXT("|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|\n");
		}

		FString ToMarkdown() const
		{
			return FString::Printf(TEXT("| %d | %s | %s | %.2f | %.2f (%.2f, %.2f) | %.2f (%.2f) | %.2f (%.2f) | %.2f (%.2f) | %.0f | %.2f M | %.3f (%.3f) | %.3f | %.3f (%.3f, %.3f) | %.3f (%.3f) | %.0f (%.0f) | %dx%d |\n"),
				Count, *Scenario, Sample > 0 ? *FString::Printf(TEXT("%d (%d frames)"), Sample, Frames) : TEXT("-"), Mean(FrameMs),
				Mean(GameMs), P95GameMs, MaxGameMs, Mean(RenderMs), P95RenderMs, Mean(RHIMs), P95RHIMs, Mean(GPUMs), P95GPUMs, Mean(DrawCalls),
				Mean(Triangles) / 1.0e6, Mean(AvoidanceMs), MaxAvoidanceMs, Mean(SearchMs), Mean(LocomotionMs), P95LocomotionMs,
				MaxLocomotionMs, Mean(RunningGearMs), P95RunningGearMs, UsedMemoryMB, PeakMemoryMB, RenderSize.X, RenderSize.Y);
		}

		FString ToSolidsMarkdown() const
		{
			return FString::Printf(TEXT("| %d | %s | %d | %.2f | %.1f%% | %.2f | %.2f | %.4f | %.4f |\n"), Count, *Scenario, Sample,
				Mean(Solids.Updates), SolidReuseShare() * 100.0, Mean(Solids.Checks), Mean(Solids.ComponentQueries),
				Mean(Solids.BroadSeconds * 1000.0), Mean(Solids.NarrowSeconds * 1000.0));
		}

		FString ToTracksMarkdown() const
		{
			return FString::Printf(TEXT("| %d | %s | %d | %.1f | %d | %d | %d |\n"), Count, *Scenario, Sample, Mean(TrackUpdates), Tracks,
				DrawnTracks, AnimatedTracks);
		}

		FString ToMaterialsMarkdown() const
		{
			return FString::Printf(TEXT("| %d | %s | %d | %d | %d | %d | %d | %d / %d / %d / %d | %d |\n"), Count, *Scenario, Sample, Primitives,
				DrawnPrimitives, DynamicMaterials, DistinctMaterials, HullLOD(0), HullLOD(1), HullLOD(2), HullLOD(3), MixedLODUnits);
		}
	};

	class FBenchRun : public TSharedFromThis<FBenchRun>
	{
	public:
		FBenchRun(UWorld& InWorld, const FString& InUnitName, const FRTSBenchPlan& InPlan)
			: World(&InWorld), UnitName(InUnitName), Counts(InPlan.Counts), Plan(InPlan)
			, bSolids(InUnitName.Contains(RTSBenchScene::SolidsSuffix))
			, bRigidTracks(InUnitName.Contains(RTSBenchScene::RigidTracksSuffix))
			, bMixedTeams(InUnitName.Contains(RTSBenchScene::MixedTeamsSuffix))
			, bForceLOD0(InUnitName.Contains(RTSBenchScene::ForceLOD0Suffix))
			, bOccluded(InUnitName.Contains(RTSBenchScene::OccludedSuffix))
			, bClose(InUnitName.Contains(RTSBenchScene::CloseSuffix))
			, Viewpoint(RTSBenchScene::FindViewpoint(InUnitName))
			, CameraDistance(bClose ? RTSBenchScene::CloseCameraDistance : InUnitName.Contains(RTSBenchScene::NearSuffix) ? RTSBenchScene::NearCameraDistance
				: InUnitName.Contains(RTSBenchScene::FarSuffix) ? RTSBenchScene::FarCameraDistance : DefaultCameraDistance)
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
			Run->StartTime = FDateTime::UtcNow();
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
			if (Viewpoint && World->GetMapName() != RTSBenchScene::TestMapName)
			{
				return Finish(FString::Printf(TEXT("%s is a view of %s, not of %s (ue.ps1 -Map %s)"), Viewpoint->Suffix,
					RTSBenchScene::TestMapName, *World->GetMapName(), RTSBenchScene::TestMapName));
			}
			// A mix of sizes is several names joined by '+'; the grid takes them in turn. The suffixes follow the last name.
			TArray<FString> Names;
			int32 FirstSuffix = INDEX_NONE;
			const FString Mix = UnitName.FindChar(TEXT('@'), FirstSuffix) ? UnitName.Left(FirstSuffix) : UnitName;
			Mix.ParseIntoArray(Names, TEXT("+"));
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
			if (Viewpoint)
			{
				ClearMapUnits();
			}
			if (const ARTSCameraPawn* Pawn = bClose ? Controller->GetPawn<ARTSCameraPawn>() : nullptr)
			{
				Pitch = Pawn->GetPitchForZoom(CameraDistance);
			}
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
			// Some are read when the units are made or make their parts, so all are set before the first count spawns.
			for (const TTuple<const TCHAR*, const TCHAR*, const TCHAR*>& Setting : SuffixCVars)
			{
				if (IConsoleVariable* CVar = UnitName.Contains(Setting.Get<0>()) ? IConsoleManager::Get().FindConsoleVariable(Setting.Get<1>()) : nullptr)
				{
					SavedCVars.Add(Setting.Get<1>(), CVar->GetString());
					CVar->Set(Setting.Get<2>(), ECVF_SetByConsole);
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
			AimCamera();
			GetController()->SetViewTarget(Camera.Get());
		}

		void AimCamera()
		{
			const FRotator Rotation(Pitch, Viewpoint ? Viewpoint->Yaw : 0.f, 0.f);
			Camera->SetActorLocationAndRotation(GetCameraLookAt() - Rotation.Vector() * CameraDistance, Rotation);
		}

		/** A viewpoint's map comes with units of its own; only the grid is measured. */
		void ClearMapUnits()
		{
			int32 Cleared = 0;
			for (TActorIterator<ARTSUnit> It(World.Get()); It; ++It)
			{
				if (AController* Controller = It->GetController())
				{
					Controller->Destroy();
				}
				It->Destroy();
				++Cleared;
			}
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: cleared %d of %s's own units"), Cleared, *World->GetMapName());
		}

		/** Where a viewpoint's grid of Count units goes, and the camera on it; false when the map lacks its headquarters. */
		bool PlaceViewpointGrid(int32 Count, const FVector2D& Spacing, const FVector2D& Largest)
		{
			int32 Rows = 0;
			int32 Columns = 0;
			RTSBenchScene::MakeGrid(Count, Spacing, FVector::ZeroVector, Rows, Columns);
			const FVector2D Size((Rows - 1) * Spacing.X + Largest.X, (Columns - 1) * Spacing.Y + Largest.Y);
			FVector2D Anchor = Viewpoint->Anchor;
			if (Viewpoint->bBehindHeadquarters)
			{
				const ARTSBuilding* Headquarters = nullptr;
				for (TActorIterator<ARTSBuilding> It(World.Get()); It && !Headquarters; ++It)
				{
					Headquarters = It->GetTeamId() == 0 ? *It : nullptr;
				}
				if (!Headquarters)
				{
					return false;
				}
				// The grid's side that Align points to faces the headquarters, HeadquartersGap from its edge.
				const FBox Box = Headquarters->GetComponentsBoundingBox(/*bNonColliding*/ true);
				Anchor = FVector2D(Box.GetCenter()) - Viewpoint->Align * (FVector2D(Box.GetExtent()) + RTSBenchScene::HeadquartersGap);
			}
			ViewGridCentre = FVector(RTSBenchScene::PlaceGrid(Anchor, Viewpoint->Align, Size), 0.f);
			AimCamera();
			return true;
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
				Spawned->InitFromData(UnitData[Index % UnitData.Num()].Get(), bMixedTeams ? TeamId + Index % RTSBenchScene::MixedTeams : TeamId);
				Spawned->FinishSpawning(Transform);
				if (bMixedTeams)
				{
					Spawned->GetWeaponComponent()->SetComponentTickEnabled(false);
				}
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
				const TInlineComponentArray<UPrimitiveComponent*> Primitives(First);
				KindComponents.FindOrAdd(Index) = Primitives.Num();
			}
			const FVector2D Spacing(Largest.X + RTSBenchScene::RowGap, Largest.Y + RTSBenchScene::ColumnGap);
			if (Viewpoint && !PlaceViewpointGrid(Count, Spacing, Largest))
			{
				return false;
			}
			int32 Rows = 0;
			int32 Columns = 0;
			const TArray<FVector> Places = RTSBenchScene::MakeGrid(Count, Spacing, GetGridCentre(), Rows, Columns);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector& Location = Places[Index];
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
			if (bSolids)
			{
				SpawnSolids();
			}
			if (bOccluded)
			{
				SpawnCover();
			}
			return true;
		}

		/** The roof of an "@Occluded" set over the southern part of the grid and its drive, cleared with the solids. */
		void SpawnCover()
		{
			TArray<FBox2D> Hulls;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
			{
				Hulls.Add(RTSBenchScene::GetHullBox(Unit->GetFootprint()));
			}
			const FBox Cover = RTSBenchScene::MakeCover(Hulls, Plan.bLongScene ? LongMoveDistance : MoveDistance, GetGridCentre().Z);
			if (AActor* Actor = RTSBenchScene::SpawnCover(*World, Cover))
			{
				SolidActors.Add(Actor);
			}
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: a roof over %.0f x %.0f m, %.0f m up"), Cover.GetSize().X / 100.f,
				Cover.GetSize().Y / 100.f, RTSBenchScene::CoverHeight / 100.f);
		}

		/** The solids of an "@Solids" set over the ground the grid stands on and drives over, clear of its hulls. */
		void SpawnSolids()
		{
			TArray<FBox2D> Hulls;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
			{
				Hulls.Add(RTSBenchScene::GetHullBox(Unit->GetFootprint()));
			}
			const FBox2D Area = RTSBenchScene::GetSolidsArea(Hulls, Plan.bLongScene ? LongMoveDistance : MoveDistance);
			SolidLayout = RTSBenchScene::MakeSolids(Area, Hulls);
			SolidActors = RTSBenchScene::SpawnSolids(*World, SolidLayout, GetGridCentre().Z);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: %d solids over %.0f x %.0f m: %s"), SolidActors.Num(),
				Area.GetSize().X / 100.f, Area.GetSize().Y / 100.f, *DescribeSolids());
		}

		int32 CountSolids(RTSBenchScene::ESolidKind Kind) const
		{
			return Algo::CountIf(SolidLayout, [Kind](const RTSBenchScene::FSolid& Solid) { return Solid.Kind == Kind; });
		}

		FString DescribeSolids() const
		{
			return FString::Printf(TEXT("%d trunks, %d boulders, %d walls"), CountSolids(RTSBenchScene::ESolidKind::Trunk),
				CountSolids(RTSBenchScene::ESolidKind::Boulder), CountSolids(RTSBenchScene::ESolidKind::Wall));
		}

		FVector GetGridCentre() const { return Viewpoint ? ViewGridCentre : Plan.bLongScene ? LongGridCentre : GridCentre; }

		/** A viewpoint looks at its grid; L_Bench's camera at the middle of the grid and its drive. */
		FVector GetCameraLookAt() const
		{
			return Viewpoint ? ViewGridCentre : Plan.bLongScene ? LongGridCentre + FVector(LongMoveDistance / 2.f, 0.f, 0.f) : CameraLookAt;
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
			if (bMixedTeams)
			{
				RTSCommands::Dispatch(Ordered, Move);
			}
			else
			{
				GetController()->Server_IssueCommand(Ordered, Move);
			}
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
			for (const TWeakObjectPtr<AActor>& Solid : SolidActors)
			{
				if (Solid.IsValid())
				{
					Solid->Destroy();
				}
			}
			SolidActors.Reset();
			bMoveBack = false;
			GEngine->ForceGarbageCollection(true);
		}

		void BeginMeasure(const TCHAR* Scenario, EStep Next)
		{
			Current = FScenarioStats();
			Current.Scenario = Scenario;
			Current.Count = Counts[CountIndex];
			Current.Sample = Plan.IsTimed() ? 1 : 0;
			Current.Reserve(Plan.IsTimed() ? LongFrameCapacity : Plan.SampleFrames);
			if (bKeepFrames)
			{
				Current.KeptFrames.Reserve(LongFrameCapacity);
			}
			// The move's slowest frame includes the order's own frame, before the measured ones.
			Current.MaxGameMs = Next == EStep::MeasureMove ? Lead.MaxGameMs : 0.0;
			URTSWeaponComponent::TakeSearchSeconds();
			URTSUnitMovement::TakeLocomotionSeconds();
			URTSRunningGearSubsystem::TakeSeconds();
			URTSRunningGearSubsystem::TakeTrackUpdates();
			FRTSSolidStats::Take();
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
			Current.Reserve(LongFrameCapacity);
			if (bKeepFrames)
			{
				Current.KeptFrames.Reserve(LongFrameCapacity);
			}
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
				URTSUnitMovement::TakeLocomotionSeconds() * 1000.0, URTSRunningGearSubsystem::TakeSeconds() * 1000.0);
			Stats.AddSolids(FRTSSolidStats::Take());
			Stats.TrackUpdates += URTSRunningGearSubsystem::TakeTrackUpdates();
		}

		/** Takes one frame of the scenario; true once it has all of them, logged and kept. */
		bool Measure()
		{
			TakeFrame(Current);
			const double Elapsed = FPlatformTime::Seconds() - StepStart;
			if (bKeepFrames && Current.KeptFrames.Num() < LongFrameCapacity)
			{
				Current.KeptFrames.Emplace(Elapsed, Current.LastFrameMs, Current.LastGameMs);
			}
			if (Plan.IsTimed() ? Elapsed < Plan.SampleSeconds : StepFrames < Plan.SampleFrames)
			{
				return false;
			}
			TRACE_END_REGION(*RegionName());
			Current.SampleSeconds = Elapsed;
			Current.TakePercentiles();
			Current.Units = Units.Num();
			const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
			Current.UsedMemoryMB = Memory.UsedPhysical / (1024.0 * 1024.0);
			Current.PeakMemoryMB = Memory.PeakUsedPhysical / (1024.0 * 1024.0);
			Current.RenderSize = GetRenderSize();
			CountMaterials(Current);
			CountLODs(Current);
			CountTracks(Current);
			Results.Add(Current);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.Bench: %s"), *Current.ToCsv());
			return true;
		}

		/**
		 * What the units draw with: their primitive components, those drawn on screen in the last frames, the slots of those that
		 * are dynamic instances, and the materials.
		 */
		void CountMaterials(FScenarioStats& Stats) const
		{
			// A part drawn in the main view stamps its last render time on screen with the world's time; its shadow does not.
			const float DrawnSince = World->GetTimeSeconds() - DrawnSeconds;
			TSet<const UMaterialInterface*> Materials;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
			{
				if (!Unit.IsValid())
				{
					continue;
				}
				for (const UPrimitiveComponent* Primitive : TInlineComponentArray<UPrimitiveComponent*>(Unit.Get()))
				{
					const int32 Slots = Primitive->GetNumMaterials();
					Stats.Primitives += Slots > 0 ? 1 : 0;
					Stats.DrawnPrimitives += Slots > 0 && Primitive->GetLastRenderTimeOnScreen() >= DrawnSince ? 1 : 0;
					for (int32 Slot = 0; Slot < Slots; ++Slot)
					{
						if (const UMaterialInterface* Material = Primitive->GetMaterial(Slot))
						{
							Materials.Add(Material);
							Stats.DynamicMaterials += Material->IsA<UMaterialInstanceDynamic>() ? 1 : 0;
						}
					}
				}
			}
			Stats.DistinctMaterials = Materials.Num();
		}

		/**
		 * The units on screen by the LOD the renderer picks for their hull, and those with a part at another (#149). r.ForceLOD
		 * is left out: an @LOD0 set counts what it would have drawn.
		 */
		void CountLODs(FScenarioStats& Stats) const
		{
			FVector Origin;
			FMatrix Projection;
			const APlayerController* Controller = GetController();
			if (!Controller || !RTSUnitLOD::GetPlayerView(*Controller, Origin, Projection))
			{
				return;
			}
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
			{
				const UStaticMeshComponent* Hull = Unit.IsValid() ? RTSUnitLOD::FindHull(*Unit) : nullptr;
				if (!Hull || !Hull->WasRecentlyRendered(0.2f))
				{
					continue;
				}
				const RTSUnitLOD::FPartLODs LODs = RTSUnitLOD::GetPartLODs(*Unit, Origin, Projection);
				if (Stats.HullLODs.Num() <= LODs.RootLOD)
				{
					Stats.HullLODs.SetNumZeroed(LODs.RootLOD + 1);
				}
				++Stats.HullLODs[LODs.RootLOD];
				Stats.MixedLODUnits += LODs.IsCoherent() ? 0 : 1;
			}
		}

		/**
		 * The units' tracks, those drawn on screen, and those of them whose links the renderer moves (#212): within the link
		 * shift's distance of the view (UStaticMeshComponent::GetWorldPositionOffsetDisableDistance, 0 always; measured to the
		 * track's bounds) and at an LOD whose first section is drawn with a material other than the track's rigid one. The
		 * LOD is RTSUnitLOD's, or r.ForceLOD's when an @LOD0 set forces it.
		 */
		void CountTracks(FScenarioStats& Stats) const
		{
			FVector Origin;
			FMatrix Projection;
			const APlayerController* Controller = GetController();
			if (!Controller || !RTSUnitLOD::GetPlayerView(*Controller, Origin, Projection))
			{
				return;
			}
			static const IConsoleVariable* ForceLOD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ForceLOD"));
			const int32 ForcedLOD = ForceLOD ? ForceLOD->GetInt() : -1;
			const float DrawnSince = World->GetTimeSeconds() - DrawnSeconds;
			TArray<const UStaticMeshComponent*, TInlineAllocator<2>> TrackComponents;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
			{
				if (!Unit.IsValid())
				{
					continue;
				}
				TArray<const UMaterialInterface*, TInlineAllocator<2>> Rigid;
				for (const FRTSVehiclePart& Part : Unit->GetUnitData()->Parts)
				{
					if (Part.TrackPitch > 0.f && Part.RigidMaterial)
					{
						Rigid.AddUnique(Part.RigidMaterial);
					}
				}
				Unit->GetTrackComponents(TrackComponents);
				for (const UStaticMeshComponent* Track : TrackComponents)
				{
					++Stats.Tracks;
					const UStaticMesh* Mesh = Track->GetStaticMesh();
					if (!Mesh || !Mesh->GetRenderData() || Track->GetLastRenderTimeOnScreen() < DrawnSince)
					{
						continue;
					}
					++Stats.DrawnTracks;
					const FBoxSphereBounds& Bounds = Track->Bounds;
					const int32 LODs = Mesh->GetRenderData()->LODResources.Num();
					const int32 LOD = FMath::Clamp(ForcedLOD >= 0 ? ForcedLOD
						: RTSUnitLOD::ChooseLOD(*Mesh, Bounds.SphereRadius, FVector::Dist(Bounds.Origin, Origin), Projection), 0, LODs - 1);
					const FStaticMeshLODResources& Resources = Mesh->GetRenderData()->LODResources[LOD];
					const UMaterialInterface* Material = Resources.Sections.IsEmpty() ? nullptr : Track->GetMaterial(Resources.Sections[0].MaterialIndex);
					const int32 Disable = Track->GetWorldPositionOffsetDisableDistance();
					const bool bNear = Disable <= 0 || Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin) <= FMath::Square(static_cast<double>(Disable));
					Stats.AnimatedTracks += Material && !Rigid.Contains(Material) && bNear ? 1 : 0;
				}
			}
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
				Plan.Method == ERTSBenchMethod::Quick || Plan.Method == ERTSBenchMethod::Screen ? TEXT(" (diagnostic, not gate evidence)") : TEXT(""),
				*GetCPUName(), *GRHIAdapterName, *GRHIAdapterUserDriverVersion, GetRHIName(), LexToString(FApp::GetBuildConfiguration()));
			Markdown += FScenarioStats::MarkdownHeader();
			for (const FScenarioStats& Result : Results)
			{
				Csv += Result.ToCsv() + TEXT("\n");
				Markdown += Result.ToMarkdown();
			}
			const TArray<FString> Flags = GetFlags();
			Markdown += Flags.IsEmpty() ? FString(TEXT("\nValidity (#171): every sample complete, no stalls, the samples agree.\n"))
				: TEXT("\n**Invalid (#171), not a measurement:** ") + FString::Join(Flags, TEXT("; ")) + TEXT("\n");
			Markdown += TEXT("\nSolids the navmesh leaves out (#168), per frame:\n\n")
				TEXT("| Units | Scenario | Sample | Broad overlaps | Reused | Hull checks | Component queries | Broad ms | Narrow ms |\n")
				TEXT("|---:|---|---:|---:|---:|---:|---:|---:|---:|\n");
			for (const FScenarioStats& Result : Results)
			{
				Markdown += Result.ToSolidsMarkdown();
			}
			Markdown += TEXT("\nWhat the units draw with (#150), at each sample's end:\n\n")
				TEXT("| Units | Scenario | Sample | Primitive components | Drawn on screen (#214) | Dynamic material slots | Distinct materials | On screen at LOD0 / 1 / 2 / 3 (#149) | With a part at another LOD |\n")
				TEXT("|---:|---|---:|---:|---:|---:|---:|---:|---:|\n");
			for (const FScenarioStats& Result : Results)
			{
				Markdown += Result.ToMaterialsMarkdown();
			}
			Markdown += TEXT("\nThe tracks (#212): phases sent per frame, and at each sample's end the tracks, those drawn on screen and those animated there:\n\n")
				TEXT("| Units | Scenario | Sample | Phase updates per frame | Tracks | Drawn on screen | Animated on screen |\n")
				TEXT("|---:|---|---:|---:|---:|---:|---:|\n");
			for (const FScenarioStats& Result : Results)
			{
				Markdown += Result.ToTracksMarkdown();
			}
			return FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Dir, TEXT("bench.csv")))
				&& FFileHelper::SaveStringToFile(Markdown, *FPaths::Combine(Dir, TEXT("bench.md")))
				&& FFileHelper::SaveStringToFile(DescribeRun(), *FPaths::Combine(Dir, TEXT("bench.json")))
				&& (!bKeepFrames || FFileHelper::SaveStringToFile(DescribeFrames(), *FPaths::Combine(Dir, TEXT("frames.csv"))));
		}

		/** What makes this run unfit to measure with (RTSBenchGate::CheckRun); empty when it is valid. */
		TArray<FString> GetFlags() const
		{
			TArray<RTSBenchGate::FRow> Rows;
			for (const FScenarioStats& Result : Results)
			{
				Rows.Add(Result.ToGateRow());
			}
			return RTSBenchGate::CheckRun(Rows, Plan.IsTimed() ? Plan.SampleSeconds : 0.0);
		}

		/** frames.csv: every measured frame of every sample, for the method studies of #171. */
		FString DescribeFrames() const
		{
			FString Csv = TEXT("count,scenario,sample,t_s,frame_ms,game_ms\n");
			for (const FScenarioStats& Result : Results)
			{
				for (const FVector3f& Frame : Result.KeptFrames)
				{
					Csv += FString::Printf(TEXT("%d,%s,%d,%.4f,%.3f,%.3f\n"), Result.Count, *Result.Scenario, Result.Sample, Frame.X, Frame.Y, Frame.Z);
				}
			}
			return Csv;
		}

		/** Whether an Unreal Insights trace records into this run: its overhead is then in every number. */
		static bool IsTracing()
		{
			FString Channels;
			return FParse::Value(FCommandLine::Get(), TEXT("trace="), Channels) || FParse::Param(FCommandLine::Get(), TEXT("trace"));
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
			Scene->SetNumberField(TEXT("camera_pitch"), Pitch);
			Scene->SetNumberField(TEXT("camera_distance"), CameraDistance);
			Scene->SetNumberField(TEXT("camera_fov"), CameraFOV);
			// Only an "@Solids" set has the field, so the baselines of the open scene stay comparable as they are.
			if (bSolids)
			{
				const TSharedRef<FJsonObject> Solids = MakeShared<FJsonObject>();
				Solids->SetNumberField(TEXT("version"), RTSBenchScene::SolidsVersion);
				Solids->SetNumberField(TEXT("seed"), RTSBenchScene::SolidsSeed);
				Solids->SetNumberField(TEXT("trunks"), CountSolids(RTSBenchScene::ESolidKind::Trunk));
				Solids->SetNumberField(TEXT("boulders"), CountSolids(RTSBenchScene::ESolidKind::Boulder));
				Solids->SetNumberField(TEXT("walls"), CountSolids(RTSBenchScene::ESolidKind::Wall));
				Scene->SetObjectField(TEXT("solids"), Solids);
			}
			// Likewise only a "@RigidTracks" set says its tracks stood still.
			if (bRigidTracks)
			{
				Scene->SetStringField(TEXT("tracks"), TEXT("rigid"));
			}
			// And an "@AllTracks" set that its tracks animate at any distance (#212).
			if (UnitName.Contains(RTSBenchScene::AllTracksSuffix))
			{
				Scene->SetNumberField(TEXT("track_animation_distance"), 0);
			}
			// And a "@MixedTeams" set how many teams its units are spread over.
			if (bMixedTeams)
			{
				Scene->SetNumberField(TEXT("teams"), RTSBenchScene::MixedTeams);
			}
			// And an "@LOD0" set that it drew every mesh at LOD0 (#149).
			if (bForceLOD0)
			{
				Scene->SetNumberField(TEXT("forced_lod"), 0);
			}
			// And a set's occlusion when a suffix changes it, the roof of an "@Occluded" one and the view of L_TestMap (#214).
			if (const TCHAR* Occlusion = UnitName.Contains(RTSBenchScene::ExactOcclusionSuffix) ? TEXT("exact")
				: UnitName.Contains(RTSBenchScene::NoOcclusionSuffix) ? TEXT("none")
				: UnitName.Contains(RTSBenchScene::HZBOcclusionSuffix) ? TEXT("hzb") : nullptr)
			{
				Scene->SetStringField(TEXT("occlusion"), Occlusion);
			}
			if (bOccluded)
			{
				Scene->SetNumberField(TEXT("cover_share"), RTSBenchScene::CoverShare);
				Scene->SetNumberField(TEXT("cover_height"), RTSBenchScene::CoverHeight);
			}
			if (Viewpoint)
			{
				Scene->SetStringField(TEXT("viewpoint"), Viewpoint->Description);
				Scene->SetNumberField(TEXT("camera_yaw"), Viewpoint->Yaw);
			}

			const TSharedRef<FJsonObject> Machine = MakeShared<FJsonObject>();
			Machine->SetStringField(TEXT("cpu"), GetCPUName());
			Machine->SetStringField(TEXT("gpu"), GRHIAdapterName);
			Machine->SetStringField(TEXT("driver"), GRHIAdapterUserDriverVersion);
			Machine->SetStringField(TEXT("rhi"), GetRHIName());
			Machine->SetStringField(TEXT("build"), LexToString(FApp::GetBuildConfiguration()));

			// The run itself (#171): when it ran, whether it is fit to measure with, and what the set is made of. A baseline need
			// not match it, so the baselines stored before it stay comparable.
			const TSharedRef<FJsonObject> Run = MakeShared<FJsonObject>();
			Run->SetStringField(TEXT("start"), StartTime.ToIso8601());
			Run->SetStringField(TEXT("end"), FDateTime::UtcNow().ToIso8601());
			Run->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
			Run->SetBoolField(TEXT("trace"), IsTracing());
			const TArray<FString> Flags = GetFlags();
			Run->SetBoolField(TEXT("valid"), Flags.IsEmpty());
			TArray<TSharedPtr<FJsonValue>> FlagValues;
			for (const FString& Flag : Flags)
			{
				FlagValues.Add(MakeShared<FJsonValueString>(Flag));
			}
			Run->SetArrayField(TEXT("flags"), FlagValues);
			TArray<TSharedPtr<FJsonValue>> Kinds;
			bool bVehicles = false;
			for (int32 Index = 0; Index < UnitData.Num(); ++Index)
			{
				const URTSUnitData* Data = UnitData[Index].Get();
				const bool bAircraft = Data->IsA<URTSAircraftData>();
				const TSharedRef<FJsonObject> Kind = MakeShared<FJsonObject>();
				Kind->SetStringField(TEXT("name"), Data->GetName());
				Kind->SetStringField(TEXT("locomotion"), StaticEnum<ERTSLocomotionType>()->GetNameStringByValue(static_cast<int64>(Data->Locomotion.Type)));
				Kind->SetBoolField(TEXT("aircraft"), bAircraft);
				Kind->SetNumberField(TEXT("primitive_components"), KindComponents.FindRef(Index));
				Kinds.Add(MakeShared<FJsonValueObject>(Kind));
				bVehicles |= Data->Locomotion.IsVehicle() && !bAircraft;
			}
			Run->SetArrayField(TEXT("units"), Kinds);
			Run->SetBoolField(TEXT("vehicles"), bVehicles);

			const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetStringField(TEXT("date"), FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")));
			Root->SetObjectField(TEXT("method"), Method);
			Root->SetObjectField(TEXT("scene"), Scene);
			Root->SetObjectField(TEXT("machine"), Machine);
			Root->SetObjectField(TEXT("run"), Run);
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
		FDateTime StartTime;
		/** -RTSBenchFrames: keep every measured frame for frames.csv. */
		bool bKeepFrames = FParse::Param(FCommandLine::Get(), FramesParam);
		/** Each kind's primitive components, from its first unit. */
		TMap<int32, int32> KindComponents;
		/** The mix, held through the garbage collection between counts, when no unit refers to it. */
		TArray<TStrongObjectPtr<URTSUnitData>> UnitData;
		int32 TeamId = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		/** An "@Solids" set: the solids of the current count, and their actors. */
		bool bSolids = false;
		/** A "@RigidTracks" set. */
		bool bRigidTracks = false;
		/** A "@MixedTeams" set. */
		bool bMixedTeams = false;
		/** A "@LOD0" set, and the camera's distance, which "@Near" and "@Far" change (#149). */
		bool bForceLOD0 = false;
		/** An "@Occluded" set, an "@Close" one, and the view of L_TestMap a set stands in, with its grid's middle (#214). */
		bool bOccluded = false;
		bool bClose = false;
		const RTSBenchScene::FViewpoint* Viewpoint = nullptr;
		FVector ViewGridCentre = FVector::ZeroVector;
		float CameraDistance = DefaultCameraDistance;
		float Pitch = CameraPitch;
		TArray<RTSBenchScene::FSolid> SolidLayout;
		TArray<TWeakObjectPtr<AActor>> SolidActors;

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
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.Bench <UnitData> [Count,Count,...] [short|long|quick|screen] needs a running game"));
			return;
		}
		ERTSBenchMethod Method = ERTSBenchMethod::Short;
		if (Args.Num() > 2 && !FRTSBenchPlan::ParseMethod(Args[2], Method))
		{
			Output.Logf(ELogVerbosity::Error, TEXT("rts.Debug.Bench: no method named %s (short, long, quick or screen)"), *Args[2]);
			return;
		}
		// A count of 0 (or none) takes the method's default counts, so "quick" can follow without them.
		FRTSBenchPlan Plan = FRTSBenchPlan::Make(Method, Args.Num() > 1 ? FRTSBenchPlan::ParseCounts(Args[1]) : TArray<int32>());
		// The method studies of #171 only: bench.json records the length, so such a run never matches a stored baseline.
		if (double SampleSeconds = 0.0; Plan.IsTimed() && FParse::Value(FCommandLine::Get(), SampleSecondsParam, SampleSeconds) && SampleSeconds > 0.0)
		{
			Plan.SampleSeconds = SampleSeconds;
		}
		const int32 Place = FBenchRun::StartOrQueue(MakeShared<FBenchRun>(*World, Args[0], Plan));
		Output.Logf(TEXT("rts.Debug.Bench: %s, %s method, to %s%s"), *Args[0], FRTSBenchPlan::GetMethodName(Method),
			*FPaths::ConvertRelativePathToFull(FBenchRun::GetOutputDir(Args[0])),
			Place > 0 ? *FString::Printf(TEXT(", queued %d"), Place) : TEXT(""));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice BenchCommand(
		TEXT("rts.Debug.Bench"),
		TEXT("rts.Debug.Bench <UnitData>[+UnitData...] [Count,Count,...|0] [short|long|quick|screen]: measures that many of the unit, or of the mix in turn (by default 1, 30, 120 and 240; ")
		TEXT("quick, screen: 240) standing in a grid and driving on a move order, from a fixed camera, to Saved/Bench/<UnitData>. Given while a run is going, it is queued."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunBenchCommand));
}

#endif
