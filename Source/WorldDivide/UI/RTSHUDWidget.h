#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSHUDWidget.generated.h"

class URTSResourceBarWidget;
class USizeBox;

/**
 * The root of the match HUD, which ARTSHUD shows the local player. It only lays out the panels and owns no gameplay state:
 * the resource bar along the top, the battlefield in the centre, and a bottom band with empty slots for the minimap (left
 * corner), the selection (centre) and the commands and production (right), filled by later panels. Without a Widget
 * Blueprint it builds that layout itself; a subclass may supply its own for art.
 */
UCLASS()
class WORLDDIVIDE_API URTSHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Heights and widths in HUD units, which the DPI scale turns into pixels. */
	static constexpr float TopBarHeight = 48.f;
	static constexpr float BottomBandHeight = 230.f;
	static constexpr float MinimapWidth = 230.f;
	static constexpr float CommandPanelWidth = 460.f;

	URTSResourceBarWidget* GetResourceBar() const { return ResourceBar; }

	/**
	 * Point, in viewport pixels, lies on a hit-testable panel, as laid out last frame; a click there stays in the HUD. Nothing
	 * is before the first layout.
	 */
	bool IsOnPanel(const FVector2D& Point) const;

	/** Viewport pixels the top bar covers, which the canvas HUD keeps clear. */
	float GetTopBandHeight() const;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<URTSResourceBarWidget> ResourceBar;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> MinimapSlot;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SelectionSlot;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> CommandSlot;

private:
	void BuildDefaultLayout();
};
