#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RTSSettings.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UWorld;
class URTSFactionData;
class URTSMapData;
class URTSHUDWidget;
class URTSUIStyleSet;

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

	/** Unlit additive material with a "Color" vector parameter, for muzzle flashes, shells and impacts. */
	UPROPERTY(Config, EditAnywhere, Category = "Effects")
	TSoftObjectPtr<UMaterialInterface> ShotEffectMaterial;

	/** A sphere 100 cm across, stretched into shells and grown into flashes. */
	UPROPERTY(Config, EditAnywhere, Category = "Effects")
	TSoftObjectPtr<UStaticMesh> ShotEffectMesh;

	/** Where the game starts, and where leaving the lobby returns to. */
	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	TSoftObjectPtr<UWorld> MainMenuMap;

	/** Where the players pick the seats of a skirmish. */
	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	TSoftObjectPtr<UWorld> LobbyMap;

	/** The maps the host picks from in the lobby, in the order it shows them; the first is the default. */
	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	TArray<TSoftObjectPtr<URTSMapData>> SkirmishMaps;

	/** The root of the match HUD that ARTSHUD shows the local player; unset is the C++ class. A Widget Blueprint subclass may set it for art. */
	UPROPERTY(Config, EditAnywhere, Category = "HUD")
	TSoftClassPtr<URTSHUDWidget> HUDWidgetClass;

	/** The shared sprites, tried after the local player's faction style; generated from UI/Sprites/Common.sprites.json. */
	UPROPERTY(Config, EditAnywhere, Category = "HUD")
	TSoftObjectPtr<URTSUIStyleSet> CommonUIStyle;

	/** The neutral theme, tried after Common; generated from UI/Themes/Neutral.theme.json. */
	UPROPERTY(Config, EditAnywhere, Category = "HUD")
	TSoftObjectPtr<URTSUIStyleSet> NeutralUIStyle;
};
