#include "Menu/RTSLobbyGameMode.h"

#include "Data/RTSMapData.h"
#include "GameFramework/PlayerState.h"
#include "Menu/RTSLobbyGameState.h"
#include "Menu/RTSLobbyPlayerController.h"
#include "WorldDivide.h"

ARTSLobbyGameMode::ARTSLobbyGameMode()
{
	GameStateClass = ARTSLobbyGameState::StaticClass();
	PlayerControllerClass = ARTSLobbyPlayerController::StaticClass();
	// Nothing to steer in the lobby.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}

ARTSLobbyGameState* ARTSLobbyGameMode::GetLobbyState() const
{
	return GetGameState<ARTSLobbyGameState>();
}

void ARTSLobbyGameMode::InitGameState()
{
	Super::InitGameState();

	// The map's defaults with room for every player who joins: the host takes the first open seat, offline the AI is its
	// opponent, and open seats nobody takes stay out of the match.
	ARTSLobbyGameState* State = GetLobbyState();
	const URTSMapData* Map = State->GetMap();
	if (!Map)
	{
		UE_LOG(LogWorldDivide, Error, TEXT("No skirmish maps in the World Divide settings"));
		return;
	}
	State->GetMutableSeats() = RTSSkirmish::MakeLobbySeats(Map->DefaultSeats, Map->StartSpots);
}

void ARTSLobbyGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	const bool bAnyOpen = GetLobbyState()->GetSeats().ContainsByPredicate(
		[](const FRTSSeat& Seat) { return Seat.Type == ERTSSeatType::Open; });
	if (ErrorMessage.IsEmpty() && !bAnyOpen)
	{
		ErrorMessage = TEXT("The lobby is full");
	}
}

void ARTSLobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	const APlayerState* Player = NewPlayer->GetPlayerState<APlayerState>();
	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const int32 Seat = RTSSkirmish::TakeOpenSeat(GetLobbyState()->GetMutableSeats(), Player->GetPlayerId(), Player->GetPlayerName(), Token,
		IsHost(*NewPlayer));
	if (Seat == INDEX_NONE)
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("No open seat for %s"), *Player->GetPlayerName());
		return;
	}
	if (ARTSLobbyPlayerController* Controller = Cast<ARTSLobbyPlayerController>(NewPlayer))
	{
		Controller->Client_SetSeatToken(Token);
	}
	UE_LOG(LogWorldDivide, Log, TEXT("%s takes seat %d"), *Player->GetPlayerName(), Seat);
}

void ARTSLobbyGameMode::Logout(AController* Exiting)
{
	if (const APlayerState* Player = Exiting ? Exiting->GetPlayerState<APlayerState>() : nullptr)
	{
		RTSSkirmish::ReleaseSeat(GetLobbyState()->GetMutableSeats(), Player->GetPlayerId());
	}
	Super::Logout(Exiting);
}

bool ARTSLobbyGameMode::IsHost(const APlayerController& Requester)
{
	return Requester.IsLocalController() && Requester.HasAuthority();
}

bool ARTSLobbyGameMode::SetSeatType(const APlayerController& Requester, int32 Index, ERTSSeatType Type, ERTSAIDifficulty Difficulty)
{
	TArray<FRTSSeat>& Seats = GetLobbyState()->GetMutableSeats();
	if (!RTSSkirmish::CanChangeType(Seats, Index, IsHost(Requester), Type))
	{
		return false;
	}
	Seats[Index].Type = Type;
	Seats[Index].Difficulty = Difficulty;
	return true;
}

bool ARTSLobbyGameMode::SetStartSpot(const APlayerController& Requester, int32 Index, int32 Spot)
{
	ARTSLobbyGameState* State = GetLobbyState();
	const APlayerState* Player = Requester.GetPlayerState<APlayerState>();
	return Player && RTSSkirmish::ChangeStartSpot(State->GetMutableSeats(), Index, IsHost(Requester), Player->GetPlayerId(), Spot,
		State->GetSpotCount());
}

bool ARTSLobbyGameMode::SetMap(const APlayerController& Requester, int32 Index)
{
	ARTSLobbyGameState* State = GetLobbyState();
	const URTSMapData* Map = ARTSLobbyGameState::LoadMap(Index);
	const APlayerState* Player = Requester.GetPlayerState<APlayerState>();
	if (!Map || !Player || !RTSSkirmish::CanChangeMap(State->GetSeats(), IsHost(Requester), Map->StartSpots)
		|| !RTSSkirmish::ResizeSeats(State->GetMutableSeats(), Map->DefaultSeats, Map->StartSpots, Player->GetPlayerId()))
	{
		return false;
	}
	State->SetMapIndex(Index);
	UE_LOG(LogWorldDivide, Log, TEXT("The lobby plays %s"), *Map->DisplayName.ToString());
	return true;
}

bool ARTSLobbyGameMode::SetReady(const APlayerController& Requester, bool bReady)
{
	const APlayerState* Player = Requester.GetPlayerState<APlayerState>();
	return Player && RTSSkirmish::SetReady(GetLobbyState()->GetMutableSeats(), Player->GetPlayerId(), IsHost(Requester), bReady);
}

FString ARTSLobbyGameMode::GetMatchURL() const
{
	const URTSMapData* Map = GetLobbyState()->GetMap();
	if (!Map)
	{
		return FString();
	}
	FString URL = Map->Map.GetLongPackageName();
	if (GetNetMode() == NM_ListenServer)
	{
		URL += TEXT("?listen");
	}
	return URL + TEXT("?Seats=") + RTSSkirmish::FormatSeats(RTSSkirmish::ToMatchSeats(GetLobbyState()->GetSeats()));
}

bool ARTSLobbyGameMode::StartMatch(const APlayerController& Requester)
{
	if (!IsHost(Requester) || !RTSSkirmish::CanStart(GetLobbyState()->GetSeats()))
	{
		return false;
	}
	const FString URL = GetMatchURL();
	if (URL.IsEmpty())
	{
		return false;
	}
	UE_LOG(LogWorldDivide, Log, TEXT("Starting the match: %s"), *URL);
	// Relative, so the match listens on the lobby's port.
	return GetWorld()->ServerTravel(URL);
}
