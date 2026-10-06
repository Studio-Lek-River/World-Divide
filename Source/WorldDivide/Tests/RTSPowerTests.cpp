#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
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

	URTSBuildingData* MakePowerBuilding(int32 PowerOutput, int32 PowerDraw, float BuildTime = 2.f)
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(2, 2);
		Data->MaxHealth = 500.f;
		Data->BuildTime = BuildTime;
		Data->PowerOutput = PowerOutput;
		Data->PowerDraw = PowerDraw;
		return Data;
	}

	/** A tower drawing 2 whose gun kills a 100 hp unit in two shots. */
	URTSBuildingData* MakePoweredTower(bool bRequiresPower)
	{
		URTSWeaponData* Gun = NewObject<URTSWeaponData>();
		Gun->Range = 1000.f;
		Gun->Damage = 50.f;
		Gun->ReloadSeconds = 1.f;

		URTSBuildingData* Data = MakePowerBuilding(0, 2);
		Data->Weapon = Gun;
		Data->bRequiresPower = bRequiresPower;
		return Data;
	}

	ARTSBuilding* SpawnCompleted(const FRTSTestWorld& TestWorld, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, TeamId, Location);
		Building->UpdateConstruction(Data->BuildTime);
		return Building;
	}

	URTSFactionData* MakeFaction(bool bUsesPower)
	{
		URTSFactionData* Faction = NewObject<URTSFactionData>();
		Faction->bUsesPower = bUsesPower;
		return Faction;
	}

	/** Steps Tower's weapon for Seconds and returns whether Target lost health. */
	bool FiresAt(ARTSBuilding& Tower, ARTSUnit& Target, float Seconds)
	{
		constexpr float Step = 0.05f;
		const float Before = Target.GetHealthComponent()->GetHealth();
		for (float Time = 0.f; Time < Seconds && IsValid(&Target); Time += Step)
		{
			Tower.GetWeaponComponent()->UpdateWeapon(Step);
		}
		return !IsValid(&Target) || Target.GetHealthComponent()->GetHealth() < Before;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPowerEfficiencyTest, "WorldDivide.Power.Efficiency", TestFlags)

bool FRTSPowerEfficiencyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No grid at all runs at full power"), ARTSPlayerState::ComputePowerEfficiency(0, 0), 1.f);
	TestEqual(TEXT("A surplus runs at full power"), ARTSPlayerState::ComputePowerEfficiency(5, 2), 1.f);
	TestEqual(TEXT("A balanced grid runs at full power"), ARTSPlayerState::ComputePowerEfficiency(5, 5), 1.f);
	TestEqual(TEXT("Half the draw covered is half efficiency"), ARTSPlayerState::ComputePowerEfficiency(5, 10), 0.5f);
	TestEqual(TEXT("Draw without any output is zero"), ARTSPlayerState::ComputePowerEfficiency(0, 4), 0.f);

	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
	ARTSPlayerState* Other = TestWorld.SpawnPlayer(1, 0);
	URTSBuildingData* PlantData = MakePowerBuilding(5, 0);
	URTSBuildingData* ConsumerData = MakePowerBuilding(0, 3);

	ARTSBuilding* Plant = SpawnCompleted(TestWorld, PlantData, 0, FVector::ZeroVector);
	SpawnCompleted(TestWorld, ConsumerData, 0, FVector(1000.f, 0.f, 0.f));
	SpawnCompleted(TestWorld, ConsumerData, 1, FVector(5000.f, 0.f, 0.f));
	TestEqual(TEXT("The plant's output counts"), Player->GetPowerProduced(), 5);
	TestEqual(TEXT("The consumer's draw counts"), Player->GetPowerConsumed(), 3);
	TestEqual(TEXT("Covered draw is full efficiency"), Player->GetPowerEfficiency(), 1.f);
	TestEqual(TEXT("The other team's building counts for that team only"), Other->GetPowerConsumed(), 3);

	ARTSBuilding* Site = TestWorld.SpawnBuilding(ConsumerData, 0, FVector(2000.f, 0.f, 0.f));
	Site->UpdateConstruction(ConsumerData->BuildTime * 0.5f);
	TestEqual(TEXT("A construction site draws nothing yet"), Player->GetPowerConsumed(), 3);
	Site->UpdateConstruction(ConsumerData->BuildTime);
	TestEqual(TEXT("It draws once completed"), Player->GetPowerConsumed(), 6);
	TestEqual(TEXT("Drawing more than produced lowers efficiency"), Player->GetPowerEfficiency(), 5.f / 6.f);
	TestTrue(TEXT("The player is low on power"), Player->IsLowPower());
	TestEqual(TEXT("Low power cuts production by a third"), Player->GetProductionRate(), ARTSPlayerState::LowPowerProductionRate);

	Plant->GetHealthComponent()->ApplyDamage(PlantData->MaxHealth, nullptr);
	TestFalse(TEXT("The plant is destroyed"), IsValid(Plant));
	TestEqual(TEXT("A destroyed plant produces nothing"), Player->GetPowerProduced(), 0);
	TestEqual(TEXT("Without output efficiency is zero"), Player->GetPowerEfficiency(), 0.f);
	TestEqual(TEXT("Production keeps going at the low-power rate"), Player->GetProductionRate(), ARTSPlayerState::LowPowerProductionRate);

	Site->Destroy();
	TestEqual(TEXT("A destroyed consumer draws nothing"), Player->GetPowerConsumed(), 3);

	Player->SetFaction(MakeFaction(false));
	TestEqual(TEXT("A faction without power always runs at full efficiency"), Player->GetPowerEfficiency(), 1.f);
	TestFalse(TEXT("It is never low on power"), Player->IsLowPower());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLowPowerTest, "WorldDivide.Power.LowPower", TestFlags)

bool FRTSLowPowerTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 0.1f;
	constexpr float BuildTime = 4.f;

	URTSUnitData* Infantry = NewObject<URTSUnitData>();
	Infantry->MaxHealth = 100.f;
	URTSBuildingData* PlantData = MakePowerBuilding(5, 0);
	URTSBuildingData* SiteData = MakePowerBuilding(0, 0, BuildTime);

	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
	Player->SetFaction(MakeFaction(true));
	ARTSBuilding* Plant = SpawnCompleted(TestWorld, PlantData, 0, FVector(0.f, -3000.f, 0.f));
	ARTSBuilding* Tower = SpawnCompleted(TestWorld, MakePoweredTower(true), 0, FVector::ZeroVector);
	ARTSBuilding* Unpowered = SpawnCompleted(TestWorld, MakePoweredTower(false), 0, FVector(0.f, 5000.f, 0.f));

	// Construction progress after one second of building, at the current power.
	auto ProgressPerSecond = [&](const FVector& Location)
	{
		ARTSBuilding* Site = TestWorld.SpawnBuilding(SiteData, 0, Location);
		for (float Time = 0.f; Time < 1.f - KINDA_SMALL_NUMBER; Time += Step)
		{
			Site->UpdateConstruction(Step);
		}
		const float Progress = Site->GetConstructionProgress();
		Site->Destroy();
		return Progress;
	};

	TestFalse(TEXT("The plant covers both towers"), Player->IsLowPower());
	TestTrue(TEXT("The powered tower is armed"), Tower->GetWeaponComponent()->IsArmed());
	TestTrue(TEXT("Full power fires"), FiresAt(*Tower, *TestWorld.SpawnUnit(Infantry, 1, FVector(600.f, 0.f, 0.f)), 3.f));
	TestTrue(TEXT("Full power builds at the build time"),
		FMath::IsNearlyEqual(ProgressPerSecond(FVector(3000.f, 0.f, 0.f)), 1.f / BuildTime, 0.01f));

	// Losing the only plant: the powered tower goes dark and construction slows by a third.
	Plant->GetHealthComponent()->ApplyDamage(PlantData->MaxHealth, nullptr);
	TestTrue(TEXT("Without the plant the player is low on power"), Player->IsLowPower());
	TestFalse(TEXT("The powered tower is disarmed"), Tower->GetWeaponComponent()->IsArmed());
	ARTSUnit* Intruder = TestWorld.SpawnUnit(Infantry, 1, FVector(600.f, 0.f, 0.f));
	TestFalse(TEXT("The powered tower holds fire"), FiresAt(*Tower, *Intruder, 3.f));
	TestTrue(TEXT("A tower that needs no power keeps firing"),
		FiresAt(*Unpowered, *TestWorld.SpawnUnit(Infantry, 1, FVector(600.f, 5000.f, 0.f)), 3.f));
	const float LowProgress = ProgressPerSecond(FVector(3000.f, 0.f, 0.f));
	TestTrue(FString::Printf(TEXT("Low power builds at two thirds of the speed (%.3f per second)"), LowProgress),
		FMath::IsNearlyEqual(LowProgress, ARTSPlayerState::LowPowerProductionRate / BuildTime, 0.01f));

	// Rebuilding the plant restores both; the plant itself builds at the low-power rate.
	ARTSBuilding* NewPlant = TestWorld.SpawnBuilding(PlantData, 0, FVector(0.f, -3000.f, 0.f));
	NewPlant->UpdateConstruction(PlantData->BuildTime);
	TestFalse(TEXT("The new plant is not done in its full-power build time"), NewPlant->IsConstructed());
	NewPlant->UpdateConstruction(PlantData->BuildTime);
	TestTrue(TEXT("The new plant completes"), NewPlant->IsConstructed());
	TestFalse(TEXT("The new plant restores power"), Player->IsLowPower());
	TestTrue(TEXT("The powered tower is armed again"), Tower->GetWeaponComponent()->IsArmed());
	TestTrue(TEXT("It fires again"), FiresAt(*Tower, *Intruder, 3.f));
	TestTrue(TEXT("Construction is back to full speed"),
		FMath::IsNearlyEqual(ProgressPerSecond(FVector(3000.f, 0.f, 0.f)), 1.f / BuildTime, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNoPowerFactionTest, "WorldDivide.Power.NoPowerFaction", TestFlags)

bool FRTSNoPowerFactionTest::RunTest(const FString& Parameters)
{
	constexpr float BuildTime = 4.f;

	URTSUnitData* Infantry = NewObject<URTSUnitData>();
	Infantry->MaxHealth = 100.f;

	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
	Player->SetFaction(MakeFaction(false));

	// Draw and no plant at all.
	ARTSBuilding* Tower = SpawnCompleted(TestWorld, MakePoweredTower(true), 0, FVector::ZeroVector);
	TestEqual(TEXT("The draw is still counted"), Player->GetPowerConsumed(), 2);
	TestEqual(TEXT("Efficiency stays full"), Player->GetPowerEfficiency(), 1.f);
	TestTrue(TEXT("The tower is armed"), Tower->GetWeaponComponent()->IsArmed());
	TestTrue(TEXT("The tower fires"), FiresAt(*Tower, *TestWorld.SpawnUnit(Infantry, 1, FVector(600.f, 0.f, 0.f)), 3.f));

	ARTSBuilding* Site = TestWorld.SpawnBuilding(MakePowerBuilding(0, 0, BuildTime), 0, FVector(3000.f, 0.f, 0.f));
	Site->UpdateConstruction(1.f);
	TestTrue(TEXT("Construction runs at full speed"), FMath::IsNearlyEqual(Site->GetConstructionProgress(), 1.f / BuildTime, 0.01f));

	// The same base in a faction that uses power is low on power.
	Player->SetFaction(MakeFaction(true));
	TestTrue(TEXT("A faction with power and no plant is low on power"), Player->IsLowPower());
	TestFalse(TEXT("Its powered tower is disarmed"), Tower->GetWeaponComponent()->IsArmed());
	return true;
}

#endif
