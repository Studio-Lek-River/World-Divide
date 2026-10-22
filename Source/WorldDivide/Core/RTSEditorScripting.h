#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RTSEditorScripting.generated.h"

class ALandscape;
class UFont;
class UFontFace;
class UMaterialInterface;
class UNiagaraSystem;
class UPhysicalMaterial;
class USoundCue;
class USoundWave;

/** Engine calls Tools/Editor scripts need that Unreal does not expose to Python. */
UCLASS()
class WORLDDIVIDE_API URTSEditorScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds the navmesh of every nav agent class (RTSNavAgents) in WorldContextObject's world from scratch and waits for
	 * them, so a map saved afterwards carries them and plays with full navmeshes straight away. Logs each one's tiles and
	 * memory and the time the build took. Returns the fewest tiles any class got, 0 when one has no navmesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static int32 BuildNavigation(UObject* WorldContextObject);

	/**
	 * Whether this process has a renderer, so CapturePreview can work: false under -nullrhi and in a commandlet run
	 * without -AllowCommandletRendering.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static bool CanRender();

	/**
	 * Where the camera of a map preview sits over Area and how wide it sees, looking straight down with world +X up and
	 * world +Y right on the image, as URTSMapData::ToPreviewPosition. Area must be square.
	 */
	static void GetPreviewCamera(const FBox2D& Area, FVector& OutLocation, FRotator& OutRotation, double& OutOrthoWidth);

	/**
	 * Renders WorldContextObject's world top-down over Area into a Size x Size PNG at FileName, through the camera of
	 * GetPreviewCamera. Returns false when there is no renderer or the image could not be written.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static bool CapturePreview(UObject* WorldContextObject, FBox2D Area, int32 Size, const FString& FileName);

	/**
	 * What bake_map.py (#289) reads from WorldContextObject's level: OutArea, the square its preview covers
	 * (RTSMaps::FindMapArea), and OutSpotLocations, the location of start spot N at index N (RTSMaps::FindStartSpots). Each
	 * start spot used twice or left out, and a level with no area or no start spots, goes to OutProblems. Returns nothing:
	 * Python drops the out parameters of a function that returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static void ReadMapLayout(UObject* WorldContextObject, FBox2D& OutArea, TArray<FVector>& OutSpotLocations, TArray<FString>& OutProblems);

	/**
	 * Spawns a Landscape in WorldContextObject's world from the 16-bit heightmap at HeightmapFileName (raw, little endian,
	 * row by row along +X, 32768 at the actor's Z), Components x Components components of 2 x 2 sections of QuadsPerSection
	 * quads each, its first vertex at Location and Scale cm per quad (Z: cm per 128 height steps), drawn in Material, for a map's scripted
	 * greybox (rivercrossing_greybox.py, #154): Python cannot import a heightmap. Returns null and logs why when the file is
	 * missing or the wrong size.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static ALandscape* CreateLandscape(UObject* WorldContextObject, const FString& HeightmapFileName, int32 Components, int32 QuadsPerSection,
		FVector Location, FVector Scale, UMaterialInterface* Material);

	/**
	 * Gives every landscape in WorldContextObject's world Material as its default physical material, the one its collision
	 * reports where no layer has its own (#337), and rebuilds the collision of each one that changed, since the physical
	 * materials are cooked into it. Returns how many changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static int32 SetLandscapePhysicalMaterial(UObject* WorldContextObject, UPhysicalMaterial* Material);

	/**
	 * Lays a Water plugin river (#155) in WorldContextObject's world along Points (world cm, Z its surface), Widths and
	 * Depths cm at each point, as the editor's Place Actors panel does, so it comes with the water zone that draws it and
	 * the water brush that carves it into the world's landscape through the landscape's Water edit layer. The bed lies
	 * Depth under the surface and the banks rise from the water's edge at BankAngle degrees, ShoreHeight cm over the
	 * surface at the edge. The river has no collision, so world-static ground traces (RTSGround) never stop on its surface,
	 * and is a null nav area. It merges the edit layers before it returns, so the carved heights are what a level saved
	 * afterwards keeps and what its navmesh builds on; that needs a renderer (-AllowCommandletRendering). Returns null and
	 * logs why when the world has no landscape, there is no renderer or the river did not carve the landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static AActor* CreateRiver(UObject* WorldContextObject, const TArray<FVector>& Points, const TArray<float>& Widths, const TArray<float>& Depths,
		float BankAngle, float ShoreHeight, UMaterialInterface* WaterMaterial);

	/**
	 * Stores every actor of WorldContextObject's level in its own file (One File Per Actor), as the World Settings' "Use
	 * External Actors" does, so actors can be LFS-locked one by one. Returns false without a level.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static bool UseExternalActors(UObject* WorldContextObject);

	/**
	 * Makes Font a runtime font whose default typeface is Face alone, for the UI fonts setup_testmap.py imports: Python
	 * cannot set the font face of a typeface entry. Returns false without a font or a face.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static bool BuildFont(UFont* Font, UFontFace* Face);

	/**
	 * Imports the TTF or OTF at FileName as the font face asset AssetPath (such as /Game/UI/Fonts/FF_Name), replacing the
	 * one there, with its data inline. The engine's font import flushes the Slate font cache, which a commandlet does not
	 * have. Returns null when the file cannot be read.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static UFontFace* ImportFontFace(const FString& FileName, const FString& AssetPath);

	/**
	 * Loads the editor module ModuleName, which a commandlet leaves out, so that its editor subsystems exist: the
	 * StaticMeshEditor module's StaticMeshEditorSubsystem makes vehicle_art.py's LODs (#149). Returns whether it is loaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static bool LoadEditorModule(FName ModuleName);

	/**
	 * Builds the muzzle flash of vehicle guns (#220, #294) as the Niagara system AssetPath (such as
	 * /Game/Effects/NS_MuzzleFlash), new, in place of the one there: Python cannot edit a Niagara system's modules. The
	 * tongue, the blast cone, the brake jets and the sparks glow in GlowMaterial, the fireball burns in FireMaterial and the
	 * glare (#354) washes over the view in GlareMaterial. Returns null and logs why when it fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static UNiagaraSystem* BuildMuzzleFlashSystem(const FString& AssetPath, UMaterialInterface* GlowMaterial, UMaterialInterface* FireMaterial,
		UMaterialInterface* GlareMaterial);

	/**
	 * Builds the muzzle smoke of vehicle cannons (#221, #303, #355) and the ground blast (#294) as the Niagara system AssetPath
	 * (such as /Game/Effects/NS_MuzzleSmoke), new, in place of the one there, as BuildMuzzleFlashSystem: the blast clouds and
	 * bore puffs in BlastMaterial, the veil over the gun in VeilMaterial and the dust in DustMaterial. Returns null and logs
	 * why when it fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static UNiagaraSystem* BuildMuzzleSmokeSystem(const FString& AssetPath, UMaterialInterface* BlastMaterial, UMaterialInterface* VeilMaterial,
		UMaterialInterface* DustMaterial);

	/**
	 * Builds the engine exhaust of vehicles (#222) as the Niagara system AssetPath (such as /Game/Effects/NS_Exhaust), new, in
	 * place of the one there, as BuildMuzzleFlashSystem: a continuous stream of thin puffs in Material, at User.Rate a second.
	 * Returns null and logs why when it fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static UNiagaraSystem* BuildExhaustSystem(const FString& AssetPath, UMaterialInterface* Material);

	/**
	 * Builds the dust ground vehicles raise behind their tracks or wheels (#224) as the Niagara system AssetPath (such as
	 * /Game/Effects/NS_Dust), new, in place of the one there, as BuildMuzzleFlashSystem: at User.Rate a second, a low haze
	 * in HazeMaterial that spreads along the ground and settles, and chunks in ChunkMaterial thrown back and falling again,
	 * each shaped by the ground's look (FRTSDustSurface, #336). Returns null and logs why when it fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static UNiagaraSystem* BuildDustSystem(const FString& AssetPath, UMaterialInterface* HazeMaterial, UMaterialInterface* ChunkMaterial);

	/**
	 * Builds a cannon's firing sound (#356) as the sound cue AssetPath, new, in place of the one there: Python cannot build a
	 * cue's graph. Waves holds the takes in threes, a blast, a body and an echo tail each; every shot plays one take at random,
	 * the blast fading out with distance before the body and the echo last, duller the further away. At most a dozen play at
	 * once. Returns null and logs why when it fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static USoundCue* BuildLayeredShotSound(const FString& AssetPath, const TArray<USoundWave*>& Waves);
};
