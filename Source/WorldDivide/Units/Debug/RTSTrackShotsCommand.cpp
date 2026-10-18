#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Data/RTSUnitData.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSPlayerController.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "UnrealClient.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#include "WorldDivide.h"

/**
 * rts.Debug.TrackShots <UnitData>: close-up frame sequences of a tracked unit's running gear (#151), from a camera carried with
 * the hull beside its left track, or at its sprocket or idler: driving forward and in reverse, turning either way, pivoting
 * either way, accelerating, braking, stopped, driven into Block_North, and coming back into the animation distance from
 * far away. Each scenario runs the gear on a command as the server does, then saves Frames pictures one after another to
 * Saved/TrackShots/<UnitData>/<scenario>_<n>.png, and logs with each the realized side travel, each track's phase and what
 * its material has. With -RTSTrackShotsQuit on the command line (ue.ps1 -Task trackshots) the game quits when done.
 */
namespace RTSTrackShots
{
	const TCHAR* QuitParam = TEXT("RTSTrackShotsQuit");
	constexpr double ReadyTimeout = 60.0;
	constexpr double CaptureTimeout = 10.0;
	constexpr int32 Frames = 12;

	/** The empty strip north of Block_North in L_TestMap, where the unit drives; Block_North is driven into from the north. */
	const FVector DriveStart(-8000.f, 20000.f, 0.f);
	const TCHAR* BlockLabel = TEXT("Block_North");

	/**
	 * Where the camera rides, in the hull's frame from the ground under its middle (as m20_guardian.json places the wheels):
	 * beside the left track, low enough to see under the skirts, and close to its sprocket (rear) and idler (front).
	 */
	struct FView
	{
		FVector Eye;
		FVector LookAt;
		float FOV;
	};
	const FView SideView{ FVector(0.f, -720.f, 45.f), FVector(0.f, -132.f, 40.f), 60.f };
	const FView SprocketView{ FVector(-170.f, -390.f, 30.f), FVector(-285.f, -132.f, 50.f), 50.f };
	const FView IdlerView{ FVector(170.f, -390.f, 30.f), FVector(285.f, -132.f, 50.f), 50.f };
	/** Far enough that the renderer leaves the links' shift out. */
	const FView FarView{ FVector(0.f, -9000.f, 2500.f), FVector(0.f, -132.f, 40.f), 10.f };

	struct FScenario
	{
		FString Name;
		FView View;
		/** Set up the unit, its place and its command; false when it cannot. */
		TFunction<bool(ARTSUnit&)> Setup;
		/** Real seconds the gear runs before the first picture; or, with Until, at most this long until it holds. */
		double Warm = 2.0;
		TFunction<bool(const ARTSUnit&)> Until;
		/** Run just before the first picture, such as a new command to brake on. */
		TFunction<void(ARTSUnit&)> AtFirst;
		/** Real seconds between pictures; zero takes them as fast as they come. */
		double Spacing = 0.0;
		/**
		 * The game's speed while the pictures are taken: slowed, so that from one picture to the next the links move a share of
		 * a link rather than several, which a sequence could not tell from going backward.
		 */
		float TimeDilation = 0.1f;
	};

	RTSLocomotion::FCommand Ahead(float Speed)
	{
		RTSLocomotion::FCommand Command;
		Command.Speed = Speed;
		return Command;
	}

	class FTrackShotsRun : public TSharedFromThis<FTrackShotsRun>
	{
	public:
		FTrackShotsRun(UWorld& InWorld, const FString& InUnitName)
			: World(&InWorld), UnitName(InUnitName)
		{
		}

		static bool IsRunning() { return Active.IsValid(); }

		static void Start(const TSharedRef<FTrackShotsRun>& Run)
		{
			Active = Run;
			Run->StepStart = FPlatformTime::Seconds();
			Run->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddSP(Run, &FTrackShotsRun::OnCaptured);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FTrackShotsRun::Tick));
		}

		static FString GetShotDir(const FString& InUnitName)
		{
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrackShots"), InUnitName);
		}

	private:
		enum class EStep : uint8 { WaitForUnit, Warm, Capture };

		bool Tick(float)
		{
			if (!World.IsValid())
			{
				return Finish(TEXT("the world ended"));
			}
			switch (Step)
			{
			case EStep::WaitForUnit:
				return WaitForUnit();
			case EStep::Warm:
				return Warm();
			case EStep::Capture:
				return Capture();
			}
			return true;
		}

		bool WaitForUnit()
		{
			for (TActorIterator<ARTSUnit> It(World.Get()); It && !Unit.IsValid(); ++It)
			{
				if (It->GetUnitData() && It->GetUnitData()->GetName() == UnitName)
				{
					Unit = *It;
				}
			}
			ARTSPlayerController* Controller = Cast<ARTSPlayerController>(World->GetFirstPlayerController());
			if (!World->HasBegunPlay() || !Controller || !Unit.IsValid())
			{
				return FPlatformTime::Seconds() - StepStart > ReadyTimeout ? Finish(TEXT("no player or unit in time")) : true;
			}
#if WITH_EDITOR
			// Until the shaders are in, the tracks draw with the default material.
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			{
				return true;
			}
#endif
			if (!Unit->HasAuthority() || !Unit->GetUnitMovement()->GetLimits().IsTracked())
			{
				return Finish(TEXT("the unit must be tracked, on the server"));
			}
			for (TActorIterator<AStaticMeshActor> It(World.Get()); It; ++It)
			{
				if (It->GetActorLabel() == BlockLabel)
				{
					Block = It->GetComponentsBoundingBox();
				}
			}
			for (int32 PartIndex = 0; PartIndex < Unit->GetUnitData()->Parts.Num(); ++PartIndex)
			{
				const FRTSVehiclePart& Part = Unit->GetUnitData()->Parts[PartIndex];
				if (Part.TrackPitch > 0.f)
				{
					TrackParts[Part.Offset.Y < 0.f ? 0 : 1] = PartIndex;
				}
			}
			Camera = World->SpawnActor<ACameraActor>();
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->AttachToActor(Unit.Get(), FAttachmentTransformRules::KeepRelativeTransform);
			Controller->SetViewTarget(Camera.Get());
			// Each picture shows where the links are, not where they were going.
			GEngine->Exec(World.Get(), TEXT("r.MotionBlurQuality 0"));
			GroundOffset = Unit->GetActorLocation().Z - Unit->GetNavAgentLocation().Z;
			// Stop whatever the map gave it, and stand it on the open strip.
			FRTSCommand Stop;
			Stop.Type = ERTSCommandType::Stop;
			Unit->ExecuteCommand(Stop);
			BuildScenarios();
			return StartScenario();
		}

		void Place(ARTSUnit& InUnit, const FVector& Location, float Yaw)
		{
			InUnit.SetActorLocationAndRotation(FVector(Location.X, Location.Y, InUnit.GetActorLocation().Z), FRotator(0.f, Yaw, 0.f));
		}

		void BuildScenarios()
		{
			auto Drive = [this](RTSLocomotion::FCommand Command)
			{
				return [this, Command](ARTSUnit& InUnit)
				{
					Place(InUnit, DriveStart, 0.f);
					InUnit.GetUnitMovement()->SetCommandOverride(Command);
					return true;
				};
			};
			auto Tracks = [](float Left, float Right)
			{
				return [Left, Right](const RTSLocomotion::FLimits& Limits) { return RTSLocomotion::FCommand::FromTracks(Limits, Left, Right); };
			};
			const RTSLocomotion::FLimits& Limits = Unit->GetUnitMovement()->GetLimits();
			Scenarios.Add({ TEXT("forward"), SideView, Drive(Ahead(300.f)) });
			Scenarios.Add({ TEXT("reverse"), SideView, Drive(Ahead(-250.f)) });
			Scenarios.Add({ TEXT("turn_left"), SideView, Drive(Tracks(150.f, 350.f)(Limits)) });
			Scenarios.Add({ TEXT("turn_right"), SideView, Drive(Tracks(350.f, 150.f)(Limits)) });
			Scenarios.Add({ TEXT("pivot_left"), SideView, Drive(Tracks(-200.f, 200.f)(Limits)) });
			Scenarios.Add({ TEXT("pivot_right"), SideView, Drive(Tracks(200.f, -200.f)(Limits)) });
			Scenarios.Add({ TEXT("accelerate"), SideView, Drive(RTSLocomotion::FCommand()), 1.0, nullptr,
				[](ARTSUnit& InUnit) { InUnit.GetUnitMovement()->SetCommandOverride(Ahead(400.f)); }, 0.3, 0.3f });
			Scenarios.Add({ TEXT("brake"), SideView, Drive(Ahead(400.f)), 2.5, nullptr,
				[](ARTSUnit& InUnit) { InUnit.GetUnitMovement()->SetCommandOverride(RTSLocomotion::FCommand()); }, 0.12, 0.3f });
			Scenarios.Add({ TEXT("stopped"), SideView, Drive(RTSLocomotion::FCommand()), 1.0, nullptr, nullptr, 0.4, 1.f });
			Scenarios.Add({ TEXT("blocked"), SideView, [this](ARTSUnit& InUnit)
			{
				if (!Block.IsValid)
				{
					return false;
				}
				Place(InUnit, FVector(Block.GetCenter().X, Block.Max.Y + 900.f, 0.f), -90.f);
				InUnit.GetUnitMovement()->SetCommandOverride(Ahead(1.e5f));
				return true;
			}, 8.0, [](const ARTSUnit& InUnit) { return InUnit.GetUnitMovement()->GetBlockedSeconds() > 1.0f; }, nullptr, 0.4, 1.f });
			Scenarios.Add({ TEXT("wrap_sprocket"), SprocketView, Drive(Ahead(150.f)) });
			Scenarios.Add({ TEXT("wrap_idler"), IdlerView, Drive(Ahead(150.f)) });
			Scenarios.Add({ TEXT("far"), FarView, Drive(Ahead(300.f)), 2.0, nullptr, nullptr, 0.1, 1.f });
			Scenarios.Add({ TEXT("back_near"), SideView, [](ARTSUnit&) { return true; }, 0.0 });
		}

		bool StartScenario()
		{
			if (Index >= Scenarios.Num())
			{
				return Finish(FString());
			}
			if (!Unit.IsValid())
			{
				return Finish(TEXT("the unit is gone"));
			}
			const FScenario& Scenario = Scenarios[Index];
			if (!Scenario.Setup(*Unit))
			{
				UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.TrackShots: skipped %s"), *Scenario.Name);
				++Index;
				return StartScenario();
			}
			Camera->SetActorRelativeLocation(Scenario.View.Eye - FVector(0.f, 0.f, GroundOffset));
			Camera->SetActorRelativeRotation((Scenario.View.LookAt - Scenario.View.Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(Scenario.View.FOV);
			Step = EStep::Warm;
			StepStart = FPlatformTime::Seconds();
			Shot = 0;
			return true;
		}

		bool Warm()
		{
			const FScenario& Scenario = Scenarios[Index];
			const double Elapsed = FPlatformTime::Seconds() - StepStart;
			const bool bReady = Scenario.Until ? (Scenario.Until(*Unit) || Elapsed > Scenario.Warm) : Elapsed >= Scenario.Warm;
			if (!bReady)
			{
				return true;
			}
			if (Scenario.AtFirst)
			{
				Scenario.AtFirst(*Unit);
			}
			World->GetWorldSettings()->SetTimeDilation(Scenario.TimeDilation);
			Request();
			return true;
		}

		void Request()
		{
			Step = EStep::Capture;
			StepStart = FPlatformTime::Seconds();
			Pixels.Reset();
			FScreenshotRequest::RequestScreenshot(/*bInShowUI*/ false, /*bInRestrictToGameViewport*/ true);
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
			const FScenario& Scenario = Scenarios[Index];
			if (Pixels.IsEmpty())
			{
				return FPlatformTime::Seconds() - StepStart > CaptureTimeout ? Finish(TEXT("no screenshot arrived")) : true;
			}
			if (Scenario.Spacing > 0.0 && FPlatformTime::Seconds() - LastShotTime < Scenario.Spacing && Shot > 0)
			{
				return true;
			}
			const FString Path = FPaths::Combine(GetShotDir(UnitName), FString::Printf(TEXT("%s_%02d.png"), *Scenario.Name, Shot));
			for (FColor& Pixel : Pixels)
			{
				Pixel.A = 255;
			}
			if (!FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), PixelsSize.X, PixelsSize.Y)))
			{
				return Finish(FString::Printf(TEXT("could not write %s"), *Path));
			}
			LogGear(Scenario.Name);
			LastShotTime = FPlatformTime::Seconds();
			if (++Shot < Frames)
			{
				Request();
				return true;
			}
			World->GetWorldSettings()->SetTimeDilation(1.f);
			++Index;
			return StartScenario();
		}

		/** The realized motion, each track's phase and what its material has, for the picture just saved. */
		void LogGear(const FString& Name) const
		{
			const FRTSRealizedMotion& Motion = Unit->GetRealizedMotion();
			float Phase[2] = { -1.f, -1.f };
			float Sent[2] = { -1.f, -1.f };
			for (int32 Side = 0; Side < 2; ++Side)
			{
				Unit->GetTrackPhase(TrackParts[Side], Phase[Side]);
				TArray<UStaticMeshComponent*> Meshes;
				Unit->GetComponents(Meshes);
				const UStaticMesh* Mesh = Unit->GetUnitData()->Parts.IsValidIndex(TrackParts[Side]) ? Unit->GetUnitData()->Parts[TrackParts[Side]].Mesh.Get() : nullptr;
				for (const UStaticMeshComponent* Component : Meshes)
				{
					if (Component->GetStaticMesh() == Mesh && Component->GetCustomPrimitiveData().Data.Num() > 0)
					{
						Sent[Side] = Component->GetCustomPrimitiveData().Data[0];
					}
				}
			}
			UE_LOG(LogWorldDivide, Display,
				TEXT("rts.Debug.TrackShots: %s %02d: %s, %.0f cm/s, %.1f deg/s; travel L %.1f R %.1f cm; phase L %.4f (material %.4f) R %.4f (material %.4f)"),
				*Name, Shot, *UEnum::GetValueAsString(Motion.Status), Motion.ForwardSpeed, Motion.YawRate, Motion.LeftTravel, Motion.RightTravel,
				Phase[0], Sent[0], Phase[1], Sent[1]);
		}

		bool Finish(const FString& Error)
		{
			if (Unit.IsValid())
			{
				Unit->GetUnitMovement()->SetCommandOverride({});
			}
			if (World.IsValid())
			{
				World->GetWorldSettings()->SetTimeDilation(1.f);
			}
			if (Camera.IsValid())
			{
				Camera->Destroy();
			}
			if (Error.IsEmpty())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.TrackShots: done"));
			}
			else
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.TrackShots failed: %s"), *Error);
			}
			UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
			if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() ? 0 : 1);
			}
			Active.Reset();
			return false;
		}

		static TSharedPtr<FTrackShotsRun> Active;

		TWeakObjectPtr<UWorld> World;
		FString UnitName;
		TWeakObjectPtr<ARTSUnit> Unit;
		TWeakObjectPtr<ACameraActor> Camera;
		FBox Block = FBox(ForceInit);
		int32 TrackParts[2] = { INDEX_NONE, INDEX_NONE };
		/** The actor's height over the ground it stands on, which the views are given from. */
		float GroundOffset = 0.f;
		TArray<FScenario> Scenarios;
		int32 Index = 0;
		int32 Shot = 0;
		EStep Step = EStep::WaitForUnit;
		double StepStart = 0.0;
		double LastShotTime = 0.0;
		TArray<FColor> Pixels;
		FIntPoint PixelsSize = FIntPoint::ZeroValue;
		FDelegateHandle CaptureHandle;
	};

	TSharedPtr<FTrackShotsRun> FTrackShotsRun::Active;

	void RunTrackShotsCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld() || Args.IsEmpty() || FTrackShotsRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.TrackShots <UnitData> needs a running game, and runs one at a time"));
			return;
		}
		Output.Logf(TEXT("rts.Debug.TrackShots: %s to %s"), *Args[0], *FPaths::ConvertRelativePathToFull(FTrackShotsRun::GetShotDir(Args[0])));
		FTrackShotsRun::Start(MakeShared<FTrackShotsRun>(*World, Args[0]));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice TrackShotsCommand(
		TEXT("rts.Debug.TrackShots"),
		TEXT("rts.Debug.TrackShots <UnitData>: saves close-up frame sequences of a tracked unit's running gear driving, turning, ")
		TEXT("pivoting, ramping, stopped and blocked to Saved/TrackShots (#151)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunTrackShotsCommand));
}

#endif
