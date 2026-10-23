# Issue 403 - Round 4 review

All images are Blender Cycles renders, not Unreal or gameplay evidence.

- `final_neutral_rts.png`: three-quarter camera matched to final HQ RTS orientation (55 degree depression), framed at 105 m.
- `final_neutral_reference.png`: lower reference presentation, front three-quarter, 31 degrees.
- `night_rts.png`, `night_reference.png`: same cameras in low cool illumination, constant ember material.
- `rts_12m.png`: closest camera aimed at the broken garage portal; the full building cannot fit a 12 m arm.
- `rts_150m.png`, `rts_450m.png`: 60 degree horizontal lens, 16:9, true arm distances.
- `rts_150m_night.png`: added true 150 m night review, same constant ember shader.
- `lod0_260m.png`, `lod1_260m.png`: identical camera and lighting, manual LOD selection at the switch.
- `lod0_249p6m.png`, `lod1_270p4m.png`: 4 percent inside/outside switch.
- `top_footprint.png`: orange 38 x 56 m boundary, preview curve excluded from export.
- `intact_vs_rubble.png`: original shell FBX (left) and rubble (right), same camera and neutral light. The intact shell uses a neutral override to compare massing, and excludes separate masts/moving equipment.

Exact camera transforms are in `cameras.json`. The intact FBX is read only and is not stored in the rubble blend.
The saved source is a clean two-LOD scene with editable texture graphs and relative textures, without preview terrain.
Round 4 retains the round-3 fill and mast; planar ear-clipped caps remove fan pinching, broader stepped wall breaks remove spike fans, independent corner cuts vary slabs, and stronger interior soot preserves pale exterior dust. Ember intensity remains 7. The 150 m night view is rts_150m_night.png.
UV0 remains unique per slot; UV1 SurfaceDetail intentionally tiles the Debris normal and linear DetailColor multiplier at 0.5 m. No FinishBake is claimed. See material_recipe.json for the exact Unreal graph.
Procedural authored model is the game mesh; no separate dense approved master or finish-normal bake exists.

Round 4 contact sheet: `round4_contact_sheet.png`. Historical before images are in `../evidence/round3_baseline/`. Verdict and independent verification: `../ROUND_NOTES.md`, `../evidence/round4_independent_check.json`.
