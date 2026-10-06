#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RTSSettings.generated.h"

class UMaterialInterface;
class URTSFactionData;

/** Project-wide asset references, kept in DefaultGame.ini so they stay reviewable text. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "World Divide"))
class WORLDDIVIDE_API URTSSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Every player gets this faction until faction choice exists. */
	UPROPERTY(Config, EditAnywhere, Category = "Factions")
	TSoftObjectPtr<URTSFactionData> DefaultFaction;

	/** Translucent material with a "Color" vector parameter, tinted green or red by placement validity. */
	UPROPERTY(Config, EditAnywhere, Category = "Placement")
	TSoftObjectPtr<UMaterialInterface> PlacementGhostMaterial;
};
