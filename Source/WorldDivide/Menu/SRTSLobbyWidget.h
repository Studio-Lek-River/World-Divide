#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ARTSLobbyPlayerController;
class SVerticalBox;

/**
 * The lobby screen: the map (a combo of URTSSettings::SkirmishMaps for the host, its name for the others), then one row per seat with its team colour, who plays it (a combo of Open, Closed and the AI difficulties for
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

private:
	/** The seats arrive by replication: the rows are built once their number is known. */
	void RebuildRows(int32 SeatCount);
	TSharedRef<SWidget> MakeRow(int32 Index);

	TWeakObjectPtr<ARTSLobbyPlayerController> Controller;
	TSharedPtr<SVerticalBox> Rows;
	int32 BuiltSeats = INDEX_NONE;

	/** Indices into URTSSettings::SkirmishMaps. */
	TArray<TSharedPtr<int32>> MapOptions;

	/** Indices into the type options (Open, Closed, AI Easy, AI Normal, AI Hard) and the spots (INDEX_NONE for Random). */
	TArray<TSharedPtr<int32>> TypeOptions;
	TArray<TSharedPtr<int32>> SpotOptions;
};
