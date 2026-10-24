# #446 — round 1 of 4

Round1 delivered for producer/Claude review. [Review gallery](REVIEW.html). Work is confined to the destroyed span, its separate LOD1, retained master and own atlas. No visual approval or production readiness is claimed.

## Result

Added 42 bent, closed triangular-prism bars: a nine-bar peeled mat at each dropped end, plus six-bar fans on each of the four side faces. Nominal diameter 6 cm (triangular cross-section maximum width about 5.2 cm); authored exposed paths 1.29–1.48 m. Roots sit in the approved concrete above the waterline; free segments turn down toward the collapse. The inherited submerged bars remain. New warm rust edges are atlas texels on `Trim`; `SurfaceTint` remains exactly `(0.10, 0.075, 0.055)`. No inherited material scalar/vector values or parent changes.

The final placement puts the new mat across the visible slab surface rather than showing only short vertical tips above water. The 12 m view reads clearly as bent reinforcement. At 150 m it adds grouped fine bristles and their shadows; each bar is still below one pixel in width, so the strength of that read remains a producer review point for round 2. No further exaggeration beyond the brief's diameter/length range was used.

## Triangle counts

| Mesh | Accepted Blender | Round1 Blender | FBX reimport | Unreal | Increase |
|---|---:|---:|---:|---:|---:|
| LOD0 | 984 | 1,572 | 1,572 | 1,572 | +588 |
| LOD1 | 984 | 1,572 | 1,572 | 1,572 | +588 |
| Master | 2,760 | 3,348 | 3,348 | 3,348 | +588 |

LOD0 and LOD1 each have 12 triangles of allowance left. Four source/exported/native slots in order: `Brick`, `Roof`, `Trim`, `Rubble` (source material datablocks retain their mesh-name prefixes). Unused inherited Glass/Wood slots and other-state source datablocks were removed. Nanite disabled; each native asset has one native LOD. Separate LOD1 keeps all new bar geometry. Deck-top-centre origin, transforms and every inherited vertex/face are unchanged; material-index compaction preserves the same surfaces. Exactly two original UCX hulls, native collision data byte-for-data equal to the accepted mesh, `CTF_UseSimpleAsComplex`.

## Authority, paths and hashes

Portable authority is this folder's `fbx/`, `source/`, `textures/`. Native review authority is `/Game/Art/Candidates/Village/Rebar446/R1/Review03/`: `Meshes/`, `Materials/`, `Textures/`. The preview remains `/Game/Art/Candidates/Village/L_VillagePreview`. Earlier exploratory R1 imports and `Refined/` are retained, parked and not the review authority. `checkpoint01/` and `checkpoint02/` preserve validated earlier passes; their native reimport metadata is historical.

| Portable file | SHA-256 |
|---|---|
| [fbx/SM_Bridge_Span_Destroyed.fbx](fbx/SM_Bridge_Span_Destroyed.fbx) | `423c31e6047146495ca948b3a23b2e35fb2d511047910daece180cdfb566017e` |
| [fbx/SM_Bridge_Span_Destroyed_LOD1.fbx](fbx/SM_Bridge_Span_Destroyed_LOD1.fbx) | `76254061871b7c41b6ec1d089a1dfdecf22e3471b8e05c4298183cb0950cdb56` |
| [source/SM_Bridge_Span_Destroyed_MASTER.fbx](source/SM_Bridge_Span_Destroyed_MASTER.fbx) | `cc813c14f56f5a0d90b19a02341d71bc348a178176de953a4e652170718618ca` |
| [source/Bridge_baked.blend](source/Bridge_baked.blend) | `57a1b4610adfd92ed708eb4f48f472fa7bbee577edcd702a6787ab36f3e64151` |
| [textures/SM_Bridge_Span_Destroyed_Tint.png](textures/SM_Bridge_Span_Destroyed_Tint.png) | `d751982496c1d56b9fdb3808dc0b9111afb07b32ac22a9dfc4e60669d287dcb8` |
| [textures/SM_Bridge_Span_Destroyed_BakeNormal.png](textures/SM_Bridge_Span_Destroyed_BakeNormal.png) | `41d8c4af5ab60f118013a98b604325f362e36a4e48d5c632306c1fb29604380a` |
| [textures/SM_Bridge_Span_Destroyed_BakeAO.png](textures/SM_Bridge_Span_Destroyed_BakeAO.png) | `8a519bfe2390fbdb735a5cc8016e766773c2cf5baa7771e9c5b84472fd119c95` |

[All file/script hashes](hashes.json) · [Manifest and exact native bindings](manifest.json) · [Native verification](evidence/unreal_verified.json) · [Independent source audit](evidence/independent_audit.json) · [Blender/FBX QA](evidence/source_qa.json) · [Bar paths and bake receipt](evidence/build.json).

Only this mesh's three 2048² atlas PNGs were rebaked, from the retained 2,760-triangle accepted master plus the same 588-triangle reinforcement: Cycles selected-to-active object normals and AO, four samples, 3.5 cm cage, 8 cm maximum ray, 2 px margin. Object-normal Y converted for Unreal as in round4; inherited tint/weathering recipe retained, only new rebar texels receive the rust edge. Source texture dependencies are local to `textures/`; inherited concrete/other scan PNGs are unchanged copies for Blender portability. Native instances reuse the accepted `/Final/M_SetSurface` and accepted scan textures, with their atlas references redirected to Review03. Mipmaps and inherited streaming policy retained. No original round4 rebuild or bake entrypoint was run; this round calls only its pure bake/raster/QA helpers.

## Captures

The fixed cameras were captured from the accepted assets before modelling; the original first captures are preserved in `checkpoint01/evidence/before*`. The final before/after pairs use identical camera pose, 60° FOV, sun `(-42,135,0)` and the accepted #431 preview staging. Contract waterline is mesh-local -600 cm; the accepted water cube has its top at world +11 cm (centre +10, thickness 2), or mesh-local -589 cm with the bridge at +600. An exact-zero staging attempt intersected the preview ground and was reverted before the final captures. All new bars are above both heights. Camera JSON and image hashes: [evidence index](evidence/index.json).

- 150 m before: [PNG](evidence/before/SM_Bridge_Span_Destroyed_150m.png), [camera](evidence/before/SM_Bridge_Span_Destroyed_150m.json); after: [PNG](evidence/after/SM_Bridge_Span_Destroyed_150m.png), [camera](evidence/after/SM_Bridge_Span_Destroyed_150m.json).
- 12 m: [game](evidence/after/SM_Bridge_Span_Destroyed_12m.png), [master](evidence/master/SM_Bridge_Span_Destroyed_12m.png).
- 150 m: [game](evidence/after/SM_Bridge_Span_Destroyed_150m.png), [master](evidence/master/SM_Bridge_Span_Destroyed_150m.png).
- 450 m: [game](evidence/after/SM_Bridge_Span_Destroyed_450m.png), [master](evidence/master/SM_Bridge_Span_Destroyed_450m.png).
- Switch: [LOD0 249.6 m](evidence/switch/SM_Bridge_Span_Destroyed_249_6m.png), [LOD1 270.4 m](evidence/switch/SM_Bridge_Span_Destroyed_270_4m.png), from a common LOD0 bounds centre. Manual separate actors, not an automatic LOD test.

## Verification and open items

Blender and independent FBX reimport pass degenerate geometry, UV0 and tangent checks. Game minimum triangle altitude is 3.89 cm. The retained master has inherited finer bevel triangles, used only as bake/review source. Source audit verifies exact inherited geometry/collision and all image paths. New rebar LOD1 UV1 sample error is below 0.001 atlas texel. Native source MD5s match these FBXs; native counts, bounds, slot bindings, unchanged material values, collision and save/reload persistence verified. All 670 protected accepted/greybox files still match their starting hashes.

The inherited LOD1 triangulation has overlapping/coplanar construction surfaces. A surface-ray UV audit reports ambiguity/differences on those inherited faces (maximum 0.921 UV units); new rebar maps match. LOD1 is for beyond 260 m, after the unchanged parent fades normal/AO at 180–230 m. Review the supplied switch images; forced near-distance LOD1 bake fidelity is not certified.

Open: producer judgment of 150 m readability; later rounds' realism polish; actual gameplay camera, automatic production LOD switching, collision/navigation/state integration and runtime performance remain Claude's integration work. These captures are Unreal editor evidence. Same-editor save/reload only; the editor was not restarted or closed. No native dialogs, commits, pushes, production assets, setup scripts or accepted `final/` files changed.

Continue round 2 from this report and `source/Bridge_baked.blend`, with `Review03` as the native candidate. Preserve the accepted surrounding geometry and the +600-per-game-mesh ceiling; review the full-size 150 m pair before spending more detail.
