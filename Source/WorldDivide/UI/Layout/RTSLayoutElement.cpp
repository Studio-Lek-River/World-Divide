#include "UI/Layout/RTSLayoutElement.h"

#include "Dom/JsonObject.h"

const TCHAR* RTSLayout::ItemPrefix = TEXT("item.");
const FName RTSLayout::ItemEmptyKey(TEXT("item.empty"));
const TCHAR* RTSLayout::TeamColor = TEXT("Team");

namespace
{
	struct FTypeInfo
	{
		const TCHAR* Name;
		ERTSLayoutType Type;
		bool bContainer;
		TArray<const TCHAR*> Properties;
	};

	const TArray<FTypeInfo>& GetTypes()
	{
		static const TArray<FTypeInfo> Types = {
			{ TEXT("Panel"), ERTSLayoutType::Panel, true, { TEXT("sprite") } },
			{ TEXT("HBox"), ERTSLayoutType::HBox, true, { TEXT("spacing"), TEXT("align") } },
			{ TEXT("VBox"), ERTSLayoutType::VBox, true, { TEXT("spacing"), TEXT("align") } },
			{ TEXT("Grid"), ERTSLayoutType::Grid, true, { TEXT("columns"), TEXT("rows"), TEXT("cellSize"), TEXT("spacing") } },
			{ TEXT("Image"), ERTSLayoutType::Image, false, { TEXT("sprite"), TEXT("tile") } },
			{ TEXT("Text"), ERTSLayoutType::Text, false,
				{ TEXT("text"), TEXT("font"), TEXT("fontSize"), TEXT("align"), TEXT("wrap"), TEXT("maxLines") } },
			{ TEXT("Button"), ERTSLayoutType::Button, false, { TEXT("sprite"), TEXT("action"), TEXT("tooltip") } },
			{ TEXT("ProgressBar"), ERTSLayoutType::ProgressBar, false, { TEXT("sprite"), TEXT("fillSprite"), TEXT("fillColor") } },
			{ TEXT("Slot"), ERTSLayoutType::Slot, false, { TEXT("layout"), TEXT("widget") } },
		};
		return Types;
	}

	const TCHAR* CommonProperties[] = {
		TEXT("name"), TEXT("type"), TEXT("anchor"), TEXT("position"), TEXT("size"), TEXT("padding"), TEXT("color"),
		TEXT("opacity"), TEXT("hitTest"), TEXT("visibleWhen"), TEXT("bind"),
	};

	const TCHAR* ContainerProperties[] = { TEXT("children"), TEXT("repeat"), TEXT("template"), TEXT("count") };

	const TCHAR* BindProperties[] = { TEXT("progress"), TEXT("sprite"), TEXT("enabled"), TEXT("selected"), TEXT("color") };

	const TCHAR* AnchorNames[] = {
		TEXT("TopLeft"), TEXT("Top"), TEXT("TopRight"), TEXT("Left"), TEXT("Center"), TEXT("Right"), TEXT("BottomLeft"),
		TEXT("Bottom"), TEXT("BottomRight"), TEXT("TopStretch"), TEXT("BottomStretch"), TEXT("LeftStretch"),
		TEXT("RightStretch"), TEXT("Fill"),
	};

	/** Reads one element; the context for the errors of one file. */
	class FParser
	{
	public:
		FParser(const FString& InFile, TArray<FString>& InErrors)
			: File(InFile)
			, Errors(InErrors)
		{
		}

		void ParseElement(const FJsonObject& Object, const FString& DefaultLabel, bool bRoot, FRTSLayoutElement& Out)
		{
			Out.Label = DefaultLabel;
			if (Object.HasField(TEXT("name")) && ReadString(Object, TEXT("name"), Out.Name, Out))
			{
				Out.Label = Out.Name;
			}

			FString Type;
			const FTypeInfo* Info = nullptr;
			if (!Object.TryGetStringField(TEXT("type"), Type))
			{
				Error(Out, TEXT("has no type"));
			}
			else
			{
				Info = GetTypes().FindByPredicate([&Type](const FTypeInfo& Candidate) { return Type == Candidate.Name; });
				if (!Info)
				{
					Error(Out, FString::Printf(TEXT("unknown type \"%s\""), *Type));
				}
			}
			if (Info)
			{
				Out.Type = Info->Type;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
			{
				const bool bKnown = IsIn(Field.Key, CommonProperties)
					|| (Info && Info->Properties.ContainsByPredicate([&Field](const TCHAR* Name) { return Field.Key == Name; }))
					|| (Info && Info->bContainer && IsIn(Field.Key, ContainerProperties))
					|| (Info && Info->Type == ERTSLayoutType::Button && Field.Key == TEXT("children"))
					|| (bRoot && Field.Key == TEXT("modals"));
				if (!bKnown && Info)
				{
					Error(Out, FString::Printf(TEXT("unknown property \"%s\" for %s"), *Field.Key, Info->Name));
				}
			}

			ReadCommon(Object, Out);
			ReadSpecific(Object, Out);

			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (Object.TryGetArrayField(TEXT("children"), Children))
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					const TSharedPtr<FJsonObject>* Child = nullptr;
					FRTSLayoutElement& Element = Out.Children.AddDefaulted_GetRef();
					if (!(*Children)[Index]->TryGetObject(Child))
					{
						Error(Out, FString::Printf(TEXT("child %d is no object"), Index));
						Out.Children.Pop();
						continue;
					}
					FString ChildType;
					(*Child)->TryGetStringField(TEXT("type"), ChildType);
					ParseElement(**Child, FString::Printf(TEXT("%s/%s#%d"), *Out.Label, *ChildType, Index), false, Element);
				}
			}

			if (Object.HasField(TEXT("repeat")))
			{
				FString Repeat;
				ReadString(Object, TEXT("repeat"), Repeat, Out);
				Out.Repeat = FName(*Repeat);
				const TSharedPtr<FJsonObject>* Template = nullptr;
				if (!Object.TryGetObjectField(TEXT("template"), Template))
				{
					Error(Out, TEXT("has repeat but no template object"));
				}
				else
				{
					Out.Template = MakeShared<FRTSLayoutElement>();
					ParseElement(**Template, Out.Label + TEXT("/template"), false, *Out.Template);
				}
				if (Object.HasField(TEXT("count")))
				{
					ReadInt(Object, TEXT("count"), Out.Count, Out);
				}
			}
			else if (Object.HasField(TEXT("template")) || Object.HasField(TEXT("count")))
			{
				Error(Out, TEXT("has a template or count without repeat"));
			}

			if (bRoot && Object.HasField(TEXT("modals")))
			{
				const TArray<TSharedPtr<FJsonValue>>* Modals = nullptr;
				if (Object.TryGetArrayField(TEXT("modals"), Modals))
				{
					for (const TSharedPtr<FJsonValue>& Modal : *Modals)
					{
						FString Name;
						if (Modal->TryGetString(Name))
						{
							Out.Modals.Add(Name);
						}
						else
						{
							Error(Out, TEXT("modals holds a value that is no layout name"));
						}
					}
				}
				else
				{
					Error(Out, TEXT("modals is no list of layout names"));
				}
			}
		}

	private:
		template <int32 N>
		static bool IsIn(const FString& Key, const TCHAR* (&Names)[N])
		{
			for (const TCHAR* Name : Names)
			{
				if (Key == Name)
				{
					return true;
				}
			}
			return false;
		}

		void Error(const FRTSLayoutElement& Element, const FString& Message)
		{
			Errors.Add(RTSLayout::FormatError(File, Element.Label, Message));
		}

		bool ReadString(const FJsonObject& Object, const TCHAR* Field, FString& Out, const FRTSLayoutElement& Element)
		{
			if (!Object.TryGetStringField(Field, Out))
			{
				Error(Element, FString::Printf(TEXT("%s is no text"), Field));
				return false;
			}
			return true;
		}

		bool ReadName(const FJsonObject& Object, const TCHAR* Field, FName& Out, const FRTSLayoutElement& Element)
		{
			FString Text;
			if (Object.HasField(Field) && ReadString(Object, Field, Text, Element))
			{
				Out = FName(*Text);
				return true;
			}
			return false;
		}

		bool ReadNumber(const FJsonObject& Object, const TCHAR* Field, float& Out, const FRTSLayoutElement& Element)
		{
			double Number = 0.0;
			if (!Object.TryGetNumberField(Field, Number))
			{
				Error(Element, FString::Printf(TEXT("%s is no number"), Field));
				return false;
			}
			Out = static_cast<float>(Number);
			return true;
		}

		bool ReadInt(const FJsonObject& Object, const TCHAR* Field, int32& Out, const FRTSLayoutElement& Element)
		{
			float Number = 0.f;
			if (!ReadNumber(Object, Field, Number, Element))
			{
				return false;
			}
			Out = FMath::RoundToInt(Number);
			return true;
		}

		bool ReadBool(const FJsonObject& Object, const TCHAR* Field, bool& Out, const FRTSLayoutElement& Element)
		{
			if (!Object.TryGetBoolField(Field, Out))
			{
				Error(Element, FString::Printf(TEXT("%s is not true or false"), Field));
				return false;
			}
			return true;
		}

		bool ReadNumbers(const FJsonObject& Object, const TCHAR* Field, int32 Count, TArray<double>& Out, const FRTSLayoutElement& Element)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			Out.Reset();
			if (Object.TryGetArrayField(Field, Values) && Values->Num() == Count)
			{
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					double Number = 0.0;
					if (!Value->TryGetNumber(Number))
					{
						break;
					}
					Out.Add(Number);
				}
			}
			if (Out.Num() != Count)
			{
				Error(Element, FString::Printf(TEXT("%s is not %d numbers"), Field, Count));
				return false;
			}
			return true;
		}

		bool ReadVector(const FJsonObject& Object, const TCHAR* Field, FVector2D& Out, const FRTSLayoutElement& Element)
		{
			TArray<double> Numbers;
			if (!Object.HasField(Field) || !ReadNumbers(Object, Field, 2, Numbers, Element))
			{
				return false;
			}
			Out = FVector2D(Numbers[0], Numbers[1]);
			return true;
		}

		void ReadCondition(const FString& Text, FRTSLayoutCondition& Out, const FRTSLayoutElement& Element)
		{
			FString Key = Text.TrimStartAndEnd();
			FString Value;
			if (Key.Split(TEXT("!="), &Key, &Value))
			{
				Out.Op = FRTSLayoutCondition::EOp::NotEqual;
			}
			else if (Key.Split(TEXT("="), &Key, &Value))
			{
				Out.Op = FRTSLayoutCondition::EOp::Equal;
			}
			else if (Key.RemoveFromStart(TEXT("!")))
			{
				Out.Op = FRTSLayoutCondition::EOp::Falsy;
			}
			else
			{
				Out.Op = FRTSLayoutCondition::EOp::Truthy;
			}
			Key.TrimStartAndEndInline();
			if (Key.IsEmpty() || Key.Contains(TEXT(" ")))
			{
				Error(Element, FString::Printf(TEXT("visibleWhen \"%s\" names no key"), *Text));
				Out.Op = FRTSLayoutCondition::EOp::Always;
				return;
			}
			Out.Key = FName(*Key);
			Out.Value = Value.TrimStartAndEnd();
		}

		void ReadText(const FString& Text, TArray<FRTSLayoutTextPart>& Out, const FRTSLayoutElement& Element)
		{
			int32 Start = 0;
			while (Start < Text.Len())
			{
				const int32 Open = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
				if (Open == INDEX_NONE)
				{
					Out.Add({ false, Text.Mid(Start), NAME_None });
					return;
				}
				const int32 Close = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
				if (Close == INDEX_NONE || Close == Open + 1)
				{
					Error(Element, FString::Printf(TEXT("text \"%s\" has an unclosed or empty {key}"), *Text));
					Out.Add({ false, Text.Mid(Start), NAME_None });
					return;
				}
				if (Open > Start)
				{
					Out.Add({ false, Text.Mid(Start, Open - Start), NAME_None });
				}
				Out.Add({ true, FString(), FName(*Text.Mid(Open + 1, Close - Open - 1).TrimStartAndEnd()) });
				Start = Close + 1;
			}
		}

		void ReadCommon(const FJsonObject& Object, FRTSLayoutElement& Out)
		{
			FString Anchor;
			if (Object.HasField(TEXT("anchor")) && ReadString(Object, TEXT("anchor"), Anchor, Out))
			{
				const int32 Index = UE_ARRAY_COUNT(AnchorNames);
				int32 Found = INDEX_NONE;
				for (int32 Candidate = 0; Candidate < Index; ++Candidate)
				{
					if (Anchor == AnchorNames[Candidate])
					{
						Found = Candidate;
					}
				}
				if (Found == INDEX_NONE)
				{
					Error(Out, FString::Printf(TEXT("unknown anchor \"%s\""), *Anchor));
				}
				else
				{
					Out.Anchor = static_cast<ERTSLayoutAnchor>(Found);
				}
			}
			ReadVector(Object, TEXT("position"), Out.Position, Out);
			Out.bHasSize = ReadVector(Object, TEXT("size"), Out.Size, Out);

			TArray<double> Padding;
			if (Object.HasField(TEXT("padding")) && ReadNumbers(Object, TEXT("padding"), 4, Padding, Out))
			{
				Out.Padding = FMargin(Padding[0], Padding[1], Padding[2], Padding[3]);
			}
			if (Object.HasField(TEXT("color")))
			{
				ReadString(Object, TEXT("color"), Out.Color, Out);
			}
			if (Object.HasField(TEXT("opacity")))
			{
				ReadNumber(Object, TEXT("opacity"), Out.Opacity, Out);
			}
			bool bHitTest = false;
			if (Object.HasField(TEXT("hitTest")) && ReadBool(Object, TEXT("hitTest"), bHitTest, Out))
			{
				Out.HitTest = bHitTest;
			}
			FString Condition;
			if (Object.HasField(TEXT("visibleWhen")) && ReadString(Object, TEXT("visibleWhen"), Condition, Out))
			{
				ReadCondition(Condition, Out.VisibleWhen, Out);
			}
			if (Object.HasField(TEXT("bind")))
			{
				const TSharedPtr<FJsonObject>* Bind = nullptr;
				if (!Object.TryGetObjectField(TEXT("bind"), Bind))
				{
					Error(Out, TEXT("bind is no object"));
					return;
				}
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Bind)->Values)
				{
					FString Key;
					if (!IsIn(Field.Key, BindProperties))
					{
						Error(Out, FString::Printf(TEXT("unknown bind property \"%s\""), *Field.Key));
					}
					else if (!Field.Value->TryGetString(Key) || Key.IsEmpty())
					{
						Error(Out, FString::Printf(TEXT("bind.%s is no binding key"), *Field.Key));
					}
					else
					{
						Out.Bind.Add(FName(*Field.Key), FName(*Key));
					}
				}
			}
		}

		void ReadSpecific(const FJsonObject& Object, FRTSLayoutElement& Out)
		{
			ReadName(Object, TEXT("sprite"), Out.Sprite, Out);
			if (ReadVector(Object, TEXT("tile"), Out.TileSize, Out) && (Out.TileSize.X <= 0.0 || Out.TileSize.Y <= 0.0))
			{
				Error(Out, TEXT("tile is no positive size"));
				Out.TileSize = FVector2D::ZeroVector;
			}
			ReadName(Object, TEXT("fillSprite"), Out.FillSprite, Out);
			if (Object.HasField(TEXT("fillColor")))
			{
				ReadString(Object, TEXT("fillColor"), Out.FillColor, Out);
			}
			FString Text;
			if (Object.HasField(TEXT("text")) && ReadString(Object, TEXT("text"), Text, Out))
			{
				ReadText(Text, Out.Text, Out);
			}
			ReadName(Object, TEXT("font"), Out.Font, Out);
			if (Object.HasField(TEXT("fontSize")))
			{
				ReadInt(Object, TEXT("fontSize"), Out.FontSize, Out);
			}
			if (Object.HasField(TEXT("align")))
			{
				ReadString(Object, TEXT("align"), Out.Align, Out);
				static const TCHAR* BoxAligns[] = { TEXT("Start"), TEXT("Center"), TEXT("End"), TEXT("Fill") };
				static const TCHAR* TextAligns[] = { TEXT("Left"), TEXT("Center"), TEXT("Right") };
				const bool bValid = Out.Type == ERTSLayoutType::Text ? IsIn(Out.Align, TextAligns) : IsIn(Out.Align, BoxAligns);
				if (!bValid)
				{
					Error(Out, FString::Printf(TEXT("unknown align \"%s\""), *Out.Align));
				}
			}
			if (Object.HasField(TEXT("wrap")))
			{
				ReadBool(Object, TEXT("wrap"), Out.bWrap, Out);
			}
			if (Object.HasField(TEXT("maxLines")))
			{
				ReadInt(Object, TEXT("maxLines"), Out.MaxLines, Out);
			}
			if (Object.HasField(TEXT("spacing")))
			{
				ReadNumber(Object, TEXT("spacing"), Out.Spacing, Out);
			}
			if (Object.HasField(TEXT("columns")))
			{
				ReadInt(Object, TEXT("columns"), Out.Columns, Out);
			}
			if (Object.HasField(TEXT("rows")))
			{
				ReadInt(Object, TEXT("rows"), Out.Rows, Out);
			}
			ReadVector(Object, TEXT("cellSize"), Out.CellSize, Out);
			ReadName(Object, TEXT("action"), Out.Action, Out);
			if (Object.HasField(TEXT("tooltip")))
			{
				ReadBool(Object, TEXT("tooltip"), Out.bTooltip, Out);
			}
			if (Object.HasField(TEXT("layout")))
			{
				ReadString(Object, TEXT("layout"), Out.Layout, Out);
			}
			ReadName(Object, TEXT("widget"), Out.Widget, Out);

			if (Out.Type == ERTSLayoutType::Grid && (Out.Columns <= 0 || Out.Rows <= 0))
			{
				Error(Out, TEXT("a Grid needs columns and rows"));
			}
			if (Out.Type == ERTSLayoutType::Slot && Out.Layout.IsEmpty() == Out.Widget.IsNone())
			{
				Error(Out, TEXT("a Slot needs either layout or widget"));
			}
		}

		const FString& File;
		TArray<FString>& Errors;
	};
}

bool FRTSLayoutElement::IsStretchedX() const
{
	return Anchor == ERTSLayoutAnchor::TopStretch || Anchor == ERTSLayoutAnchor::BottomStretch || Anchor == ERTSLayoutAnchor::Fill;
}

bool FRTSLayoutElement::IsStretchedY() const
{
	return Anchor == ERTSLayoutAnchor::LeftStretch || Anchor == ERTSLayoutAnchor::RightStretch || Anchor == ERTSLayoutAnchor::Fill;
}

bool FRTSLayoutElement::CatchesMouse() const
{
	if (HitTest.IsSet())
	{
		return HitTest.GetValue();
	}
	return Type == ERTSLayoutType::Button || (Type == ERTSLayoutType::Panel && !Sprite.IsNone());
}

void FRTSLayoutElement::GetKeys(TArray<FName>& OutKeys) const
{
	for (const FRTSLayoutTextPart& Part : Text)
	{
		if (Part.bKey)
		{
			OutKeys.Add(Part.Key);
		}
	}
	for (const TPair<FName, FName>& Binding : Bind)
	{
		OutKeys.Add(Binding.Value);
	}
	if (VisibleWhen.Op != FRTSLayoutCondition::EOp::Always)
	{
		OutKeys.Add(VisibleWhen.Key);
	}
}

FString RTSLayout::FormatError(const FString& File, const FString& Element, const FString& Message)
{
	return FString::Printf(TEXT("%s: %s: %s"), *File, *Element, *Message);
}

bool RTSLayout::Parse(const FJsonObject& Object, const FString& File, FRTSLayoutElement& OutRoot, TArray<FString>& OutErrors)
{
	const int32 ErrorsBefore = OutErrors.Num();
	FString RootType;
	Object.TryGetStringField(TEXT("type"), RootType);
	FParser(File, OutErrors).ParseElement(Object, RootType.IsEmpty() ? FString(TEXT("root")) : RootType, true, OutRoot);
	return OutErrors.Num() == ErrorsBefore;
}

bool RTSLayout::ParseHexColor(const FString& Text, FLinearColor& OutColor)
{
	if (!Text.StartsWith(TEXT("#")) || (Text.Len() != 7 && Text.Len() != 9))
	{
		return false;
	}
	for (int32 Index = 1; Index < Text.Len(); ++Index)
	{
		if (!FChar::IsHexDigit(Text[Index]))
		{
			return false;
		}
	}
	OutColor = FLinearColor(FColor::FromHex(Text));
	return true;
}
