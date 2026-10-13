#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"

class FJsonObject;

/** The element types of docs/ui/DESIGNER_HANDBOOK.md section 2. */
enum class ERTSLayoutType : uint8
{
	Panel,
	HBox,
	VBox,
	Grid,
	Image,
	Text,
	Button,
	ProgressBar,
	Slot,
};

enum class ERTSLayoutAnchor : uint8
{
	TopLeft,
	Top,
	TopRight,
	Left,
	Center,
	Right,
	BottomLeft,
	Bottom,
	BottomRight,
	TopStretch,
	BottomStretch,
	LeftStretch,
	RightStretch,
	Fill,
};

/** visibleWhen: "key" (true or non-empty), "!key" (false or empty), "key=value", "key!=value"; Always without one. */
struct FRTSLayoutCondition
{
	enum class EOp : uint8
	{
		Always,
		Truthy,
		Falsy,
		Equal,
		NotEqual,
	};

	EOp Op = EOp::Always;
	FName Key;
	FString Value;
};

/** A piece of a text: literal, or a {key} filled in from the binding. */
struct FRTSLayoutTextPart
{
	bool bKey = false;
	FString Literal;
	FName Key;
};

/** One element of a layout file, read and checked for known types and properties; the builder and the validator share it. */
struct WORLDDIVIDE_API FRTSLayoutElement
{
	ERTSLayoutType Type = ERTSLayoutType::Panel;

	/** The name, or for an unnamed element its place under its parent, such as "Root/Text#2": what errors and tests use. */
	FString Name;
	FString Label;

	ERTSLayoutAnchor Anchor = ERTSLayoutAnchor::TopLeft;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	bool bHasSize = false;
	FMargin Padding;
	FString Color;
	float Opacity = 1.f;
	TOptional<bool> HitTest;
	FRTSLayoutCondition VisibleWhen;

	/** Bound property (progress, sprite, enabled, selected, color) to its binding key. */
	TMap<FName, FName> Bind;

	FName Sprite;
	FName FillSprite;
	FString FillColor;
	TArray<FRTSLayoutTextPart> Text;
	FName Font;
	int32 FontSize = 0;
	FString Align;
	bool bWrap = false;
	int32 MaxLines = 0;
	float Spacing = 0.f;
	int32 Columns = 0;
	int32 Rows = 0;
	FVector2D CellSize = FVector2D::ZeroVector;
	FName Action;
	bool bTooltip = false;
	FString Layout;
	FName Widget;

	/** A list key: one copy of Template per item, or Count copies when set, the ones past the data with item.empty. */
	FName Repeat;
	int32 Count = 0;
	TSharedPtr<FRTSLayoutElement> Template;

	TArray<FRTSLayoutElement> Children;

	/** The root only: the layouts shown as modal screens over this one. */
	TArray<FString> Modals;

	bool IsStretchedX() const;
	bool IsStretchedY() const;

	/** Whether it catches the mouse: hitTest, else true for a Panel with a sprite and a Button. */
	bool CatchesMouse() const;

	/** Every key the element reads: its text, bind and visibleWhen. */
	void GetKeys(TArray<FName>& OutKeys) const;
};

namespace RTSLayout
{
	/** "<File>: <Element>: <Message>", the form of every layout error. */
	WORLDDIVIDE_API FString FormatError(const FString& File, const FString& Element, const FString& Message);

	/**
	 * Reads Object as the root element of File. Unknown types and properties, wrong value types and malformed texts and
	 * conditions are errors; parsing carries on past them, so the element is still usable.
	 */
	WORLDDIVIDE_API bool Parse(const FJsonObject& Object, const FString& File, FRTSLayoutElement& OutRoot, TArray<FString>& OutErrors);

	/** "#RRGGBB" or "#RRGGBBAA" in sRGB, to linear. */
	WORLDDIVIDE_API bool ParseHexColor(const FString& Text, FLinearColor& OutColor);

	/** The item.* keys, item.empty included, start with this. */
	WORLDDIVIDE_API extern const TCHAR* ItemPrefix;
	WORLDDIVIDE_API extern const FName ItemEmptyKey;

	/** The colour name that means the team colour of the item. */
	WORLDDIVIDE_API extern const TCHAR* TeamColor;
}
