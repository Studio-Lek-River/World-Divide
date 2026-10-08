#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
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

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIWavesTest, "WorldDivide.AI.Waves", TestFlags)

	bool FRTSAIWavesTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		URTSWeaponData* Weapon = MakeWeapon();
		URTSUnitData* Tank = MakeUnit(700, Weapon);
		URTSBuilderData* Builder = NewObject<URTSBuilderData>();
		Builder->Weapon = Weapon;
		URTSHarvesterData* Harvester = NewObject<URTSHarvesterData>();
		Harvester->Weapon = Weapon;

		ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-3000.f, 0.f, 0.f));
		SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(3000.f, 0.f, 0.f));
		// Armed builders and harvesters keep working; only the rest is army.
		TestWorld.SpawnUnit(Builder, AITeam, FVector(2500.f, 600.f, 0.f));
		TestWorld.SpawnUnit(Harvester, AITeam, FVector(2500.f, -600.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(2000.f, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(2000.f, 300.f, 0.f));
		TestWorld.SpawnUnit(Tank, PlayerTeam, FVector(-2000.f, 0.f, 0.f));

		FRTSAIWaveSettings Settings;
		Settings.Size = 3;
		Settings.MinSize = 2;
		Settings.Interval = 30.f;
		AI->SetWaveSettings(Settings);

		AI->Think(10.f);
		TestEqual(TEXT("Two tanks are no full wave, and the interval has not passed"), AI->GetWaveCount(), 0);
		TestWorld.SpawnUnit(Tank, AITeam, FVector(2000.f, -300.f, 0.f));
		AI->Think(1.f);
		TestEqual(TEXT("A third tank fills the wave"), AI->GetWaveCount(), 1);
		const TArray<ARTSUnit*> Wave = AI->GetWaveUnits();
		TestEqual(TEXT("The wave holds the three tanks"), Wave.Num(), 3);
		for (const ARTSUnit* Unit : Wave)
		{
			TestEqual(TEXT("Only AI tanks go"), Unit->GetUnitData(), static_cast<URTSUnitData*>(Tank));
			TestEqual(TEXT("Of the AI's team"), Unit->GetTeamId(), AITeam);
		}

		TestWorld.SpawnUnit(Tank, AITeam, FVector(1800.f, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(1800.f, 300.f, 0.f));
		AI->Think(10.f);
		TestEqual(TEXT("Two new tanks wait for the interval"), AI->GetWaveCount(), 1);
		AI->Think(20.f);
		TestEqual(TEXT("Once it has passed, the minimum goes"), AI->GetWaveCount(), 2);
		TestEqual(TEXT("Both waves are tracked"), AI->GetWaveUnits().Num(), 5);
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
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIDefendTest, "WorldDivide.AI.Defend", TestFlags)

	bool FRTSAIDefendTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		URTSUnitData* Tank = MakeUnit(700, MakeWeapon());

		ARTSAIPlayer* AI = SpawnAI(TestWorld, NewObject<URTSAIBuildOrderData>(), 0);
		SpawnCompleted(TestWorld, MakeBuilding(), PlayerTeam, FVector(-3000.f, 0.f, 0.f));
		SpawnCompleted(TestWorld, MakeBuilding(), AITeam, FVector(3000.f, 0.f, 0.f));
		ARTSUnit* Enemy = TestWorld.SpawnUnit(Tank, PlayerTeam, FVector(-2000.f, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(1500.f, 0.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(1500.f, 300.f, 0.f));
		TestWorld.SpawnUnit(Tank, AITeam, FVector(1500.f, -300.f, 0.f));

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
		FarWaveUnit->SetActorLocation(FVector(-1500.f, 0.f, 0.f));
		ARTSUnit* ArmyA = TestWorld.SpawnUnit(Tank, AITeam, FVector(1800.f, 0.f, 0.f));
		ARTSUnit* ArmyB = TestWorld.SpawnUnit(Tank, AITeam, FVector(1800.f, 300.f, 0.f));
		AI->Think(1.f);
		TestFalse(TEXT("Still no threat"), AI->IsDefending());
		TestEqual(TEXT("Nobody defends"), AI->GetDefenders().Num(), 0);

		Enemy->SetActorLocation(FVector(2000.f, 1500.f, 0.f));
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
