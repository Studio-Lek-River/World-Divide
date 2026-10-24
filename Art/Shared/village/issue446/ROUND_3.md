# #446 — Round 3 of 4

**Yes: the approved 150 m read held.** Both slab ends retain the dark, irregular fringe over water in the full-size view and the matched 2× crops. Density and tip reach remain recognisably round2. At 12 m, the new bars have a ridge and two differently lit facets rather than a flat card, their overall colour is dark steel/rust, bends run mainly in vertical planes, and a ragged aggregate band now marks their roots. [Review gallery](REVIEW.html). Claude/producer judgment of this round remains pending.

## Approval retained and changes

The round2 review says **“150 m: pass”** and the producer requested **“the 150 m read now passes; keep it.”** This remains the visual constraint. The starting source was `round2/source/Bridge_baked.blend` and its saved centreline/width data. Only round2's added reinforcement was replaced; every inherited accepted body vertex, face, surface assignment, object transform and hull was retained.

All 47 pieces remain: 32 across the two water-entry lines, 12 on the four side faces, three short transverse pieces. Widths stay 10.5–13.9 cm. Main/side centreline lengths are 1.69–2.48 m; short transverse pieces are 1.43–1.54 m after removing horizontal zigzags. Roots are unchanged. Ordinary free endpoints are unchanged; four hooks keep their outward reach while their in-plane bends are flattened into their vertical planes. There are two hooked bars per end plus one transverse piece at one end and two at the other: three/four deliberately hooked or crossed pieces per end. No horizontal letter-shaped zigzag was authored.

Each segment now has an open, apex-up angle profile: two upper facets, four triangles. Ridge heights are about 4–5.4 cm over the 10–14 cm width. Both upper facets face upward, including the turned-back tips; no backfaces or tube caps are needed for the intended cameras. The lit facet/ridge provides a narrow highlight against a shaded face. The 580-triangle reinforcement cost is unchanged.

Rebar UV1 has a separate atlas shelf with continuous charts across bends and wide dark gutters. The shelf avoids pale concrete/background bleeding into the thin bars' mipmaps. Actual saved PNG texels sampled at all 580 rebar face centroids range from `[0.0627, 0.0588, 0.051]` to `[0.0784, 0.0706, 0.0627]` in linear mask-data RGB; no tan edge was painted. `Trim` SurfaceTint remains exactly `(0.10,0.075,0.055)`. The atlas modulation is roughly one-third round2's, with restrained warm variation; normal lighting of the two facets supplies the highlight.

The root line is now a connected, ragged band nominally 35–55 cm along the dropped slab, varying around the retained root positions. Darker broken-concrete tint, irregular aggregate/lip normals and AO are painted into this mesh's three atlases. Angular scabs also surround the side-face roots. Rough surface response comes from the normal/AO detail; no material roughness scalar was changed. Fine periodic patterning seen in the first preview was replaced with irregular aggregate. No new concrete geometry or chunks were added; the remaining 20 triangles were left unused.

## Counts

| Mesh | Round2 | Round3 Blender | FBX reimport | Unreal | Delta from R2 |
|---|---:|---:|---:|---:|---:|
| LOD0 | 1,564 | 1,564 | 1,564 | 1,564 | +0 |
| LOD1 | 1,564 | 1,564 | 1,564 | 1,564 | +0 |
| Master | 3,340 | 3,340 | 3,340 | 3,340 | +0 |

Game LODs are each +580 against accepted #431's 984 (20 below the +600 ceiling). Master is +580 against accepted 2,760. Four slots in the same order: `Brick`, `Roof`, `Trim`, `Rubble`; source datablocks retain their mesh-name prefixes. Nanite disabled, one native LOD in each asset; LOD1 remains a separate authored mesh with identical new bar geometry/charts. Exactly two original UCX stub hulls, native collision equal to accepted, `CTF_UseSimpleAsComplex`. Deck-top-centre origin, +X along span, Z up unchanged.

## Paths and hashes

Portable authority: this round's `fbx/`, `source/`, `textures/`. Native authority: `/Game/Art/Candidates/Village/Rebar446/R3/Review03/` with `Meshes/`, `Materials/`, `Textures/`. Existing preview only: `/Game/Art/Candidates/Village/L_VillagePreview`. Earlier R3 imports/checkpoints are retained and superseded; do not select them by folder number without this report. `checkpoint01/` and `checkpoint02/` preserve intermediate validated exports; main files below are the review delivery.

| File | SHA-256 |
|---|---|
| [fbx/SM_Bridge_Span_Destroyed.fbx](fbx/SM_Bridge_Span_Destroyed.fbx) | `a5965ce4aa28c21186c4eb1c13a6db21ec185746a380f9c2f6a54747efeeb2db` |
| [fbx/SM_Bridge_Span_Destroyed_LOD1.fbx](fbx/SM_Bridge_Span_Destroyed_LOD1.fbx) | `807e88860c0e3a80218351d97c3618b782ee9b85e880a69b6f2f831e383f038a` |
| [source/SM_Bridge_Span_Destroyed_MASTER.fbx](source/SM_Bridge_Span_Destroyed_MASTER.fbx) | `b6151daed8c1e7456259480ee0778db69236973fdc221072a737cd893dcdf51f` |
| [source/Bridge_baked.blend](source/Bridge_baked.blend) | `8056754e6a387e016dfb6d761bf638c28b66ad8e4a63fb473440fa0803219f4f` |
| [textures/SM_Bridge_Span_Destroyed_Tint.png](textures/SM_Bridge_Span_Destroyed_Tint.png) | `3f349a9c5a5bd0a15298bda99d5576634c69ed64317949789f2010578b14a956` |
| [textures/SM_Bridge_Span_Destroyed_BakeNormal.png](textures/SM_Bridge_Span_Destroyed_BakeNormal.png) | `3042688e67da6b1f4c6da598a95672437a09ebdc60e44adb9eec6fcd82743b8c` |
| [textures/SM_Bridge_Span_Destroyed_BakeAO.png](textures/SM_Bridge_Span_Destroyed_BakeAO.png) | `447886285a78364fd33307cd246c6c9208097a80d38bac9d13d7276454632035` |

[Complete hashes](hashes.json) · [Manifest with bounds, native bindings and texture settings](manifest.json) · [Blender/FBX QA](evidence/source_qa.json) · [Source audit](evidence/independent_audit.json) · [Native verification](evidence/unreal_verified.json) · [Paths, profile data, changes against R2 and bake receipt](evidence/build.json).

Only this mesh's Tint, BakeNormal and BakeAO were rebaked/painted, each 2048². Same selected-to-active Cycles object-normal/AO recipe: four samples, 3.5 cm cage, 8 cm ray, two-pixel bake margin, round4 object-normal Y conversion and outlier repair. Root-band detail is applied to those maps after the geometric bake. Rebar atlas gutters are expanded 16 steps; the body occupies the upper 70% of UV1. Game, LOD1 and master sample the matching atlas charts, including the new profile facets. No original round4 rebuild or bake entrypoint was run; only safe helpers were reused with round3 outputs.

Native instances reuse the accepted `/Final/M_SetSurface` and scan textures. Parent/scalar/vector values match the accepted templates; only this mesh's atlas references point into R3. Inherited mipmap/streaming policy retained. Blender dependencies resolve within this round's texture folder; unchanged scan PNGs are portable copies.

## Captures

Before means the reviewed round2 game mesh and its corrected master. Captured the fixed camera set before modelling. Before/after/master share matching poses, 60° FOV, target and sun. [Capture index, hashes and camera JSON](evidence/index.json).

- 150 m: [round2 before](evidence/before/SM_Bridge_Span_Destroyed_150m.png), [round3 after](evidence/after/SM_Bridge_Span_Destroyed_150m.png).
- 2× far-end crop: [before](evidence/crops_2x/before_negative_X_far_end_2x.png), [after](evidence/crops_2x/after_negative_X_far_end_2x.png).
- 2× near-end crop: [before](evidence/crops_2x/before_positive_X_near_end_2x.png), [after](evidence/crops_2x/after_positive_X_near_end_2x.png).
- 12 m against round2: [before](evidence/before/SM_Bridge_Span_Destroyed_12m.png), [after](evidence/after/SM_Bridge_Span_Destroyed_12m.png).
- Master against game: [12 m master](evidence/master/SM_Bridge_Span_Destroyed_12m.png) / [game](evidence/after/SM_Bridge_Span_Destroyed_12m.png); [150 m master](evidence/master/SM_Bridge_Span_Destroyed_150m.png) / [game](evidence/after/SM_Bridge_Span_Destroyed_150m.png); [450 m master](evidence/master/SM_Bridge_Span_Destroyed_450m.png) / [game](evidence/after/SM_Bridge_Span_Destroyed_450m.png).
- Switch: [LOD0 at 249.6 m](evidence/switch/SM_Bridge_Span_Destroyed_249_6m.png), [LOD1 at 270.4 m](evidence/switch/SM_Bridge_Span_Destroyed_270_4m.png), measured from one common LOD0 whole-building bounds centre.

The four crops use identical original-pixel boxes and 2× nearest-neighbour duplication, without enhancement or generated detail. [Crop coordinates and provenance](evidence/crops_2x/index.json). Full images use the editor viewport's native resolution, recorded in the index. 150 m: arm15000 cm to `(0,0,600)`, pitch -55°, yaw145°, FOV60°, sun `(-42,135,0)`. The inherited 12 m camera uses pitch -35°. Accepted water staging is unchanged: nominal waterline -600 cm; preview top world +11 cm relative to the deck at +600 cm gives -589 cm. The new reinforcement still crosses that water-entry region and projects over water. These are editor previews, not production gameplay-camera or automatic LOD evidence.

## Verification and open items

Blender and fresh FBX imports pass finite tangent, nondegenerate geometry and nondegenerate UV0 checks. Game minimum triangle altitude 3.89 cm; finer inherited master bevel triangles remain source-only. All new facet normals point upward (minimum normal Z 0.490). New LOD1 charts exactly match game; master charts are transferred onto the same atlas. The inherited LOD1's overlapping/coplanar body surfaces still produce ambiguous surface-ray UV comparisons (max 0.716 UV units); this is documented from R2. LOD1 is used beyond 260 m, after the unchanged parent's 180–230 m normal/AO fade. Forced near-distance LOD1 bake fidelity is not certified; inspect the supplied switch pair.

Native counts, centimetre bounds, four-slot bindings, source MD5s, unchanged material values, collision and same-editor save/reload persistence checked. Scoped native import log is in the receipt. All 698 protected accepted/greybox/round1/round2 files retain their starting hashes. Modelling lessons were rechecked before reporting: projecting steel stays geometry; game faces exceed 1 cm; no approved opening/body was removed; explicit `FBX_SCALE_NONE` export; atlas/parent bindings persisted; no new level or native file dialog.

Open for round4: realism judgment of the deliberately oversized, two-facet steel and painted root break; underside views are outside the intended camera envelope. Master materials retain their inherited zero bake strength, so atlas-painted fine normal/AO finish is assessed on the game view while both show the common painted tint. Producer approval of round3 is not inferred. Production LOD attachment, gameplay/state integration, collision/navigation and performance remain Claude's work. No restart, editor close, commit, push, accepted `final/` write, production asset edit, village.json or setup edit.

Continue round4 from this report, `source/Bridge_baked.blend` and R3/Review03. Retain the approved 150 m read and examine the round2/round3 12 m pair before further realism changes. Use the repo building-production workflow; do not rerun old round4 rebuild scripts.
