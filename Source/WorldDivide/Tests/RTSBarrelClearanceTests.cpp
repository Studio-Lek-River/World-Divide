#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSBarrelClearance.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The M20's barrel against walls in a bare world (#162): no navmesh, so the sweeps alone decide, and the tests step the
 * turret and weapon themselves. The M20 stands at the origin facing +X, its gun forward; a box stands across its way where
 * the barrel reaches past the hull's front. The driving cases are WorldDivide.Navigation.BarrelClearance.Drive.
 */
namespace RTSBarrelClearanceTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	constexpr float Step = 0.05f;
	/** Long enough for the M20's turret to traverse half a turn. */
	constexpr float SettleSeconds = 5.f;
	/** Long enough for it to turn its barrel out of a wall across its front: some 45 degrees at 45 degrees a second. */
	constexpr float TraverseSeconds = 1.5f;
	/** As the hull tests: a barrel may touch an obstacle by this much at most. */
	constexpr float OverlapTolerance = 2.f;

	/** The box's near face lies this far past the hull's front, its far face this far past the barrel's reach. */
	constexpr float BoxFrontGap = 20.f;
	constexpr float BoxBeyondReach = 200.f;
	constexpr float BoxHalfWidth = 300.f;
	/** A low wall the gun reaches over, and a wall taller than the gun can clear raised. */
	constexpr float LowBoxHeight = 100.f;
	constexpr float TallBoxHeight = 500.f;

	/** The M20 with an instant weapon, so a target's health tells when it fired. */
	URTSUnitData* MakeM20(FAutomationTestBase& Test)
	{
		const URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
		if (!Test.TestNotNull(TEXT("M20 data"), M20) || !Test.TestTrue(TEXT("The M20 has a separate barrel"), RTSUnitShape::FromData(*M20).Barrel.IsSet()))
		{
			return nullptr;
		}
		URTSUnitData* Data = DuplicateObject<URTSUnitData>(M20, GetTransientPackage());
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = 2000.f;
		Weapon->Damage = 10.f;
		Weapon->ReloadSeconds = 0.1f;
		Data->Weapon = Weapon;
		return Data;
	}

	/** Enough health to outlast every shot of a test. */
	constexpr float TargetHealth = 100000.f;

	URTSUnitData* MakeTarget()
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = TargetHealth;
		return Data;
	}

	/** Spawns Data's unit standing on the ground (z = 0) at Location's XY, facing +X. */
	ARTSUnit* SpawnOnGround(const FRTSTestWorld& TestWorld, URTSUnitData* Data, int32 TeamId, const FVector& Location)
	{
		const FRTSUnitShape Shape = RTSUnitShape::FromData(*Data);
		return TestWorld.SpawnUnit(Data, TeamId, FVector(Location.X, Location.Y, -Shape.Hull.Min.Z));
	}

	/** A solid box on the ground across the M20's way, Height tall, as the world's static geometry. */
	FBox SpawnBox(const FRTSTestWorld& TestWorld, const FRTSUnitShape& Shape, float Height)
	{
		const float Front = Shape.Hull.Max.X + BoxFrontGap;
		const FBox Box(FVector(Front, -BoxHalfWidth, 0.f), FVector(Shape.Barrel->GetReach() + BoxBeyondReach, BoxHalfWidth, Height));
		AStaticMeshActor* Actor = TestWorld.World->SpawnActor<AStaticMeshActor>(Box.GetCenter(), FRotator::ZeroRotator);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CubePath));
		Actor->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		// The engine cube is 100 cm a side round its middle.
		Actor->SetActorScale3D(Box.GetSize() / 100.f);
		return Box;
	}

	float GetPenetration(const ARTSUnit& Unit, const FBox& Box)
	{
		FVector Start;
		FVector End;
		return Unit.GetBarrelSegment(Start, End) ? RTSBarrelClearance::GetPenetration(Start, End, Unit.GetShape().Barrel->Radius, Box) : 0.f;
	}

	/** Steps the turret and weapon for Seconds; the deepest the barrel reached into Box from TraverseSeconds on. */
	float Run(ARTSUnit& Unit, const FBox& Box, float Seconds)
	{
		float Deepest = 0.f;
		for (int32 Frame = 1; Frame * Step <= Seconds; ++Frame)
		{
			Unit.UpdateTurret(Step);
			Unit.GetWeaponComponent()->UpdateWeapon(Step);
			if (Frame * Step >= TraverseSeconds)
			{
				Deepest = FMath::Max(Deepest, GetPenetration(Unit, Box));
			}
		}
		return Deepest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBarrelClearanceShapeTest, "WorldDivide.Combat.BarrelClearance.Shape", RTSTests::TestFlags)

bool FRTSBarrelClearanceShapeTest::RunTest(const FString& Parameters)
{
	using namespace RTSBarrelClearanceTests;
	const URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
	const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
	if (!TestNotNull(TEXT("M20 data"), M20) || !TestNotNull(TEXT("Tank data"), Tank))
	{
		return false;
	}
	const FRTSUnitShape Shape = RTSUnitShape::FromData(*M20);
	if (!TestTrue(TEXT("The M20's barrel comes from its data"), Shape.Barrel.IsSet()))
	{
		return false;
	}
	// The muzzle at rest where its data puts it: on the gun, on the turret, on the hull mesh.
	FVector Start;
	FVector End;
	RTSBarrelClearance::GetSegment(*Shape.Barrel, FTransform::Identity, 0.f, 0.f, Start, End);
	const FVector Muzzle = Shape.MeshOffset + (M20->TurretOffset + M20->GunOffset + M20->GunMuzzleOffset) * M20->MeshScale;
	TestTrue(FString::Printf(TEXT("The barrel ends at the muzzle (%s, data %s)"), *End.ToCompactString(), *Muzzle.ToCompactString()),
		End.Equals(Muzzle, 0.1f));
	TestTrue(FString::Printf(TEXT("The gun reaches past the hull's front (%.0f cm against %.0f)"), End.X, Shape.Hull.Max.X),
		End.X > Shape.Hull.Max.X);
	TestTrue(FString::Printf(TEXT("The barrel has a thickness (%.1f cm)"), Shape.Barrel->Radius), Shape.Barrel->Radius > 0.f);
	TestTrue(TEXT("Its reach covers the muzzle at any yaw"), Shape.Barrel->GetReach() >= FVector2D(End).Size());
	TestFalse(TEXT("The placeholder tank's gun is part of its hull mesh: no separate barrel"), RTSUnitShape::FromData(*Tank).Barrel.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBarrelClearanceOverRoofTest, "WorldDivide.Combat.BarrelClearance.OverRoof", RTSTests::TestFlags)

bool FRTSBarrelClearanceOverRoofTest::RunTest(const FString& Parameters)
{
	using namespace RTSBarrelClearanceTests;
	URTSUnitData* Data = MakeM20(*this);
	if (!Data)
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* M20 = SpawnOnGround(TestWorld, Data, 0, FVector::ZeroVector);
	const FBox Box = SpawnBox(TestWorld, M20->GetShape(), LowBoxHeight);
	FVector Start;
	FVector End;
	M20->GetBarrelSegment(Start, End);
	TestTrue(FString::Printf(TEXT("The barrel reaches over the low wall (muzzle at x %.0f, z %.0f; wall from x %.0f, %.0f high)"),
		End.X, End.Z, Box.Min.X, Box.Max.Z), End.X > Box.Min.X && End.Z - M20->GetShape().Barrel->Radius > Box.Max.Z);

	Run(*M20, Box, SettleSeconds);
	TestFalse(TEXT("The barrel is clear over the low wall"), M20->IsAimBlocked());
	TestEqual(TEXT("The turret stays forward"), M20->GetTurretYaw(), 0.f);
	TestEqual(TEXT("The gun stays level"), M20->GetGunPitch(), 0.f);
	TestTrue(FString::Printf(TEXT("The barrel stays out of the wall (%.1f cm)"), GetPenetration(*M20, Box)),
		GetPenetration(*M20, Box) <= OverlapTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBarrelClearanceWallTest, "WorldDivide.Combat.BarrelClearance.Wall", RTSTests::TestFlags)

bool FRTSBarrelClearanceWallTest::RunTest(const FString& Parameters)
{
	using namespace RTSBarrelClearanceTests;
	URTSUnitData* Data = MakeM20(*this);
	if (!Data)
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* M20 = SpawnOnGround(TestWorld, Data, 0, FVector::ZeroVector);
	const FBox Box = SpawnBox(TestWorld, M20->GetShape(), TallBoxHeight);
	TestTrue(TEXT("Parked with its gun forward, the barrel starts in the tall wall"), GetPenetration(*M20, Box) > OverlapTolerance);

	const float Deepest = Run(*M20, Box, SettleSeconds);
	TestTrue(FString::Printf(TEXT("The turret turned the barrel out of the wall (%.0f degrees)"), M20->GetTurretYaw()),
		FMath::Abs(M20->GetTurretYaw()) >= 15.f);
	TestTrue(FString::Printf(TEXT("The barrel is out of the wall (%.1f cm now)"), GetPenetration(*M20, Box)),
		GetPenetration(*M20, Box) <= OverlapTolerance);
	TestTrue(FString::Printf(TEXT("It stayed out once turned (%.1f cm at most from %.1f s on)"), Deepest, TraverseSeconds),
		Deepest <= OverlapTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBarrelClearanceTargetTest, "WorldDivide.Combat.BarrelClearance.Target", RTSTests::TestFlags)

bool FRTSBarrelClearanceTargetTest::RunTest(const FString& Parameters)
{
	using namespace RTSBarrelClearanceTests;
	URTSUnitData* Data = MakeM20(*this);
	if (!Data)
	{
		return false;
	}

	// A target behind the tall wall: the turret will not swing the barrel into it, so the gun waits.
	{
		const FRTSTestWorld TestWorld;
		ARTSUnit* M20 = SpawnOnGround(TestWorld, Data, 0, FVector::ZeroVector);
		const FBox Box = SpawnBox(TestWorld, M20->GetShape(), TallBoxHeight);
		ARTSUnit* Enemy = SpawnOnGround(TestWorld, MakeTarget(), 1, FVector(Box.Max.X + 300.f, 0.f, 0.f));
		// Ordered at it, so the turret knows its target from the first frame, before the weapon could fire.
		M20->GetWeaponComponent()->SetOrderedTarget(Enemy);
		Run(*M20, Box, SettleSeconds);
		TestTrue(TEXT("The M20 targets the enemy behind the wall"), M20->GetWeaponComponent()->GetTarget() == Enemy);
		TestTrue(TEXT("Its aim at the enemy is blocked"), M20->IsAimBlocked());
		TestFalse(TEXT("The turret does not point at it"), M20->IsAimedAt(*Enemy));
		TestEqual(TEXT("The gun held its fire"), Enemy->GetHealthComponent()->GetHealth(), TargetHealth);
		TestTrue(FString::Printf(TEXT("The barrel is out of the wall (%.1f cm)"), GetPenetration(*M20, Box)),
			GetPenetration(*M20, Box) <= OverlapTolerance);
	}

	// A target off to the side, clear of the wall: targeting and firing work as without one.
	{
		const FRTSTestWorld TestWorld;
		ARTSUnit* M20 = SpawnOnGround(TestWorld, Data, 0, FVector::ZeroVector);
		const FBox Box = SpawnBox(TestWorld, M20->GetShape(), TallBoxHeight);
		ARTSUnit* Enemy = SpawnOnGround(TestWorld, MakeTarget(), 1, FVector(0.f, 1000.f, 0.f));
		Run(*M20, Box, SettleSeconds);
		TestFalse(TEXT("The aim at the side is clear"), M20->IsAimBlocked());
		TestTrue(FString::Printf(TEXT("The turret points at the enemy (%.0f degrees)"), M20->GetTurretYaw()), M20->IsAimedAt(*Enemy));
		TestTrue(TEXT("The gun fired"), Enemy->GetHealthComponent()->GetHealth() < TargetHealth);
		TestTrue(FString::Printf(TEXT("The barrel is out of the wall (%.1f cm)"), GetPenetration(*M20, Box)),
			GetPenetration(*M20, Box) <= OverlapTolerance);
	}
	return true;
}

#endif
