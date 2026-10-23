# #393 — five kit lighting pieces: modelling brief for Codex

Issue: https://github.com/Studio-Lek-River/World-Divide/issues/393 (M7.00-08). Read first: `AGENTS.md`,
`.agents/skills/world-divide-building-production/SKILL.md` and its `references/hq-lessons.md`, `docs/art/BUILDING_KIT.md`
(Piece schema, Visual quality, Triangle budgets, USA Lighting Kit, Appearance references), `docs/art/MODEL_PERFORMANCE.md`.

**Precedent, approved by the producer on 2026-10-23:** the five #392 pieces in `Art/Shared/kit/lighting/01_WallLamp` …
`06_RoofFloodLight` (their `STATE.md`, `CLAUDE_HANDOFF.md`, `evidence/astra_check.md`, `review/`), their modules in
`Tools/Art/kit/` (`wall_lamp.py` and the shared `wall_lamp_materials.py` `Builder`/`pair`, `wall_lamp_render.py`), and
`_work_392/ROUND_NOTES.md`, which records every review note those pieces needed. Reuse that toolchain, that finish
quality and that deliverable set. Do not repeat the mistakes the #392 round notes and `hq-lessons.md` record.

## Your job, and only your job

**You make the 3D models and make them ready for Unreal Engine. Claude does all coding and integration** (catalogue,
setup_testmap.py, import, Unreal materials, C++, tests, commits).

You may write ONLY:
- the five piece folders `Art/Shared/kit/lighting/{07_ServiceBayLight,08_WarningBeacon,09_IndicatorLED,12_MastBeaconSingle,13_MastBeaconDual}/`
  — in `12_MastBeaconSingle` never overwrite or delete the existing `SM_Kit_MastBeaconSingle.fbx` at the folder root;
  the runner writes 12's FBX into `12_MastBeaconSingle/candidate/`
- new modules in `Tools/Art/kit/`: `service_bay_light.py`, `warning_beacon.py`, `indicator_led.py`,
  `mast_beacon_single_393.py`, `mast_beacon_dual.py`, plus new helpers they need (e.g. `beacon_common.py`,
  `<module>_render.py`). You may import from `wall_lamp_materials.py` and the other #392 helpers but must not change them.
- `Art/Shared/kit/lighting/_work_393/ROUND_NOTES.md` (append a section per round)

Never touch: `Tools/Art/kit/mast_beacon_single.py` (the live 12 that the HQ builds from today), any #392 or 04 piece or
module, `kit_catalogue.json`, `build/kit_manifest.json`, `_work_393/catalogue_393.json`, `_work_393/run_pieces.py`,
`Tools/Art/kit_blender.py`, `Source/`, `Content/`, `Tools/Editor/`, `Config/`, any building. No Unreal editor work.
No git commit/push. Keep Blender/editor caches out of `Art/`.

## The contract with the runner (it is checked)

- Each module has `build(piece)` taking its entry from `_work_393/catalogue_393.json` and returning
  `(lod0_object, lod1_object)`; **class S pieces (09, 12, 13) return `(lod0, None)`** — one LOD, nothing disappears.
  It runs after `read_factory_settings(use_empty=True)`, so it creates everything it needs, deterministically.
- Blender metres, export centimetres, Z up, exactly as `Tools/Art/kit_blender.py` exports.
- Exactly two material slots in this order: `Housing`, `Lens`. Everything that glows is `Lens`, as its own island(s);
  nothing else uses `Lens`.
- Budgets (hard, the runner fails over them): 07 class M ≤1,200 LOD0 / ≤400 LOD1; 08 class L ≤2,500 / ≤800;
  09, 12, 13 class S ≤250 triangles, LOD0 only. LOD1 (07, 08) shows no visible change at 260 m.
- UV0 non-overlapping, housing ~512 px/m, lens up to 4× that; UV1 `FinishBake` with one baked DirectX finish normal per
  slot (UV0 tangent basis), as in #392. Textures per piece in `<piece>/textures/` named `T_<Piece>_<Slot>_<Map>.png`
  (BaseColor sRGB, Finish_Normal DirectX, ORM linear; the Lens also LightProfile), Housing ≤2048², Lens ≤1024². No 4K.
- Build and check:
  `"D:\Program Files\Blender Foundation\Blender 5.1\blender.exe" -b --python Art/Shared/kit/lighting/_work_393/run_pieces.py -- 08_WarningBeacon`
  (no names after `--` builds all five). It writes the FBXs and `_work_393/manifest_393.json`. Every round ends with all
  five building cleanly, twice, with matching digests.
- Real geometry for silhouette parts (domes, stems, flanges, base plates, bars, brackets, trim frames); detail under
  ~1 cm goes into the finish normal. Façade-style members that stand proud by more than ~1 cm stay geometry, not normal
  map. Bevel the silhouette edges that catch light; no microscopic geometry.
- For S pieces the 250-triangle budget is tight: spend it on the silhouette that reads at a mast tip (dome profile,
  flange, stem) and put bolts, ribs and the inner LED in the maps. If the reference cannot be read at all within 250,
  say so in the round notes with the count you need; do not exceed the budget.

## Fixture frames

- **Wall/ceiling pieces (07, 09):** origin at the centre of the mounting face, **+X out of the surface** (the way the
  light faces), Z up, as 01–05 and 04. 07 is mounted on a ceiling by the mount's pitch, so model it facing +X.
- **Standing pieces (08, 12, 13):** origin at the centre of the underside of the base, Z up, +X forward, as 06.

## The pieces

Each folder has `reference/producer_reference.png`: the producer's own image. **The image wins over the older spec text**
(producer, 2026-10-23). The producer's image labels use other numbers; ignore the labels, the folder decides. Match
silhouette, proportions, construction and finish; no brands, no flag decals, no text.

| Piece | Reference | Real size (cm, approx.) | Notes |
|---|---|---|---|
| 07 ServiceBayLight | **No producer image.** Spec only: recessed high-bay LED panel set flush in a service-bay ceiling, trim frame. `reference/style_reference_04.png` is the approved 04 strip light: match its family look (weathered dark/graphite metal, screwed end caps, ribbed opal diffuser) | 60 × 30, 10 deep (the box sits behind the ceiling plane; only the trim frame stands ~1.5 cm proud) | Steel trim frame with corner screws, white-enamel or graphite inner reflector tray, prismatic acrylic panel (`Lens`) facing +X. One Rect light through the panel, 4000 K. Must read as a clear lit rectangle from 150 m at night |
| 08 WarningBeacon | Image "07 Beacon Light – Rotating": cylindrical Fresnel dome over a dark cast collar with two screwed bands, on a short round stem, flange and square base plate with four hex bolts; inner bulb and a curved mirror reflector behind it | dome Ø 14 × 14 tall, overall ~Ø 18 × 32 tall, base plate 18 × 18 | **Amber**, not the image's red-orange (BUILDING_KIT: amber 2200 K). Dome = `Lens`, as one island; inner bulb and mirror are drawn into the lens maps (or as a simple inner `Lens` shape), not separate moving parts — the rotation sweep is done in the material. Put the mirror's facing direction along +X at rest and note in the handoff how the lens UVs run around the dome (U around the circumference, starting at +X) so Claude can sweep it |
| 09 IndicatorLED | Image "08 Indicator Light – Small": a small chamfered rectangular wall box, weathered steel, a slotted round plug on one end, two Torx screws, recessed bezel with a ribbed diffuser on the underside-front | ~24 long × 9 deep × 9 tall | Colour is set per mount: the diffuser's BaseColor and LightProfile are **neutral white/frosted**, no blue baked in; the review renders show it tinted blue, red and green by emission tint only. Emissive only, no real light |
| 12 MastBeaconSingle | Left half: red obstruction dome with an inner vertical LED stack, a cast collar with screwed flange, on a short stem | keep the current envelope: about 44 wide × 56 tall at scale 1 (the HQ mounts it on its mast tips at scale 1.75) | **Tip-mount version:** dome, collar, flange and short stem standing on its base, no side arm or pole clamp (the mast is the building's). Red glass dome = `Lens`; dark between flashes. Emissive only |
| 13 MastBeaconDual | Right half: two red obstruction domes on a bolted horizontal bar bracket, each on its own collar | about 80 wide × 20 deep × 56 tall at the same scale as 12, bar centred over the origin | Same dome/collar as 12 (share the code). Lens UVs: the −Y dome's lens island at U < 0.5, the +Y dome's at U ≥ 0.5, so the material can blink them alternately. Emissive only |

Materials: dark weathered cast metal / galvanised steel as in the references, silver edge wear, light dust, rust only
locally (fasteners, low edges, short streaks), as the approved #392 Round 3/4 finish. Domes read as coloured glass by day
(not glowing) and as a bright pixel cluster at 150/450 m at night.

## Deliverables per piece (in its folder)

As #392: `source/<Piece>.blend` (clean, packed textures, editable material graph); the FBX(s) written by the runner;
`textures/`; `material_recipe.json` (PBR values, emission strength and tint); `review/` with reference-angle Cycles
renders day and night, lit and unlit, `reference_left_roundNN_right_{day,night}.png`, the 12/150/450 m distance shots
(LOD0/LOD1 pairs for 07 and 08), and for 08, 12, 13 a **LowPower** night view (lens at 30 %); `review/README.md`;
`evidence/verification.json` (triangles, slots, bounds, lens bounds, texture sizes, UV checks, digests); `STATE.md` and
`CLAUDE_HANDOFF.md` (files, slot order, texture settings, UV1 use, emissive values and tint, light origin/direction,
the lens UV layout of 08 and 13, anything unresolved).

## Rounds

Rounds 1–3 are gpt-6.1-sol; Claude reviews between rounds and sends notes. Round 4 is a separate realism and technical
check on gpt-6-astra. At the end of each round append to `_work_393/ROUND_NOTES.md`: what changed per piece, counts and
headroom, and open issues, with links to the review images. Do not claim producer approval; the producer approves each
piece after round 4.
