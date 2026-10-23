# WD_RiverCrossing_01: technical plan

Each item is tagged **Verified** (checked in the project or the engine install), **Recommendation** or **To validate** (it needs the editor or a test in phase 2).

## 1. What the project has today (verified)

| Area | State | Source |
|---|---|---|
| Engine | UE 5.8, DX12, SM6 | `WorldDivide.uproject`, `Config/DefaultEngine.ini` |
| Project plugins enabled | ModelingToolsEditorMode, EnhancedInput, CommonUI, PythonScriptPlugin, EditorScriptingUtilities, ModelContextProtocol, EditorToolset, SlateInspectorToolset, WorldDivideMCP | `WorldDivide.uproject` |
| Installed in the engine but **not enabled** | PCG (plus PCGGeometryScriptInterop, PCGWaterInterop (experimental), PCGPythonInterop), WaterAdvanced, WaterExtras, Landmass, LandscapePatch, GeometryScripting, ProceduralVegetationEditor, Fab, Bridge. Water is enabled and used since #155 (§7). | `D:\Program Files\Epic Games\UE_5.8\Engine\Plugins\` |
| Not installed | Cesium, Houdini Engine | same |
| Lighting | `r.AllowStaticLighting=False`; Lumen, VSM and Nanite at engine defaults | `DefaultEngine.ini` |
| Navigation | `NavigationSystemV1` with `bAllowClientSideNavigation=True`; `RecastNavMesh` with `RuntimeGeneration=Dynamic`; one default agent (radius 34, height 144, step 35, slope 44°, cell 19, tile 1000) | `DefaultEngine.ini`, engine `BaseEngine.ini` |
| Movement | `ARTSUnit` is an `APawn` with `UFloatingPawnMovement`, `MoveToLocation(..., bCanStrafe=true, bAllowPartialPath=true)`; separation by `URTSAvoidanceSubsystem` | `Units/RTSUnit.cpp`, `Units/RTSAvoidanceSubsystem.cpp` |
| Ground finding | `RTSGround::Trace` traces WorldStatic only; the HQ uses a WorldStatic trace with `GroundSearchDepth=1000` | `Units/RTSGround.cpp`, `Core/RTSGameMode.cpp` |
| Placement | `RTSBuildPlacement::CanPlace`: navmesh under the centre and four corners, a WorldStatic ground trace, an empty overlap box (WorldStatic, WorldDynamic, Pawn); 1 m grid; **no slope check** | `Buildings/RTSBuildPlacement.cpp` |
| Camera bounds | Union of all `ANavMeshBoundsVolume` plus `EdgeMargin` (500), with a fallback of ±150 m | `Player/RTSCameraPawn.cpp` |
| Map registration | `URTSMapData` (map, `StartSpots`, preview, `PreviewArea`, `StartSpotPositions`) listed in `DefaultGame.ini` `+SkirmishMaps`; start spots are `ARTSStartSpot`s numbered by `SpotIndex`, which tag themselves `Team<N>` | `Data/RTSMapData.h`, `Core/RTSStartSpot.h`, `Core/RTSMaps.h` |
| Map tests | `Maps.LobbyMaps`, `Maps.SavedNavMesh`, `Maps.StartSpotPositions`, `Maps.PreviewArea`, `Maps.PreviewCameraAlignment`, `Smoke.Maps`, `Smoke.Terrain`, `Smoke.SolidObstacles` | `Source/WorldDivide/Tests/RTSMap*.cpp`, `RTSTerrainSmokeTest.cpp`, `RTSTestMapSmokeTest.cpp` |
| Content generation | `Tools/Editor/setup_testmap.py` builds every test and lobby map from engine cubes; run with `ue.ps1 -Task setup` | `Tools/Scripts/ue.ps1` |
| Editor helpers | `URTSEditorScripting::BuildNavigation`, `CapturePreview` | `Core/RTSEditorScripting.h` |

## 2. Map architecture (recommendation)

**A standard level, not World Partition, with a single Landscape and One File Per Actor turned on.**

- **No World Partition.**
  - A 2.5 km 1v1 map fits in memory comfortably.
  - World Partition streaming brings cell loading, HLOD build steps and data-layer handling that a fixed-camera RTS doesn't need.
  - Every existing level is non-WP, and game code iterates level actors at `BeginPlay`, for example the camera's `TActorIterator<ANavMeshBoundsVolume>`. That works best when everything is always loaded.
- **One File Per Actor (OFPA)** for this level only (World Settings → *Use External Actors*).
  - Actors are stored as separate LFS-lockable files, so placing the village doesn't lock the whole `.umap`.
  - **Verified (#154):** OFPA on a non-WP level saves cleanly through `ue.ps1 -Task rivercrossing` and loads in the map tests and PIE. The actors live under `Content/__ExternalActors__/Maps/WD_RiverCrossing_01/`.
- **Level path:** `/Game/Maps/WD_RiverCrossing_01`. Map data: `/Game/Data/Maps/DA_MapRiverCrossing01`, plus `T_Preview_WD_RiverCrossing_01`.
- **Ownership:**
  - The level is **authored by hand**. `setup_testmap.py` must never regenerate it.
  - Only the map data asset, the preview capture and a navmesh build come from script (§9).

## 3. Plugins to enable (each needs producer approval)

| Plugin | Why | Required |
|---|---|---|
| **Water** | River body, shoreline, water material, landscape carving | Yes |
| **PCG** | Forests, copses, field edges and clutter, from repeatable rules | Yes |
| LandscapePatch | Non-destructive flattening of base pads and bridge abutments | Optional (Water carves the riverbed itself) |
| GeometryScripting | Editor tools for spline-based road and kerb meshes | Optional |
| PCGWaterInterop | Lets PCG exclude the river from a Water spline | Optional; experimental in 5.8 |

Not recommended: Landmass (Water uses the landscape edit-layer workflow in 5.8 **[to validate]**), ProceduralVegetationEditor (experimental and not needed when packs ship their own trees), WaterAdvanced (not needed for a river).

## 4. Landscape

| Setting | Recommendation |
|---|---|
| Size | 2017 × 2017 vertices; 16 × 16 components of 2 × 2 sections of 63 quads |
| X/Y scale | 125 (1.25 m per quad), giving about 2,520 m: 2,000 m playable plus a 260 m border |
| Z range | Default scale. Height variation is small (river -4 m, hills +25 m, border hills +60 m). |
| Edit layers | On: a base sculpt layer, a "gameplay flats" layer (base pads, bridge approaches) and the Water layer |
| Collision | Default landscape collision on WorldStatic. Ground traces, placement and units already rely on WorldStatic. |
| Nanite landscape | **To validate** on the target GPU. Off by default for the first pass. |

Heightmap authoring:
- Sculpt the greybox by hand in the editor.
- Optionally start from a heightmap made externally (World Machine, Gaea or a painted 16-bit PNG) and imported.
- No procedural terrain generation. The layout must follow the hand-designed map ([02_MAP_DESIGN.md](02_MAP_DESIGN.md)).

## 5. Landscape material

- **One master material**, `M_Landscape_WD_Temperate` (project-owned, under `/Game/World/Landscape/`), built from the imported surface pack's textures.
- **Paint layers (7):** Grass, Meadow (darker, rough), Dirt, Mud (riverbank), Gravel (tracks), Rock (steep slopes), Asphalt shoulder.
- **Blending:**
  - Height-based blending between layers.
  - Automatic rock on slopes above about 30° and mud near the water (height mask), with painted layers on top.
  - Distance-based macro variation to break tiling at RTS camera heights.
- **Roads:** a runtime virtual texture (RVT) for spline road decals and asphalt blending. It also lets PCG and the grass read the road mask.
- **Grass:** Landscape Grass Types tied to the Grass and Meadow layers for short grass and wildflowers (≤ 0.6 m, cull about 150 m, no collision).
- **Budget:** at most 3 texture samplers per layer (albedo plus height, normal, ORM packing).

As built (#156, `Tools/Editor/rivercrossing_greybox.py`):

| Layer | Surface | Physical material | Where |
|---|---|---|---|
| Grass | UncutGrass | PM_Grass | Pasture fields, the village rise |
| Meadow | WildGrass (dark, tinted up) | PM_Grass | Meadow fields, field margins, the river's rough banks |
| Dirt | Military trench dirt, fine 03 | PM_Dirt | Fallow fields, the base pads |
| Mud | MudPebbles | PM_Mud | 8 m along the water's edge |
| Gravel | GravelGround | PM_Sand | Under the tracks and their falloff |
| Rock | GougedRockCliff | PM_Dirt | Slopes over 30° (the bridge ramps' sides) |
| Asphalt | Military trench dirt, coarse 01 | PM_Road | Under the asphalt roads, their shoulders and falloff |

- The script paints the layers from the layout, not the material: a seeded patchwork of fields 240 × 120 m (rows of varied width, symmetric about the centre), each with soft patches of a second layer and a 1.5 m meadow margin that wanders a little. The roads paint their layer twice: into the imported weightmaps, so every component they cross has it from the start, and through the splines' `LayerName`, so a road moved in the editor takes its paint along. The physical material under a unit is therefore always the layer it shows.
- The material writes the layers into the RVT and the main pass only reads the RVT. In the RVT they are height-blended, with each ORM's AO as the height, and each base colour is mixed with its own tile 6.3 times larger and shaded by a macro mask. The roads draw into the RVT only (`Exclusive`), over the landscape.
- Samples: 4 per layer (base colour twice, normal, ORM) from 3 textures per layer, all through the shared wrap sampler, plus the macro mask's 2 samples of one of them: 21 textures and 30 samples, paid only when an RVT page is drawn. The main pass samples the RVT's 2 textures and the weightmaps.
- RVT: 2048 tiles of 256 texels over 2,520 m (about 5 mm a texel), adaptive. Its volume reaches 20 m above and below the landscape, so ground the river and roads carve stays inside it; ground outside it draws black.
- Grass: `LGT_WD_Grass` (Bermuda grass, 0.15–0.25 m, 300 per 100 m²) and `LGT_WD_Meadow` (dry medium grass, 0.35–0.6 m, 500 per 100 m²), culled from 120 to 150 m, no collision or shadows.
- Exposure: River Crossing has no post-process volume yet, so auto exposure lifts the scanned ground about 1.5 stops and washes it out. The #156 sign-off sheets were also taken with `r.ExposureOffset -1.5`. Setting the exposure is #159's.

## 6. Vegetation and PCG

| Purpose | Method | Collision |
|---|---|---|
| Ground cover (grass, flowers) | Landscape Grass Types | None |
| Forests, copses, hedgerows, field edges | PCG graph per biome zone, driven by painted or spline volumes | Trunks only, as WorldStatic; canopy none |
| Bushes and clutter near roads | PCG, with exclusion near splines | None |
| Village gardens, landmark trees | Hand-placed | Trunks only |
| Border ring forests | PCG at high density with low-cost LODs or impostors | None (outside the navmesh) |

**PCG exclusions** (hard rules in every graph):
- the river (Water spline)
- road splines plus 6 m
- the bridge corridor (60 m)
- base pads
- village street volumes
- an 8 m radius around supply sources

**Readability rules** (from [02_MAP_DESIGN.md](02_MAP_DESIGN.md)): trees ≤ 18 m within 40 m of lanes, grass ≤ 0.6 m, no dense undergrowth inside playable woods.

**Forest navigation**
- **Recommendation:** trees in playable woods get no navmesh influence, so vehicles drive through and the woods are cover and visual only.
- Trunk collision is set not to affect navigation, because hundreds of small holes would wreck path quality and navmesh cost.
- **Producer decision:** whether woods should slow or block vehicles. If so, that becomes a `NavArea` cost volume, not individual tree collision.

**Performance**
- Nanite foliage for trees, if the imported pack supports it in 5.8. Otherwise use LODs plus impostor billboards.
- Cull distances are tuned to the 300 m maximum camera arm.
- `SM_HP_Tree` (564k triangles) is for hand-placed hero trees only.

## 7. Water

- **Water plugin `WaterBodyRiver`** along one spline (#155), laid by `URTSEditorScripting::CreateRiver` from `rivercrossing_greybox.py`: a point every 40 m on the S-curve, edge to edge.
- **Decision (#155, producer 2026-10-22):** the Water plugin is used although it is experimental in 5.8. Gameplay does not depend on it: the `Water_*` volumes block the navmesh and the water body has no collision, so a later engine change to the plugin can break the look but not pathing, placement or grounding.
- **Width:** the spline width varies from 45 m at the bridges to 70 m at the bends.
- **Depth:** 2 m at the bridges to 3 m at the bends, under a surface 3 m below the fields.
- **Carving:** the river's water brush (`AWaterBrushManager`) carves the bed through the landscape's Water edit layer; the heightmap itself has no channel. The brush draws on the GPU, so building the greybox needs a renderer (`ue.ps1 -Task rivercrossing -Rebuild -Render`), and `CreateRiver` merges the edit layers before the level is saved, so the saved heights and collision carry the bed. Banks rise from 16 cm over the water at 60° (angle falloff, no curve channel). In a commandlet `CreateRiver` makes the Water edit layer and the water brush itself before it spawns the river: the editor's own path asks where to insert the layer in a dialog and touches the level editor's mode tools, which a commandlet does not have. **Validated (#155):** the rebuild logs the bed under 62 of the 64 spline points (the two end points lie past the landscape), 5.2 m down in mid-river under a surface at -3 m.
- **Baked carve (#382):** after the merge, `CreateRiver` writes the merged heights into the base edit layer, destroys the water brush, deletes the Water layer and turns the river's `bAffectsLandscape` off. The brush's class lives in the editor-only WaterEditor module, which a `-game` run never loads, and the map's actors are one file each without World Partition, so a saved brush failed to load in every `-game` run. The cook already stripped it. `ALandscape::CollapseLayer` does not bake it: it renders the layers without their brushes. **Consequence:** moving the river spline in the editor no longer re-carves the bed; change the script and rebuild (`ue.ps1 -Task rivercrossing -Rebuild -Render`).
- **Materials:** `MI_RiverCrossing_River`, the engine river material tuned to a clear river blue, readable at default and max zoom (producer, #381). Its Absorption reads as a clarity distance per channel: blue carries furthest, and low values turn the water near black. Riverbed mud and pebbles come from the landscape material's Mud layer (#156).
- **Surface (#381):** the water zone's mesh lays a river's tiles only over the boxes of the river's *enabled* collision (`UWaterMeshComponent::RebuildWaterMesh`, through `GetCollisionComponents`). #155 turned the body's collision off, so the river had no tiles and no surface drew, in every view and in static-mesh mode alike. `CreateRiver` now gives the body the `RTSWaterSurface` profile (`Config/DefaultEngine.ini`): query-only, its own `WaterSurface` object type that no trace or overlap in the game asks for, and every channel ignored. **Validated:** the surface draws in the `camerashots -Map /Game/Maps/WD_RiverCrossing_01` bridge shots at every zoom.
- **Ground units must not cross the river.** Two blocks do this:
  1. Banks above 44° slope in the carved section (60°), so the navmesh is cut by slope.
  2. A chain of `NavModifierVolume`s with `NavArea_Null` labelled `Water_*` along the river. They follow the existing `spawn_water` and `Smoke.SolidObstacles` naming rule (labels `Wall_`, `Block_`, `Water_`).
  - **Decided (#155):** the water body's own navigation (`WaterNavAreaClass`) does not replace the volumes. It only acts through the body's collision, which answers no trace (below), so the body's nav area is set to `NavArea_Null` and it cannot affect navigation; the volumes stay the block. **Validated:** `WorldDivide.Maps.RiverCrossing.Layout` passes its check that neither the Infantry nor the Vehicle navmesh lies anywhere over the water off the bridges (samples in every `Water_*` box inside the playable square).
- **Trace risk:**
  - The Water body's collision must **not** answer the WorldStatic ground traces. Otherwise HQ placement, unit snapping and `CanPlace` could treat the water surface as ground.
  - The plugin's default profile (`WaterBodyCollision`) is a WorldStatic query profile, so it would answer them; `CreateRiver` sets `RTSWaterSurface` instead (above), and `DefaultEngine.ini` makes it the plugin's default (`UWaterRuntimeSettings::DefaultWaterCollisionProfileName`, #382), which also ends the Water editor module's missing-profile error in every editor run, whose object type is not WorldStatic. Collision cannot simply be off: the water mesh needs it to draw the river. **Validated (#155, #381):** `WorldDivide.Maps.RiverCrossing.Layout` passes with every `RTSGround::Trace` over the river reaching the carved bed at least 0.5 m under the surface, and every world-static ray there stopping on the landscape.
- **Shoreline:** reeds, mud decals and stones from PCG along the spline (with PCGWaterInterop, or a spline copy).

## 8. Roads and bridges

- **Roads (#383):** **Landscape Splines** with spline-mesh road segments, laid by `rivercrossing_greybox.py` (`WEST_ROADS`, turned about the centre for the east side) through `URTSEditorScripting::AddLandscapeSplineRoad` and `ApplyLandscapeSplines`.
  - The main axis (asphalt, 8 m), the farm track to the forward supply and on to the far bridge's approach (5 m), the village ring road along the street at x = ±157 m (asphalt, 6 m), and a dressing road off the map behind each base. Corners are rounded at 40 m, with a spline point at most every 30 m at the greybox height.
  - They deform the landscape through its reserved splines edit layer (`ULandscapeEditLayerSplines`), so the road follows a smoothed height. The layer stays in the level: it is a runtime Landscape class, and a `-game` run loads the map cleanly. Moving a road in the editor reshapes the ground.
  - A landscape spline control point has one direction, so a branch (a T junction) ends at the through road's edge instead of sharing its point; only roads that carry straight on share one.
  - The bridge ramps' roads do not deform the ground (`bShapeGround` off): a road's falloff would soften the ramps' steep sides into walkable slopes, which breaks the tile-seam rule below. Everywhere else the roads follow the rolling ground and are never raised.
  - Placeholder surface `M_RiverCrossing_Road` (`MI_Road_Asphalt`, `MI_Road_Track` in `/Game/Materials/Road`) on the engine cube, 4 m tiles, half a metre deep with the top 10 cm over the ground: a flat plane's flat bounds lay in the ground and occlusion culling dropped whole stretches of it.
  - They draw both in the main pass and into the RVT (`VirtualTextureRenderPassType` Always) until the landscape material (#156) reads the RVT; then switch them to Exclusive.
  - No collision, no navigation effect: the road is part of the walkable ground, and ground traces and dust still find the landscape.
- **RVT (#383):** `r.VirtualTextures=True`; `/Game/World/Landscape/RVT_WD_RiverCrossing_01` (base colour, normal, roughness; default size, tuning in #160) on the volume `RVT_Landscape` over the landscape's bounds, listed in the landscape's `RuntimeVirtualTextures` (`URTSEditorScripting::CreateLandscapeVirtualTexture`).
- **Bridges (#155):**
  - A project-built concrete girder bridge from `Tools/Art/bridge_blender.py` (FBX in `Art/Shared/bridge/`, imported to `/Game/Art/Shared/Bridge`): three separate 30 m spans on two piers and two abutments, Nanite on. Each part is its own static mesh actor, `Bridge_<North|South>_<Span1-3|Pier1-2|AbutmentW|AbutmentE>`, so a span can be dropped later.
  - **WorldStatic** `BlockAll` collision from `UCX_` boxes: the deck and both parapets of each span, the pier caps and columns, the abutments' seats, approach slabs and wing walls.
  - The deck's top stands 3 m over the approaches, 6 m over the water, on earthen ramps in the heightmap: flat for 19.5 m behind each abutment's approach slab, then down over 30 m (5.7°), with sides steeper than any agent climbs.
  - The ramps keep the navmesh linked across its tile seams (`TileSizeUU` 64 m): a seam on a ramp with walkable sides came out as one far-tile polygon at field height, which Detour does not link to the road at deck height, and the hull clearance read it as a wall across the road (every tank of the column test stopped there). Keep a tile seam off a raised road's slope, or give the road sides no agent can climb.
  - Bridges sit on the navmesh as normal geometry; no nav links are needed. The decks stand well above the `Water_*` volumes (their top is 1 m under the water).
- **Bridge width:** a 13 m carriageway between the parapets, checked against the vehicle agent radius of 190 cm (§9): lane = 13 m − 2 × 1.9 m = 9.2 m, which meets the ≥ 6 m rule. `WorldDivide.Maps.RiverCrossing.Layout` measures it on each span, and `WorldDivide.Maps.RiverCrossing.BridgeColumn` drives a column of twelve M20s across each bridge.

## 9. Navigation at 2 km

Today:
- **One agent.** Every unit paths with radius 34 cm, whatever its size, so large vehicles clip corners.
- **Small tiles at the default size.** With `TileSizeUU=1000` a 2 km map has 200 × 200 = **40,000 tiles**.

Recommendations (part of the rescale issue, not this map):
- **Two `SupportedAgents`:**
  - Infantry: radius 40 cm, height 180 cm.
  - Vehicle: radius 190 cm, height 350 cm. Decided by the producer (D-D, 2026-10-15) to match [the world scale standard](../../../tech/scale/WORLD_SCALE_STANDARD.md) rule 4; 350 cm clears the M20's 3.36 m hull. The implementation is #135.
  - `ARTSUnit` picks its agent from its data, through the `NavAgentProperties` of its movement component.
  - This is a gameplay change and needs its own issue and tests.
- **Bigger tiles:** `TileSizeUU` about 6400 and `CellSize` about 25–30 (#135), which gives about 1,000 tiles. **To validate:** build time and how the buildings' dynamic obstacle holes look.
- **Keep the saved navmesh:**
  - `Maps.SavedNavMesh` requires a navmesh saved in the level.
  - Keep `RuntimeGeneration=Dynamic` so buildings still cut it. `DynamicModifiersOnly` is cheaper but must first be proven with the building obstacle (`NavArea_Null` dynamic obstacle in `Buildings/RTSBuilding.cpp`) **[to validate]**.
- **Build with the existing helper:** `URTSEditorScripting::BuildNavigation`.

## 10. Lighting and post

| Actor | Setting |
|---|---|
| DirectionalLight "Sun" | About 45° elevation, warm afternoon colour (about 5,800 K), atmosphere sun light, VSM shadows |
| SkyAtmosphere | Default Earth, slightly raised Mie for haze |
| SkyLight | Real-time capture (as today) |
| VolumetricCloud | Sparse cumulus, high altitude. Shadows on the ground are optional and costly. |
| ExponentialHeightFog | Low density (≤ 0.01) with a falloff that adds depth at the border ring only. **No volumetric fog.** |
| PostProcessVolume (unbound) | Manual exposure as the project already uses for previews (`RTSEditorScripting.cpp`, manual exposure); mild bloom; no vignette, chromatic aberration or film grain; Lumen GI and reflections at a budgeted quality |

- **GI:** Lumen software ray tracing. Hardware RT only if the minimum spec allows it. That is a producer decision.
- **Lighting presets:** the existing `spawn_lighting` (Sun, SkyLight, SkyAtmosphere) stays the base, extended in this level by hand.

## 11. Nanite, LODs and HLOD

- Nanite on for buildings, bridges, rocks and cliffs from the imported packs.
- The game's own placeholder units stay Nanite off by design; the `WorldDivide.Art.PlaceholderWithoutNanite` test covers that.
- Foliage uses Nanite foliage or LODs plus impostors (see §6).
- HLOD isn't needed without World Partition. Distance culling plus Nanite covers the border ring.

## 12. Integration with gameplay systems

| System | What the map provides |
|---|---|
| Lobby | `DA_MapRiverCrossing01` in `+SkirmishMaps`, `StartSpots=2`, `PreviewArea` ±1000 m, `StartSpotPositions` baked from the start spots (`ue.ps1 -Task bakemap`, `../AUTHORING_A_MAP.md`) |
| Start spots | `PlayerStart` tagged `Team0`/`Team1` at the base centres; the HQ snaps to the 1 m grid and traces down |
| Starting units | Placed by `ARTSGameMode` at the start spots as today; nothing map-specific |
| Supply sources | `ARTSSupplySource` actors with labels and amounts matching the existing pattern |
| Camera | One `NavMeshBoundsVolume` over the 2,000 m playable area. Camera bounds follow automatically. |
| Preview | `URTSEditorScripting::CapturePreview` over `PreviewArea` |
| AI | Works unchanged once the rescale issue scales the AI's search distances |

Nothing in gameplay code needs to change for the map itself. The changes needed come from the **rescale issue**: unit and building data, camera zoom, nav agents, AI distances, test radii.

## 13. Automation

- **`Tools/Editor/setup_rivercrossing.py`** (`ue.ps1 -Task rivercrossing`, #154) follows `setup_testmap.py`'s conventions and exits 3 on read-only files.
  - It builds and saves the navmesh.
  - When the level doesn't exist yet, it first creates the scripted greybox (`rivercrossing_greybox.py`). It never overwrites an existing level's terrain or dressing.
  - `DA_MapRiverCrossing01` and the preview come from `ue.ps1 -Task bakemap -Map /Game/Maps/WD_RiverCrossing_01 -DataAsset DA_MapRiverCrossing01 -Render`.
- **PCG graphs** are assets, so foliage regenerates the same way each time from the same inputs.
- **The map tests `WorldDivide.Maps.RiverCrossing.Layout` and `.AIvsAI`** (`Tests/RTSScaleTests.cpp`, #154). `.AIvsAI` plays an AI-against-AI match to its end. `.Layout` checks:
  - a path exists from each base to both bridges and to the village
  - no path crosses the river outside the bridges (path points never inside a `Water_*` volume)
  - `CanPlace` passes at a grid of test points on both base pads
  - both bridge decks have navmesh
- **Editor checks** in phase 2 run with the Unreal MCP server (`ue.ps1 -Task editor`): screenshots, the actor list and `stat` commands. The server can't edit assets, so authoring is done in the editor.

## 14. Technical risks

| Risk | Impact | Mitigation |
|---|---|---|
| **The real-scale migration** comes before the map and touches all unit, building, camera, AI and test data | High: every map number depends on it | Run it as a separate issue first. The map numbers in 02 stay provisional until then. |
| Navmesh build time and memory at 2 km | Medium | Bigger tiles and cells; saved navmesh; profile in the greybox |
| One agent radius lets vehicles clip bridge parapets and corners | Medium | A vehicle agent in the rescale issue; bridge width margin |
| Water surface or forest collision answering WorldStatic traces | High: broken placement and grounding | Collision profiles checked by test (§7, §13) |
| Foliage overdraw and Lumen cost with 200+ units | Medium | Readability density limits, Nanite foliage, cull distances, fps budget in phase 8 |
| Mixed art styles across separate Fab packs | Medium | Pick one surface and scan family and one building kit before import (05) |
| LFS size and lock conflicts on a large level | Medium | OFPA; landscape data stays in the level file, so lock it for sculpt sessions |
| Third-party tree of unknown licence | Low/Medium | Confirm the licence before committing |
| Fog of war and minimap not built yet | Low for this map | Strong landmarks now; integrate later |
