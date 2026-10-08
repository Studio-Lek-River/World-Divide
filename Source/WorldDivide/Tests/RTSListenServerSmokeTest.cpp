#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "PlayInEditorDataTypes.h"
#include "Player/RTSPlayerController.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Units/RTSUnit.h"

/**
 * Plays L_TestMap as a listen server with one client in the same process. Checks that the host gets team 0 and the
 * client team 1 at their tagged player starts, and that the client sees its own credits but not the host's, then sends a move for every unit the client sees through the client's
 * own controller: the server must move only the client's units, and the client must see them arrive. While its
 * headquarters stands, the client's controller does not start a ghost for a second one and the server refuses the order
 * its builder sends anyway, uncharged. The client then
 * sends its builder to buy a construction site and must see its credits drop before the site is placed, then stops the
 * builder and cancels the site: the site must disappear in both
 * worlds and the client must see its refund while the host's credits stay put. The server then gives the client a
 * barracks: the client sets its rally point, queues two infantry, sees them charged and cancels one for a refund, and
 * must see the other come out and walk to the rally point. At the same barracks the client researches rifle training: it sees
 * the charge and the upgrade queued, then researched, while the host's player state stays without it; only then is the
 * grenadier it gates accepted. Then the server places a host building and the client orders its units to attack it: the client's copy must show the building's
 * health dropping before it disappears. Last, the server gives the client a tank in the empty south strip and puts a host
 * infantry beside its lane; the client attack-moves the tank past it and must see the infantry disappear and the tank arrive.
 * Finally the server destroys the host's headquarters, its last building: the client must see Victory for itself and the
 * host Defeat, only the host may restart, and a move the client sends afterwards is refused.
 */
namespace RTSListenServerTest
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const FVector MoveTarget(0.f, 0.f, 0.f);
	constexpr int32 UnitsPerTeam = 18;
	constexpr float ArrivalRadius = 800.f;
	constexpr float UnmovedTolerance = 50.f;
	constexpr double SessionTimeout = 60.0;
	constexpr double MoveTimeout = 60.0;

	/** The empty strip north of both bases, far out of range of every unit. */
	const FVector HostBuildingSpot(0.f, 7000.f, 0.f);
	const FVector ClientSiteSpot(-3000.f, 7000.f, 0.f);
	const TCHAR* PowerPlantPath = TEXT("/Game/Data/Buildings/DA_TestPowerPlant.DA_TestPowerPlant");
	constexpr double ReplicationTimeout = 15.0;
	/** The client's builder walks from the middle of the map to its site before placing it. */
	constexpr double ClientSiteTimeout = 60.0;
	constexpr double BuildingAttackTimeout = 60.0;

	/** The client's barracks stands north of its cancelled site, clear of the hole that site left; its infantry gather east of it. */
	const TCHAR* BarracksPath = TEXT("/Game/Data/Buildings/DA_TestBarracks.DA_TestBarracks");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");
	const FVector ClientBarracksSpot(-3000.f, 8500.f, 0.f);
	const FVector ClientRallySpot(-1800.f, 8900.f, 0.f);
	constexpr float RallyArrivalRadius = 250.f;
	/** Without a power plant the client produces at the low-power rate. */
	constexpr double ProductionTimeout = 60.0;

	/** The client researches rifle training at its barracks, which the grenadier needs. Without power it takes a third longer. */
	const TCHAR* RifleTrainingPath = TEXT("/Game/Data/Upgrades/DA_TestRifleTraining.DA_TestRifleTraining");
	const TCHAR* GrenadierPath = TEXT("/Game/Data/Units/DA_TestGrenadier.DA_TestGrenadier");
	constexpr double ResearchTimeout = 60.0;

	/**
	 * The client's tank attack-moves along the empty strip south of the bases, away from the units gathered at the host
	 * building. The host infantry stands beside the lane, inside the tank's range as it passes but outside its own.
	 */
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const FVector ClientTankStart(-3000.f, -7000.f, 0.f);
	const FVector ClientTankTarget(3000.f, -7000.f, 0.f);
	const FVector HostInfantrySpot(0.f, -7900.f, 0.f);
	constexpr float AttackMoveArrivalRadius = 150.f;
	constexpr double AttackMoveTimeout = 60.0;

	/** Each team starts with one; the checks on buildings the test places leave them out. */
	const TCHAR* HeadquartersPath = TEXT("/Game/Data/Buildings/DA_TestHeadquarters.DA_TestHeadquarters");
	/** Game seconds the client's refused order is watched for any movement. */
	constexpr double RefusedOrderSeconds = 2.0;

	constexpr int32 HostTeam = 0;
	constexpr int32 ClientTeam = 1;

	struct FSessionState
	{
		TWeakObjectPtr<UWorld> ServerWorld;
		TWeakObjectPtr<UWorld> ClientWorld;
		double PhaseStart = 0.0;

		/** Indexed by team. Each world's own copies, so the client checks what replication gave it. */
		TArray<TWeakObjectPtr<ARTSUnit>> ServerUnits[2];
		TArray<TWeakObjectPtr<ARTSUnit>> ClientUnits[2];
		TArray<FVector> ServerHostStarts;
		TArray<FVector> ClientHostStarts;

		/** The host building the client attacks, as each world sees it. */
		TWeakObjectPtr<ARTSBuilding> ServerBuilding;
		TWeakObjectPtr<ARTSBuilding> ClientBuilding;
		float ClientMaxHealth = 0.f;
		float ClientLowestHealth = 0.f;

		/** The construction site the client's builder buys and the client cancels, as each world sees it, and the credits around it. */
		TWeakObjectPtr<ARTSUnit> ClientBuilder;
		bool bClientBuilderStopped = false;
		TWeakObjectPtr<ARTSBuilding> ServerSite;
		TWeakObjectPtr<ARTSBuilding> ClientSite;
		int32 ClientCreditsBefore = 0;
		int32 ExpectedClientCredits = 0;
		int32 HostCredits = 0;

		/** The barracks the server gives the client, as each world sees it. */
		TWeakObjectPtr<ARTSBuilding> ServerBarracks;
		TWeakObjectPtr<ARTSBuilding> ClientBarracks;
		int32 ClientProductionCredits = 0;
		int32 ClientResearchCredits = 0;

		/** The client's attack-moving tank and the host infantry beside its lane, as each world sees them. */
		TWeakObjectPtr<ARTSUnit> ServerTank;
		TWeakObjectPtr<ARTSUnit> ClientTank;
		TWeakObjectPtr<ARTSUnit> ServerInfantry;
		TWeakObjectPtr<ARTSUnit> ClientInfantry;

		/** Where the client's tank stood when the server got its order after the match. */
		FVector RefusedStart = FVector::ZeroVector;
		int32 RefusedQueue = 0;
		double RefusedAtWorldTime = 0.0;

		/** The client's credits on the server when it ordered a second headquarters, and when the server took the order. */
		int32 ClientHeadquartersCredits = 0;
		double HeadquartersOrderedAtWorldTime = 0.0;
	};

	UWorld* FindPIEWorld(ENetMode NetMode)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::PIE && World && World->GetNetMode() == NetMode)
			{
				return World;
			}
		}
		return nullptr;
	}

	bool IsNavMeshReady(UWorld& World)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		const ARecastNavMesh* NavMesh = NavSystem ? Cast<ARecastNavMesh>(NavSystem->GetDefaultNavDataInstance()) : nullptr;
		return NavMesh && NavMesh->GetNavMeshTilesCount() > 0 && !NavSystem->IsNavigationBuildInProgress();
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

	/** A building the test placed, not the headquarters the team started with; false while its data has not replicated. */
	bool IsPlacedBuilding(const ARTSBuilding& Building)
	{
		const URTSBuildingData* Headquarters = LoadObject<URTSBuildingData>(nullptr, HeadquartersPath);
		return Building.GetBuildingData() && Building.GetBuildingData() != Headquarters;
	}

	ARTSPlayerController* GetLocalController(UWorld& World)
	{
		return Cast<ARTSPlayerController>(World.GetFirstPlayerController());
	}

	/** Server only. A unit of TeamId standing on the ground at Location. */
	ARTSUnit* SpawnUnit(UWorld& World, const TCHAR* DataPath, int32 TeamId, const FVector& Location)
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, DataPath);
		if (!Data)
		{
			return nullptr;
		}
		FTransform Transform(Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(
			ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	/** The world's copy of a unit of TeamId within a few metres of Location, or null. */
	ARTSUnit* FindUnitNear(UWorld& World, int32 TeamId, const FVector& Location)
	{
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			if (It->GetTeamId() == TeamId && FVector::Dist2D(It->GetActorLocation(), Location) < 200.f)
			{
				return *It;
			}
		}
		return nullptr;
	}

	void CollectUnits(UWorld& World, TArray<TWeakObjectPtr<ARTSUnit>> (&OutUnits)[2], TArray<FVector>& OutHostStarts)
	{
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			if (It->GetTeamId() == HostTeam || It->GetTeamId() == ClientTeam)
			{
				OutUnits[It->GetTeamId()].Add(*It);
			}
			if (It->GetTeamId() == HostTeam)
			{
				OutHostStarts.Add(It->GetActorLocation());
			}
		}
	}

	int32 CountArrived(const TArray<TWeakObjectPtr<ARTSUnit>>& Units)
	{
		int32 Arrived = 0;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
		{
			if (Unit.IsValid() && FVector::Dist2D(Unit->GetActorLocation(), MoveTarget) <= ArrivalRadius)
			{
				++Arrived;
			}
		}
		return Arrived;
	}

	int32 CountUnmoved(const TArray<TWeakObjectPtr<ARTSUnit>>& Units, const TArray<FVector>& Starts)
	{
		int32 Unmoved = 0;
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			if (Units[Index].IsValid() && FVector::Dist2D(Units[Index]->GetActorLocation(), Starts[Index]) < UnmovedTolerance)
			{
				++Unmoved;
			}
		}
		return Unmoved;
	}

	void CheckStart(FAutomationTestBase& Test, const ARTSPlayerController& Controller, const TCHAR* Who, int32 ExpectedTeam)
	{
		Test.TestEqual(FString::Printf(TEXT("%s is on team %d"), Who, ExpectedTeam), Controller.GetTeamId(), ExpectedTeam);

		const APlayerStart* Start = Cast<APlayerStart>(Controller.StartSpot.Get());
		const FName ExpectedTag(*FString::Printf(TEXT("Team%d"), ExpectedTeam));
		Test.TestTrue(FString::Printf(TEXT("%s started at the PlayerStart tagged %s"), Who, *ExpectedTag.ToString()),
			Start && Start->PlayerStartTag == ExpectedTag);

		const APawn* Pawn = Controller.GetPawn();
		Test.TestTrue(FString::Printf(TEXT("%s's camera stands on its start"), Who),
			Start && Pawn && FVector::Dist2D(Pawn->GetActorLocation(), Start->GetActorLocation()) < 1.f);
	}

	DEFINE_LATENT_AUTOMATION_COMMAND(FRTSStartListenServerPIE);
	bool FRTSStartListenServerPIE::Update()
	{
		// The host plays on the listen server and counts as one of the two players.
		ULevelEditorPlaySettings* PlaySettings = NewObject<ULevelEditorPlaySettings>();
		PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_ListenServer);
		PlaySettings->SetPlayNumberOfClients(2);
		PlaySettings->bLaunchSeparateServer = false;
		PlaySettings->SetRunUnderOneProcess(true);

		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		Params.EditorPlaySettings = PlaySettings;
		GEditor->RequestPlaySession(Params);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForListenSession, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSWaitForListenSession::Update()
	{
		if (State->PhaseStart == 0.0)
		{
			State->PhaseStart = FPlatformTime::Seconds();
		}

		UWorld* ServerWorld = FindPIEWorld(NM_ListenServer);
		UWorld* ClientWorld = FindPIEWorld(NM_Client);
		int32 ServerControllers = 0;
		if (ServerWorld)
		{
			for (TActorIterator<ARTSPlayerController> It(ServerWorld); It; ++It)
			{
				ServerControllers += It->GetTeamId() != INDEX_NONE ? 1 : 0;
			}
		}
		const ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		// The controller holds its own player state before the client's GameState lists every player, the host's included.
		const ARTSPlayerState* ClientOwnState = ClientWorld ? ARTSPlayerState::FindForTeam(*ClientWorld, ClientTeam) : nullptr;
		const ARTSPlayerState* ClientHostState = ClientWorld ? ARTSPlayerState::FindForTeam(*ClientWorld, HostTeam) : nullptr;
		const bool bClientPlayersReady = ClientController && ClientHostState
			&& ClientOwnState && ClientOwnState == ClientController->GetPlayerState<ARTSPlayerState>()
			&& ClientController->GetFaction() && ClientController->GetCredits() > 0;

		const bool bReady = ServerWorld && ClientWorld
			&& ServerControllers == 2
			&& IsNavMeshReady(*ServerWorld)
			&& RTSTests::IsNavigationReady(*ServerWorld) && RTSTests::IsNavigationReady(*ClientWorld)
			&& ClientController && ClientController->GetTeamId() != INDEX_NONE
			&& bClientPlayersReady
			&& CountUnits(*ClientWorld) == 2 * UnitsPerTeam;
		if (bReady)
		{
			State->ServerWorld = ServerWorld;
			State->ClientWorld = ClientWorld;
			Test->AddInfo(FString::Printf(TEXT("Listen session ready after %.1f s"), FPlatformTime::Seconds() - State->PhaseStart));
			// The client takes the server's dilation through replication; setting its copy too keeps both in step at once.
			RTSTests::ApplyTestTimeDilation(*ServerWorld);
			RTSTests::ApplyTestTimeDilation(*ClientWorld);
			return true;
		}
		if (FPlatformTime::Seconds() - State->PhaseStart > SessionTimeout)
		{
			Test->AddError(FString::Printf(TEXT("Listen session not ready: server world %d, client world %d, server players %d, client team %d, client units %d, ")
				TEXT("client sees host state %d, own state %d, faction %d, credits %d"),
				ServerWorld != nullptr, ClientWorld != nullptr, ServerControllers,
				ClientController ? ClientController->GetTeamId() : INDEX_NONE, ClientWorld ? CountUnits(*ClientWorld) : 0,
				ClientHostState != nullptr, ClientOwnState != nullptr, ClientController && ClientController->GetFaction(),
				ClientController ? ClientController->GetCredits() : 0));
			return true;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSCheckTeamsAndStarts, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSCheckTeamsAndStarts::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		if (!ServerWorld || !ClientWorld)
		{
			return true;
		}

		for (TActorIterator<ARTSPlayerController> It(ServerWorld); It; ++It)
		{
			const bool bHost = It->IsLocalController();
			CheckStart(*Test, **It, bHost ? TEXT("Host") : TEXT("Client"), bHost ? HostTeam : ClientTeam);
		}

		const ARTSPlayerController* ClientController = GetLocalController(*ClientWorld);
		Test->TestEqual(TEXT("The client sees itself on team 1"), ClientController ? ClientController->GetTeamId() : INDEX_NONE, ClientTeam);

		// Credits replicate to their owner only.
		const ARTSPlayerState* ServerClientState = ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam);
		const ARTSPlayerState* ClientHostState = ARTSPlayerState::FindForTeam(*ClientWorld, HostTeam);
		Test->TestTrue(TEXT("The client starts with credits"), ServerClientState && ServerClientState->GetCredits() > 0);
		Test->TestEqual(TEXT("The client sees its own credits"),
			ClientController ? ClientController->GetCredits() : INDEX_NONE, ServerClientState ? ServerClientState->GetCredits() : 0);
		Test->TestNotNull(TEXT("The client sees the host's player state"), ClientHostState);
		Test->TestTrue(TEXT("The client does not see the host's credits"), ClientHostState && ClientHostState->GetCredits() == 0);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueClientMove, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSIssueClientMove::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		if (!ServerWorld || !ClientController)
		{
			Test->AddError(TEXT("Lost the server world or the client's controller"));
			return true;
		}

		CollectUnits(*ServerWorld, State->ServerUnits, State->ServerHostStarts);
		CollectUnits(*ClientWorld, State->ClientUnits, State->ClientHostStarts);
		for (int32 Team : { HostTeam, ClientTeam })
		{
			Test->TestEqual(FString::Printf(TEXT("Server has team %d's units"), Team), State->ServerUnits[Team].Num(), UnitsPerTeam);
			Test->TestEqual(FString::Printf(TEXT("Client sees team %d's units"), Team), State->ClientUnits[Team].Num(), UnitsPerTeam);
		}

		// A client may name any unit it can see; the server has to drop the host's.
		TArray<ARTSUnit*> Everyone;
		for (TActorIterator<ARTSUnit> It(ClientWorld); It; ++It)
		{
			Everyone.Add(*It);
		}
		FRTSCommand Command;
		Command.Type = ERTSCommandType::Move;
		Command.TargetLocation = MoveTarget;
		ClientController->Server_IssueCommand(Everyone, Command);

		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientMove, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientMove::Update()
	{
		const int32 ServerArrived = CountArrived(State->ServerUnits[ClientTeam]);
		const int32 ClientArrived = CountArrived(State->ClientUnits[ClientTeam]);
		const bool bAllArrived = ServerArrived == UnitsPerTeam && ClientArrived == UnitsPerTeam;
		const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
		if (!bAllArrived && Elapsed < MoveTimeout)
		{
			return false;
		}

		Test->AddInfo(FString::Printf(TEXT("Client's units arrived after %.1f s"), Elapsed));
		Test->TestEqual(TEXT("Server moved the client's units to the target"), ServerArrived, UnitsPerTeam);
		Test->TestEqual(TEXT("Client sees its units at the target"), ClientArrived, UnitsPerTeam);
		Test->TestEqual(TEXT("Server ignored the client's order for the host's units"),
			CountUnmoved(State->ServerUnits[HostTeam], State->ServerHostStarts), UnitsPerTeam);
		Test->TestEqual(TEXT("Client sees the host's units where they were"),
			CountUnmoved(State->ClientUnits[HostTeam], State->ClientHostStarts), UnitsPerTeam);
		return true;
	}

	/**
	 * A team may own one headquarters: while the client's stands, its own controller does not start the ghost, and the
	 * order its builder sends anyway is refused by the server.
	 */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSClientOrdersSecondHeadquarters, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSClientOrdersSecondHeadquarters::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		const ARTSPlayerState* ServerClientState = ServerWorld ? ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam) : nullptr;
		URTSBuildingData* Headquarters = LoadObject<URTSBuildingData>(nullptr, HeadquartersPath);
		if (!ClientController || !ServerClientState || !Headquarters)
		{
			Test->AddError(TEXT("Lost the client's controller or its player state, or missing the headquarters data"));
			return true;
		}
		ARTSUnit* ClientBuilder = nullptr;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : State->ClientUnits[ClientTeam])
		{
			if (!ClientBuilder && Unit.IsValid() && Cast<URTSBuilderData>(Unit->GetUnitData()))
			{
				ClientBuilder = Unit.Get();
			}
		}
		if (!ClientBuilder)
		{
			Test->AddError(TEXT("The client has no builder"));
			return true;
		}
		Test->TestTrue(TEXT("The client's builder can build a headquarters"),
			CastChecked<URTSBuilderData>(ClientBuilder->GetUnitData())->Buildings.Contains(Headquarters));
		Test->TestTrue(TEXT("The client sees itself at the headquarters limit"), ClientController->IsAtBuildLimit(*Headquarters));
		ClientController->BeginPlacement(Headquarters);
		Test->TestNull(TEXT("The client's ghost for a second headquarters does not start"), ClientController->GetPlacementData());

		State->ClientHeadquartersCredits = ServerClientState->GetCredits();
		State->HeadquartersOrderedAtWorldTime = ServerWorld->GetTimeSeconds();
		FRTSCommand Build;
		Build.Type = ERTSCommandType::Build;
		Build.EntityData = Headquarters;
		Build.TargetLocation = ClientSiteSpot;
		ClientController->Server_IssueCommand({ ClientBuilder }, Build);
		return true;
	}

	/** An accepted order would be charged at once and leave the builder walking to its site. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifySecondHeadquartersRefused, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifySecondHeadquartersRefused::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		const ARTSPlayerState* ServerClientState = ServerWorld ? ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam) : nullptr;
		if (!ServerClientState)
		{
			Test->AddError(TEXT("Lost the server world or the client's player state"));
			return true;
		}
		if (ServerWorld->GetTimeSeconds() - State->HeadquartersOrderedAtWorldTime < RefusedOrderSeconds)
		{
			return false;
		}
		Test->TestEqual(TEXT("The server charged nothing for a second headquarters"), ServerClientState->GetCredits(),
			State->ClientHeadquartersCredits);
		for (const TWeakObjectPtr<ARTSUnit>& Unit : State->ServerUnits[ClientTeam])
		{
			if (Unit.IsValid() && Cast<URTSBuilderData>(Unit->GetUnitData()))
			{
				Test->TestTrue(TEXT("No client builder took the order"), Unit->GetBuilderComponent()->GetState() == ERTSBuildState::Idle);
			}
		}
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSClientBuysSite, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSClientBuysSite::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		const ARTSPlayerState* ServerClientState = ServerWorld ? ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam) : nullptr;
		const ARTSPlayerState* HostState = ServerWorld ? ARTSPlayerState::FindForTeam(*ServerWorld, HostTeam) : nullptr;
		if (!ClientController || !ServerClientState || !HostState)
		{
			Test->AddError(TEXT("Lost the client's controller or a player state"));
			return true;
		}
		State->ClientCreditsBefore = ServerClientState->GetCredits();
		State->HostCredits = HostState->GetCredits();

		// The client's own copy of one of its builders, as a selection would hold it.
		for (const TWeakObjectPtr<ARTSUnit>& Unit : State->ClientUnits[ClientTeam])
		{
			if (Unit.IsValid() && Cast<URTSBuilderData>(Unit->GetUnitData()))
			{
				State->ClientBuilder = Unit;
				break;
			}
		}
		if (!State->ClientBuilder.IsValid())
		{
			Test->AddError(TEXT("The client has no builder"));
			return true;
		}

		FRTSCommand Build;
		Build.Type = ERTSCommandType::Build;
		Build.EntityData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		Build.TargetLocation = ClientSiteSpot;
		ClientController->Server_IssueCommand({ State->ClientBuilder.Get() }, Build);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The Build order is paid when the server takes it: the client sees its credits drop while the builder still walks. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientCharged, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientCharged::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		const ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		const URTSBuildingData* PowerPlant = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		if (!ServerWorld || !ClientController || !PowerPlant)
		{
			Test->AddError(TEXT("Lost a world, the client's controller or the power plant data"));
			return true;
		}

		const int32 Expected = State->ClientCreditsBefore - PowerPlant->Cost;
		if (ClientController->GetCredits() != Expected)
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > ReplicationTimeout)
			{
				Test->AddError(FString::Printf(TEXT("The client never saw the order charged: %d credits, expected %d"),
					ClientController->GetCredits(), Expected));
				return true;
			}
			return false;
		}

		auto HasClientSite = [](UWorld& World)
		{
			for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
			{
				if (It->GetTeamId() == ClientTeam && IsPlacedBuilding(**It))
				{
					return true;
				}
			}
			return false;
		};
		Test->TestFalse(TEXT("The order was charged before the server placed the site"), HasClientSite(*ServerWorld));
		Test->TestFalse(TEXT("The client saw the charge before the site"), HasClientSite(*ClientWorld));
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForClientSite, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSWaitForClientSite::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		if (!ServerWorld || !ClientWorld)
		{
			return true;
		}

		if (!State->ServerSite.IsValid())
		{
			for (TActorIterator<ARTSBuilding> It(ServerWorld); It; ++It)
			{
				if (It->GetTeamId() == ClientTeam && IsPlacedBuilding(**It))
				{
					State->ServerSite = *It;
				}
			}
		}
		if (!State->ClientSite.IsValid())
		{
			for (TActorIterator<ARTSBuilding> It(ClientWorld); It; ++It)
			{
				if (It->GetTeamId() == ClientTeam && IsPlacedBuilding(**It))
				{
					State->ClientSite = *It;
				}
			}
		}

		const ARTSBuilding* ServerSite = State->ServerSite.Get();
		const ARTSBuilding* ClientSite = State->ClientSite.Get();
		ARTSPlayerController* ClientController = GetLocalController(*ClientWorld);
		const ARTSPlayerState* ServerClientState = ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam);

		// Once the builder has placed the site, the client stops it so the refund holds until the cancel arrives.
		if (ServerSite && ClientController && State->ClientBuilder.IsValid() && !State->bClientBuilderStopped)
		{
			FRTSCommand Stop;
			Stop.Type = ERTSCommandType::Stop;
			ClientController->Server_IssueCommand({ State->ClientBuilder.Get() }, Stop);
			State->bClientBuilderStopped = true;
		}
		bool bBuildersIdle = true;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : State->ServerUnits[ClientTeam])
		{
			bBuildersIdle &= !Unit.IsValid() || Unit->GetBuilderComponent()->GetState() == ERTSBuildState::Idle;
		}

		const bool bSynced = ServerSite && ClientSite && ClientController && ServerClientState && bBuildersIdle
			&& ClientController->GetCredits() == ServerClientState->GetCredits()
			&& ClientSite->GetCancelRefund() == ServerSite->GetCancelRefund();
		if (!bSynced)
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > ClientSiteTimeout)
			{
				Test->AddError(FString::Printf(TEXT("The client's site never synced: server site %d, client site %d, builders idle %d"),
					ServerSite != nullptr, ClientSite != nullptr, bBuildersIdle));
				return true;
			}
			return false;
		}

		const int32 Cost = ServerSite->GetBuildingData()->Cost;
		Test->TestEqual(TEXT("The purchase charged the client"), ServerClientState->GetCredits(), State->ClientCreditsBefore - Cost);
		Test->TestEqual(TEXT("The client's copy knows what it paid"), ClientSite->GetPaidCost(), Cost);
		Test->TestFalse(TEXT("The site is still under construction"), ServerSite->IsConstructed());
		Test->TestFalse(TEXT("The builder's site does not build itself"), ServerSite->bAutoConstruct);
		Test->TestTrue(TEXT("Cancelling would refund something"), ServerSite->GetCancelRefund() > 0);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueClientCancel, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSIssueClientCancel::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		const ARTSBuilding* ServerSite = State->ServerSite.Get();
		if (!ClientController || !ServerSite || !State->ClientSite.IsValid())
		{
			Test->AddError(TEXT("Lost the client's controller or the construction site"));
			return true;
		}

		State->ExpectedClientCredits = ClientController->GetCredits() + ServerSite->GetCancelRefund();
		// What Delete or the HUD button sends: the client names its own copy of the site.
		Test->TestTrue(TEXT("The client may cancel its own site"), ClientController->SelectBuilding(State->ClientSite.Get())
			&& ClientController->RequestCancelConstruction(ClientController->GetSelectedBuilding()));
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientRefund, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientRefund::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		const ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		if (!ServerWorld || !ClientController)
		{
			Test->AddError(TEXT("Lost the server world or the client's controller"));
			return true;
		}

		const bool bDone = !State->ServerSite.IsValid() && !State->ClientSite.IsValid()
			&& ClientController->GetCredits() == State->ExpectedClientCredits;
		if (!bDone && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}

		const ARTSPlayerState* ServerClientState = ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam);
		const ARTSPlayerState* HostState = ARTSPlayerState::FindForTeam(*ServerWorld, HostTeam);
		Test->TestFalse(TEXT("The server removed the cancelled site"), State->ServerSite.IsValid());
		Test->TestFalse(TEXT("The site disappeared on the client"), State->ClientSite.IsValid());
		Test->TestEqual(TEXT("The server refunded the client"),
			ServerClientState ? ServerClientState->GetCredits() : INDEX_NONE, State->ExpectedClientCredits);
		Test->TestEqual(TEXT("The client sees its refund"), ClientController->GetCredits(), State->ExpectedClientCredits);
		Test->TestEqual(TEXT("The host's credits are untouched"), HostState ? HostState->GetCredits() : INDEX_NONE, State->HostCredits);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSGiveClientBarracks, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSGiveClientBarracks::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		const ARTSPlayerState* ServerClientState = ServerWorld ? ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam) : nullptr;
		if (!ServerClientState)
		{
			Test->AddError(TEXT("Lost the server world or the client's player state"));
			return true;
		}

		FRTSCommand Build;
		Build.Type = ERTSCommandType::Build;
		Build.EntityData = LoadObject<URTSBuildingData>(nullptr, BarracksPath);
		Build.TargetLocation = ClientBarracksSpot;
		ARTSBuilding* Barracks = RTSCommands::PlaceBuilding(*ServerWorld, ServerClientState->GetFaction(), ClientTeam, Build);
		if (!Barracks)
		{
			Test->AddError(TEXT("The server could not place the client's barracks"));
			return true;
		}
		Barracks->UpdateConstruction(Barracks->GetBuildingData()->BuildTime);
		State->ServerBarracks = Barracks;
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSClientQueuesInfantry, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSClientQueuesInfantry::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
		if (!ClientController || !Infantry || !State->ServerBarracks.IsValid())
		{
			Test->AddError(TEXT("Lost the client's controller or the barracks, or the test infantry is missing"));
			return true;
		}

		// Wait until the client's copy of the barracks is complete, as the production menu needs.
		for (TActorIterator<ARTSBuilding> It(ClientWorld); It; ++It)
		{
			if (It->GetTeamId() == ClientTeam && IsPlacedBuilding(**It) && It->GetProductionComponent()->CanProduce())
			{
				State->ClientBarracks = *It;
			}
		}
		ARTSBuilding* Barracks = State->ClientBarracks.Get();
		if (!Barracks)
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > ReplicationTimeout)
			{
				Test->AddError(TEXT("The client never saw its completed barracks"));
				return true;
			}
			return false;
		}

		// What selecting the barracks, a right click and pressing 1 twice send.
		Test->TestTrue(TEXT("The client selects its barracks"), ClientController->SelectBuilding(Barracks));
		Test->TestTrue(TEXT("It is the client's production building"), ClientController->GetProductionBuilding() == Barracks);
		Test->TestTrue(TEXT("The client sets the rally point"), ClientController->RequestRallyPoint(Barracks, ClientRallySpot));
		State->ClientProductionCredits = ClientController->GetCredits();
		Test->TestTrue(TEXT("The client queues infantry"), ClientController->RequestProduce(Barracks, Infantry));
		Test->TestTrue(TEXT("And a second"), ClientController->RequestProduce(Barracks, Infantry));
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSClientCancelsInfantry, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSClientCancelsInfantry::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		ARTSBuilding* Barracks = State->ClientBarracks.Get();
		URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
		if (!ClientController || !Barracks || !Infantry)
		{
			Test->AddError(TEXT("Lost the client's controller or its copy of the barracks"));
			return true;
		}

		const URTSProductionComponent* Production = Barracks->GetProductionComponent();
		const int32 Charged = State->ClientProductionCredits - 2 * Infantry->Cost;
		const bool bSynced = Production->GetQueue().Num() == 2 && ClientController->GetCredits() == Charged
			&& Production->GetRallyPoint().Equals(ClientRallySpot);
		if (!bSynced && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestEqual(TEXT("The client sees both infantry queued"), Production->GetQueue().Num(), 2);
		Test->TestEqual(TEXT("The client sees both charged"), ClientController->GetCredits(), Charged);
		Test->TestTrue(TEXT("The client sees its rally point"), Production->GetRallyPoint().Equals(ClientRallySpot));

		// What clicking the second queue entry sends.
		Test->TestTrue(TEXT("The client cancels one"), ClientController->RequestCancelProduction(Barracks, Infantry));
		State->ExpectedClientCredits = Charged + Infantry->Cost;
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientProduction, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientProduction::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		const ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		const ARTSBuilding* ServerBarracks = State->ServerBarracks.Get();
		if (!ServerWorld || !ClientController || !ServerBarracks)
		{
			Test->AddError(TEXT("Lost a world, the client's controller or the barracks"));
			return true;
		}

		// New units of the client, in each world: only the barracks adds any.
		int32 ServerProduced = 0;
		for (TActorIterator<ARTSUnit> It(ServerWorld); It; ++It)
		{
			ServerProduced += It->GetTeamId() == ClientTeam && !State->ServerUnits[ClientTeam].Contains(*It) ? 1 : 0;
		}
		const ARTSUnit* ClientInfantry = nullptr;
		for (TActorIterator<ARTSUnit> It(ClientWorld); It; ++It)
		{
			if (It->GetTeamId() == ClientTeam && !State->ClientUnits[ClientTeam].Contains(*It))
			{
				ClientInfantry = *It;
			}
		}
		const float Distance = ClientInfantry ? FVector::Dist2D(ClientInfantry->GetActorLocation(), ClientRallySpot) : -1.f;
		const bool bDone = ClientInfantry && Distance <= RallyArrivalRadius && ServerBarracks->GetProductionComponent()->GetQueue().IsEmpty();
		const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
		if (!bDone && Elapsed < ProductionTimeout)
		{
			return false;
		}

		Test->AddInfo(FString::Printf(TEXT("The client's infantry reached its rally point after %.1f s"), Elapsed));
		Test->TestEqual(TEXT("The client sees the cancelled infantry refunded"), ClientController->GetCredits(), State->ExpectedClientCredits);
		Test->TestEqual(TEXT("The server produced one infantry for the client"), ServerProduced, 1);
		Test->TestTrue(TEXT("The server's queue is empty"), ServerBarracks->GetProductionComponent()->GetQueue().IsEmpty());
		Test->TestNotNull(TEXT("The client sees its new infantry"), ClientInfantry);
		Test->TestTrue(FString::Printf(TEXT("The client sees it at the rally point (%.0f cm away)"), Distance),
			ClientInfantry && Distance <= RallyArrivalRadius);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSClientResearches, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSClientResearches::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		ARTSBuilding* Barracks = State->ClientBarracks.Get();
		URTSUnitData* Grenadier = LoadObject<URTSUnitData>(nullptr, GrenadierPath);
		URTSUpgradeData* RifleTraining = LoadObject<URTSUpgradeData>(nullptr, RifleTrainingPath);
		if (!ClientController || !Barracks || !Grenadier || !RifleTraining)
		{
			Test->AddError(TEXT("Lost the client's controller or its copy of the barracks, or the test data is missing"));
			return true;
		}

		// What pressing the grenadier's and then the upgrade's number with the barracks selected sends.
		Test->TestTrue(TEXT("The client's grenadier needs rifle training"), ClientController->GetMissingPrerequisite(*Grenadier) == RifleTraining);
		Test->TestFalse(TEXT("So the client's menu refuses the grenadier"), ClientController->RequestProduce(Barracks, Grenadier));
		State->ClientResearchCredits = ClientController->GetCredits();
		Test->TestTrue(TEXT("The client researches rifle training"), ClientController->RequestProduce(Barracks, RifleTraining));
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientResearchQueued, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientResearchQueued::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		ARTSBuilding* Barracks = State->ClientBarracks.Get();
		URTSUpgradeData* RifleTraining = LoadObject<URTSUpgradeData>(nullptr, RifleTrainingPath);
		if (!ClientController || !Barracks || !RifleTraining)
		{
			Test->AddError(TEXT("Lost the client's controller or its copy of the barracks"));
			return true;
		}

		const int32 Charged = State->ClientResearchCredits - RifleTraining->Cost;
		const bool bQueued = Barracks->GetProductionComponent()->GetQueue().Contains(RifleTraining);
		if ((!bQueued || ClientController->GetCredits() != Charged) && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestTrue(TEXT("The client sees the upgrade queued"), bQueued);
		Test->TestEqual(TEXT("The client sees the research charged"), ClientController->GetCredits(), Charged);
		Test->TestFalse(TEXT("The client's menu refuses it a second time while queued"), ClientController->RequestProduce(Barracks, RifleTraining));
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientResearched, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientResearched::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		const ARTSPlayerController* HostController = ServerWorld ? GetLocalController(*ServerWorld) : nullptr;
		ARTSBuilding* Barracks = State->ClientBarracks.Get();
		URTSUnitData* Grenadier = LoadObject<URTSUnitData>(nullptr, GrenadierPath);
		URTSUpgradeData* RifleTraining = LoadObject<URTSUpgradeData>(nullptr, RifleTrainingPath);
		if (!ClientController || !HostController || !Barracks || !Grenadier || !RifleTraining)
		{
			Test->AddError(TEXT("Lost a controller or the client's copy of the barracks"));
			return true;
		}

		const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
		if (!ClientController->HasUpgrade(*RifleTraining) && Elapsed < ResearchTimeout)
		{
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("The client saw rifle training researched after %.1f s"), Elapsed));
		const ARTSPlayerState* ServerClientState = ARTSPlayerState::FindForTeam(*ServerWorld, ClientTeam);
		const ARTSPlayerState* ServerHostState = ARTSPlayerState::FindForTeam(*ServerWorld, HostTeam);
		const ARTSPlayerState* ClientHostState = ARTSPlayerState::FindForTeam(*ClientWorld, HostTeam);
		Test->TestTrue(TEXT("The server records the client's upgrade"), ServerClientState && ServerClientState->HasUpgrade(RifleTraining));
		if (!Test->TestTrue(TEXT("The client sees its upgrade"), ClientController->HasUpgrade(*RifleTraining)))
		{
			return true;
		}
		// Each player researches for itself: the host's own controller and player state know nothing of it.
		Test->TestFalse(TEXT("The host's controller does not have the client's upgrade"), HostController->HasUpgrade(*RifleTraining));
		Test->TestTrue(TEXT("The host's player state lists no upgrades"), ServerHostState && ServerHostState->GetCompletedUpgrades().IsEmpty());
		Test->TestTrue(TEXT("The client's copy of the host's state lists none"), ClientHostState && ClientHostState->GetCompletedUpgrades().IsEmpty());

		Test->TestFalse(TEXT("The client's menu refuses it once researched"), ClientController->RequestProduce(Barracks, RifleTraining));
		Test->TestNull(TEXT("The client's grenadier has its prerequisite"), ClientController->GetMissingPrerequisite(*Grenadier));
		State->ClientResearchCredits = ClientController->GetCredits();
		Test->TestTrue(TEXT("The client queues the grenadier"), ClientController->RequestProduce(Barracks, Grenadier));
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientGrenadierQueued, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientGrenadierQueued::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		const ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		const ARTSBuilding* ServerBarracks = State->ServerBarracks.Get();
		const URTSUnitData* Grenadier = LoadObject<URTSUnitData>(nullptr, GrenadierPath);
		if (!ClientController || !ServerBarracks || !Grenadier)
		{
			Test->AddError(TEXT("Lost the client's controller or the barracks"));
			return true;
		}

		const int32 Charged = State->ClientResearchCredits - Grenadier->Cost;
		const bool bQueued = ServerBarracks->GetProductionComponent()->GetQueue().Contains(Grenadier);
		if ((!bQueued || ClientController->GetCredits() != Charged) && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestTrue(TEXT("The server accepted the client's grenadier"), bQueued);
		Test->TestEqual(TEXT("The client sees the grenadier charged"), ClientController->GetCredits(), Charged);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSPlaceHostBuilding, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSPlaceHostBuilding::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		const ARTSPlayerController* HostController = ServerWorld ? GetLocalController(*ServerWorld) : nullptr;
		if (!HostController)
		{
			Test->AddError(TEXT("Lost the server world or the host's controller"));
			return true;
		}

		FRTSCommand Build;
		Build.Type = ERTSCommandType::Build;
		Build.EntityData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		Build.TargetLocation = HostBuildingSpot;
		State->ServerBuilding = RTSCommands::PlaceBuilding(*ServerWorld, HostController->GetFaction(), HostTeam, Build);
		if (!State->ServerBuilding.IsValid())
		{
			Test->AddError(TEXT("The server could not place the host building"));
		}
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForClientBuilding, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSWaitForClientBuilding::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		if (!ClientWorld || !State->ServerBuilding.IsValid())
		{
			return true;
		}

		for (TActorIterator<ARTSBuilding> It(ClientWorld); It; ++It)
		{
			// Wait for the health component's first update too, not just the actor.
			const URTSHealthComponent* Health = It->GetHealthComponent();
			if (It->GetTeamId() == HostTeam && IsPlacedBuilding(**It) && Health->GetMaxHealth() > 1.f)
			{
				State->ClientBuilding = *It;
				State->ClientMaxHealth = Health->GetMaxHealth();
				State->ClientLowestHealth = Health->GetHealth();
				Test->TestEqual(TEXT("The client sees the building at full health"), Health->GetHealth(), Health->GetMaxHealth());
				Test->TestEqual(TEXT("The client sees the server's maximum health"),
					Health->GetMaxHealth(), State->ServerBuilding->GetHealthComponent()->GetMaxHealth());
				return true;
			}
		}
		if (FPlatformTime::Seconds() - State->PhaseStart > ReplicationTimeout)
		{
			Test->AddError(TEXT("The host building never replicated to the client"));
			return true;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueClientBuildingAttack, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSIssueClientBuildingAttack::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		if (!ClientController || !State->ClientBuilding.IsValid())
		{
			Test->AddError(TEXT("Lost the client's controller or its copy of the building"));
			return true;
		}

		TArray<ARTSUnit*> OwnUnits;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : State->ClientUnits[ClientTeam])
		{
			if (Unit.IsValid())
			{
				OwnUnits.Add(Unit.Get());
			}
		}
		// What a right-click on the building sends: the client names its own copy, the server resolves it to the real one.
		FRTSCommand Command;
		Command.Type = ERTSCommandType::Attack;
		Command.TargetActor = State->ClientBuilding.Get();
		ClientController->Server_IssueCommand(OwnUnits, Command);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientBuildingHealth, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientBuildingHealth::Update()
	{
		if (const ARTSBuilding* Building = State->ClientBuilding.Get())
		{
			State->ClientLowestHealth = FMath::Min(State->ClientLowestHealth, Building->GetHealthComponent()->GetHealth());
		}
		const bool bGone = !State->ServerBuilding.IsValid() && !State->ClientBuilding.IsValid();
		const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
		if (!bGone && Elapsed < BuildingAttackTimeout)
		{
			return false;
		}

		Test->AddInfo(FString::Printf(TEXT("Host building destroyed after %.1f s; lowest health the client saw: %.0f of %.0f"),
			Elapsed, State->ClientLowestHealth, State->ClientMaxHealth));
		Test->TestFalse(TEXT("The client's units destroyed the host building on the server"), State->ServerBuilding.IsValid());
		Test->TestFalse(TEXT("The building disappeared on the client"), State->ClientBuilding.IsValid());
		Test->TestTrue(TEXT("The client saw the building's health drop"), State->ClientLowestHealth < State->ClientMaxHealth);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSSpawnClientTank, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSSpawnClientTank::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		if (!ServerWorld)
		{
			Test->AddError(TEXT("Lost the server world"));
			return true;
		}

		State->ServerTank = SpawnUnit(*ServerWorld, TankPath, ClientTeam, ClientTankStart);
		State->ServerInfantry = SpawnUnit(*ServerWorld, InfantryPath, HostTeam, HostInfantrySpot);
		if (!State->ServerTank.IsValid() || !State->ServerInfantry.IsValid())
		{
			Test->AddError(TEXT("The server could not spawn the client's tank or the host infantry"));
		}
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForClientTank, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSWaitForClientTank::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		if (!ClientWorld || !State->ServerTank.IsValid() || !State->ServerInfantry.IsValid())
		{
			return true;
		}

		State->ClientTank = FindUnitNear(*ClientWorld, ClientTeam, ClientTankStart);
		State->ClientInfantry = FindUnitNear(*ClientWorld, HostTeam, HostInfantrySpot);
		if (State->ClientTank.IsValid() && State->ClientInfantry.IsValid())
		{
			return true;
		}
		if (FPlatformTime::Seconds() - State->PhaseStart > ReplicationTimeout)
		{
			Test->AddError(TEXT("The client's tank or the host infantry never replicated to the client"));
			return true;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueClientAttackMove, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSIssueClientAttackMove::Update()
	{
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		if (!ClientController || !State->ClientTank.IsValid())
		{
			Test->AddError(TEXT("Lost the client's controller or its copy of the tank"));
			return true;
		}

		// What J and a left click send: the client names its own copy, the server resolves it to the real one.
		FRTSCommand Command;
		Command.Type = ERTSCommandType::AttackMove;
		Command.TargetLocation = ClientTankTarget;
		ClientController->Server_IssueCommand({ State->ClientTank.Get() }, Command);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientAttackMove, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientAttackMove::Update()
	{
		const ARTSUnit* ClientTank = State->ClientTank.Get();
		const float Distance = ClientTank ? FVector::Dist2D(ClientTank->GetActorLocation(), ClientTankTarget) : -1.f;
		const bool bDone = !State->ServerInfantry.IsValid() && !State->ClientInfantry.IsValid()
			&& ClientTank && Distance <= AttackMoveArrivalRadius;
		const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
		if (!bDone && Elapsed < AttackMoveTimeout)
		{
			return false;
		}

		Test->AddInfo(FString::Printf(TEXT("The client's attack-move finished after %.1f s"), Elapsed));
		Test->TestFalse(TEXT("The client's tank destroyed the host infantry on the server"), State->ServerInfantry.IsValid());
		Test->TestFalse(TEXT("The infantry disappeared on the client"), State->ClientInfantry.IsValid());
		Test->TestTrue(FString::Printf(TEXT("The client sees its tank arrive (%.0f cm away)"), Distance),
			ClientTank && Distance <= AttackMoveArrivalRadius);
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSDestroyHostBase, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSDestroyHostBase::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		const ARTSGameState* GameState = ServerWorld ? ServerWorld->GetGameState<ARTSGameState>() : nullptr;
		if (!GameState)
		{
			Test->AddError(TEXT("Lost the server world or its match game state"));
			return true;
		}
		Test->TestFalse(TEXT("The match runs while the host has its headquarters"), GameState->IsMatchOver());

		// The client's tank takes the credit, as if it had shot them down.
		TArray<ARTSBuilding*> HostBuildings;
		for (TActorIterator<ARTSBuilding> It(ServerWorld); It; ++It)
		{
			if (It->GetTeamId() == HostTeam)
			{
				HostBuildings.Add(*It);
			}
		}
		Test->TestEqual(TEXT("The host has only its headquarters left"), HostBuildings.Num(), 1);
		for (ARTSBuilding* Building : HostBuildings)
		{
			Building->GetHealthComponent()->ApplyDamage(Building->GetHealthComponent()->GetMaxHealth(), State->ServerTank.Get());
		}
		Test->TestTrue(TEXT("The match is over on the server"), GameState->IsMatchOver());
		Test->TestEqual(TEXT("The client's team wins"), GameState->GetWinningTeam(), ClientTeam);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientVictory, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientVictory::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		UWorld* ClientWorld = State->ClientWorld.Get();
		ARTSPlayerController* HostController = ServerWorld ? GetLocalController(*ServerWorld) : nullptr;
		ARTSPlayerController* ClientController = ClientWorld ? GetLocalController(*ClientWorld) : nullptr;
		if (!HostController || !ClientController || !State->ClientTank.IsValid() || !State->ServerTank.IsValid())
		{
			Test->AddError(TEXT("Lost a controller or the client's tank"));
			return true;
		}

		// The match end stops the tank where it is, an unfinished attack-move kept; the refused order is judged from there.
		const bool bTankAtRest = State->ServerTank->GetVelocity().Size2D() < 1.f;
		if (ClientController->GetMatchResult() == ERTSMatchResult::InProgress || !bTankAtRest)
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > ReplicationTimeout)
			{
				Test->AddError(FString::Printf(TEXT("The client never saw the match end (%d) or the tank never came to rest (%d)"),
					ClientController->GetMatchResult() != ERTSMatchResult::InProgress, bTankAtRest));
				return true;
			}
			return false;
		}
		Test->TestTrue(TEXT("The client sees Victory"), ClientController->GetMatchResult() == ERTSMatchResult::Victory);
		Test->TestTrue(TEXT("The host sees Defeat"), HostController->GetMatchResult() == ERTSMatchResult::Defeat);
		const ARTSGameState* ClientGameState = ClientWorld->GetGameState<ARTSGameState>();
		Test->TestTrue(TEXT("The client's game state has only its team left"),
			ClientGameState && ClientGameState->GetAliveTeams() == TArray<int32>({ ClientTeam }));
		Test->TestFalse(TEXT("The client cannot restart"), ClientController->CanRestart());
		Test->TestTrue(TEXT("The host can restart"), HostController->CanRestart());

		// What a right click with the tank selected sends after the match.
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = ClientTankStart;
		ClientController->Server_IssueCommand({ State->ClientTank.Get() }, Move);
		State->RefusedStart = State->ServerTank->GetActorLocation();
		State->RefusedQueue = State->ServerTank->GetQueuedCommandCount();
		State->RefusedAtWorldTime = ServerWorld->GetTimeSeconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientOrderRefused, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientOrderRefused::Update()
	{
		UWorld* ServerWorld = State->ServerWorld.Get();
		const ARTSUnit* ServerTank = State->ServerTank.Get();
		if (!ServerWorld || !ServerTank)
		{
			Test->AddError(TEXT("Lost the server world or the client's tank"));
			return true;
		}
		if (ServerWorld->GetTimeSeconds() - State->RefusedAtWorldTime < RefusedOrderSeconds)
		{
			return false;
		}
		Test->TestEqual(TEXT("The server refused the client's move"), ServerTank->GetQueuedCommandCount(), State->RefusedQueue);
		const float Moved = FVector::Dist2D(ServerTank->GetActorLocation(), State->RefusedStart);
		Test->TestTrue(FString::Printf(TEXT("The client's tank stayed put (moved %.0f cm)"), Moved), Moved < UnmovedTolerance);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSListenServerSmokeTest, "WorldDivide.Smoke.ListenServer", RTSTests::TestFlags)

	bool FRTSListenServerSmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(TestMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
			return false;
		}

		const TSharedRef<FSessionState> State = MakeShared<FSessionState>();
		ADD_LATENT_AUTOMATION_COMMAND(FRTSStartListenServerPIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForListenSession(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSCheckTeamsAndStarts(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueClientMove(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientMove(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientOrdersSecondHeadquarters(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifySecondHeadquartersRefused(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientBuysSite(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientCharged(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForClientSite(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueClientCancel(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientRefund(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSGiveClientBarracks(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientQueuesInfantry(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientCancelsInfantry(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientProduction(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientResearches(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientResearchQueued(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientResearched(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientGrenadierQueued(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceHostBuilding(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForClientBuilding(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueClientBuildingAttack(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientBuildingHealth(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSSpawnClientTank(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForClientTank(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueClientAttackMove(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientAttackMove(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSDestroyHostBase(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientVictory(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientOrderRefused(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}
}

#endif
