#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"
#include "Data/RTSUnitData.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSPlayerController.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

/**
 * Console commands for inspecting a vehicle in a running game:
 *
 * rts.Debug.AimTurret <Yaw> <Pitch> | off: the selected units hold their turret at Yaw and their gun at Pitch (degrees,
 * relative to the hull) instead of aiming, or aim again with off.
 *
 * rts.Debug.UnitShots <UnitData> [StressCount]: pictures of the first unit whose data asset is named UnitData, as the
 * player's RTS camera sees it at its default zoom, zoomed fully in and fully out, and from a close inspection camera, then
 * with its turret turned and gun raised, then picked with the cursor's trace, selected and driving off on a move order
 * issued as the player issues it (the pick, the distance driven and the wheels' roll are logged). With StressCount, that many more of the unit stand in a grid and a last picture
 * shows them with stat unit and stat rhi; the frame time, draw calls and triangles are logged. The pictures go to
 * Saved/UnitShots/<UnitData>/<shot>.png. With -RTSUnitShotsQuit on the command line (ue.ps1 -Task unitshots) the game
 * quits when done, with exit code 1 on failure.
 */
namespace
{
	const TCHAR* QuitParam = TEXT("RTSUnitShotsQuit");

	/** Real seconds the game may take to give the player a camera and the unit, and a shot to arrive. */
	constexpr double ReadyTimeout = 60.0;
	constexpr double CaptureTimeout = 10.0;
	constexpr double ResizeTimeout = 10.0;

	/** The size of every picture; -ResX and -ResY alone leave an offscreen window at its default size. */
	const FIntPoint ShotSize(1920, 1080);
	/** Real seconds for the camera's lag and the turret to settle on a new view, and frames measured for the stats. */
	constexpr double SettleSeconds = 3.0;
	constexpr double TraverseSeconds = 5.0;
	constexpr int32 MeasureFrames = 120;

	/** The inspection camera: in front of the unit and to its left, a little above, with a narrow lens. */
	const FVector CloseUpOffset(950.f, -650.f, 380.f);
	const FVector CloseUpLookAt(0.f, 0.f, 150.f);
	constexpr float CloseUpFOV = 45.f;

	/** The RTS camera's focus for its closest zoom: this far behind the unit along the view, so the unit fills the frame. */
	constexpr float ZoomInStandOff = 1000.f;

	/** The turned turret for the turret shot. */
	const FVector2D TurretShotAim(60.f, 10.f);

	/** The drive shot: the unit is picked along a ray from here relative to it, as from the RTS camera, and sent this far ahead. */
	const FVector PickFrom(-1700.f, 0.f, 2500.f);
	constexpr float DriveDistance = 2500.f;
	constexpr double DriveSeconds = 2.5;

	/**
	 * The stress grid: columns along X by rows along Y, spaced for a tank's length and width, centred here. Bigger units
	 * stand further apart, clear of each other's selection circle, so the avoidance does not keep pushing them.
	 */
	constexpr int32 StressColumns = 24;
	const FVector2D StressSpacing(800.f, 500.f);
	constexpr float StressClearance = 2.05f;
	const FVector StressCentre(0.f, 6200.f, 0.f);

	struct FFrameStats
	{
		double GameMs = 0.0;
		double GameThreadMs = 0.0;
		double AvoidanceMs = 0.0;
		double MaxAvoidanceMs = 0.0;
		double GPUMs = 0.0;
		double DrawCalls = 0.0;
		double Triangles = 0.0;
	};

	class FUnitShotsRun : public TSharedFromThis<FUnitShotsRun>
	{
	public:
		FUnitShotsRun(UWorld& InWorld, const FString& InUnitName, int32 InStressCount)
			: World(&InWorld), UnitName(InUnitName), StressCount(InStressCount)
		{
		}

		static bool IsRunning() { return Active.IsValid(); }

		static void Start(const TSharedRef<FUnitShotsRun>& Run)
		{
			Active = Run;
			Run->StepStart = FPlatformTime::Seconds();
			Run->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddSP(Run, &FUnitShotsRun::OnCaptured);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FUnitShotsRun::Tick));
		}

		static FString GetShotDir(const FString& InUnitName)
		{
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnitShots"), InUnitName);
		}

	private:
		/** A view to picture: it is set up, given time to settle, measured when asked, then saved. */
		struct FShot
		{
			FString Name;
			TFunction<bool(FUnitShotsRun&)> Setup;
			double Settle = SettleSeconds;
			bool bShowStats = false;
			/** The HUD and the selection on the picture; the stats show with it. */
			bool bShowUI = false;
		};

		enum class EStep : uint8
		{
			WaitForUnit,
			Resize,
			Settle,
			Measure,
			Capture,
		};

		bool Tick(float)
		{
			if (!World.IsValid())
			{
				return Finish(TEXT("the world ended"));
			}
			++StepFrames;
			// The cursor rests wherever the offscreen window put it; at an edge it would scroll the camera off the view.
			if (ARTSPlayerController* Controller = GetController(); Controller && GetViewportSize().X > 0)
			{
				Controller->SetMouseLocation(GetViewportSize().X / 2, GetViewportSize().Y / 2);
			}
			switch (Step)
			{
			case EStep::WaitForUnit:
				return WaitForUnit();
			case EStep::Resize:
				return Resize();
			case EStep::Settle:
				if (FPlatformTime::Seconds() - StepStart >= Shots[ShotIndex].Settle)
				{
					GoTo(Shots[ShotIndex].bShowStats ? EStep::Measure : EStep::Capture);
					if (Step == EStep::Capture)
					{
						RequestShot();
					}
				}
				return true;
			case EStep::Measure:
				return Measure();
			case EStep::Capture:
				return Capture();
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

		ARTSCameraPawn* GetCameraPawn() const
		{
			const ARTSPlayerController* Controller = GetController();
			return Controller ? Controller->GetPawn<ARTSCameraPawn>() : nullptr;
		}

		ARTSUnit* FindUnit() const
		{
			for (TActorIterator<ARTSUnit> It(World.Get()); It; ++It)
			{
				if (It->GetUnitData() && It->GetUnitData()->GetName() == UnitName)
				{
					return *It;
				}
			}
			return nullptr;
		}

		bool WaitForUnit()
		{
			Unit = FindUnit();
			if (!World->HasBegunPlay() || !GetCameraPawn() || !Unit.IsValid())
			{
				return FPlatformTime::Seconds() - StepStart > ReadyTimeout
					? Finish(FString::Printf(TEXT("no camera, or no unit with data %s, in time"), *UnitName)) : true;
			}
			if (!Unit->HasAuthority())
			{
				return Finish(TEXT("the game is a client; the unit is posed on the server"));
			}
			BuildShots();
			GoTo(EStep::Resize);
			return true;
		}

		FIntPoint GetViewportSize() const
		{
			const UGameViewportClient* Client = World->GetGameViewport();
			return Client && Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		}

		/** PIE draws into the editor's viewport, whose size the game cannot change: there the shots take its size. */
		bool Resize()
		{
			if (World->WorldType == EWorldType::PIE || GetViewportSize() == ShotSize)
			{
				return StartShot();
			}
			if (StepFrames == 1)
			{
				FSystemResolution::RequestResolutionChange(ShotSize.X, ShotSize.Y, EWindowMode::Windowed);
			}
			if (FPlatformTime::Seconds() - StepStart > ResizeTimeout)
			{
				const FIntPoint Size = GetViewportSize();
				return Finish(FString::Printf(TEXT("the screen stayed %dx%d instead of %dx%d"), Size.X, Size.Y, ShotSize.X, ShotSize.Y));
			}
			return true;
		}

		/** The camera pawn looks along its own yaw; the unit is turned to show the camera its front three-quarter. */
		void BuildShots()
		{
			Shots.Add({ TEXT("rts_default"), [](FUnitShotsRun& Run)
			{
				Run.PoseUnit();
				Run.UseRTSCamera(Run.GetCameraPawn()->GetDefaultZoom(), 0.f);
				return true;
			} });
			Shots.Add({ TEXT("zoom_in_max"), [](FUnitShotsRun& Run)
			{
				Run.UseRTSCamera(Run.GetCameraPawn()->GetMinZoom(), ZoomInStandOff);
				return true;
			} });
			Shots.Add({ TEXT("zoom_out_max"), [](FUnitShotsRun& Run)
			{
				Run.UseRTSCamera(Run.GetCameraPawn()->GetMaxZoom(), 0.f);
				return true;
			} });
			Shots.Add({ TEXT("closeup"), [](FUnitShotsRun& Run)
			{
				return Run.UseCloseUpCamera();
			} });
			Shots.Add({ TEXT("turret_rotated"), [](FUnitShotsRun& Run)
			{
				if (!Run.Unit->HasTurret())
				{
					UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.UnitShots: %s has no turret to turn"), *Run.UnitName);
				}
				Run.Unit->SetAimOverride(TurretShotAim);
				return true;
			}, TraverseSeconds });
			Shots.Add({ TEXT("driving"), [](FUnitShotsRun& Run)
			{
				Run.Unit->SetAimOverride({});
				return Run.Drive();
			}, DriveSeconds, false, true });
			// The same view at the default zoom with the stats on, before and after the stress grid, to compare.
			Shots.Add({ TEXT("stats_one"), [](FUnitShotsRun& Run)
			{
				Run.Unit->SetAimOverride({});
				Run.UseRTSCamera(Run.GetCameraPawn()->GetDefaultZoom(), 0.f);
				Run.SetStats(true);
				return true;
			}, SettleSeconds, true });
			if (StressCount > 0)
			{
				Shots.Add({ FString::Printf(TEXT("stress_%d"), StressCount), [](FUnitShotsRun& Run)
				{
					Run.SpawnStressGrid();
					Run.GetCameraPawn()->JumpTo(StressCentre);
					Run.GetCameraPawn()->SetZoomNow(Run.GetCameraPawn()->GetMaxZoom());
					return true;
				}, SettleSeconds, true });
			}
		}

		/** Sets up the next shot, or finishes after the last; false once finished, so the ticker drops the run. */
		bool StartShot()
		{
			if (ShotIndex >= Shots.Num())
			{
				return Finish(FString());
			}
			if (!Unit.IsValid())
			{
				return Finish(TEXT("the unit is gone"));
			}
			if (!Shots[ShotIndex].Setup(*this))
			{
				return Finish(FString::Printf(TEXT("could not set up %s"), *Shots[ShotIndex].Name));
			}
			GoTo(EStep::Settle);
			return true;
		}

		/** Stands the unit still, facing back toward the camera's default heading at a three-quarter angle. */
		void PoseUnit()
		{
			FRTSCommand Stop;
			Stop.Type = ERTSCommandType::Stop;
			Unit->ExecuteCommand(Stop);
			Unit->SetActorRotation(FRotator(0.f, GetCameraPawn()->GetActorRotation().Yaw + 145.f, 0.f));
		}

		void UseRTSCamera(float Zoom, float StandOff)
		{
			ARTSCameraPawn* Camera = GetCameraPawn();
			GetController()->SetViewTarget(Camera);
			Camera->JumpTo(Unit->GetActorLocation() - Camera->GetActorForwardVector() * StandOff);
			Camera->SetZoomNow(Zoom);
		}

		bool UseCloseUpCamera()
		{
			if (!CloseUp.IsValid())
			{
				CloseUp = World->SpawnActor<ACameraActor>();
				if (!CloseUp.IsValid())
				{
					return false;
				}
				CloseUp->GetCameraComponent()->SetFieldOfView(CloseUpFOV);
				CloseUp->GetCameraComponent()->bConstrainAspectRatio = false;
			}
			const FTransform Pose = Unit->GetActorTransform();
			const FVector Ground = Unit->GetNavAgentLocation();
			const FVector Eye = Ground + Pose.TransformVectorNoScale(CloseUpOffset);
			CloseUp->SetActorLocationAndRotation(Eye, (Ground + CloseUpLookAt - Eye).Rotation());
			GetController()->SetViewTarget(CloseUp.Get());
			return true;
		}

		/** Picks the unit as the cursor would, selects it and orders it ahead through the player's own command path. */
		bool Drive()
		{
			ARTSPlayerController* Controller = GetController();
			const FVector Origin = Unit->GetActorLocation() + PickFrom;
			const ARTSUnit* Picked = Controller->GetUnitAlongRay(Origin, (Unit->GetActorLocation() - Origin).GetSafeNormal());
			Controller->SelectUnits({ Unit.Get() });
			const bool bSelected = Controller->GetSelectedUnits().Contains(Unit);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: picked %s by the cursor's trace, %s"),
				Picked == Unit ? TEXT("the unit") : *GetNameSafe(Picked), bSelected ? TEXT("selected") : TEXT("NOT selected"));
			if (Picked != Unit || !bSelected)
			{
				return false;
			}

			for (int32 Part = 0; Unit->GetUnitData() && Part < Unit->GetUnitData()->Parts.Num(); ++Part)
			{
				if (Unit->GetUnitData()->Parts[Part].bWheel)
				{
					WheelPart = Part;
					Unit->GetPartTransform(Part, WheelBefore);
					break;
				}
			}
			DriveStart = Unit->GetActorLocation();
			FRTSCommand Move;
			Move.Type = ERTSCommandType::Move;
			Move.TargetLocation = Unit->GetNavAgentLocation() + Unit->GetActorForwardVector() * DriveDistance;
			Controller->Server_IssueCommand({ Unit.Get() }, Move);
			// The RTS camera at its default zoom over the middle of the drive.
			GetController()->SetViewTarget(GetCameraPawn());
			GetCameraPawn()->JumpTo(DriveStart + Unit->GetActorForwardVector() * DriveDistance / 2.f);
			GetCameraPawn()->SetZoomNow(GetCameraPawn()->GetDefaultZoom());
			bDriving = true;
			return true;
		}

		void LogDrive()
		{
			FTransform WheelNow;
			const bool bWheel = WheelPart != INDEX_NONE && Unit->GetPartTransform(WheelPart, WheelNow);
			const float Roll = bWheel ? FMath::RadiansToDegrees(WheelBefore.GetRotation().AngularDistance(WheelNow.GetRotation())) : 0.f;
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: drove %.0f cm in %.1f s at up to %.0f cm/s; first wheel turned %.0f degrees from where it stood"),
				FVector::Dist2D(DriveStart, Unit->GetActorLocation()), DriveSeconds, Unit->GetUnitData()->MoveSpeed, Roll);
			bDriving = false;
		}

		void SetStats(bool bOn)
		{
			if (bStatsOn == bOn)
			{
				return;
			}
			// stat commands toggle.
			GEngine->Exec(World.Get(), TEXT("stat unit"));
			GEngine->Exec(World.Get(), TEXT("stat rhi"));
			bStatsOn = bOn;
		}

		void SpawnStressGrid()
		{
			const int32 Rows = FMath::DivideAndRoundUp(StressCount, StressColumns);
			const FVector2D Spacing = FVector2D::Max(StressSpacing, FVector2D(Unit->GetSelectionRadius() * StressClearance));
			const FVector2D Size((StressColumns - 1) * Spacing.X, (Rows - 1) * Spacing.Y);
			for (int32 Index = 0; Index < StressCount; ++Index)
			{
				const FVector Location = StressCentre + FVector(Index % StressColumns * Spacing.X - Size.X / 2.f,
					Index / StressColumns * Spacing.Y - Size.Y / 2.f, Unit->GetGroundOffset());
				const FTransform Transform(FRotator::ZeroRotator, Location);
				ARTSUnit* Spawned = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform);
				Spawned->InitFromData(Unit->GetUnitData(), Unit->GetTeamId());
				Spawned->FinishSpawning(Transform);
			}
		}

		bool Measure()
		{
			Stats.GameMs += FApp::GetDeltaTime() * 1000.0;
			Stats.GameThreadMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
			const URTSAvoidanceSubsystem* Avoidance = World->GetSubsystem<URTSAvoidanceSubsystem>();
			const double AvoidanceMs = Avoidance ? Avoidance->GetLastSeparateSeconds() * 1000.0 : 0.0;
			Stats.AvoidanceMs += AvoidanceMs;
			Stats.MaxAvoidanceMs = FMath::Max(Stats.MaxAvoidanceMs, AvoidanceMs);
			Stats.GPUMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
			Stats.DrawCalls += GNumDrawCallsRHI[0];
			Stats.Triangles += GNumPrimitivesDrawnRHI[0];
			if (StepFrames < MeasureFrames)
			{
				return true;
			}
			UE_LOG(LogWorldDivide, Display,
				TEXT("rts.Debug.UnitShots: %s over %d frames: frame %.2f ms, game thread %.2f ms, avoidance %.2f ms (max %.2f), GPU %.2f ms, %.0f draw calls, %.0f triangles drawn, %d units"),
				*Shots[ShotIndex].Name, MeasureFrames, Stats.GameMs / MeasureFrames, Stats.GameThreadMs / MeasureFrames,
				Stats.AvoidanceMs / MeasureFrames, Stats.MaxAvoidanceMs, Stats.GPUMs / MeasureFrames,
				Stats.DrawCalls / MeasureFrames, Stats.Triangles / MeasureFrames, CountUnits());
			Stats = FFrameStats();
			GoTo(EStep::Capture);
			RequestShot();
			return true;
		}

		int32 CountUnits() const
		{
			int32 Count = 0;
			for (TActorIterator<ARTSUnit> It(World.Get()); It; ++It)
			{
				++Count;
			}
			return Count;
		}

		void RequestShot()
		{
			if (bDriving)
			{
				LogDrive();
			}
			const APlayerCameraManager* View = GetController()->PlayerCameraManager;
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: %s from %s looking %s at %s, unit at %s"), *Shots[ShotIndex].Name,
				*View->GetCameraLocation().ToString(), *View->GetCameraRotation().ToString(), *GetNameSafe(GetController()->GetViewTarget()),
				*Unit->GetActorLocation().ToString());
			Pixels.Reset();
			// The stats draw on the viewport with the UI; the views without either leave the HUD out.
			FScreenshotRequest::RequestScreenshot(/*bInShowUI*/ Shots[ShotIndex].bShowStats || Shots[ShotIndex].bShowUI,
				/*bInRestrictToGameViewport*/ true);
		}

		void OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
		{
			if (Step == EStep::Capture)
			{
				PixelsSize = FIntPoint(Width, Height);
				Pixels = Colors;
			}
		}

		bool Capture()
		{
			if (Pixels.IsEmpty())
			{
				return FPlatformTime::Seconds() - StepStart > CaptureTimeout ? Finish(TEXT("no screenshot arrived")) : true;
			}
			const FString Path = FPaths::Combine(GetShotDir(UnitName), Shots[ShotIndex].Name + TEXT(".png"));
			for (FColor& Pixel : Pixels)
			{
				Pixel.A = 255;
			}
			if (!FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), PixelsSize.X, PixelsSize.Y)))
			{
				return Finish(FString::Printf(TEXT("could not write %s"), *Path));
			}
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: %s"), *FPaths::ConvertRelativePathToFull(Path));
			++Written;
			++ShotIndex;
			return StartShot();
		}

		/** Gives the player their camera back; false, so the ticker drops the run. */
		bool Finish(const FString& Error)
		{
			if (World.IsValid())
			{
				SetStats(false);
				if (Unit.IsValid())
				{
					Unit->SetAimOverride({});
				}
				if (ARTSPlayerController* Controller = GetController(); Controller && GetCameraPawn())
				{
					Controller->SetViewTarget(GetCameraPawn());
				}
				if (CloseUp.IsValid())
				{
					CloseUp->Destroy();
				}
			}
			if (Error.IsEmpty())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: wrote %d files"), Written);
			}
			else
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.UnitShots failed: %s"), *Error);
			}
			UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
			if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() ? 0 : 1);
			}
			// The ticker holds the last reference until it drops the run.
			Active.Reset();
			return false;
		}

		static TSharedPtr<FUnitShotsRun> Active;

		TWeakObjectPtr<UWorld> World;
		FString UnitName;
		int32 StressCount = 0;
		TWeakObjectPtr<ARTSUnit> Unit;
		TWeakObjectPtr<ACameraActor> CloseUp;

		TArray<FShot> Shots;
		int32 ShotIndex = 0;
		EStep Step = EStep::WaitForUnit;
		int32 StepFrames = 0;
		double StepStart = 0.0;
		bool bStatsOn = false;
		bool bDriving = false;
		FVector DriveStart = FVector::ZeroVector;
		int32 WheelPart = INDEX_NONE;
		FTransform WheelBefore;
		int32 Written = 0;
		FFrameStats Stats;

		TArray<FColor> Pixels;
		FIntPoint PixelsSize = FIntPoint::ZeroValue;
		FDelegateHandle CaptureHandle;
	};

	TSharedPtr<FUnitShotsRun> FUnitShotsRun::Active;

	void RunUnitShotsCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld() || Args.IsEmpty())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.UnitShots <UnitData> [StressCount] needs a running game"));
			return;
		}
		if (FUnitShotsRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.UnitShots is already running"));
			return;
		}
		const int32 StressCount = Args.Num() > 1 ? FMath::Max(FCString::Atoi(*Args[1]), 0) : 0;
		Output.Logf(TEXT("rts.Debug.UnitShots: %s to %s"), *Args[0],
			*FPaths::ConvertRelativePathToFull(FUnitShotsRun::GetShotDir(Args[0])));
		FUnitShotsRun::Start(MakeShared<FUnitShotsRun>(*World, Args[0], StressCount));
	}

	void RunAimTurretCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		const ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller || !Controller->HasAuthority())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.AimTurret needs a running game on the server"));
			return;
		}
		TOptional<FVector2D> Aim;
		if (Args.Num() >= 2)
		{
			Aim = FVector2D(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
		}
		else if (Args.Num() != 1 || Args[0] != TEXT("off"))
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.AimTurret <Yaw> <Pitch> | off"));
			return;
		}
		int32 Count = 0;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Controller->GetSelectedUnits())
		{
			if (Unit.IsValid() && Unit->HasTurret())
			{
				Unit->SetAimOverride(Aim);
				++Count;
			}
		}
		Output.Logf(TEXT("rts.Debug.AimTurret: %d selected units with a turret"), Count);
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice UnitShotsCommand(
		TEXT("rts.Debug.UnitShots"),
		TEXT("rts.Debug.UnitShots <UnitData> [StressCount]: saves views of the first unit with that data asset (RTS camera at its ")
		TEXT("default, closest and farthest zoom, a close-up, its turret turned) to Saved/UnitShots, and with StressCount a ")
		TEXT("grid of that many more with stat unit and stat rhi."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunUnitShotsCommand));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice AimTurretCommand(
		TEXT("rts.Debug.AimTurret"),
		TEXT("rts.Debug.AimTurret <Yaw> <Pitch> | off: the selected units hold their turret and gun there (degrees, relative ")
		TEXT("to the hull), or aim again."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunAimTurretCommand));
}

#endif
