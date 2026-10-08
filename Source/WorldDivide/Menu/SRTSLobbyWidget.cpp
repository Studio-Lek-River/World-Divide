#include "Menu/SRTSLobbyWidget.h"

#include "Core/RTSSettings.h"
#include "Core/RTSSkirmishSeats.h"
#include "Data/RTSMapData.h"
#include "Menu/RTSLobbyGameState.h"
#include "Menu/RTSLobbyPlayerController.h"
#include "Styling/CoreStyle.h"
#include "Units/RTSUnit.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr int32 OpenOption = 0;
	constexpr int32 ClosedOption = 1;
	constexpr int32 FirstAIOption = 2;
	constexpr int32 TypeOptionCount = FirstAIOption + 3;

	int32 GetTypeOption(const FRTSSeat& Seat)
	{
		switch (Seat.Type)
		{
		case ERTSSeatType::Open: return OpenOption;
		case ERTSSeatType::AI: return FirstAIOption + static_cast<int32>(Seat.Difficulty);
		default: return ClosedOption;
		}
	}

	FSlateFontInfo GetFont(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}

	const FRTSSeat* FindSeat(const TWeakObjectPtr<ARTSLobbyPlayerController>& Controller, int32 Index)
	{
		const TConstArrayView<FRTSSeat> Seats = Controller.IsValid() ? Controller->GetSeats() : TConstArrayView<FRTSSeat>();
		return Seats.IsValidIndex(Index) ? &Seats[Index] : nullptr;
	}
}

FString SRTSLobbyWidget::GetTypeLabel(int32 TypeOption)
{
	if (TypeOption == OpenOption)
	{
		return TEXT("Open");
	}
	if (TypeOption == ClosedOption)
	{
		return TEXT("Closed");
	}
	const int64 Difficulty = TypeOption - FirstAIOption;
	return FString::Printf(TEXT("AI (%s)"), *StaticEnum<ERTSAIDifficulty>()->GetNameStringByValue(Difficulty));
}

FString SRTSLobbyWidget::GetMapLabel(int32 MapIndex)
{
	const URTSMapData* Map = ARTSLobbyGameState::LoadMap(MapIndex);
	return Map ? Map->DisplayName.ToString() : FString();
}

FString SRTSLobbyWidget::GetSpotLabel(int32 Spot)
{
	return Spot == INDEX_NONE ? TEXT("Random") : FString::Printf(TEXT("Base %d"), Spot + 1);
}

void SRTSLobbyWidget::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	for (int32 Option = 0; Option < TypeOptionCount; ++Option)
	{
		TypeOptions.Add(MakeShared<int32>(Option));
	}
	for (int32 MapIndex = 0; MapIndex < GetDefault<URTSSettings>()->SkirmishMaps.Num(); ++MapIndex)
	{
		MapOptions.Add(MakeShared<int32>(MapIndex));
	}

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.9f))
		.Padding(24.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
			[
				SNew(STextBlock).Font(GetFont(24)).Text(FText::FromString(TEXT("Skirmish")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(STextBlock).Font(GetFont(14)).Text(FText::FromString(TEXT("Map")))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(220.f)
					[
						SNew(SComboBox<TSharedPtr<int32>>)
						.OptionsSource(&MapOptions)
						.IsEnabled_Lambda([this]() { return Controller.IsValid() && Controller->IsHost(); })
						.OnGenerateWidget_Lambda([](TSharedPtr<int32> Option)
						{
							return SNew(STextBlock).Text(FText::FromString(GetMapLabel(*Option)));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Option, ESelectInfo::Type SelectInfo)
						{
							if (Option.IsValid() && Controller.IsValid() && SelectInfo != ESelectInfo::Direct)
							{
								Controller->Server_SetMap(*Option);
							}
						})
						[
							SNew(STextBlock).Text_Lambda([this]()
							{
								return FText::FromString(Controller.IsValid() ? GetMapLabel(Controller->GetMapIndex()) : FString());
							})
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(Rows, SVerticalBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Font(GetFont(12))
				.Text_Lambda([this]()
				{
					if (!Controller.IsValid())
					{
						return FText::GetEmpty();
					}
					const bool bAllReady = !Controller->GetSeats().ContainsByPredicate(
						[](const FRTSSeat& Seat) { return Seat.Type == ERTSSeatType::Human && !Seat.bReady; });
					if (Controller->IsHost())
					{
						return FText::FromString(bAllReady ? TEXT("Pick the map and set up the seats, then start the match.")
							: TEXT("Waiting for every player to be ready."));
					}
					const FRTSSeat* Own = Controller->GetOwnSeat();
					return FText::FromString(Own && !Own->bReady ? TEXT("Pick your base, then press Ready.")
						: TEXT("Waiting for the host to start the match."));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 16.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Back")))
					.OnClicked_Lambda([this]()
					{
						if (Controller.IsValid())
						{
							Controller->LeaveLobby();
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton)
					.Text_Lambda([this]()
					{
						const FRTSSeat* Own = Controller.IsValid() ? Controller->GetOwnSeat() : nullptr;
						return FText::FromString(Own && Own->bReady ? TEXT("Not ready") : TEXT("Ready"));
					})
					.Visibility_Lambda([this]()
					{
						return Controller.IsValid() && !Controller->IsHost() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.IsEnabled_Lambda([this]() { return Controller.IsValid() && Controller->GetOwnSeat() != nullptr; })
					.OnClicked_Lambda([this]()
					{
						if (const FRTSSeat* Own = Controller.IsValid() ? Controller->GetOwnSeat() : nullptr)
						{
							Controller->Server_SetReady(!Own->bReady);
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Start")))
					.Visibility_Lambda([this]()
					{
						return Controller.IsValid() && Controller->IsHost() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.IsEnabled_Lambda([this]()
					{
						return Controller.IsValid() && RTSSkirmish::CanStart(Controller->GetSeats());
					})
					.OnClicked_Lambda([this]()
					{
						if (Controller.IsValid())
						{
							Controller->RequestStart();
						}
						return FReply::Handled();
					})
				]
			]
		]
	];
}

void SRTSLobbyWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const int32 SeatCount = Controller.IsValid() ? Controller->GetSeats().Num() : 0;
	if (SeatCount != BuiltSeats)
	{
		RebuildRows(SeatCount);
	}
}

void SRTSLobbyWidget::RebuildRows(int32 SeatCount)
{
	BuiltSeats = SeatCount;
	SpotOptions.Reset();
	SpotOptions.Add(MakeShared<int32>(INDEX_NONE));
	for (int32 Spot = 0; Spot < SeatCount; ++Spot)
	{
		SpotOptions.Add(MakeShared<int32>(Spot));
	}
	Rows->ClearChildren();
	for (int32 Index = 0; Index < SeatCount; ++Index)
	{
		Rows->AddSlot().AutoHeight().Padding(0.f, 4.f)[MakeRow(Index)];
	}
}

TSharedRef<SWidget> SRTSLobbyWidget::MakeRow(int32 Index)
{
	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
	[
		SNew(SBox).WidthOverride(24.f).HeightOverride(24.f)
		[
			SNew(SImage)
			.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.ColorAndOpacity(ARTSUnit::GetTeamColor(Index))
		]
	]
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
	[
		SNew(SBox).WidthOverride(220.f)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this, Index]()
			{
				const FRTSSeat* Seat = FindSeat(Controller, Index);
				return Seat && Seat->Type == ERTSSeatType::Human ? 1 : 0;
			})
			+ SWidgetSwitcher::Slot()
			[
				SNew(SComboBox<TSharedPtr<int32>>)
				.OptionsSource(&TypeOptions)
				.IsEnabled_Lambda([this]() { return Controller.IsValid() && Controller->IsHost(); })
				.OnGenerateWidget_Lambda([](TSharedPtr<int32> Option)
				{
					return SNew(STextBlock).Text(FText::FromString(GetTypeLabel(*Option)));
				})
				.OnSelectionChanged_Lambda([this, Index](TSharedPtr<int32> Option, ESelectInfo::Type SelectInfo)
				{
					if (!Option.IsValid() || !Controller.IsValid() || SelectInfo == ESelectInfo::Direct)
					{
						return;
					}
					const bool bAI = *Option >= FirstAIOption;
					const ERTSSeatType Type = bAI ? ERTSSeatType::AI : *Option == OpenOption ? ERTSSeatType::Open : ERTSSeatType::Closed;
					const ERTSAIDifficulty Difficulty = bAI ? static_cast<ERTSAIDifficulty>(*Option - FirstAIOption) : ERTSAIDifficulty::Normal;
					Controller->Server_SetSeatType(Index, Type, Difficulty);
				})
				[
					SNew(STextBlock).Text_Lambda([this, Index]()
					{
						const FRTSSeat* Seat = FindSeat(Controller, Index);
						return FText::FromString(Seat ? GetTypeLabel(GetTypeOption(*Seat)) : FString());
					})
				]
			]
			+ SWidgetSwitcher::Slot().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(GetFont(14)).Text_Lambda([this, Index]()
				{
					const FRTSSeat* Seat = FindSeat(Controller, Index);
					const bool bOwn = Seat && Controller.IsValid() && Seat->PlayerId == Controller->GetOwnPlayerId();
					return FText::FromString(Seat ? Seat->PlayerName + (bOwn ? TEXT(" (you)") : TEXT("")) : FString());
				})
			]
		]
	]
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(140.f)
		[
			SNew(SComboBox<TSharedPtr<int32>>)
			.OptionsSource(&SpotOptions)
			.IsEnabled_Lambda([this, Index]()
			{
				const FRTSSeat* Seat = FindSeat(Controller, Index);
				return Seat && RTSSkirmish::IsPlayed(*Seat)
					&& (Controller->IsHost() || (Seat->Type == ERTSSeatType::Human && Seat->PlayerId == Controller->GetOwnPlayerId()));
			})
			.OnGenerateWidget_Lambda([](TSharedPtr<int32> Option)
			{
				return SNew(STextBlock).Text(FText::FromString(GetSpotLabel(*Option)));
			})
			.OnSelectionChanged_Lambda([this, Index](TSharedPtr<int32> Option, ESelectInfo::Type SelectInfo)
			{
				if (Option.IsValid() && Controller.IsValid() && SelectInfo != ESelectInfo::Direct)
				{
					Controller->Server_SetStartSpot(Index, *Option);
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this, Index]()
				{
					const FRTSSeat* Seat = FindSeat(Controller, Index);
					return FText::FromString(Seat && RTSSkirmish::IsPlayed(*Seat) ? GetSpotLabel(Seat->StartSpot) : FString());
				})
			]
		]
	]
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
	[
		SNew(SBox).WidthOverride(90.f)
		[
			SNew(STextBlock)
			.Font(GetFont(12))
			.Text_Lambda([this, Index]()
			{
				const FRTSSeat* Seat = FindSeat(Controller, Index);
				const bool bHuman = Seat && Seat->Type == ERTSSeatType::Human;
				return FText::FromString(!bHuman ? FString() : Seat->bReady ? TEXT("Ready") : TEXT("Not ready"));
			})
			.ColorAndOpacity_Lambda([this, Index]()
			{
				const FRTSSeat* Seat = FindSeat(Controller, Index);
				return FSlateColor(Seat && Seat->bReady ? FLinearColor(0.3f, 0.9f, 0.3f) : FLinearColor(0.6f, 0.6f, 0.6f));
			})
		]
	];
}
