# WD_RiverCrossing_01: producer actions

What only the producer can do or decide, ordered by when it blocks work.

## 1. Decisions

| # | Decision | Blocks | Recommendation |
|---|---|---|---|
| D1 | **Real-scale target values** for units, buildings, ranges and the camera. The direction is decided (2026-10-15); the numbers are not. | Phase 0 and every map number | Start from the planning envelope in [02_MAP_DESIGN.md](02_MAP_DESIGN.md) |
| D2 | Minimum spec PC and fps target (for example, 60 fps at 1080p with 200 units) | Phases 6–8 | Name a GPU class |
| D3 | Season and biome: summer or early autumn temperate | Phase 1 pack choice | **Decided 2026-10-20: late summer, temperate** (greens with dry field tones) |
| D4 | Do playable woods slow or block vehicles, or are they cover only? | Phase 6 | Cover only for 01 |
| D5 | Are village buildings garrisonable at the map's launch, or only tagged for later? | Phase 5 | Tag now, garrison when the system exists |
| D6 | Does `WD_RiverCrossing_01` join the lobby next to `L_RiverFord`, or replace it? | Phase 2 and 9 | Join; retire `L_RiverFord` once the rescale is done |
| D7 | Is Lumen hardware ray tracing allowed on the minimum spec? | Phase 7 | Software Lumen |
| D8 | Approve enabling the **Water** and **PCG** plugins (optionally LandscapePatch and GeometryScripting) in `WorldDivide.uproject` | Phase 2 | Approve Water and PCG |

## 2. Assets to add from your Fab library

Two batches of packs were imported and audited on 2026-10-20 (manifest and coverage in [01_ASSET_AUDIT.md](01_ASSET_AUDIT.md), "Imported packs"). Trees, rocks, mud, asphalt, concrete and paving are covered. #153 closed on those; these gaps moved to its follow-up issue, which comes before painting (#156) and the villages (#157):

| Category | Still missing (late-summer temperate Europe, realistic) |
|---|---|
| Landscape surfaces | Grass, meadow, dirt, gravel, rock, sand/riverbank. Megascans surface ZIPs already downloaded in `D:\Downloads\textures` (wild and lush grass, rocky soil, gravel, forest floor, leaves) are only source textures; the follow-up adds a script that imports them. Still to download: riverbank sand or silt, a rock face, a dry harvested field, a dirt track. |
| Bushes, grass, flowers | Grass and wildflower cards for Landscape Grass; more hedgerow bushes |
| Buildings | One modern European village kit: houses, farmhouse, barn, workshop/garage, a landmark (church or mill). The Procedural Building Generator is a slum kit with broken material references. Facade materials (brick, stucco, plaster, concrete) are in the ZIPs; roof tiles and wood planks are not. |
| Infrastructure | A realistic two-lane road bridge or bridge kit, fences, power poles and lines, European signs. Freeway lamps, poles and barriers are covered by Deko_MatrixDemo; asphalt, kerbs, sidewalks and guardrails by UnrealDrive_CitySample. The **MetaRoad** road tool needs its plugin: Epic Games Launcher → Fab Library → MetaRoad → *Install to Engine* → 5.8, if the seller offers a 5.8 build. |
| Natural details | Small cliffs, logs, debris, ideally from the surface family. Rocks are covered (Real Stones Pack 01). |

**Storage:** the repository is public, so packs are never committed. Keep each in its own `Content/<Pack>/` root, add it to `.gitignore` and to `DirectoriesToNeverCook` in `Config/DefaultGame.ini` (the cook test fails otherwise), then run `Tools\Scripts\ue.ps1 -Task packaudit` and tell me the folder name.

**How to add a pack** (no purchase is needed for packs already in your library):
1. Open the project: `Tools\Scripts\ue.ps1 -Task editor`.
2. In the editor, open the **Fab** window (Window → Fab, or the Content Browser's *Add → Add Fab content*). The Fab plugin is installed in the engine.
3. Sign in there, open **My Library**, and press **Add to project** on each pack. Megascans assets can also come through **Quixel Bridge**, which is installed.
4. Alternatively, use the Epic Games Launcher → Fab Library → *Add to project* → WorldDivide (5.8).
5. Tell me the folder names that appear under `Content/`. I will then run `ue.ps1 -Task packaudit` and update 01.

**Packs listed for an older engine:** a content-only pack usually loads in 5.8. In the Launcher, use *Add to project* with **Show all projects** ticked and pick WorldDivide. A code plugin (MetaRoad, ProceduralVegetation tools) only works with a build for 5.8.

**Before importing:**
- Check each pack's licence covers use in a commercial game.
- Note any pack that is not offered for UE 5.8 (it may need *Show all projects*).

## 3. Existing files to resolve

- **`Content/HighPoly_Tree_Model/`** (git-ignored, kept locally since 2026-10-20).
  - The audit shows it usable as a hand-placed hero tree (Nanite, 28 m, complex collision), not for forests.
  - It cannot ship until you confirm where it came from and that its licence allows it.
- **`Content/Art/Local/` Abrams** is git-ignored on purpose. It is the only real-scale MBT besides the M20 Guardian; keep that in mind for the scale checks in phase 1.

## 4. Editor and settings

- **Turning on the plugins:** after D8 I'll add Water and PCG to `WorldDivide.uproject`. You then reopen the editor once so the plugins compile and load.
- **Navigation settings:** two nav agents and bigger tiles in `Config/DefaultEngine.ini` are part of the phase 0 issue, not a separate change.
- **Running editor checks:** for phases 1 and onward, keep the editor open with `ue.ps1 -Task editor` while I run checks. The MCP server only reads and plays; it can't save.
- **LFS locks on the level:** landscape sculpting and painting lock `WD_RiverCrossing_01.umap` through LFS. Only one person can sculpt at a time.

## 5. Access that is missing

| Missing | Effect | Fix |
|---|---|---|
| Unreal MCP server was not running during this audit | Asset types and Nanite flags were read from file headers, not the Asset Registry | Open the editor before phase 1 |
| Fab account library | Can't tell which of your packs exist or fit | Your list of pack names, or add them as in §2 |
| GitHub milestone work orders | Resolved 2026-10-19: the map is the Map track of `M1 First slice` (M1-M01 to M1-M09) | — |
