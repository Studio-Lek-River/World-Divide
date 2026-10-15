#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Vehicles built from parts: a gun that elevates on the turret, add-ons on the hull, turret and gun, and road wheels that
 * roll with the ground. The parts are engine shapes; the real M20 Guardian is checked against its export in RTSArtTests.
 */
namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const FVector TurretOffset(0.f, 0.f, 100.f);
	const FVector GunOffset(50.f, 0.f, 20.f);
	const FVector GunMuzzleOffset(300.f, 0.f, 0.f);
	const FVector StationOffset(-20.f, 40.f, 60.f);
	const FVector SightOffset(100.f, 0.f, 10.f);
	const FVector LeftWheelOffset(100.f, -150.f, 50.f);
	const FVector RightWheelOffset(100.f, 150.f, 50.f);

	/** Parts in the order MakeVehicleData lists them. */
	enum EPart : int32 { Station, Sight, LeftWheel, RightWheel, LeftRearWheel };

	UStaticMesh* LoadShape(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	URTSUnitData* MakeVehicleData(URTSWeaponData* Weapon = nullptr)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->Weapon = Weapon;
		Data->SelectionRadius = 200.f;
		Data->Mesh = LoadShape(TEXT("Cube"));
		Data->TurretMesh = LoadShape(TEXT("Cube"));
		Data->TurretOffset = TurretOffset;
		Data->TurretTurnRate = 90.f;
		Data->GunMesh = LoadShape(TEXT("Cylinder"));
		Data->GunOffset = GunOffset;
		Data->GunMuzzleOffset = GunMuzzleOffset;
		Data->GunMinPitch = -10.f;
		Data->GunMaxPitch = 20.f;
		Data->GunElevationRate = 30.f;

		auto AddPart = [Data](ERTSVehiclePartMount Mount, const FVector& Offset, const FRotator& Rotation, bool bWheel)
		{
			FRTSVehiclePart& Part = Data->Parts.AddDefaulted_GetRef();
			Part.Mesh = LoadShape(bWheel ? TEXT("Cylinder") : TEXT("Cube"));
			Part.MountedOn = Mount;
			Part.Offset = Offset;
			Part.Rotation = Rotation;
			Part.bWheel = bWheel;
		};
		AddPart(ERTSVehiclePartMount::Turret, StationOffset, FRotator::ZeroRotator, false);
		AddPart(ERTSVehiclePartMount::Gun, SightOffset, FRotator::ZeroRotator, false);
		// The right wheel is the left one turned round, as the M20 exports a single wheel.
		AddPart(ERTSVehiclePartMount::Hull, LeftWheelOffset, FRotator::ZeroRotator, true);
		AddPart(ERTSVehiclePartMount::Hull, RightWheelOffset, FRotator(0.f, 180.f, 0.f), true);
		AddPart(ERTSVehiclePartMount::Hull, LeftWheelOffset - FVector(200.f, 0.f, 0.f), FRotator::ZeroRotator, true);
		return Data;
	}

	URTSWeaponData* MakeWeapon()
	{
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = 3000.f;
		Weapon->Damage = 1.f;
		Weapon->ReloadSeconds = 100.f;
		return Weapon;
	}

	FTransform GetPart(const ARTSUnit& Unit, int32 Part)
	{
		FTransform Transform;
		Unit.GetPartTransform(Part, Transform);
		return Transform;
	}

	/** Where the wheel's top, straight up at rest in the hull's frame, points now. */
	FVector GetWheelTop(const ARTSUnit& Unit, int32 Part)
	{
		const FTransform Transform = GetPart(Unit, Part);
		const FQuat Rest = Part == RightWheel ? FRotator(0.f, 180.f, 0.f).Quaternion() : FQuat::Identity;
		return Transform.GetRotation() * Rest.Inverse() * FVector::UpVector;
	}

	/** Drives the unit by Distance along its heading, then turns it by Yaw, and lets the running gear catch up. */
	void Drive(ARTSUnit& Unit, float Distance, float Yaw = 0.f)
	{
		Unit.SetActorLocation(Unit.GetActorLocation() + Unit.GetActorForwardVector() * Distance);
		Unit.SetActorRotation(Unit.GetActorRotation() + FRotator(0.f, Yaw, 0.f));
		Unit.UpdateRunningGear();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehiclePartsTest, "WorldDivide.Units.VehicleParts.Attach", TestFlags)

bool FRTSVehiclePartsTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeVehicleData(), 0, FVector::ZeroVector);
	TestTrue(TEXT("The data gives the unit a gun"), Unit->HasGun());

	FTransform Unused;
	for (int32 Part = Station; Part <= LeftRearWheel; ++Part)
	{
		TestTrue(*FString::Printf(TEXT("Part %d is placed"), Part), Unit->GetPartTransform(Part, Unused));
	}
	TestFalse(TEXT("No part past the data's"), Unit->GetPartTransform(LeftRearWheel + 1, Unused));

	TArray<UStaticMeshComponent*> Meshes;
	Unit->GetComponents(Meshes);
	// Hull, turret, gun, station and sight, and one instanced component for the three wheels, which share a mesh.
	TestEqual(TEXT("Wheels of one mesh share a component"), Meshes.Num(), 6);
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		TestFalse(*FString::Printf(TEXT("%s does not collide"), *Mesh->GetName()), Mesh->IsCollisionEnabled());
	}

	// The wheels hang off the hull at their offsets; the unit stands unturned at the origin.
	const FVector LeftWheelBefore = GetPart(*Unit, LeftWheel).GetLocation();
	TestTrue(TEXT("The wheels sit at their offsets on the hull"),
		(GetPart(*Unit, LeftRearWheel).GetLocation() - LeftWheelBefore).Equals(FVector(-200.f, 0.f, 0.f), 0.1f)
		&& (GetPart(*Unit, RightWheel).GetLocation() - LeftWheelBefore).Equals(FVector(0.f, 300.f, 0.f), 0.1f)
		&& FMath::IsNearlyEqual(LeftWheelBefore.Y, LeftWheelOffset.Y, 0.1f));
	const FVector StationBefore = GetPart(*Unit, Station).GetLocation();
	const FVector SightBefore = GetPart(*Unit, Sight).GetLocation();
	const FVector MuzzleBefore = Unit->GetMuzzleLocation();
	TestTrue(TEXT("The muzzle is on the gun, ahead of the turret"), MuzzleBefore.X > GunOffset.X + GunMuzzleOffset.X * 0.9f);

	// A quarter turn of the turret and a raised gun.
	Unit->SetAimOverride(FVector2D(90.f, 20.f));
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Unit->UpdateTurret(0.05f);
	}
	TestTrue(FString::Printf(TEXT("The turret turned (%.1f)"), Unit->GetTurretYaw()), FMath::IsNearlyEqual(Unit->GetTurretYaw(), 90.f, 0.1f));
	TestTrue(FString::Printf(TEXT("The gun rose (%.1f)"), Unit->GetGunPitch()), FMath::IsNearlyEqual(Unit->GetGunPitch(), 20.f, 0.1f));

	// A quarter turn right takes the station from the turret's right (+Y) round to its back (-X).
	const FVector StationAfter = GetPart(*Unit, Station).GetLocation();
	TestTrue(TEXT("The station turned with the turret"), StationBefore.Y > 0.f && StationAfter.X < StationBefore.X - 20.f
		&& FMath::IsNearlyEqual(StationAfter.Z, StationBefore.Z, 0.1f));
	TestTrue(TEXT("The sight on the gun turned with the turret and rose with the gun"),
		GetPart(*Unit, Sight).GetLocation().Y > SightBefore.Y + 50.f && GetPart(*Unit, Sight).GetLocation().Z > SightBefore.Z);
	const FVector Muzzle = Unit->GetMuzzleLocation();
	TestTrue(TEXT("The muzzle points out to the right and up"), Muzzle.Y > 250.f && Muzzle.Z > MuzzleBefore.Z + 50.f);
	TestTrue(TEXT("The wheels stayed on the hull"), GetPart(*Unit, LeftWheel).GetLocation().Equals(LeftWheelBefore, 0.1f));

	Unit->SetAimOverride({});
	Unit->UpdateTurret(100.f);
	TestTrue(TEXT("Without the override and a target the turret and gun come back to the front"),
		FMath::IsNearlyZero(Unit->GetTurretYaw()) && FMath::IsNearlyZero(Unit->GetGunPitch()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRunningGearTest, "WorldDivide.Units.RunningGear.Spin", TestFlags)

bool FRTSRunningGearTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeVehicleData(), 0, FVector::ZeroVector);
	// The first call only notes where the unit stands.
	Unit->UpdateRunningGear();

	auto Pitch = [Unit](int32 Wheel)
	{
		// Forward of straight up, in the hull's frame: positive once the wheel rolled forward.
		return FVector::DotProduct(GetWheelTop(*Unit, Wheel), Unit->GetActorForwardVector());
	};

	const FTransform Rest = GetPart(*Unit, LeftWheel);
	Unit->UpdateRunningGear();
	TestTrue(TEXT("A unit standing still leaves its wheels alone"), GetPart(*Unit, LeftWheel).Equals(Rest));

	// The cylinder is 50 cm round: 10 cm is about a quarter radian.
	Drive(*Unit, 10.f);
	TestTrue(FString::Printf(TEXT("Driving forward rolls the left wheel's top forward (%.3f)"), Pitch(LeftWheel)), Pitch(LeftWheel) > 0.1f);
	TestTrue(FString::Printf(TEXT("and the right one's, though it is turned round (%.3f)"), Pitch(RightWheel)), Pitch(RightWheel) > 0.1f);
	TestTrue(TEXT("Wheels of one side roll together"), FMath::IsNearlyEqual(Pitch(LeftWheel), Pitch(LeftRearWheel), 0.001f));
	TestTrue(TEXT("The wheels stay where they are on the hull"),
		GetPart(*Unit, LeftWheel).GetLocation().Equals(Rest.GetLocation() + FVector(10.f, 0.f, 0.f), 0.1f));

	Drive(*Unit, -20.f);
	TestTrue(FString::Printf(TEXT("Reversing rolls them back past where they started (%.3f)"), Pitch(LeftWheel)), Pitch(LeftWheel) < -0.1f);

	Drive(*Unit, 10.f);
	TestTrue(TEXT("Back at the start, the wheels are as they were"), FMath::IsNearlyZero(Pitch(LeftWheel), 0.001f));

	// Turning right on the spot drives the left side forward and the right side back.
	Drive(*Unit, 0.f, 5.f);
	TestTrue(FString::Printf(TEXT("A right turn rolls the left wheels forward (%.3f)"), Pitch(LeftWheel)), Pitch(LeftWheel) > 0.1f);
	TestTrue(FString::Printf(TEXT("and the right wheels back (%.3f)"), Pitch(RightWheel)), Pitch(RightWheel) < -0.1f);

	const FTransform Turned = GetPart(*Unit, LeftWheel);
	Unit->UpdateRunningGear();
	TestTrue(TEXT("Once the unit stops, the wheels stop"), GetPart(*Unit, LeftWheel).Equals(Turned));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSGunElevationTest, "WorldDivide.Combat.Turret.GunElevation", TestFlags)

bool FRTSGunElevationTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(MakeVehicleData(MakeWeapon()), 0, FVector::ZeroVector);
	URTSUnitData* TargetData = NewObject<URTSUnitData>();
	TargetData->MaxHealth = 100.f;
	// Ahead and well above the gun, as on a ridge.
	ARTSUnit* High = TestWorld.SpawnUnit(TargetData, 1, FVector(2000.f, 0.f, 600.f));

	Tank->GetWeaponComponent()->UpdateWeapon(0.05f);
	TestTrue(TEXT("The tank picks the target"), Tank->GetWeaponComponent()->GetTarget() == High);
	const float MuzzleBefore = Tank->GetMuzzleLocation().Z;
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Tank->UpdateTurret(0.05f);
	}
	// The trunnion stands about 30 cm below the tank's middle: some 630 cm up over 1950 cm, about 18 degrees, under the limit.
	TestTrue(FString::Printf(TEXT("The gun rose toward the target (%.1f)"), Tank->GetGunPitch()),
		Tank->GetGunPitch() > 15.f && Tank->GetGunPitch() < 19.5f);
	TestTrue(TEXT("The muzzle rose with it"), Tank->GetMuzzleLocation().Z > MuzzleBefore + 20.f);

	// Far above: the gun stops at its elevation limit.
	High->SetActorLocation(FVector(500.f, 0.f, 3000.f));
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Tank->UpdateTurret(0.05f);
	}
	TestEqual(TEXT("The gun stops at its highest"), Tank->GetGunPitch(), 20.f);
	return true;
}

#endif
