#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CommonTextBlock.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/RTSTestUI.h"
#include "UI/Layout/RTSLayoutFiles.h"
#include "UI/Layout/RTSLayoutValidator.h"
#include "UI/Layout/RTSLayoutWidget.h"
#include "UI/Layout/RTSUICommandlet.h"
#include "UI/RTSUIStyleSet.h"

namespace
{
	using RTSTests::TestFlags;

	const FVector2D LayoutViewport(1920.f, 1080.f);

	/** A binding source the tests set by hand: plain keys, one list of named items and one action. */
	class FTestBindings : public IRTSUIBindingSource
	{
	public:
		int32 Credits = 100;
		float Progress = 0.f;
		FString Color;
		bool bOn = true;
		FString Mode;
		TArray<FString> Items;
		/** Each item's item.teamColor, white past the end. */
		TArray<FString> TeamColors;
		int32 Clicks = 0;

		virtual void Declare(FRTSUIDeclarations& Out) const override
		{
			Out.Keys.Append({ TEXT("test.credits"), TEXT("test.progress"), TEXT("test.color"), TEXT("test.on"), TEXT("test.mode") });
			Out.Lists.Add(TEXT("test.items"), { TEXT("name"), TEXT("enabled"), TEXT("teamColor") });
			Out.Actions.Add(TEXT("Click"));
		}

		virtual TArray<FName> GetPanels() const override
		{
			return { TEXT("Test") };
		}

		virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			if (Key == TEXT("test.credits"))
			{
				OutValue = FRTSUIValue::MakeNumber(Credits);
			}
			else if (Key == TEXT("test.progress"))
			{
				OutValue = FRTSUIValue::MakeProgress(Progress);
			}
			else if (Key == TEXT("test.color"))
			{
				OutValue = FRTSUIValue::MakeColor(FName(*Color));
			}
			else if (Key == TEXT("test.on"))
			{
				OutValue = FRTSUIValue::MakeBool(bOn);
			}
			else if (Key == TEXT("test.mode"))
			{
				OutValue = FRTSUIValue::MakeText(Mode);
			}
			else
			{
				return false;
			}
			return true;
		}

		virtual int32 GetListCount(FName List, const FRTSUIContext& Context) const override
		{
			return List == TEXT("test.items") ? Items.Num() : 0;
		}

		virtual bool GetListValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const override
		{
			if (Key == TEXT("name"))
			{
				OutValue = FRTSUIValue::MakeText(Items[Context.Index]);
				return true;
			}
			if (Key == TEXT("enabled"))
			{
				OutValue = FRTSUIValue::MakeBool(true);
				return true;
			}
			if (Key == TEXT("teamColor"))
			{
				OutValue = FRTSUIValue::MakeColor(TeamColors.IsValidIndex(Context.Index) ? FName(*TeamColors[Context.Index]) : FName(TEXT("#FFFFFF")));
				return true;
			}
			return false;
		}

		virtual bool HandleAction(FName Action, const FRTSUIContext& Context) const override
		{
			if (Action != TEXT("Click"))
			{
				return false;
			}
			const_cast<FTestBindings*>(this)->Clicks++;
			return true;
		}
	};

	/** Builds Json as File in a new layout widget of the editor world, with the test bindings and the generated styles after Extra. */
	URTSLayoutWidget* BuildLayout(FAutomationTestBase& Test, const TCHAR* Json, const FString& File, const FTestBindings& Bindings,
		const URTSUIStyleSet* Extra = nullptr)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		URTSLayoutWidget* Widget = World ? CreateWidget<URTSLayoutWidget>(World, URTSLayoutWidget::StaticClass()) : nullptr;
		FString Error;
		const TSharedPtr<FJsonObject> Object = RTSLayoutFiles::Parse(Json, Error);
		if (!Widget || !Object)
		{
			Test.AddError(FString::Printf(TEXT("Could not build %s: %s"), *File, *Error));
			return nullptr;
		}
		TArray<const URTSUIStyleSet*> Chain = URTSLayoutWidget::GetDefaultChain(World);
		if (Extra)
		{
			Chain.Insert(Extra, 0);
		}
		Widget->BuildFromJson(*Object, File, MoveTemp(Chain), { &Bindings });
		return Widget;
	}

	/** The element's top left corner and size in layout pixels, as laid out last. */
	FBox2D GetRect(const URTSLayoutWidget& Layout, const UWidget* Widget)
	{
		if (!Widget)
		{
			return FBox2D(ForceInit);
		}
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D Min = Geometry.GetAbsolutePosition() - Layout.GetCachedGeometry().GetAbsolutePosition();
		return FBox2D(Min, Min + Geometry.GetAbsoluteSize());
	}

	void TestRect(FAutomationTestBase& Test, const FString& What, const FBox2D& Rect, const FVector2D& Position, const FVector2D& Size)
	{
		Test.TestTrue(FString::Printf(TEXT("%s at %s, expected %s"), *What, *Rect.Min.ToString(), *Position.ToString()),
			Rect.Min.Equals(Position, 0.5));
		Test.TestTrue(FString::Printf(TEXT("%s sized %s, expected %s"), *What, *Rect.GetSize().ToString(), *Size.ToString()),
			Rect.GetSize().Equals(Size, 0.5));
	}

	bool IsShown(const UWidget* Widget)
	{
		return Widget && Widget->IsVisible();
	}

	const TCHAR* GeometryLayout = TEXT(R"({
		"name": "Root", "type": "Panel", "anchor": "Fill",
		"children": [
			{ "name": "Corner", "type": "Panel", "anchor": "TopRight", "position": [10, 20], "size": [100, 50] },
			{ "name": "Band", "type": "Panel", "anchor": "BottomStretch", "size": [0, 230] },
			{ "name": "Row", "type": "HBox", "anchor": "TopLeft", "position": [100, 100], "spacing": 8, "children": [
				{ "name": "First", "type": "Image", "size": [28, 28] },
				{ "name": "Second", "type": "Image", "size": [40, 28] }
			] },
			{ "name": "Commands", "type": "Grid", "anchor": "BottomRight", "columns": 5, "rows": 3, "cellSize": [84, 64], "spacing": 4,
				"repeat": "test.items", "count": 15,
				"template": { "type": "Button", "sprite": "Button", "bind": { "enabled": "item.enabled" }, "visibleWhen": "!item.empty" } }
		]
	})");

	const TCHAR* BindingLayout = TEXT(R"({
		"name": "Root", "type": "Panel", "anchor": "Fill",
		"children": [
			{ "name": "Label", "type": "Text", "text": "Credits: {test.credits}" },
			{ "name": "Bar", "type": "ProgressBar", "bind": { "progress": "test.progress" } },
			{ "name": "Tinted", "type": "Image", "bind": { "color": "test.color" } },
			{ "name": "WhenOn", "type": "Image", "visibleWhen": "test.on" },
			{ "name": "WhenOff", "type": "Image", "visibleWhen": "!test.on" },
			{ "name": "WhenRed", "type": "Image", "visibleWhen": "test.mode=Red" },
			{ "name": "WhenNotRed", "type": "Image", "visibleWhen": "test.mode!=Red" },
			{ "name": "Names", "type": "VBox", "repeat": "test.items", "count": 3,
				"template": { "type": "Text", "text": "{item.name}", "visibleWhen": "!item.empty" } }
		]
	})");

	const TCHAR* UnknownLayout = TEXT(R"({
		"name": "Root", "type": "Panel", "anchor": "Fill",
		"children": [
			{ "name": "Ghost", "type": "Text", "text": "{test.missing}", "visibleWhen": "test.missing" },
			{ "name": "Press", "type": "Button", "action": "Missing", "size": [40, 40] }
		]
	})");

	/** One fault per element, each one the validator must name with the file and the element. */
	const TCHAR* BrokenLayout = TEXT(R"({
		"name": "Broken", "type": "Panel", "anchor": "Fill",
		"children": [
			{ "name": "Icon", "type": "Image", "sprite": "NoSuchSprite" },
			{ "name": "Thing", "type": "Widget" },
			{ "name": "Label", "type": "Text", "text": "{no.such.key}", "fonts": "Body" },
			{ "name": "Label", "type": "Text", "font": "Huge", "color": "Purple" },
			{ "name": "Press", "type": "Button", "action": "NoSuchAction" },
			{ "name": "Hole", "type": "Slot", "layout": "NoSuchLayout" }
		]
	})");

	const TCHAR* MaxLinesLayout = TEXT(R"({
		"name": "Root", "type": "Panel", "anchor": "Fill",
		"children": [
			{ "name": "Column", "type": "VBox", "anchor": "TopLeft", "align": "Start", "children": [
				{ "name": "Limited", "type": "Text", "text": "{test.mode}", "wrap": true, "maxLines": 2, "size": [200, 0] },
				{ "name": "Unlimited", "type": "Text", "text": "{test.mode}", "wrap": true, "size": [200, 0] }
			] }
		]
	})");

	const TCHAR* SpriteSheetLayout = TEXT(R"({
		"name": "Root", "type": "Panel", "anchor": "Fill",
		"children": [ { "name": "Spinner", "type": "Image", "sprite": "Sheet" } ]
	})");

	const TCHAR* TeamColorLayout = TEXT(R"({
		"name": "Root", "type": "Panel", "anchor": "Fill",
		"children": [
			{ "name": "Players", "type": "VBox", "repeat": "test.items", "count": 2,
				"template": { "type": "Image", "color": "Team", "size": [16, 16] } },
			{ "name": "Mine", "type": "Image", "color": "Team" }
		]
	})");

	void WriteFile(const FString& Path, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutGeometryTest, "WorldDivide.UI.Layout.Geometry", TestFlags)

bool FRTSLayoutGeometryTest::RunTest(const FString& Parameters)
{
	FTestBindings Bindings;
	// One short of the 15 cells, so the last one is empty.
	for (int32 Item = 0; Item < 14; ++Item)
	{
		Bindings.Items.Add(FString::Printf(TEXT("Item%d"), Item));
	}
	URTSLayoutWidget* Layout = BuildLayout(*this, GeometryLayout, TEXT("Geometry.layout.json"), Bindings);
	if (!Layout)
	{
		return false;
	}
	// Held, as a viewport would hold it: the widgets keep their geometry only while their Slate widgets live.
	const TSharedRef<SWidget> Slate = Layout->TakeWidget();
	RTSTests::LayOutHeadless(*Layout, LayoutViewport);

	// At 1920x1080 the DPI scale is 1, so HUD units are pixels.
	TestRect(*this, TEXT("A TopRight panel, offset inward"), GetRect(*Layout, Layout->FindElement(TEXT("Corner"))), FVector2D(1810, 20), FVector2D(100, 50));
	TestRect(*this, TEXT("A BottomStretch band"), GetRect(*Layout, Layout->FindElement(TEXT("Band"))), FVector2D(0, 850), FVector2D(1920, 230));
	TestRect(*this, TEXT("An HBox's first child"), GetRect(*Layout, Layout->FindElement(TEXT("First"))), FVector2D(100, 100), FVector2D(28, 28));
	TestRect(*this, TEXT("Its second, after the spacing"), GetRect(*Layout, Layout->FindElement(TEXT("Second"))), FVector2D(136, 100), FVector2D(40, 28));

	// 5 x 84 + 4 x 4 wide and 3 x 64 + 2 x 4 high, in the bottom right corner.
	const TArray<UWidget*> Cells = Layout->GetRepeatItems(TEXT("Commands"));
	TestEqual(TEXT("repeat with count 15 makes 15 cells"), Cells.Num(), 15);
	if (Cells.Num() == 15)
	{
		TestRect(*this, TEXT("The first cell"), GetRect(*Layout, Cells[0]), FVector2D(1484, 880), FVector2D(84, 64));
		TestRect(*this, TEXT("The sixth cell starts the second row"), GetRect(*Layout, Cells[5]), FVector2D(1484, 948), FVector2D(84, 64));
		TestRect(*this, TEXT("The last cell with an item"), GetRect(*Layout, Cells[13]), FVector2D(1748, 1016), FVector2D(84, 64));
		TestTrue(TEXT("A cell with an item shows"), IsShown(Cells[13]));
		TestFalse(TEXT("The cell past the items is item.empty, and hidden"), IsShown(Cells[14]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutBindingsTest, "WorldDivide.UI.Layout.Bindings", TestFlags)

bool FRTSLayoutBindingsTest::RunTest(const FString& Parameters)
{
	FTestBindings Bindings;
	Bindings.Progress = 0.25f;
	Bindings.Color = TEXT("#FF0000");
	Bindings.Mode = TEXT("Red");
	Bindings.Items = { TEXT("Alpha"), TEXT("Bravo") };
	URTSLayoutWidget* Layout = BuildLayout(*this, BindingLayout, TEXT("Bindings.layout.json"), Bindings);
	if (!Layout)
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Layout->TakeWidget();
	const UCommonTextBlock* Label = Cast<UCommonTextBlock>(Layout->FindElement(TEXT("Label")));
	const UProgressBar* Bar = Cast<UProgressBar>(Layout->FindElement(TEXT("Bar")));
	const UImage* Tinted = Cast<UImage>(Layout->FindElement(TEXT("Tinted")));
	if (!Label || !Bar || !Tinted)
	{
		AddError(TEXT("The layout lacks its Text, ProgressBar or Image"));
		return false;
	}

	TestEqual(TEXT("{key} fills in the value"), Label->GetText().ToString(), FString(TEXT("Credits: 100")));
	TestEqual(TEXT("bind.progress"), Bar->GetPercent(), 0.25f);
	TestEqual(TEXT("bind.color with #RRGGBB"), Tinted->GetColorAndOpacity(), FLinearColor(FColor::Red));
	TestTrue(TEXT("visibleWhen key, true"), IsShown(Layout->FindElement(TEXT("WhenOn"))));
	TestFalse(TEXT("visibleWhen !key, true"), IsShown(Layout->FindElement(TEXT("WhenOff"))));
	TestTrue(TEXT("visibleWhen key=value, equal"), IsShown(Layout->FindElement(TEXT("WhenRed"))));
	TestFalse(TEXT("visibleWhen key!=value, equal"), IsShown(Layout->FindElement(TEXT("WhenNotRed"))));

	const TArray<UWidget*> Names = Layout->GetRepeatItems(TEXT("Names"));
	TestEqual(TEXT("A copy of the template per count"), Names.Num(), 3);
	if (Names.Num() == 3)
	{
		TestEqual(TEXT("item.* reads the copy's item"), Cast<UCommonTextBlock>(Names[1])->GetText().ToString(), FString(TEXT("Bravo")));
		TestFalse(TEXT("The copy past the items is empty"), IsShown(Names[2]));
	}

	Bindings.Credits = 1250;
	Bindings.Progress = 0.75f;
	Bindings.Color = TEXT("#0000FF");
	Bindings.bOn = false;
	Bindings.Mode = TEXT("Blue");
	Bindings.Items.Add(TEXT("Charlie"));
	Layout->UpdateBindings();

	TestEqual(TEXT("{key} follows the value, formatted in the player's culture"), Label->GetText().ToString(),
		TEXT("Credits: ") + FText::AsNumber(1250).ToString());
	TestEqual(TEXT("bind.progress follows"), Bar->GetPercent(), 0.75f);
	TestEqual(TEXT("bind.color follows"), Tinted->GetColorAndOpacity(), FLinearColor(FColor::Blue));
	TestFalse(TEXT("visibleWhen key, false"), IsShown(Layout->FindElement(TEXT("WhenOn"))));
	TestTrue(TEXT("visibleWhen !key, false"), IsShown(Layout->FindElement(TEXT("WhenOff"))));
	TestFalse(TEXT("visibleWhen key=value, different"), IsShown(Layout->FindElement(TEXT("WhenRed"))));
	TestTrue(TEXT("visibleWhen key!=value, different"), IsShown(Layout->FindElement(TEXT("WhenNotRed"))));
	if (Names.Num() == 3)
	{
		TestTrue(TEXT("A new item fills an empty copy"), IsShown(Names[2]));
		TestEqual(TEXT("with its own text"), Cast<UCommonTextBlock>(Names[2])->GetText().ToString(), FString(TEXT("Charlie")));
	}

	Layout->RunAction(TEXT("Click"), FRTSUIContext());
	TestEqual(TEXT("An action reaches the source that declares it"), Bindings.Clicks, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutMaxLinesTest, "WorldDivide.UI.Layout.MaxLines", TestFlags)

bool FRTSLayoutMaxLinesTest::RunTest(const FString& Parameters)
{
	FTestBindings Bindings;
	Bindings.Mode = FString::Join(TArray<FString>({ TEXT("A long text that wraps over many lines in a narrow column,"),
		TEXT("far more than the two lines its maxLines allows, so the rest is cut off with an ellipsis"),
		TEXT("while the same text without maxLines keeps every line it needs.") }), TEXT(" "));
	URTSLayoutWidget* Layout = BuildLayout(*this, MaxLinesLayout, TEXT("MaxLines.layout.json"), Bindings);
	if (!Layout)
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Layout->TakeWidget();
	// The first pass gives the texts their width, the second wraps them to it.
	RTSTests::LayOutHeadless(*Layout, LayoutViewport);
	RTSTests::LayOutHeadless(*Layout, LayoutViewport);

	const URTSLayoutText* Limited = Cast<URTSLayoutText>(Layout->FindElement(TEXT("Limited")));
	const UWidget* Unlimited = Layout->FindElement(TEXT("Unlimited"));
	if (!Limited || !Unlimited)
	{
		AddError(TEXT("The layout lacks its texts"));
		return false;
	}
	const float TwoLines = Limited->GetMaxHeight();
	const float LimitedHeight = GetRect(*Layout, Limited).GetSize().Y;
	const float UnlimitedHeight = GetRect(*Layout, Unlimited).GetSize().Y;
	TestTrue(FString::Printf(TEXT("Two lines are some height (%.1f)"), TwoLines), TwoLines > 1.f);
	TestTrue(FString::Printf(TEXT("maxLines 2 is at most two lines high: %.1f of %.1f"), LimitedHeight, TwoLines), LimitedHeight <= TwoLines);
	TestTrue(FString::Printf(TEXT("and more than one: %.1f"), LimitedHeight), LimitedHeight > TwoLines / 2.f);
	TestTrue(FString::Printf(TEXT("Without maxLines the text takes more lines: %.1f"), UnlimitedHeight), UnlimitedHeight > TwoLines);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutSpriteSheetTest, "WorldDivide.UI.Layout.SpriteSheet", TestFlags)

bool FRTSLayoutSpriteSheetTest::RunTest(const FString& Parameters)
{
	// 8 frames of 64x64 in one row, played at 12 fps.
	URTSUIStyleSet* Styles = NewObject<URTSUIStyleSet>();
	FRTSUISprite& Sheet = Styles->Sprites.Add(TEXT("Sheet"));
	Sheet.Brush.ImageSize = FVector2D(512, 64);
	Sheet.Frames = 8;
	Sheet.FrameSize = FIntPoint(64, 64);
	Sheet.Fps = 12.f;

	FTestBindings Bindings;
	URTSLayoutWidget* Layout = BuildLayout(*this, SpriteSheetLayout, TEXT("SpriteSheet.layout.json"), Bindings, Styles);
	const UImage* Spinner = Layout ? Cast<UImage>(Layout->FindElement(TEXT("Spinner"))) : nullptr;
	if (!Spinner)
	{
		AddError(TEXT("The layout lacks its Image"));
		return false;
	}
	// The part of the sheet the image shows, from left to right edge.
	const auto TestFrame = [this, Spinner](const TCHAR* What, float Left, float Right)
	{
		const FBox2f Region = Spinner->GetBrush().GetUVRegion();
		TestTrue(FString::Printf(TEXT("%s: shows %s, expected [%.3f, %.3f] x [0, 1]"), What, *Region.ToString(), Left, Right),
			Region.Min.Equals(FVector2f(Left, 0.f)) && Region.Max.Equals(FVector2f(Right, 1.f)));
	};
	const FVector2D ImageSize = Spinner->GetBrush().GetImageSize();
	TestTrue(FString::Printf(TEXT("The image is one frame big, not %s"), *ImageSize.ToString()), ImageSize.Equals(FVector2D(64, 64)));
	TestFrame(TEXT("It starts on the first frame"), 0.f, 0.125f);

	Layout->Animate(0.25f);
	TestFrame(TEXT("After 0.25 s at 12 fps it shows frame 3"), 0.375f, 0.5f);
	Layout->Animate(0.5f);
	TestFrame(TEXT("and after 0.75 s it has looped to frame 1"), 0.125f, 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutTeamColorTest, "WorldDivide.UI.Layout.TeamColor", TestFlags)

bool FRTSLayoutTeamColorTest::RunTest(const FString& Parameters)
{
	FTestBindings Bindings;
	Bindings.Items = { TEXT("Alpha"), TEXT("Bravo") };
	Bindings.TeamColors = { TEXT("#FF0000"), TEXT("#0000FF") };
	URTSLayoutWidget* Layout = BuildLayout(*this, TeamColorLayout, TEXT("TeamColor.layout.json"), Bindings);
	if (!Layout)
	{
		return false;
	}
	const TArray<UWidget*> Players = Layout->GetRepeatItems(TEXT("Players"));
	const UImage* Mine = Cast<UImage>(Layout->FindElement(TEXT("Mine")));
	if (Players.Num() != 2 || !Mine)
	{
		AddError(TEXT("The layout lacks its copies or its Image"));
		return false;
	}
	// The copies sit in size boxes; their images are inside.
	const auto GetColor = [&Players](int32 Item)
	{
		return CastChecked<UImage>(CastChecked<UPanelWidget>(Players[Item])->GetChildAt(0))->GetColorAndOpacity();
	};
	TestEqual(TEXT("Team in a template takes the first item's team colour"), GetColor(0), FLinearColor(FColor::Red));
	TestEqual(TEXT("and the second item's"), GetColor(1), FLinearColor(FColor::Blue));
	TestEqual(TEXT("Team outside a template without a player is white"), Mine->GetColorAndOpacity(), FLinearColor::White);

	Bindings.TeamColors = { TEXT("#00FF00"), TEXT("#0000FF") };
	Layout->UpdateBindings();
	TestEqual(TEXT("An item's new team colour follows"), GetColor(0), FLinearColor(FColor::Green));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutUnknownKeyTest,"WorldDivide.UI.Layout.UnknownKey", TestFlags)

bool FRTSLayoutUnknownKeyTest::RunTest(const FString& Parameters)
{
	// Once per key and action, however often they are read.
	AddExpectedMessage(TEXT("unknown binding key \"test.missing\""), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	AddExpectedMessage(TEXT("unknown action \"Missing\""), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);

	FTestBindings Bindings;
	URTSLayoutWidget* Layout = BuildLayout(*this, UnknownLayout, TEXT("Unknown.layout.json"), Bindings);
	if (!Layout)
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Layout->TakeWidget();
	Layout->UpdateBindings();
	Layout->UpdateBindings();
	Layout->RunAction(TEXT("Missing"), FRTSUIContext());
	Layout->RunAction(TEXT("Missing"), FRTSUIContext());

	const UCommonTextBlock* Ghost = Cast<UCommonTextBlock>(Layout->FindElement(TEXT("Ghost")));
	TestTrue(TEXT("An element with an unknown key still shows"), IsShown(Ghost));
	TestEqual(TEXT("and shows the key it lacks"), Ghost ? Ghost->GetText().ToString() : FString(), FString(TEXT("{test.missing}")));
	TestTrue(TEXT("A button with an unknown action still shows"), IsShown(Layout->FindElement(TEXT("Press"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutModOverrideTest, "WorldDivide.UI.Layout.ModOverride", TestFlags)

bool FRTSLayoutModOverrideTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("LayoutModOverride"));
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	const FString Base = FPaths::Combine(Dir, TEXT("UI"), TEXT("Layouts"));
	const FString ModA = FPaths::Combine(Dir, TEXT("Mods"), TEXT("A"), TEXT("UI"), TEXT("Layouts"));
	const FString ModB = FPaths::Combine(Dir, TEXT("Mods"), TEXT("B"), TEXT("UI"), TEXT("Layouts"));
	WriteFile(FPaths::Combine(Base, TEXT("Panel.layout.json")), TEXT(R"({ "name": "FromBase", "type": "Panel" })"));
	WriteFile(FPaths::Combine(Base, TEXT("Other.layout.json")), TEXT(R"({ "name": "OtherBase", "type": "Panel" })"));
	WriteFile(FPaths::Combine(ModA, TEXT("Panel.layout.json")), TEXT(R"({ "name": "FromA", "type": "Panel" })"));
	WriteFile(FPaths::Combine(ModB, TEXT("Panel.layout.json")), TEXT(R"({ "name": "FromB", "type": "Panel" })"));

	const TArray<FString> Roots = { Base, ModA, ModB };
	const TMap<FString, FString> Files = RTSLayoutFiles::FindFiles(Roots);
	TestEqual(TEXT("Both layouts are found"), Files.Num(), 2);

	FString Error;
	const TSharedPtr<FJsonObject> Panel = RTSLayoutFiles::Load(Roots, TEXT("Panel"), Error);
	TestEqual(TEXT("The last mod's layout replaces the base one and the earlier mod's"),
		Panel ? Panel->GetStringField(TEXT("name")) : FString(), FString(TEXT("FromB")));
	const TSharedPtr<FJsonObject> Other = RTSLayoutFiles::Load(Roots, TEXT("Other"), Error);
	TestEqual(TEXT("A layout no mod replaces stays the base one"), Other ? Other->GetStringField(TEXT("name")) : FString(), FString(TEXT("OtherBase")));
	TestNull(TEXT("A missing layout loads nothing"), RTSLayoutFiles::Load(Roots, TEXT("Missing"), Error).Get());

	const TArray<FString> GameRoots = RTSLayoutFiles::GetRoots();
	TestTrue(TEXT("The game's layouts come from UI/Layouts first"), !GameRoots.IsEmpty() && GameRoots[0].EndsWith(TEXT("UI/Layouts")));

	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLayoutValidateTest, "WorldDivide.UI.Validate", TestFlags)

bool FRTSLayoutValidateTest::RunTest(const FString& Parameters)
{
	const FRTSLayoutValidationContext Context = FRTSLayoutValidationContext::MakeDefault();
	TestTrue(TEXT("The generated style sets are loaded"), Context.Sprites.Contains(TEXT("Panel")) && Context.Fonts.Contains(TEXT("Body")));

	// Every committed layout, and those of any mod present.
	const TMap<FString, FString> Files = RTSLayoutFiles::FindFiles(RTSLayoutFiles::GetRoots());
	TestTrue(TEXT("The HUD layout exists"), Files.Contains(TEXT("HUD")));
	for (const TPair<FString, FString>& File : Files)
	{
		FString Text;
		TArray<FString> Errors;
		if (!FFileHelper::LoadFileToString(Text, *File.Value))
		{
			AddError(FString::Printf(TEXT("Could not read %s"), *File.Value));
			continue;
		}
		RTSLayout::Validate(Text, FPaths::GetCleanFilename(File.Value), Context, Errors);
		for (const FString& Error : Errors)
		{
			AddError(Error);
		}
	}

	TArray<FString> Errors;
	TestFalse(TEXT("A layout with a broken sprite reference fails"), RTSLayout::Validate(BrokenLayout, TEXT("Broken.layout.json"), Context, Errors));
	const FString All = FString::Join(Errors, TEXT("\n"));
	const TCHAR* Expected[] = {
		TEXT("Broken.layout.json: Icon: unknown sprite \"NoSuchSprite\""),
		TEXT("Broken.layout.json: Thing: unknown type \"Widget\""),
		TEXT("Broken.layout.json: Label: unknown property \"fonts\" for Text"),
		TEXT("Broken.layout.json: Label: unknown binding key \"no.such.key\""),
		TEXT("Broken.layout.json: Label: the name is used twice in this file"),
		TEXT("Broken.layout.json: Label: unknown font \"Huge\""),
		TEXT("Broken.layout.json: Label: unknown colour \"Purple\""),
		TEXT("Broken.layout.json: Press: unknown action \"NoSuchAction\""),
		TEXT("Broken.layout.json: Hole: unknown layout \"NoSuchLayout\""),
	};
	for (const TCHAR* Message : Expected)
	{
		TestTrue(FString::Printf(TEXT("Reports %s"), Message), All.Contains(Message));
	}

	Errors.Reset();
	TestFalse(TEXT("A file that does not parse fails"), RTSLayout::Validate(TEXT("{ \"type\": \"Panel\", }x"), TEXT("Bad.layout.json"), Context, Errors));
	TestTrue(TEXT("naming the file"), Errors.Num() == 1 && Errors[0].StartsWith(TEXT("Bad.layout.json: ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUIBindingPanelsTest, "WorldDivide.UI.Bindings.Panels", TestFlags)

bool FRTSUIBindingPanelsTest::RunTest(const FString& Parameters)
{
	const TMap<FString, FString> Files = RTSLayoutFiles::FindFiles(RTSLayoutFiles::GetRoots());
	const IRTSUIBindingSource* ResourceBar = nullptr;
	for (const IRTSUIBindingSource* Source : RTSUIBindings::GetDefaultSources())
	{
		const TArray<FName> Panels = Source->GetPanels();
		TestFalse(TEXT("Every binding source serves a panel"), Panels.IsEmpty());
		for (const FName Panel : Panels)
		{
			TestTrue(FString::Printf(TEXT("The panel %s is a layout file"), *Panel.ToString()), Files.Contains(Panel.ToString()));
			if (Panel == TEXT("ResourceBar"))
			{
				ResourceBar = Source;
			}
		}
	}
	if (!TestNotNull(TEXT("A source serves the resource bar"), ResourceBar))
	{
		return false;
	}
	FRTSUIDeclarations Declarations;
	ResourceBar->Declare(Declarations);
	for (const TCHAR* Key : { TEXT("player.credits"), TEXT("player.power.produced"), TEXT("player.power.consumed"), TEXT("player.power.low"),
		TEXT("player.power.color"), TEXT("player.power.ratio"), TEXT("player.faction") })
	{
		TestTrue(FString::Printf(TEXT("The resource bar's source declares %s"), Key), Declarations.Keys.Contains(FName(Key)));
	}
	TestFalse(TEXT("Every faction uses power, so the resource bar has no usesPower"), Declarations.Keys.Contains(FName(TEXT("player.usesPower"))));
	TestTrue(TEXT("The resource bar's source declares the rank list"), Declarations.Lists.Contains(FName(TEXT("player.rank"))));

	const FString Json = URTSUICommandlet::DescribeBindings(FRTSLayoutValidationContext::MakeDefault());
	TestTrue(TEXT("The bindings file lists the resource bar's keys"), Json.Contains(TEXT("\"player.credits\"")) && Json.Contains(TEXT("\"ResourceBar\"")));
	TestTrue(TEXT("the commander bar and the map buttons"), Json.Contains(TEXT("\"CommanderBar\"")) && Json.Contains(TEXT("\"MapButtons\"")));
	TestTrue(TEXT("and the engine panels"), Json.Contains(TEXT("\"Minimap\"")) && Json.Contains(TEXT("\"Upgrades\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUIPowerRatioTest, "WorldDivide.UI.Bindings.PowerRatio", TestFlags)

bool FRTSUIPowerRatioTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Normal: consumed over produced"), RTSUIBindings::GetPowerRatio(100, 40), 0.4f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("At the limit the bar is full"), RTSUIBindings::GetPowerRatio(100, 100), 1.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Low power stays full, not over"), RTSUIBindings::GetPowerRatio(50, 120), 1.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("No production but a load is full"), RTSUIBindings::GetPowerRatio(0, 30), 1.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("No production and no load is empty"), RTSUIBindings::GetPowerRatio(0, 0), 0.f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Production without load is empty"), RTSUIBindings::GetPowerRatio(80, 0), 0.f, KINDA_SMALL_NUMBER);

	// Without a player the key still answers, as an empty bar.
	FRTSUIValue Value;
	bool bAnswered = false;
	for (const IRTSUIBindingSource* Source : RTSUIBindings::GetDefaultSources())
	{
		bAnswered |= Source->GetValue(TEXT("player.power.ratio"), FRTSUIContext(), Value);
	}
	TestTrue(TEXT("A source answers player.power.ratio"), bAnswered);
	TestTrue(TEXT("as a fill"), Value.Type == FRTSUIValue::EType::Progress);
	TestEqual(TEXT("empty without a player"), Value.Number, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUIBindingStubsTest, "WorldDivide.UI.Bindings.Stubs", TestFlags)

bool FRTSUIBindingStubsTest::RunTest(const FString& Parameters)
{
	auto FindSource = [](const TCHAR* Panel) -> const IRTSUIBindingSource* {
		for (const IRTSUIBindingSource* Source : RTSUIBindings::GetDefaultSources())
		{
			if (Source->GetPanels().Contains(FName(Panel)))
			{
				return Source;
			}
		}
		return nullptr;
	};
	auto ListValue = [](const IRTSUIBindingSource& Source, const TCHAR* List, int32 Index, const TCHAR* Key) {
		FRTSUIContext Context;
		Context.List = List;
		Context.Index = Index;
		FRTSUIValue Value;
		Source.GetListValue(Key, Context, Value);
		return Value;
	};
	const IRTSUIBindingSource* ResourceBar = FindSource(TEXT("ResourceBar"));
	const IRTSUIBindingSource* Commander = FindSource(TEXT("CommanderBar"));
	const IRTSUIBindingSource* MapButtons = FindSource(TEXT("MapButtons"));
	if (!TestNotNull(TEXT("A source serves the resource bar"), ResourceBar) || !TestNotNull(TEXT("the commander bar"), Commander)
		|| !TestNotNull(TEXT("and the map buttons"), MapButtons))
	{
		return false;
	}
	const FRTSUIContext NoPlayer;

	// Rank comes in M5: five stars, none earned.
	TestEqual(TEXT("The rank has five stars"), ResourceBar->GetListCount(TEXT("player.rank"), NoPlayer), 5);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FRTSUIValue Earned = ListValue(*ResourceBar, TEXT("player.rank"), Index, TEXT("earned"));
		TestTrue(FString::Printf(TEXT("Star %d answers earned as a flag"), Index), Earned.Type == FRTSUIValue::EType::Bool);
		TestFalse(FString::Printf(TEXT("Star %d is not earned"), Index), Earned.bBool);
	}

	// Generals come in #73, their powers in M5: the faction's name, the placeholder portrait and nine locked slots.
	FRTSUIValue Portrait;
	TestTrue(TEXT("The commander bar answers its portrait"), Commander->GetValue(TEXT("commander.portrait"), NoPlayer, Portrait));
	TestTrue(TEXT("as a sprite"), Portrait.Type == FRTSUIValue::EType::Sprite);
	TestEqual(TEXT("the placeholder portrait"), Portrait.String, FString(TEXT("Portrait.Placeholder")));
	FRTSUIValue Name;
	TestTrue(TEXT("The commander bar answers its name"), Commander->GetValue(TEXT("commander.name"), NoPlayer, Name));
	TestTrue(TEXT("empty without a player"), Name.String.IsEmpty());
	TestEqual(TEXT("There are nine power slots"), Commander->GetListCount(TEXT("commander.powers"), NoPlayer), 9);
	for (int32 Index = 0; Index < 9; ++Index)
	{
		TestTrue(FString::Printf(TEXT("Slot %d is locked"), Index), ListValue(*Commander, TEXT("commander.powers"), Index, TEXT("locked")).bBool);
		TestFalse(FString::Printf(TEXT("Slot %d is not ready"), Index), ListValue(*Commander, TEXT("commander.powers"), Index, TEXT("ready")).bBool);
		TestEqual(FString::Printf(TEXT("Slot %d has no charge"), Index), ListValue(*Commander, TEXT("commander.powers"), Index, TEXT("charge")).Number, 0.0);
		TestFalse(FString::Printf(TEXT("Slot %d has an icon"), Index), ListValue(*Commander, TEXT("commander.powers"), Index, TEXT("icon")).String.IsEmpty());
	}
	TestTrue(TEXT("UsePower is the commander bar's action"), Commander->HandleAction(TEXT("UsePower"), NoPlayer));

	for (const TCHAR* Key : { TEXT("map.overlays.enabled"), TEXT("map.powers.enabled"), TEXT("map.objectives.enabled") })
	{
		FRTSUIValue Enabled;
		TestTrue(FString::Printf(TEXT("The map buttons answer %s"), Key), MapButtons->GetValue(Key, NoPlayer, Enabled));
		TestFalse(FString::Printf(TEXT("%s is false until its issue"), Key), Enabled.IsTruthy());
	}
	for (const TCHAR* Action : { TEXT("OpenOverlays"), TEXT("OpenGeneralPowers"), TEXT("OpenObjectives") })
	{
		TestTrue(FString::Printf(TEXT("%s is the map buttons' action"), Action), MapButtons->HandleAction(Action, NoPlayer));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUICheckFolderTest, "WorldDivide.UI.Commandlet.Validate", TestFlags)

bool FRTSUICheckFolderTest::RunTest(const FString& Parameters)
{
	// An unpacked delivery: a layout naming a sprite and a layout that exist only in the delivery, and a key that exists nowhere.
	const FString Dir = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("UICheck"), TEXT("UI"));
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	WriteFile(FPaths::Combine(Dir, TEXT("Sprites"), TEXT("USA.sprites.json")),
		TEXT(R"({ "sprites": { "USA.DeliveryOnly": { "texture": "Textures/USA/T_UI_USA_DeliveryOnly.png", "size": [64, 64] } } })"));
	WriteFile(FPaths::Combine(Dir, TEXT("Layouts"), TEXT("Extra.layout.json")), TEXT(R"({ "name": "Extra", "type": "Panel" })"));
	WriteFile(FPaths::Combine(Dir, TEXT("Layouts"), TEXT("Delivered.layout.json")), TEXT(R"({ "name": "Delivered", "type": "Panel", "children": [
		{ "name": "Icon", "type": "Image", "sprite": "DeliveryOnly" },
		{ "name": "Label", "type": "Text", "text": "{no.such.key}" },
		{ "name": "Hole", "type": "Slot", "layout": "Extra" } ] })"));

	FRTSLayoutValidationContext Context = FRTSLayoutValidationContext::MakeDefault();
	Context.AddFolder(Dir);
	TArray<FString> Errors;
	TestFalse(TEXT("The delivery has a problem"), RTSLayout::ValidateFolder(Dir, Context, Errors));
	const FString All = FString::Join(Errors, TEXT("\n"));
	TestTrue(TEXT("The unknown key is reported with its file and element"), All.Contains(TEXT("Delivered.layout.json: Label: unknown binding key \"no.such.key\"")));
	TestFalse(TEXT("A sprite of the delivery's sprite file is known"), All.Contains(TEXT("DeliveryOnly")));
	TestFalse(TEXT("A layout of the delivery is known"), All.Contains(TEXT("Extra")));
	TestEqual(TEXT("Nothing else is reported"), Errors.Num(), 1);

	IFileManager::Get().DeleteDirectory(*FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("UICheck")), false, true);
	return true;
}

#endif
