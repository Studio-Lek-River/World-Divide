#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Buildings/RTSBuilding.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSPlayerController.h"
#include "PrimitiveSceneProxy.h"
#include "RenderCore.h"
#include "UnrealClient.h"
#include "Units/Debug/RTSUnitLOD.h"
#include "Units/RTSPrimitiveData.h"
#include "WorldDivide.h"

/**
 * The shared building kit's spike (#387, docs/art/BUILDING_KIT.md "Per-instance and LOD"), on the git-ignored content
 * Tools/Editor/kit_spike.py builds in /Game/Developers/KitSpike (ue.ps1 -Task kitspike). Throwaway: no game code uses it.
 *
 * rts.Debug.KitSpikeLODSync 1 is option C: each frame, every building's kit ISMs (meshes SM_KitSpike_*) are forced to the LOD
 * its shell draws at (RTSUnitLOD's estimate of the renderer's pick), set only when it changes; 2 forces every mesh of the
 * building with LODs, its shell too, to that LOD, so the building switches as one from a single decision on the game
 * thread. ue.ps1 -Task buildingshots -Building DA_KitSpike_<Large|Small>_A -Cvars rts.Debug.KitSpikeLODSync=<1|2>
 * pictures it.
 *
 * rts.Debug.KitSpikeLens: in L_Bench, a row of LensCount SM_KitSpike_Lens instances in one ISM, each with its own lens
 * state (M_KitSpike_Lens: on/level, group, fault, flash, colour), pictured by day, at night, with the Fixtures group off,
 * after one instance changed and at LOD1, to Saved/KitSpike/<shot>.png. Then it changes ChangedCount instances a frame
 * for MeasureFrames frames in each of the ways an update can be sent, and logs the game thread's cost of the calls, the
 * frame's game and render thread times and whether the component's scene proxy was rebuilt. -RTSKitSpikeQuit quits when
 * done, exit code 1 on failure.
 */
namespace RTSKitSpike
{
	const TCHAR* QuitParam = TEXT("RTSKitSpikeQuit");
	const TCHAR* LensMeshPath = TEXT("/Game/Developers/KitSpike/SM_KitSpike_Lens.SM_KitSpike_Lens");
	const TCHAR* KitMeshPrefix = TEXT("SM_KitSpike_");

	constexpr int32 LensLevel = 0;
	constexpr int32 LensGroup = 1;
	constexpr int32 LensFault = 2;
	constexpr int32 LensFlash = 3;
	constexpr int32 LensColor = 4;
	constexpr int32 LensFloats = 7;

	constexpr int32 LensColumns = 20;
	constexpr int32 LensRows = 10;
	constexpr int32 LensCount = LensColumns * LensRows;
	constexpr float LensSpacing = 300.f;
	const FVector LensOrigin(0.f, 0.f, 300.f);
	constexpr int32 ChangedCount = 128;
	constexpr int32 MeasureFrames = 60;

	constexpr double ReadyTimeout = 60.0;
	constexpr double FirstSettleSeconds = 8.0;
	constexpr double SettleSeconds = 2.0;
	constexpr double CaptureTimeout = 10.0;

	TAutoConsoleVariable<int32> CVarLODSync(
		TEXT("rts.Debug.KitSpikeLODSync"), 0,
		TEXT("1: force every building's kit spike ISMs (SM_KitSpike_*) to the LOD its shell draws at (#387, option C). ")
		TEXT("2: force every mesh of the building, its shell too, to that LOD (option C for the whole building)."),
		ECVF_Default);

	bool IsKitSpikeMesh(const UStaticMeshComponent& Component)
	{
		return Component.GetStaticMesh() && Component.GetStaticMesh()->GetName().StartsWith(KitMeshPrefix);
	}

	/** Option C: the shell's LOD pushed to the building's kit ISMs, only when it changed. */
	bool TickLODSync(float)
	{
		if (CVarLODSync.GetValueOnGameThread() <= 0 || !GEngine)
		{
			return true;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			const APlayerController* Controller = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
			FVector Origin;
			FMatrix Projection;
			if (!Controller || !RTSUnitLOD::GetPlayerView(*Controller, Origin, Projection))
			{
				continue;
			}
			for (TActorIterator<ARTSBuilding> It(World); It; ++It)
			{
				const UStaticMeshComponent* Shell = RTSUnitLOD::FindHull(**It);
				if (!Shell || !Shell->GetStaticMesh())
				{
					continue;
				}
				const int32 LOD = RTSUnitLOD::ChooseLOD(*Shell->GetStaticMesh(), Shell->Bounds.SphereRadius,
					FVector::Dist(Shell->Bounds.Origin, Origin), Projection);
				const bool bWholeBuilding = CVarLODSync.GetValueOnGameThread() >= 2;
				int32 Changed = 0;
				for (UStaticMeshComponent* Kit : TInlineComponentArray<UStaticMeshComponent*>(*It))
				{
					if ((bWholeBuilding ? Kit->GetStaticMesh() && Kit->GetStaticMesh()->GetNumLODs() > 1 : IsKitSpikeMesh(*Kit))
						&& Kit->ForcedLodModel != LOD + 1)
					{
						Kit->SetForcedLodModel(LOD + 1);
						++Changed;
					}
				}
				if (Changed > 0)
				{
					UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.KitSpikeLODSync: %s to LOD%d at %.1f m, %d components"),
						*It->GetName(), LOD, FVector::Dist(Shell->Bounds.Origin, Origin) / 100.0, Changed);
				}
			}
		}
		return true;
	}

	FDelayedAutoRegisterHelper RegisterLODSync(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickLODSync));
	});

	/** One lens's state: a spread of on, off, gated by Fixtures, faulty, flashing and coloured lenses. */
	void GetLensState(int32 Index, float (&Out)[LensFloats])
	{
		static const FLinearColor Colors[] = {
			FLinearColor(1.f, 0.62f, 0.32f), // warm white, 3000 K
			FLinearColor(1.f, 0.05f, 0.02f), // obstruction red
			FLinearColor(1.f, 0.45f, 0.f), // amber
			FLinearColor(0.1f, 1.f, 0.2f), // indicator green
		};
		const int32 Row = Index / LensColumns;
		const FLinearColor& Color = Colors[Row % UE_ARRAY_COUNT(Colors)];
		Out[LensLevel] = Index % 5 == 4 ? 0.f : (Index % 3 == 2 ? 0.3f : 1.f);
		Out[LensGroup] = Row % 2 == 1 ? 1.f : 0.f;
		Out[LensFault] = Index % 7 == 0 ? 0.05f + FMath::Frac(Index * 0.37f) : 0.f;
		Out[LensFlash] = Index % 11 == 0 ? 0.05f + FMath::Frac(Index * 0.61f) : 0.f;
		Out[LensColor] = Color.R;
		Out[LensColor + 1] = Color.G;
		Out[LensColor + 2] = Color.B;
	}

	/** The ways one frame's changes can reach the renderer. */
	enum class EUpdate : uint8
	{
		/** SetCustomData per instance without dirtying, then MarkRenderInstancesDirty once. */
		InstancesDirty,
		/** SetCustomDataValue per float with bMarkRenderStateDirty. */
		ValueDirty,
		/** SetCustomData per instance without dirtying, then MarkRenderStateDirty once. */
		RenderStateDirty,
		/** Nothing changes: the frame times to compare with. */
		None,
	};

	const TCHAR* GetUpdateName(EUpdate Update)
	{
		switch (Update)
		{
		case EUpdate::InstancesDirty: return TEXT("SetCustomData + MarkRenderInstancesDirty");
		case EUpdate::ValueDirty: return TEXT("SetCustomDataValue(bMarkRenderStateDirty)");
		case EUpdate::RenderStateDirty: return TEXT("SetCustomData + MarkRenderStateDirty");
		default: return TEXT("no change");
		}
	}

	class FLensRun : public TSharedFromThis<FLensRun>
	{
	public:
		explicit FLensRun(UWorld& InWorld) : World(&InWorld) {}

		static bool IsRunning() { return Active.IsValid(); }

		static void Start(const TSharedRef<FLensRun>& Run)
		{
			Active = Run;
			Run->StepStart = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FLensRun::Tick));
		}

		static FString GetShotDir() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KitSpike"))); }

	private:
		struct FShot
		{
			const TCHAR* Name;
			float Night;
			float Fixtures;
			int32 ForcedLod;
			bool bChangeOne;
		};

		static constexpr FShot Shots[] = {
			{ TEXT("lens_day_lod0"), 0.f, 1.f, 0, false },
			{ TEXT("lens_night_lod0"), 1.f, 1.f, 0, false },
			{ TEXT("lens_night_fixtures_off"), 1.f, 0.f, 0, false },
			{ TEXT("lens_night_one_changed"), 1.f, 1.f, 0, true },
			{ TEXT("lens_night_lod1"), 1.f, 1.f, 2, false },
		};

		enum class EStep : uint8
		{
			WaitForPlayer,
			Settle,
			Capture,
			Measure,
		};

		bool Tick(float)
		{
			if (!World.IsValid())
			{
				return Finish(TEXT("the world ended"));
			}
			ARTSPlayerController* Controller = Cast<ARTSPlayerController>(World->GetFirstPlayerController());
			switch (Step)
			{
			case EStep::WaitForPlayer:
				return WaitForPlayer(Controller);
			case EStep::Settle:
				if (bCheckProxy && FPlatformTime::Seconds() > StepStart)
				{
					// The change went out at the end of the frame it was made in.
					bCheckProxy = false;
					UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.KitSpikeLens: %s: scene proxy %s"), Shots[ShotIndex].Name,
						Lenses->SceneProxy != ProxyBefore ? TEXT("rebuilt") : TEXT("kept"));
				}
				if (FPlatformTime::Seconds() - StepStart >= (ShotIndex == 0 ? FirstSettleSeconds : SettleSeconds))
				{
					IFileManager::Get().Delete(*ShotPath(), false, true);
					FScreenshotRequest::RequestScreenshot(ShotPath(), /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
					GoTo(EStep::Capture);
				}
				return true;
			case EStep::Capture:
				if (IFileManager::Get().FileSize(*ShotPath()) > 0)
				{
					UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.KitSpikeLens: %s"), *ShotPath());
					++ShotIndex;
					if (ShotIndex < UE_ARRAY_COUNT(Shots))
					{
						return StartShot();
					}
					Lenses->SetForcedLodModel(0);
					GoTo(EStep::Measure);
					MeasureFrame = -1;
					return true;
				}
				return FPlatformTime::Seconds() - StepStart > CaptureTimeout ? Finish(TEXT("no screenshot arrived")) : true;
			case EStep::Measure:
				return Measure();
			}
			return true;
		}

		void GoTo(EStep Next)
		{
			Step = Next;
			StepStart = FPlatformTime::Seconds();
		}

		FString ShotPath() const { return FPaths::Combine(GetShotDir(), FString(Shots[ShotIndex].Name) + TEXT(".png")); }

		bool WaitForPlayer(ARTSPlayerController* Controller)
		{
			if (!World->HasBegunPlay() || !Controller || !Controller->GetPawn())
			{
				return FPlatformTime::Seconds() - StepStart > ReadyTimeout ? Finish(TEXT("no player in time")) : true;
			}
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, LensMeshPath);
			if (!Mesh)
			{
				return Finish(FString::Printf(TEXT("no %s (ue.ps1 -Task kitspike builds it)"), LensMeshPath));
			}
			Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(LensOrigin));
			Lenses = NewObject<UInstancedStaticMeshComponent>(Holder.Get(), TEXT("Lenses"));
			Lenses->SetStaticMesh(Mesh);
			Lenses->SetCastShadow(false);
			Lenses->SetNumCustomDataFloats(LensFloats);
			Holder->SetRootComponent(Lenses.Get());
			Holder->AddInstanceComponent(Lenses.Get());
			TArray<FTransform> Transforms;
			for (int32 Index = 0; Index < LensCount; ++Index)
			{
				Transforms.Emplace(FVector((Index % LensColumns - (LensColumns - 1) * 0.5f) * LensSpacing,
					(Index / LensColumns - (LensRows - 1) * 0.5f) * LensSpacing, 0.f));
			}
			Lenses->AddInstances(Transforms, /*bShouldReturnIndices*/ false);
			for (int32 Index = 0; Index < LensCount; ++Index)
			{
				float State[LensFloats];
				GetLensState(Index, State);
				Lenses->SetCustomData(Index, MakeArrayView(State, LensFloats));
			}
			Lenses->RegisterComponent();
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.KitSpikeLens: %d lenses in one ISM, %d custom floats each"),
				Lenses->GetInstanceCount(), LensFloats);

			Camera = World->SpawnActor<ACameraActor>();
			Camera->GetCameraComponent()->SetFieldOfView(60.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			const FRotator Rotation(-45.f, 0.f, 0.f);
			Camera->SetActorLocationAndRotation(LensOrigin - Rotation.Vector() * 6500.f, Rotation);
			Controller->SetViewTarget(Camera.Get());
			IFileManager::Get().MakeDirectory(*GetShotDir(), /*Tree*/ true);
			return StartShot();
		}

		bool StartShot()
		{
			const FShot& Shot = Shots[ShotIndex];
			// The building's state reaches the lens through the component's custom primitive data, as ARTSBuilding writes it.
			Lenses->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LightFixtures, Shot.Fixtures);
			Lenses->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LightNight, Shot.Night);
			if (Lenses->ForcedLodModel != Shot.ForcedLod)
			{
				ProxyBefore = Lenses->SceneProxy;
				bCheckProxy = true;
				Lenses->SetForcedLodModel(Shot.ForcedLod);
			}
			if (Shot.bChangeOne)
			{
				// One lens of the first row, off and gated by nothing, switched on blue on its own.
				ProxyBefore = Lenses->SceneProxy;
				bCheckProxy = true;
				const double Start = FPlatformTime::Seconds();
				const float Blue[LensFloats] = { 1.f, 0.f, 0.f, 0.f, 0.1f, 0.3f, 1.f };
				Lenses->SetCustomData(4, MakeArrayView(Blue, LensFloats));
				Lenses->MarkRenderInstancesDirty();
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.KitSpikeLens: one instance changed in %.4f ms"),
					(FPlatformTime::Seconds() - Start) * 1000.0);
			}
			GoTo(EStep::Settle);
			return true;
		}

		/** Each update kind for MeasureFrames frames: the calls' cost now, the frame's times and the proxy on the next tick. */
		bool Measure()
		{
			if (MeasureFrame < 0)
			{
				// The frame that put the lenses back to their own LOD rebuilt the proxy: start after it.
				++MeasureFrame;
				return true;
			}
			if (MeasureFrame > 0)
			{
				// The previous frame's changes went out at its end: was the proxy rebuilt, what did the frame cost?
				ProxyRebuilds += Lenses->SceneProxy != LastProxy ? 1 : 0;
				GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
				RenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
			}
			const EUpdate Update = static_cast<EUpdate>(UpdateIndex);
			if (MeasureFrame == MeasureFrames)
			{
				const int32 Frames = MeasureFrames;
				UE_LOG(LogWorldDivide, Display,
					TEXT("rts.Debug.KitSpikeLens: %s: %d of %d instances a frame, calls %.4f ms, frame game thread %.3f ms, render thread %.3f ms, proxy rebuilt in %d of %d frames"),
					GetUpdateName(Update), Update == EUpdate::None ? 0 : ChangedCount, LensCount, CallMs / MeasureFrames,
					GameMs / Frames, RenderMs / Frames, ProxyRebuilds, Frames);
				MeasureFrame = 0;
				CallMs = GameMs = RenderMs = 0.0;
				ProxyRebuilds = 0;
				if (++UpdateIndex > static_cast<int32>(EUpdate::None))
				{
					return Finish(FString());
				}
				return true;
			}
			const double Start = FPlatformTime::Seconds();
			if (Update != EUpdate::None)
			{
				for (int32 Change = 0; Change < ChangedCount; ++Change)
				{
					const int32 Index = (Change * 3 + MeasureFrame) % LensCount;
					float State[LensFloats];
					GetLensState(Index, State);
					State[LensLevel] = MeasureFrame % 2 == 0 ? 1.f : 0.2f;
					if (Update == EUpdate::ValueDirty)
					{
						for (int32 Float = 0; Float < LensFloats; ++Float)
						{
							Lenses->SetCustomDataValue(Index, Float, State[Float], /*bMarkRenderStateDirty*/ true);
						}
					}
					else
					{
						Lenses->SetCustomData(Index, MakeArrayView(State, LensFloats));
					}
				}
				if (Update == EUpdate::InstancesDirty)
				{
					Lenses->MarkRenderInstancesDirty();
				}
				else if (Update == EUpdate::RenderStateDirty)
				{
					Lenses->MarkRenderStateDirty();
				}
			}
			CallMs += (FPlatformTime::Seconds() - Start) * 1000.0;
			LastProxy = Lenses->SceneProxy;
			++MeasureFrame;
			return true;
		}

		bool Finish(const FString& Error)
		{
			if (Holder.IsValid())
			{
				Holder->Destroy();
			}
			if (Camera.IsValid())
			{
				Camera->Destroy();
			}
			if (Error.IsEmpty())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.KitSpikeLens: done, %d shots to %s"), ShotIndex, *GetShotDir());
			}
			else
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.KitSpikeLens failed: %s"), *Error);
			}
			Active.Reset();
			if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() ? 0 : 1);
			}
			return false;
		}

		static TSharedPtr<FLensRun> Active;

		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AActor> Holder;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Lenses;
		TWeakObjectPtr<ACameraActor> Camera;
		EStep Step = EStep::WaitForPlayer;
		double StepStart = 0.0;
		int32 ShotIndex = 0;
		/** The proxy before a shot's change, compared on the next tick. */
		const FPrimitiveSceneProxy* ProxyBefore = nullptr;
		bool bCheckProxy = false;
		int32 UpdateIndex = 0;
		int32 MeasureFrame = 0;
		const FPrimitiveSceneProxy* LastProxy = nullptr;
		int32 ProxyRebuilds = 0;
		double CallMs = 0.0;
		double GameMs = 0.0;
		double RenderMs = 0.0;
	};

	TSharedPtr<FLensRun> FLensRun::Active;

	void RunLens(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.KitSpikeLens needs a running game"));
			return;
		}
		if (FLensRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.KitSpikeLens is already running"));
			return;
		}
		FLensRun::Start(MakeShared<FLensRun>(*World));
		Output.Logf(TEXT("rts.Debug.KitSpikeLens: to %s"), *FLensRun::GetShotDir());
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice LensCommand(
		TEXT("rts.Debug.KitSpikeLens"),
		TEXT("rts.Debug.KitSpikeLens: the kit spike's lens row pictured by day, at night, Fixtures off, at LOD1 and with one lens changed, ")
		TEXT("then the cost of updating its instances logged, to Saved/KitSpike (#387)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunLens));
}

#endif
