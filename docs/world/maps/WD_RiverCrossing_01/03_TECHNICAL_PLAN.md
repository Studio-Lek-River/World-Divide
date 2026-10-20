# WD_RiverCrossing_01: technical plan

Each item is tagged **Verified** (checked in the project or the engine install), **Recommendation** or **To validate** (it needs the editor or a test in phase 2).

## 1. What the project has today (verified)

| Area | State | Source |
|---|---|---|
| Engine | UE 5.8, DX12, SM6 | `WorldDivide.uproject`, `Config/DefaultEngine.ini` |
| Project plugins enabled | ModelingToolsEditorMode, EnhancedInput, CommonUI, PythonScriptPlugin, EditorScriptingUtilities, ModelContextProtocol, EditorToolset, SlateInspectorToolset, WorldDivideMCP | `WorldDivide.uproject` |
| Installed in the engine but **not enabled** | PCG (plus PCGGeometryScriptInterop, PCGWaterInterop (experimental), PCGPythonInterop), Water, WaterAdvanced, WaterExtras, Landmass, LandscapePatch, GeometryScripting, ProceduralVegetationEditor, Fab, Bridge | `D:\Program Files\Epic Games\UE_5.8\Engine\Plugins\` |
| Not installed | Cesium, Houdini Engine | same |
| Lighting | `r.AllowStaticLighting=False`; Lumen, VSM and Nanite at engine defaults | `DefaultEngine.ini` |
| Navigation | `NavigationSystemV1` with `bAllowClientSideNavigation=True`; `RecastNavMesh` with `RuntimeGeneration=Dynamic`; one default agent (radius 34, height 144, step 35, slope 44°, cell 19, tile 1000) | `DefaultEngine.ini`, engine `BaseEngine.ini` |
| Movement | `ARTSUnit` is an `APawn` with `UFloatingPawnMovement`, `MoveToLocation(..., bCanStrafe=true, bAllowPartialPath=true)`; separation by `URTSAvoidanceSubsystem` | `Units/RTSUnit.cpp`, `Units/RTSAvoidanceSubsystem.cpp` |
| Ground finding | `RTSGround::Trace` traces WorldStatic only; the HQ uses a WorldStatic trace with `GroundSearchDepth=1000` | `Units/RTSGround.cpp`, `Core/RTSGameMode.cpp` |
| Placement | `RTSBuildPlacement::CanPlace`: navmesh under the centre and four corners, a WorldStatic ground trace, an empty overlap box (WorldStatic, WorldDynamic, Pawn); 1 m grid; **no slope check** | `Buildings/RTSBuildPlacement.cpp` |
| Camera bounds | Union of all `ANavMeshBoundsVolume` plus `EdgeMargin` (500), with a fallback of ±150 m | `Player/RTSCameraPawn.cpp` |
| Map registration | `URTSMapData` (map, `StartSpots`, preview, `PreviewArea`, `StartSpotPositions`) listed in `DefaultGame.ini` `+SkirmishMaps`; start spots are `ARTSStartSpot`s numbered by `SpotIndex`, which tag themselves `Team<N>` | `Data/RTSMapData.h`, `Core/RTSStartSpot.h`, `Core/RTSMaps.h` |
| Map tests | `Maps.LobbyMaps`, `Maps.SavedNavMesh`, `Maps.StartSpotPositions`, `Maps.PreviewCameraAlignment`, `Smoke.Maps`, `Smoke.Terrain`, `Smoke.SolidObstacles` | `Source/WorldDivide/Tests/RTSMap*.cpp`, `RTSTerrainSmokeTest.cpp`, `RTSTestMapSmokeTest.cpp` |
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
  - **To validate:** that OFPA on a non-WP level saves cleanly through the existing setup and test flow.
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

- **Water plugin `WaterBodyRiver`** along one spline.
- **Width:** the spline width varies from 40 to 70 m.
- **Depth:** 2–4 m.
- **Carving:** riverbed carving is done through the Water landscape edit layer.
- **Materials:** the engine river material, adjusted for colour and opacity. Riverbed mud and pebbles come from the landscape material's Mud layer.
- **Ground units must not cross the river.** Two blocks do this:
  1. Banks above 44° slope in the carved section, so the navmesh is cut by slope.
  2. A chain of `NavModifierVolume`s with `NavArea_Null` labelled `Water_*` along the river. They follow the existing `spawn_water` and `Smoke.SolidObstacles` naming rule (labels `Wall_`, `Block_`, `Water_`).
  - **To validate:** whether the water body's own navigation setting (`bAffectsNavigation` / `WaterNavAreaClass` in 5.8) can replace the volumes.
- **Trace risk:**
  - The Water body's collision must **not** answer the WorldStatic ground traces. Otherwise HQ placement, unit snapping and `CanPlace` could treat the water surface as ground.
  - **To validate:** the water body's collision profile.
- **Shoreline:** reeds, mud decals and stones from PCG along the spline (with PCGWaterInterop, or a spline copy).

## 8. Roads and bridges

- **Roads:** **Landscape Splines** with spline-mesh road segments and kerbs.
  - They deform the landscape, so the road follows a smoothed height.
  - They write into the RVT for blending.
  - Purely visual for navigation: the road is part of the walkable ground.
- **Bridges:**
  - Static mesh actors (modular spans from the imported infrastructure pack, or a project-built Geometry Script bridge if no pack fits).
  - **WorldStatic** collision with simple box collision on the deck.
  - Bridges sit on the navmesh as normal geometry; no nav links are needed.
  - **To validate** with a navmesh draw (`show Navigation`): deck connectivity and the gap between the deck and the `Water_*` volume.
- **Bridge width:** a 12 m carriageway, checked against the vehicle agent radius of 190 cm (§9): lane = 12 m − 2 × 1.9 m = 8.2 m, which meets the ≥ 6 m rule.

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
| Lobby | `DA_MapRiverCrossing01` in `+SkirmishMaps`, `StartSpots=2`, `PreviewArea` ±1000 m, `StartSpotPositions` from the PlayerStarts |
| Start spots | `PlayerStart` tagged `Team0`/`Team1` at the base centres; the HQ snaps to the 1 m grid and traces down |
| Starting units | Placed by `ARTSGameMode` at the start spots as today; nothing map-specific |
| Supply sources | `ARTSSupplySource` actors with labels and amounts matching the existing pattern |
| Camera | One `NavMeshBoundsVolume` over the 2,000 m playable area. Camera bounds follow automatically. |
| Preview | `URTSEditorScripting::CapturePreview` over `PreviewArea` |
| AI | Works unchanged once the rescale issue scales the AI's search distances |

Nothing in gameplay code needs to change for the map itself. The changes needed come from the **rescale issue**: unit and building data, camera zoom, nav agents, AI distances, test radii.

## 13. Automation

- **`Tools/Editor/setup_rivercrossing.py`**, a new script that follows `setup_testmap.py`'s conventions: it saves only what differs and exits 3 on read-only files. It must never create or overwrite the level's terrain or dressing. Its jobs:
  - create or update `DA_MapRiverCrossing01` (start spots, preview area)
  - check that the level has exactly one `NavMeshBoundsVolume` matching the playable area
  - build and save the navmesh
  - capture the preview
- **PCG graphs** are assets, so foliage regenerates the same way each time from the same inputs.
- **A new map test, `WorldDivide.Maps.RiverCrossing`**, following `Tests/RTSMapTests.cpp`. It checks:
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
