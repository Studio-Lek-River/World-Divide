#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitAIController.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSGroundTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 0.05f;

	/** A ramp rising toward +X at this many degrees: an engine cube 20 m long, 10 m wide and 1 m thick, centred on the origin. */
	constexpr float RampDegrees = 20.f;
	const FVector RampScale(20.f, 10.f, 1.f);

	AStaticMeshActor* SpawnRamp(const FRTSTestWorld& TestWorld)
	{
		AStaticMeshActor* Ramp = TestWorld.World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator(RampDegrees, 0.f, 0.f));
		// Static meshes cannot change on a static component in a game world.
		Ramp->SetMobility(EComponentMobility::Movable);
		Ramp->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Ramp->SetActorScale3D(RampScale);
		return Ramp;
	}

	/** Height of the ramp's top face at X. */
	float RampZ(float X)
	{
		const float Angle = FMath::DegreesToRadians(RampDegrees);
		const FVector Normal(-FMath::Sin(Angle), 0.f, FMath::Cos(Angle));
		const FVector TopCentre = Normal * 50.f * RampScale.Z;
		return TopCentre.Z + (X - TopCentre.X) * FMath::Tan(Angle);
	}

	URTSUnitData* MakeUnit(bool bTilts)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->bTiltsToGround = bTilts;
		return Data;
	}

	/** Ticks the units, which keep them on the ground and lean their meshes, for Seconds. */
	void StepUnits(TConstArrayView<ARTSUnit*> Units, float Seconds)
	{
		for (float Time = 0.f; Time < Seconds; Time += Step)
		{
			for (ARTSUnit* Unit : Units)
			{
				Unit->Tick(Step);
			}
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFollowGroundTest, "WorldDivide.Units.FollowGround", TestFlags)

	bool FRTSFollowGroundTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		SpawnRamp(TestWorld);
		ARTSUnit* Tank = TestWorld.SpawnUnit(MakeUnit(true), 0, FVector(300.f, 0.f, RampZ(300.f) + 50.f));
		ARTSUnit* Infantry = TestWorld.SpawnUnit(MakeUnit(false), 0, FVector(-300.f, 200.f, RampZ(-300.f) - 50.f));
		ARTSUnit* OffMap = TestWorld.SpawnUnit(MakeUnit(true), 0, FVector(5000.f, 0.f, 123.f));

		StepUnits({ Tank, Infantry, OffMap }, 2.f);

		TestNearlyEqual(TEXT("A unit too high comes down onto the ramp"), static_cast<float>(Tank->GetNavAgentLocation().Z), RampZ(300.f), 1.f);
		TestNearlyEqual(TEXT("A unit sunk into the ramp comes up onto it"), static_cast<float>(Infantry->GetNavAgentLocation().Z), RampZ(-300.f), 1.f);
		TestNearlyEqual(TEXT("The vehicle's mesh pitches with the ramp"), static_cast<float>(Tank->GetMeshTilt().Pitch), RampDegrees, 0.5f);
		TestNearlyEqual(TEXT("Without rolling"), static_cast<float>(Tank->GetMeshTilt().Roll), 0.f, 0.5f);
		TestTrue(TEXT("The infantry stays upright"), Infantry->GetMeshTilt().IsNearlyZero(0.01f));
		TestNearlyEqual(TEXT("Over no ground a unit stays where it is"), static_cast<float>(OffMap->GetActorLocation().Z), 123.f, 0.01f);
		TestTrue(TEXT("And upright"), OffMap->GetMeshTilt().IsNearlyZero(0.01f));

		// Turned across the slope, the vehicle rolls instead: the ground rises on its right, so the right side lifts.
		Tank->SetActorRotation(FRotator(0.f, -90.f, 0.f));
		StepUnits({ Tank }, 2.f);
		TestNearlyEqual(TEXT("Across the slope the vehicle does not pitch"), static_cast<float>(Tank->GetMeshTilt().Pitch), 0.f, 0.5f);
		TestNearlyEqual(TEXT("It rolls with the slope"), static_cast<float>(FMath::Abs(Tank->GetMeshTilt().Roll)), RampDegrees, 0.5f);
		TestTrue(TEXT("Its right side up, as negative roll lifts it"), Tank->GetMeshTilt().Roll < 0.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSIdleGroundingTest, "WorldDivide.Units.IdleGrounding", TestFlags)

	bool FRTSIdleGroundingTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		SpawnRamp(TestWorld);
		ARTSUnit* Tank = TestWorld.SpawnUnit(MakeUnit(true), 0, FVector(300.f, 0.f, RampZ(300.f) + 50.f));
		StepUnits({ Tank }, 2.f);
		const float StandZ = Tank->GetActorLocation().Z;

		// Standing still, it keeps its ground and its lean without tracing again.
		StepUnits({ Tank }, 2.f);
		TestNearlyEqual(TEXT("An idle vehicle stays on the ramp"), static_cast<float>(Tank->GetNavAgentLocation().Z), RampZ(300.f), 1.f);
		TestNearlyEqual(TEXT("And leaning with it"), static_cast<float>(Tank->GetMeshTilt().Pitch), RampDegrees, 0.5f);

		// Within a centimetre it counts as unmoved: the ground is not looked for again.
		Tank->SetActorLocation(Tank->GetActorLocation() - FVector(0.f, 0.f, 0.5f));
		StepUnits({ Tank }, 0.2f);
		TestNearlyEqual(TEXT("Half a centimetre lower, it is not snapped back: the trace was skipped"),
			static_cast<float>(Tank->GetActorLocation().Z), StandZ - 0.5f, 0.01f);

		// Further, it is grounded again on the very next tick.
		Tank->SetActorLocation(Tank->GetActorLocation() - FVector(0.f, 0.f, 5.f));
		StepUnits({ Tank }, Step);
		TestNearlyEqual(TEXT("Moved 5 cm, it stands on the ramp again at once"), static_cast<float>(Tank->GetActorLocation().Z), StandZ, 0.1f);

		// Driving up the ramp, it follows the ground every frame.
		for (int32 Frame = 0; Frame < 20; ++Frame)
		{
			Tank->SetActorLocation(Tank->GetActorLocation() + FVector(10.f, 0.f, 0.f));
			StepUnits({ Tank }, Step);
		}
		TestNearlyEqual(TEXT("A moving vehicle follows the ramp"), static_cast<float>(Tank->GetNavAgentLocation().Z), RampZ(500.f), 1.f);

		TestFalse(TEXT("Unit AI controllers never tick"), GetDefault<ARTSUnitAIController>()->PrimaryActorTick.bCanEverTick);
		return true;
	}
}

#endif
