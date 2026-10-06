#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSBuildingData.generated.h"

/** A structure placed on the 100 cm build grid. An armed building (a tower) fires once construction completes. */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSBuildingData : public URTSEntityData
{
	GENERATED_BODY()

public:
	/** Size in 100 cm grid cells along X and Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "1"))
	FIntPoint Footprint = FIntPoint(2, 2);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0"))
	int32 PowerOutput = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0"))
	int32 PowerDraw = 0;
};
