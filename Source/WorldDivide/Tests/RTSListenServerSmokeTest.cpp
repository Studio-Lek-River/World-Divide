#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
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
 * own controller: the server must move only the client's units, and the client must see them arrive. The client then
 * sends its builder to buy a construction site, stops the builder and cancels the site: the site must disappear in both
 * worlds and the client must see its refund while the host's credits stay put. The server then gives the client a
 * barracks: the client sets its rally point, queues two infantry, sees them charged and cancels one for a refund, and
 * must see the other come out and walk to the rally point. Then the server places a host building and the client orders its units to attack it: the client's copy must show the building's
 * health dropping before it disappears.
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

	ARTSPlayerController* GetLocalController(UWorld& World)
	{
		return Cast<ARTSPlayerController>(World.GetFirstPlayerController());
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

		const bool bReady = ServerWorld && ClientWorld
			&& ServerControllers == 2
			&& IsNavMeshReady(*ServerWorld)
			&& ClientController && ClientController->GetTeamId() != INDEX_NONE
			&& CountUnits(*ClientWorld) == 2 * UnitsPerTeam;
		if (bReady)
		{
			State->ServerWorld = ServerWorld;
			State->ClientWorld = ClientWorld;
			Test->AddInfo(FString::Printf(TEXT("Listen session ready after %.1f s"), FPlatformTime::Seconds() - State->PhaseStart));
			return true;
		}
		if (FPlatformTime::Seconds() - State->PhaseStart > SessionTimeout)
		{
			Test->AddError(FString::Printf(TEXT("Listen session not ready: server world %d, client world %d, server players %d, client team %d, client units %d"),
				ServerWorld != nullptr, ClientWorld != nullptr, ServerControllers,
				ClientController ? ClientController->GetTeamId() : INDEX_NONE, ClientWorld ? CountUnits(*ClientWorld) : 0));
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
				if (It->GetTeamId() == ClientTeam)
				{
					State->ServerSite = *It;
				}
			}
		}
		if (!State->ClientSite.IsValid())
		{
			for (TActorIterator<ARTSBuilding> It(ClientWorld); It; ++It)
			{
				if (It->GetTeamId() == ClientTeam && It->GetBuildingData())
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
			if (It->GetTeamId() == ClientTeam && It->GetProductionComponent()->CanProduce())
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
			if (It->GetTeamId() == HostTeam && Health->GetMaxHealth() > 1.f)
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
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientBuysSite(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForClientSite(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueClientCancel(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientRefund(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSGiveClientBarracks(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientQueuesInfantry(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientCancelsInfantry(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientProduction(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceHostBuilding(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForClientBuilding(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueClientBuildingAttack(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientBuildingHealth(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}
}

#endif
