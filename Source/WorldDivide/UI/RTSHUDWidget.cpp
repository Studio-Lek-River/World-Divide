#include "UI/RTSHUDWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "UI/RTSResourceBarWidget.h"

void URTSHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	// The battlefield shows through: only the panels take clicks.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void URTSHUDWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

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
}

bool URTSHUDWidget::IsOnPanel(const FVector2D& Point) const
{
	if (!IsInViewport() && !GetParent())
	{
		return false;
	}
	// The root covers the player's viewport, and absolute units are pixels.
	const FVector2D Absolute = GetCachedGeometry().GetAbsolutePosition() + Point;

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

float URTSHUDWidget::GetTopBandHeight() const
{
	if (!ResourceBar || !ResourceBar->IsVisible())
	{
		return 0.f;
	}
	return TopBarHeight * UWidgetLayoutLibrary::GetViewportScale(const_cast<URTSHUDWidget*>(this));
}
