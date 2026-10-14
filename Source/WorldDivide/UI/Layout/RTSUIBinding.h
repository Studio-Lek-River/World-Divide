#pragma once

#include "CoreMinimal.h"

class APlayerController;

/** One value a binding key gives a layout: a text, a number, a flag, a 0-1 fill, a sprite name or a theme colour name. */
struct WORLDDIVIDE_API FRTSUIValue
{
	enum class EType : uint8
	{
		None,
		Text,
		Number,
		Bool,
		Progress,
		Sprite,
		Color,
	};

	EType Type = EType::None;
	FString String;
	double Number = 0.0;
	bool bBool = false;

	static FRTSUIValue MakeText(const FString& InText);
	static FRTSUIValue MakeNumber(double InNumber);
	static FRTSUIValue MakeBool(bool bInBool);
	static FRTSUIValue MakeProgress(float InProgress);
	static FRTSUIValue MakeSprite(FName InSprite);
	static FRTSUIValue MakeColor(FName InColor);

	/** What visibleWhen "key" tests: true, a non-empty text or name, a number other than 0. */
	bool IsTruthy() const;

	/** As {key} shows it and key=value compares it: numbers formatted ("1,250"), flags as "true" or "false". */
	FString ToString() const;
};

/** Who asks: the local player whose HUD it is, and inside a repeated template, the list and the item's index. */
struct FRTSUIContext
{
	APlayerController* Player = nullptr;
	FName List;
	int32 Index = INDEX_NONE;
};

/** Every key, list (with its item.* keys) and action a binding source has, declared up front for the validator. */
struct WORLDDIVIDE_API FRTSUIDeclarations
{
	TSet<FName> Keys;
	TMap<FName, TSet<FName>> Lists;
	TSet<FName> Actions;

	void Append(const FRTSUIDeclarations& Other);
};

/**
 * The C++ behind a panel: the values its layout shows by binding key, its lists and the actions its buttons send. Layouts
 * only show what a source gives them, so no gameplay is decided in a layout. Values are pulled every frame. A source keeps
 * no state of its own; it reads the player in the context.
 */
class WORLDDIVIDE_API IRTSUIBindingSource
{
public:
	virtual ~IRTSUIBindingSource() = default;

	virtual void Declare(FRTSUIDeclarations& Out) const = 0;

	/** The layouts (file names without .layout.json) this source serves, so a design brief lists only their keys. */
	virtual TArray<FName> GetPanels() const = 0;

	/** False when Key is not this source's. */
	virtual bool GetValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const = 0;

	virtual int32 GetListCount(FName List, const FRTSUIContext& Context) const { return 0; }

	/** Key is the item key without "item.", read for Context.Index of Context.List. */
	virtual bool GetListValue(FName Key, const FRTSUIContext& Context, FRTSUIValue& OutValue) const { return false; }

	/** Carries out a button's action; Context.Index is its item inside a template. False when Action is not this source's. */
	virtual bool HandleAction(FName Action, const FRTSUIContext& Context) const { return false; }
};

namespace RTSUIBindings
{
	/** The game's binding sources, which every HUD layout reads. */
	WORLDDIVIDE_API TArray<const IRTSUIBindingSource*> GetDefaultSources();

	/** Everything Sources declare. */
	WORLDDIVIDE_API FRTSUIDeclarations Declare(TConstArrayView<const IRTSUIBindingSource*> Sources);

	/** The power bar's fill, consumed over produced, 0-1: full when something consumes and nothing produces, empty when neither. */
	WORLDDIVIDE_API float GetPowerRatio(int32 Produced, int32 Consumed);
}
