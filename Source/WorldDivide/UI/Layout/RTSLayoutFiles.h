#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Where layout files come from: UI/Layouts/<Name>.layout.json, then Mods/<Mod>/UI/Layouts/ with the mods in alphabetical
 * order, a later file replacing an earlier one with the same name. Files are read once and kept until ClearCache, which
 * rts.UI.Reload calls.
 */
namespace RTSLayoutFiles
{
	WORLDDIVIDE_API extern const TCHAR* Suffix;

	/** UI/Layouts under the project, then each mod's, in the order they are searched. */
	WORLDDIVIDE_API TArray<FString> GetRoots();

	/** Every layout in Roots by name, to the file that wins. */
	WORLDDIVIDE_API TMap<FString, FString> FindFiles(TConstArrayView<FString> Roots);

	/** Parses a layout file's text; null with OutError set when it is no JSON object. */
	WORLDDIVIDE_API TSharedPtr<FJsonObject> Parse(const FString& Text, FString& OutError);

	/** The layout Name from GetRoots, cached; null with OutError set when it is missing or does not parse. */
	WORLDDIVIDE_API TSharedPtr<FJsonObject> Load(const FString& Name, FString& OutError);

	/** The same from Roots, uncached; for the tests. */
	WORLDDIVIDE_API TSharedPtr<FJsonObject> Load(TConstArrayView<FString> Roots, const FString& Name, FString& OutError);

	WORLDDIVIDE_API void ClearCache();
}
