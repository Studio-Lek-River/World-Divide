#pragma once

#include "CoreMinimal.h"
#include "CommonBorder.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/RTSUIStyleSet.h"
#include "RTSUIStyleSubsystem.generated.h"

/** What a button looks like when no style set has the sprite asked for: flat grey states, placeholder text. */
UCLASS()
class WORLDDIVIDE_API URTSPlaceholderButtonStyle : public UCommonButtonStyle
{
	GENERATED_BODY()

public:
	URTSPlaceholderButtonStyle();
};

/** The engine's default font in white, for a font role or colour no style set has. */
UCLASS()
class WORLDDIVIDE_API URTSPlaceholderTextStyle : public UCommonTextStyle
{
	GENERATED_BODY()

public:
	URTSPlaceholderTextStyle();
};

/** A flat grey background, for a panel sprite no style set has. */
UCLASS()
class WORLDDIVIDE_API URTSPlaceholderBorderStyle : public UCommonBorderStyle
{
	GENERATED_BODY()

public:
	URTSPlaceholderBorderStyle();
};

/**
 * Resolves the names a layout uses (sprites, theme colours, font roles) to the looks of the local player's faction. Each
 * lookup tries the faction's style set (URTSFactionData::UIStyle), then Common, then Neutral (URTSSettings), then a
 * built-in grey placeholder, so it never fails: widgets never ask which faction it is.
 */
UCLASS()
class WORLDDIVIDE_API URTSUIStyleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The local player's faction set, Common and Neutral, those that exist, in the order they are tried. */
	TArray<const URTSUIStyleSet*> GetStyleChain() const;

	UFUNCTION(BlueprintPure, Category = "UI Style")
	FSlateBrush FindBrush(FName Sprite) const;

	UFUNCTION(BlueprintPure, Category = "UI Style")
	FRTSUISprite FindSprite(FName Sprite) const;

	UFUNCTION(BlueprintPure, Category = "UI Style")
	TSubclassOf<UCommonButtonStyle> FindButtonStyle(FName Sprite) const;

	UFUNCTION(BlueprintPure, Category = "UI Style")
	TSubclassOf<UCommonTextStyle> FindTextStyle(FName Font, FName Color) const;

	UFUNCTION(BlueprintPure, Category = "UI Style")
	TSubclassOf<UCommonBorderStyle> FindBorderStyle(FName Sprite) const;

	UFUNCTION(BlueprintPure, Category = "UI Style")
	FLinearColor FindColor(FName Color) const;

	/** The lookups over any chain, first match wins, the placeholder when none has it. Public for the tests. */
	static const FRTSUISprite& FindSpriteIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Sprite);
	static TSubclassOf<UCommonButtonStyle> FindButtonStyleIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Sprite);
	static TSubclassOf<UCommonTextStyle> FindTextStyleIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Font, FName Color);
	static TSubclassOf<UCommonBorderStyle> FindBorderStyleIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Sprite);
	static FLinearColor FindColorIn(TConstArrayView<const URTSUIStyleSet*> Chain, FName Color);

	/** The grey sprite and colour returned for a name no set has. */
	static const FRTSUISprite& GetPlaceholderSprite();
	static FLinearColor GetPlaceholderColor();

private:
	const URTSUIStyleSet* LoadStyleSet(const TSoftObjectPtr<URTSUIStyleSet>& StyleSet) const;

	/** The sets loaded so far, kept from garbage collection. */
	UPROPERTY(Transient)
	mutable TMap<FSoftObjectPath, TObjectPtr<URTSUIStyleSet>> LoadedSets;
};
