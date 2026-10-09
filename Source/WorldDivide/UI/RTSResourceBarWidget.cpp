#include "UI/RTSResourceBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSFactionData.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const FLinearColor BarBackground(0.03f, 0.035f, 0.04f, 0.85f);
	const FLinearColor CreditsColor(1.f, 0.85f, 0.3f, 1.f);
	const FLinearColor PowerColor(0.4f, 0.9f, 1.f, 1.f);
	const FLinearColor LowPowerColor(1.f, 0.25f, 0.2f, 1.f);

	constexpr float BarPadding = 16.f;
	constexpr float ResourceSpacing = 32.f;
	constexpr int32 FontSize = 16;

	UTextBlock* MakeText(UWidgetTree& Tree, const FName Name, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}
}

FString URTSResourceBarWidget::FormatCredits(int32 Credits)
{
	return FString::Printf(TEXT("Credits: %d"), Credits);
}

FString URTSResourceBarWidget::FormatPower(int32 Produced, int32 Consumed, bool bLowPower)
{
	return FString::Printf(TEXT("Power: %d / %d%s"), Produced, Consumed, bLowPower ? TEXT(" (low)") : TEXT(""));
}

FLinearColor URTSResourceBarWidget::GetPowerColor(bool bLowPower)
{
	return bLowPower ? LowPowerColor : PowerColor;
}

void URTSResourceBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	// Hit-testable, so a click lands here and NativeOnMouseButtonDown keeps it from the world.
	SetVisibility(ESlateVisibility::Visible);
	Refresh();
}

void URTSResourceBarWidget::BuildDefaultLayout()
{
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	Background->SetBrushColor(BarBackground);
	Background->SetPadding(FMargin(BarPadding, 0.f));
	Background->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Background;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Resources"));
	Background->SetContent(Row);

	CreditsText = MakeText(*WidgetTree, TEXT("CreditsText"), CreditsColor);
	Row->AddChildToHorizontalBox(CreditsText)->SetVerticalAlignment(VAlign_Center);

	USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("Gap"));
	Gap->SetSize(FVector2D(ResourceSpacing, 0.f));
	Row->AddChildToHorizontalBox(Gap);

	PowerText = MakeText(*WidgetTree, TEXT("PowerText"), PowerColor);
	Row->AddChildToHorizontalBox(PowerText)->SetVerticalAlignment(VAlign_Center);
}

void URTSResourceBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	Refresh();
}

void URTSResourceBarWidget::Refresh()
{
	const APlayerController* Owner = GetOwningPlayer();
	const ARTSPlayerState* State = Owner ? Owner->GetPlayerState<ARTSPlayerState>() : nullptr;
	if (CreditsText)
	{
		CreditsText->SetText(FText::FromString(FormatCredits(State ? State->GetCredits() : 0)));
	}
	if (PowerText)
	{
		const URTSFactionData* Faction = State ? State->GetFaction() : nullptr;
		const bool bShowPower = State && (!Faction || Faction->bUsesPower);
		PowerText->SetVisibility(bShowPower ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShowPower)
		{
			PowerText->SetText(FText::FromString(FormatPower(State->GetPowerProduced(), State->GetPowerConsumed(), State->IsLowPower())));
			PowerText->SetColorAndOpacity(FSlateColor(GetPowerColor(State->IsLowPower())));
		}
	}
}

FReply URTSResourceBarWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled();
}

FReply URTSResourceBarWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled();
}
