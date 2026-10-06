#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RTSHUD.generated.h"

class ARTSBuilding;
class ARTSPlayerController;
class ARTSUnit;

/**
 * Draws the drag box, the brackets plus health bar of selected units, construction progress of own buildings,
 * the health of the building under the cursor or selected, a cancel button for a selected own construction site,
 * the player's credits and power, and the build menu of the selected builder (or the debug build menu), without any UI assets.
 */
UCLASS()
class WORLDDIVIDE_API ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Point, in viewport pixels, lies on the cancel button drawn last frame. */
	bool IsOnCancelButton(const FVector2D& Point) const;

private:
	void DrawSelectionBox(const FVector2D& Start, const FVector2D& End);
	void DrawBrackets(float Left, float Top, float Right, float Bottom);
	void DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit);
	void DrawBuildingMarker(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawCancelButton(const ARTSBuilding& Building);
	void DrawConstructionBar(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawBuildingHealth(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawBuildMenu(const ARTSPlayerController& Controller);
	void DrawCredits(const ARTSPlayerController& Controller);

	/** Produced and consumed power under the credits, red while low; nothing for a faction without power. */
	void DrawPower(const ARTSPlayerController& Controller);

	/** Green to red over a dark backdrop; Top is the bar's upper edge. */
	void DrawHealthBar(float Left, float Top, float Width, float HealthFraction);

	/** Invalid while no cancel button is shown. */
	FBox2D CancelButton = FBox2D(ForceInit);
};
