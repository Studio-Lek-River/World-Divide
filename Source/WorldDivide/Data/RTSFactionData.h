#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSFactionData.generated.h"

class URTSBuildingData;

/** What one faction can build. The player state references the faction the player plays. */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSFactionData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FText DisplayName;

	/** In build menu order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TArray<TObjectPtr<URTSBuildingData>> Buildings;

	/** A faction without a power grid always runs at full efficiency, whatever its buildings produce or draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bUsesPower = true;
};
