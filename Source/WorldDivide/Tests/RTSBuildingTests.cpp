#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSBuilderData.h"
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

	// Quarter turns swap a footprint's sides on odd turns, and snapping follows the turned footprint.
	TestEqual(TEXT("No turn keeps the footprint"), RTSBuildPlacement::GetRotatedFootprint(FIntPoint(4, 8), 0), FIntPoint(4, 8));
	TestEqual(TEXT("A quarter turn swaps its sides"), RTSBuildPlacement::GetRotatedFootprint(FIntPoint(4, 8), 1), FIntPoint(8, 4));
	TestEqual(TEXT("A half turn keeps them"), RTSBuildPlacement::GetRotatedFootprint(FIntPoint(3, 2), 2), FIntPoint(3, 2));
	TestEqual(TEXT("Three quarter turns swap them"), RTSBuildPlacement::GetRotatedFootprint(FIntPoint(3, 2), 3), FIntPoint(2, 3));
	TestEqual(TEXT("A turned 3x2 snaps as a 2x3"),
		RTSBuildPlacement::SnapToGrid(Cursor, RTSBuildPlacement::GetRotatedFootprint(FIntPoint(3, 2), 1)), FVector(100.f, -50.f, 7.f));
	TestEqual(TEXT("Yaw of a quarter turn"), RTSBuildPlacement::GetYaw(1), 90.f);
	TestEqual(TEXT("Turns of a yaw, any sign"), RTSBuildPlacement::GetQuarterTurns(-90.f), 3);
	TestEqual(TEXT("Turns of a full turn"), RTSBuildPlacement::GetQuarterTurns(360.f), 0);
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

/** A defence outranges a standard tank by a quarter, so tanks take fire while they close in (setup_testmap.py). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTowerOutrangesTankTest, "WorldDivide.Buildings.TowerOutrangesTank", TestFlags)

bool FRTSTowerOutrangesTankTest::RunTest(const FString& Parameters)
{
	const URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TEXT("/Game/Data/Buildings/DA_TestTower.DA_TestTower"));
	const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank"));
	if (!TestNotNull(TEXT("The test tower loads"), Tower) || !TestNotNull(TEXT("The test tank loads"), Tank)
		|| !TestNotNull(TEXT("The tower is armed"), Tower->Weapon.Get()) || !TestNotNull(TEXT("The tank is armed"), Tank->Weapon.Get()))
	{
		return false;
	}
	TestEqual(TEXT("The tower's range is 1.25 times the tank's"), Tower->Weapon->Range, Tank->Weapon->Range * 1.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingUnderFireTest, "WorldDivide.Buildings.UnderFire", TestFlags)

bool FRTSBuildingUnderFireTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.05f;
	constexpr float Range = 300.f;

	URTSUnitData* Gunner = NewObject<URTSUnitData>();
	Gunner->MaxHealth = 100.f;
	Gunner->Weapon = MakeWeapon(Range, 25.f, 1.f);

	// A 4x4 site that never finishes: its edges lie 200 cm from its centre.
	URTSBuildingData* Site = MakeBuildingData(10.f, nullptr);
	Site->Footprint = FIntPoint(4, 4);
	Site->MaxHealth = 100.f;

	const FRTSTestWorld TestWorld;
	ARTSBuilding* Enemy = TestWorld.SpawnBuilding(Site, 1, FVector::ZeroVector);
	ARTSBuilding* Friendly = TestWorld.SpawnBuilding(Site, 0, FVector(900.f, 0.f, 0.f));
	ARTSUnit* Unit = TestWorld.SpawnUnit(Gunner, 0, FVector(450.f, 0.f, 0.f));
	URTSWeaponComponent* Weapon = Unit->GetWeaponComponent();

	TestFalse(TEXT("The enemy building is still under construction"), Enemy->IsConstructed());
	TestTrue(TEXT("Range reaches the footprint edge although the centre is out of range"),
		Weapon->IsInRange(Enemy) && FVector::Dist2D(Unit->GetActorLocation(), Enemy->GetActorLocation()) > Range);
	TestTrue(TEXT("The friendly building is just as close"), Weapon->IsInRange(Friendly));
	TestFalse(TEXT("Friendly buildings are not targets"), Weapon->CanTarget(Friendly));

	// Four shots, the first immediate.
	float Elapsed = 0.f;
	for (int32 Frame = 0; IsValid(Enemy) && Elapsed < 5.f; ++Frame, Elapsed += Step)
	{
		Weapon->UpdateWeapon(Step);
		if (Frame == 0)
		{
			TestTrue(TEXT("Idle unit acquired the enemy building on its own"), Weapon->GetTarget() == Enemy);
		}
	}

	TestFalse(FString::Printf(TEXT("Enemy construction site destroyed (after %.2f s)"), Elapsed), IsValid(Enemy));
	TestTrue(TEXT("Friendly building untouched"), IsValid(Friendly) && Friendly->GetHealthComponent()->GetHealth() == 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPreferUnitsTest, "WorldDivide.Buildings.PreferUnits", TestFlags)

bool FRTSPreferUnitsTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.05f;

	URTSWeaponData* Gun = MakeWeapon(1000.f, 50.f, 1.f);
	URTSUnitData* Gunner = NewObject<URTSUnitData>();
	Gunner->MaxHealth = 100.f;
	Gunner->Weapon = Gun;
	URTSUnitData* Infantry = NewObject<URTSUnitData>();
	Infantry->MaxHealth = 100.f;
	URTSBuildingData* Depot = MakeBuildingData(0.f, nullptr);

	// The enemy building is nearer than the enemy unit, both well in range.
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(Gunner, 0, FVector::ZeroVector);
	ARTSBuilding* Building = TestWorld.SpawnBuilding(Depot, 1, FVector(400.f, 0.f, 0.f));
	ARTSUnit* Enemy = TestWorld.SpawnUnit(Infantry, 1, FVector(800.f, 0.f, 0.f));
	URTSWeaponComponent* Weapon = Unit->GetWeaponComponent();

	Weapon->UpdateWeapon(Step);
	TestTrue(TEXT("The enemy unit is preferred over a nearer building"), Weapon->GetTarget() == Enemy);

	for (float Time = Step; IsValid(Enemy) && Time < 3.f; Time += Step)
	{
		Weapon->UpdateWeapon(Step);
	}
	TestFalse(TEXT("Enemy unit killed"), IsValid(Enemy));
	TestEqual(TEXT("The building took no fire while the unit lived"), Building->GetHealthComponent()->GetHealth(), 500.f);

	Weapon->UpdateWeapon(Step);
	TestTrue(TEXT("Fire moves to the building once no unit is in range"), Weapon->GetTarget() == Building);

	// The weapon looks for units again within its search interval of 0.2 s.
	ARTSUnit* Newcomer = TestWorld.SpawnUnit(Infantry, 1, FVector(700.f, 0.f, 0.f));
	for (float Waited = 0.f; Weapon->GetTarget() != Newcomer && Waited < 0.2f - KINDA_SMALL_NUMBER; Waited += Step)
	{
		Weapon->UpdateWeapon(Step);
	}
	TestTrue(TEXT("An enemy unit coming into range pulls fire off the building within 0.2 s"), Weapon->GetTarget() == Newcomer);

	// Towers pick the same way.
	ARTSBuilding* Tower = TestWorld.SpawnBuilding(MakeBuildingData(0.f, Gun), 0, FVector(0.f, 5000.f, 0.f));
	Tower->UpdateConstruction(Step);
	TestTrue(TEXT("Tower is armed"), Tower->GetWeaponComponent()->IsArmed());
	TestWorld.SpawnBuilding(Depot, 1, FVector(0.f, 5400.f, 0.f));
	ARTSUnit* TowerEnemy = TestWorld.SpawnUnit(Infantry, 1, FVector(0.f, 5800.f, 0.f));
	Tower->GetWeaponComponent()->UpdateWeapon(Step);
	TestTrue(TEXT("Tower prefers the enemy unit too"), Tower->GetWeaponComponent()->GetTarget() == TowerEnemy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuilderChargeTest, "WorldDivide.Buildings.BuilderCharge", TestFlags)

bool FRTSBuilderChargeTest::RunTest(const FString& Parameters)
{
	constexpr int32 Cost = 400;

	URTSBuildingData* Depot = MakeBuildingData(2.f, nullptr);
	Depot->Cost = Cost;
	URTSBuilderData* BuilderData = NewObject<URTSBuilderData>();
	BuilderData->MaxHealth = 100.f;
	BuilderData->SelectionRadius = 60.f;
	BuilderData->Buildings.Add(Depot);

	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, Cost * 2);
	// Docked at the east edge of a depot at the origin, whose 2x2 footprint reaches 100 cm out.
	ARTSUnit* Unit = TestWorld.SpawnUnit(BuilderData, 0, FVector(220.f, 0.f, 0.f));
	URTSBuilderComponent* Builder = Unit->GetBuilderComponent();
	auto MakeOrder = [Depot]()
	{
		FRTSCommand Order;
		Order.Type = ERTSCommandType::Build;
		Order.EntityData = Depot;
		Order.TargetLocation = FVector::ZeroVector;
		return Order;
	};

	// Ordering pays at once, before the builder has done anything.
	FRTSCommand Order = MakeOrder();
	TestTrue(TEXT("The builder accepts a building the player can afford"), Builder->ChargeOrder(Order));
	TestEqual(TEXT("The order is charged at once"), Player->GetCredits(), Cost);
	TestEqual(TEXT("The order holds the payment"), Order.PaidCost, Cost);

	// An order dropped before it starts, such as a queued one, gives the money back once.
	Builder->RefundOrder(Order);
	TestEqual(TEXT("Dropping the order refunds it"), Player->GetCredits(), Cost * 2);
	TestEqual(TEXT("The dropped order holds nothing"), Order.PaidCost, 0);
	Builder->RefundOrder(Order);
	TestEqual(TEXT("A refund is paid once"), Player->GetCredits(), Cost * 2);

	// Started, the payment moves to the builder; stopping before the site is placed refunds it.
	FRTSCommand Unpaid = MakeOrder();
	TestFalse(TEXT("An unpaid order does not start"), Builder->StartBuild(Unpaid));
	Order = MakeOrder();
	Builder->ChargeOrder(Order);
	TestTrue(TEXT("A paid order starts"), Builder->StartBuild(Order));
	TestEqual(TEXT("The builder took over the payment"), Order.PaidCost, 0);
	Builder->StopBuild();
	TestEqual(TEXT("Stopping before the site is placed refunds it"), Player->GetCredits(), Cost * 2);

	// Sent aside, the builder hands the payment back to the order instead of refunding it.
	Order = MakeOrder();
	Builder->ChargeOrder(Order);
	Builder->StartBuild(Order);
	Builder->SuspendBuild(Order);
	TestEqual(TEXT("Suspending refunds nothing"), Player->GetCredits(), Cost);
	TestEqual(TEXT("The suspended order holds the payment again"), Order.PaidCost, Cost);
	TestTrue(TEXT("The suspended order starts again"), Builder->StartBuild(Order));
	TestEqual(TEXT("Restarting charges nothing more"), Player->GetCredits(), Cost);

	// The test world has no navmesh, so the spot is refused once the builder stands next to it.
	TestFalse(TEXT("A spot refused on arrival ends the order"), Builder->UpdateBuild(0.1f));
	TestNull(TEXT("Nothing was placed"), Builder->GetSite());
	TestEqual(TEXT("The refused spot was refunded"), Player->GetCredits(), Cost * 2);

	// More than the player has is refused and charges nothing.
	Player->AddCredits(Cost - 1 - Player->GetCredits());
	Order = MakeOrder();
	TestFalse(TEXT("An order the player cannot afford is refused"), Builder->ChargeOrder(Order));
	TestEqual(TEXT("The refused order charged nothing"), Player->GetCredits(), Cost - 1);
	TestEqual(TEXT("The refused order holds nothing"), Order.PaidCost, 0);

	// Resuming an own site was paid when the site was placed.
	FRTSCommand Resume;
	Resume.Type = ERTSCommandType::Build;
	Resume.TargetActor = TestWorld.SpawnBuilding(Depot, 0, FVector(2000.f, 0.f, 0.f));
	TestTrue(TEXT("A resume order is accepted"), Builder->ChargeOrder(Resume));
	TestEqual(TEXT("Resuming a site costs nothing"), Player->GetCredits(), Cost - 1);

	// A builder that dies on its way gives back what it was carrying.
	Player->AddCredits(1);
	Order = MakeOrder();
	Builder->ChargeOrder(Order);
	Builder->StartBuild(Order);
	TestEqual(TEXT("The last order spent everything"), Player->GetCredits(), 0);
	Unit->GetHealthComponent()->ApplyDamage(BuilderData->MaxHealth * 2.f, nullptr);
	TestFalse(TEXT("The builder died"), IsValid(Unit));
	TestEqual(TEXT("Its unplaced building was refunded"), Player->GetCredits(), Cost);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuilderPrerequisitesTest, "WorldDivide.Buildings.Prerequisites", TestFlags)

bool FRTSBuilderPrerequisitesTest::RunTest(const FString& Parameters)
{
	URTSBuildingData* Barracks = MakeBuildingData(2.f, nullptr);
	URTSBuildingData* Factory = MakeBuildingData(2.f, nullptr);
	Factory->Cost = 500;
	Factory->Prerequisites.Add(Barracks);
	URTSBuilderData* BuilderData = NewObject<URTSBuilderData>();
	BuilderData->MaxHealth = 100.f;
	BuilderData->SelectionRadius = 60.f;
	BuilderData->Buildings.Add(Barracks);
	BuilderData->Buildings.Add(Factory);

	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 5000);
	TestWorld.SpawnPlayer(1, 0);
	URTSBuilderComponent* Builder = TestWorld.SpawnUnit(BuilderData, 0, FVector(220.f, 0.f, 0.f))->GetBuilderComponent();
	auto MakeOrder = [Factory]()
	{
		FRTSCommand Order;
		Order.Type = ERTSCommandType::Build;
		Order.EntityData = Factory;
		Order.TargetLocation = FVector::ZeroVector;
		return Order;
	};

	FRTSCommand Order = MakeOrder();
	TestFalse(TEXT("Without a barracks the factory is refused"), Builder->ChargeOrder(Order));
	ARTSBuilding* Site = TestWorld.SpawnBuilding(Barracks, 0, FVector(3000.f, 0.f, 0.f));
	TestFalse(TEXT("A barracks under construction does not count"), Builder->ChargeOrder(Order));
	TestWorld.SpawnBuilding(Barracks, 1, FVector(6000.f, 0.f, 0.f))->UpdateConstruction(Barracks->BuildTime);
	TestFalse(TEXT("The enemy's barracks does not count"), Builder->ChargeOrder(Order));
	TestEqual(TEXT("Refusals charge nothing"), Player->GetCredits(), 5000);

	Site->UpdateConstruction(Barracks->BuildTime);
	TestTrue(TEXT("Once the barracks completes the factory is accepted"), Builder->ChargeOrder(Order));
	TestEqual(TEXT("The accepted order is charged"), Player->GetCredits(), 5000 - Factory->Cost);

	// What is already paid for carries on: losing the barracks only blocks new orders.
	Site->GetHealthComponent()->ApplyDamage(Barracks->MaxHealth * 2.f, nullptr);
	TestFalse(TEXT("The barracks was destroyed"), IsValid(Site));
	FRTSCommand NewOrder = MakeOrder();
	TestFalse(TEXT("Once the barracks is destroyed a new factory is refused"), Builder->ChargeOrder(NewOrder));
	TestTrue(TEXT("The factory already paid for still starts"), Builder->StartBuild(Order));
	TestEqual(TEXT("Nothing more was charged"), Player->GetCredits(), 5000 - Factory->Cost);

	// Resuming an own site needs no prerequisites: it was checked when it was ordered.
	FRTSCommand Resume;
	Resume.Type = ERTSCommandType::Build;
	Resume.TargetActor = TestWorld.SpawnBuilding(Factory, 0, FVector(-3000.f, 0.f, 0.f));
	TestTrue(TEXT("Resuming a factory site is accepted without a barracks"), Builder->ChargeOrder(Resume));
	TestTrue(TEXT("And starts"), Builder->StartBuild(Resume));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildLimitTest, "WorldDivide.Buildings.BuildLimit", TestFlags)

bool FRTSBuildLimitTest::RunTest(const FString& Parameters)
{
	URTSBuildingData* Headquarters = MakeBuildingData(2.f, nullptr);
	Headquarters->Cost = 500;
	Headquarters->MaxCount = 1;
	URTSBuildingData* Barracks = MakeBuildingData(2.f, nullptr);
	URTSBuilderData* BuilderData = NewObject<URTSBuilderData>();
	BuilderData->MaxHealth = 100.f;
	BuilderData->SelectionRadius = 60.f;
	BuilderData->Buildings.Add(Headquarters);

	const FRTSTestWorld TestWorld;
	UWorld& World = *TestWorld.World;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 5000);
	TestWorld.SpawnPlayer(1, 0);
	URTSBuilderComponent* Builder = TestWorld.SpawnUnit(BuilderData, 0, FVector(220.f, 0.f, 0.f))->GetBuilderComponent();
	FRTSCommand Order;
	Order.Type = ERTSCommandType::Build;
	Order.EntityData = Headquarters;
	Order.TargetLocation = FVector::ZeroVector;

	TestFalse(TEXT("Without a headquarters the team is below the limit"), RTSProduction::IsAtBuildLimit(World, 0, *Headquarters));
	TestWorld.SpawnBuilding(Headquarters, 1, FVector(6000.f, 0.f, 0.f))->UpdateConstruction(Headquarters->BuildTime);
	TestFalse(TEXT("The enemy's headquarters does not count"), RTSProduction::IsAtBuildLimit(World, 0, *Headquarters));
	TestTrue(TEXT("The enemy is at its own limit"), RTSProduction::IsAtBuildLimit(World, 1, *Headquarters));

	// A construction site counts as soon as it is placed.
	ARTSBuilding* Site = TestWorld.SpawnBuilding(Headquarters, 0, FVector(3000.f, 0.f, 0.f));
	TestTrue(TEXT("A construction site reaches the limit"), RTSProduction::IsAtBuildLimit(World, 0, *Headquarters));
	TestFalse(TEXT("A second headquarters is refused beside a site"), Builder->ChargeOrder(Order));
	Site->UpdateConstruction(Headquarters->BuildTime);
	TestTrue(TEXT("A completed one stays at the limit"), RTSProduction::IsAtBuildLimit(World, 0, *Headquarters));
	TestFalse(TEXT("A second headquarters is refused beside a completed one"), Builder->ChargeOrder(Order));
	TestEqual(TEXT("Refusals charge nothing"), Player->GetCredits(), 5000);
	TestEqual(TEXT("The refused order holds nothing"), Order.PaidCost, 0);

	// Once it falls the team may build one again.
	Site->GetHealthComponent()->ApplyDamage(Headquarters->MaxHealth * 2.f, nullptr);
	TestFalse(TEXT("The headquarters was destroyed"), IsValid(Site));
	TestFalse(TEXT("Its loss frees the limit"), RTSProduction::IsAtBuildLimit(World, 0, *Headquarters));
	TestTrue(TEXT("A new headquarters is accepted"), Builder->ChargeOrder(Order));
	TestEqual(TEXT("The accepted order is charged"), Player->GetCredits(), 5000 - Headquarters->Cost);

	// Without a limit any number may stand.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestWorld.SpawnBuilding(Barracks, 0, FVector(-3000.f * (Index + 1), 0.f, 0.f))->UpdateConstruction(Barracks->BuildTime);
	}
	TestFalse(TEXT("A building without a limit never reaches one"), RTSProduction::IsAtBuildLimit(World, 0, *Barracks));
	return true;
}

#endif
