#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSUpgradeData.generated.h"

/** The stat an upgrade multiplies. */
UENUM(BlueprintType)
enum class ERTSUpgradeStat : uint8
{
	/** Damage per shot of the entity's weapon. */
	WeaponDamage,
	/** Maximum health; current health scales along, so a damaged entity keeps its share of health. */
	MaxHealth,
};

/**
 * A one-time purchase per player, researched in a building's production queue like a unit. Once complete it multiplies Stat
 * of every unit or building listed in Targets the player owns, those standing and those still to come, and counts as owned
 * for prerequisites. Mesh, health and weapon go unused.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSUpgradeData : public URTSEntityData
{
	GENERATED_BODY()

public:
	/** The unit and building data the upgrade improves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	TArray<TObjectPtr<URTSEntityData>> Targets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	ERTSUpgradeStat Stat = ERTSUpgradeStat::WeaponDamage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade", meta = (ClampMin = "0"))
	float Multiplier = 1.f;

	/** The product of the Multipliers of those Upgrades that change Stat and list Data; 1 when none does. */
	static float GetMultiplier(const TArray<TObjectPtr<URTSUpgradeData>>& Upgrades, const URTSEntityData& Data, ERTSUpgradeStat Stat);
};
