#include "UI/Layout/RTSLayoutScreen.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Misc/App.h"
#include "UI/Layout/RTSLayoutWidget.h"

bool URTSLayoutScreen::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}
	if (!WidgetTree->RootWidget)
	{
		Content = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Content"));
		Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Content;
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return true;
}

void URTSLayoutScreen::Configure(bool bInMenuInput, bool bInBackHandler, FSimpleDelegate InOnBack, bool bInCloseOnBack)
{
	bMenuInput = bInMenuInput;
	bCloseOnBack = bInCloseOnBack;
	bIsBackHandler = bInBackHandler;
	// A stack activates the screens it shows; the Game layer is always active.
	bAutoActivate = !bInMenuInput;
	OnBack = MoveTemp(InOnBack);
}

URTSLayoutWidget* URTSLayoutScreen::ShowLayout(const FString& Name)
{
	URTSLayoutWidget* Built = CreateWidget<URTSLayoutWidget>(this, URTSLayoutWidget::StaticClass());
	if (!Built || !Built->BuildFromFile(Name))
	{
		return nullptr;
	}
	ShowWidget(Built);
	Layout = Built;
	LayoutName = Name;
	return Built;
}

void URTSLayoutScreen::ShowWidget(UWidget* Widget)
{
	Layout = nullptr;
	LayoutName.Reset();
	Content->ClearChildren();
	if (Widget)
	{
		UOverlaySlot* OverlaySlot = Content->AddChildToOverlay(Widget);
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}
}

TOptional<FUIInputConfig> URTSLayoutScreen::GetDesiredInputConfig() const
{
	// A headless test run has no real viewport; locking would clip the desktop cursor to a corner.
	const bool bLockMouse = FApp::CanEverRender() && !GIsAutomationTesting;
	const EMouseLockMode LockMode = bLockMouse ? EMouseLockMode::LockAlways : EMouseLockMode::DoNotLock;
	if (bMenuInput)
	{
		// Without capture: a viewport that holds the mouse would still pass game input in Menu mode.
		return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, LockMode, false);
	}
	// As ARTSPlayerController::SetupInputComponent sets it.
	return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CaptureDuringMouseDown, LockMode, false);
}

bool URTSLayoutScreen::NativeOnHandleBackAction()
{
	if (!bIsBackHandler)
	{
		return Super::NativeOnHandleBackAction();
	}
	OnBack.ExecuteIfBound();
	return bCloseOnBack ? Super::NativeOnHandleBackAction() : true;
}
