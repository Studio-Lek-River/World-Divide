#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr float TurretTurnRate = 90.f;

	URTSUnitData* MakeUnitData(URTSWeaponData* Weapon, bool bTurret)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->Weapon = Weapon;
		if (bTurret)
		{
			Data->TurretMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			Data->TurretTurnRate = TurretTurnRate;
			Data->MuzzleOffset = FVector(100.f, 0.f, 0.f);
		}
		return Data;
	}

	/** Hits the moment it fires, so the target's health tells when the first shot went off. */
	URTSWeaponData* MakeInstantWeapon()
	{
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = 1000.f;
		Weapon->Damage = 10.f;
		Weapon->ReloadSeconds = 0.1f;
		return Weapon;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTurretAimTest, "WorldDivide.Combat.Turret.Aim", TestFlags)

bool FRTSTurretAimTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.05f;

	// The tank faces +X; the enemy stands a quarter turn to its left, and has no weapon of its own.
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(MakeUnitData(MakeInstantWeapon(), true), 0, FVector::ZeroVector);
	ARTSUnit* Enemy = TestWorld.SpawnUnit(MakeUnitData(nullptr, false), 1, FVector(0.f, 500.f, 0.f));
	URTSHealthComponent* EnemyHealth = Enemy->GetHealthComponent();

	TestTrue(TEXT("The data gives the tank a turret"), Tank->HasTurret());
	Tank->GetWeaponComponent()->UpdateWeapon(Step);
	TestTrue(TEXT("The tank picks the enemy as its target"), Tank->GetWeaponComponent()->GetTarget() == Enemy);
	TestFalse(TEXT("The turret still points ahead, away from the enemy"), Tank->IsAimedAt(*Enemy));
	TestEqual(TEXT("No shot before the turret comes round"), EnemyHealth->GetHealth(), 100.f);

	float FirstShotTime = -1.f;
	for (int32 Frame = 1; Frame * Step <= 2.f && FirstShotTime < 0.f; ++Frame)
	{
		Tank->UpdateTurret(Step);
		Tank->GetWeaponComponent()->UpdateWeapon(Step);
		if (EnemyHealth->GetHealth() < 100.f)
		{
			FirstShotTime = Frame * Step;
		}
	}

	// A quarter turn at the turn rate, less what the aim tolerance lets off.
	const float TraverseTime = 90.f / TurretTurnRate;
	TestTrue(FString::Printf(TEXT("The first shot came at %.2f s, once the turret had traversed (about %.2f s)"), FirstShotTime,
		TraverseTime), FirstShotTime > TraverseTime * 0.8f && FirstShotTime <= TraverseTime + Step);
	TestTrue(FString::Printf(TEXT("The turret points at the enemy (%.1f degrees)"), Tank->GetTurretYaw()),
		FMath::IsNearlyEqual(Tank->GetTurretYaw(), 90.f, 6.f));
	TestTrue(TEXT("The hull did not turn"), FMath::IsNearlyZero(Tank->GetActorRotation().Yaw));
	TestTrue(TEXT("Shots leave from the barrel tip, out toward the enemy"), Tank->GetMuzzleLocation().Y > 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTurretlessTest, "WorldDivide.Combat.Turret.None", TestFlags)

bool FRTSTurretlessTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeUnitData(MakeInstantWeapon(), false), 0, FVector::ZeroVector);
	ARTSUnit* Enemy = TestWorld.SpawnUnit(MakeUnitData(nullptr, false), 1, FVector(0.f, 500.f, 0.f));

	TestFalse(TEXT("No turret without a turret mesh"), Unit->HasTurret());
	TestTrue(TEXT("A unit without a turret counts as aimed, since the whole unit turns"), Unit->IsAimedAt(*Enemy));
	Unit->GetWeaponComponent()->UpdateWeapon(0.05f);
	Unit->UpdateTurret(0.05f);
	TestEqual(TEXT("It fires straight away, as before"), Enemy->GetHealthComponent()->GetHealth(), 90.f);
	TestEqual(TEXT("Its turret yaw stays zero"), Unit->GetTurretYaw(), 0.f);
	return true;
}

#endif
