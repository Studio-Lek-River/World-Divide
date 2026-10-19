#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSShotEffect.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSForceFireTurretAimTest, "WorldDivide.Combat.ForceFire.TurretAim", TestFlags)

bool FRTSForceFireTurretAimTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.05f;

	// The tank faces +X and is ordered to fire at a point on the ground a quarter turn to its left.
	URTSWeaponData* Weapon = MakeInstantWeapon();
	Weapon->SplashRadius = 200.f;
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(MakeUnitData(Weapon, true), 0, FVector::ZeroVector);
	const FVector Point(0.f, 500.f, 0.f);
	URTSWeaponComponent* Gun = Tank->GetWeaponComponent();
	Gun->SetOrderedLocation(Point);

	Gun->UpdateWeapon(Step);
	TestFalse(TEXT("The turret still points ahead, away from the point"), Tank->IsAimedAt(Point));
	TestEqual(TEXT("No shot before the turret comes round"), Gun->GetShotsFired(), 0);

	float FirstShotTime = -1.f;
	for (int32 Frame = 1; Frame * Step <= 2.f && FirstShotTime < 0.f; ++Frame)
	{
		Tank->UpdateTurret(Step);
		Gun->UpdateWeapon(Step);
		if (Gun->GetShotsFired() > 0)
		{
			FirstShotTime = Frame * Step;
		}
	}

	const float TraverseTime = 90.f / TurretTurnRate;
	TestTrue(FString::Printf(TEXT("The first shot came at %.2f s, once the turret had traversed (about %.2f s)"), FirstShotTime,
		TraverseTime), FirstShotTime > TraverseTime * 0.8f && FirstShotTime <= TraverseTime + Step);
	TestTrue(FString::Printf(TEXT("The turret points at the point (%.1f degrees)"), Tank->GetTurretYaw()),
		FMath::IsNearlyEqual(Tank->GetTurretYaw(), 90.f, 6.f));
	TestTrue(TEXT("The hull did not turn"), FMath::IsNearlyZero(Tank->GetActorRotation().Yaw));
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

/**
 * The M20's shot event (#217): with the turret turned and the gun raised, the event's muzzle is the gun mesh's Muzzle
 * socket within 1 cm, X along the bore, and the shot's flash starts there.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSShotMuzzleTest, "WorldDivide.Combat.Turret.ShotMuzzle", TestFlags)

bool FRTSShotMuzzleTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.05f;
	constexpr float Tolerance = 1.f;
	URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"));
	if (!TestNotNull(TEXT("DA_TestM20Guardian loads"), M20) || !TestNotNull(TEXT("It has a weapon"), M20->Weapon.Get()))
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	// Ahead and to the left, above the tank, so the turret turns and the gun rises.
	const FVector TargetLocation(1500.f, 1500.f, Tank->GetNavAgentLocation().Z + 500.f);
	ARTSUnit* Enemy = TestWorld.SpawnUnit(MakeUnitData(nullptr, false), 1, TargetLocation);

	UStaticMeshComponent* Gun = nullptr;
	Tank->ForEachComponent<UStaticMeshComponent>(false, [&Gun](UStaticMeshComponent* Component)
	{
		Gun = Component->DoesSocketExist(ARTSUnit::MuzzleSocketName) ? Component : Gun;
	});
	if (!TestNotNull(TEXT("The gun mesh has a Muzzle socket"), Gun))
	{
		return false;
	}

	TArray<FRTSShotEvent> Events;
	TArray<FTransform> Sockets;
	Tank->OnShot().AddLambda([&Events, &Sockets, Gun](const FRTSShotEvent& Event)
	{
		Events.Add(Event);
		Sockets.Add(Gun->GetSocketTransform(ARTSUnit::MuzzleSocketName));
	});
	for (int32 Frame = 0; Frame < 200 && Events.IsEmpty(); ++Frame)
	{
		Tank->UpdateTurret(Step);
		Tank->GetWeaponComponent()->UpdateWeapon(Step);
	}
	if (!TestEqual(TEXT("One shot, one event"), Events.Num(), 1))
	{
		return false;
	}
	const FRTSShotEvent& Event = Events[0];
	TestTrue(FString::Printf(TEXT("The turret turned (%.1f degrees)"), Tank->GetTurretYaw()), Tank->GetTurretYaw() > 30.f);
	TestTrue(FString::Printf(TEXT("The gun rose (%.1f degrees)"), Tank->GetGunPitch()), Tank->GetGunPitch() > 5.f);
	TestTrue(TEXT("The event carries the weapon"), Event.Weapon == M20->Weapon);
	const float Distance = FVector::Dist(Event.Muzzle.GetLocation(), Sockets[0].GetLocation());
	TestTrue(FString::Printf(TEXT("The muzzle is the socket (%.2f cm off)"), Distance), Distance <= Tolerance);
	const FVector Bore = Event.Muzzle.GetUnitAxis(EAxis::X);
	TestTrue(TEXT("Its X runs along the bore"), FVector::DotProduct(Bore, Sockets[0].GetUnitAxis(EAxis::X)) > 0.9999f);
	TestTrue(FString::Printf(TEXT("The bore points up by the gun's pitch (%.1f degrees)"), Bore.Rotation().Pitch),
		FMath::IsNearlyEqual(Bore.Rotation().Pitch, Tank->GetGunPitch(), 0.5f));
	TestTrue(TEXT("The shot leaves toward the enemy"), FVector::DotProduct(Event.Direction,
		(Enemy->GetActorLocation() - Event.Muzzle.GetLocation()).GetSafeNormal()) > 0.95f);
	TestTrue(TEXT("The direction is a unit vector"), Event.Direction.IsNormalized());

	TActorIterator<ARTSShotEffect> Effect(TestWorld.World);
	if (TestTrue(TEXT("The shot is shown"), static_cast<bool>(Effect)))
	{
		const float FlashDistance = FVector::Dist(Effect->GetGlows()[0]->GetComponentLocation(), Event.Muzzle.GetLocation());
		TestTrue(FString::Printf(TEXT("Its flash starts at the muzzle (%.2f cm off)"), FlashDistance), FlashDistance <= Tolerance);
	}
	return true;
}

#endif
