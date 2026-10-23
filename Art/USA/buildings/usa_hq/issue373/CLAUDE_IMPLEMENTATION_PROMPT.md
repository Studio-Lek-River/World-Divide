# USA HQ #373: integrated

The baked shell is in production. This folder keeps only what integration uses, plus the record of the four Codex rounds.

- `final/HQ373_Shell_Final.blend` holds the accepted source, with the objects `SM_HQ373_R4_Shell` (LOD0) and `SM_HQ373_R4_Shell_LOD1`.
  - It is Round 4's save from before its unfinished paving-edge UV edit (`round4/HQ373_Round4_Final.blend1`).
  - `Tools/Art/usa_hq_game_blender.py -- --baked-shell` takes it into `game/SM_HQ_Shell.fbx` and `game/SM_HQ_Shell_LOD1.fbx`.
- `final/texture_set_mapping.json` gives each shell slot its bake set. The maps are `textures/T_HQ_ShellBake_Set{0,1}_{Normal,AO}.png`, two 2K sets on UV1.
- `usa_hq_recipe.py` (`SHELL_BAKE_*`) gives each baked slot its own `MI_*_Baked` instance, with the building masters' `ShellBake` switch on and the set's maps (`building_art._shell_bake`). Five slots stay unbaked: the floors, the walkpads and the lit interior.
- Counts in Unreal, checked by `WorldDivide.Art.USAHeadquarters`:
  - shell: 62,077 / 19,255 triangles;
  - whole HQ: 144,485 / 39,596 triangles;
  - 80 material sections.
- Evidence: Codex's Unreal master-against-game captures in `round4/unreal/`. The Round 1–4 reports describe how the shell was made. The working files they refer to (logs, intermediate blends, Blender sheets, candidate packages and scratch scripts) were removed after integration.

Open: Unreal reports "nearly zero tangents" on `SM_HQ_Shell` when it reimports over the existing asset. It is display-level only; a clean first import of the same FBX does not report it.
