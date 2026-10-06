#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RTSEntityData.generated.h"

class UStaticMesh;
class URTSWeaponData;

/**
 * Everything a player can build or train shares this schema: how it looks, what it costs and what it needs first.
 * A new faction is a new set of these assets, not new code.
 */
UCLASS(Abstract, BlueprintType)
class WORLDDIVIDE_API URTSEntityData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> Mesh;

	/** Scale applied to Mesh; placeholder blocks need this until real models exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector MeshScale = FVector::OneVector;

	/** Footprint radius in cm, used for collision, selection brackets and formation spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display", meta = (ClampMin = "10"))
	float SelectionRadius = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	/** No weapon means unarmed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	TObjectPtr<URTSWeaponData> Weapon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production", meta = (ClampMin = "0"))
	int32 Cost = 100;

	/** Seconds to produce at full power. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production", meta = (ClampMin = "0"))
	float BuildTime = 5.f;

	/** All of these must be owned before this entry becomes available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TArray<TObjectPtr<URTSEntityData>> Prerequisites;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	FGameplayTag FactionTag;
};
