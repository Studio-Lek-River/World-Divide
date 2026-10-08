#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RTSEditorScripting.generated.h"

/** Engine calls Tools/Editor scripts need that Unreal does not expose to Python. */
UCLASS()
class WORLDDIVIDE_API URTSEditorScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds the navmesh of WorldContextObject's world from scratch and waits for it, so a map saved afterwards carries it
	 * and plays with a full navmesh straight away. Returns the number of tiles built, 0 when there is no navmesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static int32 BuildNavigation(UObject* WorldContextObject);
};
