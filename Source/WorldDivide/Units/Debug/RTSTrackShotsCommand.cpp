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
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSPlayerController.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "UnrealClient.h"
#include "Units/Debug/RTSShotNight.h"
#include "Units/RTSDustSubsystem.h"
#include "Units/RTSExhaustSubsystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#include "WorldDivide.h"

/**
 * rts.Debug.TrackShots <UnitData>: close-up frame sequences of a tracked unit's running gear (#151), from a camera carried with
 * the hull beside its left track, or at its sprocket or idler: driving forward and in reverse, turning either way, pivoting
 * either way, accelerating, braking, stopped, driven into Block_North, and coming back into the animation distance from
 * far away; then driving past a still camera at full speed, beside it and from the RTS angle, plain and with the previous
 * frame's reprojection shown (motion_*, #243); and standing while its gun fires, the hull rocking on its suspension, seen from
 * beside the track the gun turns to and from the sprocket and idler the gun lifts (fire_*, #219); and its engine's exhaust
 * standing, setting off and driving, seen from behind, by day and by night (exhaust_*, #222, #325); and its body
 * squatting as it pulls away, diving as it brakes, leaning into a turn and its turret trailing a traverse (body_*, #297); and
 * the dust behind its tracks standing, driving slowly and fast and pivoting, seen from behind and above with the ground it
 * leaves (dust_<ground>_*, #224), on the map's dirt and on each of its ground strips (#336). A wheeled unit takes the dust
 * scenarios only. A unit the map does not hold, such as the flak truck, is spawned for team 0 on the open strip. Each scenario runs the gear on a command as the server does, then saves Frames pictures one after another to
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
	 * L_TestMap's strips of other ground (setup_testmap.py, TEST_GROUND_STRIPS), labelled Ground_<Kind> and running along x:
	 * the dust scenarios drive along each from this far in from its west end (#336).
	 */
	const TCHAR* StripPrefix = TEXT("Ground_");
	constexpr float StripRunIn = 1500.f;

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
	/** Fixed views for the motion scenarios, from the start place: the hull passes their middle a second after it sets off. */
	const FView MotionSideView{ FVector(300.f, -900.f, 60.f), FVector(300.f, -132.f, 40.f), 60.f };
	const FView MotionRTSView{ FVector(-900.f, -1200.f, 1700.f), FVector(300.f, 0.f, 0.f), 30.f };
	/** Behind the hull at three quarters and above, on the rear grille where the exhaust leaves (#222). */
	const FView ExhaustView{ FVector(-1100.f, -650.f, 420.f), FVector(-380.f, 0.f, 90.f), 50.f };
	/** Square behind the hull at its height, so its lean shows against the horizon, and high over the turret for its lag (#297). */
	const FView RearView{ FVector(-1300.f, 0.f, 180.f), FVector(0.f, 0.f, 150.f), 40.f };
	const FView TopView{ FVector(-200.f, -500.f, 1600.f), FVector(0.f, 0.f, 200.f), 45.f };
	/** Behind the hull at three quarters and well above it, on the ground behind the tracks or wheels, where the dust rises (#224). */
	const FView DustView{ FVector(-1800.f, -1100.f, 750.f), FVector(-700.f, 0.f, 0.f), 55.f };

	/**
	 * The body scenarios (#297): game seconds between pictures, through the hull's settle and the turret's traverse, at a
	 * quarter of the game's speed.
	 */
	constexpr double BodySpacing = 0.1;
	constexpr double TurretSpacing = 0.2;
	constexpr float BodyTimeDilation = 0.25f;

	/**
	 * The exhaust scenarios (#222): game seconds for the plume to build before the first picture, game seconds between
	 * pictures, and the game's step while they run. Each picture holds the game for a long frame, which would show the puffs
	 * of that frame all at one place: a fixed 30 fps step shows the trail as a game running at 30 fps leaves it.
	 */
	constexpr double ExhaustWarm = 3.0;
	constexpr double ExhaustSpacing = 0.2;
	constexpr double ExhaustStep = 1.0 / 30.0;

	/**
	 * The fire scenarios (#219): seconds for the turret to come round before the shot, and the game's speed while the hull
	 * rocks, so the pictures, one every FireSpacing real seconds, run through the whole rock until it has settled.
	 */
	constexpr double FireWarm = 5.0;
	constexpr float FireTimeDilation = 0.25f;

	struct FScenario
	{
		FString Name;
		FView View;
		/** Set up the unit, its place and its command; false when it cannot. */
		TFunction<bool(ARTSUnit&)> Setup;
		/**
		 * Real seconds the gear runs before the first picture, game seconds with a FixedStep, so a fast machine does not drive
		 * the unit off its ground first; or, with Until, at most this long until it holds.
		 */
		double Warm = 2.0;
		TFunction<bool(const ARTSUnit&)> Until;
		/** Run just before the first picture, such as a new command to brake on. */
		TFunction<void(ARTSUnit&)> AtFirst;
		/** Real seconds between pictures, game seconds with a FixedStep; zero takes them as fast as they come. */
		double Spacing = 0.0;
		/**
		 * The game's speed while the pictures are taken: slowed, so that from one picture to the next the links move a share of
		 * a link rather than several, which a sequence could not tell from going backward.
		 */
		float TimeDilation = 0.1f;
		/**
		 * The camera stays where the view puts it from the unit's place at the start, so the unit drives through the picture
		 * at full speed: what the temporal upscaler makes of moving parts shows only so, never with a camera riding along (#243).
		 */
		bool bFixedCamera = false;
		/** Shows the previous frame reprojected onto this one: a part whose motion vectors are wrong is drawn twice. */
		bool bReprojection = false;
		/** Game seconds each frame steps while the scenario runs, whatever the frame took; zero steps by real time. */
		double FixedStep = 0.0;
		/** At night (FRTSShotNight): the sun and the sky dimmed, the exposure held at the daylight one. */
		bool bNight = false;
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
			if (!Unit.IsValid() && World->HasBegunPlay() && Controller && World->GetNetMode() != NM_Client)
			{
				// A unit the map does not hold, such as the flak truck (#336): one of team 0's, on the open strip.
				Unit = SpawnUnit();
			}
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
			if (!Unit->HasAuthority() || !Unit->GetUnitData()->Locomotion.IsVehicle())
			{
				return Finish(TEXT("the unit must be a vehicle, on the server"));
			}
			bTracked = Unit->GetUnitMovement()->GetLimits().IsTracked();
			for (TActorIterator<AStaticMeshActor> It(World.Get()); It; ++It)
			{
				if (It->GetActorLabel() == BlockLabel)
				{
					Block = It->GetComponentsBoundingBox();
				}
				else if (It->GetActorLabel().StartsWith(StripPrefix))
				{
					Strips.Emplace(It->GetActorLabel().RightChop(FCString::Strlen(StripPrefix)).ToLower(), It->GetComponentsBoundingBox());
				}
			}
			Strips.Sort([](const TPair<FString, FBox>& A, const TPair<FString, FBox>& B) { return A.Key < B.Key; });
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

		/** A unit of the data UnitName (in /Game/Data/Units) for team 0 at DriveStart, on the ground; null without such data. */
		ARTSUnit* SpawnUnit() const
		{
			URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, *FString::Printf(TEXT("/Game/Data/Units/%s.%s"), *UnitName, *UnitName));
			const FTransform Transform(DriveStart);
			ARTSUnit* Spawned = Data ? World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
			if (!Spawned)
			{
				return nullptr;
			}
			Spawned->InitFromData(Data, 0);
			Spawned->FinishSpawning(Transform);
			Spawned->SetActorLocation(Transform.GetLocation() + FVector(0.f, 0.f, Spawned->GetGroundOffset()));
			return Spawned;
		}

		void Place(ARTSUnit& InUnit, const FVector& Location, float Yaw)
		{
			InUnit.SetActorLocationAndRotation(FVector(Location.X, Location.Y, InUnit.GetActorLocation().Z), FRotator(0.f, Yaw, 0.f));
		}

		void BuildScenarios()
		{
			auto DriveFrom = [this](const FVector& Start, RTSLocomotion::FCommand Command)
			{
				return [this, Start, Command](ARTSUnit& InUnit)
				{
					Place(InUnit, Start, 0.f);
					InUnit.GetUnitMovement()->SetCommandOverride(Command);
					InUnit.SetAimOverride({});
					return true;
				};
			};
			auto Drive = [DriveFrom](RTSLocomotion::FCommand Command) { return DriveFrom(DriveStart, Command); };
			auto Tracks = [](float Left, float Right)
			{
				return [Left, Right](const RTSLocomotion::FLimits& Limits) { return RTSLocomotion::FCommand::FromTracks(Limits, Left, Right); };
			};
			const RTSLocomotion::FLimits& Limits = Unit->GetUnitMovement()->GetLimits();
			// The dust behind the tracks or wheels (#224) at the game's own speed, as it rises and settles: none standing, a
			// little at a slow drive, a trail at full speed, and some from a pivot on the spot; stepped like the exhaust. On the
			// map's dirt, then along each strip of other ground (#336).
			TArray<TPair<FString, FVector>> Grounds = { { TEXT("dirt"), DriveStart } };
			for (const TPair<FString, FBox>& Strip : Strips)
			{
				Grounds.Emplace(Strip.Key, FVector(Strip.Value.Min.X + StripRunIn, Strip.Value.GetCenter().Y, 0.f));
			}
			for (const TPair<FString, FVector>& Ground : Grounds)
			{
				const FString Prefix = FString::Printf(TEXT("dust_%s_"), *Ground.Key);
				TArray<FScenario> Dusts = {
					{ Prefix + TEXT("idle"), DustView, DriveFrom(Ground.Value, RTSLocomotion::FCommand()), ExhaustWarm, nullptr, nullptr, ExhaustSpacing, 1.f },
					{ Prefix + TEXT("slow"), DustView, DriveFrom(Ground.Value, Ahead(400.f)), ExhaustWarm, nullptr, nullptr, ExhaustSpacing, 1.f },
					{ Prefix + TEXT("fast"), DustView, DriveFrom(Ground.Value, Ahead(1.e5f)), ExhaustWarm, nullptr, nullptr, ExhaustSpacing, 1.f },
				};
				if (bTracked)
				{
					Dusts.Add({ Prefix + TEXT("pivot"), DustView, DriveFrom(Ground.Value, Tracks(-250.f, 250.f)(Limits)), ExhaustWarm, nullptr, nullptr,
						ExhaustSpacing, 1.f });
				}
				for (FScenario& Dust : Dusts)
				{
					Dust.FixedStep = ExhaustStep;
					Scenarios.Add(MoveTemp(Dust));
				}
			}
			if (!bTracked)
			{
				return;
			}
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
			// Standing and firing (#219): the hull rocks up on the gun's side, and the tracks and wheels have to ride with it. The
			// gun turned toward the camera beside the left track, ahead for the idler, behind for the sprocket.
			const double FireSpacing = Unit->GetUnitData()->GunRecoil.HullRockSeconds / FireTimeDilation / Frames;
			for (const TTuple<const TCHAR*, FView, float>& Fire : { MakeTuple(TEXT("fire_side"), SideView, -90.f),
				MakeTuple(TEXT("fire_idler"), IdlerView, 0.f), MakeTuple(TEXT("fire_sprocket"), SprocketView, 180.f) })
			{
				const float Yaw = Fire.Get<2>();
				auto Stand = Drive(RTSLocomotion::FCommand());
				Scenarios.Add({ Fire.Get<0>(), Fire.Get<1>(), [Stand, Yaw](ARTSUnit& InUnit)
				{
					Stand(InUnit);
					InUnit.SetAimOverride(FVector2D(Yaw, 0.f));
					return InUnit.GetUnitData()->GunRecoil.HullRockDegrees > 0.f;
				}, FireWarm, nullptr, [](ARTSUnit& InUnit) { InUnit.PlayGunRecoil(); }, FireSpacing, FireTimeDilation });
			}
			// The engine's exhaust (#222) at the game's own speed, as the puffs drift and fade: standing, setting off from a stand
			// for full speed (the pictures run through 2.4 s of the climb, to about 1,000 cm/s), and driving on at a steady speed;
			// by day, then by night, where its colours must hold too (#325).
			for (const bool bNight : { false, true })
			{
				const TCHAR* Suffix = bNight ? TEXT("_night") : TEXT("");
				FScenario Exhausts[] = {
					{ FString(TEXT("exhaust_idle")) + Suffix, ExhaustView, Drive(RTSLocomotion::FCommand()), ExhaustWarm, nullptr, nullptr,
						ExhaustSpacing, 1.f },
					{ FString(TEXT("exhaust_accelerate")) + Suffix, ExhaustView, Drive(RTSLocomotion::FCommand()), ExhaustWarm, nullptr,
						[](ARTSUnit& InUnit) { InUnit.GetUnitMovement()->SetCommandOverride(Ahead(1.e5f)); }, ExhaustSpacing, 1.f },
					{ FString(TEXT("exhaust_moving")) + Suffix, ExhaustView, Drive(Ahead(300.f)), ExhaustWarm, nullptr, nullptr, ExhaustSpacing,
						1.f },
				};
				for (FScenario& Exhaust : Exhausts)
				{
					Exhaust.FixedStep = ExhaustStep;
					Exhaust.bNight = bNight;
					Scenarios.Add(MoveTemp(Exhaust));
				}
			}
			// The body with its drive (#297): squatting back as it pulls away from a stand at full power, diving under full
			// brakes from speed, leaning out as it swings from a straight run into a turn, seen square from behind, and the
			// turret trailing a quarter turn and settling on its aim, seen from above.
			Scenarios.Add({ TEXT("body_pull_away"), SideView, Drive(RTSLocomotion::FCommand()), 1.0, nullptr,
				[](ARTSUnit& InUnit) { InUnit.GetUnitMovement()->SetCommandOverride(Ahead(1.e5f)); },
				BodySpacing / BodyTimeDilation, BodyTimeDilation });
			Scenarios.Add({ TEXT("body_brake"), SideView, Drive(Ahead(1.e5f)), 3.0, nullptr,
				[](ARTSUnit& InUnit) { InUnit.GetUnitMovement()->SetCommandOverride(RTSLocomotion::FCommand()); },
				BodySpacing / BodyTimeDilation, BodyTimeDilation });
			Scenarios.Add({ TEXT("body_turn"), RearView, Drive(Ahead(600.f)), 2.0, nullptr,
				[Turn = Tracks(700.f, 450.f)(Limits)](ARTSUnit& InUnit) { InUnit.GetUnitMovement()->SetCommandOverride(Turn); },
				BodySpacing / BodyTimeDilation, BodyTimeDilation });
			Scenarios.Add({ TEXT("body_turret"), TopView, [Stand = Drive(RTSLocomotion::FCommand())](ARTSUnit& InUnit)
			{
				Stand(InUnit);
				InUnit.SetAimOverride(FVector2D(0.f, 0.f));
				return InUnit.GetUnitData()->BodyMotion.TurretLagDegrees > 0.f;
			}, 3.0, nullptr, [](ARTSUnit& InUnit) { InUnit.SetAimOverride(FVector2D(90.f, 0.f)); },
				TurretSpacing / BodyTimeDilation, BodyTimeDilation });
			// Driving past a still camera at full speed, pictures as fast as they come, without letting a frame settle (#243).
			for (const bool bReprojection : { false, true })
			{
				for (const TPair<const TCHAR*, FView>& View : { TPair<const TCHAR*, FView>(TEXT("motion_side"), MotionSideView),
					TPair<const TCHAR*, FView>(TEXT("motion_rts"), MotionRTSView) })
				{
					FScenario& Motion = Scenarios.Add_GetRef({ FString(View.Key) + (bReprojection ? TEXT("_reprojection") : TEXT("")),
						View.Value, Drive(Ahead(300.f)), 0.5, nullptr, nullptr, 0.0, 1.f });
					Motion.bFixedCamera = true;
					Motion.bReprojection = bReprojection;
				}
			}
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
			const FVector Eye = Scenario.View.Eye - FVector(0.f, 0.f, GroundOffset);
			const FRotator Look = (Scenario.View.LookAt - Scenario.View.Eye).Rotation();
			if (Scenario.bFixedCamera)
			{
				Camera->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
				const FTransform& Start = Unit->GetActorTransform();
				Camera->SetActorLocationAndRotation(Start.TransformPosition(Eye), Start.TransformRotation(Look.Quaternion()));
			}
			else
			{
				Camera->AttachToActor(Unit.Get(), FAttachmentTransformRules::KeepRelativeTransform);
				Camera->SetActorRelativeLocation(Eye);
				Camera->SetActorRelativeRotation(Look);
			}
			GEngine->Exec(World.Get(), Scenario.bReprojection ? TEXT("ShowFlag.VisualizeReprojection 1") : TEXT("ShowFlag.VisualizeReprojection 0"));
			FApp::SetUseFixedTimeStep(Scenario.FixedStep > 0.0);
			if (Scenario.FixedStep > 0.0)
			{
				FApp::SetFixedDeltaTime(Scenario.FixedStep);
			}
			Camera->GetCameraComponent()->SetFieldOfView(Scenario.View.FOV);
			Night.Set(*World, Scenario.bNight);
			Step = EStep::Warm;
			StepStart = FPlatformTime::Seconds();
			WarmStartGame = World->GetTimeSeconds();
			Shot = 0;
			return true;
		}

		bool Warm()
		{
			const FScenario& Scenario = Scenarios[Index];
			const double Elapsed = Scenario.FixedStep > 0.0 ? World->GetTimeSeconds() - WarmStartGame : FPlatformTime::Seconds() - StepStart;
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
			const double Now = Scenario.FixedStep > 0.0 ? World->GetTimeSeconds() : FPlatformTime::Seconds();
			if (Scenario.Spacing > 0.0 && Now - LastShotTime < Scenario.Spacing && Shot > 0)
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
			LastShotTime = Now;
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
				// What the material draws now, from the link rate it was sent (#216).
				Unit->GetTrackMaterialPhase(TrackParts[Side], Sent[Side]);
			}
			// The hull's rock from a shot (#219) and its sway with the drive (#297): its front and its right side lifted, in degrees.
			const FVector2D Rock = Unit->GetHullRock();
			const FVector2D Sway = Unit->GetHullSway();
			// The engine's exhaust state (#222).
			const URTSExhaustSubsystem* Exhaust = World->GetSubsystem<URTSExhaustSubsystem>();
			const ERTSExhaustState ExhaustState = Exhaust ? Exhaust->GetState(*Unit) : ERTSExhaustState::Idle;
			// The dust's rate from the motion and the ground under it (#224).
			const URTSDustSubsystem* Dust = World->GetSubsystem<URTSDustSubsystem>();
			const float DustRate = URTSDustSubsystem::GetRate(Unit->GetUnitData()->Dust, Motion.ForwardSpeed, Motion.YawRate);
			UE_LOG(LogWorldDivide, Display,
				TEXT("rts.Debug.TrackShots: %s %02d: %s, %.0f cm/s, %.1f deg/s; travel L %.1f R %.1f cm; phase L %.4f (material %.4f) R %.4f (material %.4f); rock front %.3f right %.3f deg; sway front %.3f right %.3f deg; turret %.1f lag %.2f deg; exhaust %s; dust %.1f/s on surface %d, %d emitters"),
				*Name, Shot, *UEnum::GetValueAsString(Motion.Status), Motion.ForwardSpeed, Motion.YawRate, Motion.LeftTravel, Motion.RightTravel,
				Phase[0], Sent[0], Phase[1], Sent[1], Rock.X, Rock.Y, Sway.X, Sway.Y, Unit->GetTurretYaw(), Unit->GetTurretLag(), *UEnum::GetDisplayValueAsText(ExhaustState).ToString(),
				DustRate, Dust ? static_cast<int32>(Dust->GetSurface(*Unit)) : 0, Dust ? Dust->GetActiveEmitters() : 0);
		}

		bool Finish(const FString& Error)
		{
			if (Unit.IsValid())
			{
				Unit->GetUnitMovement()->SetCommandOverride({});
				Unit->SetAimOverride({});
			}
			FApp::SetUseFixedTimeStep(false);
			if (World.IsValid())
			{
				World->GetWorldSettings()->SetTimeDilation(1.f);
				GEngine->Exec(World.Get(), TEXT("ShowFlag.VisualizeReprojection 0"));
				Night.Set(*World, false);
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
		/** The map's strips of other ground by kind, lower case, and their boxes. */
		TArray<TPair<FString, FBox>> Strips;
		int32 TrackParts[2] = { INDEX_NONE, INDEX_NONE };
		/** The actor's height over the ground it stands on, which the views are given from. */
		float GroundOffset = 0.f;
		TArray<FScenario> Scenarios;
		/** Whether the unit runs on tracks; a wheeled one takes the dust scenarios only. */
		bool bTracked = false;
		/** The night of the night scenarios. */
		FRTSShotNight Night;
		int32 Index = 0;
		int32 Shot = 0;
		EStep Step = EStep::WaitForUnit;
		double StepStart = 0.0;
		/** The game's time as the warm-up began, for the scenarios on a fixed step. */
		double WarmStartGame = 0.0;
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
