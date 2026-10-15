# WD_RiverCrossing_01: environment asset audit

Phase 1, investigation only. Date: 2026-10-15. Engine: UE 5.8 (`WorldDivide.uproject`, `"EngineAssociation": "5.8"`).

## How this was checked

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
