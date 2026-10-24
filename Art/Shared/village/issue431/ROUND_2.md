# #431 — Round 2: HouseA quality bar

**Ready for producer quality-bar review. Approval pending; stop here.**
Requested lane: GPT-6.1-Sol, round 2 of 3 Sol rounds. Round 1 architecture remains approved. Round 3 has not started.

Open the [final Unreal gallery](round2/REVIEW.html). All three states have final 12 m, 150 m, 450 m and three-quarter views beside an Abrams; matched master/game views at 12/150/450 m; and separate-actor LOD comparisons at 249.6/270.4 m, including same-distance controls. These are native **editor previews, not gameplay**. Camera receipts accompany every PNG. The 60° horizontal lens has constrained 16:9 content; the exact 12 m ground-target view crops the house, so use the three-quarter views for the whole asset.

## Final pass against Claude's review

- Roof lifted to red-brown with scanned tile relief, broad variation and a slight slope tint. It reads as a tiled surface at 150 m without the first version's near-black uniformity.
- Damaged chimney cap is supported by the chimney shaft in the final three-quarter capture. The first capture's floating cap is superseded.
- Rubble uses the standing building's brick course scale and tint, with dust variation. Courses show in the final three-quarter view. The heap mixes brick, roof tile, timber and plaster/concrete fragments; it is lighter than the first black heap.
- The approved breach debris arrangement is retained.

The approved 11 × 9 m massing, 5.8 m eaves / 9.6 m ridge, +X front gable, opening positions and damage placement remain. The two attic heads extend to 7.8 m as requested, clearing the existing 7.5 m firing eye. Round 1 files were kept unchanged.

## Round 2 construction

Brick tiling is halved to approximately 6.5 cm courses and tinted browner. Concrete lintels, slimmer sills/drips, stepped PVC frame/sash profiles and the dark 50 cm plinth remain geometry. The door has a canopy, number plate, lamp, dish, vent and meter box. RoofingTiles013A replaces the orange scan; roof edges retain thickness and fascia/gutter geometry.

The hole has uneven blast edges, varied rafters, broken battens and hanging tiles. Blown windows have missing glazing and partial frames; tint atlases carry localized soot and ground-contact dust. Rubble stumps retain closed 30 cm thickness, varied broken tops under 3 m, and a fallen gable fragment. Weathering is restrained; its strength remains part of the visual quality review.

The review masters add 20 mm chamfers on stable edges. Touching masonry joins are excluded to avoid false vertical seams. Geometric object-space normal/AO is baked high-to-low without baking the scan normal twice. UV0 holds real-scale scans; UV1 is the unique atlas. Large horizontal floors bypass AO. Invalid cross-member bake rays are repaired to the receiving normal, preserving valid chamfer detail; the cleanup receipt records affected texels.

## Verified budgets

| State | Game LOD0 | Authored LOD1 | Review master | Native game slots |
| --- | ---: | ---: | ---: | ---: |
| Pristine | 6,120 | 2,376 | 7,092 | 6 |
| Damaged | 6,158 | 2,988 | 7,172 | 6 |
| Rubble | 1,760 | 1,568 | 3,812 | 4 |

All nine source, independently reimported FBX and Unreal counts agree. The higher-count rule is therefore satisfied. LOD0 is below 12k; LOD1 below 3k. Zero degenerate geometry/UV triangles and invalid tangents in portable QA. Minimum game triangle altitude exceeds 1.6 cm. All 23 aperture checks pass in each standing state. No Nanite. Native imports each have one LOD, intentionally; the authored LOD1 is a separate mesh. Each game LOD0 retains two convex collision hulls for review; production collision remains Claude's.

Portable slot order: **Brick, Roof, Trim, Glass, Wood, Rubble**. Native rubble strips unused Trim/Glass and has Brick/Roof/Wood/Rubble. LOD0 and LOD1 match per state; bind by slot name. Masters are source/review assets, not production geometry.

## Authoritative assembly

| Item | Path |
| --- | --- |
| Final source | `round2/source/HouseA_round2_baked.blend` |
| Geometry recovery checkpoint | `round2/source/HouseA_round2.blend` |
| Game and separate LOD1 FBXs | `round2/fbx/SM_Village_HouseA*.fbx` (individual files) |
| Review-master FBXs | `round2/source/*_MASTER.fbx` |
| Preview level | `/Game/Art/Candidates/Village/L_VillagePreview` |
| Final LOD0/LOD1 meshes | `/Game/Art/Candidates/Village/Round2/Final02/` |
| Final masters | `/Game/Art/Candidates/Village/Round2/Masters02/Final/` |
| Surface parent | `/Game/Art/Candidates/Village/Round2/M_Round2_Surface` |
| State/slot instances | `/Game/Art/Candidates/Village/Round2/Materials/MI_<State>_<Slot>[_MASTER]` |
| Scanned textures | `/Game/Art/Candidates/Village/Round2/Textures/` |
| Final normal/AO/tint atlases | `/Game/Art/Candidates/Village/Round2/Textures/Atlas02/` |

The saved preview has three inspection columns: game at X=0, master at X=24 m, LOD1 at X=48 m; state rows Y=-24/0/24 m, Z=0. Evidence temporarily moves the chosen variant to the same origin and parks the others offscreen, then restores inspection columns. Actor references/transforms are in `round2/evidence/preview_state.json`.

Earlier First, Final, Masters, Masters02 root and texture experiments are historical diagnostics. No cleanup was authorized or performed. Final02 uses the materials/atlases listed above, with no Round1 material dependency. Shared glass and the existing Abrams preview dependencies remain required.

## Material and LOD handoff requirements

The current shared `M_Bld_Surface` lacks scan/atlas inputs, so a candidate copy was extended for UV0 BaseColor/Normal/ORM and UV1 Tint/BakeNormal/BakeAO. Shared parents and Round1 were untouched. Glass uses the current shared `M_Bld_Glass` directly. Atlas normals are object-space data (linear mask compression), decoded and transformed local-to-tangent before scanned micro-normal addition; do not import them as tangent normal maps. Tint/AO are linear masks. Game BakeStrength is **0.35**, master **0**. Exact overrides and parents are in the native verification receipt.

**LOD1 remains a separately imported mesh and actor by Claude's decision.** No native LOD attachment and no `village_431_round2_lods.py` execution. That historical recipe now fails immediately to prevent accidental use. Claude's production setup attaches LOD1 at **260 m**; the captures demonstrate manual mesh substitution, not an automatic switch.

LOD1 merged faces cross some unique near-detail atlas charts. The final surface shader therefore fades **only geometric atlas normal/AO** from camera-to-bounds-centre 180 m to 230 m, before either switch view. Scanned normal/colour/ORM and atlas tint stay present. Carry this graph into setup: `BakeStrength * (1 - saturate((distanceCm - 18000) / 5000))` drives both bake blends. See [detail_fade.json](round2/evidence/detail_fade.json). LOD1 is intended for the far range; its atlas interpolation is unsuitable for close-range use without this shader. The same-distance controls expose the remaining geometric difference for review. Close under-eave contrast differs slightly between baked game and physical master; final matched images retain that difference for the producer's judgment.

## Textures and persistence checks

21 portable PNGs, all at most 2048²: twelve unchanged ambientCG CC0 maps from **Bricks085, RoofingTiles013A, Planks023A, Concrete036** (BaseColor, Normal, packed ORM), plus three in-house normal/AO/tint atlas sets. Source copies match `Saved/Surfaces/<Name>/` by SHA-256. Mips and streaming remain enabled in the candidate. No git-ignored surface pack is needed for the delivered building.

Read-only restart checks confirmed the assets/preview survived Claude's restart. The clean production level was left unchanged. Final assets and the existing Village preview were saved, then the same preview was reloaded in the running editor. Counts, centimetre bounds, slot bindings, parent/texture/scalar overrides, texture settings and saved dependencies were queried; post-reload native smoke captures confirm the rendered assembly. No editor restart or close was performed by this continuation, and no native file dialog was triggered.

- [Native verification](round2/evidence/unreal_final_verified.json), [source / independent FBX QA](round2/evidence/source_qa.json)
- [Final captures](round2/evidence/final/), [matched masters](round2/evidence/master/), [switch controls](round2/evidence/switch/), [post-reload smoke](round2/evidence/persisted_smoke/)
- [Manifest](round2/manifest.json), [file hashes and provenance](round2/evidence/delivery_receipt.json), [bake receipt](round2/evidence/bake_receipt.json), [normal cleanup](round2/evidence/normal_bake_cleanup.json)

The first improved capture set remains in `round2/evidence/first/`, taken before refinement. `checks/`, `warmup/` and `atlas_baseline/` are diagnostics, not final evidence. Log history includes missing-parent warnings for obsolete Round2 root instances after the restart, a corrected graph-pin connection error, and a rejected read of the unavailable `sourceModels` property. Final instances use the verified M_Round2_Surface parent; the final graph compiled and rendered. No claim of an empty historical log is made.

## Modelling-lessons check and gate

Checked against the current shared lessons: substantial trim/reveals kept as geometry; backing/returns retained; fine chamfers baked; floors bypass AO; game triangles wider than 1 cm; source/FBX/UE counts agree; real brick scale; irregular breaks and mixed debris; full-thickness stumps; FBX_SCALE_NONE and centimetre bounds; portable CC0/in-house textures; current shared parents extended only where necessary; caches outside Art; captures before refinement; validated exports; no native-dialog operations. Separate LOD comparison follows the newest lesson and explicit producer decision.

Visual quality-bar approval remains the producer's next action. Gameplay state swaps, production LOD attachment, navigation, placement, firing/garrison integration and runtime benchmarking were not performed. The material fade and remaining close-view shading difference are disclosed above. No Round3 work, commits or pushes.

## Reproduction / continuation

Do not replay the destructive `village_431_round2.py` rebuild over the approved architecture. The validated baked blend and FBXs are authoritative. Portable pipeline: `round2_bake.py`, `round2_qa.py`, `round2_bake_clean.py`, `round2_tint.py`. Native recipes: `round2_materials.py`, `round2_finish.py install_final`, then `round2_detail_fade.py`; new installs need an unused candidate destination because native import refuses overwrites. `finish.py` game/master/switch stages reproduce the fixed captures; verify saves/reloads and queries. Never run `round2_lods.py`. Report generation is read-only with respect to Unreal.

Continuation: use `$world-divide-building-production` for #431. Read `STATE.md` and this report. Round1 architecture is approved; Round2 is delivered for quality-bar review, with separate LOD1 and the stated shader requirement. Await the producer's approval or fixes before Round3. Preserve round1 and leave the editor running. Do not commit or push.
