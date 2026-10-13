#include "UI/RTSUIStyleSubsystem.h"

#include "Brushes/SlateColorBrush.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSFactionData.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor PlaceholderGrey(0.25f, 0.25f, 0.25f, 1.f);
	const FLinearColor PlaceholderGreyLight(0.4f, 0.4f, 0.4f, 1.f);
	const FLinearColor PlaceholderGreyDark(0.15f, 0.15f, 0.15f, 1.f);
}

URTSPlaceholderButtonStyle::URTSPlaceholderButtonStyle()
{
	NormalBase = FSlateColorBrush(PlaceholderGrey);
	NormalHovered = FSlateColorBrush(PlaceholderGreyLight);
	NormalPressed = FSlateColorBrush(PlaceholderGreyDark);
	SelectedBase = FSlateColorBrush(PlaceholderGreyLight);
	SelectedHovered = FSlateColorBrush(PlaceholderGreyLight);
	SelectedPressed = FSlateColorBrush(PlaceholderGreyDark);
	Disabled = FSlateColorBrush(PlaceholderGreyDark);
	NormalTextStyle = URTSPlaceholderTextStyle::StaticClass();
	NormalHoveredTextStyle = URTSPlaceholderTextStyle::StaticClass();
	SelectedTextStyle = URTSPlaceholderTextStyle::StaticClass();
	SelectedHoveredTextStyle = URTSPlaceholderTextStyle::StaticClass();
	DisabledTextStyle = URTSPlaceholderTextStyle::StaticClass();
}

URTSPlaceholderTextStyle::URTSPlaceholderTextStyle()
{
	Font = FCoreStyle::GetDefaultFontStyle("Regular", 14);
	Color = FLinearColor::White;
}

URTSPlaceholderBorderStyle::URTSPlaceholderBorderStyle()
{
	Background = FSlateColorBrush(PlaceholderGrey);
}

TArray<const URTSUIStyleSet*> URTSUIStyleSubsystem::GetStyleChain() const
{
	TArray<const URTSUIStyleSet*> Chain;
	const APlayerController* Controller = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	const ARTSPlayerState* PlayerState = Controller ? Controller->GetPlayerState<ARTSPlayerState>() : nullptr;
	if (const URTSFactionData* Faction = PlayerState ? PlayerState->GetFaction() : nullptr)
	{
		if (const URTSUIStyleSet* StyleSet = LoadStyleSet(Faction->UIStyle))
		{
			Chain.Add(StyleSet);
		}
	}
	const URTSSettings* Settings = GetDefault<URTSSettings>();
	for (const TSoftObjectPtr<URTSUIStyleSet>& Fallback : { Settings->CommonUIStyle, Settings->NeutralUIStyle })
	{
		if (const URTSUIStyleSet* StyleSet = LoadStyleSet(Fallback))
		{
			Chain.AddUnique(StyleSet);
		}
	}
	return Chain;
}

const URTSUIStyleSet* URTSUIStyleSubsystem::LoadStyleSet(const TSoftObjectPtr<URTSUIStyleSet>& StyleSet) const
{
	if (StyleSet.IsNull())
	{
		return nullptr;
	}
	if (const TObjectPtr<URTSUIStyleSet>* Loaded = LoadedSets.Find(StyleSet.ToSoftObjectPath()))
	{
		return *Loaded;
	}
	URTSUIStyleSet* Loaded = StyleSet.LoadSynchronous();
	// A missing asset is remembered as null too, so it is not looked for every frame.
	LoadedSets.Add(StyleSet.ToSoftObjectPath(), Loaded);
	return Loaded;
}

FSlateBrush URTSUIStyleSubsystem::FindBrush(FName Sprite) const
{
	return FindSpriteIn(GetStyleChain(), Sprite).Brush;
}

FRTSUISprite URTSUIStyleSubsystem::FindSprite(FName Sprite) const
{
	return FindSpriteIn(GetStyleChain(), Sprite);
}

TSubclassOf<UCommonButtonStyle> URTSUIStyleSubsystem::FindButtonStyle(FName Sprite) const
{
	return FindButtonStyleIn(GetStyleChain(), Sprite);
}

TSubclassOf<UCommonTextStyle> URTSUIStyleSubsystem::FindTextStyle(FName Font, FName Color) const
{
	return FindTextStyleIn(GetStyleChain(), Font, Color);
}

TSubclassOf<UCommonBorderStyle> URTSUIStyleSubsystem::FindBorderStyle(FName Sprite) const
{
	return FindBorderStyleIn(GetStyleChain(), Sprite);
}

FLinearColor URTSUIStyleSubsystem::FindColor(FName Color) const
{
	return FindColorIn(GetStyleChain(), Color);
}

const FRTSUISprite& URTSUIStyleSubsystem::FindSpriteIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Sprite)
{
	for (const URTSUIStyleSet* StyleSet : Chain)
	{
		if (const FRTSUISprite* Found = StyleSet ? StyleSet->Sprites.Find(Sprite) : nullptr)
		{
			return *Found;
		}
	}
	return GetPlaceholderSprite();
}

TSubclassOf<UCommonButtonStyle> URTSUIStyleSubsystem::FindButtonStyleIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Sprite)
{
	for (const URTSUIStyleSet* StyleSet : Chain)
	{
		const TSubclassOf<UCommonButtonStyle>* Found = StyleSet ? StyleSet->ButtonStyles.Find(Sprite) : nullptr;
		if (Found && *Found)
		{
			return *Found;
		}
	}
	return URTSPlaceholderButtonStyle::StaticClass();
}

TSubclassOf<UCommonTextStyle> URTSUIStyleSubsystem::FindTextStyleIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Font, FName Color)
{
	const FName Key = URTSUIStyleSet::MakeTextStyleKey(Font, Color);
	for (const URTSUIStyleSet* StyleSet : Chain)
	{
		const TSubclassOf<UCommonTextStyle>* Found = StyleSet ? StyleSet->TextStyles.Find(Key) : nullptr;
		if (Found && *Found)
		{
			return *Found;
		}
	}
	return URTSPlaceholderTextStyle::StaticClass();
}

TSubclassOf<UCommonBorderStyle> URTSUIStyleSubsystem::FindBorderStyleIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Sprite)
{
	for (const URTSUIStyleSet* StyleSet : Chain)
	{
		const TSubclassOf<UCommonBorderStyle>* Found = StyleSet ? StyleSet->BorderStyles.Find(Sprite) : nullptr;
		if (Found && *Found)
		{
			return *Found;
		}
	}
	return URTSPlaceholderBorderStyle::StaticClass();
}

FLinearColor URTSUIStyleSubsystem::FindColorIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Color)
{
	for (const URTSUIStyleSet* StyleSet : Chain)
	{
		if (const FLinearColor* Found = StyleSet ? StyleSet->Colors.Find(Color) : nullptr)
		{
			return *Found;
		}
	}
	return GetPlaceholderColor();
}

const FRTSUISprite& URTSUIStyleSubsystem::GetPlaceholderSprite()
{
	static const FRTSUISprite Placeholder = []()
	{
		FRTSUISprite Sprite;
		Sprite.Brush = FSlateColorBrush(PlaceholderGrey);
		return Sprite;
	}();
	return Placeholder;
}

FLinearColor URTSUIStyleSubsystem::GetPlaceholderColor()
{
	return PlaceholderGrey;
}
