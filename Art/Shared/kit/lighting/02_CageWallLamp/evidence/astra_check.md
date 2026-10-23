# 02_CageWallLamp — independent final check, Round 4

2026-10-23; affected entries refreshed in Round 4b. Scope: five assigned fixture deliveries and their Blender helpers only. Producer references govern appearance. **Producer approval pending.**

| Checklist item | Status | Finding / action |
|---|---|---|
| 1. Reference fidelity | fixed | Round 4b: all 13 corresponding plate charts share the same LOD0/LOD1 UV0 and FinishBake regions. Restored the complete four-ring LOD1 frame, including its rear lip, and the rear globe cap; the missing rear rim caused the orange light spill on the plate. The LOD1 frame has 14 capsule vertices and the globe 20, with a two-triangle front closure behind the vertical strap. The top retains a rounded outline at 398 triangles. LOD0 construction, cage straps, frost and local corrosion are retained. Reference-aligned day/night and actual-distance images inspected. |
| 2. Generator contract | pass | Two factory-reset builds have identical geometry/UV/shading digests; saved sources match. The untouched Tools/Art/kit_blender.py built all five twice with matching manifest digests. Slots are Housing then Lens; optical faces alone use Lens. |
| 3. Budgets | pass | 924 / 398 triangles against 1200 / 400. Headroom 276 / 2. |
| 4. LOD1 continuity | fixed | Repaired LOD texture addressing and baked both LODs. Maximum projected fixture-outline difference: 0.0381 px at 150 m / 0.0220 px at 260 m. Lens-outline difference: 0.0076 / 0.0044 px. No discernible silhouette change at native 150/260 m. Small shading/antialiasing differences remain quantified; no bit-identical or runtime claim. |
| 5. Fixture frame and dimensions | pass | Mounting plate 14 × 26 cm; approximately 12.2 cm projection, with a roughly 10 × 22 cm globe. Origin and centimetre roundtrip passed. Direction omnidirectional, 3000 K. Lens orientation matches the prescribed light.  |
| 6. UVs / normals / topology | fixed | The 13 corresponding plate charts share exact UV0 and FinishBake regions across LODs (vertex-by-vertex verification). Each individual LOD has zero positive-area UV overlaps; combined occupancy excludes only verified aliases. Zero degenerate geometry/UV triangles. Unit normals, orthogonal tangents and positive closed-shell volumes passed. Invalid bevel-bake hemispheres repaired. Exactly one final DirectX normal per slot. |
| 7. Texture contract | pass | Seven current runtime maps, Housing 2048² / Lens 1024². Actual image sizes, sRGB/linear bindings, packed-byte equality, DirectX convention and recipe values checked. Documented roughness corrected to the actual 0.18 clamp. |
| 8. Finish / emission | fixed | Narrower worn edges, finer pits and local corrosion; 02 remains the most weathered. Structured optical profiles and centre/edge falloff retained. Fixed misplaced centre-weighting code. Display tint [1,0.44,0.16], 3000 K; emission multiplier 7.  |
| 9. Evidence / handoff | fixed | Refreshed source, both runner FBXs, verification.json, day/night on/off and reference renders, enlarged LOD sheets and native 12/150/260/450 m pairs. Refreshed affected Round 4b entries, independent technical/distance receipts and artifact hashes. STATE and handoff supersede earlier claims. Producer approval and Unreal/gameplay acceptance are not claimed. |

## Measured distance evidence

| Distance | Changed pixels > 1/255 | Maximum display RGB delta | Source |
|---|---:|---:|---|
| 12 m | 423 | 0.2627 | distance |
| 150 m | 5 | 0.0784 | distance |
| 260 m | 3 | 0.0235 | switch |
| 450 m | 9 | 0.0392 | distance |

1920 × 1080, 60° horizontal FOV, fixed elevated fixture-centre camera; the comparison forces each LOD at the same distance. Delta statistics include rendered shading and antialiasing, not just geometry. Enlarged sheets are diagnostic crops, not the native on-screen size. 

## Open items and limits

No remaining blocking Blender model/export defect identified. Producer appearance approval remains pending.

Opaque optical approximations and modest enlarged LOD simplification are documented. Unreal appearance, production light behavior and the gameplay LOD switch remain Claude’s integration checks, outside this task. No approved high-poly master or producer acceptance is inferred.

## Evidence

- [Reference day](../review/ref_match_day.png) / [night](../review/ref_match_night.png); [producer comparison](../review/reference_left_round04_right_night.png).
- [Enlarged LOD pair](../review/rts_LOD0_left_LOD1_right.png); [150 m crop](../review/astra_150m_LOD0_left_LOD1_right.png) / [260 m crop](../review/astra_260m_LOD0_left_LOD1_right.png).
- [Source and reimport verification](verification.json), [independent audit](astra_technical.json), [distance metrics](astra_distance_comparison.json), [final inventory](delivery.json).
