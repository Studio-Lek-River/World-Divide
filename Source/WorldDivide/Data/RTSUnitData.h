#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSUnitData.generated.h"

UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSUnitData : public URTSEntityData
{
	GENERATED_BODY()

public:
	/** Top speed in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "0"))
	float MoveSpeed = 400.f;

	/** Degrees per second the unit turns toward its heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float TurnRate = 360.f;

	/** Vehicles tilt their mesh to the slope they stand on; infantry stays upright. Aircraft ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	bool bTiltsToGround = false;
};
