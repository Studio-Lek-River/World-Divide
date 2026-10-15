#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RTSEditorScripting.generated.h"

class UFont;
class UFontFace;

/** Engine calls Tools/Editor scripts need that Unreal does not expose to Python. */
UCLASS()
class WORLDDIVIDE_API URTSEditorScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds the navmesh of every nav agent class (RTSNavAgents) in WorldContextObject's world from scratch and waits for
	 * them, so a map saved afterwards carries them and plays with full navmeshes straight away. Logs each one's tiles and
	 * memory and the time the build took. Returns the fewest tiles any class got, 0 when one has no navmesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static int32 BuildNavigation(UObject* WorldContextObject);

	/**
	 * Whether this process has a renderer, so CapturePreview can work: false under -nullrhi and in a commandlet run
	 * without -AllowCommandletRendering.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static bool CanRender();

	/**
	 * Where the camera of a map preview sits over Area and how wide it sees, looking straight down with world +X up and
	 * world +Y right on the image, as URTSMapData::ToPreviewPosition. Area must be square.
	 */
	static void GetPreviewCamera(const FBox2D& Area, FVector& OutLocation, FRotator& OutRotation, double& OutOrthoWidth);

	/**
	 * Renders WorldContextObject's world top-down over Area into a Size x Size PNG at FileName, through the camera of
	 * GetPreviewCamera. Returns false when there is no renderer or the image could not be written.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static bool CapturePreview(UObject* WorldContextObject, FBox2D Area, int32 Size, const FString& FileName);

	/**
	 * Makes Font a runtime font whose default typeface is Face alone, for the UI fonts setup_testmap.py imports: Python
	 * cannot set the font face of a typeface entry. Returns false without a font or a face.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static bool BuildFont(UFont* Font, UFontFace* Face);

	/**
	 * Imports the TTF or OTF at FileName as the font face asset AssetPath (such as /Game/UI/Fonts/FF_Name), replacing the
	 * one there, with its data inline. The engine's font import flushes the Slate font cache, which a commandlet does not
	 * have. Returns null when the file cannot be read.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static UFontFace* ImportFontFace(const FString& FileName, const FString& AssetPath);
};
