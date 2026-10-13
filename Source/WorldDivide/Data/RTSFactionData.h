#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RTSFactionData.generated.h"

class URTSAIBuildOrderData;
class URTSBuildingData;
class URTSUIStyleSet;

/** What one faction can build. The player state references the faction the player plays. */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSFactionData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FText DisplayName;

	/** The tag its entities carry in URTSEntityData::FactionTag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FGameplayTag FactionTag;

	/** In build menu order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TArray<TObjectPtr<URTSBuildingData>> Buildings;

	/**
	 * The completed building each team of this faction starts with at its PlayerStart, such as the headquarters that
	 * produces builders. Listed in Buildings as well, a team that lost it can build it again.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TObjectPtr<URTSBuildingData> Headquarters;

	/** A faction without a power grid always runs at full efficiency, whatever its buildings produce or draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bUsesPower = true;

	/** What an AI opponent playing this faction builds and when it attacks. Without one the AI only harvests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TObjectPtr<URTSAIBuildOrderData> AIBuildOrder;

	/** The HUD look of a player of this faction, generated from UI/; unset uses the Common and Neutral looks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TSoftObjectPtr<URTSUIStyleSet> UIStyle;

	/** The faction of URTSSettings::Factions or DefaultFaction with this tag, loaded; null for none. */
	static const URTSFactionData* FindByTag(FGameplayTag Tag);
};
