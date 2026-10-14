#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "RTSHUDWidget.generated.h"

class UCanvasPanel;
class UCommonActivatableWidgetStack;
class URTSLayoutScreen;
class URTSLayoutWidget;
class URTSResourceBarWidget;
class USizeBox;

/**
 * The root of the match HUD, which ARTSHUD shows the local player. It owns no gameplay state, and has two layers:
 * - Game: the panels, built from UI/Layouts/HUD.layout.json (see docs/ui/DESIGNER_HANDBOOK.md). Without that file it
 *   builds a fixed layout in C++: the resource bar along the top and a bottom band with empty slots.
 * - Modal: a stack of screens over everything, each built from its own layout file: the match result once the match is
 *   over and the in-game menu while it is open. While one is shown the battlefield gets no clicks or hotkeys.
 * rts.UI.Reload (and F5 outside shipping builds) rereads the layout files and builds both layers anew.
 */
UCLASS()
class WORLDDIVIDE_API URTSHUDWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Heights and widths in HUD units, which the DPI scale turns into pixels. */
	static constexpr float TopBarHeight = 48.f;
	static constexpr float BottomBandHeight = 230.f;
	static constexpr float MinimapWidth = 230.f;
	static constexpr float CommandPanelWidth = 460.f;

	/** The layout files of the HUD and its modal screens. */
	static const TCHAR* HUDLayout;
	static const TCHAR* MatchResultLayout;
	static const TCHAR* InGameMenuLayout;

	/** The Game layer built from HUD.layout.json, or null when it is the C++ fallback. */
	URTSLayoutWidget* GetLayout() const;

	/** The fallback's resource bar, or null when the HUD is built from its layout file. */
	URTSResourceBarWidget* GetResourceBar() const { return ResourceBar; }

	/** The modal screens shown, bottom first. */
	TArray<URTSLayoutScreen*> GetModals() const;

	/**
	 * Shows the layout Name as a modal screen over the HUD, or its built-in fallback when the file is missing. With
	 * bBackHandler Esc closes it, except on the match result, where Esc opens the in-game menu over it.
	 */
	URTSLayoutScreen* PushModal(const FString& Name, bool bBackHandler);

	/** Closes every modal screen; the ones the match state asks for come back next frame. */
	void ClearModals();

	/**
	 * Point, in viewport pixels, lies on a hit-testable panel, as laid out last frame, or a modal screen is shown; a click
	 * there stays in the HUD. Nothing is before the first layout.
	 */
	bool IsOnPanel(const FVector2D& Point) const;

	/** Viewport pixels of the HUD layout's element Name, as laid out last frame; false without it or before the first layout. */
	bool GetElementRect(FName Name, FBox2D& OutRect) const;

	/**
	 * Viewport pixels the canvas HUD keeps clear: down from the top on the left (the commander bar and notifications) and
	 * on the right (the minimap and map buttons), and up from the bottom (the resource bar and the bottom panels).
	 */
	float GetLeftColumnBottom() const;
	float GetRightColumnBottom() const;
	float GetBottomFrameHeight() const;

	/** Builds the Game layer from the layout files again, and the modal screens with it. */
	void Rebuild();

	/** Rereads every layout file and rebuilds the HUD of each local player in World, selection and state kept. */
	static void ReloadAll(UWorld* World);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** The C++ layout of the Game layer, when HUD.layout.json is missing or unreadable. */
	UWidget* BuildDefaultLayout();

	/** Shows the modal screens the match asks for: the result once it is over, then the in-game menu while it is open. */
	void SyncModals();

	UPROPERTY(Transient)
	TObjectPtr<URTSLayoutScreen> GameLayer;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> ModalStack;

	UPROPERTY(Transient)
	TObjectPtr<URTSResourceBarWidget> ResourceBar;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> MinimapSlot;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> SelectionSlot;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> CommandSlot;

	/** The layouts of the modal screens SyncModals showed, bottom first, and those screens. */
	TArray<FString> ShownModals;
	TArray<TWeakObjectPtr<URTSLayoutScreen>> SyncedModals;
};
