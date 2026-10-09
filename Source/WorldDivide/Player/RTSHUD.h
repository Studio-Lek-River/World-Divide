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
 * a scoreboard of every player's team colour and name, the build menu of the selected builder (or the debug build menu), and the production menu,
 * queue and rally point of a selected production building, a hint while attack-move is armed, a warning when an own
 * building or unit is attacked, every superweapon's countdown, the launch entry of a selected own superweapon and a hint
 * while targeting it, the target of a strike on its way, a warning when a superweapon is built or ready, and once the match is over
 * a pop-up with Victory or Defeat and, on the host, a restart button in place of the menus, and over everything the in-game
 * menu with Resume and Leave match while it is open, without any UI assets. It also shows the local player the UMG
 * URTSHUDWidget (the class set in URTSSettings), whose resource bar holds the credits and power, and keeps its top bar clear.
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

	/** Point, in viewport pixels, lies on the cancel button drawn last frame. */
	bool IsOnCancelButton(const FVector2D& Point) const;

	/** Point, in viewport pixels, lies on the restart button drawn last frame. */
	bool IsOnRestartButton(const FVector2D& Point) const;

	/** Point, in viewport pixels, lies on the in-game menu's Resume button drawn last frame. */
	bool IsOnResumeButton(const FVector2D& Point) const;

	/** Point, in viewport pixels, lies on the in-game menu's Leave match button drawn last frame. */
	bool IsOnLeaveButton(const FVector2D& Point) const;

	/** Point, in viewport pixels, lies on the selected superweapon's launch entry drawn last frame. */
	bool IsOnLaunchButton(const FVector2D& Point) const;

	/** A superweapon's countdown as the HUD shows it: "Ready", else minutes and seconds left, rounded up, such as "2:05". */
	static FString FormatCountdown(float Seconds);

	/** Index into the production menu of the entry drawn at Point last frame, or INDEX_NONE. */
	int32 GetProduceButtonAt(const FVector2D& Point) const;

	/** Index into the production queue of the entry drawn at Point last frame, or INDEX_NONE. */
	int32 GetQueueButtonAt(const FVector2D& Point) const;

	/** The pop-up's title for a finished match, "Victory!" or "Defeated!"; empty while it runs. */
	static FString GetMatchResultTitle(ERTSMatchResult Result);

	/** The line under the title: whose forces were eliminated; empty while the match runs. */
	static FString GetMatchResultMessage(ERTSMatchResult Result);

private:
	void DrawSelectionBox(const FVector2D& Start, const FVector2D& End);
	void DrawBrackets(float Left, float Top, float Right, float Bottom);
	void DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit);

	/** The health bar a selected unit shows, without the brackets. */
	void DrawUnitHealth(const ARTSPlayerController& Controller, const ARTSUnit& Unit);

	/** Above the circle, in viewport pixels, that the unit's selection radius covers on screen. */
	void DrawUnitHealthBar(const FVector2D& Center, float Radius, const ARTSUnit& Unit);

	/** That circle, at least 8 pixels across; false while the unit cannot be projected. */
	static bool GetUnitScreenCircle(const ARTSPlayerController& Controller, const ARTSUnit& Unit, FVector2D& OutCenter, float& OutRadius);
	void DrawBuildingMarker(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawCancelButton(const ARTSBuilding& Building);
	void DrawConstructionBar(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawBuildingHealth(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawBuildMenu(const ARTSPlayerController& Controller);

	/** The selected production building's menu (greyed while not available) and its queue, the front entry's progress filled in. */
	void DrawProduction(const ARTSPlayerController& Controller);

	/** A line from the building to its rally point, and a marker there. */
	void DrawRallyPoint(const ARTSPlayerController& Controller, const ARTSBuilding& Building);

	/** Creates HUDWidget once the owning controller is local and has its player. */
	void EnsureHUDWidget();

	/**
	 * Under the top bar, every player in the match by team: its colour and GetScoreboardLabel, greyed once eliminated. Returns
	 * the line below it.
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

	/** Bottom center, where the cancel button would be: that needs a selected building, attack-move selected units. */
	void DrawAttackMoveHint(const ARTSPlayerController& Controller);

	/**
	 * Once the match is over: the screen dimmed and a pop-up in the middle with the result, then the restart button on the
	 * host or a wait note on a client.
	 */
	void DrawMatchResult(const ARTSPlayerController& Controller);

	/** While the in-game menu is open: the screen dimmed and a pop-up in the middle with Resume and Leave match. */
	void DrawInGameMenu(const ARTSPlayerController& Controller);

	/** Green to red over a dark backdrop; Top is the bar's upper edge. */
	void DrawHealthBar(float Left, float Top, float Width, float HealthFraction);

	UPROPERTY(Transient)
	TObjectPtr<URTSHUDWidget> HUDWidget;

	/** Viewport pixels from the top where the canvas starts drawing, under the widget's top bar. */
	float TopInset = 0.f;

	/** Invalid while no cancel button is shown. */
	FBox2D CancelButton = FBox2D(ForceInit);

	/** Invalid while no restart button is shown. */
	FBox2D RestartButton = FBox2D(ForceInit);

	/** Invalid while the in-game menu is closed. */
	FBox2D ResumeButton = FBox2D(ForceInit);
	FBox2D LeaveButton = FBox2D(ForceInit);

	/** Invalid while no superweapon is selected. */
	FBox2D LaunchButton = FBox2D(ForceInit);

	/** Drawn last frame, by index into the production menu and queue; invalid boxes for entries not drawn. */
	TArray<FBox2D> ProduceButtons;
	TArray<FBox2D> QueueButtons;
};
