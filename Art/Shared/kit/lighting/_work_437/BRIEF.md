# #437 — rebake kit 04 StripLight's maps to the kit's finish contract: brief for Codex

Issue: https://github.com/Studio-Lek-River/World-Divide/issues/437 (M7.00-21). Read first: `AGENTS.md`,
`.agents/skills/world-divide-building-production/SKILL.md` and its `references/modelling-lessons.md` /
`references/hq-lessons.md`, `docs/art/BUILDING_KIT.md` (Visual quality: the `finish` contract), and 04's own
`Art/Shared/kit/lighting/04_StripLight/STATE.md`, `CLAUDE_HANDOFF.md`, `material_recipe.json`.

**This is a rebake, not a remodel.** The producer approved 04's shape and final finish on 2026-10-22 ("Yes is prima zo").
Keep that look: same graphite powder-coat housing, same opal lens, same finish normal, same glow distribution. Only the way
the maps are laid out changes, so Unreal's shared kit masters can draw them.

## Why

Unreal draws a kit piece's own finish with two shared masters, `M_Kit_Housing` and `M_Kit_Lens`. They sample, per slot:

- `BaseColor` (sRGB) and `ORM` (linear; R occlusion, G roughness, B metallic) on **UV0, untiled** (scale 1, no offset);
- `Finish_Normal` (DirectX, linear) on **UV1** (`FinishBake`), tangent basis on UV0;
- for the Lens also `LightProfile` (linear; red = glow mask) on **UV0, untiled**, with clamp-free wrap sampling.

Setup imports them by name from the piece's `textures/` as `T_StripLight_<Slot>_<Map>.png`. The other kit lamps
(01–03, 05–09, 12, 13) already deliver exactly this. 04 does not:

- `material_recipe.json`: housing normal tiled 8×, housing ORM 4×, lens normal and ORM 10×, and the LightProfile sampled
  through scale (1/1.106, 1/0.146) and offset (0.5 − 0.5/1.106, 0.5 − 0.5/0.146).
- One shared `T_StripLight_Finish_Normal.png` for both slots instead of one per slot.
- Lens UV0 is a planar projection at 1 UV/m, so the 1.106 m long lens runs from U ≈ −0.053 to 1.053: outside 0–1, so an
  untiled LightProfile cannot be drawn on it as it is.

## Your job, and only your job

**You rebake the maps and make the source ready. Claude does all integration** (catalogue, `Tools/Art/kit_blender.py`
export, setup, Unreal, tests, commits).

You may write ONLY:
- `Art/Shared/kit/lighting/04_StripLight/` (source, textures, recipe, review, evidence, STATE.md, CLAUDE_HANDOFF.md,
  archive) — **except** the FBXs at its root, which `kit_blender.py` writes;
- `Tools/Art/kit/strip_light*.py` and a new `Tools/Art/kit/strip_light_rebake.py`;
- `Art/Shared/kit/lighting/_work_437/ROUND_NOTES.md` (append a section per round).

Never touch: any other piece or module (`wall_lamp_materials.py` etc. may be imported, not changed),
`kit_catalogue.json`, `build/kit_manifest.json`, `Tools/Art/kit_blender.py`, `Source/`, `Content/`, `Tools/Editor/`,
`Config/`, any building. No Unreal editor work. No git commit/push. Keep Blender caches and `__pycache__` out of `Art/`.

## What to deliver

1. **Textures in `textures/`, exactly these seven** (the old tiled set moves to `archive/material_pass02/`, with the
   current `material_recipe.json`):
   - `T_StripLight_Housing_BaseColor.png`, `T_StripLight_Housing_ORM.png`, `T_StripLight_Housing_Finish_Normal.png` — 2048²
   - `T_StripLight_Lens_BaseColor.png`, `T_StripLight_Lens_ORM.png`, `T_StripLight_Lens_Finish_Normal.png`,
     `T_StripLight_Lens_LightProfile.png` — 1024² (the lens finish normal may stay 2048² if halving it visibly softens the
     diffuser's edge roundover; say which in the handoff)
   - No `Normal` (UV0 micro-normal) maps: the handoff already says the finish normal includes that microstructure and the
     two must not stack. No `NormalGL` in `textures/` (keep Blender's GL copy in `source/` or derive it in the graph).
2. **BaseColor and ORM, untiled on UV0.** Resample the current maps into UV0 space at scale 1 so each texel shows what the
   approved material showed there (`new(u, v) = old(frac(k·u), frac(k·v))` with the recipe's k; filter, do not
   point-sample). The housing's UV0 sits inside 0–1 already (0.25 UV/m); keep it.
3. **Lens UV0 into 0–1.** Rescale the Lens slot's UV0 islands, axis-aligned about (0.5, 0.5), so every lens UV lies inside
   0–1 with a few texels of margin. A uniform or an axis-aligned non-uniform scale is fine; recompute the lens
   BaseColor/ORM density for it. Then bake the LightProfile into the new UV0 so the glow looks exactly as approved: broad
   even middle, soft perimeter, darkened ends, no LED dots, no banding (write it 16-bit linear as `lighting_393_finish.py`'s
   `data16` does). Nothing else about UV0 changes, and positions, faces, slots, normals, UV1 and transforms do not change
   at all. Show that the tangent directions on UV0 are unchanged, so the UV1 finish normal stays valid.
4. **Finish normal per slot.** Both slots share UV1, so each slot's `Finish_Normal` is the existing bake with the other
   slot's islands removed (or the same image if cleaner); it stays DirectX, UV0 tangent basis, sampled on UV1. Do not
   rebake it unless you must; if you do, it must match the approved one.
5. **`source/StripLight.blend`** saved with the new UV0 and a material graph that samples the new maps the way Unreal
   will (UV0 scale 1, finish normal on UV1, LightProfile on UV0, emission tint (1, 0.779, 0.615), strength 3.2), textures
   packed. `kit_blender.py` builds 04's FBXs from this file through `strip_light.py`'s `build(piece)`, so the object must
   keep its name `SM_Kit_StripLight`, its two slots `Housing`, `Lens` in that order, and both UV layers.
6. **`material_recipe.json`** rewritten for the new maps: every UV scale 1, no profile scale/offset, sizes and sha256 of all
   seven maps, emission tint and strength, which UV each map uses.
7. **Proof the look is kept** in `review/rebake/`: Cycles renders of the new graph against the approved `pbr_*` renders at
   the same cameras and lighting (day on, day off, night on, night off), side by side as
   `before_after_{day_on,day_off,night_on,night_off}.png`, plus a close-up of a lens end (end attenuation) and of a housing
   edge (finish normal). And `evidence/rebake_verification.json`: geometry digest without UV0 before/after (equal), UV1
   digest before/after (equal), UV0 digest of the housing loops before/after (equal), lens UV0 min/max (inside 0–1), the
   seven maps' sizes and hashes, and a per-render mean absolute difference before vs after.
8. **`STATE.md` and `CLAUDE_HANDOFF.md`**: a new top section for this rebake (what changed, the seven maps, UVs, the
   catalogue values Claude needs: Housing none, Lens `EmissionTint` [1, 0.779, 0.615] and `Intensity` 3.2), and the old
   sections marked historical. Keep both short.

## Done means

The seven maps exist with the right names and sizes; the blend loads and `strip_light.py`'s `build(piece)` returns it; all
digests that must be equal are equal; the before/after renders show no visible difference in housing colour, roughness,
edge roundover, lens brightness or glow shape. If any of those cannot be met, stop and write why in the round notes. Do not
claim producer approval or Unreal acceptance.
