#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSHUD.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSMatchTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	URTSBuildingData* MakeBuilding(bool bIsDefense = false)
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(4, 4);
		Data->MaxHealth = 1000.f;
		Data->BuildTime = 2.f;
		Data->Cost = 400;
		Data->bIsDefense = bIsDefense;
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

	bool HasTeams(const ARTSGameState& GameState, const TArray<int32>& Teams)
	{
		return GameState.GetAliveTeams() == Teams;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMatchEliminationTest, "WorldDivide.Match.Elimination", TestFlags)

	bool FRTSMatchEliminationTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSGameState* GameState = TestWorld.SpawnGameState();
		TestWorld.SpawnPlayer(0, 0);
		TestWorld.SpawnPlayer(1, 0);
		URTSBuildingData* HeadquartersData = MakeBuilding();
		URTSBuildingData* BarracksData = MakeBuilding();
		URTSBuildingData* TowerData = MakeBuilding(/*bIsDefense*/ true);

		ARTSBuilding* Headquarters = SpawnCompleted(TestWorld, HeadquartersData, 0, FVector(0.f, 0.f, 0.f));
		ARTSBuilding* EnemyHeadquarters = SpawnCompleted(TestWorld, HeadquartersData, 1, FVector(3000.f, 0.f, 0.f));
		ARTSBuilding* EnemyBarracks = SpawnCompleted(TestWorld, BarracksData, 1, FVector(6000.f, 0.f, 0.f));
		ARTSBuilding* EnemyTower = SpawnCompleted(TestWorld, TowerData, 1, FVector(9000.f, 0.f, 0.f));
		TestTrue(TEXT("Both teams are in the match"), HasTeams(*GameState, { 0, 1 }));
		TestFalse(TEXT("The match runs"), GameState->IsMatchOver());
		TestTrue(TEXT("A running match has no result"), GameState->GetMatchResult(0) == ERTSMatchResult::InProgress);

		EnemyHeadquarters->GetHealthComponent()->ApplyDamage(HeadquartersData->MaxHealth * 0.5f, nullptr);
		TestFalse(TEXT("A damaged headquarters changes nothing"), GameState->IsMatchOver());

		Kill(*EnemyHeadquarters);
		TestTrue(TEXT("The barracks keeps the enemy in the match"), HasTeams(*GameState, { 0, 1 }));
		TestFalse(TEXT("Losing the headquarters alone ends nothing"), GameState->IsMatchOver());

		Kill(*EnemyBarracks);
		TestTrue(TEXT("The enemy tower is still standing"), IsValid(EnemyTower) && !EnemyTower->IsActorBeingDestroyed());
		TestTrue(TEXT("A defence keeps no team in the match"), HasTeams(*GameState, { 0 }));
		TestTrue(TEXT("The match is over"), GameState->IsMatchOver());
		TestTrue(TEXT("The world reports it over"), ARTSGameState::IsMatchOver(*TestWorld.World));
		TestEqual(TEXT("Team 0 wins"), GameState->GetWinningTeam(), 0);
		TestTrue(TEXT("Victory for the winner"), GameState->GetMatchResult(0) == ERTSMatchResult::Victory);
		TestTrue(TEXT("Defeat for the loser"), GameState->GetMatchResult(1) == ERTSMatchResult::Defeat);

		Kill(*Headquarters);
		TestEqual(TEXT("A loss after the end changes no winner"), GameState->GetWinningTeam(), 0);
		TestTrue(TEXT("Nor the winner's result"), GameState->GetMatchResult(0) == ERTSMatchResult::Victory);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMatchConstructionSiteTest, "WorldDivide.Match.ConstructionSite", TestFlags)

	bool FRTSMatchConstructionSiteTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSGameState* GameState = TestWorld.SpawnGameState();
		TestWorld.SpawnPlayer(0, 0);
		ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 0);
		URTSBuildingData* HeadquartersData = MakeBuilding();
		URTSBuildingData* BarracksData = MakeBuilding();

		SpawnCompleted(TestWorld, HeadquartersData, 0, FVector(0.f, 0.f, 0.f));
		ARTSBuilding* EnemyHeadquarters = SpawnCompleted(TestWorld, HeadquartersData, 1, FVector(3000.f, 0.f, 0.f));
		ARTSBuilding* EnemySite = TestWorld.SpawnBuilding(BarracksData, 1, FVector(6000.f, 0.f, 0.f));
		TestFalse(TEXT("The site is under construction"), EnemySite->IsConstructed());

		Kill(*EnemyHeadquarters);
		TestTrue(TEXT("A construction site keeps its team in the match"), HasTeams(*GameState, { 0, 1 }));
		TestFalse(TEXT("The match runs"), GameState->IsMatchOver());

		FRTSCommand Cancel;
		Cancel.Type = ERTSCommandType::CancelConstruction;
		Cancel.TargetActor = EnemySite;
		TestTrue(TEXT("The enemy cancels its last site"), RTSCommands::CancelConstruction(*Enemy, Cancel) != INDEX_NONE);
		TestTrue(TEXT("Cancelling the last building ends the match"), GameState->IsMatchOver());
		TestEqual(TEXT("Team 0 wins"), GameState->GetWinningTeam(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMatchThreeTeamsTest, "WorldDivide.Match.ThreeTeams", TestFlags)

	bool FRTSMatchThreeTeamsTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSGameState* GameState = TestWorld.SpawnGameState();
		URTSBuildingData* HeadquartersData = MakeBuilding();

		ARTSBuilding* First = SpawnCompleted(TestWorld, HeadquartersData, 0, FVector(0.f, 0.f, 0.f));
		SpawnCompleted(TestWorld, HeadquartersData, 1, FVector(3000.f, 0.f, 0.f));
		ARTSBuilding* Third = SpawnCompleted(TestWorld, HeadquartersData, 2, FVector(6000.f, 0.f, 0.f));
		TestTrue(TEXT("Three teams are in the match"), HasTeams(*GameState, { 0, 1, 2 }));

		Kill(*Third);
		TestTrue(TEXT("Team 2 is out"), HasTeams(*GameState, { 0, 1 }));
		TestFalse(TEXT("Two teams left play on"), GameState->IsMatchOver());

		Kill(*First);
		TestTrue(TEXT("The second elimination ends the match"), GameState->IsMatchOver());
		TestEqual(TEXT("Team 1 wins"), GameState->GetWinningTeam(), 1);
		TestTrue(TEXT("Defeat for the first team out"), GameState->GetMatchResult(2) == ERTSMatchResult::Defeat);
		TestTrue(TEXT("Defeat for the last team out"), GameState->GetMatchResult(0) == ERTSMatchResult::Defeat);
		TestTrue(TEXT("Victory for the team left"), GameState->GetMatchResult(1) == ERTSMatchResult::Victory);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMatchWithoutGameStateTest, "WorldDivide.Match.WithoutGameState", TestFlags)

	bool FRTSMatchWithoutGameStateTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		URTSBuildingData* HeadquartersData = MakeBuilding();
		SpawnCompleted(TestWorld, HeadquartersData, 0, FVector(0.f, 0.f, 0.f));
		ARTSBuilding* EnemyHeadquarters = SpawnCompleted(TestWorld, HeadquartersData, 1, FVector(3000.f, 0.f, 0.f));

		Kill(*EnemyHeadquarters);
		TestFalse(TEXT("A world without the match game state never ends a match"), ARTSGameState::IsMatchOver(*TestWorld.World));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMatchResultTitleTest, "WorldDivide.Match.ResultTitle", TestFlags)

	bool FRTSMatchResultTitleTest::RunTest(const FString& Parameters)
	{
		TestEqual(TEXT("The winner's pop-up"), ARTSHUD::GetMatchResultTitle(ERTSMatchResult::Victory), FString(TEXT("Victory!")));
		TestEqual(TEXT("The loser's pop-up"), ARTSHUD::GetMatchResultTitle(ERTSMatchResult::Defeat), FString(TEXT("Defeated!")));
		TestTrue(TEXT("No pop-up while the match runs"), ARTSHUD::GetMatchResultTitle(ERTSMatchResult::InProgress).IsEmpty());
		TestFalse(TEXT("The winner reads why"), ARTSHUD::GetMatchResultMessage(ERTSMatchResult::Victory).IsEmpty());
		TestFalse(TEXT("The loser reads why"), ARTSHUD::GetMatchResultMessage(ERTSMatchResult::Defeat).IsEmpty());
		TestTrue(TEXT("No message while the match runs"), ARTSHUD::GetMatchResultMessage(ERTSMatchResult::InProgress).IsEmpty());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSScoreboardLabelTest, "WorldDivide.Match.ScoreboardLabel", TestFlags)

	bool FRTSScoreboardLabelTest::RunTest(const FString& Parameters)
	{
		const FString Name = TEXT("Commander");
		TestEqual(TEXT("A human by its name"), ARTSPlayerState::GetScoreboardLabel(Name, false, ERTSAIDifficulty::Normal, true), Name);
		TestEqual(TEXT("An easy AI"), ARTSPlayerState::GetScoreboardLabel(Name, true, ERTSAIDifficulty::Easy, true), FString(TEXT("AI (Easy)")));
		TestEqual(TEXT("A normal AI"), ARTSPlayerState::GetScoreboardLabel(Name, true, ERTSAIDifficulty::Normal, true), FString(TEXT("AI (Normal)")));
		TestEqual(TEXT("A hard AI"), ARTSPlayerState::GetScoreboardLabel(Name, true, ERTSAIDifficulty::Hard, true), FString(TEXT("AI (Hard)")));
		TestEqual(TEXT("An eliminated human"), ARTSPlayerState::GetScoreboardLabel(Name, false, ERTSAIDifficulty::Normal, false),
			FString(TEXT("Commander (eliminated)")));
		TestEqual(TEXT("An eliminated AI"), ARTSPlayerState::GetScoreboardLabel(Name, true, ERTSAIDifficulty::Hard, false),
			FString(TEXT("AI (Hard) (eliminated)")));
		return true;
	}
}

#endif
