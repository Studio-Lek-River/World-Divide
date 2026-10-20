# WD_RiverCrossing_01: environment asset audit

Phase 1. First audit 2026-10-15 (no packs yet, from "How this was checked" down); packs imported and audited 2026-10-20 (#153, the next section). Engine: UE 5.8 (`WorldDivide.uproject`, `"EngineAssociation": "5.8"`).

## Imported packs (2026-10-20)

### Storage rule

The repository is public and the pack licences do not allow sharing the files, so the packs are **not** in git:

- Each pack stays in its own `Content/<Pack>/` root, as Fab installs it. Moving them to `/Game/World/Packs/` would only rewrite references in files nobody else has.
- Each is git-ignored (`.gitignore`) and never cooked (`Config/DefaultGame.ini`, `DirectoriesToNeverCook`; `WorldDivide.Config.Cook` checks it). Cooking what the map uses is decided with #154 onwards.
- Every machine adds the packs in the manifest below from Fab (*My Library → Add to project*, see [05_PRODUCER_ACTIONS.md](05_PRODUCER_ACTIONS.md) §2) and then runs `ue.ps1 -Task packaudit`.

### How this was checked

`Tools/Scripts/ue.ps1 -Task packaudit` runs `Tools/Editor/audit_packs.py` headless. It reads every asset's Asset Registry tags (not the file headers as on 2026-10-15) and writes:

- `Saved/PackAudit/<Pack>.csv`: one row per asset, with mesh size in cm (`ApproxSize`; 1 uu = 1 cm), Nanite, LODs, triangles, collision primitives and complexity, material slots, and texture size and format.
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
- Open for #158: cost of Nanite skeletal trees at RTS densities is not measured yet.

**Real_Stones_Pack_01** (`/Game/Real_Stones_Pack_01/Meshes/SM_Stone_01`–`08`)

- 2–3 m boulders, 8 k triangles, 3 LODs, no Nanite, simple and complex collision, one material each.
- Textures 4096² (32). Turn on Nanite or keep the LODs; both are fine at 8 k.

**StylizedBridge** (`/Game/StylizedBridge/...`)

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

### Coverage against the minimum set (05 §2)

| Category | Minimum set | Now | Status |
|---|---|---|---|
| A. Landscape surfaces | Grass, meadow, dirt, mud, gravel, rock, sand/riverbank, asphalt; one realistic family, 2K+ | Stylized auto-material (StylizedBridge); a few loose PBG ground textures | **Missing** |
| B. Trees | 3 deciduous + 1–2 conifers, Nanite or LODs | Black alder, hazel, cherry; Norway spruce, Baltic pine (broken bark) | **Met** |
| B. Bushes, grass, flowers | Hedgerow bushes, shrubs, grass and wildflower cards | Spindle, beach rose; no realistic grass or flower cards | **Partly**: grass and flowers missing |
| D. Buildings | One modern European village kit | PBG slum prefabs, broken references | **Missing** |
| E. Infrastructure | Road bridge, road decals, kerbs, fences, power poles and lines, streetlights, signs, barriers | Stylized bridge and roads; US freeway lamps, poles, barriers | **Partly**: realistic bridge, European signs, fences, power lines missing |
| F. Natural details | Rocks, small cliffs, logs, debris, same family as A | 8 scanned stones | **Partly**: cliffs and logs missing |

### L_PackScale

`/Game/Developers/PackAudit/L_PackScale` holds, on a 300 m ground: the Abrams (`/Game/Art/Local/Meshes/SM_Abrams_*`) and the M20 Guardian at the origin; one variant of each Megaplant species except the Baltic pine, and `SM_HP_Tree`, along +x; four stones; one StylizedBridge pillar base; the packs' landscape and water materials on 10 m tiles. Missing references: 0. No building is placed, because no pack has a usable one.

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

There are no landscape materials, layer infos, terrain textures, RVT assets or blend materials.

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
