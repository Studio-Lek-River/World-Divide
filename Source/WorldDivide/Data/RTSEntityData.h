#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
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

	/** One or two sentences for the tooltip and the selection panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display", meta = (MultiLine = true))
	FText Description;

	/**
	 * The sprite of the buttons, the queue and the selection panel, without the set prefix (for example Icon.Tank). It
	 * resolves by the faction of this entity, see RTSUI::GetEntityIcon; None shows Icon.Placeholder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FName IconSprite;

	/** The key that orders this from its command panel; none gives it its position key 1-9 there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FKey Hotkey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> Mesh;

	/** Scale applied to Mesh; placeholder blocks need this until real models exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector MeshScale = FVector::OneVector;

	/**
	 * Interaction radius in cm: how far a unit stands off a dock or a building site and holds short of a new building, and
	 * for now its avoidance circle and formation spacing. A unit's collision, brackets and footprint come from its mesh
	 * (FRTSUnitShape); this sizes them only for data without one.
	 */
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

	/** The faction this belongs to (URTSFactionData::FactionTag), whose UI style its icon comes from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	FGameplayTag FactionTag;
};
