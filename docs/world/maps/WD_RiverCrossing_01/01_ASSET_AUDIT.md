# WD_RiverCrossing_01: environment asset audit

Phase 1. First audit 2026-10-15 (no packs yet, from "How this was checked" down); packs imported and audited 2026-10-20 (#153, the next section; a second batch of eight the same day); CC0 props, roof and plank surfaces and the in-house roadside kit added 2026-10-22 (#409). Engine: UE 5.8 (`WorldDivide.uproject`, `"EngineAssociation": "5.8"`).

## Imported packs (2026-10-20)

### Storage rule

The repository is public and the pack licences do not allow sharing the files, so the packs are **not** in git:

- Each pack stays in its own `Content/<Pack>/` root, as Fab installs it. Moving them to `/Game/World/Packs/` would only rewrite references in files nobody else has.
- Each is git-ignored (`.gitignore`) and never cooked (`Config/DefaultGame.ini`, `DirectoriesToNeverCook`; `WorldDivide.Config.Cook` checks it).
- Exception (producer's decision on #156, 2026-10-23): the textures the landscape material uses (21 of the Surfaces, 3 per layer) and the two grass models' meshes and textures are copied into `/Game/World/Landscape/Textures` and `/Game/World/Landscape/Grass`, committed and cooked. `rivercrossing_greybox.py` copies them only when the copy is missing, so a checkout without the packs still builds the map. Nothing else of a pack is committed.
- Every machine adds the packs in the manifest below from Fab (*My Library → Add to project*, see [05_PRODUCER_ACTIONS.md](05_PRODUCER_ACTIONS.md) §2), runs `ue.ps1 -Task freeassets` (the CC0 models and surface ZIPs, downloaded again) and `ue.ps1 -Task surfaces -Path D:\Downloads\textures`, and then runs `ue.ps1 -Task packaudit`.
- The CC0 models follow the same rule although their licence would allow committing them: they are 0.6 GB and download again in minutes. Only the in-house roadside kit (`Art/Shared/roadside` → `/Game/Art/Shared/Roadside`, own work) is committed and may cook.

### How this was checked

`Tools/Scripts/ue.ps1 -Task packaudit` runs `Tools/Editor/audit_packs.py` headless. It reads every asset's Asset Registry tags (not the file headers as on 2026-10-15) and writes:

- `Saved/PackAudit/<Pack>.csv`: one row per asset, with mesh size in cm (`ApproxSize`; 1 uu = 1 cm), Nanite, LODs, triangles, collision primitives and complexity, material slots, texture size and format, and the plugin an asset's class needs when this project lacks it.
- `Saved/PackAudit/summary.md`: counts and flags (textures over 4K, meshes without simple collision, meshes with neither Nanite nor LODs).
- `/Game/Developers/PackAudit/L_PackScale` (git-ignored): a sample of each pack next to the Abrams and the M20 Guardian. The run fails if the level references a missing package; assets that reference packages their own pack lacks are left out and listed.

Skeletal meshes have no size tag; the Megaplant trees' heights are checked by eye in `L_PackScale`.

### Manifest

| Pack root | Fab title (as installed) | Size | Category | Style | Verdict |
|---|---|---|---|---|---|
| `Megaplant_Library` | Megaplant Library | 3.9 GB | B trees, bushes | Realistic, scanned | **Use.** 9 species, Nanite skeletal (PVE) |
| `Real_Stones_Pack_01` | Real Stones Pack 01 | 0.7 GB | F natural details | Realistic, scanned | **Use** for rocks |
| `StylizedBridge` | Stylized Bridge | 1.2 GB | C, E, A | **Stylized** | Reference only; clashes with the M20 bar |
| `ProceduralBuildingGenerator` | Procedural Building Generator | 0.7 GB | D | Urban slum / ghost city | **Not usable as is**: broken material references, wrong style |
| `Deko_MatrixDemo` | Deko Matrix Demo (Freeway) | 4.1 GB | E | Realistic, US freeway | Partly: lamps, poles, barriers, sandbags; US signs are off-setting |
| `HighPoly_Tree_Model` | unknown | 37 MB | B | Realistic | Keep locally as a hero tree; **cannot ship** until its licence is confirmed |
| `UltimateUIMenusSFX` | Ultimate UI Menus SFX | 35 MB | (UI audio) | — | Not environment |
| `UnrealDrive_CitySample` | MetaRoad (UnrealDrive) city sample | 3.9 GB | E, A | Realistic, US city | **Use** the meshes and materials: asphalt, sidewalks, kerbs, barriers, bollards, guardrails, cones. Its 9 road profile assets need the **MetaRoad** plugin, not installed |
| `MuddyGroundPack` | Muddy Ground Pack | 131 MB | A | Realistic, scanned | **Use** for mud and wet tracks |
| `SM_Concrete_07` | Scans Matter Concrete 07 | 1.3 GB | A, D | Realistic, scanned | **Use** for concrete (quays, the HQ apron); 8K textures |
| `SM_Tiles_02` | Scans Matter Tiles 02 | 1.6 GB | D | Realistic, scanned | Partly: granite tiles and large bricks for the village square; office ceiling tiles not; 8K textures |
| `Materials_StoneTiles` | Stone Tiles materials | 0.3 GB | A, D | Realistic | **Use** for paving in the villages |
| `AdvancedRealisticGlass` | Advanced Realistic Glass | 124 MB | D | Realistic | Later: windows for village buildings and the HQ |
| `StylizedSurface_Lite` | Stylized Surface Lite | 311 MB | D | **Stylized** | Not usable |
| `AccuCities` | AccuCities (London city sample) | 58 MB | D | Photogrammetry, UK city blocks | Not usable for the villages: low-detail city tiles, no interiors (added 2026-10-22, not yet audited) |
| `FogArea` | Fog Area | 27 MB | G atmosphere | Volumetric fog volumes | Candidate for the river mist in #159 (added 2026-10-22, not yet audited) |
| `Surfaces` | Megascans surface ZIPs in `D:\Downloads\textures`, through `ue.ps1 -Task surfaces` (#330); four ambientCG sets added by `-Task freeassets` (#409) | 40 sets at 2K | A, D, F | Realistic, scanned | **Use** for grass, meadow, clover, dirt and tracks, gravel, forest floor, sand, rock faces, roof tiles and planks. Snow sets skipped; desert, Thai beach, coal ash and swamp water are imported but off-biome |
| `PolyHaven` | Poly Haven models (CC0), through `ue.ps1 -Task freeassets` (#409) | 0.6 GB | B grass and flowers, E poles, fences, lamp, F logs, debris, cliffs | Realistic, scanned and modelled | **Use**: 26 models as 221 Nanite meshes, 2K textures |
| `Art/Shared/Roadside` (committed) | In-house roadside kit (#409) | < 1 MB | E signs, fences, power line | Realistic, plain | **Use**: European signs, post-and-rail and pasture fences, a cable for spline power lines |
| `MeshRift_CW_MaterialPackSample` | Chiselled Wood Material Pack Sample | 0.8 GB | D | **Stylized** (carved wood) | Not usable |

`NeonStrips`, `QuantumCharacter` and `Realistic_Starter_VFX_Pack_Vol2` are also in `Content/` but were committed to git in a7904b0; they are not environment packs (see the report of #153).

### Per pack

**Megaplant_Library** (`/Game/Megaplant_Library/<Species>/<Species>_01/<Species>_01_A`, variants `_B`, `_C`, ...)

| Species | Kind | Fit for late-summer temperate Europe |
|---|---|---|
| `Tree_Black_Alder` | Deciduous | Yes, riverbanks |
| `Tree_Common_Hazel` | Deciduous (small) | Yes, hedgerows and wood edges |
| `Tree_Yoshino_Cherry` | Deciduous | Villages and gardens only |
| `Tree_Ginkgo` | Deciduous | No (East Asian, park tree) |
| `Tree_Norway_Spruce` | Conifer | Yes |
| `Tree_Baltic_Pine` | Conifer | Yes, **but its bark material references 4 missing textures** (`/Game/Developers/simonbarle/...`, `/Game/Megaplant_Library/Tree_Aleppo_Pine/...`): a pack defect |
| `Shrub_European_Spindle` | Bush | Yes, hedgerows |
| `Shrub_Beach_Rose` | Bush | Partly (coastal) |
| `Shrub_Greasewood` | Bush | No (desert) |

- All 148 skeletal and 159 static meshes are Nanite with 1 LOD, which is how Nanite foliage is meant to work in 5.8. Tree LOD0: 2–4 k triangles in the skeletal skin.
- Textures: 4096² (37), 2048×4096 (11); one 4096×8192 displacement map.
- The 10 `Decoration_Norway_Spruce_*` meshes (cones, twigs) have no simple collision, which is right for decoration.
- Measured in #158: about 38,000 trees on River Crossing add 0.7 ms of GPU time on average and 2.9 ms at worst on the development PC, with `r.Nanite.Foliage=1`. Their tick must be off, or it costs 8 ms of game thread. Heights: alder 12–17 m, spruce 8–29 m, hazel 1–5 m, spindle 2–6 m, cherry 4–8 m.
- The materials' parent, `MA_Foliage_Trees`, is in the ProceduralVegetationEditor plugin's content, and the meshes need its DynamicWind: without the plugin the trees render grey. It is enabled since #158.

**Real_Stones_Pack_01** (`/Game/Real_Stones_Pack_01/Meshes/SM_Stone_01`–`08`)

- 2–3 m boulders, 8 k triangles, 3 LODs, no Nanite, simple and complex collision, one material each.
- Textures 4096² (32). Turn on Nanite or keep the LODs; both are fine at 8 k.

**StylizedBridge** (`/Game/StylizedBridge/...`; the producer's *Ready Map Vol 2 – Bridge Environment*)

- 470 static meshes with LODs (464) and no Nanite. Concrete bridge kit in `StaticMesh/Bridge/ConcreteBridge/BridgeModularPrefabs/` (deck parts 9 × 14 m, pillars 22 m wide, sidewalks); roads, barriers, pipes, debris, train and vehicles.
- Landscape: `Landscape/LandscapeMaterial/M_AutoMaterial_Master` with `MI_AutoMaterial_A1`, 5 landscape grass types (Dirt, Forest, GrassMain, Rock, RockSlope), 5 layer infos and RVTs; river and water materials in `Landscape/River/`; 8 procedural foliage types (birch, locust, bush).
- Textures 2048² (111), 4096² (48).
- All of it is hand-painted stylized. Useful as a **layout reference** for a two-lane road bridge, not as final art next to the M20.

**ProceduralBuildingGenerator** (`/Game/ProceduralBuildingGenerator/...`)

- 262 static meshes: no Nanite, 28 with more than 1 LOD; 235 flagged. 13 Blueprints including the prefabs `Prefabs/PBG_Building_2F_2R`, `_2F_3R`, `_3F_3R`.
- All three prefabs reference 18 textures under `/Game/ModularCity/ProceduralBuildingGenerator/...`, a folder the pack does not contain (a pack defect), so they are left out of `L_PackScale`.
- The style is slum / ghost city (tin roofs, scaffolding), not a European village.

**Deko_MatrixDemo** (`/Game/Deko_MatrixDemo/Freeway/Meshes/...`)

- 168 Nanite static meshes: freeway lamps (4), ground and mounted poles (12), barriers (8), US signs (73), billboards (7), trash (24), sandbags (40).
- 148 textures at 4096², six at 8192 (billboards, signs, a metal normal); the eight trash meshes have no simple collision.
- Usable: lamps, poles, barriers, sandbags. Not usable: the US freeway signs and billboards.

**HighPoly_Tree_Model** (`/Game/HighPoly_Tree_Model/Meshes/SM_HP_Tree`)

- 19.5 × 18.3 × 27.9 m, Nanite, complex-as-simple collision, two materials, 2048² textures.
- Usable as a few hand-placed hero trees. Origin and licence are still unknown.

**Megaplant_Library, plugin note.** Its 24 procedural vegetation assets (`ProceduralVegetation`, grower presets, growth data) need the ProceduralVegetation editor plugin, which this project does not enable. The tree meshes themselves do not need it.

**UnrealDrive_CitySample** (`/Game/UnrealDrive_CitySample/...`, the sample content of MetaRoad)

- Materials: 15 asphalt (`Materials/MI_Asphalt/MI_Asphalt_Raw`, `MI_FreewayAsphalt_*`), 15 sidewalk, 6 concrete instances; textures 2048² to 8192² (40 at 8K).
- Meshes: concrete barrier, metal bollard, rail guard, traffic cone (with LODs); the `Maps/Meshes/Road*` meshes are baked strips of the demo map, without collision.
- 8 `RoadLaneAttributeDescriptorBlueprint` and 1 `RoadBuildPreset` need `/Script/MetaRoad` and `MetaRoadEditor`. MetaRoad is a code plugin installed per engine; it is not in UE 5.8 here, so the procedural road tool itself is not available.
- **MetaRoad is out (2026-10-22, #330):** installing it to 5.8 from the launcher fails every time. Only the pack's meshes and materials are used; the roads of #383 are drawn with the engine's own splines.

**MuddyGroundPack**, **SM_Concrete_07**, **SM_Tiles_02**, **Materials_StoneTiles**

- Scanned surface materials, one instance per surface (`MI_MuddyGround_4K_Inst`, `MI_MT00082_Concrete_Clean`, `MI_MT00183_Rough_Concrete`, damaged concretes, `MI_MT00022_Large_Granite_Tiles`, `MI_MT00195_New_Large_Bricks`, `MI_StoneTiles_01`–`09`).
- MuddyGround and StoneTiles 4096²; the two Scans Matter packs ship 20 textures at 8192² each (flagged). Since #330 `packaudit` gives every pack texture over 4K a Max Texture Size of 4096 (the summary counts them per pack), so no 8K map reaches the map.

**AdvancedRealisticGlass**, **StylizedSurface_Lite**, **MeshRift_CW_MaterialPackSample**

- Glass: 13 glass material instances, 4096² textures; for buildings later.
- The two others are hand-painted or carved stylized materials and do not meet the realism bar.

**Surfaces** (`/Game/Surfaces/<Name>/`, from `ue.ps1 -Task surfaces -Path D:\Downloads\textures`, #330)

- Not a Fab pack: the producer's 39 Megascans surface ZIPs (raw 8K or 4K JPGs with the scan's JSON) and one non-Megascans set (mud and pebbles). They are source textures, so the task scales them to 2K, packs AO, roughness and metalness into one ORM, and imports `T_<Name>_BC`, `_N` and `_ORM` with `MI_<Name>` of one master, `/Game/Surfaces/M_ScannedSurface`.
- The master projects from above in world space (no UVs needed) and tiles at the JSON's `scanArea` (`TileSize`, mostly 2 m; 0.25–4 m), with `Tint` and `RoughnessScale`. It is meant for tiles, props and checks; the landscape material of #156 samples the same textures.
- Raw Megascans normals are OpenGL (green up; checked against the displacement maps), so their green channel is flipped on import.
- 40 sets imported, 3 snow sets skipped; one set per Megascans id. Four of them are ambientCG (CC0) sets that `-Task freeassets` downloads (#409): `RoofingTiles006` (red clay), `RoofingTiles013A`, `Planks037A`, `Planks023A`, for the in-house buildings; their NormalGL maps are flipped like the Megascans ones. Git-ignored and never cooked like the packs. The run's list is `Saved/Surfaces/imported.md`.
- For the late-summer map (D3): grass (lush, wild ×2, uncut, mossy, clover), dirt and tracks (soil, dry trampled soil, forest path, 8 military trench dirts, bark-soil mix), gravel, forest floor ×3, sand and riverbank (rippled ×2, rocky sand), rock (gouged cliff, rough rock ×2, rocky ground and steppe, rocky soil), mud (mud and pebbles). Off-biome but imported: bright desert sand, Thai beach sand, coal ash, swamp water.

**PolyHaven** (`/Game/PolyHaven/<id>/SM_<object>`, from `ue.ps1 -Task freeassets`, #409)

- `Tools/Editor/free_assets.json` lists the models; `Tools/Editor/import_free_assets.py` downloads each one's 2K FBX and maps from Poly Haven's API into `Saved/FreeAssets/<id>/` (cached) and imports one Nanite static mesh per FBX object: a model's variants and a modular kit's parts become separate meshes, Poly Haven's `_LOD1`–`_LOD3` objects are dropped. One instance per material of `M_PolyHavenProp` (opaque: base colour, DirectX normal, ARM), `M_PolyHavenPropMasked` (a prop material with an alpha map, such as the chain-link wire: masked by it, two-sided) or `M_PolyHavenFoliage` (masked, two-sided, Two Sided Foliage shading with the base colour as subsurface colour, so the plants' shaded sides are lit through, #428).
- Cliffs: `rock_face_01`, `rock_face_02` and `mountainside` are open one-sided scans with no back. Place them face out against the terrain, the open back buried in the slope (#157, #158); `L_PackScale` turns them 180° so their face looks at the camera row.
- Collision: props an 18-DOP hull; the cliffs their own triangles (complex as simple); plants and branches none, which the audit flags as "no simple collision" on purpose.
- All textures 2048². The run's list, with each mesh's size and Poly Haven's triangle count, is `Saved/FreeAssets/imported.md`.
- **Open for #156, #158 and #160:** the scanned plants are dense. Poly Haven's counts per model are 6 k (fern) to 1.7 M triangles (celandine, five variants), grass_medium_02 1.0 M for five tufts of 16–40 cm. Fine as Nanite set dressing; as Landscape Grass or PCG at field density they must be measured first, and a Fab Megascans grass (05 §2) stays the upgrade.

| Model | Category | Meshes | Use | Authors |
|---|---|---|---|---|
| `grass_bermuda_01` | B | 21 | Short and dead grass tufts, 3–15 cm | Rico Cilliers |
| `grass_medium_02` | B | 5 | Meadow grass tufts, 16–40 cm | Rico Cilliers |
| `dandelion_01`, `celandine_01`, `periwinkle_plant` | B | 5, 5, 6 | Wildflowers | Rob Tuytel, Rico Cilliers; Amal Kumar |
| `nettle_plant`, `weed_plant_02`, `fern_02` | B | 6, 5, 4 | Verge weeds, wood-edge ferns | Rob Tuytel, Rico Cilliers |
| `shrub_02`, `shrub_03`, `shrub_04` | B | 4 each | Low bushes, up to 1.4 m tall (hedgerow fill) | Rico Cilliers |
| `moss_01` | B | 12 | Moss clumps | Rob Tuytel |
| `modular_electricity_poles` | E | 103 | Wooden poles of 5, 6 and 7 m with crossarms, insulators, fuses and three assembled presets | James Ray Cock |
| `modular_chainlink_fence` | E | 20 | Chain-link fence panels, posts, corners and a gate (yards, the depot) | James Ray Cock, Amal Kumar |
| `street_lamp_01`, `utility_box_01` | E | 1 each | A village street lamp (3.9 m), a roadside utility box | Josh Dean; James Ray Cock |
| `dead_tree_trunk`, `dead_tree_trunk_02`, `tree_stump_01`, `tree_stump_02` | F | 1 each | Fallen logs of 3 and 4 m, stumps | Rob Tuytel; Jenelle van Heerden, Rico Cilliers |
| `dry_branches_medium_01`, `bark_debris_01`, `root_cluster_01` | F | 3, 4, 1 | Branches, bark and roots on the forest floor | Rico Cilliers; Greg Zaal, Jenelle van Heerden |
| `rock_face_01`, `rock_face_02`, `mountainside` | F | 1 each | Small cliffs of 5 m, 5 m and 10 m | Dario Barresi, Rico Cilliers |

**Roadside kit** (`/Game/Art/Shared/Roadside`, committed; source `Art/Shared/roadside`, #409)

- Own work, because no CC0 source has European signs or a rural wooden fence. `Tools/Art/roadside_textures.py` draws the sign faces from Vienna Convention geometry (pure Python, its own stroke lettering, no font); `Tools/Art/roadside_blender.py` builds the meshes; `-Task freeassets` imports them, checked against `roadside.json`.
- Signs on galvanised posts at their normal sizes, plate bottom at 2 m: `SM_Sign_speed_50` (600 mm), `SM_Sign_warning` and `SM_Sign_give_way` (900 mm triangles), `SM_Sign_priority_road` (600 mm diamond), `SM_Sign_place_name` (1200 × 500 mm on two posts, the fictional village MERWEDAL). 56–236 triangles each, UCX_ collision on the posts.
- Fences in 3 m bays along +X with an end post each: `SM_Fence_PostRail` (wooden post-and-rail, 1.25 m), `SM_Fence_FieldWire` (round post, four wires, 1.15 m).
- `SM_PowerLine_Cable`: 1 m of 16 mm conductor along +X for a spline mesh between the poles' insulators; no collision. #157 strings the lines.
- Flat materials: `M_Roadside` (`MI_Roadside_Galvanised`, `_Wood`, `_Wire`) and `M_RoadsideFace` with one `MI_Sign_<sign>_Face` per sign.

### Licences

| Source | Licence | Commercial game | Recorded |
|---|---|---|---|
| Poly Haven models (`/Game/PolyHaven`) | CC0 1.0 (polyhaven.com/license) | Yes, no attribution required | Authors per model above; source URL per mesh in `Saved/FreeAssets/imported.md` |
| ambientCG surfaces (the four sets above) | CC0 1.0 (ambientcg.com) | Yes, no attribution required | Set ids above |
| Roadside kit | Own work of this project | Yes | `Tools/Art/roadside_*.py` |
| Megascans surfaces, Fab packs | Fab / Quixel licences | Packs not committed or cooked; the landscape's 21 surface textures are committed and cooked (#156, producer's decision) | Manifest above; layer table in 03 §5 |

### Coverage against the minimum set (05 §2)

Updated 2026-10-22 (#409). The village buildings and the bridge are made in-house (05 §2); every other category has its minimum set.


| Category | Minimum set | Now | Status |
|---|---|---|---|
| A. Landscape surfaces | Grass, meadow, dirt, mud, gravel, rock, sand/riverbank, asphalt; one realistic family, 2K+ | Scanned surfaces (#330): grass, meadow, clover, dirt and tracks, gravel, forest floor, sand, rock; mud (MuddyGround, mud and pebbles), asphalt (UnrealDrive), concrete and paving (Scans Matter, StoneTiles); roof tiles and planks (ambientCG, #409) | **Met** |
| B. Trees | 3 deciduous + 1–2 conifers, Nanite or LODs | Black alder, hazel, cherry; Norway spruce, Baltic pine (broken bark) | **Met** |
| B. Bushes, grass, flowers | Hedgerow bushes, shrubs, grass and wildflower cards | Spindle, beach rose; Poly Haven grass tufts (bermuda, meadow), wildflowers (dandelion, celandine, periwinkle), nettle, weeds, ferns, low shrubs, moss (#409) | **Met**; density to measure in #156/#158 |
| D. Buildings | One modern European village kit | PBG slum prefabs, broken references; AccuCities city tiles; facade, paving and glass materials only | **Made in-house** (2026-10-22): enterable, garrisoned and destructible, which no pack offers |
| E. Infrastructure | Road bridge, road decals, kerbs, fences, power poles and lines, streetlights, signs, barriers | US freeway lamps, poles, barriers; UnrealDrive asphalt, kerbs, sidewalks, bollards, guardrails; Poly Haven wooden power poles, chain-link fence, street lamp, utility box; in-house European signs, post-and-rail and pasture fences, power-line cable (#409) | **Met**; the bridge is made in-house with the buildings |
| F. Natural details | Rocks, small cliffs, logs, debris, same family as A | 8 scanned stones; rock-face and cliff surfaces (#330); Poly Haven cliffs (2 rock faces, a mountainside), logs, stumps, branches, bark, roots (#409) | **Met** |

### L_PackScale

`/Game/Developers/PackAudit/L_PackScale` holds, on a 300 m ground: the Abrams (`/Game/Art/Local/Meshes/SM_Abrams_*`) and the M20 Guardian at the origin; one variant of each Megaplant species except the Baltic pine, and `SM_HP_Tree`, along +x; four stones; one StylizedBridge pillar base; the packs' landscape and water materials on 10 m tiles, and a second row with the surface packs (mud, two asphalts, sidewalk, two concretes, granite tiles, two stone pavings). Since #330 the rows go on with every scanned surface of `/Game/Surfaces`. Since #409 a row runs from the Abrams along −x: grass tufts, a dandelion, a celandine, a nettle, a shrub, the post-and-rail and pasture fences, a chain-link panel, a 6 m pole, the power-line cable, three signs, a log, a stump, branches, bark, two rock faces and the mountainside (turned face to the row, #428). 100 assets placed (2026-10-23), missing references: 0. No building is placed, because no pack has a usable one.

## How this was checked (2026-10-15)

The Unreal MCP server (`unreal`, `http://127.0.0.1:8000/mcp`) was not running, so the Asset Registry could not be queried. Every finding below comes from one of three sources:

| Level | Meaning |
|---|---|
| **Header verified** | The class and asset-registry strings were read from the `.uasset`/`.umap` file, for example `/Script/Engine.StaticMesh`, `NaniteEnabled True`, `Dimensions 2048x2048`. |
| **Name inferred** | Only the file name and folder were looked at. |
| **Generator verified** | Read from `Tools/Editor/setup_testmap.py`, which writes the data assets and test levels on `ue.ps1 -Task setup`. |

Phase 2 starts with a re-check in the editor (see [05_PRODUCER_ACTIONS.md](05_PRODUCER_ACTIONS.md)).

## Summary

Content holds 359 files (about 49.5 MB). The project has **one** real environment asset, a single high-poly tree. Every other environment category has no usable asset. There are no Megascans, Quixel or Fab packs, no StarterContent, no `__ExternalActors__`/`__ExternalObjects__` folders and no plugin content.

| Category | Usable assets | Coverage |
|---|---|---|
| A. Landscape materials | 0 | None |
| B. Vegetation | 1 tree (needs work, see below) | Almost none |
| C. Water | 0 | None |
| D. Buildings | 0 environment buildings | None |
| E. Infrastructure | 0 | None |
| F. Natural details | 0 | None |
| G. Atmosphere | Engine actors only (no project presets) | Basic |

## Top-level Content folders

| Folder | Files | Size | Contents |
|---|---|---|---|
| `/Game/Art` | 58 | 4.9 MB | Placeholder kit meshes for units and buildings, the local Abrams and Battlemaster, the M20 Guardian |
| `/Game/Data` | 36 | 0.5 MB | Unit, building, weapon and map data assets; map previews |
| `/Game/HighPoly_Tree_Model` | 9 | 37.1 MB | One tree pack. **Not tracked in git.** |
| `/Game/Maps` | 8 | 2.1 MB | Game levels |
| `/Game/Materials` | 7 | <0.1 MB | Flat project materials |
| `/Game/UI` | 243 | 4.9 MB | UI only, not relevant here |

## A. Landscape materials

| Asset | Path | Type | Notes |
|---|---|---|---|
| M_Ground | `/Game/Materials/M_Ground` | Material (header verified) | Constant colour (0.06, 0.075, 0.04) with roughness 0.9 (generator verified). No textures and not a landscape material. **Not suitable.** |

Since #156 the landscape draws in `/Game/World/Landscape/MI_Landscape_WD_Temperate` (03 §5), with layer infos in `/Game/World/Landscape/Layers` and the RVT `RVT_WD_RiverCrossing_01`.

## B. Vegetation

| Asset | Path | Type | Size | Notes |
|---|---|---|---|---|
| SM_HP_Tree | `/Game/HighPoly_Tree_Model/Meshes/SM_HP_Tree` | StaticMesh (header verified) | 19.1 MB | See below |
| M_HP_Tree_Leaf | `/Game/HighPoly_Tree_Model/Materials/M_HP_Tree_Leaf` | Material (header verified) | 13 KB | Uses T_HP_Tree_Leaf |
| M_HP_Tree_Trunk | `/Game/HighPoly_Tree_Model/Materials/M_HP_Tree_Trunk` | Material (header verified) | 20 KB | Uses the trunk colour and normal maps |
| T_HP_Tree_Leaf | `/Game/HighPoly_Tree_Model/Textures/Leaf/T_HP_Tree_Leaf` | Texture2D (header verified) | 4.0 MB | 2048², DXT5 with alpha |
| T_HP_Tree_Trunk | `/Game/HighPoly_Tree_Model/Textures/Trunk/T_HP_Tree_Trunk` | Texture2D (header verified) | 7.6 MB | 2048², DXT1 |
| T_HP_Tree_Trunk_normal | `/Game/HighPoly_Tree_Model/Textures/Trunk/T_HP_Tree_Trunk_normal` | Texture2D (header verified) | 4.8 MB | 2048², BC5 |

**About SM_HP_Tree** (header verified):

| Property | Value |
|---|---|
| Nanite | On; 564,214 triangles and 516,610 vertices |
| Fallback mesh | 27,797 triangles at 100% fallback |
| LODs | 1, LOD group Foliage |
| Bounds | About 19.5 × 18.3 × 27.9 m |

- **Suitability:** a believable deciduous hero tree at real scale (28 m is tall but plausible for a mature oak or beech). It's only one species, with no conifer and no variants.
- **Performance:**
  - Too heavy to paint as forest foliage at RTS densities.
  - There is no impostor, no billboard and no foliage type asset.
  - It has no wind setup that could be seen from the headers.
  - Use it only as a few hand-placed hero trees until it is tested with Nanite foliage in 5.8.
- **Provenance:**
  - The mesh's import path is `../Users/tehran/Videos/Leaf/New 3/Trees/...`, so it is a third-party asset and reimporting it won't work.
  - The folder is untracked in git (`?? Content/HighPoly_Tree_Model/`).
  - The licence must be confirmed before it is committed.
- **Defect:**
  - The demo map is `HP_Tree_Showcace` (typo) but its lighting build file is `HP_Tree_Showcase_BuiltData`, so the build data is probably orphaned.
  - The map's instanced-foliage actor references no foliage type.
  - This is harmless because the map is a demo.

There are no conifers, bushes, shrubs, grass, wildflowers or ground cover.

## C. Water

| Asset | Path | Type | Notes |
|---|---|---|---|
| M_Water | `/Game/Materials/M_Water` | Material (header verified) | Constant colour (0.015, 0.04, 0.08) with roughness 0.1. Applied to flat cubes named `Water_*` in `L_RiverFord`. **Not suitable** as a visual river. |

There are no water shaders, riverbed materials or shoreline assets. The engine's **Water** plugin is installed but not enabled (see [03_TECHNICAL_PLAN.md](03_TECHNICAL_PLAN.md)).

## D. Buildings

There are no European houses, farmhouses, barns, warehouses or garages.

The game's own buildings are stylised low-poly placeholders from a kit (`T_CityKit*`, `T_FactoryKit`, `T_CastleKit` colour maps at 512×512). They are gameplay buildings, not set dressing, and are listed below only as a scale reference.

## E. Infrastructure

| Asset | Path | Notes |
|---|---|---|
| SM_Wall | `/Game/Art/Placeholder/Meshes/SM_Wall` | A gameplay wall from the placeholder kit. Not set dressing. |

There are no roads, bridges, fences, streetlights, power lines, signs or barriers.

## F. Natural details

There are no rocks, cliffs, logs, fallen trees or ground clutter. `SM_SupplyCrate` is a gameplay prop.

## G. Atmosphere and lighting

There are no project-owned sky, fog or post-process assets. The levels contain only engine actors:

| Level | Actors (header verified) |
|---|---|
| All game maps (`L_TestMap`, `L_Skirmish`, `L_SkirmishDuel`, `L_RiverFord`, `L_QuarryCross`, `L_SixPeaks`) | `DirectionalLight` "Sun" at pitch -50°, yaw 35° as the atmosphere sun; `SkyLight` with real-time capture; `SkyAtmosphere`. Written by `spawn_lighting` in `setup_testmap.py`. |
| `/Game/HighPoly_Tree_Model/Maps/HP_Tree_Showcace` | Adds `VolumetricCloud`, `ExponentialHeightFog` and the engine sky sphere. |

There are no post-process volumes in any level. `Config/DefaultEngine.ini` sets only `r.AllowStaticLighting=False`, so Lumen and shadows run on engine defaults.

## Existing levels

| Level | Size | Terrain |
|---|---|---|
| `/Game/Maps/L_MainMenu`, `/Game/Maps/L_Lobby` | 7 KB | Empty |
| `/Game/Maps/L_TestMap` | 329 KB | Cube ground ±100 m. Editor startup map. |
| `/Game/Maps/L_Skirmish`, `/Game/Maps/L_SkirmishDuel` | 327 / 315 KB | Cube ground ±100 m (test maps) |
| `/Game/Maps/L_RiverFord` | 302 KB | Cube ground ±80 m; "river" is three `Water_*` cubes |
| `/Game/Maps/L_QuarryCross` | 430 KB | Cube ground ±110 m |
| `/Game/Maps/L_SixPeaks` | 503 KB | Cube ground ±120 m |

No level contains a Landscape, and no level uses World Partition or One File Per Actor (no `__ExternalActors__` folder). All of them are generated by `setup_testmap.py`.

## Scale reference: game meshes

All header verified as StaticMesh with Nanite off.

| Asset | Path | Notes |
|---|---|---|
| Placeholder kit | `/Game/Art/Placeholder/Meshes/SM_{Airfield, Barracks, Builder, Headquarters, Infantry, Jet, PowerPlant, SupplyCrate, SupplyDepot, Tank, Tower, Truck, VehicleFactory, Wall}` | 55–110 KB each. Scaled down in data (for example, the tank fits 260 × 150 cm). |
| Abrams | `/Game/Art/Local/Meshes/SM_Abrams_Hull`, `SM_Abrams_Turret` | Hull mesh about 8.0 × 3.7 × 1.8 m, roughly real scale. **Ignored by git** (`.gitignore`: `Content/Art/Local/**/*Abrams*`). |
| Battlemaster | `/Game/Art/Local/Meshes/SM_Battlemaster` | Tracked in git |
| M20 Guardian | `/Game/Art/USA/Vehicles/M20Guardian/Meshes/SM_M20_*` | Hull, turret, gun, RWS, wheels, tracks. 4K-class BC/N/ORM textures (3.7–5.7 MB each). Near real scale; not yet used by data. This is the quality and texel bar the environment must match. |

The current gameplay data shrinks units to about 1/3 of real size. With the producer's decision to move to real scale (see [02_MAP_DESIGN.md](02_MAP_DESIGN.md)), the Abrams and M20 meshes already match real-world environment assets.

## Fab library: what is reachable from this machine

| State | Finding |
|---|---|
| **1. Imported into World Divide** | Only `HighPoly_Tree_Model`. Its origin (Fab or elsewhere) is unknown. |
| **2. Downloaded but not imported** | **None.** `C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\` contains only `FabLibrary\listings_v1.db` (empty). `%LOCALAPPDATA%\FabPlugins\settings_v1.json` has empty download paths. There is no Megascans library, and no other Unreal project on `D:\` to migrate from. |
| **3. Not accessible** | Everything in the producer's Fab account library. It can't be listed without signing in, and that wasn't attempted. |

Tools that are installed:
- the **Fab UE Plugin** (`Engine/Plugins/Fab`)
- **Quixel Bridge** (`Engine/Plugins/Bridge`)
- the launcher manifests `UE_5.8`, `FabPlugin_5.8` and `QuixelBridge_5.8`

So the producer can add packs straight into the open editor (see [05_PRODUCER_ACTIONS.md](05_PRODUCER_ACTIONS.md)).

Other downloads are not environment assets:
- `D:\Downloads\abrams-m1a1-free.zip` is a unit model.
- `D:\Downloads\Selection_2026-10-14.zip` is a UI delivery.
- `D:\Downloads\Generals 2` is a UI structure reference only and is never an asset source.

## Missing categories (no purchases recommended yet)

| Need | Must cover |
|---|---|
| **Landscape surfaces** | Grass, meadow, dirt, mud, gravel, rock, sand/riverbank, asphalt. Tileable PBR at 2K or larger, ideally one consistent scan-based set. |
| **Vegetation** | At least 3 deciduous species and 1–2 conifers with LODs or Nanite foliage and impostors; bushes and hedgerows; grass and wildflower cards suited to Landscape Grass. |
| **Water** | Nothing to buy; the engine Water plugin covers it. Riverbed pebbles and shoreline mud textures are needed. |
| **Buildings** | A modular or kit set of modern European village buildings: houses, farmhouse, barn, small workshop/garage, a church or landmark. One style. |
| **Infrastructure** | Two-lane road bridge (or a modular bridge kit), road decals and kerbs, fences/hedges, power poles and lines, streetlights, signs, concrete barriers. |
| **Natural details** | Rocks, small cliffs, logs, debris and clutter, preferably from the same scan family as the surfaces. |
