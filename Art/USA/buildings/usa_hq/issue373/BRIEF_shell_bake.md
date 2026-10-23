# #373: a baked 150k game mesh for the HQ shell

**Why.** The HQ's game shell is the dense delivery, merged and reduced by the engine (`Tools/Art/usa_hq_game_blender.py`, `building_art.import_mesh`). Its LOD0 needed 350,000 triangles instead of the large building's 150,000, because the delivery has no baked game mesh (`docs/art/MODEL_PERFORMANCE.md`, rule 7). The HQ now sits at 349,968 of 350,000, and #394 (the kit's fixtures on the HQ) cannot add geometry until this lands. At 12 m the engine reduction already shows: the drain grilles lose their slats and the flagpole is slightly faceted (`Saved/BuildingShots/DA_TestHeadquarters` against `DA_BenchHQ_Nanite`).

The fix is a real game mesh made from the approved master, with its fine detail baked into normal and AO maps, as rule 7 asks.

## Inputs

- The approved master: `source/USA_HQ_Final.blend`.
- The nine static sources `usa_hq_game_blender.py` merges into `SM_HQ_Shell`, in `fbx/` (its `SHELL` table): `SM_HQ20_Architecture_WallCorrected`, `SM_HQ20_RoofSystems_WallCorrected`, `SM_HQ21_Courtyard`, `SM_HQ20_GarageInterior`, `SM_HQ20_DoorHardware_Delivery`, `SM_HQ21_CourtyardDetails_Delivery`, `SM_HQ20_GaragePortal`, `SM_HQ21_PlantingFinish_Delivery`, `SM_HQ20_GarageFloor`.
- The current game output and its counts per slot: `game/game_meshes.json`.

## Constraints

- Follow `.agents/skills/world-divide-building-production/SKILL.md`. Work in a candidate preview level under `/Game/Art/Candidates/USA_HQ/`. Preserve every approved HQ design (#190, #351, #379, including the H28 masts in `HANDOFF_379.md`, and the #195 fixtures).
- **What changes.** Only the static shell, the nine sources above. Moving parts (door, fans, dishes, radar, flag) and the masts stay as they are. `SM_HQ_ShellDetail` and its hand-made LOD1 stay as they are: their small details (solar cells, team lights, markings, door light strips, leaves, soil) keep their own triangles.
- **Output.** Low-poly replacements for the nine sources as the generator's input, either nine FBX or one merged shell FBX, each with LOD0 and LOD1. They keep the delivery frame (+X the front, centimetres in the FBX) and the existing material slots and instance names. LOD1 keeps the exact silhouette and the same slots in the same order.
- **Budget.** The whole HQ within 150,000 / 40,000 triangles (LOD0/LOD1), counted from the re-imported FBX, with about 5,000 LOD0 to spare for the #394 mounts. Spend triangles on what reads at the RTS camera: silhouettes, frame bevels at 12 m, the flagpole, the drain grille slats.
- **Bake.** High to low: normal and AO from the master into a second, unique UV channel (UV1). The tiling materials keep UV0 as they are. Texture sets are at most 2K (2048 × 2048), with mipmaps; 4K only with the producer's approval. Report how many sets and which meshes or parts each covers.
- **Material.** One candidate change to `M_HQ_Final_Operational`. It reads the baked normal (blended with the tiling normal) and the baked AO from UV1, behind a static switch whose default is off, so the other meshes on that parent do not change. Name the switch and texture parameters; Claude ports the change to production setup.
- **Evidence.** Rule 8 of `MODEL_PERFORMANCE.md`: the master against the game mesh at the 12 m, 150 m and 450 m RTS views, with the grille slats and the flagpole called out at 12 m, and the pair at 4 % inside and outside the 260 m switch. The manifest lists LOD0 and LOD1 triangles, material sections and texture sizes from the re-imported FBX. If the 12 m view visibly loses detail at 150k, stop and ask the producer; do not accept a worse look.

## Handoff to Claude

Deliver the FBX and LODs with hashes, the baked textures, the UV1 and texture-set mapping, the material change with its parameter values, the counts and the captures, together with a `CLAUDE_IMPLEMENTATION_PROMPT.md` in this folder.

Claude then:
- points `usa_hq_game_blender.py` at the baked inputs, which drops its decimation from the delivery's density;
- sets the HQ's budget in `usa_hq_recipe.py` and `WorldDivide.Art.USAHeadquarters` back to 150,000 / 40,000;
- imports the baked textures and adds the material switch in setup;
- drops the 350k exception from `MODEL_PERFORMANCE.md`;
- closes #373 with `buildingshots -Building DA_TestHeadquarters` at 12, 150 and 450 m against the master.

## Codex starter

> Use `$world-divide-building-production` for USA HQ #373. Read `Art/USA/buildings/usa_hq/issue373/BRIEF_shell_bake.md` and `HANDOFF_379.md`. Make a game mesh for the HQ's static shell from the approved master, with its fine detail baked into normal and AO maps on UV1, within 150k / 40k triangles and the existing material slots. Preview it against the master in a candidate level and hand it back to Claude with an implementation prompt.
