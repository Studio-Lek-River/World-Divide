#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSResourceBarWidget.generated.h"

class UTextBlock;

/**
 * The bar along the top of the match HUD: the local player's credits and its power, produced over consumed and red while
 * low, read from its player state every frame; the power is hidden for a faction without power. It takes every click on
 * it, so none reaches the world. Without a Widget Blueprint it builds its own layout; a subclass may supply the texts.
 */
UCLASS()
class WORLDDIVIDE_API URTSResourceBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The credits as the bar shows them, such as "Credits: 10000". */
	static FString FormatCredits(int32 Credits);

	/** The power as the bar shows it, such as "Power: 5 / 3", with " (low)" while the draw exceeds the output. */
	static FString FormatPower(int32 Produced, int32 Consumed, bool bLowPower);

	/** Cyan, or red while low on power. */
	static FLinearColor GetPowerColor(bool bLowPower);

	/** Reads the owning player's state into the texts; NativeTick does this every frame. */
	void Refresh();

	UTextBlock* GetCreditsText() const { return CreditsText; }
	UTextBlock* GetPowerText() const { return PowerText; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CreditsText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PowerText;

private:
	void BuildDefaultLayout();
};
