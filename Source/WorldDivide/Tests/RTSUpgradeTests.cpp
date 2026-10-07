#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Data/RTSWeaponData.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and the production tests have helpers of the same names.
namespace RTSUpgradeTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 0.1f;

	URTSUnitData* MakeUnit(int32 Cost, float BuildTime, URTSWeaponData* Weapon = nullptr)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->Cost = Cost;
		Data->BuildTime = BuildTime;
		Data->Weapon = Weapon;
		return Data;
	}

	URTSWeaponData* MakeWeapon(float Damage)
	{
		URTSWeaponData* Data = NewObject<URTSWeaponData>();
		Data->Range = 800.f;
		Data->Damage = Damage;
		Data->ReloadSeconds = 1.f;
		return Data;
	}

	URTSUpgradeData* MakeUpgrade(int32 Cost, float BuildTime, ERTSUpgradeStat Stat, float Multiplier, const TArray<URTSEntityData*>& Targets = {})
	{
		URTSUpgradeData* Data = NewObject<URTSUpgradeData>();
		Data->Cost = Cost;
		Data->BuildTime = BuildTime;
		Data->Stat = Stat;
		Data->Multiplier = Multiplier;
		Data->Targets.Append(Targets);
		return Data;
	}

	URTSBuildingData* MakeBuilding(int32 PowerOutput = 0, int32 PowerDraw = 0, const TArray<URTSUnitData*>& Units = {},
		const TArray<URTSUpgradeData*>& Upgrades = {})
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(3, 3);
		Data->MaxHealth = 500.f;
		Data->BuildTime = 2.f;
		Data->PowerOutput = PowerOutput;
		Data->PowerDraw = PowerDraw;
		Data->Units.Append(Units);
		Data->Upgrades.Append(Upgrades);
		return Data;
	}

	ARTSBuilding* SpawnCompleted(const FRTSTestWorld& TestWorld, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, TeamId, Location);
		Building->UpdateConstruction(Data->BuildTime);
		return Building;
	}

	void Kill(ARTSBuilding& Building)
	{
		Building.GetHealthComponent()->ApplyDamage(Building.GetHealthComponent()->GetMaxHealth(), nullptr);
	}

	int32 CountUnits(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	/** Steps Building's production for Seconds and returns the last unit it produced, if any. */
	ARTSUnit* RunProduction(UWorld& World, ARTSBuilding& Building, float Seconds)
	{
		TSet<ARTSUnit*> Known;
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			Known.Add(*It);
		}
		for (float Time = Step; Time < Seconds + KINDA_SMALL_NUMBER; Time += Step)
		{
			Building.GetProductionComponent()->UpdateProduction(Step);
		}
		ARTSUnit* Produced = nullptr;
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			if (!Known.Contains(*It))
			{
				Produced = *It;
			}
		}
		return Produced;
	}

	/** Progress Building's front entry makes in one second at the current power. */
	float ProgressPerSecond(ARTSBuilding& Building)
	{
		URTSProductionComponent* Production = Building.GetProductionComponent();
		const float Before = Production->GetProgress();
		for (float Time = 0.f; Time < 1.f - KINDA_SMALL_NUMBER; Time += Step)
		{
			Production->UpdateProduction(Step);
		}
		return Production->GetProgress() - Before;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUpgradeResearchOnceTest, "WorldDivide.Upgrades.ResearchOnce", TestFlags)

	bool FRTSUpgradeResearchOnceTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 1000);
		URTSUpgradeData* Drill = MakeUpgrade(300, 2.f, ERTSUpgradeStat::WeaponDamage, 1.5f);
		URTSUpgradeData* Foreign = MakeUpgrade(100, 1.f, ERTSUpgradeStat::WeaponDamage, 1.5f);
		URTSBuildingData* BarracksData = MakeBuilding(0, 0, {}, { Drill });

		ARTSBuilding* Site = TestWorld.SpawnBuilding(BarracksData, 0, FVector(0.f, 6000.f, 0.f));
		TestFalse(TEXT("A construction site refuses research"), Site->GetProductionComponent()->Enqueue(*Player, Drill));

		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, BarracksData, 0, FVector::ZeroVector);
		ARTSBuilding* Second = SpawnCompleted(TestWorld, BarracksData, 0, FVector(0.f, 3000.f, 0.f));
		URTSProductionComponent* Production = Barracks->GetProductionComponent();
		TestTrue(TEXT("A building with only upgrades can produce"), Production->CanProduce());
		TestFalse(TEXT("An upgrade not on its list is refused"), Production->Enqueue(*Player, Foreign));

		TestTrue(TEXT("Queue the upgrade"), Production->Enqueue(*Player, Drill));
		TestEqual(TEXT("Queueing charges its cost"), Player->GetCredits(), 700);
		TestFalse(TEXT("It cannot be queued twice at one building"), Production->Enqueue(*Player, Drill));
		TestFalse(TEXT("Nor at another of the player's buildings"), Second->GetProductionComponent()->Enqueue(*Player, Drill));
		TestTrue(TEXT("The queue check covers every own building"), RTSProduction::IsUpgradeQueued(*TestWorld.World, 0, *Drill));
		TestEqual(TEXT("Refusals charge nothing"), Player->GetCredits(), 700);

		// Each player researches for itself.
		ARTSBuilding* EnemyBarracks = SpawnCompleted(TestWorld, BarracksData, 1, FVector(6000.f, 0.f, 0.f));
		TestFalse(TEXT("The enemy has nothing queued"), RTSProduction::IsUpgradeQueued(*TestWorld.World, 1, *Drill));
		TestTrue(TEXT("The enemy can queue it too"), EnemyBarracks->GetProductionComponent()->Enqueue(*Enemy, Drill));
		Kill(*EnemyBarracks);
		TestFalse(TEXT("A destroyed building's queue no longer holds it"), RTSProduction::IsUpgradeQueued(*TestWorld.World, 1, *Drill));

		const int32 UnitsBefore = CountUnits(*TestWorld.World);
		RunProduction(*TestWorld.World, *Barracks, 1.f);
		TestFalse(TEXT("Halfway it is not researched yet"), Player->HasUpgrade(Drill));
		RunProduction(*TestWorld.World, *Barracks, 1.f);
		TestTrue(TEXT("After its build time it is researched"), Player->HasUpgrade(Drill));
		TestEqual(TEXT("The queue is empty"), Production->GetQueue().Num(), 0);
		TestEqual(TEXT("Research spawns no unit"), CountUnits(*TestWorld.World), UnitsBefore);
		TestFalse(TEXT("The enemy did not get it"), Enemy->HasUpgrade(Drill));

		FRTSCommand Produce;
		Produce.Type = ERTSCommandType::Produce;
		Produce.TargetActor = Second;
		Produce.EntityData = Drill;
		TestFalse(TEXT("A researched upgrade cannot be queued again"), RTSCommands::Produce(*Player, Produce));
		TestFalse(TEXT("Nor at the building that researched it"), Production->Enqueue(*Player, Drill));
		TestEqual(TEXT("It was charged once"), Player->GetCredits(), 700);
		TestFalse(TEXT("Completing it twice does nothing"), Player->CompleteUpgrade(*Drill));
		TestEqual(TEXT("The player holds it once"), Player->GetCompletedUpgrades().Num(), 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUpgradeCancelTest, "WorldDivide.Upgrades.Cancel", TestFlags)

	bool FRTSUpgradeCancelTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		URTSUnitData* Rifle = MakeUnit(100, 2.f);
		URTSUpgradeData* Drill = MakeUpgrade(300, 4.f, ERTSUpgradeStat::WeaponDamage, 1.5f);
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, MakeBuilding(0, 0, { Rifle }, { Drill }), 0, FVector::ZeroVector);
		URTSProductionComponent* Production = Barracks->GetProductionComponent();

		TestTrue(TEXT("Queue the upgrade"), Production->Enqueue(*Player, Drill));
		TestTrue(TEXT("And a rifleman behind it"), Production->Enqueue(*Player, Rifle));
		TestEqual(TEXT("Both are charged"), Player->GetCredits(), 600);
		RunProduction(*TestWorld.World, *Barracks, 1.f);
		TestTrue(TEXT("The upgrade is a quarter done"), FMath::IsNearlyEqual(Production->GetProgress(), 0.25f, 0.01f));

		TestTrue(TEXT("Cancel the upgrade"), Production->CancelLast(*Player, Drill));
		TestEqual(TEXT("Its cost is refunded in full"), Player->GetCredits(), 900);
		TestEqual(TEXT("Its progress is gone"), Production->GetProgress(), 0.f);
		TestTrue(TEXT("The rifleman moved to the front"), Production->GetQueue().Num() == 1 && Production->GetQueue()[0] == Rifle);
		TestFalse(TEXT("Cancelling does not research it"), Player->HasUpgrade(Drill));
		TestFalse(TEXT("Nothing is left to cancel"), Production->CancelLast(*Player, Drill));

		TestTrue(TEXT("A cancelled upgrade can be queued again"), Production->Enqueue(*Player, Drill));
		TestEqual(TEXT("And is charged again"), Player->GetCredits(), 600);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUpgradeLowPowerTest, "WorldDivide.Upgrades.LowPower", TestFlags)

	bool FRTSUpgradeLowPowerTest::RunTest(const FString& Parameters)
	{
		constexpr float BuildTime = 10.f;

		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		URTSFactionData* Faction = NewObject<URTSFactionData>();
		Faction->bUsesPower = true;
		Player->SetFaction(Faction);

		URTSUpgradeData* Drill = MakeUpgrade(300, BuildTime, ERTSUpgradeStat::WeaponDamage, 1.5f);
		ARTSBuilding* Plant = SpawnCompleted(TestWorld, MakeBuilding(5, 0), 0, FVector(0.f, -3000.f, 0.f));
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, MakeBuilding(0, 2, {}, { Drill }), 0, FVector::ZeroVector);
		Barracks->GetProductionComponent()->Enqueue(*Player, Drill);

		TestTrue(TEXT("Full power researches at the build time"), FMath::IsNearlyEqual(ProgressPerSecond(*Barracks), 1.f / BuildTime, 0.005f));

		Kill(*Plant);
		TestTrue(TEXT("Without the plant the player is low on power"), Player->IsLowPower());
		const float LowProgress = ProgressPerSecond(*Barracks);
		TestTrue(FString::Printf(TEXT("Low power researches at two thirds of the speed (%.3f per second)"), LowProgress),
			FMath::IsNearlyEqual(LowProgress, ARTSPlayerState::LowPowerProductionRate / BuildTime, 0.005f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUpgradeMultiplierTest, "WorldDivide.Upgrades.Multiplier", TestFlags)

	bool FRTSUpgradeMultiplierTest::RunTest(const FString& Parameters)
	{
		URTSUnitData* Rifle = MakeUnit(100, 2.f);
		URTSUnitData* Tank = MakeUnit(500, 5.f);
		TArray<TObjectPtr<URTSUpgradeData>> Upgrades;
		TestEqual(TEXT("No upgrades multiply by one"), URTSUpgradeData::GetMultiplier(Upgrades, *Rifle, ERTSUpgradeStat::WeaponDamage), 1.f);

		Upgrades.Add(MakeUpgrade(100, 1.f, ERTSUpgradeStat::WeaponDamage, 1.5f, { Rifle }));
		Upgrades.Add(MakeUpgrade(100, 1.f, ERTSUpgradeStat::WeaponDamage, 2.f, { Rifle, Tank }));
		Upgrades.Add(MakeUpgrade(100, 1.f, ERTSUpgradeStat::MaxHealth, 1.25f, { Rifle }));
		Upgrades.Add(nullptr);
		TestEqual(TEXT("Upgrades on one stat multiply together"), URTSUpgradeData::GetMultiplier(Upgrades, *Rifle, ERTSUpgradeStat::WeaponDamage), 3.f);
		TestEqual(TEXT("Only upgrades listing the data count"), URTSUpgradeData::GetMultiplier(Upgrades, *Tank, ERTSUpgradeStat::WeaponDamage), 2.f);
		TestEqual(TEXT("Each stat counts on its own"), URTSUpgradeData::GetMultiplier(Upgrades, *Rifle, ERTSUpgradeStat::MaxHealth), 1.25f);
		TestEqual(TEXT("An untouched stat stays at one"), URTSUpgradeData::GetMultiplier(Upgrades, *Tank, ERTSUpgradeStat::MaxHealth), 1.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUpgradeEffectTest, "WorldDivide.Upgrades.Effect", TestFlags)

	bool FRTSUpgradeEffectTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 5000);
		TestWorld.SpawnPlayer(1, 0);
		URTSWeaponData* RifleGun = MakeWeapon(10.f);
		URTSUnitData* Rifle = MakeUnit(100, 1.f, RifleGun);
		URTSUnitData* Other = MakeUnit(100, 1.f, RifleGun);
		URTSUnitData* Target = MakeUnit(100, 1.f);
		Target->MaxHealth = 1000.f;
		URTSUpgradeData* Drill = MakeUpgrade(300, 1.f, ERTSUpgradeStat::WeaponDamage, 1.5f, { Rifle });
		URTSBuildingData* BarracksData = MakeBuilding(0, 0, { Rifle });
		URTSUpgradeData* Armour = MakeUpgrade(300, 1.f, ERTSUpgradeStat::MaxHealth, 2.f, { Rifle, BarracksData });
		BarracksData->Upgrades.Append({ Drill, Armour });
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, BarracksData, 0, FVector::ZeroVector);
		URTSProductionComponent* Production = Barracks->GetProductionComponent();

		ARTSUnit* Existing = TestWorld.SpawnUnit(Rifle, 0, FVector(0.f, 3000.f, 0.f));
		ARTSUnit* Untouched = TestWorld.SpawnUnit(Other, 0, FVector(0.f, 4000.f, 0.f));
		ARTSUnit* EnemyRifle = TestWorld.SpawnUnit(Rifle, 1, FVector(0.f, 5000.f, 0.f));
		ARTSUnit* Dummy = TestWorld.SpawnUnit(Target, 1, FVector(300.f, 3000.f, 0.f));
		TestEqual(TEXT("Before research a rifle deals its base damage"), Existing->GetWeaponComponent()->GetDamage(), 10.f);

		Production->Enqueue(*Player, Drill);
		RunProduction(*TestWorld.World, *Barracks, 1.f);
		TestTrue(TEXT("The damage upgrade is researched"), Player->HasUpgrade(Drill));
		TestEqual(TEXT("A rifle already in the field deals more"), Existing->GetWeaponComponent()->GetDamage(), 15.f);
		TestEqual(TEXT("A unit the upgrade does not list is unchanged"), Untouched->GetWeaponComponent()->GetDamage(), 10.f);
		TestEqual(TEXT("The enemy's rifles are unchanged"), EnemyRifle->GetWeaponComponent()->GetDamage(), 10.f);
		Existing->GetWeaponComponent()->UpdateWeapon(0.f);
		TestEqual(TEXT("Its shot does the upgraded damage"), Dummy->GetHealthComponent()->GetHealth(), 985.f);

		Production->Enqueue(*Player, Rifle);
		ARTSUnit* Fresh = RunProduction(*TestWorld.World, *Barracks, 1.f);
		if (TestNotNull(TEXT("A rifle is produced"), Fresh))
		{
			TestEqual(TEXT("A rifle produced after research deals more"), Fresh->GetWeaponComponent()->GetDamage(), 15.f);
		}

		// Health keeps its share: a rifle at half health stays at half of the new maximum.
		Existing->GetHealthComponent()->ApplyDamage(50.f, nullptr);
		Production->Enqueue(*Player, Armour);
		RunProduction(*TestWorld.World, *Barracks, 1.f);
		TestTrue(TEXT("The health upgrade is researched"), Player->HasUpgrade(Armour));
		TestEqual(TEXT("The rifle's maximum health doubles"), Existing->GetHealthComponent()->GetMaxHealth(), 200.f);
		TestEqual(TEXT("Its health keeps its share"), Existing->GetHealthComponent()->GetHealth(), 100.f);
		TestEqual(TEXT("A listed building's maximum health doubles too"), Barracks->GetHealthComponent()->GetMaxHealth(), 1000.f);
		TestEqual(TEXT("And it stays at full health"), Barracks->GetHealthComponent()->GetHealth(), 1000.f);
		TestEqual(TEXT("The enemy's rifles keep their health"), EnemyRifle->GetHealthComponent()->GetMaxHealth(), 100.f);
		TestEqual(TEXT("The damage upgrade still holds"), Existing->GetWeaponComponent()->GetDamage(), 15.f);

		ARTSUnit* Later = TestWorld.SpawnUnit(Rifle, 0, FVector(0.f, 6000.f, 0.f));
		TestEqual(TEXT("A new rifle starts at the upgraded health"), Later->GetHealthComponent()->GetHealth(), 200.f);
		ARTSBuilding* NewBarracks = TestWorld.SpawnBuilding(BarracksData, 0, FVector(6000.f, 6000.f, 0.f));
		TestEqual(TEXT("A new barracks has the upgraded maximum"), NewBarracks->GetHealthComponent()->GetMaxHealth(), 1000.f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUpgradePrerequisiteTest, "WorldDivide.Upgrades.Prerequisite", TestFlags)

	bool FRTSUpgradePrerequisiteTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 5000);
		ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 5000);
		URTSUpgradeData* Drill = MakeUpgrade(300, 1.f, ERTSUpgradeStat::WeaponDamage, 1.5f);
		URTSUnitData* Grenadier = MakeUnit(200, 1.f);
		Grenadier->Prerequisites.Add(Drill);
		URTSBuildingData* BarracksData = MakeBuilding(0, 0, { Grenadier }, { Drill });
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, BarracksData, 0, FVector::ZeroVector);
		ARTSBuilding* EnemyBarracks = SpawnCompleted(TestWorld, BarracksData, 1, FVector(6000.f, 0.f, 0.f));
		URTSProductionComponent* Production = Barracks->GetProductionComponent();

		TSet<const URTSEntityData*> Owned;
		TestFalse(TEXT("Without the upgrade the unit is unavailable"), RTSProduction::IsAvailable(Owned, *Grenadier));
		Owned.Add(Drill);
		TestTrue(TEXT("With the upgrade owned it is available"), RTSProduction::IsAvailable(Owned, *Grenadier));

		TestTrue(TEXT("Before research the upgrade is missing"), RTSProduction::FindMissingPrerequisite(*TestWorld.World, 0, *Grenadier) == Drill);
		TestFalse(TEXT("The gated unit is refused"), Production->Enqueue(*Player, Grenadier));
		TestTrue(TEXT("Queue the upgrade"), Production->Enqueue(*Player, Drill));
		TestFalse(TEXT("While it is researched the unit is still refused"), Production->Enqueue(*Player, Grenadier));

		RunProduction(*TestWorld.World, *Barracks, 1.f);
		TestTrue(TEXT("The researched upgrade counts as owned"), RTSProduction::GatherOwned(*TestWorld.World, 0).Contains(Drill));
		TestNull(TEXT("Nothing is missing any more"), RTSProduction::FindMissingPrerequisite(*TestWorld.World, 0, *Grenadier));
		TestTrue(TEXT("The gated unit is accepted"), Production->Enqueue(*Player, Grenadier));
		TestFalse(TEXT("The enemy does not own it"), RTSProduction::GatherOwned(*TestWorld.World, 1).Contains(Drill));
		TestFalse(TEXT("So the enemy is still refused"), EnemyBarracks->GetProductionComponent()->Enqueue(*Enemy, Grenadier));
		return true;
	}
}

#endif
