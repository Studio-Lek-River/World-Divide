# #431: River Crossing's village set and the bridge damage states

Codex makes the candidates; Claude reviews every round and integrates the accepted delivery in #432. The design brief is
[`docs/art/village/VILLAGE_BRIEF.md`](../../../../docs/art/village/VILLAGE_BRIEF.md): style, sizes, states, garrison
openings, bridge, budgets. This file adds how the rounds run. The current state is [`STATE.md`](STATE.md).

## Read first

- `.agents/skills/world-divide-building-production/SKILL.md`, and its `references/modelling-lessons.md` (check your
  delivery against it before you report) and `references/hq-lessons.md`.
- `docs/art/village/VILLAGE_BRIEF.md` and `docs/art/MODEL_PERFORMANCE.md`.
- The greybox contract: `Tools/Art/village_blender.py`, `Art/Shared/village/village.json` (names, slots, bounds,
  collision boxes, garrison openings) and the FBX beside it. The bridge: `Tools/Art/bridge_blender.py`, `SM_Bridge_Span`.

## Rounds

| Round | Model | Scope | Gate |
| --- | --- | --- | --- |
| 1 | Sol | HouseA pristine, damaged, rubble: massing, openings, roof, real materials, first Unreal preview beside an Abrams | Producer approves HouseA's architecture |
| 2 | Sol | HouseA finished to the quality bar: game mesh, LOD1, bake, final materials, rule-8 captures | Producer approves HouseA as the bar |
| 3 | Sol | HouseB, HouseC, Farmhouse, Barn, Workshop, Church (three states each), `SM_Bridge_Span_Damaged` / `_Destroyed` | Claude review |
| 4 | Astra | Realism pass over the whole set, consolidation into `final/`, Claude implementation prompt | Producer approves the set |

Every round ends with a `ROUND_<n>.md` in this folder (what changed, counts, evidence table, open defects) and stops.
Claude writes the next round's targets into `STATE.md`.

## Paths

- Work in `Art/Shared/village/issue431/round<n>/` (blend sources, FBX, textures, evidence). Do not touch the greybox
  files in `Art/Shared/village/` itself; #432 replaces them from `final/`.
- Generators in `Tools/Art/village_431_*.py`, safe to import, parameterised roots.
- Unreal: `/Game/Art/Candidates/Village/` only, with one preview level `/Game/Art/Candidates/Village/L_VillagePreview`.
  Create it once from `/Engine/Maps/Templates/Template_Default` through the MCP's `AssetTools.duplicate` and
  `save_assets` (the pattern in `Tools/Art/kit/strip_light_preview.py`), with neutral afternoon daylight, a ground plane,
  and the Abrams (`/Game/Art/Local/Meshes/SM_Abrams_Hull` and `SM_Abrams_Turret`) beside the house. Capture with
  `EditorAppToolset.CaptureViewport`.
- Shared checkout: another session may be using the editor or Blender. Never close an editor or Blender you did not
  start, and never save outside `/Game/Art/Candidates/Village/`.

## Materials and textures

- The delivery's textures are PNGs under `round<n>/textures/` (final: `final/textures/`), at most 2048², from CC0
  sources only. The surfaces of `ue.ps1 -Task surfaces` are prepared at 2K with a packed ORM in `Saved/Surfaces/<Name>/`
  (the ambientCG sources are in `Tools/Editor/free_assets.json`):
  - roofs: `RoofingTiles006`, `RoofingTiles013A`; barn cladding: `Planks023A`, `Planks037A`;
  - brick: `Bricks085` (red-brown, the main brick), `Bricks097` (old, for the church, plinths and damage);
  - render: `Plaster001` (clean), `Plaster006` (weathered, for damage);
  - concrete: `Concrete031` (bridge, plinths), `Concrete036` (weathered, breaks and rubble).
- The git-ignored packs in `Content/` (AccuCities, ProceduralBuildingGenerator, SM_Concrete_07, SM_Tiles_02, the
  `/Game/Surfaces` instances) may serve the preview and as reference, never as delivered textures. AccuCities' London
  photogrammetry is a good realism reference for brick and roofs at RTS distance.
- At most 6 slots per mesh (the greybox damaged state has 7: fold Char into the others, e.g. by a baked or vertex-colour
  scorch). Name the slots; Claude binds them in setup.

## Evidence every round

Unreal captures in the preview level, labelled as such, at the 12 m, 150 m and 450 m RTS views (60° across, the RTS
pitch) and a three-quarter close-up, every state side by side with the Abrams; from round 2 on also the master against
the game mesh and the pair just inside and outside 260 m (`MODEL_PERFORMANCE.md` rules 7 and 8). The manifest gives LOD0
and LOD1 triangles from the re-imported FBX, slots, texture sizes and the garrison openings (kept, moved, added).

## Codex starter

> Use `$world-divide-building-production` for River Crossing's village set, issue #431. Read
> `Art/Shared/village/issue431/BRIEF.md` and `STATE.md`, then do the round `STATE.md` names as next, and stop at its gate
> with a `ROUND_<n>.md`. Do not commit or push.
