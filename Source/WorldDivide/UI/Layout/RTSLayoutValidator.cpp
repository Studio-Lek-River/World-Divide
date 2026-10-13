#include "UI/Layout/RTSLayoutValidator.h"

#include "Core/RTSSettings.h"
#include "Data/RTSFactionData.h"
#include "Dom/JsonObject.h"
#include "UI/Layout/RTSLayoutElement.h"
#include "UI/Layout/RTSLayoutFiles.h"
#include "UI/Layout/RTSLayoutWidget.h"
#include "UI/RTSUIStyleSet.h"

void FRTSLayoutValidationContext::AddStyleSet(const URTSUIStyleSet& StyleSet)
{
	for (const TPair<FName, FRTSUISprite>& Sprite : StyleSet.Sprites)
	{
		Sprites.Add(Sprite.Key);
	}
	for (const TPair<FName, FLinearColor>& Color : StyleSet.Colors)
	{
		Colors.Add(Color.Key);
	}
	// Keyed "<Font>.<Color>".
	for (const TPair<FName, TSubclassOf<UCommonTextStyle>>& TextStyle : StyleSet.TextStyles)
	{
		FString Font;
		FString Color;
		if (TextStyle.Key.ToString().Split(TEXT("."), &Font, &Color))
		{
			Fonts.Add(FName(*Font));
		}
	}
}

FRTSLayoutValidationContext FRTSLayoutValidationContext::MakeDefault()
{
	FRTSLayoutValidationContext Context;
	const URTSSettings* Settings = GetDefault<URTSSettings>();
	TArray<TSoftObjectPtr<URTSUIStyleSet>> StyleSets = { Settings->CommonUIStyle, Settings->NeutralUIStyle };
	for (const TSoftObjectPtr<URTSFactionData>& Faction : Settings->Factions)
	{
		if (const URTSFactionData* Loaded = Faction.LoadSynchronous())
		{
			StyleSets.Add(Loaded->UIStyle);
		}
	}
	for (const TSoftObjectPtr<URTSUIStyleSet>& StyleSet : StyleSets)
	{
		if (const URTSUIStyleSet* Loaded = StyleSet.LoadSynchronous())
		{
			Context.AddStyleSet(*Loaded);
		}
	}
	for (const TPair<FString, FString>& Layout : RTSLayoutFiles::FindFiles(RTSLayoutFiles::GetRoots()))
	{
		Context.Layouts.Add(Layout.Key);
	}
	Context.Widgets.Append(RTSLayout::GetPanelNames());
	Context.Declarations = RTSUIBindings::Declare(RTSUIBindings::GetDefaultSources());
	return Context;
}

namespace
{
	class FValidator
	{
	public:
		FValidator(const FString& InFile, const FRTSLayoutValidationContext& InContext, TArray<FString>& InErrors)
			: File(InFile)
			, Context(InContext)
			, Errors(InErrors)
		{
		}

		void Check(const FRTSLayoutElement& Element, FName List)
		{
			if (!Element.Name.IsEmpty())
			{
				if (Names.Contains(Element.Name))
				{
					Error(Element, TEXT("the name is used twice in this file"));
				}
				Names.Add(Element.Name);
			}

			CheckSprite(Element, Element.Sprite);
			CheckSprite(Element, Element.FillSprite);
			if (!Element.Font.IsNone() && !Context.Fonts.Contains(Element.Font))
			{
				Error(Element, FString::Printf(TEXT("unknown font \"%s\""), *Element.Font.ToString()));
			}
			CheckColor(Element, Element.Color);
			CheckColor(Element, Element.FillColor);
			if (!Element.Layout.IsEmpty() && !Context.Layouts.Contains(Element.Layout))
			{
				Error(Element, FString::Printf(TEXT("unknown layout \"%s\""), *Element.Layout));
			}
			if (!Element.Widget.IsNone() && !Context.Widgets.Contains(Element.Widget))
			{
				Error(Element, FString::Printf(TEXT("unknown widget \"%s\""), *Element.Widget.ToString()));
			}
			if (!Element.Action.IsNone() && !Context.Declarations.Actions.Contains(Element.Action))
			{
				Error(Element, FString::Printf(TEXT("unknown action \"%s\""), *Element.Action.ToString()));
			}
			for (const FString& Modal : Element.Modals)
			{
				if (!Context.Layouts.Contains(Modal))
				{
					Error(Element, FString::Printf(TEXT("unknown modal layout \"%s\""), *Modal));
				}
			}

			TArray<FName> Keys;
			Element.GetKeys(Keys);
			for (const FName Key : Keys)
			{
				CheckKey(Element, Key, List);
			}

			for (const FRTSLayoutElement& Child : Element.Children)
			{
				Check(Child, List);
			}
			if (!Element.Repeat.IsNone())
			{
				if (!Context.Declarations.Lists.Contains(Element.Repeat))
				{
					Error(Element, FString::Printf(TEXT("unknown list \"%s\""), *Element.Repeat.ToString()));
				}
				if (Element.Template)
				{
					Check(*Element.Template, Element.Repeat);
				}
			}
		}

	private:
		void Error(const FRTSLayoutElement& Element, const FString& Message)
		{
			Errors.Add(RTSLayout::FormatError(File, Element.Label, Message));
		}

		void CheckSprite(const FRTSLayoutElement& Element, FName Sprite)
		{
			if (!Sprite.IsNone() && !Context.Sprites.Contains(Sprite))
			{
				Error(Element, FString::Printf(TEXT("unknown sprite \"%s\""), *Sprite.ToString()));
			}
		}

		void CheckColor(const FRTSLayoutElement& Element, const FString& Color)
		{
			FLinearColor Hex;
			if (Color.IsEmpty() || Color == RTSLayout::TeamColor || RTSLayout::ParseHexColor(Color, Hex))
			{
				return;
			}
			if (Color.StartsWith(TEXT("#")) || !Context.Colors.Contains(FName(*Color)))
			{
				Error(Element, FString::Printf(TEXT("unknown colour \"%s\""), *Color));
			}
		}

		void CheckKey(const FRTSLayoutElement& Element, FName Key, FName List)
		{
			const FString KeyText = Key.ToString();
			if (!KeyText.StartsWith(RTSLayout::ItemPrefix))
			{
				if (!Context.Declarations.Keys.Contains(Key))
				{
					Error(Element, FString::Printf(TEXT("unknown binding key \"%s\""), *KeyText));
				}
				return;
			}
			if (List.IsNone())
			{
				Error(Element, FString::Printf(TEXT("\"%s\" outside a repeat template"), *KeyText));
				return;
			}
			const TSet<FName>* ItemKeys = Context.Declarations.Lists.Find(List);
			const FName ItemKey(*KeyText.RightChop(FCString::Strlen(RTSLayout::ItemPrefix)));
			if (Key != RTSLayout::ItemEmptyKey && ItemKeys && !ItemKeys->Contains(ItemKey))
			{
				Error(Element, FString::Printf(TEXT("unknown item key \"%s\" in list \"%s\""), *KeyText, *List.ToString()));
			}
		}

		const FString& File;
		const FRTSLayoutValidationContext& Context;
		TArray<FString>& Errors;
		TSet<FString> Names;
	};
}

bool RTSLayout::Validate(const FString& Text, const FString& File, const FRTSLayoutValidationContext& Context, TArray<FString>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	FString ParseError;
	const TSharedPtr<FJsonObject> Object = RTSLayoutFiles::Parse(Text, ParseError);
	if (!Object)
	{
		OutErrors.Add(FormatError(File, TEXT("file"), ParseError));
		return false;
	}
	FRTSLayoutElement Root;
	Parse(*Object, File, Root, OutErrors);
	FValidator(File, Context, OutErrors).Check(Root, NAME_None);
	return OutErrors.Num() == ErrorsBefore;
}
