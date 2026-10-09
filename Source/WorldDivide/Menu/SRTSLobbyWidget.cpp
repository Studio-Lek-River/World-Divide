#include "Menu/SRTSLobbyWidget.h"

#include "Core/RTSSettings.h"
#include "Core/RTSSkirmishSeats.h"
#include "Data/RTSMapData.h"
#include "Engine/Texture2D.h"
#include "Menu/RTSLobbyGameState.h"
#include "Menu/RTSLobbyPlayerController.h"
#include "Styling/CoreStyle.h"
#include "Units/RTSUnit.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr int32 OpenOption = 0;
	constexpr int32 ClosedOption = 1;
	constexpr int32 FirstAIOption = 2;
	constexpr int32 TypeOptionCount = FirstAIOption + 3;

	constexpr float PreviewSize = 256.f;
	constexpr float SpotMarkerSize = 22.f;

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
	return RTSSkirmish::GetAILabel(static_cast<ERTSAIDifficulty>(TypeOption - FirstAIOption));
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

UTexture2D* SRTSLobbyWidget::GetMapPreview(int32 MapIndex)
{
	const URTSMapData* Map = ARTSLobbyGameState::LoadMap(MapIndex);
	return Map ? Cast<UTexture2D>(Map->Preview.TryLoad()) : nullptr;
}

UTexture2D* SRTSLobbyWidget::GetShownPreview() const
{
	return Controller.IsValid() ? GetMapPreview(Controller->GetMapIndex()) : nullptr;
}

void SRTSLobbyWidget::SelectSeat(int32 Index)
{
	const FRTSSeat* Seat = FindSeat(Controller, Index);
	if (Seat && RTSSkirmish::IsPlayed(*Seat) && Controller->IsHost())
	{
		SelectedSeat = Index;
	}
}

int32 SRTSLobbyWidget::GetMovedSeat() const
{
	if (!Controller.IsValid())
	{
		return INDEX_NONE;
	}
	const FRTSSeat* Selected = FindSeat(Controller, SelectedSeat);
	if (Selected && RTSSkirmish::IsPlayed(*Selected) && Controller->IsHost())
	{
		return SelectedSeat;
	}
	const int32 OwnPlayerId = Controller->GetOwnPlayerId();
	return Controller->GetSeats().IndexOfByPredicate([OwnPlayerId](const FRTSSeat& Seat)
	{
		return Seat.Type == ERTSSeatType::Human && Seat.PlayerId == OwnPlayerId;
	});
}

bool SRTSLobbyWidget::CanPickSpot(int32 Spot) const
{
	const int32 Moved = GetMovedSeat();
	const FRTSSeat* Seat = FindSeat(Controller, Moved);
	const TConstArrayView<FRTSSeat> Seats = Seat ? Controller->GetSeats() : TConstArrayView<FRTSSeat>();
	return Seat && Seat->StartSpot != Spot && Spot != INDEX_NONE
		&& RTSSkirmish::CanChangeStartSpot(Seats, Moved, Controller->IsHost(), Controller->GetOwnPlayerId(), Spot, Seats.Num());
}

bool SRTSLobbyWidget::ClickSpot(int32 Spot)
{
	if (!CanPickSpot(Spot))
	{
		return false;
	}
	Controller->Server_SetStartSpot(GetMovedSeat(), Spot);
	return true;
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
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
				[
					SAssignNew(Rows, SVerticalBox)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(24.f, 4.f, 0.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(PreviewSize)
					.HeightOverride(PreviewSize)
					.Visibility_Lambda([this]() { return GetShownPreview() ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SImage).Image_Lambda([this]()
							{
								UTexture2D* Preview = GetShownPreview();
								if (PreviewTexture.Get() != Preview)
								{
									PreviewTexture.Reset(Preview);
									PreviewBrush.SetResourceObject(Preview);
									PreviewBrush.ImageSize = FVector2D(PreviewSize);
								}
								return &PreviewBrush;
							})
						]
						+ SOverlay::Slot()
						[
							SAssignNew(SpotMarkers, SConstraintCanvas)
						]
					]
				]
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
					return FText::FromString(Own && !Own->bReady ? TEXT("Pick your base on the map or in the list, then press Ready.")
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
	const int32 MapIndex = Controller.IsValid() ? Controller->GetMapIndex() : INDEX_NONE;
	if (MapIndex != BuiltMapIndex)
	{
		RebuildSpotMarkers(MapIndex);
	}
}

void SRTSLobbyWidget::RebuildSpotMarkers(int32 MapIndex)
{
	BuiltMapIndex = MapIndex;
	SpotMarkers->ClearChildren();
	const URTSMapData* Map = ARTSLobbyGameState::LoadMap(MapIndex);
	if (!Map)
	{
		return;
	}
	for (int32 Spot = 0; Spot < Map->StartSpotPositions.Num(); ++Spot)
	{
		const TSharedRef<SButton> Marker = SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(0.f)
			.Cursor_Lambda([this, Spot]() { return TOptional<EMouseCursor::Type>(CanPickSpot(Spot) ? EMouseCursor::Hand : EMouseCursor::Default); })
			.OnClicked_Lambda([this, Spot]()
			{
				ClickSpot(Spot);
				return FReply::Handled();
			});
		Marker->SetContent(
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this, Spot, WeakMarker = TWeakPtr<SButton>(Marker)]()
			{
				// A spot the player may pick lights up under the mouse; a taken one keeps its seat's colour.
				const TSharedPtr<SButton> Hovered = WeakMarker.Pin();
				return Hovered.IsValid() && Hovered->IsHovered() && CanPickSpot(Spot) ? FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f))
					: GetSpotColor(Spot);
			})
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(0.f)
			[
				SNew(STextBlock).Font(GetFont(11)).Text(FText::AsNumber(Spot + 1))
			]);
		const FVector2D Position = Map->StartSpotPositions[Spot];
		SpotMarkers->AddSlot()
		.Anchors(FAnchors(Position.X, Position.Y))
		.Alignment(FVector2D(0.5f, 0.5f))
		.Offset(FMargin(0.f, 0.f, SpotMarkerSize, SpotMarkerSize))
		[
			Marker
		];
	}
}

FSlateColor SRTSLobbyWidget::GetSpotColor(int32 Spot) const
{
	const TConstArrayView<FRTSSeat> Seats = Controller.IsValid() ? Controller->GetSeats() : TConstArrayView<FRTSSeat>();
	for (int32 Index = 0; Index < Seats.Num(); ++Index)
	{
		if (Seats[Index].StartSpot == Spot && RTSSkirmish::IsPlayed(Seats[Index]))
		{
			return ARTSUnit::GetTeamColor(Index);
		}
	}
	return FLinearColor(0.35f, 0.35f, 0.35f);
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
			// The host picks the seat a click on the preview moves; it is outlined.
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(0.f)
			.OnClicked_Lambda([this, Index]()
			{
				SelectSeat(Index);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([this, Index]()
				{
					const bool bOutlined = Controller.IsValid() && Controller->IsHost() && GetMovedSeat() == Index;
					return FSlateColor(bOutlined ? FLinearColor::White : ARTSUnit::GetTeamColor(Index));
				})
				.Padding(2.f)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(ARTSUnit::GetTeamColor(Index))
				]
			]
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
	]
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
	[
		SNew(SBox).WidthOverride(80.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(FText::FromString(TEXT("Remove")))
			.Visibility_Lambda([this, Index]()
			{
				const bool bRemovable = Controller.IsValid()
					&& RTSSkirmish::CanRemovePlayer(Controller->GetSeats(), Index, Controller->IsHost(), Controller->GetOwnPlayerId());
				return bRemovable ? EVisibility::Visible : EVisibility::Hidden;
			})
			.OnClicked_Lambda([this, Index]()
			{
				if (Controller.IsValid())
				{
					Controller->Server_RemovePlayer(Index);
				}
				return FReply::Handled();
			})
		]
	];
}
