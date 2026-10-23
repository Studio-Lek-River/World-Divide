# USA HQ rubble - Round 4

READY FOR PRODUCER REVIEW. Independent Round 4 model check complete; no producer approval claimed.

SM_HQ_Rubble: 41,862 triangles, SM_HQ_Rubble_LOD1: 14,234 triangles. Three slots: Debris, Scorch, Embers.

Authority: BRIEF.md, intact game/SM_HQ_Shell.fbx, usa_hq.json and final_neutral_rts/reference.
Source: source/HQ_Rubble.blend; generator: Tools/Art/usa_hq_rubble.py.
Technical checks: evidence/verification.json (independent FBX reimport, UV intersection, tangents, winding, bounds), evidence/hashes.json, evidence/round4_independent_check.json (separate fresh-process audit).
Review: review/README.md. Round 4 fixes pinched slab caps, narrow wall teeth, repeated outlines and weak soot while retaining round-3 collapse, mast and ember intensity; visual acceptance remains Claude/producer review. UV1 SurfaceDetail now drives tiled Debris normal and DetailColor; consult material_recipe.json before importing. No Unreal editor or code work performed.

Next: producer visual decision, then Claude integration if accepted. Unreal import, materials, distance switch, runtime lighting, disappearance and gameplay integration belong to Claude and remain untested.

Rebuild (overwrites only this rubble delivery, preserves BRIEF.md and ROUND_NOTES.md):
`"D:\Program Files\Blender Foundation\Blender 5.1\blender.exe" -b --python Tools/Art/usa_hq_rubble.py`

No approval gates have been passed. No external or paid assets used.

Final Round 4 audit: two clean runs reproduce both LOD geometry hashes and all maps/recipe bytes. All 15 file receipts and 14 review images pass evidence/final_audit.json; the separate fresh-process audit is evidence/round4_independent_check.json. See ROUND_NOTES.md for the final verdict and scope. No producer approval claimed.
