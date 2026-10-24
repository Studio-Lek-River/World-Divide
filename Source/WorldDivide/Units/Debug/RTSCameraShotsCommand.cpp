#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Buildings/RTSBuilding.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSPlayerController.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"
#include "WorldDivide.h"

/**
 * rts.Debug.CameraShots: the camera comparison sheet's pictures (ue.ps1 -Task camerashots, issue #137). On L_TestMap it
 * builds a base around Team0's headquarters and stands a mixed formation in front of it, then pictures, for the camera
 * before the real-scale retune and the camera as it is now, a single vehicle, the formation, the base and the battlefield
 * at the closest, default and farthest zoom at 1920x1080, the base at 2560x1440, and the base at 3440x1440, there also
 * with the horizontal field of view kept instead of the vertical. The pictures go to
 * Saved/CameraShots/<size>_<profile>_<scene>_<zoom>.png, without the HUD; metrics.csv beside them has, per picture, the
 * field of view, the arm, the pitch, the ground across the middle of the screen and from its top to its bottom, and how
 * wide an M20, a tank and a soldier standing broadside at the view's centre appear, and the frame, game thread and GPU time
 * over the second before the picture (#158: River Crossing's vegetation against the frame budget). With -RTSCameraShotsQuit on the
 * command line the game quits when done, with exit code 1 on failure.
 *
 * On WD_RiverCrossing_01 (ue.ps1 -Task camerashots -Map /Game/Maps/WD_RiverCrossing_01, #295) it stages only an M20 by
 * the map's south-west corner, so the edge shots show where the map ends, and removes it when done (#339); it pictures the camera as it is now over
 * the landscape at 1920x1080: the west hill, the north bridge, the west base turned 45 degrees, the map's south-west
 * corner straight and turned 45 degrees, and a jump past the middle of its west edge turned 45 degrees, which the camera's
 * clamp stops with the screen's centre on the map, each at the closest, default and farthest zoom.
 */
namespace RTSCameraShots
{
	const TCHAR* QuitParam = TEXT("RTSCameraShotsQuit");

	constexpr double ReadyTimeout = 60.0;
	constexpr double CaptureTimeout = 10.0;
	constexpr double ResizeTimeout = 10.0;
	/** Real seconds for the staged scene to settle, and for the camera's lag to catch up with a new view. */
	constexpr double StageSeconds = 3.0;
	constexpr double SettleSeconds = 2.0;
	/** The settle's last part, over which the frame times are averaged once the view has stopped moving. */
	constexpr double MeasureSeconds = 1.0;

	const FIntPoint Screen1080(1920, 1080);
	const FIntPoint Screen1440(2560, 1440);
	const FIntPoint ScreenUltrawide(3440, 1440);

	/** Team0's start spot on L_TestMap, where its headquarters stands, facing +X. */
	const FVector BaseCentre(-18000.f, 0.f, 0.f);
	/** The base around the headquarters, 16 to 24 m buildings with room for the builders and the army. */
	const TPair<const TCHAR*, FVector2D> BaseBuildings[] = {
		{ TEXT("DA_TestBarracks"), FVector2D(-18000.f, -4200.f) },
		{ TEXT("DA_TestSupplyDepot"), FVector2D(-18000.f, 4300.f) },
		{ TEXT("DA_TestPowerPlant"), FVector2D(-21800.f, -3600.f) },
		{ TEXT("DA_TestVehicleFactory"), FVector2D(-21800.f, 3600.f) },
		{ TEXT("DA_TestTower"), FVector2D(-13600.f, -5200.f) },
		{ TEXT("DA_TestTower"), FVector2D(-13600.f, 5200.f) },
	};
	/** The formation ahead of the base: the vehicles in a line, the infantry in two ranks behind them. */
	const FVector FormationCentre(-11500.f, 0.f, 0.f);
	const TCHAR* FormationVehicles[] = { TEXT("DA_TestM20Guardian"), TEXT("DA_TestTank"), TEXT("DA_TestAbrams"), TEXT("DA_TestFlakTruck") };
	constexpr float VehicleSpacing = 1200.f;
	constexpr int32 InfantryCount = 10;
	constexpr float InfantrySpacing = 300.f;
	constexpr float InfantryBehind = 1500.f;
	/** Turned to show the camera, which looks along +X, a front three-quarter. */
	constexpr float FormationYaw = 145.f;
	/** The battlefield view: past the formation towards the middle of the map. */
	const FVector BattlefieldCentre(-6000.f, 0.f, 0.f);

	/**
	 * River Crossing's scenes (rivercrossing_greybox.py, metres there): where, and the camera's yaw. A scene with a marker
	 * gets a marker unit at its focus, or just inside the border when the focus lies past it: on the border the picture
	 * shows where the map ends rather than plain grass, on a paint layer whether a unit stays distinct against it (#156).
	 */
	struct FLandscapeScene
	{
		const TCHAR* Name;
		FVector2D Focus;
		float Yaw;
		bool bMarker = false;
	};
	const FLandscapeScene RiverCrossingScenes[] = {
		{ TEXT("hills"), FVector2D(-35000.f, -65000.f), 0.f },
		{ TEXT("bridge"), FVector2D(0.f, 30000.f), 0.f },
		{ TEXT("base_yaw45"), FVector2D(-72000.f, -22000.f), 45.f },
		// The junction of the west main road and the village ring road (#383).
		{ TEXT("roads"), FVector2D(-15700.f, -30000.f), 0.f },
		// Each village bank from its own side (#157): the west bank's main street by the church square, the east bank's by
		// the barn, each looking toward the river.
		{ TEXT("village_west"), FVector2D(-15700.f, 0.f), 0.f },
		{ TEXT("village_east"), FVector2D(15700.f, 0.f), 180.f },
		// A power-line pole on each bank, the focus 7 m past its foot so the closest zoom frames its head and each cable end on
		// its insulator (#445).
		{ TEXT("powerline_west"), FVector2D(-15600.f, 1000.f), 0.f },
		{ TEXT("powerline_east"), FVector2D(15600.f, -1000.f), 180.f },
		{ TEXT("edge"), FVector2D(-100000.f, -100000.f), 0.f, true },
		{ TEXT("edge_yaw45"), FVector2D(-100000.f, -100000.f), 45.f, true },
		{ TEXT("past_west_yaw45"), FVector2D(-160000.f, 0.f), 45.f, true },
		// A unit on each paint layer it can drive on (#156): a pasture, a meadow and a fallow field, the mud of the west
		// bank, the farm track's gravel and the main road's asphalt.
		{ TEXT("layer_grass"), FVector2D(-27800.f, -74600.f), 0.f, true },
		{ TEXT("layer_meadow"), FVector2D(-34800.f, -88600.f), 0.f, true },
		{ TEXT("layer_dirt"), FVector2D(-46000.f, -79500.f), 0.f, true },
		{ TEXT("layer_mud"), FVector2D(-9700.f, -15000.f), 0.f, true },
		{ TEXT("layer_gravel"), FVector2D(-66500.f, 1000.f), 0.f, true },
		{ TEXT("layer_asphalt"), FVector2D(-53000.f, -24500.f), 0.f, true },
	};
	/** The playable ground's half-size (rivercrossing_greybox.py's PLAYABLE_HALF), and how far inside it the markers stand. */
	constexpr float PlayableHalf = 100000.f;
	constexpr float BorderMarkerInset = 3000.f;
	const TCHAR* SceneMarker = TEXT("DA_TestM20Guardian");

	bool IsRiverCrossing(const UWorld& World)
	{
		return World.GetMapName().Contains(TEXT("RiverCrossing"));
	}

	/** The units measured on every picture, standing broadside at the view's centre. */
	const TCHAR* MeasuredUnits[] = { TEXT("DA_TestM20Guardian"), TEXT("DA_TestTank"), TEXT("DA_TestInfantry") };

	URTSUnitData* LoadUnitData(const TCHAR* Name)
	{
		return LoadObject<URTSUnitData>(nullptr, *FString::Printf(TEXT("/Game/Data/Units/%s.%s"), Name, Name));
	}

	URTSBuildingData* LoadBuildingData(const TCHAR* Name)
	{
		return LoadObject<URTSBuildingData>(nullptr, *FString::Printf(TEXT("/Game/Data/Buildings/%s.%s"), Name, Name));
	}

	struct FProfile
	{
		FString Name;
		FRTSCameraTuning Tuning;
	};

	/**
	 * The camera as it was before the real-scale retune (#137), the camera as the pawn has it now, and the same with the
	 * horizontal field of view kept, for the wide screens.
	 */
	TArray<FProfile> MakeProfiles(const FRTSCameraTuning& Final)
	{
		FRTSCameraTuning Previous;
		Previous.FieldOfView = 90.f;
		Previous.PanSpeed = 3000.f;
		Previous.DefaultZoom = 3000.f;
		Previous.MinZoom = 175.f;
		Previous.MaxZoom = 7000.f;
		Previous.ZoomStepFraction = Final.ZoomStepFraction;
		Previous.TiltStartZoom = 3000.f;
		Previous.EdgeMargin = 500.f;
		Previous.FallbackMapArea = FBox2D(FVector2D(-15000.0), FVector2D(15000.0));

		FRTSCameraTuning FinalHorizontal = Final;
		FinalHorizontal.bKeepVerticalFOV = false;
		return { { TEXT("Previous"), Previous }, { TEXT("Final"), Final }, { TEXT("FinalHorFOV"), FinalHorizontal } };
	}

	enum class EZoom : uint8
	{
		Min,
		Default,
		Max,
	};

	const TCHAR* GetZoomName(EZoom Zoom)
	{
		return Zoom == EZoom::Min ? TEXT("min") : Zoom == EZoom::Default ? TEXT("default") : TEXT("max");
	}

	class FCameraShotsRun : public TSharedFromThis<FCameraShotsRun>
	{
	public:
		explicit FCameraShotsRun(UWorld& InWorld)
			: World(&InWorld)
		{
		}

		static bool IsRunning() { return Active.IsValid(); }

		static void Start(const TSharedRef<FCameraShotsRun>& Run)
		{
			Active = Run;
			Run->StepStart = FPlatformTime::Seconds();
			Run->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddSP(Run, &FCameraShotsRun::OnCaptured);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FCameraShotsRun::Tick));
		}

		static FString GetShotDir()
		{
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CameraShots"));
		}

	private:
		struct FShot
		{
			FIntPoint Size;
			int32 Profile = 0;
			FString Scene;
			FVector Focus = FVector::ZeroVector;
			EZoom Zoom = EZoom::Default;
			float Yaw = 0.f;

			FString GetName(const TArray<FProfile>& Profiles) const
			{
				return FString::Printf(TEXT("%dx%d_%s_%s_%s"), Size.X, Size.Y, *Profiles[Profile].Name, *Scene, GetZoomName(Zoom));
			}
		};

		enum class EStep : uint8
		{
			WaitForGame,
			Stage,
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
			++StepFrames;
			// The cursor rests wherever the offscreen window put it; at an edge it would scroll the camera off the view.
			if (ARTSPlayerController* Controller = GetController(); Controller && GetViewportSize().X > 0)
			{
				Controller->SetMouseLocation(GetViewportSize().X / 2, GetViewportSize().Y / 2);
			}
			switch (Step)
			{
			case EStep::WaitForGame:
				return WaitForGame();
			case EStep::Stage:
				return FPlatformTime::Seconds() - StepStart >= StageSeconds ? StartShot() : true;
			case EStep::Resize:
				return Resize();
			case EStep::Settle:
				if (FPlatformTime::Seconds() - StepStart >= SettleSeconds - MeasureSeconds)
				{
					++Timing.Frames;
					Timing.FrameMs += FApp::GetDeltaTime() * 1000.0;
					Timing.GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
					Timing.GPUMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
				}
				if (FPlatformTime::Seconds() - StepStart >= SettleSeconds)
				{
					RequestShot();
				}
				return true;
			case EStep::Capture:
				return Capture();
			}
			return true;
		}

		void GoTo(EStep Next)
		{
			if (Next == EStep::Settle)
			{
				Timing = FTiming();
			}
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

		FIntPoint GetViewportSize() const
		{
			const UGameViewportClient* Client = World->GetGameViewport();
			return Client && Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		}

		bool WaitForGame()
		{
			const ARTSPlayerController* Controller = GetController();
			if (!World->HasBegunPlay() || !GetCameraPawn())
			{
				return FPlatformTime::Seconds() - StepStart > ReadyTimeout ? Finish(TEXT("no camera in time")) : true;
			}
			if (!Controller->HasAuthority())
			{
				return Finish(TEXT("the game is a client; the scene is staged on the server"));
			}
			OwnTuning = GetCameraPawn()->GetTuning();
			Profiles = MakeProfiles(*OwnTuning);
			if (IsRiverCrossing(*World))
			{
				// The landscape is the scene: only the camera as it is now.
				Profiles = { { TEXT("Final"), *OwnTuning } };
				MeasureUnits();
				BuildRiverCrossingShots();
				StageSceneMarkers();
				GoTo(EStep::Stage);
				return true;
			}
			if (!StageScene())
			{
				return Finish(TEXT("could not stage the scene"));
			}
			BuildShots();
			GoTo(EStep::Stage);
			return true;
		}

		float GetGroundZ(const FVector2D& Location) const
		{
			FHitResult Hit;
			const FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSCameraShotsGround), false);
			return World->LineTraceSingleByChannel(Hit, FVector(Location, 10000.f), FVector(Location, -10000.f), ECC_WorldStatic, Params)
				? Hit.ImpactPoint.Z : 0.f;
		}

		ARTSUnit* SpawnUnit(URTSUnitData* Data, const FVector2D& Location)
		{
			const FTransform Transform(FRotator(0.f, FormationYaw, 0.f), FVector(Location, GetGroundZ(Location)));
			ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Unit)
			{
				return nullptr;
			}
			Unit->InitFromData(Data, 0);
			Unit->FinishSpawning(Transform);
			Unit->SetActorLocation(Transform.GetLocation() + FVector(0.f, 0.f, Unit->GetGroundOffset()));
			return Unit;
		}

		/** The base's buildings, built at once, and the formation ahead of it, all Team0's. */
		bool StageScene()
		{
			for (const TPair<const TCHAR*, FVector2D>& Entry : BaseBuildings)
			{
				URTSBuildingData* Data = LoadBuildingData(Entry.Key);
				const FTransform Transform(FVector(Entry.Value, GetGroundZ(Entry.Value)));
				ARTSBuilding* Building = Data ? World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr,
					nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
				if (!Building)
				{
					UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.CameraShots: could not place %s"), Entry.Key);
					return false;
				}
				Building->InitFromData(Data, 0);
				Building->FinishSpawning(Transform);
				Building->UpdateConstruction(Data->BuildTime / ARTSPlayerState::LowPowerProductionRate);
			}

			const float FirstVehicle = -VehicleSpacing * (UE_ARRAY_COUNT(FormationVehicles) - 1) / 2.f;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(FormationVehicles); ++Index)
			{
				URTSUnitData* Data = LoadUnitData(FormationVehicles[Index]);
				ARTSUnit* Unit = Data ? SpawnUnit(Data, FVector2D(FormationCentre) + FVector2D(0.f, FirstVehicle + Index * VehicleSpacing)) : nullptr;
				if (!Unit)
				{
					UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.CameraShots: could not place %s"), FormationVehicles[Index]);
					return false;
				}
				if (Index == 0)
				{
					Vehicle = Unit->GetActorLocation();
					Vehicle.Z = GetGroundZ(FVector2D(Vehicle));
				}
			}
			URTSUnitData* Infantry = LoadUnitData(TEXT("DA_TestInfantry"));
			const int32 PerRank = InfantryCount / 2;
			for (int32 Index = 0; Infantry && Index < InfantryCount; ++Index)
			{
				const float Side = (Index % PerRank - (PerRank - 1) / 2.f) * InfantrySpacing;
				const float Back = InfantryBehind + Index / PerRank * InfantrySpacing;
				SpawnUnit(Infantry, FVector2D(FormationCentre) + FVector2D(-Back, Side));
			}

			MeasureUnits();
			return true;
		}

		void MeasureUnits()
		{
			for (const TCHAR* Name : MeasuredUnits)
			{
				if (const URTSUnitData* Data = LoadUnitData(Name))
				{
					Measured.Add({ Name, RTSUnitShape::FromData(*Data).Hull });
				}
			}
		}

		void BuildRiverCrossingShots()
		{
			for (const FLandscapeScene& Scene : RiverCrossingScenes)
			{
				for (const EZoom Zoom : { EZoom::Min, EZoom::Default, EZoom::Max })
				{
					Shots.Add({ Screen1080, 0, Scene.Name, FVector(Scene.Focus, GetGroundZ(Scene.Focus)), Zoom, Scene.Yaw });
				}
			}
		}

		/** A marker unit at each marked scene's focus, inside the border; staged at runtime, the map is not changed. */
		void StageSceneMarkers()
		{
			URTSUnitData* Data = LoadUnitData(SceneMarker);
			if (!Data)
			{
				UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.CameraShots: %s did not load; the marked shots show no marker unit"), SceneMarker);
				return;
			}
			TSet<FVector2D> Placed;
			for (const FLandscapeScene& Scene : RiverCrossingScenes)
			{
				if (!Scene.bMarker || Placed.Contains(Scene.Focus))
				{
					continue;
				}
				Placed.Add(Scene.Focus);
				// Just inside the border nearest the focus, which may lie past it.
				const float Inside = PlayableHalf - BorderMarkerInset;
				const FVector2D Location(FMath::Clamp(Scene.Focus.X, -Inside, Inside), FMath::Clamp(Scene.Focus.Y, -Inside, Inside));
				if (ARTSUnit* Marker = SpawnUnit(Data, Location))
				{
					Markers.Add(Scene.Focus, Marker);
				}
				else
				{
					UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.CameraShots: could not place the marker unit %s"), SceneMarker);
				}
			}
		}

		void BuildShots()
		{
			const TPair<const TCHAR*, FVector> Scenes[] = { { TEXT("vehicle"), Vehicle }, { TEXT("formation"), FormationCentre },
				{ TEXT("base"), BaseCentre }, { TEXT("battlefield"), BattlefieldCentre } };
			for (int32 Profile = 0; Profile < 2; ++Profile)
			{
				for (const TPair<const TCHAR*, FVector>& Scene : Scenes)
				{
					for (const EZoom Zoom : { EZoom::Min, EZoom::Default, EZoom::Max })
					{
						Shots.Add({ Screen1080, Profile, Scene.Key, Scene.Value, Zoom });
					}
				}
			}
			for (int32 Profile = 0; Profile < 2; ++Profile)
			{
				Shots.Add({ Screen1440, Profile, TEXT("base"), BaseCentre, EZoom::Default });
			}
			for (int32 Profile = 0; Profile < 3; ++Profile)
			{
				for (const EZoom Zoom : { EZoom::Default, EZoom::Max })
				{
					Shots.Add({ ScreenUltrawide, Profile, TEXT("base"), BaseCentre, Zoom });
				}
			}
		}

		/** Sets up the next shot, or finishes after the last; false once finished, so the ticker drops the run. */
		bool StartShot()
		{
			if (ShotIndex >= Shots.Num())
			{
				return Finish(FString());
			}
			GoTo(EStep::Resize);
			return Resize();
		}

		/** PIE draws into the editor's viewport, whose size the game cannot change: there the shots take its size. */
		bool Resize()
		{
			const FShot& Shot = Shots[ShotIndex];
			if (World->WorldType == EWorldType::PIE || GetViewportSize() == Shot.Size)
			{
				SetUpView();
				GoTo(EStep::Settle);
				return true;
			}
			if (StepFrames <= 1)
			{
				FSystemResolution::RequestResolutionChange(Shot.Size.X, Shot.Size.Y, EWindowMode::Windowed);
			}
			if (FPlatformTime::Seconds() - StepStart > ResizeTimeout)
			{
				const FIntPoint Size = GetViewportSize();
				return Finish(FString::Printf(TEXT("the screen stayed %dx%d instead of %dx%d"), Size.X, Size.Y, Shot.Size.X, Shot.Size.Y));
			}
			return true;
		}

		void SetUpView()
		{
			const FShot& Shot = Shots[ShotIndex];
			ARTSCameraPawn* Camera = GetCameraPawn();
			Camera->ApplyTuning(Profiles[Shot.Profile].Tuning);
			Camera->SetYawNow(Shot.Yaw);
			// Zoom before the jump: the jump keeps the view inside the pan area at the zoom it finds, and the previous shot's
			// farthest zoom would hold a near focus a few hundred metres from the map's edge (#465).
			const float Zoom = Shot.Zoom == EZoom::Min ? Camera->GetMinZoom()
				: Shot.Zoom == EZoom::Default ? Camera->GetDefaultZoom() : Camera->GetMaxZoom();
			Camera->SetZoomNow(Zoom);
			Camera->JumpTo(Shot.Focus);
			GetController()->SetViewTarget(Camera);
			// The clamp keeps a border focus's screen centre a zoom-dependent way inside the map: the marker goes to where the
			// view actually centres (#467).
			const TWeakObjectPtr<ARTSUnit>* Marker = Markers.Find(FVector2D(Shot.Focus));
			if (Marker && Marker->IsValid())
			{
				const FVector2D Centre(Camera->GetActorLocation());
				(*Marker)->SetActorLocation(FVector(Centre, GetGroundZ(Centre) + (*Marker)->GetGroundOffset()));
			}
		}

		/** Where the screen point's view ray meets the ground plane at Z; unset when it looks above the horizon. */
		TOptional<FVector> GetGroundPoint(const FVector2D& ScreenPoint, float Z) const
		{
			FVector Origin;
			FVector Direction;
			if (!GetController()->DeprojectScreenPositionToWorld(ScreenPoint.X, ScreenPoint.Y, Origin, Direction) || Direction.Z > -KINDA_SMALL_NUMBER)
			{
				return {};
			}
			return Origin + Direction * ((Z - Origin.Z) / Direction.Z);
		}

		/**
		 * How wide Hull appears standing broadside at Centre, its length across the screen, in pixels; negative off screen.
		 * Broadside to the camera's yaw, so the turned shots measure the same as the straight ones.
		 */
		float GetBroadsideWidth(const FBox& Hull, const FVector& Centre, float Yaw) const
		{
			// The hull's length runs along the camera's right axis, across the screen; its width along the camera's forward.
			const FRotator YawOnly(0.f, Yaw, 0.f);
			const FVector Across = YawOnly.RotateVector(FVector::RightVector) * Hull.GetExtent().X;
			const FVector Deep = YawOnly.RotateVector(FVector::ForwardVector) * Hull.GetExtent().Y;
			const FVector Up(0.f, 0.f, Hull.GetExtent().Z);
			const FVector Middle = Centre + Up;
			FBox2D Rect(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point = Middle + Deep * (Corner & 1 ? 1.f : -1.f) + Across * (Corner & 2 ? 1.f : -1.f) + Up * (Corner & 4 ? 1.f : -1.f);
				FVector2D OnScreen;
				if (!GetController()->ProjectWorldLocationToScreen(Point, OnScreen, /*bPlayerViewportRelative*/ true))
				{
					return -1.f;
				}
				Rect += OnScreen;
			}
			return Rect.GetSize().X;
		}

		void RequestShot()
		{
			const FShot& Shot = Shots[ShotIndex];
			const ARTSCameraPawn* Camera = GetCameraPawn();
			const FIntPoint Size = GetViewportSize();
			const float Z = Shot.Focus.Z;
			const FVector2D Middle(Size.X / 2.f, Size.Y / 2.f);
			const TOptional<FVector> Left = GetGroundPoint(FVector2D(0.f, Middle.Y), Z);
			const TOptional<FVector> Right = GetGroundPoint(FVector2D(Size.X, Middle.Y), Z);
			const TOptional<FVector> Top = GetGroundPoint(FVector2D(Middle.X, 0.f), Z);
			const TOptional<FVector> Bottom = GetGroundPoint(FVector2D(Middle.X, Size.Y), Z);
			// A ground point the screen's view ray never reaches is written as -1: the view reaches the horizon.
			const float Across = Left && Right ? FVector::Dist(*Left, *Right) / 100.f : -1.f;
			const float Deep = Top && Bottom ? FVector::Dist(*Top, *Bottom) / 100.f : -1.f;
			const FVector Ground = Camera->GetActorLocation() * FVector(1.f, 1.f, 0.f) + FVector(0.f, 0.f, Z);

			FString Row = FString::Printf(TEXT("%s,%dx%d,%s,%s,%s,%.1f,%.1f,%.0f,%.1f,%.1f,%.1f,%.0f"), *Shot.GetName(Profiles),
				Size.X, Size.Y, *Profiles[Shot.Profile].Name, *Shot.Scene, GetZoomName(Shot.Zoom),
				Profiles[Shot.Profile].Tuning.FieldOfView, Camera->GetCameraComponent()->FieldOfView, Camera->GetZoom(),
				Camera->GetPitchForZoom(Camera->GetZoom()), Across, Deep,
				Camera->GetTuning().PanSpeed * Camera->GetZoom() / Camera->GetDefaultZoom());
			const double Frames = FMath::Max(1, Timing.Frames);
			Row += FString::Printf(TEXT(",%.2f,%.2f,%.2f"), Timing.FrameMs / Frames, Timing.GameMs / Frames, Timing.GPUMs / Frames);
			for (const TPair<FString, FBox>& Unit : Measured)
			{
				const float Width = GetBroadsideWidth(Unit.Value, Ground, Camera->GetActorRotation().Yaw);
				Row += FString::Printf(TEXT(",%.0f,%.2f"), Width, Width >= 0.f ? 100.f * Width / Size.X : -1.f);
			}
			Rows.Add(Row);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.CameraShots: %s"), *Row);

			Pixels.Reset();
			GoTo(EStep::Capture);
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
			if (Pixels.IsEmpty())
			{
				return FPlatformTime::Seconds() - StepStart > CaptureTimeout ? Finish(TEXT("no screenshot arrived")) : true;
			}
			const FString Path = FPaths::Combine(GetShotDir(), Shots[ShotIndex].GetName(Profiles) + TEXT(".png"));
			for (FColor& Pixel : Pixels)
			{
				Pixel.A = 255;
			}
			if (!FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), PixelsSize.X, PixelsSize.Y)))
			{
				return Finish(FString::Printf(TEXT("could not write %s"), *Path));
			}
			++Written;
			++ShotIndex;
			return StartShot();
		}

		/** Writes the metrics and gives the player their own camera back; false, so the ticker drops the run. */
		bool Finish(const FString& Error)
		{
			FString Csv = TEXT("shot,screen,profile,scene,zoom,fov_h16x9,camera_fov,arm_cm,pitch,across_m,deep_m,pan_cm_s,frame_ms,game_ms,gpu_ms");
			for (const TPair<FString, FBox>& Unit : Measured)
			{
				Csv += FString::Printf(TEXT(",%s_px,%s_pct"), *Unit.Key, *Unit.Key);
			}
			Csv += LINE_TERMINATOR + FString::Join(Rows, LINE_TERMINATOR) + LINE_TERMINATOR;
			const FString CsvPath = FPaths::Combine(GetShotDir(), TEXT("metrics.csv"));
			FString Problem = Error;
			if (Problem.IsEmpty() && !FFileHelper::SaveStringToFile(Csv, *CsvPath))
			{
				Problem = FString::Printf(TEXT("could not write %s"), *CsvPath);
			}
			if (World.IsValid() && GetCameraPawn() && OwnTuning)
			{
				GetCameraPawn()->ApplyTuning(*OwnTuning);
			}
			// The border markers were only for the pictures; a playtest goes on without them.
			for (const TPair<FVector2D, TWeakObjectPtr<ARTSUnit>>& Marker : Markers)
			{
				if (Marker.Value.IsValid())
				{
					Marker.Value->Destroy();
				}
			}
			Markers.Reset();
			if (Problem.IsEmpty())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.CameraShots: wrote %d pictures and %s"), Written,
					*FPaths::ConvertRelativePathToFull(CsvPath));
			}
			else
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.Debug.CameraShots failed: %s"), *Problem);
			}
			UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
			if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, Problem.IsEmpty() ? 0 : 1);
			}
			// The ticker holds the last reference until it drops the run.
			Active.Reset();
			return false;
		}

		static TSharedPtr<FCameraShotsRun> Active;

		TWeakObjectPtr<UWorld> World;
		/** The pawn's own settings, given back when done. */
		TOptional<FRTSCameraTuning> OwnTuning;
		TArray<FProfile> Profiles;
		TArray<FShot> Shots;
		/** The units measured on each picture and their hulls. */
		TArray<TPair<FString, FBox>> Measured;
		FVector Vehicle = FVector::ZeroVector;
		/** River Crossing's scene markers by their scene's focus, removed when done. */
		TMap<FVector2D, TWeakObjectPtr<ARTSUnit>> Markers;
		TArray<FString> Rows;
		/** The frame times summed over the current shot's measuring second. */
		struct FTiming
		{
			int32 Frames = 0;
			double FrameMs = 0.0;
			double GameMs = 0.0;
			double GPUMs = 0.0;
		};
		FTiming Timing;
		int32 ShotIndex = 0;
		EStep Step = EStep::WaitForGame;
		int32 StepFrames = 0;
		double StepStart = 0.0;
		int32 Written = 0;

		TArray<FColor> Pixels;
		FIntPoint PixelsSize = FIntPoint::ZeroValue;
		FDelegateHandle CaptureHandle;
	};

	TSharedPtr<FCameraShotsRun> FCameraShotsRun::Active;

	void RunCameraShotsCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.CameraShots needs a running game"));
			return;
		}
		if (FCameraShotsRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.CameraShots is already running"));
			return;
		}
		Output.Logf(TEXT("rts.Debug.CameraShots: to %s"), *FPaths::ConvertRelativePathToFull(FCameraShotsRun::GetShotDir()));
		FCameraShotsRun::Start(MakeShared<FCameraShotsRun>(*World));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CameraShotsCommand(
		TEXT("rts.Debug.CameraShots"),
		TEXT("rts.Debug.CameraShots: stages a base and a formation on L_TestMap and saves the camera comparison pictures and ")
		TEXT("metrics (the camera before the real-scale retune and now, at the closest, default and farthest zoom) to Saved/CameraShots."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunCameraShotsCommand));
}

#endif
