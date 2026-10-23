# #392 — five kit lighting fixtures: modelling brief for Codex

Issue: https://github.com/Studio-Lek-River/World-Divide/issues/392 (M7.00-07). Read first: `AGENTS.md`,
`.agents/skills/world-divide-building-production/SKILL.md`, `docs/art/BUILDING_KIT.md` (Piece schema, Visual quality,
Triangle budgets), `docs/art/MODEL_PERFORMANCE.md`, and the approved precedent `Art/Shared/kit/lighting/04_StripLight/`
(STATE.md, CLAUDE_HANDOFF.md, `Tools/Art/kit/strip_light*.py`).

## Your job, and only your job

**You make the 3D models and make them ready for Unreal Engine. Claude does all coding and integration** (catalogue,
setup_testmap.py, import, materials in Unreal, C++, tests, commits). Producer decision, 2026-10-23.

You may write ONLY:
- the five piece folders `Art/Shared/kit/lighting/{01_WallLamp,02_CageWallLamp,03_EntranceLight,05_DirectionalSpot,06_RoofFloodLight}/`
- their Blender modules and helper scripts in `Tools/Art/kit/` (`wall_lamp.py`, `cage_wall_lamp.py`, `entrance_light.py`,
  `directional_spot.py`, `roof_flood_light.py`, plus `<module>_materials.py` / `_render.py` helpers if you need them)
- `Art/Shared/kit/lighting/_work_392/ROUND_NOTES.md` (your round report, append a section per round)

Never touch: `kit_catalogue.json`, `build/kit_manifest.json`, `catalogue_392.json`, `run_pieces.py`, `Tools/Art/kit_blender.py`,
`Source/`, `Content/`, `Tools/Editor/`, any other piece or building. No Unreal editor work. No git commit/push.

## The contract with the generator (must hold, it is checked)

- Each module has `build(piece)` taking its catalogue entry (`_work_392/catalogue_392.json`) and returning
  `(lod0_object, lod1_object)`, as `Tools/Art/kit/mast_beacon_single.py` and the docstring of `Tools/Art/kit_blender.py`
  describe. It runs after `read_factory_settings(use_empty=True)`, so it creates everything it needs, deterministically.
- Blender metres, the generator exports centimetres, +X forward, Z up. **Fixture frame:** origin at the mounting plate's
  back face centre (06: the base plate's underside centre), **+X out of the wall** (the way the lamp faces), Z up.
- Exactly two material slots in this order: `Housing`, `Lens`. Everything that glows (globe, diffuser, LED panels, the
  spot's lens) is drawn with `Lens`, as its own island(s). Nothing else uses `Lens`.
- Budgets (hard, the runner fails over them): 01/02/03 class M ≤1,200 LOD0 / ≤400 LOD1; 05/06 class L ≤2,500 / ≤800.
  LOD1 must show no visible change at 260 m: same silhouette, same lens shape.
- UV0: non-overlapping, housing at ~512 px/m against a 2048 trim, lens up to 4× that against a 1024 lens set.
  UV1 `FinishBake` for a baked finish normal, as 04 did (optional but expected for close-up quality).
- Per piece textures at most 2048² for Housing and 1024² for Lens (BaseColor sRGB, Normal DirectX, ORM linear =
  occlusion/roughness/metallic). No 4K. Put them in `<piece>/textures/`.
- Build and check with:
  `"D:\Program Files\Blender Foundation\Blender 5.1\blender.exe" -b --python Art/Shared/kit/lighting/_work_392/run_pieces.py -- 01_WallLamp`
  (no args after `--` builds all five). It writes the FBXs into each folder and records counts, bounds and lens
  bounds in `_work_392/manifest_392.json`. Every round ends with all five building cleanly.
- Real geometry for brackets, plates, cages, yokes, cable glands and fins on the L pieces; detail under ~1 cm on M pieces
  goes into the normal map. Bevel the silhouette edges that catch light; no microscopic geometry.

## The pieces

Each folder has `reference/producer_reference.png`: the producer's own image. **The image wins over the older spec text**
(producer, 2026-10-23). Match its silhouette, proportions, construction and finish; no brands, no flag decals.

| Piece | Reference shows | Real size (cm, approx.) | Notes |
|---|---|---|---|
| 01 WallLamp | Chamfered-top rectangular wall pack, separate back plate, Torx screw on the side, recessed ribbed frosted diffuser on the underside, screwed bezel | 30 × 13 deep × 12 tall | Light: spot straight down out of the lens (catalogue rotation pitch −90). Dark weathered cast metal |
| 02 CageWallLamp | Oval bulkhead: rectangular back plate (4 screws), oval cast frame, frosted glass globe, cast cage (one vertical + two horizontal bars), screw bosses | plate 14 × 26, globe ~11 × 22, depth ~12 | Point light inside the globe. Rust and wear on the cage and plate edges |
| 03 EntranceLight | Rectangular box head on a vertical wall bracket plate, cantilevered over a door, flat opal diffuser underneath, screwed bezel | head 35 × 20 × 10, bracket plate 12 × 25 | Spot straight down. Matt dark steel, slight drip edge |
| 05 DirectionalSpot | Cylindrical spot can with a slightly flared mouth, reflector/honeycomb lens, on a slotted swivel arm with knuckle bolts, vertical wall plate with two bolts, braided cable with gland into the can | can Ø 18 × 24, arm ~25, plate 10 × 30 | Model the head at rest aimed 30° below horizontal along +X (catalogue pitch −30). One static mesh; per-mount aiming is done by the mount transform, no separate moving part |
| 06 RoofFloodLight | Two rectangular LED flood heads (three LED panels each behind glass, screwed frame, rear cooling fins) on a U-yoke bar, short square post, base plate with four bolts, cables | each head 30 × 10 × 38, yoke ~80 wide, total height ~75, base 40 × 25 | Stands on a roof: mount point `Roof`, origin at base underside, heads face +X tilted 15° down. Both heads' panels are `Lens` |

Materials (all pieces): dark weathered cast aluminium / powder coat as in the references, edge wear, light dust and
restrained rust where the reference shows it; lens/diffuser frosted or prismatic as shown. Keep it believable at 12 m
close-up and readable at 150 m / 450 m RTS distance (the lens should read as a bright pixel cluster at night).

## Deliverables per piece (in its folder)

- `source/<Piece>.blend` (clean model, packed or relative textures, editable material graph)
- `SM_Kit_<Piece>.fbx`, `SM_Kit_<Piece>_LOD1.fbx` (written by run_pieces.py)
- `textures/` as above; `material_recipe.json` with the PBR values and emissive strength
- `review/`: Blender Cycles renders matching the reference angle, day and night, lit and unlit, plus a LOD0/LOD1
  side-by-side at small size (RTS read)
- `evidence/verification.json`: triangles per LOD, slots, bounds, lens bounds, texture sizes, UV checks
- `STATE.md` (short current state) and `CLAUDE_HANDOFF.md` (what Claude imports: files, slot order, texture
  settings, UV1 use, emissive values, light origin and direction as you modelled them, anything unresolved)

## Rounds

Rounds 1–3 are you (gpt-6.1-sol); Claude reviews between rounds and sends notes. Round 4 is a separate final check by
another model. At the end of each round, append to `_work_392/ROUND_NOTES.md`: what changed per piece, counts, and open
issues. Do not claim producer approval; the producer approves each piece after round 4.
