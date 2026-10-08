#include "Menu/SRTSMainMenuWidget.h"

#include "Menu/RTSMainMenuController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SRTSMainMenuWidget::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.9f))
		.Padding(24.f)
		[
			SNew(SBox).WidthOverride(320.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 20.f)
				[
					SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 28)).Text(FText::FromString(TEXT("World Divide")))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(STextBlock)
					.Text(InArgs._Message)
					.ColorAndOpacity(FLinearColor(1.f, 0.6f, 0.3f))
					.AutoWrapText(true)
					.Visibility(InArgs._Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
				[
					MakeButton(TEXT("Skirmish"), [](ARTSMainMenuController& Menu) { Menu.PlaySkirmish(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
				[
					MakeButton(TEXT("Host game"), [](ARTSMainMenuController& Menu) { Menu.HostGame(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SAssignNew(AddressBox, SEditableTextBox)
						.Text(FText::FromString(TEXT("127.0.0.1")))
						.ToolTipText(FText::FromString(TEXT("Address of the host")))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeButton(TEXT("Join game"), [this](ARTSMainMenuController& Menu)
						{
							Menu.JoinGame(AddressBox->GetText().ToString());
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
				[
					MakeButton(TEXT("Quit"), [](ARTSMainMenuController& Menu) { Menu.QuitGame(); })
				]
			]
		]
	];
}

TSharedRef<SWidget> SRTSMainMenuWidget::MakeButton(const TCHAR* Label, TFunction<void(ARTSMainMenuController&)> OnClicked)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.Text(FText::FromString(Label))
		.OnClicked_Lambda([this, OnClicked]()
		{
			if (Controller.IsValid())
			{
				OnClicked(*Controller);
			}
			return FReply::Handled();
		});
}
