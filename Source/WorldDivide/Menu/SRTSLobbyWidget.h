#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class ARTSLobbyPlayerController;
class SConstraintCanvas;
class SVerticalBox;
class UTexture2D;

/**
 * The lobby screen: the map (a combo of URTSSettings::SkirmishMaps for the host, its name for the others), its preview
 * with each start spot numbered as its base and coloured by the team playing from it, where a click on a free spot moves the
 * player's own seat there (the host first picks the seat to move by clicking its team colour), then one row per seat with its
 * team colour, who plays it (a combo of Open, Closed and the AI difficulties for
 * the host, or the name of the human holding it), its start spot (Random or a base), whether its human is ready and, for the
 * host, Remove next to every other player, then
 * Back, Ready for a joined player and Start for the host.
 * Everything reads the replicated seats each frame; changes go to the server through the controller.
 */
class WORLDDIVIDE_API SRTSLobbyWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRTSLobbyWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARTSLobbyPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Takes the focus the controller's UI-only input mode gives it. */
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** The label the seat's type combo shows: Open, Closed or "AI (<difficulty>)". */
	static FString GetTypeLabel(int32 TypeOption);

	/** The label the map combo shows for map MapIndex of URTSSettings::SkirmishMaps: its display name. */
	static FString GetMapLabel(int32 MapIndex);

	/** The label the start spot combo shows for Spot: Random, or "Base <Spot + 1>". */
	static FString GetSpotLabel(int32 Spot);

	/** The preview of map MapIndex of URTSSettings::SkirmishMaps, or null when it has none. */
	static UTexture2D* GetMapPreview(int32 MapIndex);

	/** The preview shown: that of the map the lobby has chosen. */
	UTexture2D* GetShownPreview() const;

	/** Host only: a click on the preview moves seat Index from now on, when it is played. */
	void SelectSeat(int32 Index);

	/** The seat a click on the preview moves: the host's selected seat while it is played, otherwise the player's own. */
	int32 GetMovedSeat() const;

	/** Whether a click on Spot would move GetMovedSeat there: the spot is free and the server would allow it. */
	bool CanPickSpot(int32 Spot) const;

	/** A click on Spot of the preview: asks the server to move GetMovedSeat there when CanPickSpot; returns whether it asked. */
	bool ClickSpot(int32 Spot);

private:
	/** The seats arrive by replication: the rows are built once their number is known. */
	void RebuildRows(int32 SeatCount);
	TSharedRef<SWidget> MakeRow(int32 Index);

	/** One numbered marker per start spot of the chosen map, where its PlayerStart is on the preview. */
	void RebuildSpotMarkers(int32 MapIndex);

	/** The colour of the team whose seat plays from Spot, grey when no seat does. */
	FSlateColor GetSpotColor(int32 Spot) const;

	TWeakObjectPtr<ARTSLobbyPlayerController> Controller;
	TSharedPtr<SVerticalBox> Rows;
	int32 BuiltSeats = INDEX_NONE;
	/** The host's choice of seat to move on the preview, or INDEX_NONE for its own. */
	int32 SelectedSeat = INDEX_NONE;

	TSharedPtr<SConstraintCanvas> SpotMarkers;
	int32 BuiltMapIndex = INDEX_NONE;
	FSlateBrush PreviewBrush;
	/** Keeps the shown preview loaded; the brush does not. */
	TStrongObjectPtr<UTexture2D> PreviewTexture;

	/** Indices into URTSSettings::SkirmishMaps. */
	TArray<TSharedPtr<int32>> MapOptions;

	/** Indices into the type options (Open, Closed, AI Easy, AI Normal, AI Hard) and the spots (INDEX_NONE for Random). */
	TArray<TSharedPtr<int32>> TypeOptions;
	TArray<TSharedPtr<int32>> SpotOptions;
};
