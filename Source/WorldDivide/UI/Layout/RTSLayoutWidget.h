#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "CommonUserWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "UI/Layout/RTSLayoutElement.h"
#include "UI/Layout/RTSUIBinding.h"
#include "RTSLayoutWidget.generated.h"

class FJsonObject;
class UCanvasPanel;
class UImage;
class UPanelWidget;
class URTSLayoutWidget;
class URTSUIStyleSet;

/**
 * A layout Text: a Common UI text with the style of its font and colour, plus the fontSize or #RRGGBB colour on top. With
 * maxLines, what does not fit the lines its size box allows ends in an ellipsis.
 */
UCLASS()
class WORLDDIVIDE_API URTSLayoutText : public UCommonTextBlock
{
	GENERATED_BODY()

public:
	void SetOverrides(int32 InFontSize, TOptional<FLinearColor> InColor);
	void SetMaxLines(int32 InMaxLines);

	/** The height of maxLines lines in the text's font, with its margin. */
	float GetMaxHeight() const;

	virtual void SynchronizeProperties() override;

private:
	int32 FontSizeOverride = 0;
	int32 MaxLines = 0;
	TOptional<FLinearColor> ColorOverride;
};

/** A layout Button: the button style of its sprite, its children on a canvas inside, and its action sent on click. */
UCLASS()
class WORLDDIVIDE_API URTSLayoutButton : public UCommonButtonBase
{
	GENERATED_BODY()

public:
	/** The canvas the button's children are placed on, inside its padding. */
	UCanvasPanel* GetContent() const { return Content; }
	void SetContentPadding(const FMargin& Padding);

	void SetAction(URTSLayoutWidget* InLayout, FName InAction, const FRTSUIContext& InContext);

protected:
	virtual UCommonButtonInternalBase* ConstructInternalButton() override;
	virtual void NativeOnClicked() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Content;

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> PaddingBorder;

	TWeakObjectPtr<URTSLayoutWidget> Layout;
	FName Action;
	FRTSUIContext Context;
};

/**
 * One layout file built into Common UI widgets (docs/ui/DESIGNER_HANDBOOK.md section 2): Panel, HBox, VBox, Grid, Image,
 * Text, Button, ProgressBar and Slot, styled from the style sets #103 generates. Every frame it pulls the values of its
 * binding keys from the binding sources and updates texts, visibleWhen and bind. A key or action no source has logs one
 * warning, and its element still shows.
 */
UCLASS()
class WORLDDIVIDE_API URTSLayoutWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Builds Root, read from File, replacing what was built before. Sprites, colours and fonts resolve in Chain, keys and
	 * actions in Sources.
	 */
	void Build(const TSharedRef<FRTSLayoutElement>& Root, const FString& File, TArray<const URTSUIStyleSet*> InChain,
		TArray<const IRTSUIBindingSource*> InSources);

	/**
	 * Builds the layout Name from the layout files with the game's binding sources and the local player's style chain.
	 * False, with a warning logged, when the file is missing or does not parse; its other errors are logged.
	 */
	bool BuildFromFile(const FString& Name);

	/** Parses Object as File and builds it. False when it does not parse at all. */
	bool BuildFromJson(const FJsonObject& Object, const FString& File, TArray<const URTSUIStyleSet*> InChain,
		TArray<const IRTSUIBindingSource*> InSources);

	/** Pulls every binding once; NativeTick does this every frame. */
	void UpdateBindings();

	/** Plays the sprite sheets DeltaSeconds on; NativeTick does this every frame. */
	void Animate(float DeltaSeconds);

	/** Point, in absolute (desktop) pixels, lies on a shown element with hitTest, as laid out last frame. */
	bool IsOnPanel(const FVector2D& AbsolutePoint) const;

	/** The widget of the first element called Name, or null. */
	UWidget* FindElement(FName Name) const;

	/** The copies a repeat container called Name holds, in order. */
	TArray<UWidget*> GetRepeatItems(FName Name) const;

	const FString& GetFile() const { return File; }
	const FRTSLayoutElement* GetRoot() const { return Root.Get(); }

	/** Sends Action to the source that declares it; logs a warning when none does. */
	void RunAction(FName Action, const FRTSUIContext& Context);

	/** The style chain of the local player's faction, or Common and Neutral without one. */
	static TArray<const URTSUIStyleSet*> GetDefaultChain(const UObject* WorldContextObject);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	struct FBoundElement
	{
		const FRTSLayoutElement* Element = nullptr;
		/** What sits in the parent: the element's widget, or a size box or border around it. */
		TWeakObjectPtr<UWidget> Outer;
		TWeakObjectPtr<UWidget> Widget;
		FRTSUIContext Context;
		/** The repeat container this element is a copy of the template of. */
		FName RepeatOf;
		/** The visibility while visibleWhen holds. */
		ESlateVisibility ShownVisibility = ESlateVisibility::SelfHitTestInvisible;
		/** A Grid cell keeps its place while hidden: the grid's cells are fixed. */
		bool bKeepSpace = false;
		FString LastColor;
		FString LastSprite;
		/** The colour Team last resolved to, for color and fillColor. */
		FLinearColor LastTeamColor = FLinearColor::Transparent;
		/** The Image's sprite sheet, 0 frames for a still image, how long it has played and the frame it shows. */
		int32 Frames = 0;
		FVector2f FrameSize = FVector2f::ZeroVector;
		FVector2f SheetSize = FVector2f::ZeroVector;
		float Fps = 0.f;
		double SheetTime = 0.0;
		int32 ShownFrame = 0;
	};

	/**
	 * Builds Element and adds it to Panel, placed as Parent places its children: by anchor on a canvas (Panel, Button, Slot
	 * or none), in order in an HBox or VBox, or as cell Index of a Grid.
	 */
	void BuildInto(UPanelWidget& Panel, const FRTSLayoutElement* Parent, const FRTSLayoutElement& Element, int32 Index,
		const FRTSUIContext& Context, int32 Depth, FName RepeatOf);
	void BuildChildren(const FRTSLayoutElement& Element, UPanelWidget& Panel, const FRTSUIContext& Context, int32 Depth);
	UWidget* BuildSlot(const FRTSLayoutElement& Element, const FRTSUIContext& Context, int32 Depth);

	/** The text style of the element's font in Color: a theme colour, or the Text colour with a #RRGGBB or Team on top. */
	void ApplyTextStyle(URTSLayoutText& Text, const FRTSLayoutElement& Element, const FString& Color, const FRTSUIContext& Context);
	/** A theme colour, #RRGGBB or Team: the item's item.teamColor inside a template, else the local player's team colour. */
	FLinearColor ResolveColor(const FString& Color, const FRTSUIContext& Context);
	FLinearColor GetTeamColor(const FRTSUIContext& Context);
	/** Colours a Text, an Image, a Panel or a ProgressBar's fill. */
	void ApplyColor(UWidget& Widget, const FRTSLayoutElement& Element, const FString& Color, const FRTSUIContext& Context);
	/** Shows Sprite on Image, its first frame for a sprite sheet. */
	void SetImageSprite(UImage& Image, FBoundElement& Entry, FName Sprite) const;
	static void ShowFrame(UImage& Image, const FBoundElement& Entry, int32 Frame);
	FString FormatText(const FRTSLayoutElement& Element, const FRTSUIContext& Context);
	int32 GetListCount(FName List, const FRTSUIContext& Context);
	bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue);
	bool IsShown(const FRTSLayoutCondition& Condition, const FRTSUIContext& Context);
	FRTSUIContext MakeContext(FName List, int32 Index) const;
	void Warn(FName Key, const FString& Message);
	void UpdateElement(FBoundElement& Entry);

	UFUNCTION()
	FEventReply HandlePanelMouse(FGeometry Geometry, const FPointerEvent& MouseEvent);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> LayoutRoot;

	TSharedPtr<FRTSLayoutElement> Root;
	/** The roots of the layouts its Slots placed, which its bound elements point into. */
	TArray<TSharedPtr<FRTSLayoutElement>> SlotRoots;
	FString File;
	TArray<const URTSUIStyleSet*> Chain;
	TArray<const IRTSUIBindingSource*> Sources;
	TMap<FName, const IRTSUIBindingSource*> KeySources;
	TMap<FName, const IRTSUIBindingSource*> ListSources;
	TMap<FName, const IRTSUIBindingSource*> ActionSources;
	TArray<FBoundElement> Bound;
	/** A repeat without count by its list, with the copies built; a different count rebuilds. */
	TMap<FName, int32> DynamicRepeats;
	TSet<FName> Warned;
};

namespace RTSLayout
{
	/** The engine panels a Slot may place by widget: Minimap, Commands, Upgrades, Notifications, Scoreboard, Tooltip. */
	WORLDDIVIDE_API const TArray<FName>& GetPanelNames();

	/**
	 * A layout fontSize and a theme font size are pixels of the em at the 1080p reference (DESIGNER_HANDBOOK.md); Slate
	 * reads font sizes as points at 96 DPI. setup_testmap.py's FONT_POINTS_PER_PIXEL is the same factor.
	 */
	constexpr float FontPointsPerPixel = 0.75f;
}
