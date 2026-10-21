# PCG (Procedural Content Generation)

## What it is

PCG is Epic's node-graph framework for scattering and building content from rules. A `UPCGGraph` asset is run by a `UPCGComponent` on an actor (often an `APCGVolume`). The graph reads world data (landscape, splines, volumes, actors), turns it into points with attributes, filters them, and spawns static mesh instances, actors or other data.
Results can be generated in the editor and saved, or generated at runtime around generation sources.
In 5.8 the `PCG` plugin is production quality. Many interop plugins (`PCGWaterInterop`, `PCGMeshPartitionInterop`, `PCGBiomeCore`, and others) are experimental.

## Key concepts

- **Graph and instance.** A `UPCGGraph` is reusable. A `UPCGGraphInstance` is the same graph with overridden graph parameters (user parameters), so one forest graph serves several biomes.
- **`UPCGComponent`.** Key properties:
  - `Seed`: default 42.
  - `bActivated`.
  - `bIsComponentPartitioned`: shown as "Is Partitioned".
  - `GenerationTrigger`, one of the `EPCGComponentGenerationTrigger` values:
    - `GenerateOnLoad`: generates in `BeginPlay` unless already generated and saved.
    - `GenerateOnDemand`: only when called (`Generate`, `GenerateLocal`, the editor button).
    - `GenerateAtRuntime`: scheduled by the runtime generation scheduler around generation sources.
  - Generation calls: `Generate`/`GenerateLocal` (the networked vs local variants) and `Cleanup`/`CleanupLocal`.
- **`APCGVolume`.** A box actor with a PCG component. Its bounds are the generation domain.
- **Settings and nodes.** Every node is a `UPCGSettings` subclass. Common nodes (C++ class, then UI title):
  - Inputs:
    - `UPCGGetLandscapeSettings` (Get Landscape Data)
    - `UPCGGetSplineSettings` (Get Spline Data)
    - `UPCGDataFromActorSettings` (Get Actor Data)
    - `UPCGGetPrimitiveSettings`
    - `UPCGGetVirtualTextureSettings` (read an RVT)
  - Samplers:
    - `UPCGSurfaceSamplerSettings` (Surface Sampler): points on a surface such as the landscape, at a density per m².
    - `UPCGSplineSamplerSettings` (Spline Sampler): points on, along or inside a spline.
    - `UPCGCreatePointsGridSettings`
  - Filters and set operations:
    - `UPCGDensityFilterSettings`, `UPCGAttributeFilteringSettings`
    - `UPCGDifferenceSettings` (subtract exclusion zones), `UPCGInnerIntersectionSettings`, `UPCGOuterIntersectionSettings`, `UPCGUnionSettings`
    - `UPCGSelfPruningSettings` (remove overlaps), `UPCGDistanceSettings`
  - Modifiers:
    - `UPCGTransformPointsSettings` (random offset, rotation and scale)
    - `UPCGProjectionSettings`, `UPCGBoundsModifierSettings`, `UPCGNormalToDensitySettings`, `UPCGCopyPointsSettings`
    - `UPCGSampleTextureSettings`, `UPCGWorldRayHitSettings`
  - Output:
    - `UPCGStaticMeshSpawnerSettings` (Static Mesh Spawner)
    - `UPCGSpawnActorSettings`
    - `UPCGAddTagSettings`
- **Data types.**
  - Spatial data: `UPCGSurfaceData`, `UPCGLandscapeData`, `UPCGSplineData`.
  - Points: `UPCGBasePointData`, with `UPCGPointArrayData` as the default point container (`pcg.EnablePointArrayData` = true) and legacy `UPCGPointData` (an array of `FPCGPoint`).
  - Attribute sets: `UPCGParamData`.
- **Point properties** (`EPCGPointProperties`, written `$Name` in selectors): `$Position`, `$Rotation`, `$Scale`, `$Transform`, `$Density`, `$BoundsMin`, `$BoundsMax`, `$Extents`, `$Color`, `$Steepness`, `$Seed`, `$LocalCenter`, `$LocalSize`, `$ScaledLocalSize`.
- **Attributes.**
  - Custom attributes live in `UPCGMetadata` on the data. Landscape layer weights are read as attributes named after the paint layers.
  - Use attributes to drive mesh choice, scale and filtering.
- **Static Mesh Spawner.**
  - The selector (`MeshSelectorType`) is `UPCGMeshSelectorWeighted` (weighted list) or `UPCGMeshSelectorByAttribute` (mesh path from an attribute).
  - Each entry has an `FPCGSoftISMComponentDescriptor` holding the ISM settings: collision, cull distance, shadows and custom data.
  - The default component class is `UInstancedStaticMeshComponent`. Output components are `UPCGManagedISMComponent`-tracked managed resources, cleaned up on regenerate.
  - `bApplyMeshBoundsToPoints` writes mesh bounds back to points, for later pruning. `OutAttributeName` writes the chosen mesh to an attribute.
- **Partitioned generation.**
  - With `bIsComponentPartitioned`, the component's work is split over a grid of local components inside `APCGPartitionActor`s.
  - The grid size is `APCGWorldActor::PartitionGridSize`, unless the graph uses hierarchical generation.
  - In 5.8, partitioning works in non-WP levels too, since the PCG component code supports it there.
- **Hierarchical generation (HiGen).**
  - Set `bUseHierarchicalGeneration` on the graph. Grid Size nodes then run parts of the graph at different grid levels (`EPCGHiGenGrid`, default `HiGenGridSize` Grid256, scaled by `HiGenGridSizeMultiplier`).
  - Example: big trees on a 256 m grid, and small clutter on a 32 m grid that only generates near the camera at runtime.
- **Runtime generation.**
  - `GenerateAtRuntime` plus generation sources: player controllers, the editor camera, World Partition streaming sources, `UPCGGenSourceComponent`, or anything that implements `IPCGGenSourceBase`.
  - The scheduler generates and cleans up cells by distance (`GenerationRadii`, `SchedulingPolicy`).
  - On a server, `pcg.RuntimeGeneration.Server.Mode` controls the work: 0 off, 1 local sources only, 2 all sources (the default).
- **Determinism.**
  - Randomness comes from the component `Seed` combined with each node's `Seed` (when `bUseSeed`) and each point's `$Seed`, via `PCGHelpers::ComputeSeed` and `GetRandomStreamFromSeed`.
  - A sampler derives point seeds from position (`ComputeSeedFromPosition`).
  - Same inputs, same seeds and same graph version give the same output on every machine.
- **Execution.**
  - Graphs run on worker threads, time-sliced on the game thread: `pcg.FrameTime` 5 ms in game, `pcg.EditorFrameTime` 15 ms in the editor.
  - Results are cached per node (`pcg.Cache.*`).

## Rules and pitfalls

- Decide where generation happens:
  - **Editor and saved** (`GenerateOnDemand`, generated in the editor and saved): no runtime CPU. The instances are in the level, so the level file and its OFPA actors grow, and the result is the same on every client by construction. This is the default choice for a fixed competitive map.
  - **On load** (`GenerateOnLoad`): costs CPU at map load on every machine, and is skipped when the saved state is already generated.
  - **At runtime** (`GenerateAtRuntime`): continuous CPU and streaming hitches. Use it only for camera-local cosmetic detail.
- Procedural (componentless or GPU) instances are never saved and always regenerate at `BeginPlay`.
- Never let PCG output that affects gameplay (collision, navigation, line of sight) depend on runtime generation that clients run on their own. A seed or graph-version mismatch or a timing difference desyncs. Generate gameplay-relevant content in the editor, or on the server only via replicated actors.
- Exclusion is subtraction: build an exclusion data set (splines plus a distance, volumes, actor bounds) and `Difference` it from the sampled points before spawning. Prune overlaps with Self Pruning after bounds are set.
- Collision and navigation:
  - Set ISM collision and `bCanEverAffectNavigation` in the spawner's descriptor. The default descriptor inherits the mesh's collision.
  - Thousands of small collision instances cut the navmesh into noise. For decorative clutter, set no collision and no navigation influence.
- Seeds:
  - Changing a node's seed, inserting a random node upstream, or editing an input spline changes the output.
  - Lock the component `Seed`, and keep graphs versioned with the map.
- Landscape inputs are cached (`pcg.ClearLandscapeCache` clears it). Regenerate after sculpting, since a stale cache gives points at old heights.
- Partitioning:
  - Partition big areas, so regenerating one zone does not rebuild the whole map.
  - Each partition actor is a separate OFPA file. Commit them all.
- Spawned actors (Spawn Actor node) are much heavier than ISM instances. Prefer the Static Mesh Spawner for anything numerous.
- Runtime generation on a dedicated server wastes CPU for cosmetics. Set `pcg.RuntimeGeneration.Server.Mode` to 0, or keep cosmetic graphs client-only.
- Water interop (`PCGWaterInterop`) is experimental. A spline copy of the river works as an exclusion input without it.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `UPCGComponent::Generate` / `GenerateLocal` | C++/BP | Generate (networked or local) |
| `UPCGComponent::Cleanup` / `CleanupLocal` | C++/BP | Remove generated resources |
| `UPCGComponent::Seed`, `GenerationTrigger`, `bIsComponentPartitioned` | property | Core settings |
| `UPCGSubsystem` | world subsystem | Scheduling, partition actors, landscape cache |
| `PCGHelpers::ComputeSeed`, `ComputeSeedFromPosition` | C++ | Deterministic seeds in custom nodes |
| `pcg.FrameTime` / `pcg.EditorFrameTime` | cvar | Per-frame time budget (ms) |
| `pcg.RuntimeGeneration.Enable` | cvar | Master switch for runtime generation |
| `pcg.RuntimeGeneration.Server.Mode` | cvar | Server runtime generation (0, 1, 2) |
| `pcg.RuntimeGeneration.GlobalRadiusMultiplier` | cvar | Scale runtime generation radii (scalability) |
| `pcg.RuntimeGeneration.EnableDebugOverlay` | cvar | On-screen runtime generation debug |
| `pcg.RuntimeGeneration.Refresh` | command | Regenerate runtime components |
| `pcg.GraphExecution.DebugDrawGeneratedCells` | cvar | Draw generated cells |
| `pcg.ClearLandscapeCache` | command | Drop cached landscape data |
| `pcg.MaxConcurrentTasks` | cvar | Worker task limit |

## In World Divide

- The `PCG` plugin is enabled in `WorldDivide.uproject` (together with experimental `PCGMeshPartitionInterop`), but no graph, component or C++ use exists yet. [The RiverCrossing technical plan](../world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md) §3 still lists it as "not enabled"; that row is out of date.
- Planned use (plan §6): forests, copses, hedgerows and clutter per biome zone, generated in the editor. Exclusions cover the river, roads + 6 m, the 60 m bridge corridor, base pads, village streets and an 8 m radius around supply sources.
- Trunk collision is WorldStatic with no navigation influence, and the canopy has no collision.
- The match is multiplayer with client-side navigation (`bAllowClientSideNavigation`). Keep gameplay-relevant PCG output saved in the level, not runtime-generated.
- Project docs win on conflict.

## Sources

- Epic, Building Virtual Worlds (UE 5.8): https://dev.epicgames.com/documentation/en-us/unreal-engine/building-virtual-worlds-in-unreal-engine
- Epic, Procedural Content Generation Overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview
- Epic, PCG Generation Modes: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-generation-modes-in-unreal-engine
- Epic, PCG with World Partition: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-with-world-partition-in-unreal-engine
- Epic, PCG Development Guides: https://dev.epicgames.com/documentation/en-us/unreal-engine/pcg-development-guides
- Engine source (UE 5.8):
  - `Engine/Plugins/PCG/Source/PCG/Public/PCGComponent.h`, `PCGGraph.h`, `PCGWorldActor.h`, `PCGPoint.h`, `Helpers/PCGHelpers.h`, `Elements/PCGStaticMeshSpawner.h`
  - `Engine/Plugins/PCG/Source/PCG/Private/RuntimeGen/PCGGenSourceManager.cpp`, `Graph/PCGGraphExecutor.cpp`
