#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAIBuildOrderData.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSHarvesterData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Data/RTSWeaponData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Economy/RTSSupplySource.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSAITests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr int32 PlayerTeam = 0;
	constexpr int32 AITeam = 1;

	URTSUnitData* MakeUnit(int32 Cost, URTSWeaponData* Weapon = nullptr)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->Cost = Cost;
		Data->BuildTime = 2.f;
		Data->Weapon = Weapon;
		return Data;
	}

	URTSBuildingData* MakeBuilding(const TArray<URTSUnitData*>& Units = {}, const TArray<URTSUpgradeData*>& Upgrades = {})
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(3, 3);
		Data->MaxHealth = 500.f;
		Data->BuildTime = 2.f;
		Data->Cost = 300;
		Data->Units.Append(Units);
		Data->Upgrades.Append(Upgrades);
		return Data;
	}

	URTSWeaponData* MakeWeapon()
	{
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = 800.f;
		Weapon->Damage = 10.f;
		Weapon->ReloadSeconds = 1.f;
		return Weapon;
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

	int32 CountBuildings(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	FRTSBuildOrderStep MakeStep(URTSEntityData* Entry, int32 Count)
	{
		FRTSBuildOrderStep Step;
		Step.Entry = Entry;
		Step.Count = Count;
		return Step;
	}

	/**
	 * An AI opponent on AITeam with Credits, playing a faction whose build order is BuildOrder. Without a game mode the
	 * controller makes no player state of its own, so it takes the test's.
	 */
	ARTSAIPlayer* SpawnAI(const FRTSTestWorld& TestWorld, URTSAIBuildOrderData* BuildOrder, int32 Credits)
	{
		URTSFactionData* Faction = NewObject<URTSFactionData>();
		Faction->AIBuildOrder = BuildOrder;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(AITeam, Credits);
		Player->SetFaction(Faction);
		ARTSAIPlayer* AI = TestWorld.World->SpawnActor<ARTSAIPlayer>();
		AI->SetPlayerState(Player);
		return AI;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIFindCurrentStepTest, "WorldDivide.AI.FindCurrentStep", TestFlags)

	bool FRTSAIFindCurrentStepTest::RunTest(const FString& Parameters)
	{
		URTSBuildingData* Plant = MakeBuilding();
		URTSBuildingData* Barracks = MakeBuilding();
		URTSUnitData* Infantry = MakeUnit(100);
		TArray<FRTSBuildOrderStep> Steps = { MakeStep(Plant, 1), MakeStep(nullptr, 1), MakeStep(Barracks, 1), MakeStep(Infantry, 3) };
		TMap<const URTSEntityData*, int32> Owned;
		const auto Count = [&Owned](const URTSEntityData& Entry) { return Owned.FindRef(&Entry); };

		TestEqual(TEXT("Nothing owned: the first step"), RTSAI::FindCurrentStep(Steps, Count), 0);
		Owned.Add(Plant, 1);
		TestEqual(TEXT("A met step and an empty one are skipped"), RTSAI::FindCurrentStep(Steps, Count), 2);
		Owned.Add(Barracks, 1);
		Owned.Add(Infantry, 2);
		TestEqual(TEXT("Two of three infantry: still the infantry"), RTSAI::FindCurrentStep(Steps, Count), 3);
		Owned.Add(Infantry, 3);
		TestEqual(TEXT("Every step met"), RTSAI::FindCurrentStep(Steps, Count), INDEX_NONE);
		Owned.Add(Plant, 0);
		TestEqual(TEXT("A lost building comes first again"), RTSAI::FindCurrentStep(Steps, Count), 0);

		// Steps nothing left could work on are passed over (#179), the rest still in order.
		TSet<const URTSEntityData*> OutOfReach;
		const auto CanWorkOn = [&OutOfReach](const URTSEntityData& Entry) { return !OutOfReach.Contains(&Entry); };
		Owned.Add(Infantry, 0);
		TestEqual(TEXT("Every step in reach: the lost building first"), RTSAI::FindCurrentStep(Steps, Count, CanWorkOn), 0);
		OutOfReach.Add(Plant);
		TestEqual(TEXT("A building nothing can build is passed over for the next unmet step"), RTSAI::FindCurrentStep(Steps, Count, CanWorkOn), 3);
		OutOfReach.Add(Infantry);
		TestEqual(TEXT("Every unmet step out of reach"), RTSAI::FindCurrentStep(Steps, Count, CanWorkOn), INDEX_NONE);
		OutOfReach.Reset();
		TestEqual(TEXT("Back in reach, it comes first again"), RTSAI::FindCurrentStep(Steps, Count, CanWorkOn), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIWaveSettingsTest, "WorldDivide.AI.WaveSettings", TestFlags)

	bool FRTSAIWaveSettingsTest::RunTest(const FString& Parameters)
	{
		URTSAIBuildOrderData* BuildOrder = NewObject<URTSAIBuildOrderData>();
		BuildOrder->Waves.Interval = 90.f;
		BuildOrder->Waves.Size = 8;
		BuildOrder->Waves.MinSize = 4;
		FRTSAIWaveScale Easy;
		Easy.IntervalScale = 1.5f;
		Easy.SizeScale = 0.5f;
		FRTSAIWaveScale Hard;
		Hard.IntervalScale = 0.5f;
		Hard.SizeScale = 1.5f;
		BuildOrder->DifficultyScales.Add(ERTSAIDifficulty::Easy, Easy);
		BuildOrder->DifficultyScales.Add(ERTSAIDifficulty::Hard, Hard);

		const FRTSAIWaveSettings Normal = RTSAI::GetWaveSettings(*BuildOrder, ERTSAIDifficulty::Normal);
		TestEqual(TEXT("Normal, left out of the scales, plays the build order's interval"), Normal.Interval, 90.f);
		TestEqual(TEXT("Normal plays the build order's size"), Normal.Size, 8);
		TestEqual(TEXT("Normal plays the build order's minimum"), Normal.MinSize, 4);

		const FRTSAIWaveSettings EasyWaves = RTSAI::GetWaveSettings(*BuildOrder, ERTSAIDifficulty::Easy);
		TestEqual(TEXT("Easy waits longer"), EasyWaves.Interval, 135.f);
		TestEqual(TEXT("Easy sends smaller waves"), EasyWaves.Size, 4);
		TestEqual(TEXT("Easy sends a smaller minimum"), EasyWaves.MinSize, 2);

		const FRTSAIWaveSettings HardWaves = RTSAI::GetWaveSettings(*BuildOrder, ERTSAIDifficulty::Hard);
		TestEqual(TEXT("Hard comes sooner"), HardWaves.Interval, 45.f);
		TestEqual(TEXT("Hard sends larger waves"), HardWaves.Size, 12);
		TestEqual(TEXT("Hard sends a larger minimum"), HardWaves.MinSize, 6);

		Easy.SizeScale = 0.1f;
		BuildOrder->DifficultyScales.Add(ERTSAIDifficulty::Easy, Easy);
		const FRTSAIWaveSettings Tiny = RTSAI::GetWaveSettings(*BuildOrder, ERTSAIDifficulty::Easy);
		TestEqual(TEXT("A wave is at least one unit"), Tiny.Size, 1);
		TestEqual(TEXT("The minimum never exceeds the size"), Tiny.MinSize, 1);

		TestFalse(TEXT("A small army waits"), RTSAI::ShouldLaunchWave(Normal, 3, 10.f));
		TestTrue(TEXT("A full army goes at once"), RTSAI::ShouldLaunchWave(Normal, 8, 0.f));
		TestFalse(TEXT("The interval needs the minimum"), RTSAI::ShouldLaunchWave(Normal, 3, 100.f));
		TestTrue(TEXT("After the interval the minimum goes"), RTSAI::ShouldLaunchWave(Normal, 4, 90.f));
		TestFalse(TEXT("No army, no wave"), RTSAI::ShouldLaunchWave(Tiny, 0, 1000.f));
		TestFalse(TEXT("An army that cannot grow still waits for the interval"), RTSAI::ShouldLaunchWave(Normal, 2, 10.f, false));
		TestTrue(TEXT("After the interval it goes short of the minimum"), RTSAI::ShouldLaunchWave(Normal, 2, 90.f, false));
		TestFalse(TEXT("No army, no wave, grown or not"), RTSAI::ShouldLaunchWave(Normal, 0, 1000.f, false));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIBuildOrderTest, "WorldDivide.AI.BuildOrder", TestFlags)

	bool FRTSAIBuildOrderTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		URTSUnitData* Infantry = MakeUnit(100);
		URTSUpgradeData* Training = NewObject<URTSUpgradeData>();
		Training->Cost = 0;
		URTSBuildingData* BarracksData = MakeBuilding({ Infantry }, { Training });
		URTSAIBuildOrderData* BuildOrder = NewObject<URTSAIBuildOrderData>();
		BuildOrder->Steps = { MakeStep(BarracksData, 1), MakeStep(Infantry, 2), MakeStep(Training, 1) };

		ARTSAIPlayer* AI = SpawnAI(TestWorld, BuildOrder, 250);
		ARTSPlayerState* Player = AI->GetRTSPlayerState();
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-3000.f, 0.f, 0.f));
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, BarracksData, AITeam, FVector(3000.f, 0.f, 0.f));
		const TArray<TObjectPtr<URTSEntityData>>& Queue = Barracks->GetProductionComponent()->GetQueue();
		TestEqual(TEXT("The standing barracks counts"), AI->CountOwned(*BarracksData), 1);
		// A harvester at work brings more credits, so a step the credits do not cover yet is worth waiting for (#181).
		URTSBuildingData* DepotData = MakeBuilding();
		DepotData->bAcceptsSupply = true;
		SpawnCompleted(TestWorld, DepotData, AITeam, FVector(3000.f, -3000.f, 0.f));
		TestWorld.SpawnSupplySource(1000, FIntPoint(3, 3), FVector(6000.f, -3000.f, 0.f));
		TestWorld.SpawnUnit(NewObject<URTSHarvesterData>(), AITeam, FVector(4500.f, -3000.f, 0.f));

		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("The barracks is met, so the first infantry is queued"), Queue.Num(), 1);
		TestEqual(TEXT("It is paid like a player's"), Player->GetCredits(), 150);
		TestEqual(TEXT("A queued unit counts"), AI->CountOwned(*Infantry), 1);
		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("Then the second"), Queue.Num(), 2);
		TestEqual(TEXT("Paid as well"), Player->GetCredits(), 50);

		// The upgrade costs nothing, but the third step only comes once the second is met.
		BuildOrder->Steps[1].Count = 3;
		const int32 Issued = AI->GetIssuedCommandCount();
		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("A third infantry it cannot afford is not ordered"), Queue.Num(), 2);
		TestEqual(TEXT("The AI waits rather than skipping ahead"), AI->GetIssuedCommandCount(), Issued);
		TestEqual(TEXT("Its credits stay put"), Player->GetCredits(), 50);
		BuildOrder->Steps[1].Count = 2;
		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("With the infantry met, the free upgrade is queued"), Queue.Num(), 3);
		TestEqual(TEXT("A queued upgrade counts"), AI->CountOwned(*Training), 1);

		const int32 IssuedBefore = AI->GetIssuedCommandCount();
		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("With every step met and no army units, nothing more is ordered"), AI->GetIssuedCommandCount(), IssuedBefore);

		Kill(*Barracks);
		TestEqual(TEXT("A destroyed building no longer counts"), AI->CountOwned(*BarracksData), 0);
		TestEqual(TEXT("Its queue goes with it"), AI->CountOwned(*Infantry), 0);
		return true;
	}

	/**
	 * A build order whose next step nothing left can make (#179): builders come only from the headquarters, which is gone, so
	 * the AI passes over them and queues the infantry its barracks offers, instead of waiting for builders forever.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIBuildOrderOutOfReachTest, "WorldDivide.AI.BuildOrderOutOfReach", TestFlags)

	bool FRTSAIBuildOrderOutOfReachTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		URTSUnitData* Builder = MakeUnit(100);
		URTSUnitData* Infantry = MakeUnit(100);
		URTSBuildingData* HeadquartersData = MakeBuilding({ Builder });
		URTSBuildingData* BarracksData = MakeBuilding({ Infantry });
		URTSAIBuildOrderData* BuildOrder = NewObject<URTSAIBuildOrderData>();
		BuildOrder->Steps = { MakeStep(Builder, 2), MakeStep(Infantry, 1) };

		ARTSAIPlayer* AI = SpawnAI(TestWorld, BuildOrder, 500);
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-3000.f, 0.f, 0.f));
		ARTSBuilding* Headquarters = SpawnCompleted(TestWorld, HeadquartersData, AITeam, FVector(3000.f, 0.f, 0.f));
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, BarracksData, AITeam, FVector(3000.f, 2000.f, 0.f));
		const TArray<TObjectPtr<URTSEntityData>>& Queue = Barracks->GetProductionComponent()->GetQueue();

		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("With its headquarters standing the AI works on the builders first"),
			Headquarters->GetProductionComponent()->GetQueue().Num(), 1);
		TestEqual(TEXT("The barracks waits its turn"), Queue.Num(), 0);

		Kill(*Headquarters);
		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("Without a headquarters the builders are out of reach: the barracks queues the infantry"), Queue.Num(), 1);
		TestEqual(TEXT("The infantry is paid"), AI->GetRTSPlayerState()->GetCredits(), 300);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIWavesTest, "WorldDivide.AI.Waves", TestFlags)

	bool FRTSAIWavesTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		// Laid out in DefenseRadius: what counts as near the base follows the AI's own radius.
		const float R = ARTSAIPlayer::DefenseRadius;
		URTSWeaponData* Weapon = MakeWeapon();
		URTSUnitData* Tank = MakeUnit(700, Weapon);
		URTSBuilderData* Builder = NewObject<URTSBuilderData>();
		Builder->Weapon = Weapon;
		URTSHarvesterData* Harvester = NewObject<URTSHarvesterData>();
		Harvester->Weapon = Weapon;

		ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-1.2f * R, 0.f, 0.f));
		SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
		// Armed builders and harvesters keep working; only the rest is army.
		TestWorld.SpawnUnit(Builder, AITeam, FVector(1.0f * R, 600.f, 0.f));
		TestWorld.SpawnUnit(Harvester, AITeam, FVector(1.0f * R, -600.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 300.f, 0.f));
		TestWorld.SpawnUnit(Tank, PlayerTeam, FVector(-0.8f * R, 0.f, 0.f));

		FRTSAIWaveSettings Settings;
		Settings.Size = 3;
		Settings.MinSize = 2;
		Settings.Interval = 30.f;
		AI->SetWaveSettings(Settings);

		AI->Think(10.f);
		TestEqual(TEXT("Two tanks are no full wave, and the interval has not passed"), AI->GetWaveCount(), 0);
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, -300.f, 0.f));
		AI->Think(1.f);
		TestEqual(TEXT("A third tank fills the wave"), AI->GetWaveCount(), 1);
		const TArray<ARTSUnit*> Wave = AI->GetWaveUnits();
		TestEqual(TEXT("The wave holds the three tanks"), Wave.Num(), 3);
		for (const ARTSUnit* Unit : Wave)
		{
			TestEqual(TEXT("Only AI tanks go"), Unit->GetUnitData(), static_cast<URTSUnitData*>(Tank));
			TestEqual(TEXT("Of the AI's team"), Unit->GetTeamId(), AITeam);
		}

		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.72f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.72f * R, 300.f, 0.f));
		AI->Think(10.f);
		TestEqual(TEXT("Two new tanks wait for the interval"), AI->GetWaveCount(), 1);
		AI->Think(20.f);
		TestEqual(TEXT("Once it has passed, the minimum goes"), AI->GetWaveCount(), 2);
		TestEqual(TEXT("Both waves are tracked"), AI->GetWaveUnits().Num(), 5);
		return true;
	}

	/**
	 * An army short of the minimum goes once the interval has passed if it cannot grow any more (#179): no credits for an
	 * army unit, none queued and no harvester at work. With the credits for another tank it keeps waiting.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIWavesOutOfCreditsTest, "WorldDivide.AI.WavesOutOfCredits", TestFlags)

	bool FRTSAIWavesOutOfCreditsTest::RunTest(const FString& Parameters)
	{
		for (const int32 Credits : { 700, 0 })
		{
			const FRTSTestWorld TestWorld;
			TestWorld.SpawnGameState();
			const float R = ARTSAIPlayer::DefenseRadius;
			URTSUnitData* Tank = MakeUnit(700, MakeWeapon());
			URTSAIBuildOrderData* BuildOrder = NewObject<URTSAIBuildOrderData>();
			BuildOrder->ArmyUnits = { Tank };
			ARTSAIPlayer* AI = SpawnAI(TestWorld, BuildOrder, Credits);
			SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-1.2f * R, 0.f, 0.f));
			ARTSBuilding* Factory = SpawnCompleted(TestWorld, MakeBuilding({ Tank }), AITeam, FVector(1.2f * R, 0.f, 0.f));
			TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 0.f, 0.f));
			TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 300.f, 0.f));

			FRTSAIWaveSettings Settings;
			Settings.Size = 8;
			Settings.MinSize = 4;
			Settings.Interval = 30.f;
			AI->SetWaveSettings(Settings);

			AI->Think(10.f);
			const FString With = FString::Printf(TEXT("With %d credits"), Credits);
			TestEqual(*FString::Printf(TEXT("%s, two tanks wait before the interval"), *With), AI->GetWaveCount(), 0);
			AI->Think(20.f);
			if (Credits > 0)
			{
				TestEqual(*FString::Printf(TEXT("%s, a third tank is queued"), *With), Factory->GetProductionComponent()->GetQueue().Num(), 1);
				TestEqual(*FString::Printf(TEXT("%s, the army can grow and keeps waiting for the minimum"), *With), AI->GetWaveCount(), 0);
			}
			else
			{
				TestEqual(*FString::Printf(TEXT("%s, the army cannot grow and goes after the interval"), *With), AI->GetWaveCount(), 1);
				TestEqual(*FString::Printf(TEXT("%s, both tanks go"), *With), AI->GetWaveUnits().Num(), 2);
			}
		}
		return true;
	}

	/**
	 * The assault estimate (#181) against a tower with 500 health, 30 damage a second and 81 m of range: rifles walking in
	 * under fire fall short, tanks closing in fast win with a margin only in numbers, a weapon that outranges the tower always
	 * wins, and units already beside it fire from the start.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAICanOvercomeTest, "WorldDivide.AI.CanOvercome", TestFlags)

	bool FRTSAICanOvercomeTest::RunTest(const FString& Parameters)
	{
		const RTSAI::FFighter Tower{ 500.f, 30.f, 8125.f, 0.f };
		const RTSAI::FFighter Rifle{ 100.f, 16.f, 3500.f, 450.f };
		const RTSAI::FFighter Tank{ 200.f, 20.f, 6500.f, 1100.f };
		const RTSAI::FFighter Artillery{ 100.f, 5.f, 9000.f, 400.f };

		TestFalse(TEXT("Nobody overcomes nothing"), RTSAI::CanOvercome({}, Tower));
		TestFalse(TEXT("Five rifles walking in under fire fall short"),
			RTSAI::CanOvercome({ Rifle, Rifle, Rifle, Rifle, Rifle }, Tower));
		TestFalse(TEXT("Two tanks fall short"), RTSAI::CanOvercome({ Tank, Tank }, Tower));
		TestTrue(TEXT("Three tanks win"), RTSAI::CanOvercome({ Tank, Tank, Tank }, Tower));
		TestFalse(TEXT("But not with half its health again to spare"), RTSAI::CanOvercome({ Tank, Tank, Tank }, Tower, 1.5f));
		TestTrue(TEXT("Six tanks have that margin"), RTSAI::CanOvercome({ Tank, Tank, Tank, Tank, Tank, Tank }, Tower, 1.5f));
		TestTrue(TEXT("A weapon that outranges the tower is never hit"), RTSAI::CanOvercome({ Artillery }, Tower));

		RTSAI::FFighter Beside = Rifle;
		Beside.Distance = 0.f;
		TestTrue(TEXT("Five rifles already beside it fire from the start and win"),
			RTSAI::CanOvercome({ Beside, Beside, Beside, Beside, Beside }, Tower));
		return true;
	}

	/** A tower with 500 health, 50 damage a second and 30 m of range, its weapon live. */
	ARTSBuilding* SpawnTower(FAutomationTestBase& Test, const FRTSTestWorld& TestWorld, int32 TeamId, const FVector& Location)
	{
		URTSWeaponData* Gun = MakeWeapon();
		Gun->Range = 3000.f;
		Gun->Damage = 50.f;
		URTSBuildingData* Data = MakeBuilding();
		Data->Weapon = Gun;
		Data->bIsDefense = true;
		ARTSBuilding* Tower = SpawnCompleted(TestWorld, Data, TeamId, Location);
		Test.TestNotNull(TEXT("The tower's weapon is live"), Tower->GetWeaponComponent()->GetWeaponData());
		return Tower;
	}

	/** Tanks with 100 health and 50 damage a second at 8 m, driving 10 m/s. */
	URTSUnitData* MakeStrongTank()
	{
		URTSWeaponData* Cannon = MakeWeapon();
		Cannon->Damage = 50.f;
		URTSUnitData* Tank = MakeUnit(700, Cannon);
		Tank->MoveSpeed = 1000.f;
		return Tank;
	}

	/**
	 * A wave weighs the defences at its target (#181): two light tanks do not set off at a building a tower covers, but at
	 * one no defence covers; six strong tanks set off at the covered one.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIWavesWeighDefencesTest, "WorldDivide.AI.WavesWeighDefences", TestFlags)

	bool FRTSAIWavesWeighDefencesTest::RunTest(const FString& Parameters)
	{
		const float R = ARTSAIPlayer::DefenseRadius;
		FRTSAIWaveSettings Settings;
		Settings.Size = 2;
		Settings.MinSize = 1;
		Settings.Interval = 30.f;
		{
			const FRTSTestWorld TestWorld;
			TestWorld.SpawnGameState();
			URTSUnitData* Tank = MakeUnit(700, MakeWeapon());
			ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
			SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-1.2f * R, 0.f, 0.f));
			SpawnTower(*this, TestWorld, PlayerTeam, FVector(-1.2f * R, 1500.f, 0.f));
			SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
			TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 0.f, 0.f));
			TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 300.f, 0.f));
			AI->SetWaveSettings(Settings);

			AI->Think(1.f);
			TestEqual(TEXT("Two light tanks hold back from the only building, which a tower too strong for them covers"),
				AI->GetWaveCount(), 0);

			// Far from the tower's reach.
			SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-1.2f * R, -R, 0.f));
			AI->Think(1.f);
			TestEqual(TEXT("A building no defence covers draws the wave"), AI->GetWaveCount(), 1);
			TestEqual(TEXT("With both tanks"), AI->GetWaveUnits().Num(), 2);
		}
		{
			const FRTSTestWorld TestWorld;
			TestWorld.SpawnGameState();
			URTSUnitData* Tank = MakeStrongTank();
			ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
			SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-1.2f * R, 0.f, 0.f));
			SpawnTower(*this, TestWorld, PlayerTeam, FVector(-1.2f * R, 1500.f, 0.f));
			SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
			for (int32 Index = 0; Index < 6; ++Index)
			{
				TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 300.f * Index, 0.f));
			}
			FRTSAIWaveSettings Six = Settings;
			Six.Size = 6;
			AI->SetWaveSettings(Six);

			AI->Think(1.f);
			TestEqual(TEXT("Six strong tanks can take the tower and set off"), AI->GetWaveCount(), 1);
			TestEqual(TEXT("All six"), AI->GetWaveUnits().Num(), 6);
		}
		return true;
	}

	/**
	 * Wave units a tower fires on (#181): six strong tanks turn on it together rather than on the building they were sent at;
	 * two light tanks fall back to the gather point and leave the wave.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIWavesEngageDefencesTest, "WorldDivide.AI.WavesEngageDefences", TestFlags)

	bool FRTSAIWavesEngageDefencesTest::RunTest(const FString& Parameters)
	{
		const float R = ARTSAIPlayer::DefenseRadius;
		for (const bool bStrong : { true, false })
		{
			const FRTSTestWorld TestWorld;
			TestWorld.SpawnGameState();
			URTSUnitData* Tank = bStrong ? MakeStrongTank() : MakeUnit(700, MakeWeapon());
			const int32 Count = bStrong ? 6 : 2;
			ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
			SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-1.2f * R, 0.f, 0.f));
			SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
			for (int32 Index = 0; Index < Count; ++Index)
			{
				TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 300.f * Index, 0.f));
			}
			FRTSAIWaveSettings Settings;
			Settings.Size = Count;
			Settings.MinSize = 1;
			Settings.Interval = 30.f;
			AI->SetWaveSettings(Settings);

			AI->Think(1.f);
			const TArray<ARTSUnit*> Wave = AI->GetWaveUnits();
			const FString With = bStrong ? TEXT("Six strong tanks") : TEXT("Two light tanks");
			if (!TestEqual(*FString::Printf(TEXT("%s set off at the undefended building"), *With), Wave.Num(), Count))
			{
				continue;
			}

			// A tower on their way, in its range of them but out of theirs.
			ARTSBuilding* Tower = SpawnTower(*this, TestWorld, PlayerTeam, Wave[0]->GetActorLocation() + FVector(-2000.f, 0.f, 0.f));
			const int32 IssuedBefore = AI->GetIssuedCommandCount();
			AI->Think(1.f);
			if (bStrong)
			{
				TestEqual(*FString::Printf(TEXT("%s stay in their wave"), *With), AI->GetWaveUnits().Num(), Count);
				for (const ARTSUnit* Unit : Wave)
				{
					TestEqual(*FString::Printf(TEXT("%s each attack the tower"), *With), Unit->GetWeaponComponent()->GetTarget(),
						static_cast<AActor*>(Tower));
				}
				continue;
			}
			// The test world has no navmesh, so the move back ends at once; the order itself is what counts here.
			TestEqual(*FString::Printf(TEXT("%s fall back from a tower they cannot beat"), *With), AI->GetWaveUnits().Num(), 0);
			TestEqual(*FString::Printf(TEXT("%s, in one order back to the gather point"), *With), AI->GetIssuedCommandCount(),
				IssuedBefore + 1);
		}
		return true;
	}

	/**
	 * A build-order step the credits do not cover (#181): passed over for the army while no harvester brings more, waited for
	 * while one is at work.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIBuildOrderWithoutIncomeTest, "WorldDivide.AI.BuildOrderWithoutIncome", TestFlags)

	bool FRTSAIBuildOrderWithoutIncomeTest::RunTest(const FString& Parameters)
	{
		for (const bool bHarvesting : { false, true })
		{
			const FRTSTestWorld TestWorld;
			TestWorld.SpawnGameState();
			URTSUnitData* Infantry = MakeUnit(100, MakeWeapon());
			URTSBuildingData* Factory = MakeBuilding();
			Factory->Cost = 1500;
			URTSBuilderData* Builder = NewObject<URTSBuilderData>();
			Builder->Buildings = { Factory };
			URTSAIBuildOrderData* BuildOrder = NewObject<URTSAIBuildOrderData>();
			BuildOrder->Steps = { MakeStep(Factory, 1) };
			BuildOrder->ArmyUnits = { Infantry };

			ARTSAIPlayer* AI = SpawnAI(TestWorld, BuildOrder, 650);
			SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-20000.f, 0.f, 0.f));
			ARTSBuilding* Barracks = SpawnCompleted(TestWorld, MakeBuilding({ Infantry }), AITeam, FVector(0.f, 3000.f, 0.f));
			TestWorld.SpawnUnit(Builder, AITeam, FVector(0.f, -1500.f, 0.f));
			ARTSUnit* Harvester = nullptr;
			if (bHarvesting)
			{
				URTSHarvesterData* HarvesterData = NewObject<URTSHarvesterData>();
				URTSBuildingData* DepotData = MakeBuilding();
				DepotData->bAcceptsSupply = true;
				SpawnCompleted(TestWorld, DepotData, AITeam, FVector(3000.f, -3000.f, 0.f));
				TestWorld.SpawnSupplySource(1000, FIntPoint(3, 3), FVector(6000.f, -3000.f, 0.f));
				Harvester = TestWorld.SpawnUnit(HarvesterData, AITeam, FVector(4500.f, -3000.f, 0.f));
			}

			AI->Think(ARTSAIPlayer::ThinkInterval);
			const int32 Queued = Barracks->GetProductionComponent()->GetQueue().Num();
			if (bHarvesting)
			{
				TestTrue(TEXT("The idle harvester is sent to work"), Harvester->GetQueuedCommandCount() > 0);
				TestEqual(TEXT("While it brings credits, the AI saves up for the factory"), Queued, 0);
				TestEqual(TEXT("Its credits untouched"), AI->GetRTSPlayerState()->GetCredits(), 650);
			}
			else
			{
				TestEqual(TEXT("With no income the factory is out of reach: the barracks trains infantry"), Queued, 1);
				TestEqual(TEXT("Paid from the credits"), AI->GetRTSPlayerState()->GetCredits(), 550);
			}
		}
		return true;
	}

	/**
	 * An idle harvester with no source in its search radius (#181) is sent to the nearest one elsewhere, unless an enemy
	 * defence covers it.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIHarvestFarSourceTest, "WorldDivide.AI.HarvestFarSource", TestFlags)

	bool FRTSAIHarvestFarSourceTest::RunTest(const FString& Parameters)
	{
		for (const bool bCovered : { false, true })
		{
			const FRTSTestWorld TestWorld;
			TestWorld.SpawnGameState();
			URTSHarvesterData* HarvesterData = NewObject<URTSHarvesterData>();
			HarvesterData->SearchRadius = 2000.f;
			URTSBuildingData* DepotData = MakeBuilding();
			DepotData->bAcceptsSupply = true;

			ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
			SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-20000.f, 0.f, 0.f));
			SpawnCompleted(TestWorld, DepotData, AITeam, FVector(0.f, 0.f, 0.f));
			ARTSSupplySource* Far = TestWorld.SpawnSupplySource(1000, FIntPoint(3, 3), FVector(10000.f, 0.f, 0.f));
			if (bCovered)
			{
				SpawnTower(*this, TestWorld, PlayerTeam, FVector(10000.f, 2500.f, 0.f));
			}
			ARTSUnit* Harvester = TestWorld.SpawnUnit(HarvesterData, AITeam, FVector(1500.f, 0.f, 0.f));

			AI->Think(ARTSAIPlayer::ThinkInterval);
			if (bCovered)
			{
				TestEqual(TEXT("A source an enemy tower covers is left alone"), Harvester->GetQueuedCommandCount(), 0);
			}
			else
			{
				TestTrue(TEXT("The harvester goes to the source outside its search radius"), Harvester->GetQueuedCommandCount() > 0);
				TestEqual(TEXT("That one"), Harvester->GetHarvesterComponent()->GetSource(), Far);
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIFindThreatTest, "WorldDivide.AI.FindThreat", TestFlags)

	bool FRTSAIFindThreatTest::RunTest(const FString& Parameters)
	{
		const TArray<FVector> Buildings = { FVector(0.f, 0.f, 0.f), FVector(2000.f, 0.f, 0.f) };
		const float Radius = 1000.f;

		TestEqual(TEXT("No enemies, no threat"), RTSAI::FindThreat({}, Buildings, Radius), INDEX_NONE);
		TestEqual(TEXT("Enemies out of reach are no threat"),
			RTSAI::FindThreat({ FVector(-1500.f, 0.f, 0.f), FVector(1000.f, 1200.f, 0.f) }, Buildings, Radius), INDEX_NONE);
		TestEqual(TEXT("One within the radius of any building is"),
			RTSAI::FindThreat({ FVector(-1500.f, 0.f, 0.f), FVector(2800.f, 0.f, 0.f) }, Buildings, Radius), 1);
		TestEqual(TEXT("Of several, the one nearest a building"),
			RTSAI::FindThreat({ FVector(900.f, 0.f, 0.f), FVector(2300.f, 0.f, 0.f), FVector(0.f, 500.f, 0.f) }, Buildings, Radius), 1);
		TestEqual(TEXT("Height does not count"),
			RTSAI::FindThreat({ FVector(0.f, 500.f, 5000.f) }, Buildings, Radius), 0);
		TestEqual(TEXT("Without buildings there is nothing to defend"),
			RTSAI::FindThreat({ FVector(0.f, 0.f, 0.f) }, {}, Radius), INDEX_NONE);

		const TArray<FVector> Far = { FVector(-1500.f, 0.f, 0.f), FVector(6000.f, 0.f, 0.f) };
		TestEqual(TEXT("A recent attacker out of reach is a threat"), RTSAI::FindThreat(Far, Buildings, Radius, { false, true }), 1);
		TestEqual(TEXT("Unflagged, it is not"), RTSAI::FindThreat(Far, Buildings, Radius, { false, false }), INDEX_NONE);
		TestEqual(TEXT("An enemy in the base comes before a far attacker"),
			RTSAI::FindThreat({ FVector(9000.f, 0.f, 0.f), FVector(2500.f, 0.f, 0.f) }, Buildings, Radius, { true, false }), 1);
		TestEqual(TEXT("Of two attackers, the one nearer a building"),
			RTSAI::FindThreat({ FVector(9000.f, 0.f, 0.f), FVector(-4000.f, 0.f, 0.f) }, Buildings, Radius, { true, true }), 1);
		TestEqual(TEXT("An attacker with no building left is nothing to defend"),
			RTSAI::FindThreat({ FVector(9000.f, 0.f, 0.f) }, {}, Radius, { true }), INDEX_NONE);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIDefendTest, "WorldDivide.AI.Defend", TestFlags)

	bool FRTSAIDefendTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		// Laid out in DefenseRadius: what counts as near the base follows the AI's own radius.
		const float R = ARTSAIPlayer::DefenseRadius;
		URTSUnitData* Tank = MakeUnit(700, MakeWeapon());

		ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-1.2f * R, 0.f, 0.f));
		SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
		ARTSUnit* Enemy = TestWorld.SpawnUnit(Tank, PlayerTeam, FVector(-0.8f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.6f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.6f * R, 300.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.6f * R, -300.f, 0.f));

		FRTSAIWaveSettings Settings;
		Settings.Size = 3;
		Settings.MinSize = 2;
		Settings.Interval = 30.f;
		AI->SetWaveSettings(Settings);

		AI->Think(1.f);
		TestFalse(TEXT("An enemy by its own base is no threat"), AI->IsDefending());
		TestEqual(TEXT("The first wave goes"), AI->GetWaveCount(), 1);
		const TArray<ARTSUnit*> Wave = AI->GetWaveUnits();
		TestEqual(TEXT("With three tanks"), Wave.Num(), 3);

		// One wave tank is well on its way; two new tanks gather, too few for a wave.
		ARTSUnit* FarWaveUnit = Wave[0];
		FarWaveUnit->SetActorLocation(FVector(-0.6f * R, 0.f, 0.f));
		ARTSUnit* ArmyA = TestWorld.SpawnUnit(Tank, AITeam, FVector(0.72f * R, 0.f, 0.f));
		ARTSUnit* ArmyB = TestWorld.SpawnUnit(Tank, AITeam, FVector(0.72f * R, 300.f, 0.f));
		AI->Think(1.f);
		TestFalse(TEXT("Still no threat"), AI->IsDefending());
		TestEqual(TEXT("Nobody defends"), AI->GetDefenders().Num(), 0);

		Enemy->SetActorLocation(FVector(0.8f * R, 0.6f * R, 0.f));
		const int32 IssuedBefore = AI->GetIssuedCommandCount();
		AI->Think(1.f);
		TestTrue(TEXT("An enemy near its building puts the AI on the defence"), AI->IsDefending());
		TestTrue(TEXT("Orders went out"), AI->GetIssuedCommandCount() > IssuedBefore);
		const TArray<ARTSUnit*> Defenders = AI->GetDefenders();
		TestEqual(TEXT("The army and the wave tanks still near the base defend"), Defenders.Num(), 4);
		TestTrue(TEXT("The gathered army defends"), Defenders.Contains(ArmyA) && Defenders.Contains(ArmyB));
		TestFalse(TEXT("The wave tank far out does not turn back"), Defenders.Contains(FarWaveUnit));
		TestEqual(TEXT("It stays in its wave"), AI->GetWaveUnits().Num(), 1);

		AI->Think(40.f);
		TestEqual(TEXT("No wave leaves while the base is under attack"), AI->GetWaveCount(), 1);
		TestEqual(TEXT("The defenders stay"), AI->GetDefenders().Num(), 4);

		// Waves out of reach for now, so the return is seen on its own.
		FRTSAIWaveSettings Later = Settings;
		Later.Size = 10;
		Later.MinSize = 10;
		AI->SetWaveSettings(Later);
		Enemy->GetHealthComponent()->ApplyDamage(Enemy->GetHealthComponent()->GetMaxHealth(), nullptr);
		const int32 IssuedBeforeReturn = AI->GetIssuedCommandCount();
		AI->Think(1.f);
		TestFalse(TEXT("With the enemy gone the defence ends"), AI->IsDefending());
		TestEqual(TEXT("The defenders are released"), AI->GetDefenders().Num(), 0);
		TestTrue(TEXT("And sent back to the gather point"), AI->GetIssuedCommandCount() > IssuedBeforeReturn);

		AI->SetWaveSettings(Settings);
		AI->Think(1.f);
		TestEqual(TEXT("Back in the army, the four tanks fill the next wave"), AI->GetWaveCount(), 2);
		TestEqual(TEXT("Joining the tank still out"), AI->GetWaveUnits().Num(), 5);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIDefendOutOfRangeTest, "WorldDivide.AI.DefendOutOfRange", TestFlags)

	bool FRTSAIDefendOutOfRangeTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		// Laid out in DefenseRadius: what counts as near the base follows the AI's own radius.
		const float R = ARTSAIPlayer::DefenseRadius;
		URTSUnitData* Tank = MakeUnit(700, MakeWeapon());

		ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-2.4f * R, 0.f, 0.f));
		ARTSBuilding* Base = SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
		// Well outside DefenseRadius of the AI's only building.
		ARTSUnit* Shooter = TestWorld.SpawnUnit(Tank, PlayerTeam, FVector(-0.2f * R, 0.f, 0.f));
		ARTSUnit* Friend = TestWorld.SpawnUnit(Tank, AITeam, FVector(0.6f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.6f * R, 300.f, 0.f));

		// Waves out of reach, so only the defence moves the army.
		FRTSAIWaveSettings Settings;
		Settings.Size = 10;
		Settings.MinSize = 10;
		Settings.Interval = 1000.f;
		AI->SetWaveSettings(Settings);

		AI->Think(1.f);
		TestFalse(TEXT("An enemy out of range that has not fired is no threat"), AI->IsDefending());

		Base->GetHealthComponent()->ApplyDamage(10.f, nullptr);
		Base->GetHealthComponent()->ApplyDamage(10.f, Friend);
		AI->Think(1.f);
		TestFalse(TEXT("Damage without an enemy instigator is no threat"), AI->IsDefending());

		Base->GetHealthComponent()->ApplyDamage(10.f, Shooter);
		const int32 IssuedBefore = AI->GetIssuedCommandCount();
		AI->Think(1.f);
		TestTrue(TEXT("An enemy firing on a building from out of range puts the AI on the defence"), AI->IsDefending());
		TestEqual(TEXT("Against the shooter"), AI->GetThreat(), static_cast<AActor*>(Shooter));
		TestEqual(TEXT("The idle army defends"), AI->GetDefenders().Num(), 2);
		TestEqual(TEXT("In one attack-move order"), AI->GetIssuedCommandCount(), IssuedBefore + 1);

		AI->Think(ARTSAIPlayer::AttackerMemory - 2.f);
		TestTrue(TEXT("The attacker is remembered for a while"), AI->IsDefending());
		AI->Think(2.f);
		TestFalse(TEXT("Without a new hit it is forgotten"), AI->IsDefending());
		TestNull(TEXT("And no longer a threat"), AI->GetThreat());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIDefendAgainstBuildingTest, "WorldDivide.AI.DefendAgainstBuilding", TestFlags)

	bool FRTSAIDefendAgainstBuildingTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		// Laid out in DefenseRadius: what counts as near the base follows the AI's own radius.
		const float R = ARTSAIPlayer::DefenseRadius;
		URTSUnitData* Tank = MakeUnit(700, MakeWeapon());
		URTSBuildingData* Defence = MakeBuilding();
		Defence->Weapon = MakeWeapon();

		ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
		// Keeps the match going once the tower falls.
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-2.4f * R, 0.f, 0.f));
		ARTSBuilding* Base = SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
		// Well outside DefenseRadius of the AI's only building.
		ARTSBuilding* Tower = SpawnCompleted(TestWorld, Defence, PlayerTeam, FVector(-0.2f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.6f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.6f * R, 300.f, 0.f));

		// Waves out of reach, so only the defence moves the army.
		FRTSAIWaveSettings Settings;
		Settings.Size = 10;
		Settings.MinSize = 10;
		Settings.Interval = 1000.f;
		AI->SetWaveSettings(Settings);

		AI->Think(1.f);
		TestFalse(TEXT("An enemy building that has not fired is no threat"), AI->IsDefending());

		Base->GetHealthComponent()->ApplyDamage(10.f, Tower);
		const int32 IssuedBefore = AI->GetIssuedCommandCount();
		AI->Think(1.f);
		TestTrue(TEXT("An enemy building firing on the base puts the AI on the defence"), AI->IsDefending());
		TestEqual(TEXT("Against the building"), AI->GetThreat(), static_cast<AActor*>(Tower));
		TestEqual(TEXT("The idle army defends"), AI->GetDefenders().Num(), 2);
		TestEqual(TEXT("In one attack-move order"), AI->GetIssuedCommandCount(), IssuedBefore + 1);

		Kill(*Tower);
		AI->Think(1.f);
		TestFalse(TEXT("With the building destroyed the defence ends"), AI->IsDefending());
		TestNull(TEXT("And it is no longer a threat"), AI->GetThreat());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIPickRespondersTest, "WorldDivide.AI.PickResponders", TestFlags)

	bool FRTSAIPickRespondersTest::RunTest(const FString& Parameters)
	{
		const FVector Threat(0.f, 0.f, 0.f);
		const TArray<FVector> Army = { FVector(3000.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(0.f, 2000.f, 0.f), FVector(0.f, 4000.f, 0.f) };
		TestEqual(TEXT("No army, no responders"), RTSAI::PickResponders({}, Threat, 0.5f).Num(), 0);
		TestEqual(TEXT("Half of four, nearest first"), RTSAI::PickResponders(Army, Threat, 0.5f), TArray<int32>({ 1, 2 }));
		TestEqual(TEXT("A share rounds up"), RTSAI::PickResponders(Army, Threat, 0.6f).Num(), 3);
		TestEqual(TEXT("A lone unit goes"), RTSAI::PickResponders({ FVector(500.f, 0.f, 0.f) }, Threat, 0.5f), TArray<int32>({ 0 }));
		TestEqual(TEXT("Even with a tiny share"), RTSAI::PickResponders(Army, Threat, 0.01f).Num(), 1);
		TestEqual(TEXT("Never more than the army"), RTSAI::PickResponders(Army, Threat, 2.f).Num(), 4);
		TestEqual(TEXT("Height does not count"),
			RTSAI::PickResponders({ FVector(1000.f, 0.f, 0.f), FVector(500.f, 0.f, 5000.f) }, Threat, 0.5f), TArray<int32>({ 1 }));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIDefendUnitsTest, "WorldDivide.AI.DefendUnits", TestFlags)

	bool FRTSAIDefendUnitsTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		// Laid out in DefenseRadius: what counts as near the base follows the AI's own radius.
		const float R = ARTSAIPlayer::DefenseRadius;
		URTSWeaponData* Weapon = MakeWeapon();
		URTSUnitData* Tank = MakeUnit(700, Weapon);
		URTSHarvesterData* HarvesterData = NewObject<URTSHarvesterData>();
		HarvesterData->MaxHealth = 100.f;

		ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-4.8f * R, 0.f, 0.f));
		SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(1.2f * R, 0.f, 0.f));
		// A harvester at a distant source, well outside DefenseRadius of the AI's only building, with an enemy beside it.
		ARTSUnit* Harvester = TestWorld.SpawnUnit(HarvesterData, AITeam, FVector(-3.2f * R, 0.f, 0.f));
		ARTSUnit* Shooter = TestWorld.SpawnUnit(Tank, PlayerTeam, FVector(-3.2f * R, 400.f, 0.f));
		// Two tanks nearer the harvester than the other two.
		ARTSUnit* Near1 = TestWorld.SpawnUnit(Tank, AITeam, FVector(0.4f * R, 0.f, 0.f));
		ARTSUnit* Near2 = TestWorld.SpawnUnit(Tank, AITeam, FVector(0.4f * R, 300.f, 0.f));
		ARTSUnit* Far = TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(0.8f * R, 300.f, 0.f));

		// Waves out of reach, so only the response moves the army.
		FRTSAIWaveSettings Settings;
		Settings.Size = 10;
		Settings.MinSize = 10;
		Settings.Interval = 1000.f;
		AI->SetWaveSettings(Settings);

		AI->Think(1.f);
		Harvester->GetHealthComponent()->ApplyDamage(10.f, nullptr);
		Harvester->GetHealthComponent()->ApplyDamage(10.f, Far);
		Far->GetHealthComponent()->ApplyDamage(10.f, Shooter);
		AI->Think(1.f);
		TestNull(TEXT("Damage without an enemy instigator, or to an army unit, draws no response"), AI->GetUnitThreat());
		TestEqual(TEXT("And sends no one"), AI->GetResponders().Num(), 0);

		Harvester->GetHealthComponent()->ApplyDamage(10.f, Shooter);
		const int32 IssuedBefore = AI->GetIssuedCommandCount();
		AI->Think(1.f);
		TestFalse(TEXT("A harvester hit far from the base is no base defence"), AI->IsDefending());
		TestEqual(TEXT("The shooter is the threat"), AI->GetUnitThreat(), static_cast<AActor*>(Shooter));
		const TArray<ARTSUnit*> Responders = AI->GetResponders();
		TestEqual(TEXT("Half the idle army responds"), Responders.Num(), 2);
		TestTrue(TEXT("The tanks nearest the shooter"), Responders.Contains(Near1) && Responders.Contains(Near2));
		TestEqual(TEXT("In one attack-move order"), AI->GetIssuedCommandCount(), IssuedBefore + 1);

		AI->Think(1.f);
		TestEqual(TEXT("The same responders stay on it"), AI->GetResponders().Num(), 2);

		const int32 IssuedBeforeReturn = AI->GetIssuedCommandCount();
		AI->Think(ARTSAIPlayer::AttackerMemory);
		TestNull(TEXT("Without a new hit the shooter is forgotten"), AI->GetUnitThreat());
		TestEqual(TEXT("The responders go back"), AI->GetResponders().Num(), 0);
		TestEqual(TEXT("To the gather point, in one order"), AI->GetIssuedCommandCount(), IssuedBeforeReturn + 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIIssueTest,"WorldDivide.AI.Issue", TestFlags)

	bool FRTSAIIssueTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSGameState* GameState = TestWorld.SpawnGameState();
		URTSUnitData* Infantry = MakeUnit(100, MakeWeapon());
		URTSBuildingData* BarracksData = MakeBuilding({ Infantry });
		URTSAIBuildOrderData* BuildOrder = NewObject<URTSAIBuildOrderData>();
		BuildOrder->Steps = { MakeStep(Infantry, 5) };
		ARTSAIPlayer* AI = SpawnAI(TestWorld, BuildOrder, 1000);
		ARTSPlayerState* Player = AI->GetRTSPlayerState();
		ARTSPlayerState* Human = TestWorld.SpawnPlayer(PlayerTeam, 1000);
		ARTSBuilding* PlayerBarracks = SpawnCompleted(TestWorld, BarracksData, PlayerTeam, FVector(-3000.f, 0.f, 0.f));
		ARTSBuilding* AIBarracks = SpawnCompleted(TestWorld, BarracksData, AITeam, FVector(3000.f, 0.f, 0.f));
		ARTSUnit* PlayerUnit = TestWorld.SpawnUnit(Infantry, PlayerTeam, FVector(-2000.f, 0.f, 0.f));

		// The AI's orders take the player's path, with the player's checks.
		FRTSCommand Produce;
		Produce.Type = ERTSCommandType::Produce;
		Produce.TargetActor = PlayerBarracks;
		Produce.EntityData = Infantry;
		RTSCommands::Issue(*Player, {}, Produce);
		TestEqual(TEXT("The AI cannot produce at the player's barracks"), PlayerBarracks->GetProductionComponent()->GetQueue().Num(), 0);
		TestEqual(TEXT("Nor is it charged"), Player->GetCredits(), 1000);

		FRTSCommand Attack;
		Attack.Type = ERTSCommandType::Attack;
		Attack.TargetActor = AIBarracks;
		RTSCommands::Issue(*Player, { PlayerUnit }, Attack);
		TestEqual(TEXT("The AI cannot order the player's units"), PlayerUnit->GetQueuedCommandCount(), 0);

		FRTSCommand Build;
		Build.Type = ERTSCommandType::Build;
		Build.EntityData = BarracksData;
		Build.TargetLocation = FVector(3000.f, 1500.f, 0.f);
		const int32 Buildings = CountBuildings(*TestWorld.World);
		RTSCommands::Issue(*Player, {}, Build);
		TestEqual(TEXT("Without a builder the AI places nothing, as a player outside debug build"), CountBuildings(*TestWorld.World), Buildings);
		TestEqual(TEXT("And pays nothing"), Player->GetCredits(), 1000);

		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("Its own build order queues at its own barracks"), AIBarracks->GetProductionComponent()->GetQueue().Num(), 1);
		TestEqual(TEXT("Paid from its own credits"), Player->GetCredits(), 900);
		TestEqual(TEXT("Not from the player's"), Human->GetCredits(), 1000);

		Kill(*PlayerBarracks);
		TestTrue(TEXT("The match is over"), GameState->IsMatchOver());
		const int32 Issued = AI->GetIssuedCommandCount();
		AI->Think(ARTSAIPlayer::ThinkInterval);
		TestEqual(TEXT("Once it is over the AI orders nothing"), AI->GetIssuedCommandCount(), Issued);
		RTSCommands::Issue(*Player, {}, Produce);
		Produce.TargetActor = AIBarracks;
		RTSCommands::Issue(*Player, {}, Produce);
		TestEqual(TEXT("And its orders are refused like a player's"), AIBarracks->GetProductionComponent()->GetQueue().Num(), 1);
		TestEqual(TEXT("Uncharged"), Player->GetCredits(), 900);
		return true;
	}
}

#endif
