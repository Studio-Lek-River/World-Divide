#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "RTSLayoutScreen.generated.h"

class UOverlay;
class URTSLayoutWidget;

/**
 * A layer of the HUD as a Common UI activatable widget. A modal screen, such as the in-game menu, takes Menu input: while it
 * is active the battlefield gets no clicks or hotkeys, and as a back handler Esc closes it. The Game layer under it takes
 * Game input, so camera, selection and hotkeys stay with the player controller.
 */
UCLASS()
class WORLDDIVIDE_API URTSLayoutScreen : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/**
	 * Before it is added: Menu or Game input, and whether it handles Esc. Esc calls OnBack, then closes the screen when
	 * bInCloseOnBack.
	 */
	void Configure(bool bInMenuInput, bool bInBackHandler, FSimpleDelegate InOnBack = FSimpleDelegate(), bool bInCloseOnBack = true);

	/** Builds the layout Name over the whole screen, in place of what it showed. Null when the file is missing. */
	URTSLayoutWidget* ShowLayout(const FString& Name);

	/** Shows Widget over the whole screen instead, such as a tree built in C++. */
	void ShowWidget(UWidget* Widget);

	URTSLayoutWidget* GetLayout() const { return Layout; }
	const FString& GetLayoutName() const { return LayoutName; }

protected:
	virtual bool Initialize() override;
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
	virtual bool NativeOnHandleBackAction() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UOverlay> Content;

	UPROPERTY(Transient)
	TObjectPtr<URTSLayoutWidget> Layout;

	FString LayoutName;
	bool bMenuInput = true;
	bool bCloseOnBack = true;
	FSimpleDelegate OnBack;
};
