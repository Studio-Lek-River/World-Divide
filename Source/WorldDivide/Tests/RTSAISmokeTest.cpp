#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAIBuildOrderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestPIE.h"
#include "Units/RTSUnit.h"

/**
 * End to end on L_Skirmish, where the skirmish game mode gives team 1 to an AI opponent. The base test watches the AI work
 * through its build order: each building placed in order near its headquarters and completed, the units produced, its
 * harvester earning credits, and a wave of its own infantry attacking the player's headquarters. The win test gives the AI
 * a tank column and checks its wave destroys the player's headquarters, the player's only building, so the AI wins and
 * then orders nothing more. The loss test destroys the AI's buildings: the player wins and the AI falls silent.
 */
namespace RTSAISmokeTest
{
	const TCHAR* SkirmishMap = TEXT("/Game/Maps/L_Skirmish");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	constexpr int32 PlayerTeam = 0;
	constexpr int32 AITeam = 1;
	constexpr int32 StartingCredits = 10000;

	/** Real seconds for PIE and the navmesh to come up. */
	constexpr double StartTimeout = 60.0;

	/** Game seconds the AI gets to complete its build order, and real seconds for that at real time. */
	constexpr float BaseGameSeconds = 300.f;
	constexpr double BaseTimeout = 400.0;

	/** How far from its headquarters the AI's buildings may stand, in cm. */
	constexpr float BaseRadius = 4500.f;

	/** Game seconds for a wave to reach the player's headquarters and damage it, or destroy it. */
	constexpr float WaveGameSeconds = 150.f;
	constexpr double WaveTimeout = 250.0;

	/** Game seconds the AI is watched after the match ends. */
	constexpr float SilentGameSeconds = 4.f;

	/** The tank column the win test gives the AI. */
	constexpr int32 ColumnSize = 6;

	struct FAISmokeState
	{
		double PhaseStart = 0.0;
		float PhaseWorldStart = 0.f;
		TWeakObjectPtr<ARTSAIPlayer> AI;
		TWeakObjectPtr<ARTSBuilding> AIHeadquarters;
		TWeakObjectPtr<ARTSBuilding> PlayerHeadquarters;
		/** Each building of the build order, in the order the AI placed its first one. */
		TArray<const URTSBuildingData*> PlacedOrder;
		int32 LastCredits = 0;
		bool bEarned = false;
		bool bWaveSoon = false;
		int32 IssuedAtEnd = 0;
	};

	UWorld* GetWorld()
	{
		return GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	}

	ARTSPlayerController* GetController(UWorld* World)
	{
		return World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	/** The completed headquarters of TeamId, or null. */
	ARTSBuilding* FindHeadquarters(UWorld& World, int32 TeamId)
	{
		const ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(World, TeamId);
		const URTSBuildingData* Data = Player && Player->GetFaction() ? Player->GetFaction()->Headquarters.Get() : nullptr;
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			if (It->GetTeamId() == TeamId && It->GetBuildingData() == Data && !It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Completed buildings or living units of Entry that TeamId owns. */
	int32 CountCompleted(UWorld& World, int32 TeamId, const URTSEntityData* Entry)
	{
		int32 Count = 0;
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			Count += It->GetTeamId() == TeamId && It->GetBuildingData() == Entry && It->IsConstructed()
				&& !It->IsActorBeingDestroyed() ? 1 : 0;
		}
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			Count += It->GetTeamId() == TeamId && It->GetUnitData() == Entry && !It->IsActorBeingDestroyed() ? 1 : 0;
		}
		return Count;
	}

	/** What the AI has, for a failure: its credits, its build order's counts, its units and buildings. */
	void DescribeAI(FAutomationTestBase& Test, const FAISmokeState& State, UWorld& World)
	{
		const ARTSAIPlayer* AI = State.AI.Get();
		const URTSAIBuildOrderData* BuildOrder = AI ? AI->GetBuildOrder() : nullptr;
		if (!AI || !BuildOrder)
		{
			return;
		}
		Test.AddInfo(FString::Printf(TEXT("AI credits %d, %d orders, %d waves"), AI->GetRTSPlayerState()->GetCredits(),
			AI->GetIssuedCommandCount(), AI->GetWaveCount()));
		for (const FRTSBuildOrderStep& Step : BuildOrder->Steps)
		{
			Test.AddInfo(FString::Printf(TEXT("  step %s x%d: counted %d, completed %d"), *GetNameSafe(Step.Entry), Step.Count,
				Step.Entry ? AI->CountOwned(*Step.Entry) : 0, CountCompleted(World, AITeam, Step.Entry)));
		}
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			Test.AddInfo(FString::Printf(TEXT("  team %d %s at %s, %d orders"), It->GetTeamId(), *GetNameSafe(It->GetUnitData()),
				*It->GetActorLocation().ToCompactString(), It->GetQueuedCommandCount()));
		}
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			Test.AddInfo(FString::Printf(TEXT("  team %d %s at %s, %.0f%% built, health %.0f"), It->GetTeamId(),
				*GetNameSafe(It->GetBuildingData()), *It->GetActorLocation().ToCompactString(),
				It->GetConstructionProgress() * 100.f, It->GetHealthComponent()->GetHealth()));
		}
	}

	bool TimedOut(FAutomationTestBase& Test, const FAISmokeState& State, UWorld& World, float GameSeconds, double RealSeconds,
		const TCHAR* What)
	{
		if (World.GetTimeSeconds() - State.PhaseWorldStart > GameSeconds || FPlatformTime::Seconds() - State.PhaseStart > RealSeconds)
		{
			Test.AddError(FString::Printf(TEXT("%s within %.0f game seconds"), What, GameSeconds));
			DescribeAI(Test, State, World);
			return true;
		}
		return false;
	}

	void StartPhase(FAISmokeState& State, const UWorld& World)
	{
		State.PhaseStart = FPlatformTime::Seconds();
		State.PhaseWorldStart = World.GetTimeSeconds();
	}


	/**
	 * Waits for the skirmish to run with its navmesh built, then checks the AI took team 1 like a player: a bot player state
	 * with the default faction, the starting credits and a headquarters, while the player has team 0.
	 */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForSkirmish, FAutomationTestBase*, Test, TSharedRef<FAISmokeState>, State);
	bool FRTSWaitForSkirmish::Update()
	{
		if (State->PhaseStart == 0.0)
		{
			State->PhaseStart = FPlatformTime::Seconds();
		}
		UWorld* World = RTSAISmokeTest::GetWorld();
		const ARTSPlayerController* Controller = GetController(World);
		const bool bPlaying = World && World->HasBegunPlay() && Controller && Controller->GetFaction() && Controller->GetCredits() > 0;
		UNavigationSystemV1* NavSystem = bPlaying ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		const ARecastNavMesh* NavMesh = NavSystem ? Cast<ARecastNavMesh>(NavSystem->GetDefaultNavDataInstance()) : nullptr;
		if (!NavMesh || NavMesh->GetNavMeshTilesCount() == 0 || !RTSTests::IsNavigationReady(*World))
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > StartTimeout)
			{
				Test->AddError(TEXT("PIE did not start or the navmesh was not built in time"));
				return true;
			}
			return false;
		}
		RTSTests::ApplyTestTimeDilation(*World);

		TArray<ARTSAIPlayer*> Opponents;
		for (TActorIterator<ARTSAIPlayer> It(World); It; ++It)
		{
			Opponents.Add(*It);
		}
		Test->TestEqual(TEXT("The player has team 0"), Controller->GetTeamId(), PlayerTeam);
		if (!Test->TestEqual(TEXT("One AI opponent"), Opponents.Num(), 1))
		{
			return true;
		}
		ARTSAIPlayer* AI = Opponents[0];
		const ARTSPlayerState* Player = AI->GetRTSPlayerState();
		if (!Test->TestNotNull(TEXT("The AI has an RTS player state"), Player))
		{
			return true;
		}
		Test->TestEqual(TEXT("The AI plays team 1"), Player->GetTeamId(), AITeam);
		Test->TestTrue(TEXT("Its player state is a bot's"), Player->IsABot());
		Test->TestTrue(TEXT("FindForTeam finds it"), ARTSPlayerState::FindForTeam(*World, AITeam) == Player);
		Test->TestEqual(TEXT("It plays the player's faction"), Player->GetFaction(), Controller->GetFaction());
		Test->TestNotNull(TEXT("The faction has a build order"), AI->GetBuildOrder());
		State->AI = AI;
		State->AIHeadquarters = FindHeadquarters(*World, AITeam);
		State->PlayerHeadquarters = FindHeadquarters(*World, PlayerTeam);
		Test->TestTrue(TEXT("The AI has its headquarters"), State->AIHeadquarters.IsValid());
		Test->TestTrue(TEXT("The player has its headquarters"), State->PlayerHeadquarters.IsValid());
		Test->TestTrue(TEXT("The AI has at most its starting credits"), Player->GetCredits() <= StartingCredits);
		State->LastCredits = Player->GetCredits();
		StartPhase(*State, *World);
		return true;
	}

	/**
	 * Follows the AI until every step of its build order stands completed, recording the order its buildings appear in and
	 * whether its credits ever rise, which only a harvester delivering can do.
	 */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyAIBuildsBase, FAutomationTestBase*, Test, TSharedRef<FAISmokeState>, State);
	bool FRTSVerifyAIBuildsBase::Update()
	{
		ARTSAIPlayer* AI = State->AI.Get();
		const ARTSBuilding* Headquarters = State->AIHeadquarters.Get();
		UWorld* World = AI ? AI->GetWorld() : nullptr;
		const URTSAIBuildOrderData* BuildOrder = AI ? AI->GetBuildOrder() : nullptr;
		if (!World || !BuildOrder || !Headquarters)
		{
			Test->AddError(TEXT("Lost the AI, its build order or its headquarters"));
			return true;
		}

		const int32 Credits = AI->GetRTSPlayerState()->GetCredits();
		State->bEarned |= Credits > State->LastCredits;
		State->LastCredits = Credits;

		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			const URTSBuildingData* Data = It->GetBuildingData();
			if (It->GetTeamId() == AITeam && *It != Headquarters && !State->PlacedOrder.Contains(Data))
			{
				State->PlacedOrder.Add(Data);
				Test->TestTrue(FString::Printf(TEXT("%s stands near the AI's headquarters"), *GetNameSafe(Data)),
					FVector::Dist2D(It->GetActorLocation(), Headquarters->GetActorLocation()) < BaseRadius);
			}
		}

		bool bDone = true;
		for (const FRTSBuildOrderStep& Step : BuildOrder->Steps)
		{
			bDone &= CountCompleted(*World, AITeam, Step.Entry) >= Step.Count;
		}
		if (!bDone)
		{
			return TimedOut(*Test, *State, *World, BaseGameSeconds, BaseTimeout, TEXT("The AI did not complete its build order"));
		}

		TArray<const URTSBuildingData*> Expected;
		for (const FRTSBuildOrderStep& Step : BuildOrder->Steps)
		{
			const URTSBuildingData* Data = Cast<URTSBuildingData>(Step.Entry);
			if (Data && Data != Headquarters->GetBuildingData())
			{
				Expected.AddUnique(Data);
			}
		}
		Test->TestEqual(TEXT("The AI placed its buildings in build order"), State->PlacedOrder, Expected);
		Test->TestTrue(TEXT("The AI's harvester earned credits"), State->bEarned);
		Test->AddInfo(FString::Printf(TEXT("Build order done after %.0f game seconds"), World->GetTimeSeconds() - State->PhaseWorldStart));
		StartPhase(*State, *World);
		return true;
	}

	/** Brings the AI's next wave on with a few units and waits until it damages the player's headquarters. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyAIAttacks, FAutomationTestBase*, Test, TSharedRef<FAISmokeState>, State);
	bool FRTSVerifyAIAttacks::Update()
	{
		ARTSAIPlayer* AI = State->AI.Get();
		const ARTSBuilding* Target = State->PlayerHeadquarters.Get();
		UWorld* World = AI ? AI->GetWorld() : nullptr;
		if (!World || !Target)
		{
			Test->AddError(TEXT("Lost the AI or the player's headquarters"));
			return true;
		}
		if (!State->bWaveSoon)
		{
			FRTSAIWaveSettings Settings;
			Settings.Size = 3;
			Settings.MinSize = 3;
			AI->SetWaveSettings(Settings);
			State->bWaveSoon = true;
		}
		const URTSHealthComponent* Health = Target->GetHealthComponent();
		if (AI->GetWaveCount() == 0 || Health->GetHealth() >= Health->GetMaxHealth())
		{
			return TimedOut(*Test, *State, *World, WaveGameSeconds, WaveTimeout, TEXT("No AI wave damaged the player's headquarters"));
		}
		Test->AddInfo(FString::Printf(TEXT("%d waves sent; the player's headquarters is down to %.0f"), AI->GetWaveCount(), Health->GetHealth()));
		return true;
	}

	/** Gives the AI a tank column beside its headquarters and sends it as one wave. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSGiveAIColumn, FAutomationTestBase*, Test, TSharedRef<FAISmokeState>, State);
	bool FRTSGiveAIColumn::Update()
	{
		ARTSAIPlayer* AI = State->AI.Get();
		const ARTSBuilding* Headquarters = State->AIHeadquarters.Get();
		URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
		UWorld* World = AI ? AI->GetWorld() : nullptr;
		if (!World || !Headquarters || !Tank)
		{
			Test->AddError(TEXT("Lost the AI or its headquarters, or the test tank is missing"));
			return true;
		}
		for (int32 Index = 0; Index < ColumnSize; ++Index)
		{
			const FTransform Transform(Headquarters->GetActorLocation() + FVector(-800.f, (Index - ColumnSize / 2) * 250.f, 0.f));
			ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Unit->InitFromData(Tank, AITeam);
			Unit->FinishSpawning(Transform);
		}
		FRTSAIWaveSettings Settings;
		Settings.Size = ColumnSize;
		Settings.MinSize = ColumnSize;
		AI->SetWaveSettings(Settings);
		StartPhase(*State, *World);
		return true;
	}

	/** Waits for the match to end with the player's headquarters destroyed and the AI the winner. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyAIWins, FAutomationTestBase*, Test, TSharedRef<FAISmokeState>, State);
	bool FRTSVerifyAIWins::Update()
	{
		ARTSAIPlayer* AI = State->AI.Get();
		UWorld* World = AI ? AI->GetWorld() : nullptr;
		const ARTSGameState* GameState = World ? World->GetGameState<ARTSGameState>() : nullptr;
		const ARTSPlayerController* Controller = GetController(World);
		if (!GameState || !Controller)
		{
			Test->AddError(TEXT("Lost the AI, the game state or the player's controller"));
			return true;
		}
		if (!GameState->IsMatchOver())
		{
			return TimedOut(*Test, *State, *World, WaveGameSeconds, WaveTimeout, TEXT("The AI did not destroy the player's headquarters"));
		}
		Test->TestFalse(TEXT("The player's headquarters is destroyed"), State->PlayerHeadquarters.IsValid()
			&& !State->PlayerHeadquarters->IsActorBeingDestroyed());
		Test->TestTrue(TEXT("The AI sent a wave"), AI->GetWaveCount() > 0);
		Test->TestEqual(TEXT("The AI's team wins"), GameState->GetWinningTeam(), AITeam);
		Test->TestTrue(TEXT("The player is defeated"), Controller->GetMatchResult() == ERTSMatchResult::Defeat);
		State->IssuedAtEnd = AI->GetIssuedCommandCount();
		StartPhase(*State, *World);
		return true;
	}

	/** Destroys every AI building; the player must win. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSDestroyAIBase, FAutomationTestBase*, Test, TSharedRef<FAISmokeState>, State);
	bool FRTSDestroyAIBase::Update()
	{
		ARTSAIPlayer* AI = State->AI.Get();
		UWorld* World = AI ? AI->GetWorld() : nullptr;
		const ARTSGameState* GameState = World ? World->GetGameState<ARTSGameState>() : nullptr;
		const ARTSPlayerController* Controller = GetController(World);
		if (!GameState || !Controller)
		{
			Test->AddError(TEXT("Lost the AI, the game state or the player's controller"));
			return true;
		}
		TArray<ARTSBuilding*> Buildings;
		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			if (It->GetTeamId() == AITeam)
			{
				Buildings.Add(*It);
			}
		}
		Test->TestFalse(TEXT("The match runs while the AI has its base"), GameState->IsMatchOver());
		for (ARTSBuilding* Building : Buildings)
		{
			Building->GetHealthComponent()->ApplyDamage(Building->GetHealthComponent()->GetMaxHealth(), nullptr);
		}
		Test->TestTrue(TEXT("The match is over"), GameState->IsMatchOver());
		Test->TestEqual(TEXT("The player's team wins"), GameState->GetWinningTeam(), PlayerTeam);
		Test->TestTrue(TEXT("The player has won"), Controller->GetMatchResult() == ERTSMatchResult::Victory);
		State->IssuedAtEnd = AI->GetIssuedCommandCount();
		StartPhase(*State, *World);
		return true;
	}

	/** For a few game seconds after the end, the AI orders nothing and places nothing. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyAISilent, FAutomationTestBase*, Test, TSharedRef<FAISmokeState>, State);
	bool FRTSVerifyAISilent::Update()
	{
		ARTSAIPlayer* AI = State->AI.Get();
		UWorld* World = AI ? AI->GetWorld() : nullptr;
		if (!World)
		{
			Test->AddError(TEXT("Lost the AI"));
			return true;
		}
		if (World->GetTimeSeconds() - State->PhaseWorldStart < SilentGameSeconds)
		{
			return false;
		}
		Test->TestEqual(TEXT("The AI orders nothing once the match is over"), AI->GetIssuedCommandCount(), State->IssuedAtEnd);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIBuildsBaseSmokeTest, "WorldDivide.Smoke.AIBuildsBase", RTSTests::TestFlags)

	bool FRTSAIBuildsBaseSmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(SkirmishMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), SkirmishMap));
			return false;
		}
		const TSharedRef<FAISmokeState> State = MakeShared<FAISmokeState>();
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForSkirmish(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAIBuildsBase(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAIAttacks(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIWinsSmokeTest, "WorldDivide.Smoke.AIWins", RTSTests::TestFlags)

	bool FRTSAIWinsSmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(SkirmishMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), SkirmishMap));
			return false;
		}
		const TSharedRef<FAISmokeState> State = MakeShared<FAISmokeState>();
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForSkirmish(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSGiveAIColumn(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAIWins(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAISilent(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAILosesSmokeTest, "WorldDivide.Smoke.AILoses", RTSTests::TestFlags)

	bool FRTSAILosesSmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(SkirmishMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), SkirmishMap));
			return false;
		}
		const TSharedRef<FAISmokeState> State = MakeShared<FAISmokeState>();
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForSkirmish(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSDestroyAIBase(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAISilent(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}
}

#endif
