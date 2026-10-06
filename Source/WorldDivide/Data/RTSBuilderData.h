#pragma once

#include "CoreMinimal.h"
#include "Data/RTSUnitData.h"
#include "RTSBuilderData.generated.h"

class URTSBuildingData;

/** A unit that places buildings and constructs them, walking to each site and staying there while it builds. */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSBuilderData : public URTSUnitData
{
	GENERATED_BODY()

public:
	/** What the builder can construct, in command card order. The faction must allow a building as well. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Construction")
	TArray<TObjectPtr<URTSBuildingData>> Buildings;
};
