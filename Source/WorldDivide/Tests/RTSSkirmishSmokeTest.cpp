#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuilding.h"
#include "Core/RTSGameInstance.h"
#include "Core/RTSGameMode.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/RTSLobbyGameMode.h"
#include "Menu/RTSLobbyGameState.h"
#include "Menu/RTSLobbyPlayerController.h"
#include "Menu/RTSMainMenuController.h"
#include "Menu/SRTSLobbyWidget.h"
#include "PlayInEditorDataTypes.h"
#include "Player/RTSLocalPlayer.h"
#include "Player/RTSPlayerController.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestMaps.h"
#include "Units/RTSUnit.h"

/**
 * The skirmish lobby end to end, as a listen server with one client in the same process, both starting in L_Lobby. Lobby:
 * both players take an open seat and get a token, the host makes the last seat a Hard AI and the client sees it, the client
 * may move its own start spot but not change other seats, and the match URL carries the seats. The host's start is refused
 * until the client is ready, which both see; a move by the host makes the client ready up again. On the lobby screen's map
 * preview the client's click on the host's spot changes nothing and its click on the free spot 2 moves it there; the host
 * selects the last AI's seat and moves it by a click to spot 3. Then the host picks the
 * two-spot duel map: the seats shrink to two, the client, beyond them, takes the AI's seat and must ready up again, its
 * own pick of map is refused, and the match URL names the duel map. Last, the client's removal of the host is refused, the
 * host removes the client, whose seat opens again, and the client lands on the main menu with the reason. SkirmishSeats: the host
 * moves itself to spot 3, makes the AI Hard and closes the last seat, the client picks spot 0 and readies up, and the host
 * starts the match. Both offer the test maps in the lobby (RTSTests::FRTSUseTestLobbyMaps), so it starts on L_Skirmish.
 * In L_Skirmish the host plays team 0 from spot 3, the AI team 1 from spot 1 at Hard, the client team 2 from spot 0 by its
 * token, and neither the closed team 3 nor spot 2, which nobody plays, has a building or a unit. The client's scoreboard
 * shows the host's name and "AI (Hard)". Then the host leaves the match and the client's main menu shows "The host left
 * the match". LobbyLeave: the client leaves the lobby and its main menu shows no message; the host
 * fills every open seat with an AI and the client's join by address is refused, its main menu showing "The lobby is full";
 * the host opens a seat again, the client joins, and once the host leaves the client's main menu shows "The host closed
 * the lobby".
 */
namespace RTSSkirmishSmokeTest
{
	const TCHAR* LobbyMap = TEXT("/Game/Maps/L_Lobby");
	const TCHAR* MainMenuMapName = TEXT("L_MainMenu");
	const TCHAR* SkirmishMapName = TEXT("L_Skirmish");
	/** The data assets of L_Skirmish, where the lobby starts, and of L_SkirmishDuel, with two start spots. */
	const TCHAR* SkirmishMapAsset = TEXT("DA_MapSkirmish");
	const TCHAR* DuelMapAsset = TEXT("DA_MapDuel");
	constexpr int32 HostTeam = 0;
	constexpr int32 AITeam = 1;
	constexpr int32 ClientTeam = 2;
	constexpr int32 ClosedTeam = 3;
	/** The spot of each team above; spot 2 is played by nobody. */
	constexpr int32 TeamSpots[] = { 3, 1, 0 };
	constexpr int32 EmptySpot = 2;

	/** Within this of its start spot a building belongs to that base. */
	constexpr float BaseRadius = 1500.f;
	constexpr double SessionTimeout = 60.0;
	constexpr double ReplicationTimeout = 15.0;
	/** Game seconds for the AI to issue its first order. */
	constexpr float AIOrderGameSeconds = 10.f;

	struct FSessionState
	{
		double PhaseStart = 0.0;
		float PhaseWorldStart = 0.f;

		/** The lobby. */
		TWeakObjectPtr<UWorld> LobbyServer;
		TWeakObjectPtr<UWorld> LobbyClient;
		TWeakObjectPtr<ARTSLobbyPlayerController> Host;
		TWeakObjectPtr<ARTSLobbyPlayerController> Client;
		TWeakObjectPtr<ARTSLobbyPlayerController> ClientOnServer;
		FString HostToken;
		FString ClientToken;

		/** The match the lobby travelled to. */
		TWeakObjectPtr<UWorld> MatchServer;
		TWeakObjectPtr<UWorld> MatchClient;
		TWeakObjectPtr<ARTSAIPlayer> AI;

		/** Leaving the lobby. */
		TWeakObjectPtr<UGameInstance> ClientInstance;
		TWeakObjectPtr<ARTSMainMenuController> ClientMenu;
		FString JoinAddress;
		TArray<int32> FilledSeats;
	};

	/** The PIE world of NetMode, on the map named MapName when one is given. */
	UWorld* FindPIEWorld(ENetMode NetMode, const TCHAR* MapName = nullptr)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::PIE && World && World->GetNetMode() == NetMode
				&& (!MapName || World->GetMapName().EndsWith(MapName)))
			{
				return World;
			}
		}
		return nullptr;
	}

	APlayerStart* FindSpot(UWorld& World, int32 Spot)
	{
		const FName Tag(*FString::Printf(TEXT("Team%d"), Spot));
		for (TActorIterator<APlayerStart> It(&World); It; ++It)
		{
			if (It->PlayerStartTag == Tag)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Actors of TeamId, or of any team when TeamId is INDEX_NONE, within BaseRadius of Spot, or anywhere when Spot is INDEX_NONE. */
	template <typename ActorType>
	int32 Count(UWorld& World, int32 TeamId, int32 Spot = INDEX_NONE)
	{
		const APlayerStart* Start = Spot == INDEX_NONE ? nullptr : FindSpot(World, Spot);
		int32 Result = 0;
		for (TActorIterator<ActorType> It(&World); It; ++It)
		{
			Result += (TeamId == INDEX_NONE || It->GetTeamId() == TeamId)
				&& (!Start || FVector::Dist2D(It->GetActorLocation(), Start->GetActorLocation()) < BaseRadius) ? 1 : 0;
		}
		return Result;
	}

	const FString& GetToken(const APlayerController* Controller)
	{
		static const FString None;
		const URTSLocalPlayer* Player = Controller ? Cast<URTSLocalPlayer>(Controller->GetLocalPlayer()) : nullptr;
		return Player ? Player->GetSeatToken() : None;
	}

	const ARTSLobbyGameState* GetLobbyState(const TWeakObjectPtr<UWorld>& World)
	{
		return World.IsValid() ? World->GetGameState<ARTSLobbyGameState>() : nullptr;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND(FRTSStartLobbyPIE);
	bool FRTSStartLobbyPIE::Update()
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

	/** Waits until the client sees both players seated and both have their tokens. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForLobby, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSWaitForLobby::Update()
	{
		if (State->PhaseStart == 0.0)
		{
			State->PhaseStart = FPlatformTime::Seconds();
		}
		UWorld* ServerWorld = FindPIEWorld(NM_ListenServer);
		UWorld* ClientWorld = FindPIEWorld(NM_Client);
		ARTSLobbyPlayerController* Host = ServerWorld ? Cast<ARTSLobbyPlayerController>(ServerWorld->GetFirstPlayerController()) : nullptr;
		ARTSLobbyPlayerController* Client = ClientWorld ? Cast<ARTSLobbyPlayerController>(ClientWorld->GetFirstPlayerController()) : nullptr;
		const ARTSLobbyGameState* ClientState = ClientWorld ? ClientWorld->GetGameState<ARTSLobbyGameState>() : nullptr;
		int32 SeenHumans = 0;
		for (const FRTSSeat& Seat : ClientState ? ClientState->GetSeats() : TArray<FRTSSeat>())
		{
			SeenHumans += Seat.Type == ERTSSeatType::Human ? 1 : 0;
		}
		if (Host && Client && SeenHumans == 2 && !GetToken(Host).IsEmpty() && !GetToken(Client).IsEmpty())
		{
			State->LobbyServer = ServerWorld;
			State->LobbyClient = ClientWorld;
			State->Host = Host;
			State->Client = Client;
			for (TActorIterator<ARTSLobbyPlayerController> It(ServerWorld); It; ++It)
			{
				if (!It->IsLocalController())
				{
					State->ClientOnServer = *It;
				}
			}
			return true;
		}
		if (FPlatformTime::Seconds() - State->PhaseStart > SessionTimeout)
		{
			Test->AddError(FString::Printf(TEXT("Lobby not ready: host %d, client %d, humans seen by the client %d, host token %d, client token %d"),
				Host != nullptr, Client != nullptr, SeenHumans, !GetToken(Host).IsEmpty(), !GetToken(Client).IsEmpty()));
			return true;
		}
		return false;
	}

	/** Checks the seats both players took, then the host makes seat 3 a Hard AI and the client asks for changes. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSLobbyChanges, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSLobbyChanges::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		ARTSLobbyPlayerController* Host = State->Host.Get();
		ARTSLobbyPlayerController* Client = State->Client.Get();
		if (!ServerState || !Host || !Client || ServerState->GetSeats().Num() != 4)
		{
			Test->AddError(TEXT("Lost the lobby, or it does not have four seats"));
			return true;
		}
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		Test->TestEqual(TEXT("The host takes seat 0"), Seats[0].PlayerId, Host->GetOwnPlayerId());
		Test->TestEqual(TEXT("The AI keeps seat 1"), Seats[1].Type, ERTSSeatType::AI);
		Test->TestEqual(TEXT("The client takes the next open seat"), Seats[2].Type, ERTSSeatType::Human);
		Test->TestEqual(TEXT("With its own token"), Seats[2].PlayerToken, GetToken(Client));
		Test->TestNotEqual(TEXT("The tokens differ"), GetToken(Host), GetToken(Client));
		Test->TestEqual(TEXT("The last seat stays open"), Seats[3].Type, ERTSSeatType::Open);
		Test->TestTrue(TEXT("Only the host hosts"), Host->IsHost() && !Client->IsHost());

		Host->Server_SetSeatType(3, ERTSSeatType::AI, ERTSAIDifficulty::Hard);
		// Refused: a client changes no seat type, and moves no seat but its own.
		Client->Server_SetSeatType(1, ERTSSeatType::Closed, ERTSAIDifficulty::Normal);
		Client->Server_SetStartSpot(0, 3);
		Client->Server_SetStartSpot(2, 2);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyLobbyChanges, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyLobbyChanges::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		const ARTSLobbyGameState* ClientState = GetLobbyState(State->LobbyClient);
		ARTSLobbyGameMode* GameMode = State->LobbyServer.IsValid() ? State->LobbyServer->GetAuthGameMode<ARTSLobbyGameMode>() : nullptr;
		if (!ServerState || !ClientState || !GameMode)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const TArray<FRTSSeat>& Seen = ClientState->GetSeats();
		const bool bSeen = Seen.Num() == 4 && Seen[3].Type == ERTSSeatType::AI && Seen[2].StartSpot == 2;
		if (!bSeen && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestTrue(TEXT("The client sees the host's Hard AI"), Seen.Num() == 4 && Seen[3].Type == ERTSSeatType::AI
			&& Seen[3].Difficulty == ERTSAIDifficulty::Hard);
		Test->TestEqual(TEXT("The client moved its own start spot"), Seen.Num() == 4 ? Seen[2].StartSpot : INDEX_NONE, 2);
		Test->TestTrue(TEXT("The client sees no tokens"), Seen.Num() == 4 && Seen[0].PlayerToken.IsEmpty() && Seen[2].PlayerToken.IsEmpty());

		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		Test->TestEqual(TEXT("The client could not close the AI's seat"), Seats[1].Type, ERTSSeatType::AI);
		Test->TestEqual(TEXT("Nor move the host"), Seats[0].StartSpot, 0);

		const FString Expected = FString::Printf(TEXT("/Game/Maps/L_Skirmish?listen?Seats=Human@0~%s,AI-Normal@1,Human@2~%s,AI-Hard"),
			*GetToken(State->Host.Get()), *GetToken(State->Client.Get()));
		Test->TestEqual(TEXT("The match starts with the lobby's seats"), GameMode->GetMatchURL(), Expected);
		Test->TestTrue(TEXT("The host is ready"), Seats[0].bReady);
		Test->TestFalse(TEXT("The client is not yet"), Seats[2].bReady);
		Test->TestFalse(TEXT("So the match may not start"), RTSSkirmish::CanStart(Seats));
		Test->TestFalse(TEXT("And the host's start is refused"), State->Host.IsValid() && State->Host->RequestStart());

		if (ARTSLobbyPlayerController* Client = State->Client.Get())
		{
			Client->Server_SetReady(true);
		}
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The host and the client see the client ready, then the host moves the client, which must ready up again. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyLobbyReady, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyLobbyReady::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		const ARTSLobbyGameState* ClientState = GetLobbyState(State->LobbyClient);
		ARTSLobbyGameMode* GameMode = State->LobbyServer.IsValid() ? State->LobbyServer->GetAuthGameMode<ARTSLobbyGameMode>() : nullptr;
		ARTSLobbyPlayerController* Host = State->Host.Get();
		if (!ServerState || !ClientState || !GameMode || !Host || ServerState->GetSeats().Num() != 4)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		const TArray<FRTSSeat>& Seen = ClientState->GetSeats();
		const bool bSeen = Seats[2].bReady && Seen.Num() == 4 && Seen[2].bReady;
		if (!bSeen && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestTrue(TEXT("The host sees the client ready"), Seats[2].bReady);
		Test->TestTrue(TEXT("So does the client"), Seen.Num() == 4 && Seen[2].bReady);
		Test->TestTrue(TEXT("The match may start"), RTSSkirmish::CanStart(Seats));
		Test->TestFalse(TEXT("Only the host starts it"), State->ClientOnServer.IsValid() && GameMode->StartMatch(*State->ClientOnServer));

		// The host's call runs at once on the server.
		Host->Server_SetStartSpot(2, 3);
		Test->TestEqual(TEXT("The host moves the client"), Seats[2].StartSpot, 3);
		Test->TestFalse(TEXT("Which must ready up again"), Seats[2].bReady);
		Test->TestFalse(TEXT("Before the match may start"), RTSSkirmish::CanStart(Seats));
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/**
	 * Once the client sees the host's move of it to spot 3, it clicks the host's spot on its lobby screen's preview, which
	 * sends nothing, then the free spot 2.
	 */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSLobbyClickSpot, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSLobbyClickSpot::Update()
	{
		const ARTSLobbyGameState* ClientState = GetLobbyState(State->LobbyClient);
		const TSharedPtr<SRTSLobbyWidget> ClientScreen = State->Client.IsValid() ? State->Client->GetWidget() : nullptr;
		if (!ClientState || !ClientScreen.IsValid())
		{
			Test->AddError(TEXT("Lost the lobby or the client's lobby screen"));
			return true;
		}
		const TArray<FRTSSeat>& Seen = ClientState->GetSeats();
		const bool bSeen = Seen.Num() == 4 && Seen[2].StartSpot == 3;
		if (!bSeen && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestTrue(TEXT("The client sees the host's move of it"), bSeen);
		Test->TestFalse(TEXT("The host's spot is taken"), ClientScreen->CanPickSpot(0));
		Test->TestFalse(TEXT("So a click on it asks nothing"), ClientScreen->ClickSpot(0));
		Test->TestTrue(TEXT("A click on the free spot 2 asks for it"), ClientScreen->ClickSpot(2));
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The server and the client have the client on spot 2; then the host selects the last AI's seat and clicks spot 3. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyLobbyClickedSpot, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyLobbyClickedSpot::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		const ARTSLobbyGameState* ClientState = GetLobbyState(State->LobbyClient);
		const TSharedPtr<SRTSLobbyWidget> HostScreen = State->Host.IsValid() ? State->Host->GetWidget() : nullptr;
		if (!ServerState || !ClientState || !HostScreen.IsValid() || ServerState->GetSeats().Num() != 4)
		{
			Test->AddError(TEXT("Lost the lobby or the host's lobby screen"));
			return true;
		}
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		const TArray<FRTSSeat>& Seen = ClientState->GetSeats();
		const bool bSeen = Seats[2].StartSpot == 2 && Seen.Num() == 4 && Seen[2].StartSpot == 2;
		if (!bSeen && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestEqual(TEXT("The server moved the client to the spot it clicked"), Seats[2].StartSpot, 2);
		Test->TestEqual(TEXT("Which the client sees"), Seen.Num() == 4 ? Seen[2].StartSpot : INDEX_NONE, 2);
		Test->TestEqual(TEXT("The click on the host's spot changed nothing"), Seats[0].StartSpot, 0);

		Test->TestEqual(TEXT("The host's click moves its own seat at first"), HostScreen->GetMovedSeat(), 0);
		HostScreen->SelectSeat(3);
		Test->TestEqual(TEXT("Until the host selects the last AI's seat"), HostScreen->GetMovedSeat(), 3);
		Test->TestFalse(TEXT("The client's new spot is taken"), HostScreen->ClickSpot(2));
		// The host's call runs at once on the server.
		Test->TestTrue(TEXT("The spot the client left is free"), HostScreen->ClickSpot(3));
		Test->TestEqual(TEXT("The host moved the AI there"), Seats[3].StartSpot, 3);
		Test->TestEqual(TEXT("And not the client"), Seats[2].StartSpot, 2);
		return true;
	}

	/** The host moves itself to spot 3, makes the AI Hard and closes the last seat; the client picks spot 0 and readies up. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSSetUpMatch, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSSetUpMatch::Update()
	{
		ARTSLobbyPlayerController* Host = State->Host.Get();
		ARTSLobbyPlayerController* Client = State->Client.Get();
		if (!Host || !Client)
		{
			Test->AddError(TEXT("Lost a lobby controller"));
			return true;
		}
		// The host's calls run at once on the server, so spot 0 is free before the client's request arrives.
		Host->Server_SetStartSpot(0, TeamSpots[HostTeam]);
		Host->Server_SetSeatType(1, ERTSSeatType::AI, ERTSAIDifficulty::Hard);
		Host->Server_SetSeatType(3, ERTSSeatType::Closed, ERTSAIDifficulty::Normal);
		Client->Server_SetStartSpot(2, TeamSpots[ClientTeam]);
		// After the move, which would clear it; reliable calls arrive in order.
		Client->Server_SetReady(true);
		State->HostToken = GetToken(Host);
		State->ClientToken = GetToken(Client);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** Once the server has the client's spot and ready, the host starts the match. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSStartMatch, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSStartMatch::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		ARTSLobbyPlayerController* Host = State->Host.Get();
		if (!ServerState || !Host)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		if (Seats[2].StartSpot != TeamSpots[ClientTeam] || !Seats[2].bReady)
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > ReplicationTimeout)
			{
				Test->AddError(TEXT("The server did not get the client's start spot and ready"));
				return true;
			}
			return false;
		}
		Test->TestTrue(TEXT("The host starts the match"), Host->RequestStart());
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** Waits until both players play L_Skirmish, seated, and the client sees its own headquarters. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForMatch, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSWaitForMatch::Update()
	{
		UWorld* ServerWorld = FindPIEWorld(NM_ListenServer, SkirmishMapName);
		UWorld* ClientWorld = FindPIEWorld(NM_Client, SkirmishMapName);
		int32 SeatedPlayers = 0;
		if (ServerWorld)
		{
			for (TActorIterator<ARTSPlayerController> It(ServerWorld); It; ++It)
			{
				SeatedPlayers += It->GetTeamId() != INDEX_NONE && It->GetPawn() ? 1 : 0;
			}
		}
		const ARTSPlayerController* ClientController = ClientWorld ? Cast<ARTSPlayerController>(ClientWorld->GetFirstPlayerController()) : nullptr;
		const bool bReady = ServerWorld && ClientWorld && SeatedPlayers == 2
			&& RTSTests::IsNavigationReady(*ServerWorld)
			&& ClientController && ClientController->GetTeamId() == ClientTeam && ClientController->GetFaction()
			&& Count<ARTSBuilding>(*ClientWorld, ClientTeam) > 0;
		if (bReady)
		{
			State->MatchServer = ServerWorld;
			State->MatchClient = ClientWorld;
			RTSTests::ApplyTestTimeDilation(*ServerWorld);
			RTSTests::ApplyTestTimeDilation(*ClientWorld);
			return true;
		}
		if (FPlatformTime::Seconds() - State->PhaseStart > SessionTimeout)
		{
			Test->AddError(FString::Printf(TEXT("The match did not start: server world %d, client world %d, seated players %d, client team %d"),
				ServerWorld != nullptr, ClientWorld != nullptr, SeatedPlayers, ClientController ? ClientController->GetTeamId() : INDEX_NONE));
			return true;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSCheckSeats, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSCheckSeats::Update()
	{
		UWorld* World = State->MatchServer.Get();
		const ARTSGameMode* GameMode = World ? World->GetAuthGameMode<ARTSGameMode>() : nullptr;
		if (!World || !GameMode)
		{
			Test->AddError(TEXT("Lost the match"));
			return true;
		}

		const TArray<FRTSSeat>& Seats = GameMode->GetSeats();
		if (Test->TestEqual(TEXT("The match has the lobby's four seats"), Seats.Num(), 4))
		{
			Test->TestEqual(TEXT("The host's seat carries its token"), Seats[HostTeam].PlayerToken, State->HostToken);
			Test->TestEqual(TEXT("The client's seat carries its token"), Seats[ClientTeam].PlayerToken, State->ClientToken);
		}
		Test->TestEqual(TEXT("The client still holds its token"),
			State->MatchClient.IsValid() ? GetToken(State->MatchClient->GetFirstPlayerController()) : FString(), State->ClientToken);

		for (TActorIterator<ARTSPlayerController> It(World); It; ++It)
		{
			const bool bHost = It->IsLocalController();
			const TCHAR* Who = bHost ? TEXT("Host") : TEXT("Client");
			const int32 Team = bHost ? HostTeam : ClientTeam;
			const APlayerStart* Start = FindSpot(*World, TeamSpots[Team]);
			Test->TestEqual(FString::Printf(TEXT("%s plays team %d"), Who, Team), It->GetTeamId(), Team);
			Test->TestTrue(FString::Printf(TEXT("%s starts at spot %d"), Who, TeamSpots[Team]), Start && It->StartSpot.Get() == Start);
			Test->TestTrue(FString::Printf(TEXT("%s's camera stands on its spot"), Who), Start && It->GetPawn()
				&& FVector::Dist2D(It->GetPawn()->GetActorLocation(), Start->GetActorLocation()) < 1.f);
		}

		TArray<ARTSAIPlayer*> Opponents;
		for (TActorIterator<ARTSAIPlayer> It(World); It; ++It)
		{
			Opponents.Add(*It);
		}
		if (Test->TestEqual(TEXT("One AI opponent, for the one AI seat"), Opponents.Num(), 1))
		{
			ARTSAIPlayer* AI = Opponents[0];
			const ARTSPlayerState* Player = AI->GetRTSPlayerState();
			Test->TestEqual(TEXT("The AI plays team 1"), Player ? Player->GetTeamId() : INDEX_NONE, AITeam);
			Test->TestEqual(TEXT("At the difficulty chosen in the lobby"), AI->GetDifficulty(), ERTSAIDifficulty::Hard);
			State->AI = AI;
		}

		for (int32 Team : { HostTeam, AITeam, ClientTeam })
		{
			const int32 Spot = TeamSpots[Team];
			Test->TestEqual(FString::Printf(TEXT("Team %d has its headquarters at spot %d"), Team, Spot), Count<ARTSBuilding>(*World, Team, Spot), 1);
			Test->TestEqual(FString::Printf(TEXT("Nobody else builds at spot %d"), Spot), Count<ARTSBuilding>(*World, INDEX_NONE, Spot), 1);
			// The AI may already have sent its builders off; the spot's units are the team's either way.
			Test->TestTrue(FString::Printf(TEXT("Team %d has the builders of spot %d"), Team, Spot), Count<ARTSUnit>(*World, Team) >= 2);
		}
		Test->TestEqual(TEXT("No building at the spot nobody plays"), Count<ARTSBuilding>(*World, INDEX_NONE, EmptySpot), 0);
		Test->TestEqual(TEXT("No units at the spot nobody plays"), Count<ARTSUnit>(*World, INDEX_NONE, EmptySpot), 0);
		Test->TestEqual(TEXT("The closed team has no buildings"), Count<ARTSBuilding>(*World, ClosedTeam), 0);
		Test->TestEqual(TEXT("The closed team has no units"), Count<ARTSUnit>(*World, ClosedTeam), 0);
		Test->TestNull(TEXT("Nobody plays the closed team"), ARTSPlayerState::FindForTeam(*World, ClosedTeam));

		State->PhaseStart = FPlatformTime::Seconds();
		State->PhaseWorldStart = World->GetTimeSeconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyAIPlays, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyAIPlays::Update()
	{
		const ARTSAIPlayer* AI = State->AI.Get();
		UWorld* World = State->MatchServer.Get();
		if (!AI || !World)
		{
			Test->AddError(TEXT("Lost the AI or the match"));
			return true;
		}
		if (AI->GetIssuedCommandCount() > 0)
		{
			State->PhaseStart = FPlatformTime::Seconds();
			return true;
		}
		if (World->GetTimeSeconds() - State->PhaseWorldStart > AIOrderGameSeconds || FPlatformTime::Seconds() - State->PhaseStart > SessionTimeout)
		{
			Test->AddError(FString::Printf(TEXT("The AI issued no order within %.0f game seconds"), AIOrderGameSeconds));
			return true;
		}
		return false;
	}

	/** The client's scoreboard names the host by its name and the AI by its difficulty. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyClientScoreboard, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyClientScoreboard::Update()
	{
		UWorld* ServerWorld = State->MatchServer.Get();
		UWorld* ClientWorld = State->MatchClient.Get();
		if (!ServerWorld || !ClientWorld)
		{
			Test->AddError(TEXT("Lost the match"));
			return true;
		}

		const ARTSPlayerState* ServerHost = ARTSPlayerState::FindForTeam(*ServerWorld, HostTeam);
		const ARTSPlayerState* ClientHost = ARTSPlayerState::FindForTeam(*ClientWorld, HostTeam);
		const ARTSPlayerState* ClientAI = ARTSPlayerState::FindForTeam(*ClientWorld, AITeam);
		const FString HostName = ServerHost ? ServerHost->GetPlayerName() : FString();
		const FString HostLabel = ClientHost ? ClientHost->GetScoreboardLabel(true) : FString();
		const FString AILabel = ClientAI ? ClientAI->GetScoreboardLabel(true) : FString();
		const FString ExpectedAILabel = TEXT("AI (Hard)");
		const bool bSeen = !HostName.IsEmpty() && HostLabel == HostName && AILabel == ExpectedAILabel;
		if (!bSeen && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}

		Test->TestFalse(TEXT("The host has a name"), HostName.IsEmpty());
		Test->TestEqual(TEXT("The client's scoreboard shows the host's name"), HostLabel, HostName);
		Test->TestTrue(TEXT("The client sees the AI as a bot"), ClientAI && ClientAI->IsABot());
		Test->TestEqual(TEXT("The client's scoreboard shows the AI's difficulty"), AILabel, ExpectedAILabel);
		return true;
	}

	/** The host picks the duel map; the client asks for the first map, which is refused, then readies up again. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSLobbyPickMap, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSLobbyPickMap::Update()
	{
		ARTSLobbyPlayerController* Host = State->Host.Get();
		ARTSLobbyPlayerController* Client = State->Client.Get();
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		if (!Host || !Client || !ServerState)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const int32 DuelMapIndex = RTSTests::FindMapIndex(DuelMapAsset);
		Test->TestEqual(TEXT("The lobby starts on L_Skirmish"), ServerState->GetMapIndex(), RTSTests::FindMapIndex(SkirmishMapAsset));
		// The host's call runs at once on the server.
		Host->Server_SetMap(DuelMapIndex);
		Test->TestEqual(TEXT("The host picks the duel map"), ServerState->GetMapIndex(), DuelMapIndex);
		// Reliable calls arrive in order: once the server has the client ready, it has refused the client's map.
		Client->Server_SetMap(RTSTests::FindMapIndex(SkirmishMapAsset));
		Client->Server_SetReady(true);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyLobbyMap, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyLobbyMap::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		const ARTSLobbyGameState* ClientState = GetLobbyState(State->LobbyClient);
		ARTSLobbyGameMode* GameMode = State->LobbyServer.IsValid() ? State->LobbyServer->GetAuthGameMode<ARTSLobbyGameMode>() : nullptr;
		ARTSLobbyPlayerController* Client = State->Client.Get();
		if (!ServerState || !ClientState || !GameMode || !Client)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const int32 DuelMapIndex = RTSTests::FindMapIndex(DuelMapAsset);
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		const TArray<FRTSSeat>& Seen = ClientState->GetSeats();
		const TSharedPtr<SRTSLobbyWidget> ClientScreen = Client->GetWidget();
		const UTexture2D* DuelPreview = SRTSLobbyWidget::GetMapPreview(DuelMapIndex);
		const UTexture2D* ShownPreview = ClientScreen.IsValid() ? ClientScreen->GetShownPreview() : nullptr;
		const bool bSeen = Seats.Num() == 2 && Seats[1].bReady && ClientState->GetMapIndex() == DuelMapIndex && Seen.Num() == 2
			&& Seen[1].bReady && ShownPreview && ShownPreview == DuelPreview;
		if (!bSeen && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestEqual(TEXT("The client could not change the map"), ServerState->GetMapIndex(), DuelMapIndex);
		Test->TestEqual(TEXT("The client sees the duel map"), ClientState->GetMapIndex(), DuelMapIndex);
		Test->TestNotNull(TEXT("The duel map has a preview"), DuelPreview);
		Test->TestTrue(TEXT("Its preview is not the first map's"), DuelPreview != SRTSLobbyWidget::GetMapPreview(RTSTests::FindMapIndex(SkirmishMapAsset)));
		Test->TestTrue(TEXT("The client has a lobby screen"), ClientScreen.IsValid());
		Test->TestTrue(TEXT("Which shows the duel map's preview"), ShownPreview && ShownPreview == DuelPreview);
		Test->TestEqual(TEXT("With one seat per start spot"), Seen.Num(), 2);
		Test->TestTrue(TEXT("The client took the AI's seat"), Seen.Num() == 2 && Seen[1].Type == ERTSSeatType::Human
			&& Seen[1].PlayerId == Client->GetOwnPlayerId());
		Test->TestEqual(TEXT("Its base the duel map lacks is random"), Seen.Num() == 2 ? Seen[1].StartSpot : 0, int32(INDEX_NONE));
		Test->TestTrue(TEXT("And it readied up again"), Seats.Num() == 2 && Seats[1].bReady);

		const FString Expected = FString::Printf(TEXT("/Game/Maps/L_SkirmishDuel?listen?Seats=Human@0~%s,Human~%s"),
			*GetToken(State->Host.Get()), *GetToken(Client));
		Test->TestEqual(TEXT("The match starts on the duel map"), GameMode->GetMatchURL(), Expected);
		return true;
	}

	/** The client asks to remove the host, which is refused, and takes back its ready to know the server has the request. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSLobbyRemoveHost, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSLobbyRemoveHost::Update()
	{
		ARTSLobbyPlayerController* Client = State->Client.Get();
		if (!Client)
		{
			Test->AddError(TEXT("Lost the client"));
			return true;
		}
		// Reliable calls arrive in order: once the server has the client unready, it has refused the removal.
		Client->Server_RemovePlayer(0);
		Client->Server_SetReady(false);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The host is still seated; then it removes the client. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSLobbyRemoveClient, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSLobbyRemoveClient::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		ARTSLobbyPlayerController* Host = State->Host.Get();
		ARTSLobbyPlayerController* Client = State->Client.Get();
		const UWorld* ClientWorld = State->LobbyClient.Get();
		if (!ServerState || !Host || !Client || !ClientWorld || ServerState->GetSeats().Num() != 2)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		if (Seats[1].bReady && FPlatformTime::Seconds() - State->PhaseStart < ReplicationTimeout)
		{
			return false;
		}
		Test->TestFalse(TEXT("The server has the client's requests"), Seats[1].bReady);
		Test->TestTrue(TEXT("The client could not remove the host"), Seats[0].Type == ERTSSeatType::Human
			&& Seats[0].PlayerId == Host->GetOwnPlayerId());

		// The address the client joined the PIE server by, to try again once removed.
		State->ClientInstance = Client->GetGameInstance();
		State->JoinAddress = FString::Printf(TEXT("%s:%d"), *ClientWorld->URL.Host, ClientWorld->URL.Port);
		Host->Server_RemovePlayer(1);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The client's seat is open again and the client is on the main menu, which tells it why; then it tries to join again. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyRemoved, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyRemoved::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		ARTSLobbyPlayerController* Host = State->Host.Get();
		if (!ServerState || !Host || ServerState->GetSeats().Num() != 2)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		UWorld* MenuWorld = FindPIEWorld(NM_Standalone, MainMenuMapName);
		ARTSMainMenuController* Menu = MenuWorld ? Cast<ARTSMainMenuController>(MenuWorld->GetFirstPlayerController()) : nullptr;
		const bool bDone = Seats[1].Type == ERTSSeatType::Open && Menu && !Menu->GetMessage().IsEmpty();
		if (!bDone && FPlatformTime::Seconds() - State->PhaseStart < SessionTimeout)
		{
			return false;
		}
		Test->TestEqual(TEXT("The client's seat is open again"), Seats[1].Type, ERTSSeatType::Open);
		Test->TestEqual(TEXT("With nobody in it"), Seats[1].PlayerId, int32(INDEX_NONE));
		Test->TestTrue(TEXT("The host keeps its seat"), Seats[0].Type == ERTSSeatType::Human && Seats[0].PlayerId == Host->GetOwnPlayerId());
		Test->TestNotNull(TEXT("The client is on the main menu"), Menu);
		Test->TestEqual(TEXT("Which tells it why"), Menu ? Menu->GetMessage().ToString() : FString(), FString(TEXT("Removed by the host")));
		if (Menu)
		{
			State->ClientMenu = Menu;
			Menu->JoinGame(State->JoinAddress);
			State->PhaseStart = FPlatformTime::Seconds();
		}
		return true;
	}

	/** The main menu controller of the client's game instance, once it plays L_MainMenu. */
	ARTSMainMenuController* FindClientMenu(const FSessionState& State)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::PIE && World && Context.OwningGameInstance == State.ClientInstance.Get()
				&& World->GetMapName().EndsWith(MainMenuMapName))
			{
				return Cast<ARTSMainMenuController>(World->GetFirstPlayerController());
			}
		}
		return nullptr;
	}

	int32 CountHumans(const ARTSLobbyGameState* LobbyState)
	{
		int32 Result = 0;
		for (const FRTSSeat& Seat : LobbyState ? LobbyState->GetSeats() : TArray<FRTSSeat>())
		{
			Result += Seat.Type == ERTSSeatType::Human ? 1 : 0;
		}
		return Result;
	}

	/** The lobby refuses the removed client, which lands on a new main menu that tells it why, and its seat stays open. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyRemovedRefused, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyRemovedRefused::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		const ARTSMainMenuController* Menu = FindClientMenu(*State);
		if (!ServerState || ServerState->GetSeats().Num() != 2)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const bool bDone = Menu && Menu != State->ClientMenu.Get() && !Menu->GetMessage().IsEmpty();
		if (!bDone && FPlatformTime::Seconds() - State->PhaseStart < SessionTimeout)
		{
			return false;
		}
		if (!bDone)
		{
			Test->AddError(TEXT("The removed client is not back on the main menu with a message"));
			return true;
		}
		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		Test->TestEqual(TEXT("The main menu tells the removed client why it cannot join again"), Menu->GetMessage().ToString(),
			FString(TEXT("Removed by the host")));
		Test->TestEqual(TEXT("Its seat stays open"), Seats[1].Type, ERTSSeatType::Open);
		Test->TestEqual(TEXT("With nobody in it"), Seats[1].PlayerId, int32(INDEX_NONE));
		Test->TestEqual(TEXT("The host is the only human"), CountHumans(ServerState), 1);
		return true;
	}

	/** The client leaves the lobby on its own. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSClientLeavesLobby, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSClientLeavesLobby::Update()
	{
		ARTSLobbyPlayerController* Client = State->Client.Get();
		const UWorld* ClientWorld = State->LobbyClient.Get();
		if (!Client || !ClientWorld)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		// The address the client joined the PIE server by.
		State->ClientInstance = Client->GetGameInstance();
		State->JoinAddress = FString::Printf(TEXT("%s:%d"), *ClientWorld->URL.Host, ClientWorld->URL.Port);
		Client->LeaveLobby();
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/**
	 * Once the server has opened the client's seat and the client is on the main menu without a message, the host fills
	 * every open seat with an AI and the client joins by address.
	 */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSFillLobbyAndJoin, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSFillLobbyAndJoin::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		ARTSLobbyGameMode* GameMode = State->LobbyServer.IsValid() ? State->LobbyServer->GetAuthGameMode<ARTSLobbyGameMode>() : nullptr;
		ARTSLobbyPlayerController* Host = State->Host.Get();
		ARTSMainMenuController* Menu = FindClientMenu(*State);
		if (!ServerState || !GameMode || !Host)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		if ((!Menu || CountHumans(ServerState) != 1) && FPlatformTime::Seconds() - State->PhaseStart < SessionTimeout)
		{
			return false;
		}
		Test->TestEqual(TEXT("The server opens the seat of the client that left"), CountHumans(ServerState), 1);
		if (!Menu)
		{
			Test->AddError(TEXT("The client that left is not on the main menu"));
			return true;
		}
		Test->TestTrue(TEXT("Which has nothing to tell a player that left on its own"), Menu->GetMessage().IsEmpty());

		const TArray<FRTSSeat>& Seats = ServerState->GetSeats();
		for (int32 Index = 0; Index < Seats.Num(); ++Index)
		{
			if (Seats[Index].Type == ERTSSeatType::Open)
			{
				Test->TestTrue(TEXT("The host fills an open seat with an AI"),
					GameMode->SetSeatType(*Host, Index, ERTSSeatType::AI, ERTSAIDifficulty::Normal));
				State->FilledSeats.Add(Index);
			}
		}
		if (State->FilledSeats.IsEmpty())
		{
			Test->AddError(TEXT("The host had no open seat to fill"));
			return true;
		}
		State->ClientMenu = Menu;
		Menu->JoinGame(State->JoinAddress);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The full lobby refuses the client, which lands on a new main menu that tells it why; the host opens a seat again and the client joins. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyLobbyFull, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyLobbyFull::Update()
	{
		ARTSLobbyGameMode* GameMode = State->LobbyServer.IsValid() ? State->LobbyServer->GetAuthGameMode<ARTSLobbyGameMode>() : nullptr;
		ARTSLobbyPlayerController* Host = State->Host.Get();
		ARTSMainMenuController* Menu = FindClientMenu(*State);
		if (!GameMode || !Host || State->FilledSeats.IsEmpty())
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		const bool bDone = Menu && Menu != State->ClientMenu.Get() && !Menu->GetMessage().IsEmpty();
		if (!bDone && FPlatformTime::Seconds() - State->PhaseStart < SessionTimeout)
		{
			return false;
		}
		if (!bDone)
		{
			Test->AddError(TEXT("The refused client is not back on the main menu with a message"));
			return true;
		}
		Test->TestEqual(TEXT("The main menu tells the refused client why"), Menu->GetMessage().ToString(), FString(TEXT("The lobby is full")));

		Test->TestTrue(TEXT("The host opens a seat again"),
			GameMode->SetSeatType(*Host, State->FilledSeats.Last(), ERTSSeatType::Open, ERTSAIDifficulty::Normal));
		Menu->JoinGame(State->JoinAddress);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** Once the client is back in the lobby with a seat, the host leaves it. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSHostLeavesLobby, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSHostLeavesLobby::Update()
	{
		const ARTSLobbyGameState* ServerState = GetLobbyState(State->LobbyServer);
		ARTSLobbyPlayerController* Host = State->Host.Get();
		if (!ServerState || !Host)
		{
			Test->AddError(TEXT("Lost the lobby"));
			return true;
		}
		UWorld* ClientWorld = FindPIEWorld(NM_Client, TEXT("L_Lobby"));
		const ARTSLobbyPlayerController* Client = ClientWorld ? Cast<ARTSLobbyPlayerController>(ClientWorld->GetFirstPlayerController()) : nullptr;
		const bool bSeated = CountHumans(ServerState) == 2 && Client && !GetToken(Client).IsEmpty();
		if (!bSeated && FPlatformTime::Seconds() - State->PhaseStart < SessionTimeout)
		{
			return false;
		}
		if (!bSeated)
		{
			Test->AddError(TEXT("The client did not get a seat in the lobby again"));
			return true;
		}
		Host->LeaveLobby();
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The client lost the lobby with its host, and its main menu tells it why. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyHostClosedLobby, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyHostClosedLobby::Update()
	{
		const ARTSMainMenuController* Menu = FindClientMenu(*State);
		const bool bDone = Menu && !Menu->GetMessage().IsEmpty();
		if (!bDone && FPlatformTime::Seconds() - State->PhaseStart < SessionTimeout)
		{
			return false;
		}
		Test->TestNotNull(TEXT("The client is on the main menu"), Menu);
		Test->TestEqual(TEXT("Which tells it the host closed the lobby"), Menu ? Menu->GetMessage().ToString() : FString(),
			FString(URTSGameInstance::HostClosedLobbyMessage));
		return true;
	}

	/** The host leaves the running match for the main menu, which closes the listen server. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSHostLeavesMatch, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSHostLeavesMatch::Update()
	{
		UWorld* ServerWorld = State->MatchServer.Get();
		UWorld* ClientWorld = State->MatchClient.Get();
		APlayerController* Host = ServerWorld ? ServerWorld->GetFirstPlayerController() : nullptr;
		if (!Host || !ClientWorld)
		{
			Test->AddError(TEXT("Lost the match"));
			return true;
		}
		State->ClientInstance = ClientWorld->GetGameInstance();
		UGameplayStatics::OpenLevelBySoftObjectPtr(Host, GetDefault<URTSSettings>()->MainMenuMap);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** The client lost the match with its host, and its main menu tells it why. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyHostLeftMatch, FAutomationTestBase*, Test, TSharedRef<FSessionState>, State);
	bool FRTSVerifyHostLeftMatch::Update()
	{
		const ARTSMainMenuController* Menu = FindClientMenu(*State);
		const bool bDone = Menu && !Menu->GetMessage().IsEmpty();
		if (!bDone && FPlatformTime::Seconds() - State->PhaseStart < SessionTimeout)
		{
			return false;
		}
		Test->TestNotNull(TEXT("The client is on the main menu"), Menu);
		Test->TestEqual(TEXT("Which tells it the host left the match"), Menu ? Menu->GetMessage().ToString() : FString(),
			FString(URTSGameInstance::HostLeftMatchMessage));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLobbySmokeTest, "WorldDivide.Smoke.Lobby", RTSTests::TestFlags)

	bool FRTSLobbySmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(LobbyMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), LobbyMap));
			return false;
		}
		// The refused join is a network failure, which the engine logs as errors and warnings.
		AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("Network Failure:"), EAutomationExpectedErrorFlags::Contains, 0);
		const TSharedRef<FSessionState> State = MakeShared<FSessionState>();
		ADD_LATENT_AUTOMATION_COMMAND(FRTSStartLobbyPIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForLobby(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSLobbyChanges(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyLobbyChanges(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyLobbyReady(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSLobbyClickSpot(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyLobbyClickedSpot(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSLobbyPickMap(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyLobbyMap(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSLobbyRemoveHost(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSLobbyRemoveClient(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyRemoved(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyRemovedRefused(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSUseTestLobbyMaps());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLobbyLeaveSmokeTest, "WorldDivide.Smoke.LobbyLeave", RTSTests::TestFlags)

	bool FRTSLobbyLeaveSmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(LobbyMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), LobbyMap));
			return false;
		}
		// The refused join and the lost host are network failures, which the engine logs as errors and warnings.
		AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("Network Failure:"), EAutomationExpectedErrorFlags::Contains, 0);
		const TSharedRef<FSessionState> State = MakeShared<FSessionState>();
		ADD_LATENT_AUTOMATION_COMMAND(FRTSStartLobbyPIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForLobby(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSClientLeavesLobby(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSFillLobbyAndJoin(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyLobbyFull(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSHostLeavesLobby(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyHostClosedLobby(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSUseTestLobbyMaps());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSkirmishSeatsSmokeTest, "WorldDivide.Smoke.SkirmishSeats", RTSTests::TestFlags)

	bool FRTSSkirmishSeatsSmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(LobbyMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), LobbyMap));
			return false;
		}
		// The lost host is a network failure, which the engine logs as errors and warnings.
		AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("Network Failure:"), EAutomationExpectedErrorFlags::Contains, 0);
		const TSharedRef<FSessionState> State = MakeShared<FSessionState>();
		ADD_LATENT_AUTOMATION_COMMAND(FRTSStartLobbyPIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForLobby(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSSetUpMatch(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSStartMatch(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForMatch(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSCheckSeats(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAIPlays(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyClientScoreboard(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSHostLeavesMatch(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyHostLeftMatch(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSUseTestLobbyMaps());
		return true;
	}
}

#endif
