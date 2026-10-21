#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Buildings/RTSBuilding.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/RTSBuildingData.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSCameraPawn.h"
#include "PrimitiveSceneProxy.h"
#include "Player/RTSPlayerController.h"
#include "UnrealClient.h"
#include "Units/Debug/RTSBenchScene.h"
#include "Units/Debug/RTSUnitLOD.h"
#include "WorldDivide.h"

/**
 * rts.Debug.BuildingShots <BuildingData> [Folder] (#349; ue.ps1 -Task buildingshots -Building <DA>): one finished building
 * of the data asset in the middle of the level (L_Bench), pictured by a camera at the RTS camera's pitch for each arm
 * length (ARTSCameraPawn::GetPitchForZoom) at 1920x1080, to Saved/BuildingShots/<BuildingData>[/<Folder>]/<shot>.png:
 * close_12m looks at its front from outside at the closest zoom; default_150m, far_450m and the pair either side of
 * the bench's LOD1 switch at 260 m (lod_before_249.6m, lod_after_270.4m, 4 % each way) look at the middle of its root mesh's
 * bounds from that far. Each shot logs the LOD the renderer picks for every part (RTSUnitLOD). The data asset is looked
 * for under RTSBenchScene::BenchHQDir, then /Game/Data/Buildings. ue.ps1 passes a Folder for a run with other console
 * variables, such as NaniteOff for r.Nanite 0. With -RTSBuildingShotsQuit the game quits when done, exit code 1 on failure.
 */
namespace RTSBuildingShotsCommand
{
	const TCHAR* QuitParam = TEXT("RTSBuildingShotsQuit");
	const TCHAR* BuildingDataPath = TEXT("/Game/Data/Buildings");

	/** Where the building stands; L_Bench's middle, clear of its teams' headquarters in the corners. */
	const FVector BuildingLocation(0.f, 0.f, 0.f);
	constexpr float BuildingYaw = 0.f;
	constexpr float CameraFOV = 60.f;
	/** The closest view looks at a point this far inside the building's front and this high, so its front fills the frame. */
	constexpr float FrontInset = 1000.f;
	constexpr float FrontHeight = 400.f;

	constexpr double ReadyTimeout = 60.0;
	/** Real seconds the first view settles (Nanite pages, textures and shadows streaming in), then each later one. */
	constexpr double FirstSettleSeconds = 8.0;
	constexpr double SettleSeconds = 2.0;
	constexpr double CaptureTimeout = 10.0;
	constexpr double ResizeTimeout = 10.0;
	const FIntPoint ShotSize(1920, 1080);

	struct FShot
	{
		const TCHAR* Name;
		/** The arm: the camera's distance from what it looks at, in cm. */
		float Distance;
		/** Looks at the ground before the front instead of the root mesh's middle. */
		bool bFront;
	};

	const FShot Shots[] = {
		{ TEXT("close_12m"), 1200.f, true },
		{ TEXT("default_150m"), 15000.f, false },
		{ TEXT("far_450m"), 45000.f, false },
		{ TEXT("lod_before_249.6m"), 24960.f, false },
		{ TEXT("lod_after_270.4m"), 27040.f, false },
	};

	class FBuildingShotsRun : public TSharedFromThis<FBuildingShotsRun>
	{
	public:
		FBuildingShotsRun(UWorld& InWorld, const FString& InDataName, const FString& InFolder)
			: World(&InWorld), DataName(InDataName), Folder(InFolder)
		{
		}

		static bool IsRunning() { return Active.IsValid(); }

		static void Start(const TSharedRef<FBuildingShotsRun>& Run)
		{
			Active = Run;
			Run->StepStart = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FBuildingShotsRun::Tick));
		}

		FString GetShotDir() const
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BuildingShots"), DataName, Folder));
		}

	private:
		enum class EStep : uint8
		{
			WaitForPlayer,
			Resize,
			Settle,
			Capture,
		};

		bool Tick(float)
		{
			if (!World.IsValid())
			{
				return Finish(TEXT("the world ended"));
			}
			ARTSPlayerController* Controller = Cast<ARTSPlayerController>(World->GetFirstPlayerController());
			if (Controller && World->GetGameViewport() && World->GetGameViewport()->Viewport)
			{
				// The cursor rests wherever the offscreen window put it; at an edge it would scroll the camera.
				const FIntPoint Size = World->GetGameViewport()->Viewport->GetSizeXY();
				Controller->SetMouseLocation(Size.X / 2, Size.Y / 2);
			}
			switch (Step)
			{
			case EStep::WaitForPlayer:
				return WaitForPlayer(Controller);
			case EStep::Resize:
				return Resize(*Controller);
			case EStep::Settle:
				if (FPlatformTime::Seconds() - StepStart >= (ShotIndex == 0 ? FirstSettleSeconds : SettleSeconds))
				{
					RequestShot(*Controller);
				}
				return true;
			case EStep::Capture:
				if (IFileManager::Get().FileSize(*ShotPath()) > 0)
				{
					UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.BuildingShots: %s"), *ShotPath());
					++ShotIndex;
					return ShotIndex < UE_ARRAY_COUNT(Shots) ? StartShot(*Controller) : Finish(FString());
				}
				return FPlatformTime::Seconds() - StepStart > CaptureTimeout ? Finish(TEXT("no screenshot arrived")) : true;
			}
			return true;
		}

		void GoTo(EStep Next)
		{
			Step = Next;
			StepStart = FPlatformTime::Seconds();
			bResizeRequested = false;
		}

		FIntPoint GetViewportSize() const
		{
			const UGameViewportClient* Client = World->GetGameViewport();
			return Client && Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		}

		/** The offscreen window starts at the desktop's idea of a size; the shots are ShotSize, as the bench's. */
		bool Resize(ARTSPlayerController& Controller)
		{
			if (World->WorldType == EWorldType::PIE || GetViewportSize() == ShotSize)
			{
				return StartShot(Controller);
			}
			if (!bResizeRequested)
			{
				FSystemResolution::RequestResolutionChange(ShotSize.X, ShotSize.Y, EWindowMode::Windowed);
				bResizeRequested = true;
			}
			if (FPlatformTime::Seconds() - StepStart > ResizeTimeout)
			{
				const FIntPoint Size = GetViewportSize();
				return Finish(FString::Printf(TEXT("the screen stayed %dx%d instead of %dx%d"), Size.X, Size.Y, ShotSize.X, ShotSize.Y));
			}
			return true;
		}

		FString ShotPath() const { return FPaths::Combine(GetShotDir(), FString(Shots[ShotIndex].Name) + TEXT(".png")); }

		bool WaitForPlayer(ARTSPlayerController* Controller)
		{
			if (!World->HasBegunPlay() || !Controller || !Controller->GetPawn())
			{
				return FPlatformTime::Seconds() - StepStart > ReadyTimeout ? Finish(TEXT("no player in time")) : true;
			}
			URTSBuildingData* Data = nullptr;
			for (const TCHAR* Directory : { RTSBenchScene::BenchHQDir, BuildingDataPath })
			{
				Data = Data ? Data : LoadObject<URTSBuildingData>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), Directory, *DataName, *DataName));
			}
			if (!Data)
			{
				return Finish(FString::Printf(TEXT("no building data %s in %s or %s (the bench HQs: ue.ps1 -Task setup -BenchHQ)"),
					*DataName, RTSBenchScene::BenchHQDir, BuildingDataPath));
			}
			const FTransform Transform(FRotator(0.f, BuildingYaw, 0.f), BuildingLocation);
			Building = World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Building.IsValid())
			{
				return Finish(TEXT("could not spawn the building"));
			}
			Building->InitFromData(Data, Controller->GetTeamId());
			Building->FinishSpawning(Transform);
			for (int32 Attempt = 0; Attempt < 10 && !Building->IsConstructed(); ++Attempt)
			{
				Building->UpdateConstruction(FMath::Max(Data->BuildTime, 1.f) * 10.f);
			}
			Camera = World->SpawnActor<ACameraActor>();
			Camera->GetCameraComponent()->SetFieldOfView(CameraFOV);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Controller->SetViewTarget(Camera.Get());
			IFileManager::Get().MakeDirectory(*GetShotDir(), /*Tree*/ true);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.BuildingShots: %s at %s, r.Nanite %s"), *Data->GetName(),
				*BuildingLocation.ToString(), *GetCVar(TEXT("r.Nanite")));
			GoTo(EStep::Resize);
			return true;
		}

		static FString GetCVar(const TCHAR* Name)
		{
			const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
			return CVar ? CVar->GetString() : FString(TEXT("-"));
		}

		bool StartShot(ARTSPlayerController& Controller)
		{
			const FShot& Shot = Shots[ShotIndex];
			const ARTSCameraPawn* Pawn = Controller.GetPawn<ARTSCameraPawn>();
			const float Pitch = Pawn ? Pawn->GetPitchForZoom(Shot.Distance) : -55.f;
			const UStaticMeshComponent* Root = RTSUnitLOD::FindHull(*Building);
			FVector LookAt = Root ? Root->Bounds.Origin : Building->GetActorLocation();
			if (Shot.bFront)
			{
				const FBox Box = Building->GetComponentsBoundingBox(/*bNonColliding*/ true);
				LookAt = FVector(Box.Max.X - FrontInset, Box.GetCenter().Y, Building->GetActorLocation().Z + FrontHeight);
			}
			// Looking along +X, towards the building's front for the close view (its front faces +X at yaw 0) and from behind
			// the front for the rest, as the RTS camera looks north.
			const FRotator Rotation(Pitch, Shot.bFront ? 180.f : 0.f, 0.f);
			Camera->SetActorLocationAndRotation(LookAt - Rotation.Vector() * Shot.Distance, Rotation);
			GoTo(EStep::Settle);
			return true;
		}

		void RequestShot(ARTSPlayerController& Controller)
		{
			// Whether the renderer draws each part with Nanite, or the mesh's fallback (r.Nanite 0, or a mesh without it).
			int32 Parts = 0;
			int32 NaniteParts = 0;
			int32 NaniteData = 0;
			for (const UStaticMeshComponent* Component : TInlineComponentArray<UStaticMeshComponent*>(Building.Get()))
			{
				if (Component->GetStaticMesh())
				{
					++Parts;
					NaniteParts += Component->SceneProxy && Component->SceneProxy->IsNaniteMesh() ? 1 : 0;
					NaniteData += Component->HasValidNaniteData() && !Component->IsDisallowNanite() ? 1 : 0;
				}
			}
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.BuildingShots: %s at %dx%d, %d of %d parts drawn with Nanite (%d with Nanite data)"),
				Shots[ShotIndex].Name, GetViewportSize().X, GetViewportSize().Y, NaniteParts, Parts, NaniteData);
			FVector Origin;
			FMatrix Projection;
			if (RTSUnitLOD::GetPlayerView(Controller, Origin, Projection))
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.BuildingShots: %s from %.0f m: %s"), Shots[ShotIndex].Name,
					FVector::Dist(Origin, Building->GetActorLocation()) / 100.f, *RTSUnitLOD::GetPartLODs(*Building, Origin, Projection).ToString());
			}
			IFileManager::Get().Delete(*ShotPath(), /*RequireExists*/ false, /*EvenReadOnly*/ true);
			FScreenshotRequest::RequestScreenshot(ShotPath(), /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
			GoTo(EStep::Capture);
		}

		bool Finish(const FString& Error)
		{
			if (Building.IsValid())
			{
				Building->Destroy();
			}
			if (Camera.IsValid())
			{
				Camera->Destroy();
			}
			if (Error.IsEmpty())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.BuildingShots: %d shots to %s"), ShotIndex, *GetShotDir());
			}
			else
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.BuildingShots failed: %s: %s"), *DataName, *Error);
			}
			Active.Reset();
			if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() ? 0 : 1);
			}
			return false;
		}

		static TSharedPtr<FBuildingShotsRun> Active;

		TWeakObjectPtr<UWorld> World;
		FString DataName;
		FString Folder;
		TWeakObjectPtr<ARTSBuilding> Building;
		TWeakObjectPtr<ACameraActor> Camera;
		EStep Step = EStep::WaitForPlayer;
		double StepStart = 0.0;
		int32 ShotIndex = 0;
		bool bResizeRequested = false;
	};

	TSharedPtr<FBuildingShotsRun> FBuildingShotsRun::Active;

	void RunBuildingShots(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld() || Args.IsEmpty())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.BuildingShots <BuildingData> [Folder] needs a running game"));
			return;
		}
		if (FBuildingShotsRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.BuildingShots is already running"));
			return;
		}
		const TSharedRef<FBuildingShotsRun> Run = MakeShared<FBuildingShotsRun>(*World, Args[0], Args.Num() > 1 ? Args[1] : FString());
		FBuildingShotsRun::Start(Run);
		Output.Logf(TEXT("rts.Debug.BuildingShots: %s to %s"), *Args[0], *Run->GetShotDir());
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice BuildingShotsCommand(
		TEXT("rts.Debug.BuildingShots"),
		TEXT("rts.Debug.BuildingShots <BuildingData> [Folder]: one finished building of the data asset pictured at the RTS camera's pitch from 12, 150 and 450 m ")
		TEXT("and either side of 260 m, with the LOD of every part logged, to Saved/BuildingShots/<BuildingData>[/<Folder>] (#349)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunBuildingShots));
}

#endif
