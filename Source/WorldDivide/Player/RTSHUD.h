#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RTSHUD.generated.h"

class ARTSBuilding;
class ARTSPlayerController;
class ARTSUnit;
class URTSHUDWidget;
enum class ERTSMatchResult : uint8;

/**
 * Draws the drag box, the brackets plus health bar of selected units, the health bar of the unit of any team under the
 * cursor, construction progress of own buildings,
 * the health of the building under the cursor or selected, a cancel button for a selected own construction site,
 * a scoreboard of every player's team colour and name, a hint while placing a building, the rally point of a selected
 * production building (its commands are in the command panel, its queue in the selection panel), a hint while attack-move is armed, a warning when an own
 * building or unit is attacked, every superweapon's countdown, the launch entry of a selected own superweapon and a hint
 * while targeting it, the target of a strike on its way and a warning when a superweapon is built or ready, without any
 * UI assets; once the match is over it hides the menus. It also shows the local player the URTSHUDWidget (the class set
 * in URTSSettings), which holds the HUD frame, the match result and the in-game menu, and keeps clear of its panels.
 */
UCLASS()
class WORLDDIVIDE_API ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	ARTSHUD();

	virtual void Tick(float DeltaSeconds) override;
	virtual void DrawHUD() override;

	/** The UMG HUD of the local player, or null until its controller is attached to it. */
	URTSHUDWidget* GetHUDWidget() const { return HUDWidget; }

	/** Point, in viewport pixels, lies on the selected superweapon's launch entry drawn last frame. */
	bool IsOnLaunchButton(const FVector2D& Point) const;

	/** A superweapon's countdown as the HUD shows it: "Ready", else minutes and seconds left, rounded up, such as "2:05". */
	static FString FormatCountdown(float Seconds);

	/** The result screen's title for a finished match, "Victory!" or "Defeated!"; empty while it runs. */
	static FString GetMatchResultTitle(ERTSMatchResult Result);

	/** The line under the title: whose forces were eliminated; empty while the match runs. */
	static FString GetMatchResultMessage(ERTSMatchResult Result);

	/**
	 * The screen rect, in viewport pixels, of the unit's selection bounds as Project maps world points to the screen: the
	 * eight corners' bounds, at least MinUnitRectSize across. False while a corner cannot be projected.
	 */
	static bool GetUnitScreenRect(const ARTSUnit& Unit, TFunctionRef<bool(const FVector&, FVector2D&)> Project, FBox2D& OutRect);

	/** As above, through the controller's view. */
	static bool GetUnitScreenRect(const ARTSPlayerController& Controller, const ARTSUnit& Unit, FBox2D& OutRect);

	/** Brackets never shrink below this many pixels, so a far unit stays visible. */
	static constexpr float MinUnitRectSize = 16.f;

private:
	void DrawSelectionBox(const FVector2D& Start, const FVector2D& End);
	void DrawBrackets(float Left, float Top, float Right, float Bottom);
	void DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit);

	/** The health bar a selected unit shows, without the brackets. */
	void DrawUnitHealth(const ARTSPlayerController& Controller, const ARTSUnit& Unit);

	/** Above the unit's screen rect (GetUnitScreenRect), as wide as it. */
	void DrawUnitHealthBar(const FBox2D& Rect, const ARTSUnit& Unit);

	void DrawBuildingMarker(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawConstructionBar(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawBuildingHealth(const ARTSPlayerController& Controller, const ARTSBuilding& Building);

	/** While placing a building: how to build, turn and cancel it. */
	void DrawPlacementHint(const ARTSPlayerController& Controller);

	/** A line from the building to its rally point, and a marker there. */
	void DrawRallyPoint(const ARTSPlayerController& Controller, const ARTSBuilding& Building);

	/** Creates HUDWidget once the owning controller is local and has its player. */
	void EnsureHUDWidget();

	/**
	 * Under the minimap and map buttons, every player in the match by team: its colour and GetScoreboardLabel, greyed once
	 * eliminated. Returns the line below it.
	 */
	float DrawScoreboard();

	/**
	 * From Y, every superweapon of every team: its team colour and countdown, noting a charge held by low power; and a
	 * pulsing ring where a strike is on its way.
	 */
	void DrawSuperweapons(const ARTSPlayerController& Controller, float Y);

	/** The selected own superweapon's launch entry, greyed while it charges, and a hint at the bottom while targeting. */
	void DrawSuperweaponMenu(const ARTSPlayerController& Controller);

	/** For a few seconds after a superweapon warning: a message at the top center, under the attack warning's. */
	void DrawSuperweaponAlert(const ARTSPlayerController& Controller);

	/**
	 * For a few seconds after a warning: a message at the top center, the base's in red over the units' in amber, and a
	 * pulsing ring of that color on each attacked building or unit.
	 */
	void DrawAttackAlert(const ARTSPlayerController& Controller);

	/** For a moment after a refused move order: a message under the warnings and a ring on the spot the order named. */
	void DrawOrderRefusal(const ARTSPlayerController& Controller);

	/**
	 * Bottom center above the bottom panels, where the cancel button would be: that needs a selected building, attack-move
	 * selected units.
	 */
	void DrawAttackMoveHint(const ARTSPlayerController& Controller);

	/** Green to red over a dark backdrop; Top is the bar's upper edge. */
	void DrawHealthBar(float Left, float Top, float Width, float HealthFraction);

	UPROPERTY(Transient)
	TObjectPtr<URTSHUDWidget> HUDWidget;

	/** Viewport pixels the canvas keeps clear of the widget's panels: the top of the left and right menus, and the bottom hints. */
	float LeftInset = 0.f;
	float RightInset = 0.f;
	float BottomInset = 0.f;

	/** Invalid while no superweapon is selected. */
	FBox2D LaunchButton = FBox2D(ForceInit);
};
