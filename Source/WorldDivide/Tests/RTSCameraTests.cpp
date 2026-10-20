#include "Buildings/RTSBuilding.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSCameraPawn.h"
#include "SceneView.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnitShape.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSCameraTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	/** Reads one of the camera's protected tuning values by name. */
	float GetSetting(const ARTSCameraPawn& Camera, const TCHAR* Name)
	{
		const FFloatProperty* Property = CastField<FFloatProperty>(ARTSCameraPawn::StaticClass()->FindPropertyByName(Name));
		check(Property);
		return Property->GetPropertyValue_InContainer(&Camera);
	}

	/** A camera that has begun play, as in a match. The test world never begins play itself. */
	ARTSCameraPawn* SpawnCamera(const FRTSTestWorld& TestWorld)
	{
		ARTSCameraPawn* Camera = TestWorld.World->SpawnActor<ARTSCameraPawn>();
		Camera->DispatchBeginPlay();
		return Camera;
	}

	/** Zooms fully in and ticks until the arm has eased down to its target. */
	void ZoomFullyIn(ARTSCameraPawn& Camera)
	{
		Camera.AddZoomInput(100.f);
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			Camera.Tick(1.f / 30.f);
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraPitchTest, "WorldDivide.Camera.PitchFollowsZoom", TestFlags)

	bool FRTSCameraPitchTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		const ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const float MinZoom = GetSetting(*Camera, TEXT("MinZoom"));
		const float MaxZoom = GetSetting(*Camera, TEXT("MaxZoom"));
		const float DefaultZoom = GetSetting(*Camera, TEXT("DefaultZoom"));
		const float TopDownPitch = GetSetting(*Camera, TEXT("TopDownPitch"));
		const float NearPitch = GetSetting(*Camera, TEXT("NearPitch"));

		TestEqual(TEXT("Fully zoomed out the camera looks down at the top-down pitch"), Camera->GetPitchForZoom(MaxZoom), TopDownPitch);
		TestEqual(TEXT("At the default zoom the camera looks down at the top-down pitch"), Camera->GetPitchForZoom(DefaultZoom), TopDownPitch);
		TestEqual(TEXT("Fully zoomed in the camera looks almost level"), Camera->GetPitchForZoom(MinZoom), NearPitch);
		TestTrue(TEXT("The near pitch is flatter than the top-down pitch"), NearPitch > TopDownPitch);

		// Zooming out never makes the view flatter, and somewhere in between it is neither extreme.
		constexpr int32 Samples = 100;
		bool bMonotonic = true;
		bool bInBetween = false;
		float Previous = Camera->GetPitchForZoom(MinZoom);
		for (int32 Index = 1; Index <= Samples; ++Index)
		{
			const float Pitch = Camera->GetPitchForZoom(FMath::Lerp(MinZoom, MaxZoom, float(Index) / Samples));
			bMonotonic &= Pitch <= Previous;
			bInBetween |= Pitch < NearPitch - 1.f && Pitch > TopDownPitch + 1.f;
			Previous = Pitch;
		}
		TestTrue(TEXT("The pitch steepens steadily as the camera zooms out"), bMonotonic);
		TestTrue(TEXT("Between the extremes the pitch eases between them"), bInBetween);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraZoomLimitTest, "WorldDivide.Camera.ZoomLimit", TestFlags)

	bool FRTSCameraZoomLimitTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const float MinZoom = GetSetting(*Camera, TEXT("MinZoom"));
		const USpringArmComponent* Arm = Camera->FindComponentByClass<USpringArmComponent>();

		TestEqual(TEXT("A match starts at the default zoom"), Camera->GetZoom(), GetSetting(*Camera, TEXT("DefaultZoom")));
		TestEqual(TEXT("A match starts at the top-down pitch"), Arm->GetRelativeRotation().Pitch, double(GetSetting(*Camera, TEXT("TopDownPitch"))), 0.01);

		ZoomFullyIn(*Camera);
		TestTrue(TEXT("The camera zooms closer than the old 800 cm limit"), Camera->GetZoom() < 800.f);
		TestEqual(TEXT("Zooming in stops at the minimum zoom"), Camera->GetZoom(), MinZoom, 0.5f);
		TestEqual(TEXT("The arm tilts with the zoom it has reached"),
			Arm->GetRelativeRotation().Pitch, double(Camera->GetPitchForZoom(Camera->GetZoom())), 0.01);

		ZoomFullyIn(*Camera);
		TestEqual(TEXT("Zooming in further does nothing"), Camera->GetZoom(), MinZoom, 0.5f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraClearanceTest, "WorldDivide.Camera.GroundClearance", TestFlags)

	bool FRTSCameraClearanceTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const float MinCameraHeight = GetSetting(*Camera, TEXT("MinCameraHeight"));

		ZoomFullyIn(*Camera);
		const FVector Open = Camera->GetCameraGoal();
		TestTrue(TEXT("With nothing below, fully zoomed in sits under tank height"), Open.Z < MinCameraHeight);

		URTSBuildingData* BuildingData = NewObject<URTSBuildingData>();
		BuildingData->Footprint = FIntPoint(3, 3);
		const ARTSBuilding* Building = TestWorld.SpawnBuilding(BuildingData, 0, FVector(Open.X, Open.Y, 0.f));
		const float Roof = Building->GetComponentsBoundingBox(true).Max.Z;
		TestTrue(TEXT("The building would swallow the camera"), Roof > Open.Z);

		// A still camera traces nothing (#295); a scripted jump checks the ground again.
		Camera->JumpTo(Camera->GetActorLocation());
		Camera->Tick(1.f / 30.f);
		const FVector Lifted = Camera->GetCameraGoal();
		TestTrue(TEXT("The camera rises clear of the roof under it"), Lifted.Z >= Roof + MinCameraHeight - 1.f);
		TestTrue(TEXT("Rising keeps the camera above the same spot"), FVector2D(Lifted).Equals(FVector2D(Open), 1.f));
		TestEqual(TEXT("Rising keeps the near-level pitch"),
			Camera->FindComponentByClass<USpringArmComponent>()->GetRelativeRotation().Pitch,
			double(GetSetting(*Camera, TEXT("NearPitch"))), 0.5);
		return true;
	}

	/**
	 * Without a NavMeshBoundsVolume the camera keeps to FallbackMapArea plus EdgeMargin: a rectangle, so panning and JumpTo
	 * take the ground the view shows to its corners, and no further (#295).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraPanAreaTest, "WorldDivide.Camera.PanArea", TestFlags)

	bool FRTSCameraPanAreaTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const FBox2D* Fallback = ARTSCameraPawn::StaticClass()->FindPropertyByName(TEXT("FallbackMapArea"))
			->ContainerPtrToValuePtr<FBox2D>(Camera);
		const FBox2D Expected = Fallback->ExpandBy(GetSetting(*Camera, TEXT("EdgeMargin")));
		TestTrue(FString::Printf(TEXT("The pan area is %s, not %s"), *Expected.ToString(), *Camera->GetPanArea().ToString()),
			Camera->GetPanArea().Min.Equals(Expected.Min) && Camera->GetPanArea().Max.Equals(Expected.Max));

		const auto IsViewInCorner = [Camera, &Expected]
		{
			const FBox2D View = Camera->GetViewFootprintBounds();
			return FMath::IsNearlyEqual(View.Max.X, Expected.Max.X, 1.0) && FMath::IsNearlyEqual(View.Min.Y, Expected.Min.Y, 1.0);
		};
		Camera->JumpTo(FVector(1000000.0, -1000000.0, 0.0));
		TestTrue(FString::Printf(TEXT("JumpTo far away stops with the view in the pan area's corner, not at %s"),
			*Camera->GetViewFootprintBounds().ToString()), IsViewInCorner());

		Camera->JumpTo(FVector::ZeroVector);
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			// Forward and left: the camera starts unrotated, so towards +X and -Y.
			Camera->AddPanInput(FVector2D(-1.0, 1.0));
			Camera->Tick(1.f / 10.f);
		}
		TestTrue(FString::Printf(TEXT("Panning far out stops with the view in the pan area's corner, not at %s"),
			*Camera->GetViewFootprintBounds().ToString()), IsViewInCorner());
		return true;
	}

	/**
	 * The player's view through Camera at ArmLength, looking at the ground at the origin, on a Size screen: the engine's
	 * own projection for the camera's field of view and aspect ratio constraint.
	 */
	struct FCameraView
	{
		FIntRect Rect;
		FMatrix ViewProjection;

		FCameraView(const ARTSCameraPawn& Camera, float ArmLength, const FIntPoint& Size)
			: FCameraView(Camera, -FRotator(Camera.GetPitchForZoom(ArmLength), 0.f, 0.f).Vector() * ArmLength,
				FRotator(Camera.GetPitchForZoom(ArmLength), 0.f, 0.f), Size)
		{
		}

		/** The view from Location along Rotation, as the camera's lens has it. */
		FCameraView(const ARTSCameraPawn& Camera, const FVector& Location, const FRotator& Rotation, const FIntPoint& Size)
			: Rect(FIntPoint::ZeroValue, Size)
		{
			FMinimalViewInfo View;
			Camera.GetCameraComponent()->GetCameraView(0.f, View);
			View.Rotation = Rotation;
			View.Location = Location;

			FSceneViewProjectionData Projection;
			Projection.SetViewRectangle(Rect);
			Projection.ViewOrigin = View.Location;
			Projection.ViewRotationMatrix = FInverseRotationMatrix(View.Rotation)
				* FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
			FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, AspectRatio_MaintainXFOV, Rect, Projection);
			ViewProjection = Projection.ComputeViewProjectionMatrix();
		}

		/** Where the ray through the screen point meets the ground, the plane at Z. */
		FVector GetGround(const FVector2D& Point, float Z = 0.f) const
		{
			FVector Origin;
			FVector Direction;
			FSceneView::DeprojectScreenToWorld(Point, Rect, ViewProjection.Inverse(), Origin, Direction);
			return Origin + Direction * ((Z - Origin.Z) / Direction.Z);
		}

		/** Metres of ground across the middle of the screen. */
		float GetAcross() const
		{
			return FVector::Dist(GetGround(FVector2D(0.f, Rect.Height() / 2.f)), GetGround(FVector2D(Rect.Width(), Rect.Height() / 2.f))) / 100.f;
		}

		/** Metres of ground from the bottom of the screen to its top, through its middle. */
		float GetDeep() const
		{
			return FVector::Dist(GetGround(FVector2D(Rect.Width() / 2.f, 0.f)), GetGround(FVector2D(Rect.Width() / 2.f, Rect.Height()))) / 100.f;
		}

		/** The share of the screen's width a hull standing broadside on the ground in the middle of the view takes. */
		float GetBroadsideShare(const FBox& Hull) const
		{
			// The camera looks along +X, so world Y runs across the screen.
			const FVector Extent(Hull.GetExtent().Y, Hull.GetExtent().X, Hull.GetExtent().Z);
			FBox2D OnScreen(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point = FVector(0.f, 0.f, Extent.Z) + Extent * FVector(Corner & 1 ? 1.f : -1.f, Corner & 2 ? 1.f : -1.f, Corner & 4 ? 1.f : -1.f);
				FVector2D Pixel;
				FSceneView::ProjectWorldToScreen(Point, Rect, ViewProjection, Pixel);
				OnScreen += Pixel;
			}
			return OnScreen.GetSize().X / Rect.Width();
		}
	};

	/**
	 * The real-scale camera (#137): about 170 m of ground across at the default zoom with an MBT about 5-6 % of the width, a
	 * base and its approaches at the farthest zoom, the same view at 1080p and 1440p, and a wider screen seeing more to the
	 * sides without losing depth.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraScaleTargetTest, "WorldDivide.Camera.ScaleTarget", TestFlags)

	bool FRTSCameraScaleTargetTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		const ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank"));
		if (!TestNotNull(TEXT("DA_TestTank loads"), Tank))
		{
			return true;
		}

		const FCameraView Default(*Camera, Camera->GetDefaultZoom(), FIntPoint(1920, 1080));
		TestTrue(FString::Printf(TEXT("The default zoom shows %.0f m across, about 170"), Default.GetAcross()),
			Default.GetAcross() >= 160.f && Default.GetAcross() <= 185.f);
		const float TankShare = Default.GetBroadsideShare(RTSUnitShape::FromData(*Tank).Hull);
		TestTrue(FString::Printf(TEXT("A tank is %.1f %% of the screen's width at the default zoom, about 5-6"), 100.f * TankShare),
			TankShare >= 0.05f && TankShare <= 0.065f);

		const FCameraView Farthest(*Camera, Camera->GetMaxZoom(), FIntPoint(1920, 1080));
		TestTrue(FString::Printf(TEXT("The farthest zoom shows %.0f m across: a base and its approaches"), Farthest.GetAcross()),
			Farthest.GetAcross() >= 450.f);

		const FCameraView Default1440(*Camera, Camera->GetDefaultZoom(), FIntPoint(2560, 1440));
		TestEqual(TEXT("1440p shows the same ground across as 1080p"), Default1440.GetAcross(), Default.GetAcross(), 0.5f);
		TestEqual(TEXT("1440p shows the same ground deep as 1080p"), Default1440.GetDeep(), Default.GetDeep(), 0.5f);

		const FCameraView Ultrawide(*Camera, Camera->GetDefaultZoom(), FIntPoint(3440, 1440));
		TestTrue(FString::Printf(TEXT("21:9 shows %.0f m across, more than 16:9's %.0f"), Ultrawide.GetAcross(), Default.GetAcross()),
			Ultrawide.GetAcross() > 1.25f * Default.GetAcross());
		TestEqual(TEXT("21:9 shows the same ground deep as 16:9"), Ultrawide.GetDeep(), Default.GetDeep(), 0.5f);
		return true;
	}

	/** Eases the arm onto the zoom it was asked for. */
	void SettleZoom(ARTSCameraPawn& Camera)
	{
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Camera.Tick(1.f / 30.f);
		}
	}

	/**
	 * Every wheel notch changes the arm by the same share of its length, so zooming feels the same at every height and never
	 * jumps; the tilt changes by a few degrees at most per notch; the whole range takes a practical number of notches.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraZoomStepsTest, "WorldDivide.Camera.ZoomSteps", TestFlags)

	bool FRTSCameraZoomStepsTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const float Fraction = GetSetting(*Camera, TEXT("ZoomStepFraction"));
		ZoomFullyIn(*Camera);

		int32 Notches = 0;
		float LargestShare = 0.f;
		float LargestTilt = 0.f;
		while (Camera->GetZoom() < Camera->GetMaxZoom() - 0.5f && Notches < 100)
		{
			const float Before = Camera->GetZoom();
			Camera->AddZoomInput(-1.f);
			SettleZoom(*Camera);
			++Notches;
			LargestShare = FMath::Max(LargestShare, Camera->GetZoom() / Before - 1.f);
			LargestTilt = FMath::Max(LargestTilt, FMath::Abs(Camera->GetPitchForZoom(Camera->GetZoom()) - Camera->GetPitchForZoom(Before)));
		}
		TestTrue(FString::Printf(TEXT("Zooming out reaches the farthest zoom, in %d notches"), Notches), Camera->GetZoom() >= Camera->GetMaxZoom() - 0.5f);
		TestTrue(FString::Printf(TEXT("%d notches from the closest to the farthest zoom: between 15 and 30"), Notches), Notches >= 15 && Notches <= 30);
		TestTrue(FString::Printf(TEXT("No notch grows the arm by more than %.0f %% (largest %.1f %%)"), 100.f * Fraction, 100.f * LargestShare),
			LargestShare <= Fraction + 0.001f);
		TestTrue(FString::Printf(TEXT("No notch tilts the camera by more than 8 degrees (largest %.1f)"), LargestTilt), LargestTilt <= 8.f);

		Camera->AddZoomInput(1.f);
		SettleZoom(*Camera);
		TestEqual(TEXT("One notch in from the farthest zoom comes back by the same share"), Camera->GetZoom(),
			Camera->GetMaxZoom() / (1.f + Fraction), 1.f);
		return true;
	}
	const FIntPoint Screen1080(1920, 1080);

	/** The player's view at 1920x1080 from where the camera settles. */
	FCameraView GetGoalView(const ARTSCameraPawn& Camera)
	{
		return FCameraView(Camera, Camera.GetCameraGoal(), Camera.GetCameraGoalRotation(), Screen1080);
	}

	/**
	 * The wheel zooms towards the cursor (#295): the ground under it stays there, to under 1 % of the view's width, while
	 * the arm eases in, tilts and eases back out. Over the sky the zoom keeps the camera where it is.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraZoomToCursorTest, "WorldDivide.Camera.ZoomToCursor", TestFlags)

	bool FRTSCameraZoomToCursorTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		Camera->SetYawNow(30.f);

		const FVector2D Pixel(1500.f, 780.f);
		const FVector2D ScreenPoint = Pixel / FVector2D(Screen1080);
		const FVector Ground = GetGoalView(*Camera).GetGround(Pixel);
		const float Width = 100.f * GetGoalView(*Camera).GetAcross();
		for (const float Steps : { 8.f, -8.f })
		{
			const float Before = Camera->GetZoom();
			Camera->AddZoomInput(Steps, ScreenPoint);
			float Drift = 0.f;
			for (int32 Frame = 0; Frame < 120; ++Frame)
			{
				Camera->Tick(1.f / 30.f);
				Drift = FMath::Max(Drift, float(FVector2D::Distance(FVector2D(GetGoalView(*Camera).GetGround(Pixel)), FVector2D(Ground))));
			}
			TestTrue(FString::Printf(TEXT("Zooming %.0f notches changes the zoom, from %.0f to %.0f"), Steps, Before, Camera->GetZoom()),
				FMath::Abs(Camera->GetZoom() - Before) > 0.5f * Before);
			TestTrue(FString::Printf(TEXT("Zooming %.0f notches keeps the ground under the cursor: %.0f cm off, under 1 %% of %.0f"),
				Steps, Drift, Width), Drift < 0.01f * Width);
		}

		Camera->SetZoomNow(Camera->GetMinZoom());
		const FVector2D Place(Camera->GetActorLocation());
		Camera->AddZoomInput(-2.f, FVector2D(0.5f, 0.f));
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Camera->Tick(1.f / 30.f);
		}
		TestTrue(TEXT("Zooming out with the cursor on the sky zooms out"), Camera->GetZoom() > Camera->GetMinZoom() + 100.f);
		TestTrue(FString::Printf(TEXT("Zooming with the cursor on the sky keeps the camera's place, not %s"),
			*FVector2D(Camera->GetActorLocation()).ToString()), FVector2D(Camera->GetActorLocation()).Equals(Place, 1.0));
		return true;
	}

	/**
	 * The pivot eases onto the ground's height as the camera pans over a rise, a few steps rather than one jump, and a
	 * still camera makes no ground traces (#295, PERFORMANCE_AUDIT.md).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraTerrainFollowTest, "WorldDivide.Camera.TerrainFollow", TestFlags)

	bool FRTSCameraTerrainFollowTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		constexpr float Height = 1500.f;
		AActor* Rise = TestWorld.World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Rise);
		Rise->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(15000.f, 15000.f, Height / 2.f));
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->RegisterComponent();
		Box->SetWorldLocation(FVector(35000.f, 0.f, Height / 2.f));

		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		TestEqual(TEXT("On open ground the pivot stays at its height"), Camera->GetActorLocation().Z, 0.0, 1.0);

		float LargestStep = 0.f;
		int32 EasingFrames = 0;
		for (int32 Frame = 0; Frame < 900 && Camera->GetActorLocation().X < 35000.f; ++Frame)
		{
			const double Before = Camera->GetActorLocation().Z;
			Camera->AddPanInput(FVector2D(0.f, 1.f));
			Camera->Tick(1.f / 30.f);
			const float Step = float(Camera->GetActorLocation().Z - Before);
			LargestStep = FMath::Max(LargestStep, Step);
			EasingFrames += Step > 1.f ? 1 : 0;
		}
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			Camera->Tick(1.f / 30.f);
		}
		TestTrue(TEXT("The camera panned onto the rise"), Camera->GetActorLocation().X >= 35000.f - 1.f);
		TestEqual(TEXT("On the rise the pivot settles on its top"), Camera->GetActorLocation().Z, double(Height), 1.0);
		TestTrue(FString::Printf(TEXT("The pivot eases up, at most %.0f cm a frame (largest %.0f) over %d frames"), 0.3f * Height,
			LargestStep, EasingFrames), LargestStep <= 0.3f * Height && EasingFrames >= 5);

		const int32 Traces = Camera->GetTraceCount();
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Camera->Tick(1.f / 30.f);
		}
		TestEqual(TEXT("A still camera traces nothing"), Camera->GetTraceCount(), Traces);
		Camera->AddPanInput(FVector2D(1.f, 0.f));
		Camera->Tick(1.f / 30.f);
		TestTrue(TEXT("A moving camera checks the ground again"), Camera->GetTraceCount() > Traces);
		return true;
	}

	/**
	 * The ground the view shows stays within the pan area (#295): at the default and farthest zoom, turned 0, 45 and 135
	 * degrees, jumped past each corner of the map and panned into one, no corner of the screen looks past it.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraViewClampTest, "WorldDivide.Camera.ViewClamp", TestFlags)

	bool FRTSCameraViewClampTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const FBox2D Area = Camera->GetPanArea().ExpandBy(1.0);
		const FVector2D Corners[] = { FVector2D(0.0, 0.0), FVector2D(Screen1080.X, 0.0), FVector2D(Screen1080), FVector2D(0.0, Screen1080.Y) };

		const auto VerifyView = [this, Camera, &Area, &Corners](const FString& How)
		{
			const FCameraView View = GetGoalView(*Camera);
			for (const FVector2D& Corner : Corners)
			{
				const FVector Ground = View.GetGround(Corner, Camera->GetActorLocation().Z);
				TestTrue(FString::Printf(TEXT("%s: the screen's corner %s shows %s, inside the pan area %s"), *How, *Corner.ToString(),
					*FVector2D(Ground).ToString(), *Area.ToString()), Area.IsInside(FVector2D(Ground)));
			}
		};

		for (const float Zoom : { Camera->GetDefaultZoom(), Camera->GetMaxZoom() })
		{
			for (const float Yaw : { 0.f, 45.f, 135.f })
			{
				Camera->SetZoomNow(Zoom);
				Camera->SetYawNow(Yaw);
				for (const FVector2D Far : { FVector2D(1.0, 1.0), FVector2D(-1.0, 1.0), FVector2D(-1.0, -1.0), FVector2D(1.0, -1.0) })
				{
					Camera->JumpTo(FVector(Far * 1000000.0, 0.0));
					Camera->Tick(1.f / 30.f);
					VerifyView(FString::Printf(TEXT("Zoom %.0f, yaw %.0f, jumped towards %s"), Zoom, Yaw, *Far.ToString()));
				}
			}
		}

		Camera->SetZoomNow(Camera->GetMaxZoom());
		Camera->SetYawNow(45.f);
		Camera->JumpTo(FVector::ZeroVector);
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			Camera->AddPanInput(FVector2D(1.0, 1.0));
			Camera->Tick(1.f / 10.f);
		}
		VerifyView(TEXT("Farthest zoom, yaw 45, panned into a corner"));
		const FBox2D View = Camera->GetViewFootprintBounds();
		TestTrue(FString::Printf(TEXT("Panning takes the view to the pan area's edge: %s in %s"), *View.ToString(), *Area.ToString()),
			FMath::IsNearlyEqual(View.Max.X, Camera->GetPanArea().Max.X, 1.0) || FMath::IsNearlyEqual(View.Max.Y, Camera->GetPanArea().Max.Y, 1.0));
		return true;
	}

	/** Rotation eases to the yaw asked for, and Home turns back to north the short way round, keeping the zoom (#295). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCameraRotationResetTest, "WorldDivide.Camera.RotationReset", TestFlags)

	bool FRTSCameraRotationResetTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSCameraPawn* Camera = SpawnCamera(TestWorld);
		const auto GetYaw = [Camera] { return float(FRotator::NormalizeAxis(Camera->GetActorRotation().Yaw)); };

		Camera->AddRotateInput(90.f);
		Camera->Tick(1.f / 30.f);
		TestTrue(FString::Printf(TEXT("Rotating eases rather than jumps: %.1f after one frame"), GetYaw()), GetYaw() > 0.1f && GetYaw() < 89.9f);
		SettleZoom(*Camera);
		TestEqual(TEXT("Rotating reaches the yaw asked for"), GetYaw(), 90.f, 0.05f);

		Camera->SetYawNow(350.f);
		const float Zoom = Camera->GetZoom();
		Camera->ResetYawToNorth();
		Camera->Tick(1.f / 30.f);
		TestTrue(FString::Printf(TEXT("From 350 north is the short way through 360: %.1f after one frame"), GetYaw()),
			GetYaw() > -9.9f && GetYaw() < -0.1f);
		bool bShortWay = true;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Camera->Tick(1.f / 30.f);
			bShortWay &= GetYaw() <= 0.01f && GetYaw() >= -10.f;
		}
		TestTrue(TEXT("The turn to north never goes the long way"), bShortWay);
		TestEqual(TEXT("Home turns the camera to north"), GetYaw(), 0.f, 0.05f);
		TestEqual(TEXT("Home keeps the zoom"), Camera->GetZoom(), Zoom);
		return true;
	}
}

#endif
