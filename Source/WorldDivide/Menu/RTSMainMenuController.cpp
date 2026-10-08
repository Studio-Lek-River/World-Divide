#include "Menu/RTSMainMenuController.h"

#include "Core/RTSSettings.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Menu/SRTSMainMenuWidget.h"
#include "Player/RTSLocalPlayer.h"

ARTSMainMenuController::ARTSMainMenuController()
{
	bShowMouseCursor = true;
}

void ARTSMainMenuController::PlaySkirmish()
{
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, GetDefault<URTSSettings>()->LobbyMap);
}

void ARTSMainMenuController::HostGame()
{
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, GetDefault<URTSSettings>()->LobbyMap, true, TEXT("listen"));
}

void ARTSMainMenuController::JoinGame(const FString& Address)
{
	if (!Address.TrimStartAndEnd().IsEmpty())
	{
		ClientTravel(Address.TrimStartAndEnd(), TRAVEL_Absolute);
	}
}

void ARTSMainMenuController::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ARTSMainMenuController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!IsLocalController() || !Viewport || Widget.IsValid())
	{
		return;
	}
	// A seat token from an earlier lobby has no seat to find any more; its message is shown once.
	if (URTSLocalPlayer* LocalPlayer = Cast<URTSLocalPlayer>(GetLocalPlayer()))
	{
		LocalPlayer->SetSeatToken(FString());
		Message = LocalPlayer->GetMenuMessage();
		LocalPlayer->SetMenuMessage(FText::GetEmpty());
	}
	Widget = SNew(SRTSMainMenuWidget).Controller(this).Message(Message);
	Viewport->AddViewportWidgetContent(Widget.ToSharedRef());

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget);
	SetInputMode(InputMode);
}

void ARTSMainMenuController::EndPlay(const EEndPlayReason::Type EndPlayReason)
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
