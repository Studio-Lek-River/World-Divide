# #446 — Round 4 of 4: final handoff

**Yes: the approved 150 m read held.** Both ends retain an irregular fringe over water, with distinct projecting bars in the matched 2× crops. Before means the accepted #431 asset, captured afresh before modelling. [Comparison gallery](REVIEW.html).

## Realism changes

Started from `round3/source/Bridge_baked.blend` and its 47 saved bar paths. Converging waterline tips were nudged into separate lanes; two side-bar tips were eased apart. Roots, widths, counts, outward reach and vertical droop remain. Three transverse pieces provide the intentional crossings; four returning hooks remain. [Tip separation measurements](evidence/separation.json).

The fracture band varies approximately 20–70 cm, with larger angular chips, deeper pockets at selected bar groups, fine aggregate beneath the larger shapes, and a thin dark wet margin above the water. Tint, object-normal and AO carry the finish. No concrete geometry was added.

Steel has varied warm rust pigment and curved shading across the two upper facets, replacing broad pale panels with a narrower highlight. The open V profile remains four triangles per segment. SurfaceTint is unchanged at `(0.10,0.075,0.055)`; parent, scalar and vector values match the accepted templates. The saved atlas's 580 sampled rebar face centroids range from `[0.0824, 0.0196, 0.0039]` to `[0.1451, 0.0353, 0.0078]` in linear mask-data RGB. Small lighting highlights remain; the supplied Unreal views are the visual evidence.

## Counts and final bounds

| Mesh | Blender | Fresh FBX reimport | Unreal | Increase over #431 |
|---|---:|---:|---:|---:|
| LOD0 | 1,564 | 1,564 | 1,564 | +580 |
| LOD1 | 1,564 | 1,564 | 1,564 | +580 |
| Master | 3,340 | 3,340 | 3,340 | +580 |

Counts equal round3; each game LOD is 20 triangles below the +600 allowance. Slots are `Brick`, `Roof`, `Trim`, `Rubble`. Nanite is off; LOD1 is a separate authored mesh. Two original UCX stub hulls and native collision data are unchanged; LOD0 uses `CTF_UseSimpleAsComplex`.

Bounds below are mesh-local centimetres in XYZ order, measured in Unreal after final-file import:

| Mesh | Minimum cm | Maximum cm | Size cm |
|---|---|---|---|
| LOD0 | `[-1602.862, -858.614, -944.462]` | `[1602.862, 860.566, 80.902]` | `[3205.725, 1719.18, 1025.363]` |
| LOD1 | `[-1602.862, -858.614, -944.462]` | `[1602.862, 860.566, 80.902]` | `[3205.725, 1719.18, 1025.363]` |
| Master | `[-1602.579, -858.614, -943.684]` | `[1602.579, 860.566, 80.902]` | `[3205.158, 1719.18, 1024.586]` |

Deck-top-centre origin, +X along the span, Z up and unit scale remain. Export uses `FBX_SCALE_NONE`.

## Written to final

Exactly seven asset/source files and the two JSON inventories were replaced under the explicit round4 authorization:

| Path relative to `Art/Shared/village/final/` | SHA-256 |
|---|---|
| [hashes.json](../../final/hashes.json) | `93b6a069b35afc7957aef1ab32957583048cc4db2b93acf53d9f699a78ebd186` |
| [manifest.json](../../final/manifest.json) | `099dceed4c5b23bdabad355464c2904efd053c8cd1225a7ef47ad7ba15be6ebf` |
| [fbx/SM_Bridge_Span_Destroyed.fbx](../../final/fbx/SM_Bridge_Span_Destroyed.fbx) | `bc1ec3014dbf7a48045873096e2056f6d59bab215ed52a580eb44e7b0eff48eb` |
| [fbx/SM_Bridge_Span_Destroyed_LOD1.fbx](../../final/fbx/SM_Bridge_Span_Destroyed_LOD1.fbx) | `5b755ea8a4e3e9d1cba10e1a73230591c1fe7be394a68b26e8d53fb73d15de83` |
| [source/Bridge_baked.blend](../../final/source/Bridge_baked.blend) | `c074a020b33b4ed605fd47f6397f46ec235dee01cd99c373fca04a573e48e9b3` |
| [source/SM_Bridge_Span_Destroyed_MASTER.fbx](../../final/source/SM_Bridge_Span_Destroyed_MASTER.fbx) | `21b6c91a44b5a735b86cc3910be917daaf7672a082b701c8c6f37d8a3532bdec` |
| [textures/SM_Bridge_Span_Destroyed_BakeAO.png](../../final/textures/SM_Bridge_Span_Destroyed_BakeAO.png) | `83f6197ba01b6f59dec590b956771587c17319e78d598ecbee30e592a58b0f0d` |
| [textures/SM_Bridge_Span_Destroyed_BakeNormal.png](../../final/textures/SM_Bridge_Span_Destroyed_BakeNormal.png) | `a7518f208a127c5b7e9f7bc1a5b0b70b45625178b36f923b2740bdaf52e5f9a6` |
| [textures/SM_Bridge_Span_Destroyed_Tint.png](../../final/textures/SM_Bridge_Span_Destroyed_Tint.png) | `598b11f6efc3808a44d3b9fbfce727f43dcfa3917784d0c8d09e3c6dca7c23be` |

**614 other final files are byte-identical**, including the damaged-span FBXs and atlas and every other mesh. No files were added or removed. [Before inventory](evidence/final_before_hashes.json) · [after receipt with every unchanged hash](evidence/final_delivery.json). Unrelated manifest and hash entries compare equal. Only the destroyed span's three mesh/source-QA records and affected file hashes changed.

The shared final `Bridge_baked.blend` was merged into the accepted multi-state source. All 9 unaffected objects, including damaged-span geometry, UVs, material references and transforms, were preserved and checked after reopening. [Merge audit](evidence/merged_source_audit.json). This round's `source/Bridge_baked.blend` is the isolated working source; `delivery/source/Bridge_baked.blend` is the exact multi-state file copied to final. All nine previous files are backed up under `accepted_backup/`.

## Paths and verification

Native authority: `/Game/Art/Candidates/Village/Rebar446/R4/Final/`, with three meshes in `Meshes/`, eight instances in `Materials/`, and three atlases in `Textures/`. Review01 and Review02 are superseded and retained. Existing preview only: `/Game/Art/Candidates/Village/L_VillagePreview`.

Meshes and atlases were imported from their delivered `final/` files. Source MD5s, counts, collision, material bindings and same-editor save/reload were verified. The log retains one refused overwrite of the earlier Review02 texture; the fresh R4/Final imports succeeded. Accepted `/Game/Art/Candidates/Village/Final/M_SetSurface` and scan textures remain dependencies. Material values, mipmaps and streaming policy are retained. Only this mesh's three 2048² atlases changed.

[Native receipt](evidence/unreal_verified.json) · [working-file hashes](hashes.json) · [round manifest](manifest.json) · [Blender/FBX QA](evidence/source_qa.json) · [independent audit](evidence/independent_audit.json).

## Captures

- 150 m accepted #431 [before](evidence/before/SM_Bridge_Span_Destroyed_150m.png) / [after](evidence/after/SM_Bridge_Span_Destroyed_150m.png).
- Far end 2×: [before](evidence/crops_2x/before_negative_X_far_end_2x.png) / [after](evidence/crops_2x/after_negative_X_far_end_2x.png).
- Near end 2×: [before](evidence/crops_2x/before_positive_X_near_end_2x.png) / [after](evidence/crops_2x/after_positive_X_near_end_2x.png).
- 12 m: [accepted #431](evidence/before/SM_Bridge_Span_Destroyed_12m.png) / [round3](evidence/round3_reference/SM_Bridge_Span_Destroyed_12m.png) / [final game](evidence/after/SM_Bridge_Span_Destroyed_12m.png).
- 450 m: [game](evidence/after/SM_Bridge_Span_Destroyed_450m.png).
- Switch: [LOD0 249.6 m](evidence/switch/SM_Bridge_Span_Destroyed_249_6m.png) / [LOD1 270.4 m](evidence/switch/SM_Bridge_Span_Destroyed_270_4m.png).
- Master/game: [12 m master](evidence/master/SM_Bridge_Span_Destroyed_12m.png) / [game](evidence/after/SM_Bridge_Span_Destroyed_12m.png); [150 m master](evidence/master/SM_Bridge_Span_Destroyed_150m.png) / [game](evidence/after/SM_Bridge_Span_Destroyed_150m.png); [450 m master](evidence/master/SM_Bridge_Span_Destroyed_450m.png) / [game](evidence/after/SM_Bridge_Span_Destroyed_450m.png).

[Capture hashes and cameras](evidence/index.json) · [crop provenance](evidence/crops_2x/index.json). Crops duplicate original pixels 2× with nearest-neighbour sampling. Matching 60° FOV and sun `(-42,135,0)`; 150 m arm to `(0,0,600)`, pitch −55°, yaw 145°. The inherited 12 m camera uses −35°. Switch distances use one common LOD0 bounds centre. Nominal waterline remains −600 cm; accepted preview top world +11 cm against deck +600 cm gives −589 cm.

## Limits and handoff

Source and fresh FBX imports pass geometry, UV0 and finite tangent checks. Inherited body geometry and hulls are exact. New LOD1 bar charts match LOD0. Inherited coplanar LOD1 body surfaces retain ambiguous ray-based UV comparisons; the parent's 180–230 m normal/AO fade precedes the 260 m switch. The switch captures assess the intended use. Master keeps its inherited zero bake strength: both show the common tint, while fine painted normal/AO is assessed on the game mesh.

Modelling lessons were rechecked before reporting. These are editor captures with manually swapped LOD actors. Gameplay, automatic LOD switching, navigation/state swaps and runtime performance remain Claude's integration checks. Verification uses same-editor reload and post-reload smoke; no restart was permitted. Editor stays running. No production assets, root village FBXs, `village.json`, setup scripts, commits or pushes changed.

Use [Claude implementation prompt](CLAUDE_IMPLEMENTATION_PROMPT.md). The final copy is complete under the user's explicit round4 instruction; separate producer approval of these round4 images is not inferred.
