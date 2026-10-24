#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RTSEditorScripting.generated.h"

class ALandscape;
class ULandscapeLayerInfoObject;
class UFont;
class UFontFace;
class UMaterialInterface;
class UNiagaraSystem;
class UPCGGraph;
class UPhysicalMaterial;
class URuntimeVirtualTexture;
class USkinnedAsset;
class USoundCue;
class UStaticMesh;
class USoundWave;

/** One mesh a vegetation graph spawns (BuildVegetationGraph), Weight against the other entries of its list. */
USTRUCT(BlueprintType)
struct FRTSVegetationEntry
{
	GENERATED_BODY()

	/** A skeletal mesh: the Megaplant trees and shrubs are skeletal Nanite assemblies. None when StaticMesh is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	TObjectPtr<USkinnedAsset> Mesh;

	/** A static mesh instead of Mesh (hedgerow shrubs, clutter and ground cover, #463): never a tree, so never a trunk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	TObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	float Weight = 1.f;

	/** A tree, whose trunk infantry walks around (when its graph has bTrunks); a shrub has none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	bool bTrunk = true;

	/** This entry's points scaled by it on top of the graph's scale range: a 5 m rock face as a 1 m boulder, a shrub to a hedge's height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	float ScaleFactor = 1.f;
};

/** What one vegetation graph (forest, copse, riverbank, border ring, hedgerow, clutter, wood floor) scatters, in BuildVegetationGraph. */
USTRUCT(BlueprintType)
struct FRTSVegetationGraphSpec
{
	GENERATED_BODY()

	/** Points sampled on the landscape per m² of the volume, one per grid cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	float PointsPerSquaredMeter = 0.01f;

	/** How far each point may stray from its cell's centre, 0 (a grid) to 1 (anywhere in the cell). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	float Looseness = 1.f;

	/** Uniform scale range of the points outside the lane bands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	FVector2D Scale = FVector2D(0.85, 1.15);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	TArray<FRTSVegetationEntry> Entries;

	/** Inside the lane bands (actors tagged LaneTag): only these, at LaneScale, so no tree there tops the lanes' limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	TArray<FRTSVegetationEntry> LaneEntries;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	FVector2D LaneScale = FVector2D(0.85, 1.0);

	/** Actors whose bounds nothing grows in: river, roads, bridge corridors, pads, streets, supply sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	FName ExcludeTag = TEXT("PCG_Exclude");

	/** When set, only inside the bounds of actors tagged so (riverbank strips); none, the whole volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	FName IncludeTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	FName LaneTag = TEXT("PCG_LaneBand");

	/** Whether its trees' trunks cut the infantry navmesh (URTSTrunkObstaclesComponent, made by GeneratePCG). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	bool bTrunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	bool bCastShadow = true;

	/** Distance (cm) the instances fade out at; 0 never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vegetation")
	int32 CullDistance = 0;
};

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
	 * greybox (rivercrossing_greybox.py, #154): Python cannot import a heightmap. Layers are its paint layers (#156), each
	 * painted from the 8-bit weightmap of the same index in WeightmapFileNames (raw, as the heightmap, 255 full weight).
	 * Returns null and logs why when a file is missing or the wrong size.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static ALandscape* CreateLandscape(UObject* WorldContextObject, const FString& HeightmapFileName, int32 Components, int32 QuadsPerSection,
		FVector Location, FVector Scale, UMaterialInterface* Material, const TArray<ULandscapeLayerInfoObject*>& Layers,
		const TArray<FString>& WeightmapFileNames);

	/**
	 * The landscape layer info AssetPath (such as /Game/World/Landscape/Layers/LI_Grass) for the paint layer LayerName, made
	 * when it does not exist yet (#156): advanced weight blending (PremultipliedAlphaBlending, docs/engine/world-landscape.md)
	 * and PhysMaterial, which the landscape's collision reports where the layer is the strongest, so the vehicles' dust
	 * reads the surface. Python cannot make the asset. Does not save.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static ULandscapeLayerInfoObject* CreateLandscapeLayerInfo(const FString& AssetPath, FName LayerName, UPhysicalMaterial* PhysMaterial);

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
	 * afterwards keeps and what its navmesh builds on; that needs a renderer (-AllowCommandletRendering). It then bakes the
	 * carve into the base edit layer and destroys the brush and its Water layer (#382), so the saved level has no actor of
	 * the editor-only WaterEditor module and editing the river no longer carves the landscape. Returns null and
	 * logs why when the world has no landscape, there is no renderer or the river did not carve the landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static AActor* CreateRiver(UObject* WorldContextObject, const TArray<FVector>& Points, const TArray<float>& Widths, const TArray<float>& Depths,
		float BankAngle, float ShoreHeight, UMaterialInterface* WaterMaterial);

	/**
	 * Gives the landscape of WorldContextObject's world a runtime virtual texture (#383): the asset AssetPath (such as
	 * /Game/World/Landscape/RVT_Name), made when it does not exist yet, an ARuntimeVirtualTextureVolume labelled RVT_Landscape
	 * over the landscape's bounds, and the asset in the landscape's RuntimeVirtualTextures. Python cannot make the asset
	 * without the editor's factory; it sets the asset's size and content afterwards. Returns null and logs why when the world
	 * has no landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static URuntimeVirtualTexture* CreateLandscapeVirtualTexture(UObject* WorldContextObject, const FString& AssetPath);

	/**
	 * Lays a road along Points (world cm, Z the road's surface) as landscape splines of WorldContextObject's landscape (#383),
	 * as the landscape editor's spline tool does: a control point per point, one already there within a metre reused, so a
	 * road that carries straight on from another shares its end (a control point has one direction, so a branch should end
	 * beside the through road rather than on it), and a segment between each pair. With bShapeGround the splines raise and
	 * lower the ground to the road, Width cm wide, blending into it over SideFalloff cm, through the landscape's reserved
	 * splines edit layer, made when it is missing; without, the road only lies on the ground there (a bridge ramp, whose steep
	 * sides keep the navmesh linked across a tile seam, 03_TECHNICAL_PLAN.md section 8). Each segment is tiled with Mesh in
	 * Material, TileLength cm a tile, scaled to the road's width and half a metre deep, its top a few cm over the ground,
	 * with no collision and no shadow. With a VirtualTexture the meshes draw only into it, over the landscape, whose material
	 * reads it (#156); without, in the main pass. LayerName, unless none, is the paint layer the road lays on the landscape
	 * under it and its falloff, which gives the road its physical material, since the meshes have no collision; moving the
	 * road in the editor moves its paint. Python cannot make spline points. The ground changes only in ApplyLandscapeSplines.
	 * Returns how many segments it added, 0 and logs why when the world has no landscape or Points are fewer than two.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static int32 AddLandscapeSplineRoad(UObject* WorldContextObject, const TArray<FVector>& Points, float Width, float SideFalloff,
		bool bShapeGround, UStaticMesh* Mesh, float TileLength, UMaterialInterface* Material, URuntimeVirtualTexture* VirtualTexture,
		FName LayerName);

	/**
	 * Applies the landscape splines of WorldContextObject's world to its landscape's heights and collision, merging the edit
	 * layers as CreateRiver does, so a level saved afterwards and its navmesh have the roads' ground. The splines layer
	 * stays (it is a runtime Landscape class, unlike the river's brush), so moving a road in the editor reshapes the ground.
	 * Needs a renderer. Returns false and logs why when the merge did not finish or the ground under the splines' segments
	 * did not reach the road.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static bool ApplyLandscapeSplines(UObject* WorldContextObject);

	/**
	 * Stores every actor of WorldContextObject's level in its own file (One File Per Actor), as the World Settings' "Use
	 * External Actors" does, so actors can be LFS-locked one by one. Returns false without a level.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static bool UseExternalActors(UObject* WorldContextObject);

	/**
	 * Spawns the actor Label in WorldContextObject's world with one instanced static mesh component per mesh of Meshes, each
	 * holding an instance at every one of Transforms (world), for map dressing such as River Crossing's fences, lamps and
	 * poles (#157): a modular prop's parts share one origin, so they go in one call. The instances have no collision and
	 * never affect navigation; Python cannot add components to an actor. Returns null and logs why without a world, a mesh
	 * or a transform.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static AActor* SpawnPropInstances(UObject* WorldContextObject, const FString& Label, const TArray<UStaticMesh*>& Meshes,
		const TArray<FTransform>& Transforms);

	/**
	 * Spawns the actor Label in WorldContextObject's world with a spline mesh of Mesh (modelled along +X) from each of Starts
	 * to the matching one of Ends (world cm), hanging Sag of its length lower in the middle, as a power line's cables between
	 * its poles' insulators (#157). No collision, no navigation, no shadow. Returns null and logs why without a world or a
	 * mesh, or when Starts and Ends differ in number or are empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static AActor* SpawnCables(UObject* WorldContextObject, const FString& Label, UStaticMesh* Mesh, const TArray<FVector>& Starts,
		const TArray<FVector>& Ends, float Sag);

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

	/**
	 * Spawns the actor Label in WorldContextObject's world with one instanced skinned mesh component of Mesh, an instance at
	 * every one of Transforms (world), for River Crossing's hand-placed hero trees (#158): the Megaplant trees are skeletal
	 * Nanite assemblies, which Python cannot instance. The instances are trees (tag WD_Tree): no collision and no navigation,
	 * so vehicles drive through them (D4), shadowed and culled at CullDistance (cm); with bTrunks their trunks in the infantry
	 * navmesh (URTSTrunkObstaclesComponent). Without Mesh only the trunks, as the navigation test spawns them. An actor
	 * labelled Label already there is emptied and refilled, so its actor file stays. Returns null and logs why without a
	 * world, with neither a mesh nor trunks, or without a transform.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static AActor* SpawnTreeInstances(UObject* WorldContextObject, const FString& Label, USkinnedAsset* Mesh, const TArray<FTransform>& Transforms,
		int32 CullDistance, bool bTrunks);

	/**
	 * Writes the PCG graph AssetPath (#158) that scatters Spec over the landscape inside the volume of the component running
	 * it, with that component's seed, so any map can reuse it with its own volume and seed (#322): the sampled points minus
	 * Spec's exclusion actors, the lane bands' points from Spec.LaneEntries, the rest from Spec.Entries, each picked by
	 * weight, as instanced skinned meshes or, for entries with a StaticMesh, instanced static meshes (no collision, no
	 * navigation), and with Spec.bTrunks a hidden point at every skinned tree's trunk; with Spec.IncludeTag only inside the actors tagged so. An existing graph is emptied and rebuilt, so the
	 * volumes using it keep it. Returns null and logs why when it fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor")
	static UPCGGraph* BuildVegetationGraph(const FString& AssetPath, const FRTSVegetationGraphSpec& Spec);

	/**
	 * Generates every PCG component of the actors tagged Tag in WorldContextObject's world, waiting for each, so a map saved
	 * afterwards holds the result (generated in the editor, never at runtime: clients path on their own navmesh). Then
	 * tags the trees WD_Tree, saves them never ticking and turns each actor's trunk points into its
	 * URTSTrunkObstaclesComponent. Logs each actor's instance count, trunks and a hash of the instances' locations, so two
	 * runs can be compared. Returns the instances spawned, or -1 when a component did not finish.
	 */
	UFUNCTION(BlueprintCallable, Category = "World Divide|Editor", meta = (WorldContext = "WorldContextObject"))
	static int32 GeneratePCG(UObject* WorldContextObject, FName Tag);
};
