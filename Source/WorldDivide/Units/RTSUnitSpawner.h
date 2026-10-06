#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSUnitSpawner.generated.h"

class URTSUnitData;

/** Placed in a map to give a team its starting units. Spawns once, on the server, at BeginPlay. */
UCLASS()
class WORLDDIVIDE_API ARTSUnitSpawner : public AActor
{
	GENERATED_BODY()

public:
	ARTSUnitSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TObjectPtr<URTSUnitData> UnitData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	int32 TeamId = 0;

protected:
	virtual void BeginPlay() override;
};
