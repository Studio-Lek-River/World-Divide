#include "UI/Layout/RTSLayoutWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "CommonBorder.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/RTSSettings.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Player/RTSPlayerController.h"
#include "Rendering/SlateRenderer.h"
#include "UI/Layout/RTSLayoutFiles.h"
#include "UI/RTSUIStyleSubsystem.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	/** Slots placing layouts that place layouts: deeper is a loop. */
	constexpr int32 MaxSlotDepth = 8;

	const FName DefaultFont(TEXT("Body"));
	const FName DefaultTextColor(TEXT("Text"));
	const FName DefaultButtonSprite(TEXT("Button"));
	const FName DefaultBarSprite(TEXT("ProgressBar.Background"));
	const FName DefaultFillSprite(TEXT("ProgressBar.Fill"));

	const FName BindProgress(TEXT("progress"));
	const FName BindSprite(TEXT("sprite"));
	const FName BindEnabled(TEXT("enabled"));
	const FName BindSelected(TEXT("selected"));
	const FName BindColor(TEXT("color"));

	/** The list key Team reads inside a template. */
	const FName TeamColorKey(TEXT("item.teamColor"));

	struct FAnchorInfo
	{
		FVector2D Min;
		FVector2D Max;
		FVector2D Alignment;
	};

	FAnchorInfo GetAnchorInfo(ERTSLayoutAnchor Anchor)
	{
		switch (Anchor)
		{
		case ERTSLayoutAnchor::Top: return { { 0.5, 0 }, { 0.5, 0 }, { 0.5, 0 } };
		case ERTSLayoutAnchor::TopRight: return { { 1, 0 }, { 1, 0 }, { 1, 0 } };
		case ERTSLayoutAnchor::Left: return { { 0, 0.5 }, { 0, 0.5 }, { 0, 0.5 } };
		case ERTSLayoutAnchor::Center: return { { 0.5, 0.5 }, { 0.5, 0.5 }, { 0.5, 0.5 } };
		case ERTSLayoutAnchor::Right: return { { 1, 0.5 }, { 1, 0.5 }, { 1, 0.5 } };
		case ERTSLayoutAnchor::BottomLeft: return { { 0, 1 }, { 0, 1 }, { 0, 1 } };
		case ERTSLayoutAnchor::Bottom: return { { 0.5, 1 }, { 0.5, 1 }, { 0.5, 1 } };
		case ERTSLayoutAnchor::BottomRight: return { { 1, 1 }, { 1, 1 }, { 1, 1 } };
		case ERTSLayoutAnchor::TopStretch: return { { 0, 0 }, { 1, 0 }, { 0, 0 } };
		case ERTSLayoutAnchor::BottomStretch: return { { 0, 1 }, { 1, 1 }, { 0, 1 } };
		case ERTSLayoutAnchor::LeftStretch: return { { 0, 0 }, { 0, 1 }, { 0, 0 } };
		case ERTSLayoutAnchor::RightStretch: return { { 1, 0 }, { 1, 1 }, { 1, 0 } };
		case ERTSLayoutAnchor::Fill: return { { 0, 0 }, { 1, 1 }, { 0, 0 } };
		default: return { { 0, 0 }, { 0, 0 }, { 0, 0 } };
		}
	}

	/** The cross axis of a row or column: align, Center when not given. */
	EVerticalAlignment GetBoxVAlign(const FString& Align)
	{
		return Align == TEXT("Start") ? VAlign_Top : Align == TEXT("End") ? VAlign_Bottom : Align == TEXT("Fill") ? VAlign_Fill : VAlign_Center;
	}

	EHorizontalAlignment GetBoxHAlign(const FString& Align)
	{
		return Align == TEXT("Start") ? HAlign_Left : Align == TEXT("End") ? HAlign_Right : Align == TEXT("Fill") ? HAlign_Fill : HAlign_Center;
	}

	bool IsContainer(ERTSLayoutType Type)
	{
		return Type == ERTSLayoutType::Panel || Type == ERTSLayoutType::HBox || Type == ERTSLayoutType::VBox
			|| Type == ERTSLayoutType::Grid || Type == ERTSLayoutType::Slot;
	}
}

const TArray<FName>& RTSLayout::GetPanelNames()
{
	static const TArray<FName> Names = {
		TEXT("Minimap"), TEXT("Commands"), TEXT("Upgrades"), TEXT("Notifications"), TEXT("Scoreboard"),
		TEXT("Tooltip"),
	};
	return Names;
}

void URTSLayoutText::SetOverrides(int32 InFontSize, TOptional<FLinearColor> InColor)
{
	FontSizeOverride = InFontSize;
	ColorOverride = InColor;
}

void URTSLayoutText::SynchronizeProperties()
{
	// The style sets the font and colour; the layout's own fontSize (in pixels) and #RRGGBB go on top.
	Super::SynchronizeProperties();

	if (FontSizeOverride > 0)
	{
		FSlateFontInfo SizedFont = GetFont();
		SizedFont.Size = FontSizeOverride * RTSLayout::FontPointsPerPixel;
		SetFont(SizedFont);
	}
	if (ColorOverride.IsSet())
	{
		SetColorAndOpacity(FSlateColor(ColorOverride.GetValue()));
	}
}

void URTSLayoutText::SetMaxLines(int32 InMaxLines)
{
	MaxLines = InMaxLines;
	if (MaxLines > 0)
	{
		SetTextOverflowPolicy(ETextOverflowPolicy::MultilineEllipsis);
		SetClipping(EWidgetClipping::ClipToBounds);
	}
}

float URTSLayoutText::GetMaxHeight() const
{
	const FSlateRenderer* Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
	if (!Renderer)
	{
		return 0.f;
	}
	const float LineHeight = Renderer->GetFontMeasureService()->GetMaxCharacterHeight(GetFont()) * LineHeightPercentage;
	// Half a pixel of room, so rounding never pushes the last line out.
	return MaxLines * LineHeight + Margin.GetTotalSpaceAlong<Orient_Vertical>() + 0.5f;
}

UCommonButtonInternalBase* URTSLayoutButton::ConstructInternalButton()
{
	// The base class wraps the root it finds in the clickable button, so the children's canvas must be there first.
	if (!WidgetTree->RootWidget)
	{
		PaddingBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		PaddingBorder->SetBrush(FSlateNoResource());
		PaddingBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Content = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		PaddingBorder->SetContent(Content);
		WidgetTree->RootWidget = PaddingBorder;
	}
	return Super::ConstructInternalButton();
}

void URTSLayoutButton::SetContentPadding(const FMargin& InPadding)
{
	if (PaddingBorder)
	{
		PaddingBorder->SetPadding(InPadding);
	}
}

void URTSLayoutButton::SetAction(URTSLayoutWidget* InLayout, FName InAction, const FRTSUIContext& InContext)
{
	Layout = InLayout;
	Action = InAction;
	Context = InContext;
}

void URTSLayoutButton::NativeOnClicked()
{
	Super::NativeOnClicked();

	if (Layout.IsValid() && !Action.IsNone())
	{
		Layout->RunAction(Action, Context);
	}
}

TArray<const URTSUIStyleSet*> URTSLayoutWidget::GetDefaultChain(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (const URTSUIStyleSubsystem* Styles = GameInstance ? GameInstance->GetSubsystem<URTSUIStyleSubsystem>() : nullptr)
	{
		return Styles->GetStyleChain();
	}
	TArray<const URTSUIStyleSet*> Chain;
	const URTSSettings* Settings = GetDefault<URTSSettings>();
	for (const TSoftObjectPtr<URTSUIStyleSet>& StyleSet : { Settings->CommonUIStyle, Settings->NeutralUIStyle })
	{
		if (const URTSUIStyleSet* Loaded = StyleSet.LoadSynchronous())
		{
			Chain.Add(Loaded);
		}
	}
	return Chain;
}

bool URTSLayoutWidget::BuildFromFile(const FString& Name)
{
	FString Error;
	const TSharedPtr<FJsonObject> Object = RTSLayoutFiles::Load(Name, Error);
	if (!Object)
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("Layout %s: %s"), *Name, *Error);
		return false;
	}
	return BuildFromJson(*Object, Name + RTSLayoutFiles::Suffix, GetDefaultChain(this), RTSUIBindings::GetDefaultSources());
}

bool URTSLayoutWidget::BuildFromJson(const FJsonObject& Object, const FString& InFile, TArray<const URTSUIStyleSet*> InChain,
	TArray<const IRTSUIBindingSource*> InSources)
{
	const TSharedRef<FRTSLayoutElement> Parsed = MakeShared<FRTSLayoutElement>();
	TArray<FString> Errors;
	RTSLayout::Parse(Object, InFile, *Parsed, Errors);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("Layout %s"), *Error);
	}
	Build(Parsed, InFile, MoveTemp(InChain), MoveTemp(InSources));
	return true;
}

void URTSLayoutWidget::Build(const TSharedRef<FRTSLayoutElement>& InRoot, const FString& InFile, TArray<const URTSUIStyleSet*> InChain,
	TArray<const IRTSUIBindingSource*> InSources)
{
	Root = InRoot;
	File = InFile;
	Chain = MoveTemp(InChain);
	Sources = MoveTemp(InSources);

	KeySources.Reset();
	ListSources.Reset();
	ActionSources.Reset();
	for (const IRTSUIBindingSource* Source : Sources)
	{
		FRTSUIDeclarations Declarations;
		Source->Declare(Declarations);
		for (const FName Key : Declarations.Keys)
		{
			KeySources.Add(Key, Source);
		}
		for (const TPair<FName, TSet<FName>>& List : Declarations.Lists)
		{
			ListSources.Add(List.Key, Source);
		}
		for (const FName Action : Declarations.Actions)
		{
			ActionSources.Add(Action, Source);
		}
	}

	if (!LayoutRoot)
	{
		LayoutRoot = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LayoutRoot"));
		LayoutRoot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = LayoutRoot;
	}
	LayoutRoot->ClearChildren();
	Bound.Reset();
	SlotRoots.Reset();
	DynamicRepeats.Reset();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	BuildInto(*LayoutRoot, nullptr, *Root, 0, MakeContext(NAME_None, INDEX_NONE), 0, NAME_None);
	UpdateBindings();
}

FRTSUIContext URTSLayoutWidget::MakeContext(FName List, int32 Index) const
{
	FRTSUIContext Context;
	Context.Player = GetOwningPlayer();
	Context.List = List;
	Context.Index = Index;
	return Context;
}

void URTSLayoutWidget::BuildInto(UPanelWidget& Panel, const FRTSLayoutElement* Parent, const FRTSLayoutElement& Element, int32 Index,
	const FRTSUIContext& Context, int32 Depth, FName RepeatOf)
{
	UWidget* Widget = nullptr;
	UPanelWidget* ChildPanel = nullptr;
	switch (Element.Type)
	{
	case ERTSLayoutType::Panel:
	{
		UCommonBorder* Border = WidgetTree->ConstructWidget<UCommonBorder>(UCommonBorder::StaticClass());
		if (!Element.Sprite.IsNone())
		{
			Border->SetStyle(URTSUIStyleSubsystem::FindBorderStyleIn(Chain, Element.Sprite));
		}
		else
		{
			// Without a sprite, a colour fills the panel flat, such as the dimmed backdrop of a modal screen.
			Border->SetBrush(Element.Color.IsEmpty() ? FSlateBrush(FSlateNoResource()) : FSlateBrush(FSlateColorBrush(FLinearColor::White)));
		}
		if (!Element.Color.IsEmpty())
		{
			Border->SetBrushColor(ResolveColor(Element.Color, Context));
		}
		Border->SetPadding(Element.Padding);
		Border->SetHorizontalAlignment(HAlign_Fill);
		Border->SetVerticalAlignment(VAlign_Fill);
		if (Element.CatchesMouse())
		{
			// Takes the click, so none reaches the battlefield.
			Border->OnMouseButtonDownEvent.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(URTSLayoutWidget, HandlePanelMouse));
			Border->OnMouseDoubleClickEvent.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(URTSLayoutWidget, HandlePanelMouse));
		}
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Border->SetContent(Canvas);
		Widget = Border;
		ChildPanel = Canvas;
		break;
	}
	case ERTSLayoutType::HBox:
		ChildPanel = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Widget = ChildPanel;
		break;
	case ERTSLayoutType::VBox:
		ChildPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Widget = ChildPanel;
		break;
	case ERTSLayoutType::Grid:
		ChildPanel = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass());
		Widget = ChildPanel;
		break;
	case ERTSLayoutType::Image:
	{
		UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		// The sprite goes on once the element is bound, which keeps a sprite sheet's frames.
		Image->SetBrush(FSlateNoResource());
		if (!Element.Color.IsEmpty())
		{
			Image->SetColorAndOpacity(ResolveColor(Element.Color, Context));
		}
		if (Element.CatchesMouse())
		{
			Image->OnMouseButtonDownEvent.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(URTSLayoutWidget, HandlePanelMouse));
		}
		Widget = Image;
		break;
	}
	case ERTSLayoutType::Text:
	{
		URTSLayoutText* Text = WidgetTree->ConstructWidget<URTSLayoutText>(URTSLayoutText::StaticClass());
		ApplyTextStyle(*Text, Element, Element.Color, Context);
		Text->SetJustification(Element.Align == TEXT("Center") ? ETextJustify::Center : Element.Align == TEXT("Right") ? ETextJustify::Right : ETextJustify::Left);
		Text->SetAutoWrapText(Element.bWrap);
		Text->SetMaxLines(Element.MaxLines);
		Text->SetText(FText::FromString(FormatText(Element, Context)));
		Widget = Text;
		break;
	}
	case ERTSLayoutType::Button:
	{
		URTSLayoutButton* Button = WidgetTree->ConstructWidget<URTSLayoutButton>(URTSLayoutButton::StaticClass());
		Button->SetStyle(URTSUIStyleSubsystem::FindButtonStyleIn(Chain, Element.Sprite.IsNone() ? DefaultButtonSprite : Element.Sprite));
		Button->SetContentPadding(Element.Padding);
		Button->SetAction(this, Element.Action, Context);
		Widget = Button;
		ChildPanel = Button->GetContent();
		break;
	}
	case ERTSLayoutType::ProgressBar:
	{
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		FProgressBarStyle Style = FProgressBarStyle::GetDefault();
		Style.BackgroundImage = URTSUIStyleSubsystem::FindSpriteIn(Chain, Element.Sprite.IsNone() ? DefaultBarSprite : Element.Sprite).Brush;
		Style.FillImage = URTSUIStyleSubsystem::FindSpriteIn(Chain, Element.FillSprite.IsNone() ? DefaultFillSprite : Element.FillSprite).Brush;
		Style.MarqueeImage = Style.FillImage;
		Bar->SetWidgetStyle(Style);
		Bar->SetFillColorAndOpacity(ResolveColor(Element.FillColor, Context));
		Bar->SetPercent(0.f);
		Widget = Bar;
		break;
	}
	case ERTSLayoutType::Slot:
		Widget = BuildSlot(Element, Context, Depth);
		break;
	}

	const ESlateVisibility ShownVisibility = Element.CatchesMouse() ? ESlateVisibility::Visible
		: IsContainer(Element.Type) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible;
	Widget->SetVisibility(ShownVisibility);

	UWidget* Outer = Widget;
	const URTSLayoutText* MaxLinesText = Element.MaxLines > 0 ? Cast<URTSLayoutText>(Widget) : nullptr;
	if (const float MaxHeight = MaxLinesText ? MaxLinesText->GetMaxHeight() : 0.f; MaxHeight > 0.f)
	{
		// No taller than maxLines lines; the text ends the last one in an ellipsis.
		USizeBox* LinesBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LinesBox->SetMaxDesiredHeight(MaxHeight);
		LinesBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		LinesBox->SetContent(Widget);
		Outer = LinesBox;
	}

	// A text given a height (its size, a stretch or a grid cell) sits in the middle of it, level with the icons beside it.
	const bool bGridCell = Parent && Parent->Type == ERTSLayoutType::Grid && !Parent->CellSize.IsZero();
	if (Element.Type == ERTSLayoutType::Text && (Element.Size.Y > 0.f || Element.IsStretchedY() || bGridCell))
	{
		USizeBox* CentreBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CentreBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		USizeBoxSlot* CentreSlot = CastChecked<USizeBoxSlot>(CentreBox->AddChild(Outer));
		CentreSlot->SetHorizontalAlignment(HAlign_Fill);
		CentreSlot->SetVerticalAlignment(VAlign_Center);
		Outer = CentreBox;
	}

	// Padding inside an element without its own: a border with nothing to draw.
	if (Element.Padding != FMargin() && Element.Type != ERTSLayoutType::Panel && Element.Type != ERTSLayoutType::Button)
	{
		UBorder* PaddingBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		PaddingBox->SetBrush(FSlateNoResource());
		PaddingBox->SetPadding(Element.Padding);
		PaddingBox->SetContent(Outer);
		Outer = PaddingBox;
	}

	const ERTSLayoutType ParentType = Parent ? Parent->Type : ERTSLayoutType::Panel;
	FVector2D BoxSize = Element.bHasSize ? Element.Size : FVector2D::ZeroVector;
	if (ParentType == ERTSLayoutType::Grid && !Parent->CellSize.IsZero())
	{
		BoxSize = Parent->CellSize;
	}
	const bool bInCanvas = ParentType != ERTSLayoutType::HBox && ParentType != ERTSLayoutType::VBox && ParentType != ERTSLayoutType::Grid;
	if (!bInCanvas && !BoxSize.IsZero())
	{
		USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		if (BoxSize.X > 0.f)
		{
			SizeBox->SetWidthOverride(BoxSize.X);
		}
		if (BoxSize.Y > 0.f)
		{
			SizeBox->SetHeightOverride(BoxSize.Y);
		}
		SizeBox->SetContent(Outer);
		Outer = SizeBox;
	}
	if (Outer != Widget)
	{
		Outer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	Outer->SetRenderOpacity(Element.Opacity);

	switch (ParentType)
	{
	case ERTSLayoutType::HBox:
	{
		UHorizontalBoxSlot* BoxSlot = CastChecked<UHorizontalBox>(&Panel)->AddChildToHorizontalBox(Outer);
		BoxSlot->SetPadding(FMargin(Index > 0 ? Parent->Spacing : 0.f, 0.f, 0.f, 0.f));
		BoxSlot->SetVerticalAlignment(GetBoxVAlign(Parent->Align));
		// A child stretched along the row takes the room the others leave.
		if (Element.IsStretchedX())
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		break;
	}
	case ERTSLayoutType::VBox:
	{
		UVerticalBoxSlot* BoxSlot = CastChecked<UVerticalBox>(&Panel)->AddChildToVerticalBox(Outer);
		BoxSlot->SetPadding(FMargin(0.f, Index > 0 ? Parent->Spacing : 0.f, 0.f, 0.f));
		BoxSlot->SetHorizontalAlignment(GetBoxHAlign(Parent->Align));
		if (Element.IsStretchedY())
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		break;
	}
	case ERTSLayoutType::Grid:
	{
		const int32 Columns = FMath::Max(Parent->Columns, 1);
		const int32 Row = Index / Columns;
		const int32 Column = Index % Columns;
		UGridSlot* GridSlot = CastChecked<UGridPanel>(&Panel)->AddChildToGrid(Outer, Row, Column);
		GridSlot->SetPadding(FMargin(Column > 0 ? Parent->Spacing : 0.f, Row > 0 ? Parent->Spacing : 0.f, 0.f, 0.f));
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
		break;
	}
	default:
	{
		// On a stretched axis the position is the inset from both edges; otherwise the offset from the anchor, inward for
		// right and bottom anchors, and the size.
		const FAnchorInfo Anchor = GetAnchorInfo(Element.Anchor);
		UCanvasPanelSlot* CanvasSlot = CastChecked<UCanvasPanel>(&Panel)->AddChildToCanvas(Outer);
		CanvasSlot->SetAnchors(FAnchors(Anchor.Min.X, Anchor.Min.Y, Anchor.Max.X, Anchor.Max.Y));
		CanvasSlot->SetAlignment(Anchor.Alignment);
		FMargin Offsets;
		if (Element.IsStretchedX())
		{
			Offsets.Left = Element.Position.X;
			Offsets.Right = Element.Position.X;
		}
		else
		{
			Offsets.Left = Anchor.Min.X == 1.0 ? -Element.Position.X : Element.Position.X;
			Offsets.Right = Element.Size.X;
		}
		if (Element.IsStretchedY())
		{
			Offsets.Top = Element.Position.Y;
			Offsets.Bottom = Element.Position.Y;
		}
		else
		{
			Offsets.Top = Anchor.Min.Y == 1.0 ? -Element.Position.Y : Element.Position.Y;
			Offsets.Bottom = Element.Size.Y;
		}
		CanvasSlot->SetOffsets(Offsets);
		const bool bSizedX = Element.IsStretchedX() || Element.Size.X > 0.f;
		const bool bSizedY = Element.IsStretchedY() || Element.Size.Y > 0.f;
		CanvasSlot->SetAutoSize(!bSizedX || !bSizedY);
		break;
	}
	}

	FBoundElement& Entry = Bound.AddDefaulted_GetRef();
	Entry.Element = &Element;
	Entry.Outer = Outer;
	Entry.Widget = Widget;
	Entry.Context = Context;
	Entry.RepeatOf = RepeatOf;
	Entry.ShownVisibility = Outer == Widget ? ShownVisibility : ESlateVisibility::SelfHitTestInvisible;
	Entry.bKeepSpace = ParentType == ERTSLayoutType::Grid;
	// A progress bar's colour is its fill.
	Entry.LastColor = Element.Type == ERTSLayoutType::ProgressBar ? Element.FillColor : Element.Color;
	Entry.LastSprite = Element.Sprite.IsNone() ? FString() : Element.Sprite.ToString();
	if (UImage* Image = Cast<UImage>(Widget); Image && !Element.Sprite.IsNone())
	{
		SetImageSprite(*Image, Entry, Element.Sprite);
	}

	if (ChildPanel)
	{
		BuildChildren(Element, *ChildPanel, Context, Depth);
	}
}

void URTSLayoutWidget::BuildChildren(const FRTSLayoutElement& Element, UPanelWidget& Panel, const FRTSUIContext& Context, int32 Depth)
{
	int32 Index = 0;
	for (const FRTSLayoutElement& Child : Element.Children)
	{
		BuildInto(Panel, &Element, Child, Index++, Context, Depth, NAME_None);
	}
	if (Element.Repeat.IsNone() || !Element.Template)
	{
		return;
	}
	int32 Copies = Element.Count;
	if (Copies <= 0)
	{
		Copies = GetListCount(Element.Repeat, Context);
		DynamicRepeats.Add(Element.Repeat, Copies);
	}
	const FName RepeatOf(*Element.Label);
	for (int32 Item = 0; Item < Copies; ++Item)
	{
		BuildInto(Panel, &Element, *Element.Template, Index++, MakeContext(Element.Repeat, Item), Depth, RepeatOf);
	}
}

UWidget* URTSLayoutWidget::BuildSlot(const FRTSLayoutElement& Element, const FRTSUIContext& Context, int32 Depth)
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	if (Element.Layout.IsEmpty())
	{
		// The engine panels fill their slots in their own issues; until then the slot stays empty.
		if (!RTSLayout::GetPanelNames().Contains(Element.Widget))
		{
			Warn(Element.Widget, FString::Printf(TEXT("%s: unknown widget \"%s\""), *Element.Label, *Element.Widget.ToString()));
		}
		return Canvas;
	}
	if (Depth >= MaxSlotDepth)
	{
		Warn(FName(*Element.Layout), FString::Printf(TEXT("%s: slots nest deeper than %d, a layout places itself"), *Element.Label, MaxSlotDepth));
		return Canvas;
	}
	FString Error;
	const TSharedPtr<FJsonObject> Object = RTSLayoutFiles::Load(Element.Layout, Error);
	if (!Object)
	{
		Warn(FName(*Element.Layout), FString::Printf(TEXT("%s: %s"), *Element.Label, *Error));
		return Canvas;
	}
	const TSharedRef<FRTSLayoutElement> Nested = MakeShared<FRTSLayoutElement>();
	TArray<FString> Errors;
	RTSLayout::Parse(*Object, Element.Layout + RTSLayoutFiles::Suffix, *Nested, Errors);
	for (const FString& NestedError : Errors)
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("Layout %s"), *NestedError);
	}
	SlotRoots.Add(Nested);
	BuildInto(*Canvas, nullptr, *Nested, 0, Context, Depth + 1, NAME_None);
	return Canvas;
}

void URTSLayoutWidget::ApplyTextStyle(URTSLayoutText& Text, const FRTSLayoutElement& Element, const FString& Color, const FRTSUIContext& Context)
{
	FLinearColor Hex;
	TOptional<FLinearColor> Override;
	FName StyleColor = DefaultTextColor;
	if (RTSLayout::ParseHexColor(Color, Hex))
	{
		Override = Hex;
	}
	else if (Color == RTSLayout::TeamColor)
	{
		Override = GetTeamColor(Context);
	}
	else if (!Color.IsEmpty())
	{
		StyleColor = FName(*Color);
	}
	Text.SetOverrides(Element.FontSize, Override);
	Text.SetStyle(URTSUIStyleSubsystem::FindTextStyleIn(Chain, Element.Font.IsNone() ? DefaultFont : Element.Font, StyleColor));
}

FLinearColor URTSLayoutWidget::ResolveColor(const FString& Color, const FRTSUIContext& Context)
{
	FLinearColor Hex;
	if (Color.IsEmpty())
	{
		return FLinearColor::White;
	}
	if (Color == RTSLayout::TeamColor)
	{
		return GetTeamColor(Context);
	}
	if (RTSLayout::ParseHexColor(Color, Hex))
	{
		return Hex;
	}
	return URTSUIStyleSubsystem::FindColorIn(Chain, FName(*Color));
}

FLinearColor URTSLayoutWidget::GetTeamColor(const FRTSUIContext& Context)
{
	FRTSUIValue Value;
	if (!Context.List.IsNone() && GetValue(TeamColorKey, Context, Value))
	{
		// An empty copy has no team; a source answering Team itself would loop.
		return Value.String.IsEmpty() || Value.String == RTSLayout::TeamColor ? FLinearColor::White : ResolveColor(Value.String, Context);
	}
	const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(Context.Player);
	return Controller ? ARTSUnit::GetTeamColor(Controller->GetTeamId()) : FLinearColor::White;
}

void URTSLayoutWidget::SetImageSprite(UImage& Image, FBoundElement& Entry, FName Sprite) const
{
	Entry.Frames = 0;
	if (Sprite.IsNone())
	{
		Image.SetBrush(FSlateNoResource());
		return;
	}
	const FRTSUISprite& Found = URTSUIStyleSubsystem::FindSpriteIn(Chain, Sprite);
	if (!Entry.Element->TileSize.IsZero())
	{
		// Repeats at the tile size, so a pattern keeps its scale whatever the element's size; a sheet's frames are not tiled.
		FSlateBrush Tiled = Found.Brush;
		Tiled.DrawAs = ESlateBrushDrawType::Image;
		Tiled.Tiling = ESlateBrushTileType::Both;
		Tiled.ImageSize = Entry.Element->TileSize;
		Image.SetBrush(Tiled);
		return;
	}
	Image.SetBrush(Found.Brush);
	if (Found.Frames <= 0 || Found.FrameSize.X <= 0 || Found.FrameSize.Y <= 0 || Found.Brush.ImageSize.X <= 0.f || Found.Brush.ImageSize.Y <= 0.f)
	{
		return;
	}
	Entry.Frames = Found.Frames;
	Entry.FrameSize = FVector2f(Found.FrameSize.X, Found.FrameSize.Y);
	Entry.SheetSize = FVector2f(Found.Brush.ImageSize);
	Entry.Fps = Found.Fps;
	Entry.SheetTime = 0.0;
	Entry.ShownFrame = 0;
	ShowFrame(Image, Entry, 0);
}

void URTSLayoutWidget::ShowFrame(UImage& Image, const FBoundElement& Entry, int32 Frame)
{
	// The frames sit in one row from the top left of the sheet.
	FSlateBrush Brush = Image.GetBrush();
	Brush.ImageSize = FVector2D(Entry.FrameSize);
	const FVector2f Min(Frame * Entry.FrameSize.X / Entry.SheetSize.X, 0.f);
	Brush.SetUVRegion(FBox2f(Min, Min + Entry.FrameSize / Entry.SheetSize));
	Image.SetBrush(Brush);
}

void URTSLayoutWidget::Animate(float DeltaSeconds)
{
	for (FBoundElement& Entry : Bound)
	{
		UImage* Image = Entry.Frames > 0 && Entry.Fps > 0.f ? Cast<UImage>(Entry.Widget.Get()) : nullptr;
		if (!Image)
		{
			continue;
		}
		Entry.SheetTime += DeltaSeconds;
		const int32 Frame = FMath::FloorToInt32(Entry.SheetTime * Entry.Fps) % Entry.Frames;
		if (Frame != Entry.ShownFrame)
		{
			Entry.ShownFrame = Frame;
			ShowFrame(*Image, Entry, Frame);
		}
	}
}

int32 URTSLayoutWidget::GetListCount(FName List, const FRTSUIContext& Context)
{
	const IRTSUIBindingSource* Source = ListSources.FindRef(List);
	if (!Source)
	{
		Warn(List, FString::Printf(TEXT("unknown list \"%s\""), *List.ToString()));
		return 0;
	}
	return Source->GetListCount(List, Context);
}

bool URTSLayoutWidget::GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue)
{
	const FString KeyText = Key.ToString();
	if (KeyText.StartsWith(RTSLayout::ItemPrefix))
	{
		if (Context.List.IsNone())
		{
			Warn(Key, FString::Printf(TEXT("\"%s\" outside a repeat template"), *KeyText));
			return false;
		}
		const IRTSUIBindingSource* Source = ListSources.FindRef(Context.List);
		const int32 Count = GetListCount(Context.List, Context);
		if (Key == RTSLayout::ItemEmptyKey)
		{
			OutValue = FRTSUIValue::MakeBool(Context.Index >= Count);
			return true;
		}
		if (!Source)
		{
			return false;
		}
		if (Context.Index >= Count)
		{
			// An empty slot shows nothing.
			OutValue = FRTSUIValue();
			return true;
		}
		if (Source->GetListValue(FName(*KeyText.RightChop(FCString::Strlen(RTSLayout::ItemPrefix))), Context, OutValue))
		{
			return true;
		}
		Warn(Key, FString::Printf(TEXT("unknown item key \"%s\" in list \"%s\""), *KeyText, *Context.List.ToString()));
		return false;
	}
	const IRTSUIBindingSource* Source = KeySources.FindRef(Key);
	if (Source && Source->GetValue(Key, Context, OutValue))
	{
		return true;
	}
	Warn(Key, FString::Printf(TEXT("unknown binding key \"%s\""), *KeyText));
	return false;
}

bool URTSLayoutWidget::IsShown(const FRTSLayoutCondition& Condition, const FRTSUIContext& Context)
{
	if (Condition.Op == FRTSLayoutCondition::EOp::Always)
	{
		return true;
	}
	FRTSUIValue Value;
	if (!GetValue(Condition.Key, Context, Value))
	{
		// An unknown key hides nothing, so the mistake stays visible.
		return true;
	}
	switch (Condition.Op)
	{
	case FRTSLayoutCondition::EOp::Truthy:
		return Value.IsTruthy();
	case FRTSLayoutCondition::EOp::Falsy:
		return !Value.IsTruthy();
	case FRTSLayoutCondition::EOp::Equal:
		return Value.ToString() == Condition.Value;
	case FRTSLayoutCondition::EOp::NotEqual:
		return Value.ToString() != Condition.Value;
	default:
		return true;
	}
}

FString URTSLayoutWidget::FormatText(const FRTSLayoutElement& Element, const FRTSUIContext& Context)
{
	FString Result;
	for (const FRTSLayoutTextPart& Part : Element.Text)
	{
		if (!Part.bKey)
		{
			Result += Part.Literal;
			continue;
		}
		FRTSUIValue Value;
		// An unknown key shows itself, so the mistake is visible.
		Result += GetValue(Part.Key, Context, Value) ? Value.ToString() : FString::Printf(TEXT("{%s}"), *Part.Key.ToString());
	}
	return Result;
}

void URTSLayoutWidget::Warn(FName Key, const FString& Message)
{
	if (!Warned.Contains(Key))
	{
		Warned.Add(Key);
		UE_LOG(LogWorldDivide, Warning, TEXT("Layout %s: %s"), *File, *Message);
	}
}

void URTSLayoutWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateBindings();
	Animate(InDeltaTime);
}

void URTSLayoutWidget::UpdateBindings()
{
	const FRTSUIContext Context = MakeContext(NAME_None, INDEX_NONE);
	bool bListChanged = false;
	for (const TPair<FName, int32>& Repeat : DynamicRepeats)
	{
		bListChanged |= GetListCount(Repeat.Key, Context) != Repeat.Value;
	}
	if (bListChanged)
	{
		// A list without a fixed count grew or shrank: build its copies anew.
		Build(Root.ToSharedRef(), File, Chain, Sources);
		return;
	}
	for (FBoundElement& Entry : Bound)
	{
		UpdateElement(Entry);
	}
}

void URTSLayoutWidget::UpdateElement(FBoundElement& Entry)
{
	UWidget* Outer = Entry.Outer.Get();
	UWidget* Widget = Entry.Widget.Get();
	if (!Outer || !Widget)
	{
		return;
	}
	const FRTSLayoutElement& Element = *Entry.Element;
	Entry.Context.Player = GetOwningPlayer();

	const bool bShown = IsShown(Element.VisibleWhen, Entry.Context);
	const ESlateVisibility WantedVisibility = bShown ? Entry.ShownVisibility
		: Entry.bKeepSpace ? ESlateVisibility::Hidden : ESlateVisibility::Collapsed;
	if (Outer->GetVisibility() != WantedVisibility)
	{
		Outer->SetVisibility(WantedVisibility);
	}
	if (!bShown)
	{
		return;
	}

	if (URTSLayoutText* Text = Cast<URTSLayoutText>(Widget))
	{
		if (Element.Text.ContainsByPredicate([](const FRTSLayoutTextPart& Part) { return Part.bKey; }))
		{
			const FString Formatted = FormatText(Element, Entry.Context);
			if (!Text->GetText().ToString().Equals(Formatted, ESearchCase::CaseSensitive))
			{
				Text->SetText(FText::FromString(Formatted));
			}
		}
	}

	for (const TPair<FName, FName>& Binding : Element.Bind)
	{
		FRTSUIValue Value;
		if (!GetValue(Binding.Value, Entry.Context, Value))
		{
			continue;
		}
		if (Binding.Key == BindProgress)
		{
			if (UProgressBar* Bar = Cast<UProgressBar>(Widget))
			{
				Bar->SetPercent(static_cast<float>(Value.Number));
			}
		}
		else if (Binding.Key == BindSprite)
		{
			UImage* Image = Cast<UImage>(Widget);
			if (Image && Value.String != Entry.LastSprite)
			{
				Entry.LastSprite = Value.String;
				SetImageSprite(*Image, Entry, Value.String.IsEmpty() ? NAME_None : FName(*Value.String));
			}
		}
		else if (Binding.Key == BindEnabled)
		{
			if (Widget->GetIsEnabled() != Value.IsTruthy())
			{
				Widget->SetIsEnabled(Value.IsTruthy());
			}
		}
		else if (Binding.Key == BindSelected)
		{
			URTSLayoutButton* Button = Cast<URTSLayoutButton>(Widget);
			if (Button && Button->GetSelected() != Value.IsTruthy())
			{
				Button->SetIsSelectable(true);
				Button->SetIsSelected(Value.IsTruthy(), false);
			}
		}
		else if (Binding.Key == BindColor && Value.String != Entry.LastColor)
		{
			Entry.LastColor = Value.String;
			Entry.LastTeamColor = FLinearColor::Transparent;
			if (Value.String != RTSLayout::TeamColor)
			{
				ApplyColor(*Widget, Element, Value.String, Entry.Context);
			}
		}
	}

	// Team follows the item or the player, neither of which says when it changes.
	if (Entry.LastColor == RTSLayout::TeamColor)
	{
		const FLinearColor TeamColor = GetTeamColor(Entry.Context);
		if (TeamColor != Entry.LastTeamColor)
		{
			Entry.LastTeamColor = TeamColor;
			ApplyColor(*Widget, Element, Entry.LastColor, Entry.Context);
		}
	}
}

void URTSLayoutWidget::ApplyColor(UWidget& Widget, const FRTSLayoutElement& Element, const FString& Color, const FRTSUIContext& Context)
{
	if (URTSLayoutText* Text = Cast<URTSLayoutText>(&Widget))
	{
		ApplyTextStyle(*Text, Element, Color, Context);
	}
	else if (UImage* Image = Cast<UImage>(&Widget))
	{
		Image->SetColorAndOpacity(ResolveColor(Color, Context));
	}
	else if (UBorder* Border = Cast<UBorder>(&Widget))
	{
		Border->SetBrushColor(ResolveColor(Color, Context));
	}
	else if (UProgressBar* Bar = Cast<UProgressBar>(&Widget))
	{
		Bar->SetFillColorAndOpacity(ResolveColor(Color, Context));
	}
}

void URTSLayoutWidget::RunAction(FName Action, const FRTSUIContext& Context)
{
	FRTSUIContext WithPlayer = Context;
	WithPlayer.Player = GetOwningPlayer();
	const IRTSUIBindingSource* Source = ActionSources.FindRef(Action);
	if (!Source || !Source->HandleAction(Action, WithPlayer))
	{
		Warn(Action, FString::Printf(TEXT("unknown action \"%s\""), *Action.ToString()));
	}
}

FEventReply URTSLayoutWidget::HandlePanelMouse(FGeometry Geometry, const FPointerEvent& MouseEvent)
{
	return FEventReply(true);
}

bool URTSLayoutWidget::IsOnPanel(const FVector2D& AbsolutePoint) const
{
	for (const FBoundElement& Entry : Bound)
	{
		const UWidget* Widget = Entry.Widget.Get();
		if (!Widget || !Entry.Element->CatchesMouse() || !Widget->GetCachedGeometry().IsUnderLocation(AbsolutePoint))
		{
			continue;
		}
		// A collapsed element keeps the geometry it had; it and everything around it must be shown.
		bool bShown = true;
		for (const UWidget* Ancestor = Widget; Ancestor && Ancestor != this; Ancestor = Ancestor->GetParent())
		{
			if (!Ancestor->IsVisible())
			{
				bShown = false;
				break;
			}
		}
		if (bShown)
		{
			return true;
		}
	}
	return false;
}

UWidget* URTSLayoutWidget::FindElement(FName Name) const
{
	const FString NameText = Name.ToString();
	for (const FBoundElement& Entry : Bound)
	{
		if (Entry.Element->Name == NameText)
		{
			return Entry.Widget.Get();
		}
	}
	return nullptr;
}

TArray<UWidget*> URTSLayoutWidget::GetRepeatItems(FName Name) const
{
	TArray<UWidget*> Items;
	for (const FBoundElement& Entry : Bound)
	{
		if (Entry.RepeatOf == Name)
		{
			Items.Add(Entry.Outer.Get());
		}
	}
	return Items;
}
