#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RTSHUD.generated.h"

class ARTSBuilding;
class ARTSPlayerController;
class ARTSUnit;
enum class ERTSMatchResult : uint8;

/**
 * Draws the drag box, the brackets plus health bar of selected units, the health bar of the unit of any team under the
 * cursor, construction progress of own buildings,
 * the health of the building under the cursor or selected, a cancel button for a selected own construction site,
 * the player's credits and power, the build menu of the selected builder (or the debug build menu), and the production menu,
 * queue and rally point of a selected production building, a hint while attack-move is armed, and once the match is over
 * a pop-up with Victory or Defeat and, on the host, a restart button in place of the menus, without any UI assets.
 */
UCLASS()
class WORLDDIVIDE_API ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Point, in viewport pixels, lies on the cancel button drawn last frame. */
	bool IsOnCancelButton(const FVector2D& Point) const;

	/** Point, in viewport pixels, lies on the restart button drawn last frame. */
	bool IsOnRestartButton(const FVector2D& Point) const;

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
	void DrawCredits(const ARTSPlayerController& Controller);

	/** Produced and consumed power under the credits, red while low; nothing for a faction without power. */
	void DrawPower(const ARTSPlayerController& Controller);

	/** Bottom center, where the cancel button would be: that needs a selected building, attack-move selected units. */
	void DrawAttackMoveHint(const ARTSPlayerController& Controller);

	/**
	 * Once the match is over: the screen dimmed and a pop-up in the middle with the result, then the restart button on the
	 * host or a wait note on a client.
	 */
	void DrawMatchResult(const ARTSPlayerController& Controller);

	/** Green to red over a dark backdrop; Top is the bar's upper edge. */
	void DrawHealthBar(float Left, float Top, float Width, float HealthFraction);

	/** Invalid while no cancel button is shown. */
	FBox2D CancelButton = FBox2D(ForceInit);

	/** Invalid while no restart button is shown. */
	FBox2D RestartButton = FBox2D(ForceInit);

	/** Drawn last frame, by index into the production menu and queue; invalid boxes for entries not drawn. */
	TArray<FBox2D> ProduceButtons;
	TArray<FBox2D> QueueButtons;
};
