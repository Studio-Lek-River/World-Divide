#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RTSHUD.generated.h"

class ARTSBuilding;
class ARTSPlayerController;
class ARTSUnit;

/**
 * Draws the drag box, the brackets plus health bar of selected units, construction progress of own buildings,
 * the health of the building under the cursor, the player's credits and the debug build menu, without any UI assets.
 */
UCLASS()
class WORLDDIVIDE_API ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawSelectionBox(const FVector2D& Start, const FVector2D& End);
	void DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit);
	void DrawConstructionBar(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawBuildingHealth(const ARTSPlayerController& Controller, const ARTSBuilding& Building);
	void DrawBuildMenu(const ARTSPlayerController& Controller);
	void DrawCredits(const ARTSPlayerController& Controller);

	/** Green to red over a dark backdrop; Top is the bar's upper edge. */
	void DrawHealthBar(float Left, float Top, float Width, float HealthFraction);
};
