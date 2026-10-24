# #446 — round 2 of 4

**Yes: the rebar reads at the fixed 150 m view.** Both slab ends have a distinct dark, irregular fringe extending over the water, with individual long lines and crossed pieces visible. The after image is visibly different from the accepted before and the reviewed round1 candidate. [Full-size gallery and 2× end crops](REVIEW.html). Producer/Claude visual approval remains pending.

## What changed

Rebuilt from the accepted #431 source; none of round1's 42 staple-like prism bars were carried forward. Kept the inherited submerged stubs and every other accepted vertex/face. Added 47 irregular pieces: 32 longitudinal bars across the two water-entry edges, 12 side-face bars and three short transverse pieces. Forty-three have three segments; four hooked-back bars have four. Actual ribbon widths 10.5–13.9 cm; centreline lengths 1.66–2.48 m. Roots leave the slab near the waterline, splay over water and turn downward; side bars emerge from the exposed concrete side near the same line. Some tips submerge. Spacing, bends, splay and lengths are seeded and varied; the three transverse pieces are isolated tangles rather than repeating rungs.

Each segment is an open strip with explicit reverse-wound backfaces (four triangles per segment); the broad face points upward toward the RTS view. No pipe cross-sections or end caps. This avoids changing the accepted material parent's sidedness. Atlas steel stays dark, without the light tan edge; `Trim` SurfaceTint is still exactly `(0.10, 0.075, 0.055)`. Small dark root stains are confined to this mesh's atlas. At 12 m these are visibly exaggerated heavy reinforcement strips, with flat ribbon shading; they no longer form identical hoops or a ladder. Round3/4 can refine the close-up shape while preserving the now-readable 150 m silhouettes.

## Triangle counts

| Mesh | Accepted Blender | Round2 Blender | FBX reimport | Unreal | Added |
|---|---:|---:|---:|---:|---:|
| LOD0 | 984 | 1,564 | 1,564 | 1,564 | +580 |
| LOD1 | 984 | 1,564 | 1,564 | 1,564 | +580 |
| Master | 2,760 | 3,340 | 3,340 | 3,340 | +580 |

Each game LOD has 20 triangles left under the +600 allowance. LOD1 uses the same new bars, widths and outline. Four source/export/native slots: `Brick`, `Roof`, `Trim`, `Rubble` (source datablock names retain their mesh prefix). Nanite off; one native LOD per imported asset; authored LOD1 is a separate mesh. Origin remains deck-top centre, +X along span, Z up. Two original UCX pier-stub convex hulls and `CTF_UseSimpleAsComplex` unchanged; native collision data exactly equals the accepted baseline. No approved concrete or break geometry changed.

## Files, paths and hashes

Portable authority is `round2/fbx/`, `round2/source/`, `round2/textures/`. Native authority is `/Game/Art/Candidates/Village/Rebar446/R2/Review01/`: game meshes in `Meshes/`, corrected review master in `Masters02/`, plus `Materials/` and `Textures/`. The first master import is retained but superseded after its atlas UV mapping was corrected. The only preview level is the existing `/Game/Art/Candidates/Village/L_VillagePreview`. Prior R1 and accepted assets are retained.

| Portable file | SHA-256 |
|---|---|
| [fbx/SM_Bridge_Span_Destroyed.fbx](fbx/SM_Bridge_Span_Destroyed.fbx) | `045f27e6bd3288ecd447a7cf58ea99115aea2702932eb4c82f92a6ddcf18e625` |
| [fbx/SM_Bridge_Span_Destroyed_LOD1.fbx](fbx/SM_Bridge_Span_Destroyed_LOD1.fbx) | `2870acba72eef05efdd9281f4d1b20454dc20976a78eb060d63c507653a5b52f` |
| [source/SM_Bridge_Span_Destroyed_MASTER.fbx](source/SM_Bridge_Span_Destroyed_MASTER.fbx) | `49405bd858429f7517906ab05c4b67368473c425154d69b9b6cce3a1ae44ed87` |
| [source/Bridge_baked.blend](source/Bridge_baked.blend) | `d99c758f402669fa6958ba7fd30ef7efb4783a3c243050b6cdebabde03e9c0b7` |
| [textures/SM_Bridge_Span_Destroyed_Tint.png](textures/SM_Bridge_Span_Destroyed_Tint.png) | `ab7c67e5ddbdd73b6a8a251926747bc3500e5a896e62b64a5c3ce160c06fcb8c` |
| [textures/SM_Bridge_Span_Destroyed_BakeNormal.png](textures/SM_Bridge_Span_Destroyed_BakeNormal.png) | `e0cfe4825cc1a30fb08380ee030a88147bad9732cf47043812070f24d02390b9` |
| [textures/SM_Bridge_Span_Destroyed_BakeAO.png](textures/SM_Bridge_Span_Destroyed_BakeAO.png) | `f82e81fc1ca170da92ed499a451a683606be99de2105b962b0c198d035bff812` |

[All file/script hashes](hashes.json) · [Manifest, bounds, bindings and import settings](manifest.json) · [Blender/FBX QA](evidence/source_qa.json) · [Source audit](evidence/independent_audit.json) · [Native verification](evidence/unreal_verified.json) · [Bar widths, lengths, paths and bake receipt](evidence/build.json).

Rebaked only `SM_Bridge_Span_Destroyed_Tint`, `_BakeNormal`, `_BakeAO`, all 2048², from the retained accepted chamfered master plus this reinforcement: Cycles selected-to-active object normals/AO, four samples, 3.5 cm cage, 8 cm max ray, two-pixel margin. Round4 object-normal Y conversion and inherited tint/weathering recipe retained; normal outlier repair retained. Coincident ribbon backfaces bypass self-AO darkening. New atlas texels use dark steel modulation and root stains. The original round4 rebuild/bake entrypoints were not run; only their pure helpers were reused with the output root set to round2.

Native instances reuse the accepted `/Final/M_SetSurface` and scan textures. Material parent/scalar/vector overrides match the accepted instances exactly; only this mesh's atlas texture references point into R2. Source dependencies resolve inside this round's textures; retained scan PNGs are unchanged copies. Mipmaps and streaming settings retained. The master UV1 was transferred onto the repacked game atlas, including exact charts for the new ribbons: the first master comparison caught unmapped ribbon UVs making them pale, which was corrected without changing any material values or geometry. `checkpoints/master_uv_before/` retains the previous source/export for recovery; use the main `source/` files and native `Masters02/` for review.

## Captures

Captured the accepted mesh at all fixed cameras before building, then the reviewed round1 candidate at 150/12 m. The primary before is the accepted #431 baseline named in STATE; the additional round1 views make the reviewed failure directly comparable. Before, round1, after and master share the exact camera pose, FOV and sun for their matching distances. [Capture index with camera JSON and hashes](evidence/index.json).

- 150 m before: [accepted](evidence/before/SM_Bridge_Span_Destroyed_150m.png), [round1](evidence/round1_before/SM_Bridge_Span_Destroyed_150m.png); [round2 after](evidence/after/SM_Bridge_Span_Destroyed_150m.png).
- 2× far end: [accepted](evidence/crops_2x/before_negative_X_far_end_2x.png), [round1](evidence/crops_2x/round1_before_negative_X_far_end_2x.png), [round2](evidence/crops_2x/after_negative_X_far_end_2x.png).
- 2× near end: [accepted](evidence/crops_2x/before_positive_X_near_end_2x.png), [round1](evidence/crops_2x/round1_before_positive_X_near_end_2x.png), [round2](evidence/crops_2x/after_positive_X_near_end_2x.png).
- 12 m: [game](evidence/after/SM_Bridge_Span_Destroyed_12m.png), [master](evidence/master/SM_Bridge_Span_Destroyed_12m.png).
- 150 m: [game](evidence/after/SM_Bridge_Span_Destroyed_150m.png), [master](evidence/master/SM_Bridge_Span_Destroyed_150m.png).
- 450 m: [game](evidence/after/SM_Bridge_Span_Destroyed_450m.png), [master](evidence/master/SM_Bridge_Span_Destroyed_450m.png).
- Switch: [LOD0 at 249.6 m](evidence/switch/SM_Bridge_Span_Destroyed_249_6m.png), [LOD1 at 270.4 m](evidence/switch/SM_Bridge_Span_Destroyed_270_4m.png), both from the common LOD0 whole-building bounds centre. Separate actors, not an automatic native LOD test.

The 2× crops use identical original-pixel boxes with nearest-neighbour duplication: no edge enhancement or generated detail. [Crop coordinates, provenance and hashes](evidence/crops_2x/index.json). Captures are the editor viewport's native resolution, recorded per image. Fixed 150 m camera: target `(0,0,600)` cm, arm 15000 cm, pitch -55°, yaw145°, FOV60°, sun `(-42,135,0)`. Exact 12 m uses pitch -35° as the accepted camera. Accepted water staging is unchanged: nominal mesh-local waterline -600 cm; preview water cube top is world +11 cm / mesh-local -589 cm. New bars straddle that water-entry region and most of their free length shows over water.

## Verification and open items

Blender and independent FBX reimport: zero degenerate faces, zero degenerate UV0 triangles, valid finite tangents. Game minimum triangle altitude 3.89 cm. Exact inherited positions, topology, surface assignments and object transforms verified; source material-index compaction only. Both original collision hulls unchanged. The master retains its accepted fine bevel triangles as bake source. New LOD1 ribbon UV1 charts match exactly, including both facing directions. The inherited LOD1 has coplanar/overlapping construction surfaces and a different triangulation; its surface-ray atlas audit remains ambiguous on inherited faces (max 1.153 UV units). It is used beyond 260 m, after the unchanged parent's normal/AO fade at 180–230 m; forced near-distance LOD1 bake fidelity is not certified. Supplied switch images show the authored far comparison.

Native counts, centimetre bounds, four-slot bindings, source import MD5s, unchanged scalar/vector values, collision and same-editor save/reload persistence checked. Scoped native import log is in the verification receipt. All 684 protected accepted/greybox/round1 files still match their initial hashes. Rechecked `references/modelling-lessons.md` before reporting: projecting reinforcement stays geometry, game faces exceed the 1 cm floor, no scale metadata error, no unintended opening, unchanged parent/material values, local PNG dependency delivery and all mandatory captures are present.

Open: Claude/producer judgment of the 150 m read and close-up realism; flat ribbon shading and deliberate size exaggeration at 12 m; later realism polish. Actual gameplay camera, automatic production LOD attachment, state integration, collision/navigation and runtime performance remain Claude's integration work. Verification used the same editor process; no restart. No native dialogs, new levels, editor close, commit, push, production asset or setup edit. The direct connected MCP tools required unavailable approval; the authorised local MCP driver and installed background Blender were used successfully.

Next round: use this report, `source/Bridge_baked.blend`, and R2/Review01. Preserve the readable silhouette while improving root transitions and close-up strip shading within the same allowance. Do not rerun old round4 rebuilds or overwrite accepted final delivery.
