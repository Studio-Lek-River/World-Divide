#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ARTSMainMenuController;
class SEditableTextBox;

/** The main menu: Skirmish, Host game, Join game with an address field, and Quit. */
class WORLDDIVIDE_API SRTSMainMenuWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRTSMainMenuWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARTSMainMenuController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Takes the focus the controller's UI-only input mode gives it. */
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	TSharedRef<SWidget> MakeButton(const TCHAR* Label, TFunction<void(ARTSMainMenuController&)> OnClicked);

	TWeakObjectPtr<ARTSMainMenuController> Controller;
	TSharedPtr<SEditableTextBox> AddressBox;
};
