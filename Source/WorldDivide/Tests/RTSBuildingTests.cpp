#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	URTSBuildingData* MakeBuildingData(float BuildTime, URTSWeaponData* Weapon)
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(2, 2);
		Data->MaxHealth = 500.f;
		Data->BuildTime = BuildTime;
		Data->Weapon = Weapon;
		return Data;
	}

	URTSWeaponData* MakeWeapon(float Range, float Damage, float ReloadSeconds)
	{
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = Range;
		Weapon->Damage = Damage;
		Weapon->ReloadSeconds = ReloadSeconds;
		return Weapon;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildGridTest, "WorldDivide.Buildings.Grid", TestFlags)

bool FRTSBuildGridTest::RunTest(const FString& Parameters)
{
	const FVector Cursor(149.f, -51.f, 7.f);

	TestEqual(TEXT("Even footprint centres on grid lines"),
		RTSBuildPlacement::SnapToGrid(Cursor, FIntPoint(2, 2)), FVector(100.f, -100.f, 7.f));
	TestEqual(TEXT("Odd footprint centres on cell centres"),
		RTSBuildPlacement::SnapToGrid(Cursor, FIntPoint(3, 1)), FVector(150.f, -50.f, 7.f));
	TestEqual(TEXT("Each axis snaps by its own size"),
		RTSBuildPlacement::SnapToGrid(Cursor, FIntPoint(1, 4)), FVector(150.f, -100.f, 7.f));

	// Whatever the size, the footprint's edges land on grid lines.
	for (const FIntPoint Footprint : { FIntPoint(1, 1), FIntPoint(2, 3), FIntPoint(4, 4), FIntPoint(5, 2) })
	{
		const FVector Snapped = RTSBuildPlacement::SnapToGrid(FVector(1234.f, -987.f, 0.f), Footprint);
		const FVector Corner = Snapped - RTSBuildPlacement::GetFootprintExtent(Footprint);
		TestTrue(FString::Printf(TEXT("%dx%d footprint edges on the grid"), Footprint.X, Footprint.Y),
			FMath::IsNearlyZero(FMath::Fmod(FMath::Abs(Corner.X), RTSBuildPlacement::GridSize))
			&& FMath::IsNearlyZero(FMath::Fmod(FMath::Abs(Corner.Y), RTSBuildPlacement::GridSize)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingConstructionTest, "WorldDivide.Buildings.Construction", TestFlags)

bool FRTSBuildingConstructionTest::RunTest(const FString& Parameters)
{
	constexpr float BuildTime = 4.f;
	constexpr float Step = 0.1f;

	const FRTSTestWorld TestWorld;
	ARTSBuilding* Building = TestWorld.SpawnBuilding(MakeBuildingData(BuildTime, MakeWeapon(1000.f, 10.f, 1.f)), 0, FVector::ZeroVector);

	TestEqual(TEXT("Starts as a construction site"), Building->GetConstructionProgress(), 0.f);
	TestFalse(TEXT("Not constructed when placed"), Building->IsConstructed());
	TestFalse(TEXT("Weapon inert while constructing"), Building->GetWeaponComponent()->IsArmed());
	TestEqual(TEXT("Full health from the data"), Building->GetHealthComponent()->GetHealth(), 500.f);

	float Elapsed = 0.f;
	bool bArmedEarly = false;
	while (!Building->IsConstructed() && Elapsed < BuildTime * 2.f)
	{
		Building->UpdateConstruction(Step);
		Elapsed += Step;
		bArmedEarly |= !Building->IsConstructed() && Building->GetWeaponComponent()->IsArmed();
		if (FMath::IsNearlyEqual(Elapsed, BuildTime * 0.5f, Step * 0.5f))
		{
			TestTrue(FString::Printf(TEXT("Halfway after half the build time (%.2f)"), Building->GetConstructionProgress()),
				FMath::IsNearlyEqual(Building->GetConstructionProgress(), 0.5f, 0.05f));
		}
	}

	TestTrue(TEXT("Constructed"), Building->IsConstructed());
	TestTrue(FString::Printf(TEXT("Took %.2f s, expected %.2f s"), Elapsed, BuildTime),
		FMath::Abs(Elapsed - BuildTime) <= Step + KINDA_SMALL_NUMBER);
	TestFalse(TEXT("Never armed before completion"), bArmedEarly);
	TestTrue(TEXT("Weapon armed once complete"), Building->GetWeaponComponent()->IsArmed());

	Building->UpdateConstruction(Step);
	TestEqual(TEXT("Progress stays at 1"), Building->GetConstructionProgress(), 1.f);

	ARTSBuilding* Instant = TestWorld.SpawnBuilding(MakeBuildingData(0.f, nullptr), 0, FVector(1000.f, 0.f, 0.f));
	Instant->UpdateConstruction(Step);
	TestTrue(TEXT("Zero build time completes on the first update"), Instant->IsConstructed());
	TestFalse(TEXT("Unarmed building stays unarmed"), Instant->GetWeaponComponent()->IsArmed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTowerTest, "WorldDivide.Buildings.Tower", TestFlags)

bool FRTSTowerTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.05f;
	constexpr float Duration = 5.f;

	URTSWeaponData* Gun = MakeWeapon(1000.f, 50.f, 1.f);
	URTSUnitData* Infantry = NewObject<URTSUnitData>();
	Infantry->MaxHealth = 100.f;

	const FRTSTestWorld TestWorld;
	ARTSBuilding* Tower = TestWorld.SpawnBuilding(MakeBuildingData(3.f, Gun), 0, FVector::ZeroVector);
	Tower->UpdateConstruction(3.f);
	ARTSBuilding* Unfinished = TestWorld.SpawnBuilding(MakeBuildingData(3.f, Gun), 0, FVector(0.f, 5000.f, 0.f));

	ARTSUnit* EnemyInRange = TestWorld.SpawnUnit(Infantry, 1, FVector(600.f, 0.f, 0.f));
	ARTSUnit* EnemyOutOfRange = TestWorld.SpawnUnit(Infantry, 1, FVector(2000.f, 0.f, 0.f));
	ARTSUnit* Friendly = TestWorld.SpawnUnit(Infantry, 0, FVector(300.f, 0.f, 0.f));
	ARTSUnit* EnemyAtUnfinished = TestWorld.SpawnUnit(Infantry, 1, FVector(0.f, 5300.f, 0.f));

	TestTrue(TEXT("Completed tower is armed"), Tower->GetWeaponComponent()->IsArmed());

	for (float Time = 0.f; Time < Duration; Time += Step)
	{
		Tower->GetWeaponComponent()->UpdateWeapon(Step);
		Unfinished->GetWeaponComponent()->UpdateWeapon(Step);
	}

	TestFalse(TEXT("Enemy in range killed"), IsValid(EnemyInRange));
	TestTrue(TEXT("Enemy out of range untouched"),
		IsValid(EnemyOutOfRange) && EnemyOutOfRange->GetHealthComponent()->GetHealth() == 100.f);
	TestTrue(TEXT("Friendly unit untouched"), IsValid(Friendly) && Friendly->GetHealthComponent()->GetHealth() == 100.f);
	TestTrue(TEXT("Unfinished tower never fires"),
		IsValid(EnemyAtUnfinished) && EnemyAtUnfinished->GetHealthComponent()->GetHealth() == 100.f);
	return true;
}

#endif
