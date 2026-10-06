#pragma once

#include "CoreMinimal.h"
#include "Data/RTSUnitData.h"
#include "RTSHarvesterData.generated.h"

/** A unit that hauls supplies from a supply source to its team's depots, earning credits on every delivery. */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSHarvesterData : public URTSUnitData
{
	GENERATED_BODY()

public:
	/** Supplies taken per trip and credits paid when they are delivered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest", meta = (ClampMin = "1"))
	int32 CarryAmount = 100;

	/** Seconds spent loading at the source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest", meta = (ClampMin = "0"))
	float GatherSeconds = 3.f;

	/** How far from where it stands the harvester looks for another source once its own runs dry, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Harvest", meta = (ClampMin = "0"))
	float SearchRadius = 6000.f;
};
