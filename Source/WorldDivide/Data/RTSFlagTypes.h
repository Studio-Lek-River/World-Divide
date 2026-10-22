#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSFlagTypes.generated.h"

class UMaterialInterface;

/** One flag a building can fly: its cloth material and its proportions (#351). */
USTRUCT(BlueprintType)
struct FRTSFlagType
{
	GENERATED_BODY()

	/** An instance of the shared waving cloth (M_Flag_Cloth) with the flag's texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag")
	TObjectPtr<UMaterialInterface> Material;

	/** Fly over hoist, such as 1.9 for the US flag's 10:19; the cloth's width is its height times this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag", meta = (ClampMin = "0.1"))
	float Aspect = 1.f;
};

/**
 * Every flag of the game by key, such as USA, as Art/Shared/flags/flags.json lists them (setup_testmap.py writes
 * DA_FlagTypes from it). A building flies the one its data's FlagType names on the cloth part FlagPart.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSFlagTypes : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag")
	TMap<FName, FRTSFlagType> Flags;

	/** The flag of that key, or null when there is none or it has no material. */
	const FRTSFlagType* Find(FName Key) const;
};
