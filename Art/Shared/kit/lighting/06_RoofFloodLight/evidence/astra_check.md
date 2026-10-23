# 06_RoofFloodLight — independent final check, Round 4

2026-10-23; affected entries refreshed in Round 4b. Scope: five assigned fixture deliveries and their Blender helpers only. Producer references govern appearance. **Producer approval pending.**

| Checklist item | Status | Finding / action |
|---|---|---|
| 1. Reference fidelity | fixed | Replaced sparse triangular fin spikes with sixteen 3 mm thick rectangular fins on EACH head in BOTH LODs. Removed buried duplicate LED planes; the clear-coated covers carry the three fields, with physical front separators retained. Simplified concealed LOD1 rail faces, bolt backs and cable sections to stay at 800 triangles. Preserved emitting-face winding and restored the producer-matched neutral-white display tint for the specified 4000 K floodlight. Reference-aligned day/night and actual-distance images inspected. |
| 2. Generator contract | pass | Two factory-reset builds have identical geometry/UV/shading digests; saved sources match. The untouched Tools/Art/kit_blender.py built all five twice with matching manifest digests. Slots are Housing then Lens; optical faces alone use Lens. |
| 3. Budgets | pass | 2288 / 800 triangles against 2500 / 800. Headroom 212 / 0. |
| 4. LOD1 continuity | fixed | Repaired LOD texture addressing and baked both LODs. Maximum projected fixture-outline difference: 0.0225 px at 150 m / 0.0130 px at 260 m. Lens-outline difference: 0.0000 / 0.0000 px. No discernible silhouette change at native 150/260 m. Small shading/antialiasing differences remain quantified; no bit-identical or runtime claim. |
| 5. Fixture frame and dimensions | open | Detailed brief dimensions retained: heads 30 × 10 × 38 cm, base 40 × 25 cm, yoke about 80 cm. Measured complete envelope is 82.4 cm wide × 27.9 cm deep × 70.9 cm high; BUILDING_KIT instead lists an approximate 80 × 40 × 75 cm envelope. Origin and centimetre roundtrip passed. Direction [0.965926,0,-0.258819], 4000 K. OPEN: dimensional authority for 06. The appearance table’s approximate 80 × 40 × 75 cm envelope conflicts with the detailed head/base dimensions. This delivery retains the detailed brief and measures 82.4 × 27.9 × 70.9 cm (width/depth/height). A producer clarification was requested; no answer or approval is assumed. Do not stretch the export silently.  |
| 6. UVs / normals / topology | fixed | Both LODs now occupy disjoint charts in shared UV0 and FinishBake atlases; zero positive-area overlaps, zero degenerate geometry/UV triangles. Unit normals, orthogonal tangents and positive closed-shell volumes passed. Invalid bevel-bake hemispheres repaired. Exactly one final DirectX normal per slot. |
| 7. Texture contract | pass | Seven current runtime maps, Housing 2048² / Lens 1024². Actual image sizes, sRGB/linear bindings, packed-byte equality, DirectX convention and recipe values checked. Documented roughness corrected to the actual 0.18 clamp. |
| 8. Finish / emission | fixed | Round 4b: continuous irregular rubbed edges with sparse wider chips replace the dashed grain-threshold pattern. Existing cast grain, pits and local corrosion are retained. Structured optical profiles and centre/edge falloff retained. Fixed misplaced centre-weighting code. Display tint [0.89,0.94,1], 4000 K; emission multiplier 5. Both sixteen-fin stacks are real geometry in both LODs; removed the peach emission cast in Round 4b. |
| 9. Evidence / handoff | fixed | Refreshed source, both runner FBXs, verification.json, day/night on/off and reference renders, enlarged LOD sheets and native 12/150/260/450 m pairs. Refreshed affected Round 4b entries, independent technical/distance receipts and artifact hashes. STATE and handoff supersede earlier claims. Producer approval and Unreal/gameplay acceptance are not claimed. |

## Measured distance evidence

| Distance | Changed pixels > 1/255 | Maximum display RGB delta | Source |
|---|---:|---:|---|
| 12 m | 4793 | 0.3216 | distance |
| 150 m | 23 | 0.0314 | stable |
| 260 m | 14 | 0.0235 | stable |
| 450 m | 8 | 0.0392 | distance |

1920 × 1080, 60° horizontal FOV, fixed elevated fixture-centre camera; the comparison forces each LOD at the same distance. Delta statistics include rendered shading and antialiasing, not just geometry. Enlarged sheets are diagnostic crops, not the native on-screen size. The stable 150/260 m comparison uses 512 samples without denoising; only the documented centre region is rendered at native pixel scale.

## Open items and limits

OPEN: dimensional authority for 06. The appearance table’s approximate 80 × 40 × 75 cm envelope conflicts with the detailed head/base dimensions. This delivery retains the detailed brief and measures 82.4 × 27.9 × 70.9 cm (width/depth/height). A producer clarification was requested; no answer or approval is assumed. Do not stretch the export silently.

Opaque optical approximations and modest enlarged LOD simplification are documented. Unreal appearance, production light behavior and the gameplay LOD switch remain Claude’s integration checks, outside this task. No approved high-poly master or producer acceptance is inferred.

## Evidence

- [Reference day](../review/ref_match_day.png) / [night](../review/ref_match_night.png); [producer comparison](../review/reference_left_round04_right_night.png).
- [Enlarged LOD pair](../review/rts_LOD0_left_LOD1_right.png); [150 m crop](../review/astra_150m_LOD0_left_LOD1_right.png) / [260 m crop](../review/astra_260m_LOD0_left_LOD1_right.png).
- [Source and reimport verification](verification.json), [independent audit](astra_technical.json), [distance metrics](astra_distance_comparison.json), [final inventory](delivery.json).
