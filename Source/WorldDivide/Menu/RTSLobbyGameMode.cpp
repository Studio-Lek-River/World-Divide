#include "Menu/RTSLobbyGameMode.h"

#include "Core/RTSSettings.h"
#include "Core/RTSSkirmishGameMode.h"
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

	// The skirmish's defaults with room for every player who joins: the host takes the first open seat, offline the AI
	// is its opponent, and open seats nobody takes stay out of the match.
	TArray<FRTSSeat>& Seats = GetLobbyState()->GetMutableSeats();
	Seats = GetDefault<ARTSSkirmishGameMode>()->Seats;
	for (FRTSSeat& Seat : Seats)
	{
		if (Seat.Type == ERTSSeatType::Closed)
		{
			Seat.Type = ERTSSeatType::Open;
		}
	}
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
	const int32 Seat = RTSSkirmish::TakeOpenSeat(GetLobbyState()->GetMutableSeats(), Player->GetPlayerId(), Player->GetPlayerName(), Token);
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
	if (!Player || !RTSSkirmish::CanChangeStartSpot(State->GetSeats(), Index, IsHost(Requester), Player->GetPlayerId(), Spot,
		State->GetSpotCount()))
	{
		return false;
	}
	State->GetMutableSeats()[Index].StartSpot = Spot;
	return true;
}

FString ARTSLobbyGameMode::GetMatchURL() const
{
	FString URL = GetDefault<URTSSettings>()->SkirmishMap.ToSoftObjectPath().GetLongPackageName();
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
	UE_LOG(LogWorldDivide, Log, TEXT("Starting the match: %s"), *URL);
	// Relative, so the match listens on the lobby's port.
	return GetWorld()->ServerTravel(URL);
}
