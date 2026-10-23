# #403 — rubble of the destroyed USA HQ: modelling brief for Codex

Issue: https://github.com/Studio-Lek-River/World-Divide/issues/403 (M1-B21). Read first: `AGENTS.md`,
`.agents/skills/world-divide-building-production/SKILL.md`, `docs/art/MODEL_PERFORMANCE.md`,
`docs/art/usa_hq/USA_HQ_SPEC.md` (Presentation state), and the HQ delivery this rubble belongs to:
`Art/USA/buildings/usa_hq/` (`usa_hq.json`, `game/SM_HQ_Shell.fbx`, `reference/`, `renders/`).

## Your job, and only your job

**You make the 3D model and make it ready for Unreal Engine. Claude does all Unreal and code work** (import, materials
in Unreal, `setup_testmap.py`, data assets, C++, tests, commits). Producer decision, 2026-10-23.

You may write ONLY:
- `Art/USA/buildings/usa_hq/rubble/**` (except this BRIEF.md)
- Blender-side generator/helper scripts `Tools/Art/usa_hq_rubble*.py`
- `Art/USA/buildings/usa_hq/rubble/ROUND_NOTES.md` (your round report, append a section per round)

Never touch: `Source/`, `Content/`, `Config/`, `Tools/Editor/`, `usa_hq.json` or any other file of the HQ delivery,
other buildings, the shared kit. No Unreal editor work. No git commit/push.

## How the game shows it

When the HQ is destroyed, its own meshes squash down and darken over 3 s. Then they are hidden and this rubble mesh is
shown in their place for 60 s, after which it sinks straight down into the ground over 5 s. The rubble has no
collision and blocks nothing: units drive through it and a new HQ can be built on top of it. It is drawn at the HQ's
transform, scale 1, with the shell's origin. So it must look like **this HQ**, fallen, not generic debris.

## Look

The USA HQ after a heavy destruction, read against the reference and `renders/final_neutral_*.png`:
- The plan of the HQ stays readable from the RTS camera: the tall rear block (x < −0.75 m), the low front wings and the
  garage bay on the +X / −Y side (`usa_hq.json` `collision_boxes`).
- Broken wall stubs along the outline and at the corners, jagged tops, 1–4 m, a few stubs up to ~6 m at the rear block.
- Collapsed roof slabs lying tilted on mounds of crushed concrete; exposed rebar and steel beams.
- The communication mast (it stood on the rear roof at about x −9 m, y 0; see the `MastCore` part in `usa_hq.json`) lying fallen across the roof debris; the radar
  and dish heads broken off nearby. The garage portal frame half-standing.
- Scattered roof equipment (AC units, ducts) crushed among the slabs.
- Soot and scorch: blackened concrete around the burn centres, ash, darker toward the inside; restrained, not a
  black blob. Pale concrete dust on the outside so it reads in daylight.
- Night: a few faint smouldering ember patches in the deepest debris so the ruin reads as a pale-dark shape with warm
  specks at night. Faint, not fire.

No team colours, no flags, no national markings, no text.

## The contract with Unreal (must hold, checked)

- **Frame:** the same as `game/SM_HQ_Shell.fbx`. Blender metres, FBX export in centimetres, +X forward, Z up, origin at
  the HQ's ground centre. Shell bounds are x −19..19 m, y −28..28 m, z 0..13.4 m.
- **Footprint:** every vertex within x −19..19 m and y −28..28 m; nothing below z = −0.2 m (the bottom may sink a little
  into the ground so nothing floats); nothing above 8 m except at most the end of a leaning mast stub.
- **One static mesh, two LODs:** `SM_HQ_Rubble.fbx` (LOD0) and `SM_HQ_Rubble_LOD1.fbx`, same frame. No separate
  moving parts, no skeleton, no Nanite.
- **Budget (hard):** LOD0 ≤ 60,000 triangles, LOD1 ≤ 15,000. LOD1 switches in at 260 m and must show no visible change
  there: same silhouette, same slots, same textures.
- **Material slots, exactly three, in this order:** `Debris` (concrete, plaster, steel, rebar, equipment),
  `Scorch` (charred/soot surfaces as their own islands where that is cleaner than painting it into Debris),
  `Embers` (only the smoulder islands; emissive). If you find `Scorch` unnecessary, say so in the handoff rather than
  leaving an empty slot.
- **UVs:** UV0 non-overlapping per slot (tiling allowed only if declared in `material_recipe.json`); a UV1
  `FinishBake` channel if you bake a finish normal. No degenerate faces, no flipped normals, clean tangents.
- **Textures** in `rubble/textures/`: at most 2048² per slot (Embers may be 512²). BaseColor sRGB, Normal DirectX,
  ORM linear (R occlusion, G roughness, B metallic), Embers an emissive mask. No 4K.
- **Emissive:** give the ember emissive colour and strength in `material_recipe.json` in Unreal terms (linear colour,
  intensity in the ~1–10 range for a faint glow). It must be a constant; the game turns every lamp/window channel of
  the ruin off, so do not rely on them.

## Deliverables (in `Art/USA/buildings/usa_hq/rubble/`)

- `source/HQ_Rubble.blend` (clean model, packed or relative textures, editable material graph)
- `SM_HQ_Rubble.fbx`, `SM_HQ_Rubble_LOD1.fbx`
- `textures/`, `material_recipe.json` (PBR values, texture names and import settings, emissive)
- `review/`: Blender Cycles renders: matched three-quarter view as `renders/final_neutral_rts.png` and as the
  reference angle, day and night; the 12 m, 150 m and 450 m RTS views; the 260 m LOD0/LOD1 pair; a top view with the
  HQ's 38 × 56 m footprint outlined; a side-by-side of the intact HQ (shell FBX) and the rubble from the same camera.
  `review/README.md` indexes them.
- `evidence/verification.json`: triangles per LOD, slots and their order, bounds (cm), texture sizes, UV checks,
  degenerate/flipped-face counts, and an independent FBX reimport check (counts, slots, cm round trip, origin).
- `STATE.md` (short current state) and `CLAUDE_HANDOFF.md` (what Claude imports: files and hashes, slot order,
  texture import settings, UV1 use, emissive values, frame, anything unresolved).
- A deterministic generator `Tools/Art/usa_hq_rubble.py`, run headless:
  `"D:\Program Files\Blender Foundation\Blender 5.1\blender.exe" -b --python Tools/Art/usa_hq_rubble.py`
  It rebuilds the blend, both FBXs and the evidence; two runs give the same geometry. Every round ends with a clean run.

## Rounds

Rounds 1–3 are you (gpt-6.1-sol); Claude reviews between rounds and sends notes. Round 4 is a separate final check by
another model (gpt-6-astra). At the end of each round, append to `ROUND_NOTES.md`: what changed, counts, open issues.
Do not claim producer approval; the producer approves after round 4.
