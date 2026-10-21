# World Partition, OFPA and level streaming

## What it is

World Partition (WP) stores a world as one persistent level whose actors are streamed in grid cells around streaming sources. Data Layers, HLODs and partitioned navigation build on top of it.
One File Per Actor (OFPA) saves each actor in its own package. WP requires OFPA, but OFPA also works on a plain level without WP.
Level Instances and Packed Level Actors reuse a level as a placeable prefab. Classic level streaming (sublevels) is the pre-WP alternative.
A fixed-size RTS map that is fully loaded for the whole match rarely benefits from WP streaming.

## Key concepts

- **`UWorldPartition`.**
  - It lives on the World Settings. `UWorld::IsPartitionedWorld()` and `UWorld::GetWorldPartition()` tell you whether a world uses it.
  - `bEnableStreaming` switches cell streaming on. A WP world with streaming off still gets OFPA and the WP editor tooling.
- **Runtime hash.** It decides how actors map to cells.
  - Default for new worlds in 5.8 is `UWorldPartitionRuntimeHashSet` (`RuntimeHashDefaultClass` in `BaseEngine.ini`). Its partitions are `URuntimePartitionLHGrid` (grid), `URuntimePartitionPersistent` (always loaded) and `URuntimePartitionLevelStreaming`. Each has a `LoadingRange`.
  - The legacy option is `UWorldPartitionRuntimeSpatialHash`, with `FSpatialHashRuntimeGrid` entries (`CellSize`, `LoadingRange`).
  - Actors choose a grid via `AActor::RuntimeGrid`. Set `bIsSpatiallyLoaded = false` to put an actor in the always-loaded partition.
- **Cell size and loading range.**
  - Cells load when a streaming source's range overlaps them.
  - Use a smaller `CellSize` for finer streaming and more cells, and a bigger `LoadingRange` for more memory and fewer pops.
  - A new `URuntimePartitionLHGrid` defaults to a 25600 uu (256 m) `LoadingRange` and a `CellSize` of half that (128 m). Open worlds raise the loading range per grid.
  - An RTS camera at a 450 m arm sees more than one default loading range, so streaming around it buys little.
- **Streaming sources.**
  - By default, `APlayerController` is a source (`bEnableStreamingSource`, `bStreamingSourceShouldActivate`).
  - Add more sources with `UWorldPartitionStreamingSourceComponent` (`EnableStreamingSource`, `TargetGrids`, `Priority`), or implement `IWorldPartitionStreamingSourceProvider` and return `FWorldPartitionStreamingSource`.
  - Server streaming is off by default (`wp.Runtime.EnableServerStreaming` = 0), so a dedicated server loads every cell.
- **OFPA (external actors).**
  - Actors are saved under `Content/__ExternalActors__/<map path>/` and external objects under `__ExternalObjects__`.
  - Turn it on with World Settings → *Use External Actors*, or from C++ with `ULevel::SetUseExternalActors(true)`. Check it with `ULevel::IsUsingExternalActors`.
  - The `.umap` keeps only level-wide data: World Settings, BSP and the landscape actor's own data.
- **Data Layers.**
  - A `UDataLayerAsset` is the content-browser asset. A `UDataLayerInstance` is its use in one world, stored in `AWorldDataLayers`. An `UExternalDataLayerAsset` is a data layer injected by a plugin.
  - Runtime states are `EDataLayerRuntimeState` `Unloaded`, `Loaded` and `Activated`.
  - Toggle them with `UDataLayerManager::SetDataLayerInstanceRuntimeState` or `SetDataLayerRuntimeState`. Get the manager with `UDataLayerManager::GetDataLayerManager(WorldContext)`.
  - `UDataLayerSubsystem` is the older entry point.
  - Data Layers work only in WP worlds.
- **HLOD.**
  - A `UHLODLayer` on actors (`AActor::SetHLODLayer`) or the world default (`UWorldPartition::GetDefaultHLODLayer`) builds `AWorldPartitionHLOD` proxy actors that stand in for unloaded cells.
  - Types are `EHLODLayerType` `Instancing`, `MeshMerge`, `MeshSimplify`, `MeshApproximate`, `Custom` and `CustomHLODActor`.
  - Build HLODs with the `UWorldPartitionHLODsBuilder` builder of `UWorldPartitionBuilderCommandlet`.
  - HLODs exist only with WP. The old `ALODActor` HLOD system for non-WP levels is legacy.
- **Level Instances.**
  - `ALevelInstance` places another level as an actor, and `ULevelInstanceSubsystem` manages edit and commit.
  - `ELevelInstanceRuntimeBehavior` has two values:
    - `Partitioned` (UI "Embedded"): actors are merged into the WP grid at cook time. This is the WP default.
    - `LevelStreaming` (UI "Standalone"): loaded as a streamed level at runtime. This works without WP.
- **Packed Level Actors.**
  - `APackedLevelActor` is a Level Instance whose static meshes are packed into ISM/HISM components inside a generated Blueprint.
  - Use it for cheap repeated prefabs such as fences, rock clusters or a village block.
  - Only static meshes pack. Logic actors are dropped.
- **Classic level streaming.**
  - Sublevels are `ULevelStreaming` objects in the Levels panel. Load them with `UGameplayStatics::LoadStreamLevel` and `UnloadStreamLevel`, or `ALevelStreamingVolume`.
  - Use `ULevelStreamingDynamic::LoadLevelInstance` and `LoadLevelInstanceBySoftObjectPtr` to spawn a level copy at a transform at runtime.
- **Conversion.** `UWorldPartitionConvertCommandlet` (`-run=WorldPartitionConvertCommandlet`) converts a classic level to WP. Converting back is manual.
- **Partitioned navigation.** In WP the navmesh is split into cells and built with `UWorldPartitionNavigationDataBuilder`.

## Rules and pitfalls

- Choose WP or a classic level when the map is created. Converting later rewrites every actor package and changes paths.
- With WP streaming on, an actor that is not loaded does not exist:
  - `TActorIterator`, `GetAllActorsOfClass` and `BeginPlay`-time scans miss unloaded cells.
  - Put gameplay-critical actors (start spots, nav bounds, supply sources) in the always-loaded partition (`bIsSpatiallyLoaded = false`), or turn streaming off.
- Multiplayer: a dedicated server loads everything unless `wp.Runtime.EnableServerStreaming` is set. Clients stream around their own player controller, so a client can lack actors that the server has.
- Do not rely on WP streaming to cap memory on a map that the camera can pan over completely in seconds. Every cell gets loaded anyway, and hitches move to mid-match.
- HLODs must be rebuilt after content changes. Stale HLODs show old geometry at distance. Commit HLOD output, or build it in CI.
- OFPA:
  - Every actor is a separate file. Use source control that handles many small files, and lock per actor, not per map.
  - Renaming or moving a map moves its `__ExternalActors__` folder. Do it in the editor, never on disk.
  - Untracked external actor files in git produce "missing actor" or "duplicate actor" reports. Commit them with the map.
  - Non-actor level data (World Settings, landscape heightmaps on the `ALandscape`) still lives in the `.umap`, so sculpt sessions still lock the map.
- Packed Level Actors:
  - Re-pack after editing the source level.
  - Per-instance collision follows the ISM settings, so check the navigation impact of the packed meshes.
- Level Instance in `LevelStreaming` mode costs a streamed level per instance. Avoid dozens of them, as Epic's docs note.
- Data Layers are not a team or fog-of-war system. Runtime data layer state set on the server replicates to every client.
- A world partitioned in the editor and a non-WP world have different editor workflows: Region loading and the WP editor window replace the Levels panel. Scripts that call `EditorLevelLibrary` or `EditorLoadingAndSavingUtils` on sublevels do not apply.

### When an RTS map should use WP

| Use WP | Stay with a standard level (+ OFPA) |
|---|---|
| Campaign or open map much larger than the camera's reach (≥ 8 km), with large regions never seen in a match | Skirmish maps of 0.5–4 km that are fully loaded and fully pannable |
| Many people editing one map at once, using WP regions and Data Layers | Few editors. OFPA alone gives per-actor locking |
| Content that should load by state (a seasonal layer, a destroyed variant) via Data Layers | State changes done by gameplay actors (destructible buildings, spawned props) |
| HLODs needed for a vast background ring | A border ring handled by Nanite and distance culling |

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `UWorld::IsPartitionedWorld` / `GetWorldPartition` | C++ | Detect WP |
| `ULevel::SetUseExternalActors`, `ULevel::IsUsingExternalActors` | C++ | OFPA on and off, and query it |
| `UDataLayerManager::SetDataLayerInstanceRuntimeState` | C++ | Load or activate a data layer |
| `UWorldPartitionSubsystem::IsStreamingCompleted` | C++ | Wait for streaming (loading screens, tests) |
| `UWorldPartitionStreamingSourceComponent` | component | Extra streaming source, such as an RTS camera |
| `ULevelStreamingDynamic::LoadLevelInstance` | C++ | Runtime level copy (non-WP) |
| `UGameplayStatics::LoadStreamLevel` / `UnloadStreamLevel` | BP/C++ | Sublevel streaming |
| `wp.Runtime.ToggleDrawRuntimeHash2D` | command | 2D cell and streaming debug overlay |
| `wp.Runtime.ToggleDrawStreamingSources` | command | Draw streaming sources |
| `wp.Runtime.ToggleDrawDataLayers` | command | List data layer states |
| `wp.Runtime.OverrideRuntimeSpatialHashLoadingRange` | cvar | Override loading range (spatial hash) |
| `wp.Runtime.EnableServerStreaming` | cvar (read-only at runtime) | Server-side streaming; 0 by default |
| `wp.Runtime.BlockOnSlowStreaming` | cvar | Block the game thread when streaming falls behind |
| `wp.Runtime.HLOD` | command | Toggle HLOD loading and rendering |
| `-run=WorldPartitionBuilderCommandlet -Builder=WorldPartitionHLODsBuilder` | commandlet | Build HLODs |
| `-run=WorldPartitionConvertCommandlet` | commandlet | Convert a level to WP |

## In World Divide

- No World Partition: every level is a standard level, fully loaded, and game code scans actors at `BeginPlay` (camera bounds from `TActorIterator<ANavMeshBoundsVolume>`, start spots, supply sources).
- WD_RiverCrossing_01 uses OFPA without WP. `URTSEditorScripting::UseExternalActors` (`Source/WorldDivide/Core/RTSEditorScripting.h`) calls `SetUseExternalActors(true)` from `Tools/Editor/rivercrossing_greybox.py`. Actors are under `Content/__ExternalActors__/Maps/WD_RiverCrossing_01/`.
- HLOD is not used. Nanite plus distance culling covers the border ring. Other levels are regenerated by `setup_testmap.py`.
- Rationale and the OFPA test result (#154) are in [the RiverCrossing technical plan](../world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md) §2 and §11. See also [authoring a map](../world/maps/AUTHORING_A_MAP.md).
- Project docs win on conflict.

## Sources

- Epic, Building Virtual Worlds (UE 5.8): https://dev.epicgames.com/documentation/en-us/unreal-engine/building-virtual-worlds-in-unreal-engine
- Epic, World Partition: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine
- Epic, World Partition Data Layers: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---data-layers-in-unreal-engine
- Epic, World Partition HLOD: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine
- Epic, Level Instancing: https://dev.epicgames.com/documentation/unreal-engine/level-instancing-in-unreal-engine
- Epic, World Partitioned Navigation Mesh: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partitioned-navigation-mesh
- Engine source (UE 5.8):
  - `Engine/Source/Runtime/Engine/Public/WorldPartition/` (`WorldPartition.h`, `RuntimeHashSet/`, `DataLayer/DataLayerManager.h`, `HLOD/HLODLayer.h`)
  - `Engine/Source/Runtime/Engine/Public/LevelInstance/LevelInstanceTypes.h`
  - `Engine/Source/Runtime/Engine/Public/PackedLevelActor/PackedLevelActor.h`
  - `Engine/Config/BaseEngine.ini` (`RuntimeHashDefaultClass`)
