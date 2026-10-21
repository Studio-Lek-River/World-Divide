#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSShotSubsystem.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/LightComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/DirectionalLight.h"
#include "Containers/Ticker.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NiagaraComponent.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSPlayerController.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Units/RTSAvoidanceSubsystem.h"
#include "Units/RTSBarrelClearance.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"
#include "Units/Debug/RTSShotNight.h"
#include "Units/Debug/RTSUnitLOD.h"
#include "WorldDivide.h"

/**
 * Console commands for inspecting a vehicle in a running game:
 *
 * rts.Debug.AimTurret <Yaw> <Pitch> | off: the selected units hold their turret at Yaw and their gun at Pitch (degrees,
 * relative to the hull) instead of aiming, or aim again with off.
 *
 * rts.Debug.UnitShapes [1|0]: draws every unit's shapes in every game world: the collision box (red), the selection
 * bounds (yellow), the footprint on the ground (green), the navmesh clearance its centre keeps from obstacles (cyan), the
 * actor origin (white) and the barrel (green when clear, orange while it is kept out of a nearby obstacle, red when in one).
 *
 * rts.Debug.UnitShots <UnitData> [StressCount]: pictures of the first unit whose data asset is named UnitData, as the
 * player's RTS camera sees it at its default zoom, zoomed fully in and fully out, and from a close inspection camera, then
 * with its turret turned and gun raised, then picked with the cursor's trace, selected and driving off on a move order
 * issued as the player issues it (the pick, the distance driven and the wheels' roll are logged), then with its shapes
 * drawn after driving head-on into Block_North and into a vehicle factory placed for it, along Block_North and round its
 * corner (the hull's overlap with each is logged, and the barrel's, now and at most on the way), then the unit's kind once
 * for each of four teams side by side at the default and the closest zoom and under a low sun (#150). The head- and rear
 * lights (#223) are taken by day and at night from the RTS camera at its default zoom (vehicle_lights_<day|night>). With
 * StressCount, that many more of the unit stand in a grid and a last picture
 * shows them with stat unit and stat rhi; the frame time, game thread time, avoidance pass, draw calls and triangles are
 * logged. With a third argument "lods" (ue.ps1 -LODs; #149) it takes the LOD sheet instead: the unit at each LOD forced
 * (r.ForceLOD) from the side, front, rear and front three-quarter, then the RTS camera at a zoom inside each LOD, then the
 * RTS camera's lens just inside and just outside each switch distance, then the team row at each LOD; every shot logs the
 * LOD each of the unit's meshes is drawn at (RTSUnitLOD), into the folder LODs. The pictures go to
 * Saved/UnitShots/<UnitData>/<shot>.png. With -RTSUnitShotsQuit on the command line (ue.ps1 -Task unitshots) the game
 * quits when done, with exit code 1 on failure.
 */
// Named, not anonymous: in a unity build it shares a translation unit with rts.UI.Screenshot, which uses the same names.
namespace RTSUnitDebugCommands
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
	/** More real seconds for the lights' night picture: the day/night policy looks at the sun about once a second (#223). */
	constexpr double LightsSettleSeconds = 2.0;
	constexpr int32 MeasureFrames = 120;

	/** The inspection camera: in front of the unit and to its left, a little above, with a narrow lens. */
	const FVector CloseUpOffset(950.f, -650.f, 380.f);
	const FVector CloseUpLookAt(0.f, 0.f, 150.f);
	constexpr float CloseUpFOV = 45.f;

	/** The RTS camera's focus for its closest zoom: this far behind the unit along the view, so the unit fills the frame. */
	constexpr float ZoomInStandOff = 1000.f;

	/** The turned turret for the turret shot. */
	const FVector2D TurretShotAim(60.f, 10.f);
	/**
	 * The muzzle smoke's pictures (#221): real seconds after the shot for the puff leaving the barrel, and then for the turret
	 * to turn most of the way from TurretShotAim back to the front, leaving the puff behind.
	 */
	constexpr double SmokeLeavingSeconds = 0.4;
	constexpr double SmokeTurnedSeconds = 1.0;
	/** The close muzzle smoke pictures look from in front of the unit, this far and this many degrees up, across the turned gun. */
	constexpr float SmokeViewDistance = 2200.f;
	constexpr float SmokeViewElevation = 12.f;
	/** How slowly the world runs while a muzzle flash is held for its picture (#220): as still as the engine allows. */
	constexpr float FlashHoldDilation = 0.0001f;

	/** The moments after the shot the firing is held at for its pictures (#294), in seconds. */
	constexpr float MuzzleFlashHolds[] = { 0.f, 0.03f, 0.08f, 0.15f };

	/** The step a held firing's Niagara systems are advanced to its moment by. */
	constexpr float FlashHoldStep = 1.f / 240.f;
	/**
	 * Seconds into a shot when the hull has rocked back furthest (#218): the peak of FRTSRecoilSpring, at acos(r) / Swing for
	 * its damping ratio r, a share of the settle time of about a sixth at the M20's 0.6.
	 */
	float GetHullRockPeakSeconds(const FRTSGunRecoil& Recoil)
	{
		const float Ratio = FMath::Clamp(Recoil.HullRockDamping, 0.05f, 0.95f);
		return FMath::Acos(Ratio) * Ratio / (4.f * FMath::Sqrt(1.f - FMath::Square(Ratio))) * Recoil.HullRockSeconds;
	}

	/**
	 * The rock's views (#219): from the front and from the right side, low, so the hull's lift shows against the ground, with
	 * the gun at 0, 90 and 180 degrees. The map's other units stand off the unit's left.
	 */
	const TPair<const TCHAR*, FVector> RockViews[] = {
		{ TEXT("front"), FVector(1.f, 0.f, 0.f) },
		{ TEXT("side"), FVector(0.f, 1.f, 0.f) },
	};
	constexpr float RockGunYaws[] = { 0.f, 90.f, 180.f };
	constexpr float RockViewDistance = 1100.f;
	constexpr float RockViewElevation = 4.f;

	/** The drive shot: the unit is picked along a ray from here relative to it, as from the RTS camera, and sent this far ahead. */
	const FVector PickFrom(-1700.f, 0.f, 2500.f);
	constexpr float DriveDistance = 2500.f;
	constexpr double DriveSeconds = 2.5;

	/**
	 * The stress grid, as rts.Debug.Bench lays its grid out: units facing +X, each its hull's length plus StressRowGap apart
	 * along X and its width plus StressColumnGap apart along Y, about square overall, centred on L_TestMap's empty north strip.
	 */
	constexpr float StressRowGap = 300.f;
	constexpr float StressColumnGap = 250.f;
	const FVector StressCentre(0.f, 18600.f, 0.f);

	/** The team shots (#150): this many of the unit on consecutive teams, in a row across the stress grid's centre. */
	constexpr int32 TeamRowCount = 4;
	/** The inspection camera on the row: this far back from its centre and this high. */
	constexpr float TeamCloseUpDistance = 3200.f;
	constexpr float TeamCloseUpHeight = 900.f;
	/** The sun's pitch for the low-light team shot: evening light, long shadows. */
	constexpr float LowSunPitch = -10.f;

	/**
	 * The LOD sheet (#149). The forced LODs: the inspection lens this far from the middle of the hull, from this far above
	 * the horizontal, at each view (unit space). The switches: vehicle_art.LOD_DISTANCES, each shot this share inside and
	 * outside it, through the RTS camera's lens (60 degrees across at 16:9, the vertical field of view kept), from the front
	 * three-quarter. The RTS zooms: one inside each LOD, the default and the farthest included.
	 */
	constexpr int32 LODCount = 4;
	constexpr float LODViewDistance = 1800.f;
	constexpr float LODViewElevation = 20.f;
	const TPair<const TCHAR*, FVector> LODViews[] = {
		{ TEXT("side"), FVector(0.f, -1.f, 0.f) },
		{ TEXT("front"), FVector(1.f, 0.f, 0.f) },
		{ TEXT("rear"), FVector(-1.f, 0.f, 0.f) },
		{ TEXT("three_quarter"), FVector(1.f, -0.7f, 0.f) },
	};
	const float LODSwitchDistances[] = { 3500.f, 11000.f, 26000.f };
	constexpr float LODSwitchMargin = 0.04f;
	constexpr float RTSLensFOV = 60.f;
	constexpr float LODCloseZoom = 2500.f;
	constexpr float LODTacticalZoom = 7000.f;

	/** The obstacle shots: the unit starts this far out from the obstacle's edge and is ordered into its middle. */
	const TCHAR* ObstacleBlockLabel = TEXT("Block_North");
	constexpr float ObstacleRunUp = 1500.f;
	constexpr double ObstacleSeconds = 6.0;
	/** The courses along and round the block keep the hull this far off its faces, and take this long. */
	constexpr float ObstacleSideGap = 100.f;
	constexpr double ObstacleCourseSeconds = 14.0;
	/** The building shot's vehicle factory, placed on the empty south strip when the shots are planned. */
	const TCHAR* ObstacleBuildingPath = TEXT("/Game/Data/Buildings/DA_TestVehicleFactory.DA_TestVehicleFactory");
	const FVector ObstacleBuildingSpot(-6000.f, -19500.f, 0.f);

	/** Lines a little above the ground, so the footprint and the clearance do not sink into it. */
	constexpr float ShapeLift = 5.f;
	constexpr float ShapeThickness = 3.f;
	constexpr int32 CircleSegments = 32;

	void DrawGroundCircle(const UWorld& World, const FVector& Center, float Radius, const FColor& Color)
	{
		DrawDebugCircle(&World, Center, Radius, CircleSegments, Color, false, -1.f, SDPG_Foreground, ShapeThickness,
			FVector::ForwardVector, FVector::RightVector, false);
	}

	void DrawUnitShapes(const ARTSUnit& Unit)
	{
		const UWorld& World = *Unit.GetWorld();
		const FTransform Transform = Unit.GetActorTransform();
		if (const UBoxComponent* Box = Cast<UBoxComponent>(Unit.GetRootComponent()))
		{
			DrawDebugBox(&World, Box->GetComponentLocation(), Box->GetScaledBoxExtent(), Box->GetComponentQuat(), FColor::Red,
				false, -1.f, SDPG_Foreground, ShapeThickness);
		}
		const FBox Selection = Unit.GetSelectionBounds();
		DrawDebugBox(&World, Transform.TransformPosition(Selection.GetCenter()), Selection.GetExtent(), Transform.GetRotation(),
			FColor::Yellow, false, -1.f, SDPG_Foreground, 1.f);

		const FVector Ground = Unit.GetNavAgentLocation() + FVector(0.f, 0.f, ShapeLift);
		const FRTSFootprint Footprint = Unit.GetFootprint();
		const FVector Lift(0.f, 0.f, Ground.Z - Footprint.Center.Z);
		const FVector Front = Footprint.Center + Lift + Footprint.Axis * Footprint.HalfSegment;
		const FVector Back = Footprint.Center + Lift - Footprint.Axis * Footprint.HalfSegment;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Footprint.Axis) * Footprint.Radius;
		DrawGroundCircle(World, Front, Footprint.Radius, FColor::Green);
		DrawGroundCircle(World, Back, Footprint.Radius, FColor::Green);
		DrawDebugLine(&World, Front + Side, Back + Side, FColor::Green, false, -1.f, SDPG_Foreground, ShapeThickness);
		DrawDebugLine(&World, Front - Side, Back - Side, FColor::Green, false, -1.f, SDPG_Foreground, ShapeThickness);

		const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		const ARecastNavMesh* NavMesh = NavSystem
			? Cast<ARecastNavMesh>(NavSystem->GetNavDataForProps(Unit.GetNavAgentPropertiesRef(), Unit.GetNavAgentLocation())) : nullptr;
		if (NavMesh)
		{
			DrawGroundCircle(World, Ground, NavMesh->AgentRadius, FColor::Cyan);
		}
		DrawDebugPoint(&World, Unit.GetActorLocation(), 10.f, FColor::White, false, -1.f, SDPG_Foreground);

		FVector BarrelStart;
		FVector BarrelEnd;
		if (Unit.GetShape().Barrel && Unit.GetBarrelSegment(BarrelStart, BarrelEnd))
		{
			const float Radius = Unit.GetShape().Barrel->Radius;
			const FColor Color = !RTSBarrelClearance::IsClear(World, BarrelStart, BarrelEnd, Radius) ? FColor::Red
				: Unit.HasAuthority() && Unit.IsBarrelNearObstacle() ? FColor::Orange : FColor::Green;
			DrawDebugCapsule(&World, (BarrelStart + BarrelEnd) / 2.f, FVector::Dist(BarrelStart, BarrelEnd) / 2.f + Radius, Radius,
				FRotationMatrix::MakeFromZ(BarrelEnd - BarrelStart).ToQuat(), Color, false, -1.f, SDPG_Foreground, 1.f);
		}
	}

	FTSTicker::FDelegateHandle UnitShapesTicker;

	bool TickUnitShapes(float)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				for (TActorIterator<ARTSUnit> It(World); It; ++It)
				{
					DrawUnitShapes(**It);
				}
			}
		}
		return true;
	}

	void ShowUnitShapes(bool bShow)
	{
		if (bShow == UnitShapesTicker.IsValid())
		{
			return;
		}
		if (bShow)
		{
			UnitShapesTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickUnitShapes));
		}
		else
		{
			FTSTicker::GetCoreTicker().RemoveTicker(UnitShapesTicker);
			UnitShapesTicker.Reset();
		}
	}

	/**
	 * Where the unit's collision box and Obstacle overlap on the ground, as world X by Y in cm; zero when they do not. The
	 * obstacle shots stand the unit along an axis, so its box's bounds are the box.
	 */
	FVector2D GetHullOverlap(const ARTSUnit& Unit, const FBox& Obstacle)
	{
		const FBox Hull = Unit.GetShape().Hull.TransformBy(Unit.GetActorTransform());
		const FVector2D Min = FVector2D::Max(FVector2D(Hull.Min), FVector2D(Obstacle.Min));
		const FVector2D Max = FVector2D::Min(FVector2D(Hull.Max), FVector2D(Obstacle.Max));
		// Apart on either axis is no overlap at all: a hull beside the block shares its extent along the other axis only.
		const FVector2D Overlap = Max - Min;
		return Overlap.X > 0.f && Overlap.Y > 0.f ? Overlap : FVector2D::ZeroVector;
	}

	/** How deep the unit's barrel reaches into Obstacle, in cm; zero without a barrel or when it stays out. */
	float GetBarrelOverlap(const ARTSUnit& Unit, const FBox& Obstacle)
	{
		FVector Start;
		FVector End;
		return Unit.GetShape().Barrel && Unit.GetBarrelSegment(Start, End)
			? RTSBarrelClearance::GetPenetration(Start, End, Unit.GetShape().Barrel->Radius, Obstacle) : 0.f;
	}

	/** True when the unit's barrel reaches over Obstacle seen from above, all of it higher than its roof. */
	bool IsBarrelOverRoof(const ARTSUnit& Unit, const FBox& Obstacle)
	{
		FVector Start;
		FVector End;
		if (!Unit.GetShape().Barrel || !Unit.GetBarrelSegment(Start, End))
		{
			return false;
		}
		const float Radius = Unit.GetShape().Barrel->Radius;
		const FBox Above(FVector(Obstacle.Min.X, Obstacle.Min.Y, Obstacle.Max.Z), FVector(Obstacle.Max.X, Obstacle.Max.Y, Obstacle.Max.Z + 10000.f));
		return RTSBarrelClearance::GetPenetration(Start, End, Radius, Above) > 0.f && GetBarrelOverlap(Unit, Obstacle) <= 0.f;
	}

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
		FUnitShotsRun(UWorld& InWorld, const FString& InUnitName, int32 InStressCount, bool bInLODs)
			: World(&InWorld), UnitName(InUnitName), StressCount(InStressCount), bLODs(bInLODs)
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
				if (Obstacle && Unit.IsValid())
				{
					MaxBarrelOverlap = FMath::Max(MaxBarrelOverlap, GetBarrelOverlap(*Unit, *Obstacle));
					bBarrelWentOverRoof |= IsBarrelOverRoof(*Unit, *Obstacle);
				}
				if (FPlatformTime::Seconds() - StepStart >= Shots[ShotIndex].Settle)
				{
					GoTo(Shots[ShotIndex].bShowStats ? EStep::Measure : EStep::Capture);
					if (Step == EStep::Capture)
					{
						RequestShot();
					}
					else
					{
						// The measured frames as a region of an Insights capture (ue.ps1 -Task unitshots -Trace).
						TRACE_BEGIN_REGION(*Shots[ShotIndex].Name);
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
			if (bLODs)
			{
				AddLODShots();
				return;
			}
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
			// The gun's recoil (#218) from the same view: at rest, held at full stroke, and run out again after a real stroke.
			Shots.Add({ TEXT("recoil_rest"), [](FUnitShotsRun& Run)
			{
				if (Run.Unit->GetGunRecoilStroke() <= 0.f)
				{
					UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.UnitShots: %s has no gun recoil"), *Run.UnitName);
				}
				Run.Unit->SetGunRecoilHold({});
				return true;
			} });
			Shots.Add({ TEXT("recoil_full"), [](FUnitShotsRun& Run)
			{
				Run.Unit->SetGunRecoilHold(Run.Unit->GetUnitData()->GunRecoil.RecoilSeconds);
				return true;
			} });
			Shots.Add({ TEXT("recoil_returned"), [](FUnitShotsRun& Run)
			{
				Run.Unit->SetGunRecoilHold({});
				Run.Unit->PlayGunRecoil();
				return true;
			}, SettleSeconds + Unit->GetUnitData()->GunRecoil.GetSettleSeconds() });
			// The gun's firing (#220, #294), turret turned and gun raised, held 0, 30, 80 and 150 ms after the shot: close up
			// and from the RTS camera, by day and at night.
			for (const bool bNight : { false, true })
			{
				for (const float HoldSeconds : MuzzleFlashHolds)
				{
					const FString Name = FString::Printf(TEXT("muzzle_flash_%s_%03.0f"), bNight ? TEXT("night") : TEXT("day"), HoldSeconds * 1000.f);
					// From in front with the gun at TurretShotAim, as the smoke's pictures, so the whole firing is in view.
					Shots.Add({ Name, [bNight, HoldSeconds](FUnitShotsRun& Run)
					{
						Run.SetNight(bNight);
						Run.Unit->SetAimOverride(TurretShotAim);
						return Run.UseLODCamera(FVector::ForwardVector, SmokeViewDistance, false, SmokeViewElevation)
							&& Run.HoldMuzzleFlash(HoldSeconds);
					}, HoldSeconds == MuzzleFlashHolds[0] ? TraverseSeconds : SettleSeconds });
					Shots.Add({ Name + TEXT("_rts"), [HoldSeconds](FUnitShotsRun& Run)
					{
						Run.UseRTSCamera(Run.GetCameraPawn()->GetDefaultZoom(), 0.f);
						return Run.HoldMuzzleFlash(HoldSeconds);
					} });
				}
			}
			// The head- and rear lights (#223) from the RTS camera at its default zoom, the turret to the front: the lamps dim by
			// day; at night they glow and light the ground white ahead and red behind. The day/night policy looks at the sun
			// about once a second, so these settle longer.
			for (const bool bNight : { false, true })
			{
				Shots.Add({ FString::Printf(TEXT("vehicle_lights_%s"), bNight ? TEXT("night") : TEXT("day")), [bNight](FUnitShotsRun& Run)
				{
					Run.ReleaseMuzzleFlash();
					Run.SetNight(bNight);
					Run.Unit->SetAimOverride(FVector2D::ZeroVector);
					Run.UseRTSCamera(Run.GetCameraPawn()->GetDefaultZoom(), 0.f);
					return true;
				}, bNight ? SettleSeconds + LightsSettleSeconds : TraverseSeconds });
			}
			// The muzzle smoke (#221) by day, the gun at TurretShotAim: from the RTS camera just after the shot, then from in
			// front just after another one and once the turret has turned back to the front, the puff left where it was fired.
			Shots.Add({ TEXT("muzzle_smoke_rts"), [](FUnitShotsRun& Run)
			{
				Run.ReleaseMuzzleFlash();
				Run.SetNight(false);
				Run.Unit->SetAimOverride(TurretShotAim);
				Run.UseRTSCamera(Run.GetCameraPawn()->GetDefaultZoom(), 0.f);
				return Run.FireGun();
			}, SmokeLeavingSeconds });
			Shots.Add({ TEXT("muzzle_smoke_leaving"), [](FUnitShotsRun& Run)
			{
				Run.World->GetSubsystem<URTSShotSubsystem>()->AdvanceEffects(10.f);
				return Run.UseLODCamera(FVector::ForwardVector, SmokeViewDistance, false, SmokeViewElevation) && Run.FireGun();
			}, SmokeLeavingSeconds });
			Shots.Add({ TEXT("muzzle_smoke_turned"), [](FUnitShotsRun& Run)
			{
				Run.Unit->SetAimOverride(FVector2D::ZeroVector);
				return true;
			}, SmokeTurnedSeconds });
			// The hull's rock (#219) from the front and the side, at rest and held where it has rocked back furthest, for each
			// way the gun points: the hull lifts on the gun's side and pivots on the edge across from it.
			for (const float Yaw : RockGunYaws)
			{
				for (const TPair<const TCHAR*, FVector>& View : RockViews)
				{
					const FString Suffix = FString::Printf(TEXT("%s_%03.0f"), View.Key, Yaw);
					const FVector Direction = View.Value;
					const bool bTurn = &View == &RockViews[0];
					Shots.Add({ TEXT("recoil_rest_") + Suffix, [Yaw, Direction](FUnitShotsRun& Run)
					{
						Run.ReleaseMuzzleFlash();
						Run.SetNight(false);
						Run.Unit->SetGunRecoilHold({});
						Run.Unit->SetAimOverride(FVector2D(Yaw, 0.f));
						return Run.UseLODCamera(Direction, RockViewDistance, false, RockViewElevation);
					}, bTurn ? TraverseSeconds : SettleSeconds });
					Shots.Add({ TEXT("recoil_rock_") + Suffix, [](FUnitShotsRun& Run)
					{
						Run.Unit->SetGunRecoilHold(GetHullRockPeakSeconds(Run.Unit->GetUnitData()->GunRecoil));
						return true;
					} });
				}
			}
			Shots.Add({ TEXT("driving"), [](FUnitShotsRun& Run)
			{
				Run.Unit->SetGunRecoilHold({});
				Run.Unit->SetAimOverride({});
				return Run.Drive();
			}, DriveSeconds, false, true });
			AddObstacleShots();
			AddTeamShots();
			// The same view at the default zoom with the stats on, before and after the stress grid, to compare.
			Shots.Add({ TEXT("stats_one"), [](FUnitShotsRun& Run)
			{
				Run.SetLowSun(false);
				Run.ClearTeamRow();
				ShowUnitShapes(false);
				Run.Obstacle.Reset();
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

		/** The LOD sheet (#149): see the command's description. */
		void AddLODShots()
		{
			for (int32 LOD = 0; LOD < LODCount; ++LOD)
			{
				for (const TPair<const TCHAR*, FVector>& View : LODViews)
				{
					Shots.Add({ FString::Printf(TEXT("lod%d_%s"), LOD, View.Key), [LOD, Direction = View.Value](FUnitShotsRun& Run)
					{
						Run.PoseUnit();
						Run.SetForcedLOD(LOD);
						return Run.UseLODCamera(Direction, LODViewDistance, false);
					} });
				}
			}
			const TPair<const TCHAR*, float> Zooms[] = {
				{ TEXT("auto_close"), LODCloseZoom }, { TEXT("auto_tactical"), LODTacticalZoom },
				{ TEXT("auto_default"), GetCameraPawn()->GetDefaultZoom() }, { TEXT("auto_max"), GetCameraPawn()->GetMaxZoom() },
			};
			for (const TPair<const TCHAR*, float>& Zoom : Zooms)
			{
				Shots.Add({ Zoom.Key, [Zoom = Zoom.Value](FUnitShotsRun& Run)
				{
					Run.SetForcedLOD(INDEX_NONE);
					Run.UseRTSCamera(Zoom, 0.f);
					return true;
				} });
			}
			for (int32 Switch = 0; Switch < UE_ARRAY_COUNT(LODSwitchDistances); ++Switch)
			{
				for (const float Side : { -1.f, 1.f })
				{
					const FString Name = FString::Printf(TEXT("switch%d_%s"), Switch + 1, Side < 0.f ? TEXT("before") : TEXT("after"));
					Shots.Add({ Name, [Distance = LODSwitchDistances[Switch] * (1.f + Side * LODSwitchMargin)](FUnitShotsRun& Run)
					{
						Run.SetForcedLOD(INDEX_NONE);
						return Run.UseLODCamera(LODViews[3].Value, Distance, true);
					} });
				}
			}
			for (int32 LOD = 0; LOD < LODCount; ++LOD)
			{
				Shots.Add({ FString::Printf(TEXT("teams_lod%d"), LOD), [LOD](FUnitShotsRun& Run)
				{
					if (Run.TeamRow.IsEmpty())
					{
						Run.SpawnTeamRow();
					}
					Run.SetForcedLOD(LOD);
					return Run.UseTeamRowCloseUp();
				} });
			}
		}

		/** r.ForceLOD at LOD, INDEX_NONE for the renderer's own choice; Finish puts it back. */
		void SetForcedLOD(int32 LOD)
		{
			IConsoleVariable* ForceLOD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ForceLOD"));
			if (!ForceLOD)
			{
				return;
			}
			if (!SavedForceLOD)
			{
				SavedForceLOD = ForceLOD->GetInt();
			}
			ForcedLOD = LOD;
			ForceLOD->Set(LOD, ECVF_SetByConsole);
		}

		/**
		 * The inspection camera Distance from the middle of the hull toward Direction (unit space), ElevationDegrees above the
		 * horizontal; with bRTSLens through the RTS camera's lens, which the LOD switches are measured with, else the close-up's.
		 */
		bool UseLODCamera(const FVector& Direction, float Distance, bool bRTSLens, float ElevationDegrees = LODViewElevation)
		{
			const UStaticMeshComponent* Hull = RTSUnitLOD::FindHull(*Unit);
			if (!Hull || !UseCloseUpCamera())
			{
				return false;
			}
			UCameraComponent* Lens = CloseUp->GetCameraComponent();
			Lens->SetFieldOfView(bRTSLens ? RTSLensFOV : CloseUpFOV);
			Lens->bOverrideAspectRatioAxisConstraint = bRTSLens;
			Lens->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV);
			const FVector Centre = Hull->Bounds.Origin;
			const FVector Across = Unit->GetActorTransform().TransformVectorNoScale(Direction).GetSafeNormal2D();
			const float Elevation = FMath::DegreesToRadians(ElevationDegrees);
			const FVector Eye = Centre + (Across * FMath::Cos(Elevation) + FVector::UpVector * FMath::Sin(Elevation)) * Distance;
			CloseUp->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
			return true;
		}

		/**
		 * For a LOD shot: the LOD the unit's meshes are drawn at from the player's view, and how far the hull is; for a team
		 * shot, the first of the row's.
		 */
		void LogPartLODs() const
		{
			FVector Origin;
			FMatrix Projection;
			const ARTSUnit* Subject = TeamRow.IsEmpty() ? Unit.Get() : TeamRow[0].Get();
			const UStaticMeshComponent* Hull = Subject ? RTSUnitLOD::FindHull(*Subject) : nullptr;
			if (!Hull || !RTSUnitLOD::GetPlayerView(*GetController(), Origin, Projection))
			{
				return;
			}
			const RTSUnitLOD::FPartLODs LODs = RTSUnitLOD::GetPartLODs(*Subject, Origin, Projection);
			UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: %s: forced LOD %s; the renderer's own choice: %s (%s), hull %.1f m away"),
				*Shots[ShotIndex].Name, ForcedLOD == INDEX_NONE ? TEXT("off") : *FString::FromInt(ForcedLOD), *LODs.ToString(),
				LODs.IsCoherent() ? TEXT("every part at the hull's") : TEXT("NOT every part at the hull's"),
				FVector::Dist(Hull->Bounds.Origin, Origin) / 100.f);
		}

		/**
		 * The unit's kind once for each of TeamRowCount teams side by side on the empty north strip (#150): every team colour
		 * on the same model at the default and the closest zoom, and again under a low sun. Their weapons are idled, since they
		 * stand among enemies.
		 */
		void AddTeamShots()
		{
			Shots.Add({ TEXT("teams_default"), [](FUnitShotsRun& Run)
			{
				ShowUnitShapes(false);
				Run.Obstacle.Reset();
				Run.SpawnTeamRow();
				Run.UseTeamRowCamera(Run.GetCameraPawn()->GetDefaultZoom(), 0.f);
				return true;
			} });
			Shots.Add({ TEXT("teams_close"), [](FUnitShotsRun& Run)
			{
				Run.UseTeamRowCamera(Run.GetCameraPawn()->GetMinZoom(), ZoomInStandOff);
				return true;
			} });
			Shots.Add({ TEXT("teams_closeup"), [](FUnitShotsRun& Run)
			{
				return Run.UseTeamRowCloseUp();
			} });
			Shots.Add({ TEXT("teams_low_sun"), [](FUnitShotsRun& Run)
			{
				Run.SetLowSun(true);
				return Run.UseTeamRowCloseUp();
			} });
		}

		void SpawnTeamRow()
		{
			const FRTSUnitShape Shape = RTSUnitShape::FromData(*Unit->GetUnitData());
			const float Spacing = 2.f * Shape.GetHalfWidth() + 2.f * StressColumnGap;
			const FRotator Facing(0.f, GetCameraPawn()->GetActorRotation().Yaw + 145.f, 0.f);
			for (int32 Index = 0; Index < TeamRowCount; ++Index)
			{
				const FVector Location = StressCentre + FVector(0.f, (Index - (TeamRowCount - 1) / 2.f) * Spacing, Unit->GetGroundOffset());
				const FTransform Transform(Facing, Location);
				ARTSUnit* Spawned = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Spawned->InitFromData(Unit->GetUnitData(), Unit->GetTeamId() + Index);
				Spawned->FinishSpawning(Transform);
				Spawned->GetWeaponComponent()->SetComponentTickEnabled(false);
				TeamRow.Add(Spawned);
			}
		}

		void ClearTeamRow()
		{
			for (const TWeakObjectPtr<ARTSUnit>& Member : TeamRow)
			{
				if (Member.IsValid())
				{
					Member->Destroy();
				}
			}
			TeamRow.Reset();
		}

		void UseTeamRowCamera(float Zoom, float StandOff)
		{
			ARTSCameraPawn* Camera = GetCameraPawn();
			GetController()->SetViewTarget(Camera);
			Camera->JumpTo(StressCentre - Camera->GetActorForwardVector() * StandOff);
			Camera->SetZoomNow(Zoom);
		}

		/** The inspection camera on the whole row, from the RTS camera's side. */
		bool UseTeamRowCloseUp()
		{
			if (!UseCloseUpCamera())
			{
				return false;
			}
			const FVector Back = -GetCameraPawn()->GetActorForwardVector().GetSafeNormal2D();
			const FVector Eye = StressCentre + Back * TeamCloseUpDistance + FVector(0.f, 0.f, TeamCloseUpHeight);
			CloseUp->SetActorLocationAndRotation(Eye, (StressCentre + CloseUpLookAt - Eye).Rotation());
			return true;
		}

		/** Lowers the level's sun to LowSunPitch, or puts it back where it was. */
		void SetLowSun(bool bLow)
		{
			ADirectionalLight* Sun = nullptr;
			for (TActorIterator<ADirectionalLight> It(World.Get()); It && !Sun; ++It)
			{
				Sun = *It;
			}
			if (!Sun)
			{
				return;
			}
			if (bLow && !SunRotation)
			{
				SunRotation = Sun->GetActorRotation();
				Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
				Sun->SetActorRotation(FRotator(LowSunPitch, SunRotation->Yaw, SunRotation->Roll));
			}
			else if (!bLow && SunRotation)
			{
				Sun->SetActorRotation(*SunRotation);
				SunRotation.Reset();
			}
		}

		/**
		 * Fires the unit's gun along its bore through the shot effects players see, without a target, moves the flash, its
		 * light and the smoke Seconds on, and holds the world nearly still there for the picture. False without a weapon.
		 */
		bool HoldMuzzleFlash(float Seconds = 0.f)
		{
			const URTSWeaponData* Weapon = Unit->GetUnitData()->Weapon;
			if (Weapon && !Weapon->MuzzleFlash)
			{
				UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.UnitShots: %s's weapon has no Niagara muzzle flash"), *UnitName);
			}
			ReleaseMuzzleFlash();
			// The last shot's effect plays out first, so only this one shows.
			World->GetSubsystem<URTSShotSubsystem>()->AdvanceEffects(10.f);
			if (!FireGun())
			{
				return false;
			}
			if (Seconds > 0.f)
			{
				URTSShotSubsystem* ShotSubsystem = World->GetSubsystem<URTSShotSubsystem>();
				const ARTSShotEffect* Effect = ShotSubsystem->GetLatestEffect();
				const int32 Steps = FMath::CeilToInt(Seconds / FlashHoldStep);
				for (UNiagaraComponent* System : { Effect ? Effect->GetNiagaraFlash() : nullptr, ShotSubsystem->GetLatestMuzzleSmoke(*Unit) })
				{
					if (System)
					{
						System->AdvanceSimulation(Steps, Seconds / Steps);
					}
				}
				ShotSubsystem->AdvanceEffects(Seconds);
			}
			World->GetWorldSettings()->SetTimeDilation(FlashHoldDilation);
			bHoldingFlash = true;
			return true;
		}

		/** Fires the unit's gun once along its bore through the shot effects players see, without a target. False without a weapon. */
		bool FireGun()
		{
			const URTSWeaponData* Weapon = Unit->GetUnitData()->Weapon;
			if (!Weapon)
			{
				UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.UnitShots: %s has no weapon to fire"), *UnitName);
				return false;
			}
			if (!Weapon->MuzzleSmoke)
			{
				UE_LOG(LogWorldDivide, Verbose, TEXT("rts.Debug.UnitShots: %s's weapon has no muzzle smoke"), *UnitName);
			}
			const FTransform Muzzle = Unit->GetMuzzleTransform();
			World->GetSubsystem<URTSShotSubsystem>()->PlayShot(*Unit, *Weapon, nullptr,
				Muzzle.GetLocation() + Muzzle.GetUnitAxis(EAxis::X) * Weapon->Range, 1.f, Muzzle);
			return true;
		}

		void ReleaseMuzzleFlash()
		{
			if (bHoldingFlash)
			{
				World->GetWorldSettings()->SetTimeDilation(1.f);
				bHoldingFlash = false;
			}
		}

		/** Dims the sun and the sky for the night pictures, or brings them back (FRTSShotNight). */
		void SetNight(bool bNight)
		{
			if (World.IsValid())
			{
				Night.Set(*World, bNight);
			}
		}

		/**
		 * The unit driving head-on into Block_North from the north, and into a vehicle factory from the camera's side, with its
		 * shapes drawn: what keeps a vehicle out of a solid obstacle. The factory is placed now, so its navmesh hole is built
		 * by the time its shot comes.
		 */
		void AddObstacleShots()
		{
			TOptional<FBox> Block;
#if WITH_EDITOR
			for (TActorIterator<AStaticMeshActor> It(World.Get()); It; ++It)
			{
				if (It->GetActorLabel() == ObstacleBlockLabel)
				{
					Block = It->GetComponentsBoundingBox(/*bNonColliding*/ false);
				}
			}
#endif
			if (Block)
			{
				Shots.Add({ TEXT("obstacle_block"), [Box = *Block](FUnitShotsRun& Run)
				{
					return Run.DriveInto(Box, ObstacleBlockLabel, FVector::RightVector);
				}, ObstacleSeconds });
				// Along the block's north face, the hull ObstacleSideGap off it, and round its north-east corner to the east face.
				Shots.Add({ TEXT("obstacle_along"), [Box = *Block](FUnitShotsRun& Run)
				{
					const float Side = Box.Max.Y + Run.Unit->GetShape().GetHalfWidth() + ObstacleSideGap;
					return Run.DrivePast(Box, ObstacleBlockLabel, FVector(Box.Min.X - ObstacleRunUp, Side, 0.f),
						FVector(Box.Max.X + ObstacleRunUp, Side, 0.f), FVector(Box.GetCenter().X, Box.Max.Y, 0.f));
				}, ObstacleCourseSeconds });
				Shots.Add({ TEXT("obstacle_corner"), [Box = *Block](FUnitShotsRun& Run)
				{
					const float Gap = Run.Unit->GetShape().GetHalfWidth() + ObstacleSideGap;
					return Run.DrivePast(Box, ObstacleBlockLabel, FVector(Box.Min.X - ObstacleRunUp, Box.Max.Y + Gap, 0.f),
						FVector(Box.Max.X + Gap, Box.Min.Y - ObstacleRunUp, 0.f), FVector(Box.Max.X, Box.Max.Y, 0.f));
				}, ObstacleCourseSeconds });
			}
			else
			{
				UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.UnitShots: no %s in this level, so no block shot"), ObstacleBlockLabel);
			}

			URTSBuildingData* FactoryData = LoadObject<URTSBuildingData>(nullptr, ObstacleBuildingPath);
			ARTSBuilding* Factory = FactoryData ? World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),
				FTransform(ObstacleBuildingSpot), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
			if (Factory)
			{
				Factory->InitFromData(FactoryData, Unit->GetTeamId());
				Factory->FinishSpawning(FTransform(ObstacleBuildingSpot));
				const FBox Box = Factory->GetComponentsBoundingBox(/*bNonColliding*/ false);
				Shots.Add({ TEXT("obstacle_building"), [Box, Name = FactoryData->GetName()](FUnitShotsRun& Run)
				{
					return Run.DriveInto(Box, Name, FVector::BackwardVector);
				}, ObstacleSeconds });
			}
			else
			{
				UE_LOG(LogWorldDivide, Warning, TEXT("rts.Debug.UnitShots: could not place %s, so no building shot"), ObstacleBuildingPath);
			}
		}

		/**
		 * Stands the unit ObstacleRunUp out from Box on its Approach side, facing it, orders it into Box's middle as the player
		 * would and frames the contact at the default zoom with the shapes drawn.
		 */
		bool DriveInto(const FBox& Box, const FString& Name, const FVector& Approach)
		{
			ShowUnitShapes(true);
			Unit->SetAimOverride({});
			FRTSCommand Stop;
			Stop.Type = ERTSCommandType::Stop;
			Unit->ExecuteCommand(Stop);

			const FVector Center(Box.GetCenter().X, Box.GetCenter().Y, 0.f);
			const float Edge = FMath::Abs(Approach.X) * Box.GetExtent().X + FMath::Abs(Approach.Y) * Box.GetExtent().Y;
			return DrivePast(Box, Name, Center + Approach * (Edge + ObstacleRunUp), Center, Center + Approach * Edge);
		}

		/**
		 * Stands the unit at Start (on the ground under it), facing Target, orders it to Target as the player would and frames
		 * Focus at the default zoom with the shapes drawn. The barrel's deepest reach into Box on the way is logged.
		 */
		bool DrivePast(const FBox& Box, const FString& Name, const FVector& Start, const FVector& Target, const FVector& Focus)
		{
			ShowUnitShapes(true);
			Unit->SetAimOverride({});
			FRTSCommand Stop;
			Stop.Type = ERTSCommandType::Stop;
			Unit->ExecuteCommand(Stop);

			const float Z = Unit->GetActorLocation().Z;
			Unit->SetActorLocationAndRotation(FVector(Start.X, Start.Y, Z), (Target - Start).GetSafeNormal2D().Rotation());

			FRTSCommand Move;
			Move.Type = ERTSCommandType::Move;
			Move.TargetLocation = FVector(Target.X, Target.Y, Unit->GetNavAgentLocation().Z);
			GetController()->Server_IssueCommand({ Unit.Get() }, Move);

			GetController()->SetViewTarget(GetCameraPawn());
			GetCameraPawn()->JumpTo(FVector(Focus.X, Focus.Y, Z));
			GetCameraPawn()->SetZoomNow(GetCameraPawn()->GetDefaultZoom());
			Obstacle = Box;
			ObstacleName = Name;
			MaxBarrelOverlap = 0.f;
			bBarrelWentOverRoof = false;
			return true;
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
			const FRTSUnitShape Shape = RTSUnitShape::FromData(*Unit->GetUnitData());
			const FVector2D Spacing(2.f * Shape.GetHalfLength() + StressRowGap, 2.f * Shape.GetHalfWidth() + StressColumnGap);
			const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(StressCount * Spacing.X / Spacing.Y)));
			const int32 Rows = FMath::DivideAndRoundUp(StressCount, Columns);
			const FVector2D Size((Rows - 1) * Spacing.X, (Columns - 1) * Spacing.Y);
			for (int32 Index = 0; Index < StressCount; ++Index)
			{
				const FVector Location = StressCentre + FVector(Index / Columns * Spacing.X - Size.X / 2.f,
					Index % Columns * Spacing.Y - Size.Y / 2.f, Unit->GetGroundOffset());
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
			TRACE_END_REGION(*Shots[ShotIndex].Name);
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
			if (Obstacle)
			{
				const FVector2D Overlap = GetHullOverlap(*Unit, *Obstacle);
				UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: %s: the hull overlaps %s by %.0f x %.0f cm"),
					*Shots[ShotIndex].Name, *ObstacleName, Overlap.X, Overlap.Y);
				if (Unit->GetShape().Barrel)
				{
					UE_LOG(LogWorldDivide, Display,
						TEXT("rts.Debug.UnitShots: %s: the barrel overlaps %s by %.0f cm now, %.0f cm at most on the way; %s; turret %.0f, gun %.0f degrees"),
						*Shots[ShotIndex].Name, *ObstacleName, GetBarrelOverlap(*Unit, *Obstacle), MaxBarrelOverlap,
						bBarrelWentOverRoof ? TEXT("it reached over the roof") : TEXT("it never reached over the roof"),
						Unit->GetTurretYaw(), Unit->GetGunPitch());
				}
				else
				{
					UE_LOG(LogWorldDivide, Display, TEXT("rts.Debug.UnitShots: %s: no separate barrel; a gun on the hull is part of the hull box"),
						*Shots[ShotIndex].Name);
				}
			}
			if (bLODs)
			{
				LogPartLODs();
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
			const FString Path = FPaths::Combine(GetShotDir(UnitName), bLODs ? TEXT("LODs") : TEXT(""), Shots[ShotIndex].Name + TEXT(".png"));
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
			ShowUnitShapes(false);
			if (World.IsValid())
			{
				SetLowSun(false);
				ReleaseMuzzleFlash();
				SetNight(false);
				ClearTeamRow();
				SetStats(false);
				if (SavedForceLOD)
				{
					SetForcedLOD(*SavedForceLOD);
				}
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
		/** The LOD sheet instead of the usual shots, the LOD forced for the current shot, and r.ForceLOD before the run. */
		bool bLODs = false;
		int32 ForcedLOD = INDEX_NONE;
		TOptional<int32> SavedForceLOD;
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
		/** The obstacle the current shot drives into, for the log, and how deep the barrel reached into it on the way. */
		TOptional<FBox> Obstacle;
		FString ObstacleName;
		float MaxBarrelOverlap = 0.f;
		bool bBarrelWentOverRoof = false;
		/** The team shots' units, and the sun's rotation while it is lowered. */
		TArray<TWeakObjectPtr<ARTSUnit>> TeamRow;
		TOptional<FRotator> SunRotation;
		/** The night for the night pictures, and whether a muzzle flash is held. */
		FRTSShotNight Night;
		bool bHoldingFlash = false;
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
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.UnitShots <UnitData> [StressCount] [lods] needs a running game"));
			return;
		}
		if (FUnitShotsRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.Debug.UnitShots is already running"));
			return;
		}
		const int32 StressCount = Args.Num() > 1 ? FMath::Max(FCString::Atoi(*Args[1]), 0) : 0;
		const bool bLODs = Args.Num() > 2 && Args[2] == TEXT("lods");
		Output.Logf(TEXT("rts.Debug.UnitShots: %s to %s"), *Args[0],
			*FPaths::ConvertRelativePathToFull(FUnitShotsRun::GetShotDir(Args[0])));
		FUnitShotsRun::Start(MakeShared<FUnitShotsRun>(*World, Args[0], StressCount, bLODs));
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

	void RunUnitShapesCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		const bool bShow = Args.IsEmpty() ? !UnitShapesTicker.IsValid() : FCString::Atoi(*Args[0]) != 0;
		ShowUnitShapes(bShow);
		Output.Logf(TEXT("rts.Debug.UnitShapes: %s"), bShow ? TEXT("on") : TEXT("off"));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice UnitShapesCommand(
		TEXT("rts.Debug.UnitShapes"),
		TEXT("rts.Debug.UnitShapes [1|0]: draws every unit's collision box (red), selection bounds (yellow), footprint (green), ")
		TEXT("navmesh clearance (cyan), origin (white) and barrel (green clear, orange near an obstacle, red in one); without an ")
		TEXT("argument it toggles."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunUnitShapesCommand));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice UnitShotsCommand(
		TEXT("rts.Debug.UnitShots"),
		TEXT("rts.Debug.UnitShots <UnitData> [StressCount] [lods]: saves views of the first unit with that data asset (RTS camera at ")
		TEXT("its default, closest and farthest zoom, a close-up, its turret turned) to Saved/UnitShots, and with StressCount a ")
		TEXT("grid of that many more with stat unit and stat rhi; with lods, its LOD sheet instead (#149)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunUnitShotsCommand));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice AimTurretCommand(
		TEXT("rts.Debug.AimTurret"),
		TEXT("rts.Debug.AimTurret <Yaw> <Pitch> | off: the selected units hold their turret and gun there (degrees, relative ")
		TEXT("to the hull), or aim again."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunAimTurretCommand));
}

#endif
