# Landscape

## What it is

`ALandscape` is UE's heightfield terrain: one actor that owns a grid of `ULandscapeComponent`s, each rendering a patch of a 16-bit heightmap plus paint-layer weightmaps.
It brings LOD, heightfield collision, paint layers, non-destructive edit layers, splines that deform terrain, and optional Nanite rendering.
In UE 5.8, edit layers are always on (`bCanHaveLayersContent` is deprecated). The Water plugin carves the terrain through its own edit layer.
The experimental Mesh Terrain alternative (`MeshPartition`, `MeshTerrainMode`) is separate and is not covered here.

## Key concepts

- **Actor and proxies.**
  - `ALandscape` is the parent actor. It holds the shared settings and the edit-layer stack.
  - `ALandscapeProxy` is the base class.
  - `ALandscapeStreamingProxy` holds the components of one region. Proxies exist only when the landscape is split, which World Partition does by default.
  - A non-WP level usually keeps every component on the single `ALandscape`.
  - `ULandscapeInfo` ties the landscape and its proxies together at edit time: layers, component map and `UpdateLayerInfoMap`.
- **Components, sections and quads.**
  - A component is 1×1 or 2×2 sections. A section is `SubsectionSizeQuads` quads per side: 7, 15, 31, 63, 127 or 255.
  - `ComponentSizeQuads` = sections × quads per section.
  - Vertices per side = components × `ComponentSizeQuads` + 1.
  - Each component is one render and collision unit, so CPU cost scales with the component count.
- **Recommended sizes** (vertices per side, from components × sections × quads):

  | Vertices | Layout | Note |
  |---|---|---|
  | 505 | 8×8 comp, 1×1 sec, 63 quads | Small test map |
  | 1009 | 8×8 comp, 2×2 sec, 63 quads | |
  | 2017 | 16×16 comp, 2×2 sec, 63 quads | RiverCrossing (256 components) |
  | 4033 | 32×32 comp, 2×2 sec, 63 quads | Upper end for one non-WP actor |
  | 8129 | 32×32 comp, 2×2 sec, 127 quads | Open-world size; use WP |

  Keep the component count to about 1024 or fewer. Prefer bigger sections (63 or 127 quads) over more components.
- **Scale.**
  - X/Y scale is centimetres per quad. At 100, one quad is 1 m.
  - Z scale 100 gives ±256 m: world Z (cm) = (height − 32768) × ZScale / 128.
- **Heightmap import.**
  - The source is a 16-bit grayscale PNG or a raw `.r16` (little endian) whose size exactly matches the vertex count. Import it in Landscape mode (Manage → New → Import from File).
  - In C++, `ALandscape::Import` takes per-edit-layer height arrays plus `FLandscapeImportLayerInfo` paint data and an `ELandscapeImportAlphamapType`.
- **Landscape material.**
  - Set it with `LandscapeMaterial`. `LandscapeHoleMaterial` is optional and is used for visibility holes.
  - Layers are blended with `UMaterialExpressionLandscapeLayerBlend`, choosing per layer from `LB_WeightBlend`, `LB_AlphaBlend` or `LB_HeightBlend`.
  - Other material nodes: `UMaterialExpressionLandscapeLayerSample`, `UMaterialExpressionLandscapeLayerWeight` and `UMaterialExpressionLandscapeLayerCoords` (UVs).
- **Paint layers (target layers).**
  - Each layer name in the material needs a `ULandscapeLayerInfoObject` asset, the layer info.
  - The layer info carries `PhysMaterial` (per-layer physical material for traces and footstep or dust effects) and a blend method, `ELandscapeTargetLayerBlendMethod`:
    - `None`: no weight blending, for masks and decals.
    - `FinalWeightBlending`: legacy weight blending; the weights sum to 1 after the edit layers merge.
    - `PremultipliedAlphaBlending`: advanced weight blending, applied per edit layer. Prefer it with edit layers.
  - `bNoWeightBlend` is deprecated since 5.7; use `SetBlendMethod`.
- **Edit layers.**
  - These are non-destructive height and weight layers stacked on the landscape, such as a base sculpt, gameplay flats or splines.
  - Classes: `ULandscapeEditLayerBase`, `ULandscapeEditLayerSplines`, and `FLandscapeLayer` (legacy struct).
  - Brushes and blueprint brushes plug in via `ALandscapeBlueprintBrushBase`. Water and LandscapePatch (`ALandscapePatchManager`, `ULandscapeCircleHeightPatch`, `ULandscapeTexturePatch`) use this.
- **Grass.**
  - Procedural ground cover is driven by the material through `UMaterialExpressionLandscapeGrassOutput` and `ULandscapeGrassType` assets.
  - It is generated at runtime around the camera. It has no collision and is not saved in the level.
- **Runtime Virtual Texturing (RVT).**
  - A `URuntimeVirtualTexture` asset plus an `ARuntimeVirtualTextureVolume` (`URuntimeVirtualTextureComponent`) bound to the landscape.
  - The landscape lists the asset in `RuntimeVirtualTextures`.
  - The material writes to it with `UMaterialExpressionRuntimeVirtualTextureOutput`. Other materials, such as roads, decals and foliage tinted to the ground, read it with `UMaterialExpressionRuntimeVirtualTextureSample`.
  - `VirtualTextureRenderPassType` decides whether the landscape still draws in the main pass.
  - RVT needs "Enable virtual texture support" (`r.VirtualTextures`) in the project.
- **Nanite landscape.**
  - Set `bEnableNanite` on the landscape. This builds a Nanite mesh per proxy from the heightmap.
  - It renders through Nanite, which suits VSM and dense displacement. Collision, grass, RVT and editing still use the heightfield.
  - Related settings: `bNaniteSkirtEnabled`, `NaniteSkirtDepth`, `NaniteMaxEdgeLengthFactor`.
- **LOD (non-Nanite).** Controlled by `LOD0ScreenSize`, `LOD0DistributionSetting` and `LODDistributionSetting` on `ALandscapeProxy`.
- **Collision.**
  - Each component gets a `ULandscapeHeightfieldCollisionComponent`.
  - `CollisionMipLevel` sets complex collision resolution and `SimpleCollisionMipLevel` sets simple collision; 0 is full resolution.
  - `DefaultPhysMaterial` applies where no layer has one. `BodyInstance` holds the profile (WorldStatic by default).
- **Landscape splines.**
  - `ULandscapeSplinesComponent` holds `ULandscapeSplineControlPoint`s and `ULandscapeSplineSegment`s.
  - `bRaiseTerrain` and `bLowerTerrain` deform the heights, `LayerName` paints a layer along the spline, and spline meshes (roads, kerbs, walls) are attached to segments.
  - Under WP, splines live in `ALandscapeSplineActor`s, which are not placeable by hand.
  - With edit layers, the deformation goes to a reserved splines layer (`ULandscapeEditLayerSplines`).
- **Water plugin** (experimental in 5.8):
  - `AWaterBodyRiver` and the other `AWaterBody` types sit on a `UWaterBodyComponent`. With `bAffectsLandscape` set, they carve the landscape through a water brush (`AWaterBrushManager`) on an edit layer.
  - An `AWaterZone` renders the water mesh. `WaterNavAreaClass` on the component sets the nav area.
  - Water needs the landscape edit layers and a landscape in the same level.

## Rules and pitfalls

- Pick the vertex count and the component layout before sculpting. Resizing later (Change Component Size) resamples and can blur detail.
- Match the heightmap file to the vertex count exactly. A wrong size is rejected or stretched.
- Keep the landscape origin and scale round, so map maths and baked previews stay simple.
- Do not use hundreds of tiny components. Use 63- or 127-quad sections.
- Edit layers merge on the GPU in the editor. A script that writes heights directly must target an edit layer, or must run before any layer content exists (as `ALandscape::Import` does).
- Use `PremultipliedAlphaBlending` for new paint layers. Mixing blend methods produces surprising sums.
- Landscape materials hit the 16-sampler limit fast. Set texture samplers to `SSM_Wrap_WorldGroupSettings` (Shared: Wrap) and pack textures (albedo+height, normal, ORM). Budget at most about 3 samplers per layer.
- Paint layers each add a weightmap channel per component, so 4 layers per component is the cheap path. More layers allocate more weightmaps. Use `landscape.DumpTargetLayerAllocations` to inspect them.
- Landscape grass is regenerated at runtime, so tune `grass.DensityScale` and `grass.CullDistanceScale` per scalability level. Never give grass collision.
- Nanite landscape:
  - Rebuild after sculpting: an out-of-date Nanite mesh shows old terrain until the rebuild. See `landscape.Nanite.LiveRebuildOnModification` and the landscape details panel, Nanite → "Build Data".
  - Nanite landscape does not change the collision. Traces still hit the heightfield.
- Collision:
  - A high `CollisionMipLevel` makes the collision coarser than the render, so units float or sink on slopes.
  - Gameplay traces in this project use WorldStatic, so keep the landscape WorldStatic.
- RVT:
  - Size the volume to the landscape bounds: set `BoundsAlignActor` to the landscape and press "Set Bounds".
  - A too-small tile count biases blur. Tune `r.VT.RVT.TileCountBias` per scalability level.
- Splines:
  - Control-point meshes and segment meshes are separate. Deform the terrain with `bRaiseTerrain` and `bLowerTerrain`, then paint with `LayerName`.
  - Re-apply the splines (Landscape mode → Splines → Apply) after moving the landscape.
- Water:
  - The river body must not answer gameplay ground traces. Check its collision profile.
  - Water carving only shows after the edit layers merge; the editor does this automatically.
- Do not edit `ALandscape` heights at runtime for gameplay. The landscape is an editor-authored asset. Runtime deformation means rebuilding collision and is not supported for shipped builds.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `ALandscape::Import` | C++ | Create heights and paint data from arrays (editor only) |
| `ULandscapeInfo::UpdateLayerInfoMap` | C++ | Refresh layer info after import |
| `ALandscapeProxy::GetComponentsBoundingBox` | C++ (`AActor`) | Landscape bounds |
| `ULandscapeLayerInfoObject::SetBlendMethod` | C++ | Set `ELandscapeTargetLayerBlendMethod` |
| `landscape.RenderNanite` | cvar | Toggle Nanite landscape rendering |
| `landscape.Nanite.LiveRebuildOnModification` | cvar | Rebuild Nanite while editing |
| `landscape.CollisionMesh.Show` | cvar | Draw landscape collision |
| `landscape.OverrideLODDistribution`, `landscape.OverrideLOD0ScreenSize` | cvar | Test LOD settings |
| `landscape.DumpLODs` | command | Log LOD info |
| `landscape.DumpTargetLayerAllocations` | command | Weightmap and layer usage per component |
| `landscape.ForceLayersUpdate` | command | Force an edit-layer merge |
| `grass.Enable`, `grass.DensityScale`, `grass.CullDistanceScale`, `grass.FlushCache` | cvar | Landscape grass |
| `r.VT.RVT.TileCountBias` | cvar | RVT resolution bias |
| `r.VirtualTextures` | project setting | Required for RVT |
| `r.RayTracing.Geometry.Landscape` | cvar | Landscape in the HW-RT scene |

## In World Divide

- `URTSEditorScripting::CreateLandscape` (`Source/WorldDivide/Core/RTSEditorScripting.h`) builds the WD_RiverCrossing_01 greybox landscape from a raw 16-bit heightmap via `ALandscape::Import`, called from `Tools/Editor/rivercrossing_greybox.py`: 16×16 components, 2×2 sections of 63 quads, 1.25 m per quad, default edit layer, and since #156 the 7 paint layers of `03_TECHNICAL_PLAN.md` §5, imported from weightmaps the script writes.
- At runtime, game code only reads landscape bounds as the map-area fallback (`RTSMaps::FindMapArea`). The planned material, paint layers, grass, RVT roads and splines are in [the RiverCrossing technical plan](../world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md) §4, §5 and §8.
- River Crossing's river is a Water plugin `AWaterBodyRiver` (#155), laid by `URTSEditorScripting::CreateRiver`: its brush carves the bed through the Water edit layer, the edit layers are merged before saving (a commandlet never ticks them, and the merge needs `-AllowCommandletRendering`), its collision is the query-only `RTSWaterSurface` profile that nothing hits and `Water_*` `NavArea_Null` volumes still block the navmesh ([the technical plan](../world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md) §7). In a commandlet the Water edit layer and the water brush must exist before the river is spawned: the editor's own path asks where to insert the layer in a dialog and touches the level editor's mode tools, both of which assert in a commandlet. Water body collision cannot be turned off: the water zone's mesh lays a river's tiles only over its enabled collision, and without any no surface draws (#381). PCG is enabled but unused in `Source/`.
- `MeshPartition`, `MeshPartitionWater`, `MeshTerrainMode` and `PCGMeshPartitionInterop` (all experimental) are also enabled in the `.uproject` and unused.
- Project docs win on conflict. See also [authoring a map](../world/maps/AUTHORING_A_MAP.md) and [the world scale standard](../tech/scale/WORLD_SCALE_STANDARD.md).

## Sources

- Epic, Building Virtual Worlds (UE 5.8): https://dev.epicgames.com/documentation/en-us/unreal-engine/building-virtual-worlds-in-unreal-engine
- Epic, Landscape Overview: https://dev.epicgames.com/documentation/unreal-engine/landscape-overview
- Epic, Landscape Quick Start Guide: https://dev.epicgames.com/documentation/unreal-engine/landscape-quick-start-guide-in-unreal-engine
- Epic, Crafting Mesh Terrain: https://dev.epicgames.com/documentation/unreal-engine/crafting-mesh-terrain-in-unreal-engine
- Engine source (UE 5.8):
  - `Engine/Source/Runtime/Landscape/Classes/LandscapeProxy.h`, `Landscape.h`, `LandscapeInfo.h`, `LandscapeLayerInfoObject.h`, `LandscapeSplineControlPoint.h`, `LandscapeSplineSegment.h`, `LandscapeSplineActor.h`
  - `Engine/Source/Runtime/Landscape/Public/LandscapeEditTypes.h`
  - `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyComponent.h`
