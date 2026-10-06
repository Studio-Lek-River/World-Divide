#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RTSHUD.generated.h"

class ARTSPlayerController;
class ARTSUnit;

/** Draws the drag box and the brackets plus health bar of selected units, without any UI assets. */
UCLASS()
class WORLDDIVIDE_API ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawSelectionBox(const FVector2D& Start, const FVector2D& End);
	void DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit);
};
