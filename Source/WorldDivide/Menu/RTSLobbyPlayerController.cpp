#include "Menu/RTSLobbyPlayerController.h"

#include "Core/RTSSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/RTSLobbyGameMode.h"
#include "Menu/RTSLobbyGameState.h"
#include "Menu/SRTSLobbyWidget.h"
#include "Player/RTSLocalPlayer.h"

ARTSLobbyPlayerController::ARTSLobbyPlayerController()
{
	bShowMouseCursor = true;
}

TConstArrayView<FRTSSeat> ARTSLobbyPlayerController::GetSeats() const
{
	const ARTSLobbyGameState* State = GetWorld() ? GetWorld()->GetGameState<ARTSLobbyGameState>() : nullptr;
	return State ? TConstArrayView<FRTSSeat>(State->GetSeats()) : TConstArrayView<FRTSSeat>();
}

int32 ARTSLobbyPlayerController::GetMapIndex() const
{
	const ARTSLobbyGameState* State = GetWorld() ? GetWorld()->GetGameState<ARTSLobbyGameState>() : nullptr;
	return State ? State->GetMapIndex() : INDEX_NONE;
}

bool ARTSLobbyPlayerController::IsHost() const
{
	return ARTSLobbyGameMode::IsHost(*this);
}

int32 ARTSLobbyPlayerController::GetOwnPlayerId() const
{
	const APlayerState* State = GetPlayerState<APlayerState>();
	return State ? State->GetPlayerId() : INDEX_NONE;
}

const FRTSSeat* ARTSLobbyPlayerController::GetOwnSeat() const
{
	const int32 PlayerId = GetOwnPlayerId();
	return PlayerId == INDEX_NONE ? nullptr : GetSeats().FindByPredicate([PlayerId](const FRTSSeat& Seat)
	{
		return Seat.Type == ERTSSeatType::Human && Seat.PlayerId == PlayerId;
	});
}

void ARTSLobbyPlayerController::Server_SetMap_Implementation(int32 Index)
{
	if (ARTSLobbyGameMode* GameMode = GetWorld()->GetAuthGameMode<ARTSLobbyGameMode>())
	{
		GameMode->SetMap(*this, Index);
	}
}

void ARTSLobbyPlayerController::Server_SetSeatType_Implementation(int32 Index, ERTSSeatType Type, ERTSAIDifficulty Difficulty)
{
	if (ARTSLobbyGameMode* GameMode = GetWorld()->GetAuthGameMode<ARTSLobbyGameMode>())
	{
		GameMode->SetSeatType(*this, Index, Type, Difficulty);
	}
}

void ARTSLobbyPlayerController::Server_SetStartSpot_Implementation(int32 Index, int32 Spot)
{
	if (ARTSLobbyGameMode* GameMode = GetWorld()->GetAuthGameMode<ARTSLobbyGameMode>())
	{
		GameMode->SetStartSpot(*this, Index, Spot);
	}
}

void ARTSLobbyPlayerController::Server_SetReady_Implementation(bool bReady)
{
	if (ARTSLobbyGameMode* GameMode = GetWorld()->GetAuthGameMode<ARTSLobbyGameMode>())
	{
		GameMode->SetReady(*this, bReady);
	}
}

void ARTSLobbyPlayerController::Client_SetSeatToken_Implementation(const FString& Token)
{
	if (URTSLocalPlayer* LocalPlayer = Cast<URTSLocalPlayer>(GetLocalPlayer()))
	{
		LocalPlayer->SetSeatToken(Token);
	}
}

bool ARTSLobbyPlayerController::RequestStart()
{
	ARTSLobbyGameMode* GameMode = GetWorld()->GetAuthGameMode<ARTSLobbyGameMode>();
	return GameMode && GameMode->StartMatch(*this);
}

void ARTSLobbyPlayerController::LeaveLobby()
{
	if (URTSLocalPlayer* LocalPlayer = Cast<URTSLocalPlayer>(GetLocalPlayer()))
	{
		LocalPlayer->SetSeatToken(FString());
	}
	// Opening a map without "listen" also closes a hosted lobby; its clients lose the connection and return to the menu.
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, GetDefault<URTSSettings>()->MainMenuMap);
}

void ARTSLobbyPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	// Runs once the local player is attached, which on a joining client is after BeginPlay.
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!IsLocalController() || !Viewport || Widget.IsValid())
	{
		return;
	}
	Widget = SNew(SRTSLobbyWidget).Controller(this);
	Viewport->AddViewportWidgetContent(Widget.ToSharedRef());

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget);
	SetInputMode(InputMode);
}

void ARTSLobbyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Widget.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
		}
		Widget.Reset();
	}
	Super::EndPlay(EndPlayReason);
}
