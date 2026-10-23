# 05_DirectionalSpot — independent final check, Round 4

2026-10-23; affected entries refreshed in Round 4b. Scope: five assigned fixture deliveries and their Blender helpers only. Producer references govern appearance. **Producer approval pending.**

| Checklist item | Status | Finding / action |
|---|---|---|
| 1. Reference fidelity | fixed | Preserved the mechanical construction. Corrected the backward-facing open lens, verified its outward normal through FBX reimport, rebuilt both LOD atlas/normal bindings, refined cast finish and restored centre-weighted honeycomb emission. Reference-aligned day/night and actual-distance images inspected. |
| 2. Generator contract | pass | Two factory-reset builds have identical geometry/UV/shading digests; saved sources match. The untouched Tools/Art/kit_blender.py built all five twice with matching manifest digests. Slots are Housing then Lens; optical faces alone use Lens. |
| 3. Budgets | pass | 2466 / 718 triangles against 2500 / 800. Headroom 34 / 82. |
| 4. LOD1 continuity | fixed | Repaired LOD texture addressing and baked both LODs. Maximum projected fixture-outline difference: 0.0408 px at 150 m / 0.0235 px at 260 m. Lens-outline difference: 0.0299 / 0.0172 px. No discernible silhouette change at native 150/260 m. Small shading/antialiasing differences remain quantified; no bit-identical or runtime claim. |
| 5. Fixture frame and dimensions | fixed | Can approximately 24.6 cm axially, with a 20.4 cm flared mouth; plate 10 × 30 cm. Nominal body dimensions follow the approximate 18 × 24 cm archetype. Origin and centimetre roundtrip passed. Direction [0.866025,0,-0.5], 3000 K. Corrected the backward-facing open lens; reimported lens normals now align with pitch -30.  |
| 6. UVs / normals / topology | fixed | Both LODs now occupy disjoint charts in shared UV0 and FinishBake atlases; zero positive-area overlaps, zero degenerate geometry/UV triangles. Unit normals, orthogonal tangents and positive closed-shell volumes passed. Invalid bevel-bake hemispheres repaired. Exactly one final DirectX normal per slot. |
| 7. Texture contract | pass | Seven current runtime maps, Housing 2048² / Lens 1024². Actual image sizes, sRGB/linear bindings, packed-byte equality, DirectX convention and recipe values checked. Documented roughness corrected to the actual 0.18 clamp. |
| 8. Finish / emission | fixed | Round 4b: continuous irregular rubbed edges with sparse wider chips replace the dashed grain-threshold pattern. Existing cast grain, pits and local corrosion are retained. Structured optical profiles and centre/edge falloff retained. Fixed misplaced centre-weighting code. Display tint [1,0.44,0.16], 3000 K; emission multiplier 6.  |
| 9. Evidence / handoff | fixed | Refreshed source, both runner FBXs, verification.json, day/night on/off and reference renders, enlarged LOD sheets and native 12/150/260/450 m pairs. Refreshed affected Round 4b entries, independent technical/distance receipts and artifact hashes. STATE and handoff supersede earlier claims. Producer approval and Unreal/gameplay acceptance are not claimed. |

## Measured distance evidence

| Distance | Changed pixels > 1/255 | Maximum display RGB delta | Source |
|---|---:|---:|---|
| 12 m | 1370 | 0.2353 | distance |
| 150 m | 14 | 0.1373 | distance |
| 260 m | 4 | 0.0353 | switch |
| 450 m | 2 | 0.0471 | distance |

1920 × 1080, 60° horizontal FOV, fixed elevated fixture-centre camera; the comparison forces each LOD at the same distance. Delta statistics include rendered shading and antialiasing, not just geometry. Enlarged sheets are diagnostic crops, not the native on-screen size. 

## Open items and limits

No remaining blocking Blender model/export defect identified. Producer appearance approval remains pending.

Opaque optical approximations and modest enlarged LOD simplification are documented. Unreal appearance, production light behavior and the gameplay LOD switch remain Claude’s integration checks, outside this task. No approved high-poly master or producer acceptance is inferred.

## Evidence

- [Reference day](../review/ref_match_day.png) / [night](../review/ref_match_night.png); [producer comparison](../review/reference_left_round04_right_night.png).
- [Enlarged LOD pair](../review/rts_LOD0_left_LOD1_right.png); [150 m crop](../review/astra_150m_LOD0_left_LOD1_right.png) / [260 m crop](../review/astra_260m_LOD0_left_LOD1_right.png).
- [Source and reimport verification](verification.json), [independent audit](astra_technical.json), [distance metrics](astra_distance_comparison.json), [final inventory](delivery.json).
