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

	/** A solid box of the world's static geometry. */
	void SpawnSolid(const FRTSTestWorld& TestWorld, const FBox& Box)
	{
		AStaticMeshActor* Actor = TestWorld.World->SpawnActor<AStaticMeshActor>(Box.GetCenter(), FRotator::ZeroRotator);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CubePath));
		Actor->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		// The engine cube is 100 cm a side round its middle.
		Actor->SetActorScale3D(Box.GetSize() / 100.f);
	}

	/** A solid box on the ground across the M20's way, Height tall, as the world's static geometry. */
	FBox SpawnBox(const FRTSTestWorld& TestWorld, const FRTSUnitShape& Shape, float Height)
	{
		const float Front = Shape.Hull.Max.X + BoxFrontGap;
		const FBox Box(FVector(Front, -BoxHalfWidth, 0.f), FVector(Shape.Barrel->GetReach() + BoxBeyondReach, BoxHalfWidth, Height));
		SpawnSolid(TestWorld, Box);
		return Box;
	}

	float GetPenetration(const ARTSUnit& Unit, const FBox& Box)
	{
		FVector Start;
		FVector End;
		return Unit.GetBarrelSegment(Start, End) ? RTSBarrelClearance::GetPenetration(Start, End, Unit.GetShape().Barrel->Radius, Box) : 0.f;
	}

	/** The deepest the barrel reaches into any of Boxes. */
	float GetPenetration(const ARTSUnit& Unit, TConstArrayView<FBox> Boxes)
	{
		float Deepest = 0.f;
		for (const FBox& Box : Boxes)
		{
			Deepest = FMath::Max(Deepest, GetPenetration(Unit, Box));
		}
		return Deepest;
	}

	/** One frame: the world's clock, so the unit's checks come round as in play, then the turret (or, ticked, the whole unit) and weapon. */
	void StepFrame(ARTSUnit& Unit, bool bTick)
	{
		Unit.GetWorld()->TimeSeconds += Step;
		if (bTick)
		{
			Unit.Tick(Step);
		}
		else
		{
			Unit.UpdateTurret(Step);
		}
		Unit.GetWeaponComponent()->UpdateWeapon(Step);
	}

	/** Steps the turret and weapon for Seconds; the deepest the barrel reached into Box from TraverseSeconds on. */
	float Run(ARTSUnit& Unit, const FBox& Box, float Seconds)
	{
		float Deepest = 0.f;
		for (int32 Frame = 1; Frame * Step <= Seconds; ++Frame)
		{
			StepFrame(Unit, false);
			if (Frame * Step >= TraverseSeconds)
			{
				Deepest = FMath::Max(Deepest, GetPenetration(Unit, Box));
			}
		}
		return Deepest;
	}

	/** Steps the unit for Seconds; the deepest the barrel reached into any of Boxes on any frame, the swing included. */
	float RunEveryFrame(ARTSUnit& Unit, TConstArrayView<FBox> Boxes, float Seconds, bool bTick = false)
	{
		float Deepest = 0.f;
		for (int32 Frame = 1; Frame * Step <= Seconds; ++Frame)
		{
			StepFrame(Unit, bTick);
			Deepest = FMath::Max(Deepest, GetPenetration(Unit, Boxes));
		}
		return Deepest;
	}

	/** The turret yaw closest to forward on Direction's side (1 left of the wall, -1 right) at which the level barrel is clear. */
	TOptional<float> FindClearYaw(const ARTSUnit& Unit, float Direction)
	{
		for (float Yaw = 0.f; Yaw <= 180.f; Yaw += 1.f)
		{
			if (RTSBarrelClearance::IsClearAt(*Unit.GetWorld(), *Unit.GetShape().Barrel, Unit.GetActorTransform(), Direction * Yaw, 0.f))
			{
				return Direction * Yaw;
			}
		}
		return {};
	}

	/** Turns the turret to Yaw with the inspection aim, then hands the aim back. */
	void ParkTurret(ARTSUnit& Unit, float Yaw)
	{
		Unit.SetAimOverride(FVector2D(Yaw, 0.f));
		for (float Time = 0.f; Time < SettleSeconds; Time += Step)
		{
			StepFrame(Unit, false);
		}
		Unit.SetAimOverride({});
	}

	/** A spot Distance from the unit's middle at World yaw Yaw (degrees), on the ground. */
	FVector AtYaw(float Yaw, float Distance)
	{
		return FRotator(0.f, Yaw, 0.f).Vector() * Distance;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBarrelClearanceTraverseTest, "WorldDivide.Combat.BarrelClearance.Traverse", RTSTests::TestFlags)

bool FRTSBarrelClearanceTraverseTest::RunTest(const FString& Parameters)
{
	using namespace RTSBarrelClearanceTests;
	URTSUnitData* Data = MakeM20(*this);
	if (!Data)
	{
		return false;
	}
	// The turret parked left of the tall wall, the enemy right of it: the short way, the way it would turn, sweeps through it.
	constexpr float ParkedYaw = 80.f;
	constexpr float EnemyYaw = -70.f;
	constexpr float EnemyDistance = 1500.f;
	/** Long enough for the turret to go round the back: 210 degrees at 45 a second, and to fire. */
	constexpr float TraverseRoundSeconds = 10.f;
	const float Short = FMath::FindDeltaAngleDegrees(ParkedYaw, EnemyYaw) >= 0.f ? 1.f : -1.f;

	// Open behind: the turret goes round the back, its barrel out of the wall on every frame, and fires once round.
	{
		const FRTSTestWorld TestWorld;
		ARTSUnit* M20 = SpawnOnGround(TestWorld, Data, 0, FVector::ZeroVector);
		const FBox Box = SpawnBox(TestWorld, M20->GetShape(), TallBoxHeight);
		const FRTSBarrel& Barrel = *M20->GetShape().Barrel;
		const FTransform Pose = M20->GetActorTransform();
		if (!TestTrue(TEXT("The barrel is clear parked and at the enemy"), RTSBarrelClearance::IsClearAt(*TestWorld.World, Barrel, Pose, ParkedYaw, 0.f)
				&& RTSBarrelClearance::IsClearAt(*TestWorld.World, Barrel, Pose, EnemyYaw, 0.f))
			|| !TestFalse(TEXT("The short way sweeps through the wall"),
				RTSBarrelClearance::IsSwingClear(*TestWorld.World, Barrel, Pose, ParkedYaw, EnemyYaw, Short, 0.f))
			|| !TestTrue(TEXT("The long way is clear"),
				RTSBarrelClearance::IsSwingClear(*TestWorld.World, Barrel, Pose, ParkedYaw, EnemyYaw, -Short, 0.f)))
		{
			return false;
		}
		ParkTurret(*M20, ParkedYaw);
		ARTSUnit* Enemy = SpawnOnGround(TestWorld, MakeTarget(), 1, AtYaw(EnemyYaw, EnemyDistance));
		const float Deepest = RunEveryFrame(*M20, { Box }, TraverseRoundSeconds);
		TestTrue(FString::Printf(TEXT("The barrel stayed out of the wall on every frame of the swing (%.1f cm at most)"), Deepest),
			Deepest <= OverlapTolerance);
		TestFalse(TEXT("Round the back, the aim is clear"), M20->IsAimBlocked());
		TestTrue(FString::Printf(TEXT("The turret points at the enemy (%.0f degrees)"), M20->GetTurretYaw()), M20->IsAimedAt(*Enemy));
		TestTrue(TEXT("The gun fired"), Enemy->GetHealthComponent()->GetHealth() < TargetHealth);
	}

	// A wall behind as well: neither way is clear, so the turret holds and the gun holds its fire.
	{
		const FRTSTestWorld TestWorld;
		ARTSUnit* M20 = SpawnOnGround(TestWorld, Data, 0, FVector::ZeroVector);
		const FRTSUnitShape& Shape = M20->GetShape();
		const FBox Front = SpawnBox(TestWorld, Shape, TallBoxHeight);
		const FBox Back(FVector(-Shape.Barrel->GetReach() - BoxBeyondReach, -BoxHalfWidth, 0.f),
			FVector(Shape.Hull.Min.X - BoxFrontGap, BoxHalfWidth, TallBoxHeight));
		SpawnSolid(TestWorld, Back);
		const FRTSBarrel& Barrel = *Shape.Barrel;
		const FTransform Pose = M20->GetActorTransform();
		if (!TestTrue(TEXT("Between the walls the barrel is clear parked and at the enemy"),
				RTSBarrelClearance::IsClearAt(*TestWorld.World, Barrel, Pose, ParkedYaw, 0.f)
				&& RTSBarrelClearance::IsClearAt(*TestWorld.World, Barrel, Pose, EnemyYaw, 0.f))
			|| !TestFalse(TEXT("The long way sweeps through the wall behind"),
				RTSBarrelClearance::IsSwingClear(*TestWorld.World, Barrel, Pose, ParkedYaw, EnemyYaw, -Short, 0.f)))
		{
			return false;
		}
		ParkTurret(*M20, ParkedYaw);
		ARTSUnit* Enemy = SpawnOnGround(TestWorld, MakeTarget(), 1, AtYaw(EnemyYaw, EnemyDistance));
		const float Deepest = RunEveryFrame(*M20, { Front, Back }, TraverseRoundSeconds);
		TestTrue(FString::Printf(TEXT("The barrel stayed out of both walls on every frame (%.1f cm at most)"), Deepest),
			Deepest <= OverlapTolerance);
		TestTrue(TEXT("The M20 targets the enemy"), M20->GetWeaponComponent()->GetTarget() == Enemy);
		TestTrue(TEXT("Its aim is blocked"), M20->IsAimBlocked());
		// Before the enemy is acquired the turret heads home as far as is clear: to the wall's edge, on its own side.
		const TOptional<float> Edge = FindClearYaw(*M20, 1.f);
		TestTrue(FString::Printf(TEXT("The turret held on its side of the wall (%.0f degrees, clear from %.0f to %.0f)"), M20->GetTurretYaw(),
			Edge.Get(0.f), ParkedYaw), Edge && M20->GetTurretYaw() >= *Edge - 1.f && M20->GetTurretYaw() <= ParkedYaw + 1.f);
		TestEqual(TEXT("The gun held its fire"), Enemy->GetHealthComponent()->GetHealth(), TargetHealth);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBarrelClearanceHullTurnTest, "WorldDivide.Combat.BarrelClearance.HullTurn", RTSTests::TestFlags)

bool FRTSBarrelClearanceHullTurnTest::RunTest(const FString& Parameters)
{
	using namespace RTSBarrelClearanceTests;
	URTSUnitData* Data = MakeM20(*this);
	if (!Data)
	{
		return false;
	}
	/** Long enough for the hull to turn a quarter turn and the turret to keep round. */
	constexpr float HullTurnSeconds = 10.f;
	/** How far the hull must have turned toward the enemy, out of the quarter turn. */
	constexpr float MinHullTurn = 30.f;
	constexpr float EnemyBeyondWall = 1000.f;

	// A stationary M20 with a tall wall along its left and the enemy behind it: the aim is blocked, so the hull turns toward
	// the enemy, ticked as in play, while the turret keeps the barrel out of the wall and the gun holds its fire.
	const FRTSTestWorld TestWorld;
	ARTSUnit* M20 = SpawnOnGround(TestWorld, Data, 0, FVector::ZeroVector);
	const FRTSUnitShape& Shape = M20->GetShape();
	const float Reach = Shape.Barrel->GetReach();
	// Clear of the hull however it turns; only the barrel reaches the wall.
	const float Near = FVector2D(Shape.GetHalfLength(), Shape.GetHalfWidth()).Size() + BoxFrontGap;
	if (!TestTrue(FString::Printf(TEXT("The barrel reaches past the hull's turning circle (%.0f cm against %.0f)"), Reach, Near),
		Reach > Near + 50.f))
	{
		return false;
	}
	const FBox Wall(FVector(-Reach - BoxBeyondReach, Near, 0.f), FVector(Reach + BoxBeyondReach, Near + 300.f, TallBoxHeight));
	SpawnSolid(TestWorld, Wall);
	if (!TestFalse(TEXT("The barrel cannot point at the enemy"),
		RTSBarrelClearance::IsClearAt(*TestWorld.World, *Shape.Barrel, M20->GetActorTransform(), 90.f, 0.f)))
	{
		return false;
	}
	ARTSUnit* Enemy = SpawnOnGround(TestWorld, MakeTarget(), 1, FVector(0.f, Near + EnemyBeyondWall, 0.f));
	const float Deepest = RunEveryFrame(*M20, { Wall }, HullTurnSeconds, /*bTick*/ true);
	const float HullYaw = M20->GetActorRotation().Yaw;
	TestTrue(TEXT("The M20 targets the enemy behind the wall"), M20->GetWeaponComponent()->GetTarget() == Enemy);
	TestTrue(TEXT("Its aim is blocked"), M20->IsAimBlocked());
	TestTrue(FString::Printf(TEXT("The hull turned toward the enemy (%.0f degrees)"), HullYaw), HullYaw >= MinHullTurn);
	TestTrue(FString::Printf(TEXT("The M20 stood still (%.1f cm)"), FVector::Dist2D(M20->GetActorLocation(), FVector::ZeroVector)),
		FVector::Dist2D(M20->GetActorLocation(), FVector::ZeroVector) <= 1.f);
	TestEqual(TEXT("The gun held its fire"), Enemy->GetHealthComponent()->GetHealth(), TargetHealth);
	TestTrue(FString::Printf(TEXT("The barrel stayed out of the wall on every frame of the turn (%.1f cm at most)"), Deepest),
		Deepest <= OverlapTolerance);
	return true;
}

#endif
