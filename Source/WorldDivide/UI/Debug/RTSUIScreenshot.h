#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class URTSHUDWidget;
class URTSUIStyleSet;

/**
 * rts.UI.Screenshot [Panel] [Faction]: pictures of the HUD as the game draws it, for the design rounds (docs/ui/AI_TEAM.md).
 * It puts the match in the UI test state (RTSUITestState) and, for each faction look (or only Faction) and each of
 * GetResolutions, saves the screen to Saved/UIShots/<Panel or HUD>/<Faction>_<Width>x<Height>.png; with Panel, the layout
 * element of that name, also <Faction>_<Width>x<Height>_crop.png. The arguments go in either order: a theme name is the
 * faction, anything else the panel. In PIE the resolution cannot change, so it saves the viewport's size only.
 * With -RTSUIShotsQuit on the command line (ue.ps1 -Task uishots) the game quits when done, with exit code 1 on failure.
 */
namespace RTSUIScreenshot
{
	/** 16:9 at two sizes, and 21:9. */
	TConstArrayView<FIntPoint> GetResolutions();

	/** The faction looks: one per theme file in UI/Themes but Neutral, the fallback, sorted. */
	TArray<FString> GetFactionThemes();

	/** The style set setup generated from Theme's file, DA_UIStyle_<Theme>, or null. */
	const URTSUIStyleSet* LoadThemeStyle(const FString& Theme);

	/** Where a shot goes: Saved/UIShots/<Panel or HUD>/<Faction>_<Width>x<Height>[_crop].png. */
	FString GetShotPath(const FString& Panel, const FString& Faction, const FIntPoint& Size, bool bCrop);

	/**
	 * The pixels of the HUD layout element named Panel, as laid out last frame, within a screen of ScreenSize; false when
	 * the HUD has no layout, no such element, or it covers no pixel.
	 */
	bool GetPanelRect(const URTSHUDWidget& HUD, FName Panel, const FIntPoint& ScreenSize, FIntRect& OutRect);
}

#endif
