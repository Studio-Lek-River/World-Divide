#include "UI/RTSHUDWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Core/RTSGameState.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Player/RTSHUD.h"
#include "Player/RTSPlayerController.h"
#include "UI/Layout/RTSLayoutFiles.h"
#include "UI/Layout/RTSLayoutScreen.h"
#include "UI/Layout/RTSLayoutWidget.h"
#include "UI/RTSResourceBarWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

const TCHAR* URTSHUDWidget::HUDLayout = TEXT("HUD");
const TCHAR* URTSHUDWidget::MatchResultLayout = TEXT("MatchResult");
const TCHAR* URTSHUDWidget::InGameMenuLayout = TEXT("InGameMenu");

namespace
{
	/** The modal screens when their layout files are missing, so the match can still be left or restarted. */
	const TCHAR* FallbackMatchResult = TEXT(R"({
		"type": "Panel", "anchor": "Fill", "color": "#000000A0", "hitTest": true,
		"children": [{ "type": "VBox", "anchor": "Center", "spacing": 16, "children": [
			{ "type": "Text", "text": "{match.result.title}", "font": "Title", "bind": { "color": "match.result.color" } },
			{ "type": "Text", "text": "{match.result.message}" },
			{ "type": "Button", "action": "Restart", "size": [240, 40], "visibleWhen": "match.canRestart",
				"children": [{ "type": "Text", "anchor": "Center", "text": "Restart" }] },
			{ "type": "Text", "text": "Waiting for the host to restart", "visibleWhen": "!match.canRestart" },
			{ "type": "Button", "action": "Leave", "size": [240, 40], "children": [{ "type": "Text", "anchor": "Center", "text": "Leave match" }] }
		] }]
	})");

	const TCHAR* FallbackInGameMenu = TEXT(R"({
		"type": "Panel", "anchor": "Fill", "color": "#000000A0", "hitTest": true,
		"children": [{ "type": "VBox", "anchor": "Center", "spacing": 12, "children": [
			{ "type": "Text", "text": "Menu", "font": "Title" },
			{ "type": "Button", "action": "Resume", "size": [240, 40], "children": [{ "type": "Text", "anchor": "Center", "text": "Resume" }] },
			{ "type": "Button", "action": "Leave", "size": [240, 40], "children": [{ "type": "Text", "anchor": "Center", "text": "Leave match" }] }
		] }]
	})");

	FAutoConsoleCommandWithWorld ReloadCommand(
		TEXT("rts.UI.Reload"),
		TEXT("Rereads the UI layout files and rebuilds the HUD, the match kept. Sprites and themes need the setup script."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&URTSHUDWidget::ReloadAll));
}

void URTSHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Layers"));
	Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Layers;

	GameLayer = WidgetTree->ConstructWidget<URTSLayoutScreen>(URTSLayoutScreen::StaticClass(), TEXT("Game"));
	GameLayer->Configure(/*bMenuInput*/ false, /*bBackHandler*/ false);
	UOverlaySlot* GameSlot = Layers->AddChildToOverlay(GameLayer);
	GameSlot->SetHorizontalAlignment(HAlign_Fill);
	GameSlot->SetVerticalAlignment(VAlign_Fill);

	ModalStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(), TEXT("Modal"));
	ModalStack->SetTransitionDuration(0.f);
	ModalStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UOverlaySlot* ModalSlot = Layers->AddChildToOverlay(ModalStack);
	ModalSlot->SetHorizontalAlignment(HAlign_Fill);
	ModalSlot->SetVerticalAlignment(VAlign_Fill);

	Rebuild();
	// The battlefield shows through: only the panels take clicks.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void URTSHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	SyncModals();
}

void URTSHUDWidget::Rebuild()
{
	ResourceBar = nullptr;
	MinimapSlot = nullptr;
	SelectionSlot = nullptr;
	CommandSlot = nullptr;
	if (!GameLayer->ShowLayout(HUDLayout))
	{
		GameLayer->ShowWidget(BuildDefaultLayout());
	}
	ClearModals();
}

void URTSHUDWidget::ReloadAll(UWorld* World)
{
	RTSLayoutFiles::ClearCache();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const ARTSHUD* HUD = It->IsValid() && (*It)->IsLocalController() ? (*It)->GetHUD<ARTSHUD>() : nullptr;
		if (URTSHUDWidget* Widget = HUD ? HUD->GetHUDWidget() : nullptr)
		{
			Widget->Rebuild();
		}
	}
}

UWidget* URTSHUDWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Stretched across the top; on a horizontally stretched slot the offsets are the insets, then the top and the height.
	ResourceBar = WidgetTree->ConstructWidget<URTSResourceBarWidget>(URTSResourceBarWidget::StaticClass(), TEXT("ResourceBar"));
	UCanvasPanelSlot* BarSlot = Root->AddChildToCanvas(ResourceBar);
	BarSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
	BarSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, TopBarHeight));

	// Stretched across the bottom, growing up from the edge. Empty until later panels fill its slots.
	UHorizontalBox* Band = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BottomBand"));
	Band->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UCanvasPanelSlot* BandSlot = Root->AddChildToCanvas(Band);
	BandSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
	BandSlot->SetAlignment(FVector2D(0.f, 1.f));
	BandSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, BottomBandHeight));

	auto AddSlot = [this, Band](const FName Name, float Width) {
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), Name);
		Box->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UHorizontalBoxSlot* BoxSlot = Band->AddChildToHorizontalBox(Box);
		if (Width > 0.f)
		{
			Box->SetWidthOverride(Width);
		}
		else
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		return Box;
	};
	MinimapSlot = AddSlot(TEXT("MinimapSlot"), MinimapWidth);
	SelectionSlot = AddSlot(TEXT("SelectionSlot"), 0.f);
	CommandSlot = AddSlot(TEXT("CommandSlot"), CommandPanelWidth);
	return Root;
}

URTSLayoutWidget* URTSHUDWidget::GetLayout() const
{
	return GameLayer ? GameLayer->GetLayout() : nullptr;
}

TArray<URTSLayoutScreen*> URTSHUDWidget::GetModals() const
{
	TArray<URTSLayoutScreen*> Modals;
	if (ModalStack)
	{
		for (UCommonActivatableWidget* Widget : ModalStack->GetWidgetList())
		{
			if (URTSLayoutScreen* Screen = Cast<URTSLayoutScreen>(Widget))
			{
				Modals.Add(Screen);
			}
		}
	}
	return Modals;
}

URTSLayoutScreen* URTSHUDWidget::PushModal(const FString& Name, bool bBackHandler)
{
	// Esc closes the menu the controller holds open; on the result screen, which stays, it opens the menu over it.
	FSimpleDelegate OnBack;
	bool bCloseOnBack = true;
	if (Name == InGameMenuLayout || Name == MatchResultLayout)
	{
		const bool bOpenMenu = Name == MatchResultLayout;
		bCloseOnBack = !bOpenMenu;
		OnBack = FSimpleDelegate::CreateWeakLambda(this, [this, bOpenMenu]() {
			if (ARTSPlayerController* Controller = Cast<ARTSPlayerController>(GetOwningPlayer()))
			{
				Controller->SetInGameMenuOpen(bOpenMenu);
			}
		});
	}
	// A new screen each time rather than one from the stack's pool, so it registers Esc as configured.
	URTSLayoutScreen* Screen = CreateWidget<URTSLayoutScreen>(this, URTSLayoutScreen::StaticClass());
	if (!Screen)
	{
		return nullptr;
	}
	Screen->Configure(/*bMenuInput*/ true, bBackHandler, OnBack, bCloseOnBack);
	if (!Screen->ShowLayout(Name))
	{
		const TCHAR* Fallback = Name == MatchResultLayout ? FallbackMatchResult : Name == InGameMenuLayout ? FallbackInGameMenu : nullptr;
		FString Error;
		const TSharedPtr<FJsonObject> Object = Fallback ? RTSLayoutFiles::Parse(Fallback, Error) : nullptr;
		URTSLayoutWidget* Layout = CreateWidget<URTSLayoutWidget>(Screen, URTSLayoutWidget::StaticClass());
		if (Object && Layout)
		{
			Layout->BuildFromJson(*Object, Name + TEXT(" (built in)"), URTSLayoutWidget::GetDefaultChain(this),
				RTSUIBindings::GetDefaultSources());
			Screen->ShowWidget(Layout);
		}
	}
	ModalStack->AddWidgetInstance(*Screen);
	return Screen;
}

void URTSHUDWidget::ClearModals()
{
	if (ModalStack)
	{
		ModalStack->ClearWidgets();
	}
	ShownModals.Reset();
	SyncedModals.Reset();
}

void URTSHUDWidget::SyncModals()
{
	const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(GetOwningPlayer());
	if (!Controller || !ModalStack)
	{
		return;
	}
	TArray<FString> Wanted;
	if (Controller->GetMatchResult() != ERTSMatchResult::InProgress)
	{
		Wanted.Add(MatchResultLayout);
	}
	if (Controller->IsInGameMenuOpen())
	{
		Wanted.Add(InGameMenuLayout);
	}

	// A screen Esc closed has left the stack by itself.
	bool bChanged = Wanted != ShownModals;
	for (const TWeakObjectPtr<URTSLayoutScreen>& Screen : SyncedModals)
	{
		bChanged |= !Screen.IsValid() || !ModalStack->GetWidgetList().Contains(Screen.Get());
	}
	if (!bChanged)
	{
		return;
	}
	for (const TWeakObjectPtr<URTSLayoutScreen>& Screen : SyncedModals)
	{
		if (Screen.IsValid() && ModalStack->GetWidgetList().Contains(Screen.Get()))
		{
			ModalStack->RemoveWidget(*Screen.Get());
		}
	}
	SyncedModals.Reset();
	ShownModals = Wanted;
	for (const FString& Name : Wanted)
	{
		SyncedModals.Add(PushModal(Name, /*bBackHandler*/ true));
	}
}

bool URTSHUDWidget::IsOnPanel(const FVector2D& Point) const
{
	if (!IsInViewport() && !GetParent())
	{
		return false;
	}
	// A modal screen covers everything.
	if (ModalStack && ModalStack->GetNumWidgets() > 0)
	{
		return true;
	}
	// The root covers the player's viewport, and absolute units are pixels.
	const FVector2D Absolute = GetCachedGeometry().GetAbsolutePosition() + Point;
	if (const URTSLayoutWidget* Layout = GetLayout())
	{
		return Layout->IsOnPanel(Absolute);
	}

	const UWidget* Panels[] = {
		ResourceBar,
		MinimapSlot ? MinimapSlot->GetContent() : nullptr,
		SelectionSlot ? SelectionSlot->GetContent() : nullptr,
		CommandSlot ? CommandSlot->GetContent() : nullptr,
	};
	for (const UWidget* Panel : Panels)
	{
		if (Panel && Panel->GetVisibility() == ESlateVisibility::Visible && Panel->GetCachedGeometry().IsUnderLocation(Absolute))
		{
			return true;
		}
	}
	return false;
}

bool URTSHUDWidget::GetElementRect(FName Name, FBox2D& OutRect) const
{
	const URTSLayoutWidget* Layout = GetLayout();
	const UWidget* Element = Layout ? Layout->FindElement(Name) : nullptr;
	if (!Element || !Element->IsVisible())
	{
		return false;
	}
	const FGeometry& Geometry = Element->GetCachedGeometry();
	if (Geometry.GetAbsoluteSize().IsNearlyZero())
	{
		return false;
	}
	const FVector2D Min = Geometry.GetAbsolutePosition() - GetCachedGeometry().GetAbsolutePosition();
	OutRect = FBox2D(Min, Min + Geometry.GetAbsoluteSize());
	return true;
}

float URTSHUDWidget::GetLeftColumnBottom() const
{
	FBox2D Rect;
	if (GetElementRect(TEXT("Notifications"), Rect) || GetElementRect(TEXT("CommanderBar"), Rect))
	{
		return Rect.Max.Y;
	}
	// The C++ fallback has only its resource bar along the top.
	return !GetLayout() && ResourceBar && ResourceBar->IsVisible()
		? TopBarHeight * UWidgetLayoutLibrary::GetViewportScale(const_cast<URTSHUDWidget*>(this)) : 0.f;
}

float URTSHUDWidget::GetRightColumnBottom() const
{
	FBox2D Rect;
	if (GetElementRect(TEXT("MapButtons"), Rect) || GetElementRect(TEXT("Minimap"), Rect))
	{
		return Rect.Max.Y;
	}
	return !GetLayout() && ResourceBar && ResourceBar->IsVisible()
		? TopBarHeight * UWidgetLayoutLibrary::GetViewportScale(const_cast<URTSHUDWidget*>(this)) : 0.f;
}

float URTSHUDWidget::GetBottomFrameHeight() const
{
	FBox2D Rect;
	if (GetElementRect(TEXT("ResourceBar"), Rect))
	{
		return FMath::Max(0.f, GetCachedGeometry().GetAbsoluteSize().Y - Rect.Min.Y);
	}
	return !GetLayout() ? BottomBandHeight * UWidgetLayoutLibrary::GetViewportScale(const_cast<URTSHUDWidget*>(this)) : 0.f;
}
