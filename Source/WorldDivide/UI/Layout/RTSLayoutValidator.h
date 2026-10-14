#pragma once

#include "CoreMinimal.h"
#include "UI/Layout/RTSUIBinding.h"

class URTSUIStyleSet;

/** What a layout may refer to: the names in the generated style sets, the layout files, the engine panels and the bindings. */
struct WORLDDIVIDE_API FRTSLayoutValidationContext
{
	TSet<FName> Sprites;
	TSet<FName> Fonts;
	TSet<FName> Colors;
	TSet<FString> Layouts;
	TSet<FName> Widgets;
	FRTSUIDeclarations Declarations;

	/** Adds every sprite, font role and theme colour of StyleSet. */
	void AddStyleSet(const URTSUIStyleSet& StyleSet);

	/**
	 * The game's: the Common and Neutral style sets and each faction's (URTSSettings), the layout files of
	 * RTSLayoutFiles::GetRoots, RTSLayout::GetPanelNames and RTSUIBindings::GetDefaultSources.
	 */
	static FRTSLayoutValidationContext MakeDefault();

	/**
	 * Adds the layout files in UIDir/Layouts and the sprite names (without the set prefix) in UIDir/Sprites/*.sprites.json,
	 * so a delivery whose sprites are not imported yet validates against them. A sprites file that does not parse adds
	 * nothing; the setup script reports it.
	 */
	void AddFolder(const FString& UIDir);
};

namespace RTSLayout
{
	/**
	 * Checks the text of the layout file File: that it parses, that every type and property is known, that every sprite,
	 * font, theme colour, layout, widget, binding key, list and action it names exists in Context, and that no name is used
	 * twice. Each problem is one "<File>: <Element>: <Message>" in OutErrors. True when there are none.
	 */
	WORLDDIVIDE_API bool Validate(const FString& Text, const FString& File, const FRTSLayoutValidationContext& Context, TArray<FString>& OutErrors);

	/** Validates every layout file in UIDir/Layouts against Context, each problem in OutErrors. True when there are none. */
	WORLDDIVIDE_API bool ValidateFolder(const FString& UIDir, const FRTSLayoutValidationContext& Context, TArray<FString>& OutErrors);
}
