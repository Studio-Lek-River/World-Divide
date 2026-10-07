#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSBuildingData.generated.h"

class URTSUnitData;

/**
 * A structure placed on the 100 cm build grid. An armed building (a tower) fires once construction completes; a building
 * listing Units (a barracks) produces them.
 */
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

	/** The weapon holds fire while the owner is low on power, as base defences do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bRequiresPower = false;

	/** Spawned once for the owner when construction completes, such as the harvester that comes with a supply depot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TObjectPtr<URTSUnitData> SpawnOnComplete;

	/** What the building produces once constructed, in command card order. Empty for a building without production. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TArray<TObjectPtr<URTSUnitData>> Units;

	/** Harvesters deliver supplies here once it is constructed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	bool bAcceptsSupply = false;
};
