#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSBuildingStateComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/RTSDayNightSubsystem.h"
#include "Data/RTSBuildingData.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
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
#include "Units/Debug/RTSShotNight.h"
#include "Units/Debug/RTSUnitLOD.h"
#include "WorldDivide.h"

/**
 * rts.Debug.BuildingShots <BuildingData> [Folder] [-Night] [-Turn] (#349; ue.ps1 -Task buildingshots -Building <DA>): one
 * finished building of the data asset in the middle of the level (L_Bench), pictured by a camera at the RTS camera's pitch for each
 * arm length (ARTSCameraPawn::GetPitchForZoom) at 1920x1080, to Saved/BuildingShots/<BuildingData>[/<Folder>]/<shot>.png:
 * close_12m looks at its front from outside at the closest zoom, framed from its data (the footprint's front and the root
 * mesh's height) so every run lands on the same spot (#424); default_150m, far_450m and the pair either side of
 * the bench's LOD1 switch at 260 m (lod_before_249.6m, lod_after_270.4m, 4 % each way) look at the middle of its root mesh's
 * bounds from that far. Each shot logs the LOD the renderer picks for every part (RTSUnitLOD). The data asset is looked
 * for under RTSBenchScene::BenchHQDir, then /Game/Data/Buildings, then the kit spike's /Game/Developers/KitSpike (#387), then
 * the kit showcases' /Game/Data/Showcase (DA_TestKitLighting, #392). ue.ps1 passes a Folder for a run with other console
 * variables, such as NaniteOff for r.Nanite 0. The parts that turn on their own stay at rest and the game clock is held at
 * ShotGameTime, so the flag, the plants and the lamps look the same every run. By day close_12m turns L_Bench's sun, which
 * stands behind the front, half a turn in yaw so it lights the front from behind the camera (#443). The building shows Idle
 * (#442; L_Bench's team is short of power, which would show LowPower) unless the run sets rts.Debug.BuildingState, -1 for the
 * game's own state.
 * -Night pictures it at night (#424): the sun and the sky lit as the game's night (FRTSShotNight::GetGameMoon, L_TestMapNight's
 * moon, sky and exposure) and the world dark for its lamps and glow (URTSDayNightSubsystem, as rts.Light.ForceDark 1), to the
 * Folder Night unless one is given. -Turn stands the building half a turn round (#450), as a player turns it at placement, so
 * its front faces the RTS camera at 150 and 450 m too; close_12m still looks at the front, which the sun then lights unturned.
 * With -RTSBuildingShotsQuit the game quits when done, exit code 1 on failure.
 */
namespace RTSBuildingShotsCommand
{
	const TCHAR* QuitParam = TEXT("RTSBuildingShotsQuit");
	const TCHAR* BuildingDataPath = TEXT("/Game/Data/Buildings");
	/** The shared building kit's spike buildings (Tools/Editor/kit_spike.py, #387). */
	const TCHAR* KitSpikePath = TEXT("/Game/Developers/KitSpike");
	/** Test buildings that only show kit pieces for building shots (setup_testmap.py, #392); nothing produces them. */
	const TCHAR* ShowcasePath = TEXT("/Game/Data/Showcase");

	/** Where the building stands; L_Bench's middle, clear of its teams' headquarters in the corners. */
	const FVector BuildingLocation(0.f, 0.f, 0.f);
	constexpr float BuildingYaw = 0.f;
	/** How far -Turn turns the building (#450): its front, +X at yaw 0, then faces the RTS camera, which looks along +X. */
	constexpr float TurnYaw = 180.f;
	constexpr float CameraFOV = 60.f;
	/**
	 * The closest view looks at a point inside the footprint's front, FrontInsetShare of its depth up to FrontInset, and half
	 * the root mesh's height up to FrontHeight, so a large building's front fills the frame and a small one's is not its roof.
	 */
	constexpr float FrontInset = 1000.f;
	constexpr float FrontInsetShare = 0.25f;
	constexpr float FrontHeight = 400.f;
	const TCHAR* NightArg = TEXT("-Night");
	const TCHAR* NightFolder = TEXT("Night");
	const TCHAR* TurnArg = TEXT("-Turn");
	const TCHAR* TurnFolder = TEXT("Turn");
	/** How far the day close view turns the sun in yaw (#443): L_Bench's sun at yaw 35 lights the back, at 215 the front. */
	constexpr float FrontSunYaw = 180.f;
	/** The game time every shot is taken at (#424), so what moves on the game clock is the same in every run. */
	constexpr double ShotGameTime = 60.0;

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
		FBuildingShotsRun(UWorld& InWorld, const FString& InDataName, const FString& InFolder, bool bInNight, bool bInTurned)
			: World(&InWorld), DataName(InDataName), Folder(InFolder), bNight(bInNight), bTurned(bInTurned)
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
			if (Building.IsValid())
			{
				// Before the world ticks, so the radar and the like stay at rest, and the flag's wind, the plants' sway and
				// the lamps' flashes (all on the game clock) show the same moment whenever the shot lands.
				Building->SetEquipmentMotionScale(0.f);
				World->TimeSeconds = ShotGameTime;
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
			for (const TCHAR* Directory : { RTSBenchScene::BenchHQDir, BuildingDataPath, KitSpikePath, ShowcasePath })
			{
				Data = Data ? Data : LoadObject<URTSBuildingData>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), Directory, *DataName, *DataName));
			}
			if (!Data)
			{
				return Finish(FString::Printf(TEXT("no building data %s in %s or %s (the bench HQs: ue.ps1 -Task setup -BenchHQ)"),
					*DataName, RTSBenchScene::BenchHQDir, BuildingDataPath));
			}
			const FTransform Transform(FRotator(0.f, GetBuildingYaw(), 0.f), BuildingLocation);
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
			Building->SetEquipmentMotionScale(0.f);
			URTSBuildingStateComponent* State = Building->GetStateComponent();
			const IConsoleVariable* ForcedState = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Debug.BuildingState"));
			if (State && ForcedState && (ForcedState->GetFlags() & ECVF_SetByMask) == ECVF_SetByConstructor)
			{
				State->SetDebugState(ERTSBuildingState::Idle);
			}
			if (State)
			{
				State->Refresh(/*bForce*/ true);
			}
			// The clock is held at ShotGameTime each frame and all but stopped in between (the lowest dilation there is).
			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				TimeDilation = Settings->TimeDilation;
				Settings->SetTimeDilation(0.f);
			}
			if (bNight)
			{
				const FRTSShotNight::FMoon Moon = FRTSShotNight::GetGameMoon();
				Night.Set(*World, true, &Moon);
				if (URTSDayNightSubsystem* DayNight = World->GetSubsystem<URTSDayNightSubsystem>())
				{
					DayNight->SetDarkOverride(true);
				}
			}
			Camera = World->SpawnActor<ACameraActor>();
			Camera->GetCameraComponent()->SetFieldOfView(CameraFOV);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Controller->SetViewTarget(Camera.Get());
			IFileManager::Get().MakeDirectory(*GetShotDir(), /*Tree*/ true);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.BuildingShots: %s at %s, r.Nanite %s, %s, %s"), *Data->GetName(),
				*BuildingLocation.ToString(), *GetCVar(TEXT("r.Nanite")), bNight ? TEXT("night") : TEXT("day"),
				State ? *UEnum::GetDisplayValueAsText(State->GetState()).ToString() : TEXT("no state"));
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
				// From the data and the root mesh, not every part's bounds: a turning part would move the camera.
				const FRTSBuildingFootprint Footprint = Building->GetGroundFootprint();
				const float Inset = FMath::Min(FrontInset, 2.f * Footprint.Extent.X * FrontInsetShare);
				const float Height = Root ? FMath::Min(FrontHeight, static_cast<float>(Root->Bounds.BoxExtent.Z)) : FrontHeight;
				LookAt = Footprint.ToWorld(FVector2D(Footprint.Extent.X - Inset, 0.f));
				LookAt.Z = Building->GetActorLocation().Z + Height;
			}
			TurnSunToFront(Shot.bFront && !bNight && !bTurned);
			// Looking along +X, as the RTS camera looks north, for the far views; the close view looks at the building's front
			// (+X at its yaw 0), towards -X unturned and along +X turned.
			const FRotator Rotation(Pitch, Shot.bFront ? GetBuildingYaw() + 180.f : 0.f, 0.f);
			Camera->SetActorLocationAndRotation(LookAt - Rotation.Vector() * Shot.Distance, Rotation);
			GoTo(EStep::Settle);
			return true;
		}

		float GetBuildingYaw() const { return bTurned ? BuildingYaw + TurnYaw : BuildingYaw; }

		/** Turns every sun FrontSunYaw in yaw so the close view's front is lit, or back to where it stood. */
		void TurnSunToFront(bool bTurn)
		{
			if (!bTurn)
			{
				for (const TPair<TWeakObjectPtr<ADirectionalLight>, FRotator>& Sun : SunRotations)
				{
					if (Sun.Key.IsValid())
					{
						Sun.Key->SetActorRotation(Sun.Value);
					}
				}
				SunRotations.Reset();
				return;
			}
			if (!SunRotations.IsEmpty())
			{
				return;
			}
			for (TActorIterator<ADirectionalLight> It(World.Get()); It; ++It)
			{
				It->GetLightComponent()->SetMobility(EComponentMobility::Movable);
				SunRotations.Emplace(*It, It->GetActorRotation());
				It->SetActorRotation(It->GetActorRotation() + FRotator(0.f, FrontSunYaw, 0.f));
			}
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
			TurnSunToFront(false);
			if (TimeDilation && World.IsValid() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->SetTimeDilation(*TimeDilation);
			}
			if (bNight && World.IsValid())
			{
				Night.Set(*World, false);
				if (URTSDayNightSubsystem* DayNight = World->GetSubsystem<URTSDayNightSubsystem>())
				{
					DayNight->SetDarkOverride({});
				}
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
		bool bNight = false;
		bool bTurned = false;
		FRTSShotNight Night;
		/** The world's time dilation before the run held the clock. */
		TOptional<float> TimeDilation;
		/** The suns the close view turned, with the rotation each had. */
		TArray<TPair<TWeakObjectPtr<ADirectionalLight>, FRotator>> SunRotations;
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
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.BuildingShots <BuildingData> [Folder] [-Night] [-Turn] needs a running game"));
			return;
		}
		if (FBuildingShotsRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.BuildingShots is already running"));
			return;
		}
		TArray<FString> Rest(Args);
		const bool bNight = Rest.RemoveAll([](const FString& Arg) { return Arg.Equals(NightArg, ESearchCase::IgnoreCase); }) > 0;
		const bool bTurn = Rest.RemoveAll([](const FString& Arg) { return Arg.Equals(TurnArg, ESearchCase::IgnoreCase); }) > 0;
		if (Rest.IsEmpty())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.BuildingShots <BuildingData> [Folder] [-Night] [-Turn] needs a building"));
			return;
		}
		FString Folder = Rest.Num() > 1 ? Rest[1] : FString(bTurn ? TurnFolder : TEXT(""));
		if (Rest.Num() < 2 && bNight)
		{
			Folder = Folder.IsEmpty() ? FString(NightFolder) : Folder + TEXT("_") + NightFolder;
		}
		const TSharedRef<FBuildingShotsRun> Run = MakeShared<FBuildingShotsRun>(*World, Rest[0], Folder, bNight, bTurn);
		FBuildingShotsRun::Start(Run);
		Output.Logf(TEXT("rts.Debug.BuildingShots: %s to %s"), *Rest[0], *Run->GetShotDir());
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice BuildingShotsCommand(
		TEXT("rts.Debug.BuildingShots"),
		TEXT("rts.Debug.BuildingShots <BuildingData> [Folder] [-Night] [-Turn]: one finished building of the data asset pictured at the RTS camera's pitch from 12, 150 and 450 m ")
		TEXT("and either side of 260 m, with the LOD of every part logged, to Saved/BuildingShots/<BuildingData>[/<Folder>] (#349); -Night at night, to Night by default (#424); -Turn turned half a turn, to Turn or Turn_Night by default (#450)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunBuildingShots));
}

#endif
