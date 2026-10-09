#include "Buildings/RTSBuilding.h"
#include "Data/RTSBuildingData.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSCameraPawn.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"

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

		Camera->Tick(1.f / 30.f);
		const FVector Lifted = Camera->GetCameraGoal();
		TestTrue(TEXT("The camera rises clear of the roof under it"), Lifted.Z >= Roof + MinCameraHeight - 1.f);
		TestTrue(TEXT("Rising keeps the camera above the same spot"), FVector2D(Lifted).Equals(FVector2D(Open), 1.f));
		TestEqual(TEXT("Rising keeps the near-level pitch"),
			Camera->FindComponentByClass<USpringArmComponent>()->GetRelativeRotation().Pitch,
			double(GetSetting(*Camera, TEXT("NearPitch"))), 0.5);
		return true;
	}
}

#endif
