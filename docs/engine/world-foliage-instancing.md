# Foliage and mesh instancing

## What it is

Instancing draws many copies of one static mesh from a single component.
- `UInstancedStaticMeshComponent` (ISM) is the base type.
- `UHierarchicalInstancedStaticMeshComponent` (HISM) adds a CPU cluster tree.
- Foliage mode paints instances into a per-level `AInstancedFoliageActor` (IFA). Each foliage type there is a `UFoliageInstancedStaticMeshComponent`, which is a HISM.

PCG, Packed Level Actors and landscape grass all produce ISM or HISM instances. The same rules on culling, collision and navigation apply to all of them.

## Key concepts

- **ISM.**
  - Instances are transforms in `PerInstanceSMData`.
  - Rendering goes through GPU Scene, with per-instance GPU culling and GPU LOD selection (`r.InstancedStaticMeshes.GpuLod`, default 1).
  - API:
    - `AddInstance(Transform, bWorldSpace)` and `AddInstances(Transforms, bShouldReturnIndices, bWorldSpace, bUpdateNavigation)`
    - `UpdateInstanceTransform`, `BatchUpdateInstancesTransforms`, `RemoveInstance`, `ClearInstances`
    - `GetInstanceCount`
- **HISM.**
  - It subclasses ISM and builds a cluster tree on the CPU, asynchronously, after every change. The tree is used for CPU culling, LOD and occlusion queries.
  - Foliage keeps using it.
  - For instance sets that change often at runtime (spawned debris, units' wheels), use plain ISM to avoid tree rebuilds.
  - For Nanite meshes, culling is per Nanite cluster on the GPU either way, so HISM's tree adds little.
- **Foliage types.**
  - `UFoliageType_InstancedStaticMesh` assets (`UFoliageType`) store the painting rules (`Density`, `ScaleX`, `RandomYaw`, `AlignToNormal`, `ZOffset`, `GroundSlopeAngle`, `Height`) and the instance settings.
  - Instance settings: `CullDistance` (an `FInt32Interval` of start and end), `bEnableDensityScaling`, `bEnableCullDistanceScaling`, `CollisionWithWorld`, `BodyInstance`, `bCastDynamicShadow`, `bAffectDistanceFieldLighting`, `bAffectDynamicIndirectLighting`, `bVisibleInRayTracing`, `bUseAsOccluder`, `WorldPositionOffsetDisableDistance`, `bIncludeInHLOD`.
- **IFA placement.**
  - Without World Partition, there is one IFA per level.
  - With WP, IFAs are split per grid cell (`AWorldSettings::InstancedFoliageGridSize`). The `WorldPartitionFoliageBuilder` re-grids them.
- **Cull distances.**
  - ISM: `InstanceStartCullDistance` and `InstanceEndCullDistance`, or `SetCullDistances`. Instances fade or dither between start and end and are culled past end.
  - Foliage: the same values via `CullDistance`.
  - Scaled globally by `foliage.CullDistanceScale` (only for types with `bEnableCullDistanceScaling`) and capped by `foliage.MaxEndCullDistance`.
- **Density scaling.** `foliage.DensityScale` (scalability `sg.FoliageQuality`) removes instances at load for types with `bEnableDensityScaling`. Never set it on gameplay foliage.
- **Per-instance custom data.**
  - Call `SetNumCustomDataFloats(N)` on the component, then `SetCustomDataValue(InstanceIndex, DataIndex, Value, bMarkRenderStateDirty)`.
  - Read it in the material with `UMaterialExpressionPerInstanceCustomData` or `UMaterialExpressionPerInstanceCustomData3Vector`.
  - `UMaterialExpressionPerInstanceRandom` gives a free random value per instance.
  - Use custom data for tint, damage state or team colour without extra components.
  - The material needs "Used with Instanced Static Meshes" (`MATUSAGE_InstancedStaticMeshes`).
- **Nanite foliage.**
  - Nanite works on ISM and HISM. It removes per-instance LOD popping and draw-call cost, and is needed for Lumen and VSM to treat dense foliage well.
  - Masked, two-sided and WPO materials work with Nanite but cost more. Disable WPO at distance with `WorldPositionOffsetDisableDistance`.
  - The 5.8 "Nanite Foliage (Experimental)" project setting (`r.Nanite.Foliage`, read-only, default 0) enables the dedicated foliage path. Per mesh, the Nanite settings (`FMeshNaniteSettings`) choose a shape preservation (`ENaniteShapePreservation`):
    - `PreserveArea`: legacy foliage behaviour.
    - `Voxelize`: thin geometry turns into voxels at distance.
- **Procedural foliage (legacy).**
  - `UProceduralFoliageSpawner` assets plus an `AProceduralFoliageVolume` (`UProceduralFoliageComponent`) simulate growth and fill the IFA.
  - It needs Editor Preferences → Experimental → "Procedural Foliage" (`bProceduralFoliage`).
  - PCG supersedes it.
- **Landscape grass** is ISM or HISM generated at runtime from `ULandscapeGrassType`. It has no collision and is not saved; see [world-landscape.md](world-landscape.md).

## Rules and pitfalls

- Navigation:
  - Every instance with collision is navmesh geometry. Thousands of small trunk or rock instances riddle the navmesh with holes and raise build time and memory.
  - Turn navigation off per foliage type or component (`SetCanEverAffectNavigation(false)`, or `bCanEverAffectNavigation` in the descriptor).
  - Express "forest slows vehicles" as a `NavArea` volume instead of per-instance collision.
- Collision:
  - Collision is per instance, through the component's `BodyInstance`.
  - Use simple collision (a capsule or box on the trunk), never per-poly. Set canopy and clutter to no collision.
  - Keep foliage off WorldStatic ground traces unless it should block placement.
- Cull distances:
  - Set them from the camera, not the default 0 (never culled).
  - A top-down RTS camera sees a fixed footprint, so the cull end only needs to cover the farthest visible ground at max zoom. Culling closer than that pops at the screen edge.
- Shadows:
  - Small clutter should not cast shadows (`bCastDynamicShadow = false`). Dense shadow-casting foliage is the most common VSM page overflow.
  - Under VSM, WPO animation invalidates cached shadow pages every frame. Disable WPO at distance.
- Lumen:
  - Non-Nanite foliage is mostly invisible to Lumen's surface cache. Use Nanite for trees that should bounce light, or accept screen-space only.
  - `bAffectDistanceFieldLighting` costs distance-field memory. Turn it off for grass and clutter.
- Runtime changes:
  - Removing an instance shifts later indices down, or swaps the last instance into the hole when `bSupportRemoveAtSwap` is set (or `r.InstancedStaticMeshes.ForceRemoveAtSwap`). Either way, stored indices go stale. Keep your own id map, or use instance ids (`FPrimitiveInstanceId`, `SetCustomDataValueById`).
  - Batch transform updates (`BatchUpdateInstancesTransforms`) instead of per-instance calls. Pass `bMarkRenderStateDirty` only on the last call.
- Picking and selection: instances share one component. A hit gives `FHitResult::Item` as the instance index. Do not make individually selectable game objects instances unless you map the index.
- Foliage painted on a level is saved in the IFA. With OFPA the IFA is one external actor, so painting locks that file, not the `.umap`.
- `foliage.DensityScale` < 1 changes which trees exist. Never use it for foliage that blocks movement or sight.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `UInstancedStaticMeshComponent::AddInstance` / `AddInstances` | C++ | Add instances |
| `BatchUpdateInstancesTransforms` | C++ | Move many instances cheaply |
| `SetNumCustomDataFloats` / `SetCustomDataValue` | C++ | Per-instance material data |
| `SetCullDistances` | C++ | ISM start and end cull distance |
| `UPrimitiveComponent::SetCanEverAffectNavigation` | C++ | Remove from navmesh |
| `UStaticMeshComponent::SetWorldPositionOffsetDisableDistance` | C++ | Stop WPO at distance |
| `foliage.DensityScale` | cvar | Density scalability |
| `foliage.CullDistanceScale` / `foliage.MaxEndCullDistance` | cvar | Cull distance scale and cap |
| `foliage.LODDistanceScale`, `foliage.ForceLOD` | cvar | HISM LOD tuning and debug |
| `foliage.DitheredLOD` | cvar | Dithered LOD transitions |
| `foliage.RebuildFoliageTrees` | command | Rebuild HISM trees |
| `r.InstancedStaticMeshes.GpuLod` | cvar | GPU LOD for ISM |
| `r.InstanceCulling.OcclusionCull` | cvar | GPU instance occlusion culling |
| `r.Nanite.Foliage` | cvar (project setting, read-only) | Nanite foliage path |
| `grass.DensityScale`, `grass.CullDistanceScale` | cvar | Landscape grass |
| `stat Foliage` | command | Foliage CPU counters (`STATGROUP_Foliage`) |

## In World Divide

- Units use ISM for repeated parts: wheel sets are `UInstancedStaticMeshComponent`s built in `ARTSUnit` (`Source/WorldDivide/Units/RTSUnit.cpp`, tested in `RTSPartOcclusionTests`, `RTSRecoilTests`, `RTSTeamColorTests` and `RTSVehicleTests`). `RTSArtTests` checks masters for `MATUSAGE_InstancedStaticMeshes`.
- Placeholder units stay non-Nanite by design (`WorldDivide.Art.PlaceholderWithoutNanite`).
- No foliage or IFA exists yet. The plan ([RiverCrossing technical plan](../world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md) §6 and §11):
  - Woods are visual and cover only: trunk collision WorldStatic, no navmesh influence.
  - Grass ≤ 0.6 m with no collision, cull at about 150 m.
  - Nanite foliage or LODs plus impostors, with cull distances tuned to the max camera arm (450 m per [the world scale standard](../tech/scale/WORLD_SCALE_STANDARD.md)).
- Project docs win on conflict.

## Sources

- Epic, Building Virtual Worlds (UE 5.8): https://dev.epicgames.com/documentation/en-us/unreal-engine/building-virtual-worlds-in-unreal-engine
- Epic, Nanite Virtualized Geometry: https://dev.epicgames.com/documentation/unreal-engine/nanite-virtualized-geometry-in-unreal-engine
- Epic, Lumen Technical Details (foliage and ISM support): https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- Engine source (UE 5.8):
  - `Engine/Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h`, `HierarchicalInstancedStaticMeshComponent.h`, `StaticMeshComponent.h`
  - `Engine/Source/Runtime/Foliage/Public/FoliageType.h`, `FoliageInstancedStaticMeshComponent.h`
  - `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h` (`FMeshNaniteSettings`, `ENaniteShapePreservation`)
  - `Engine/Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp` (`r.Nanite.Foliage`)
