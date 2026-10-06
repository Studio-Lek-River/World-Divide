#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSWeaponData.generated.h"

/** An instant-hit weapon. Units and buildings reference one from their entity data; none means unarmed. */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Reach in cm, measured to the edge of the target's footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0"))
	float Range = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0"))
	float Damage = 10.f;

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.01"))
	float ReloadSeconds = 1.f;
};
