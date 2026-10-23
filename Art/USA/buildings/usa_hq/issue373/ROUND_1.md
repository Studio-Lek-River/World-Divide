# USA HQ #373 — ROUND 1

**Incomplete iteration candidate. NOT accepted; do not integrate.** Meshes, UV1 maps and Unreal mesh/material packages are saved. A valid candidate level and rule-8 captures were not completed, so this round does not meet the requested end-to-end goal. Stop for Claude’s critique before Round 2.

## Delivery

Paths below are relative to Art/USA/buildings/usa_hq/issue373/.

- Final editable checkpoint: round1/HQ373_Round1_Triangulated.blend.
- Final FBXs: round1/SM_HQ373_Shell.fbx and round1/SM_HQ373_Shell_LOD1.fbx.
- Maps: round1/T_HQ373_Shell_NORMAL.png and round1/T_HQ373_Shell_AO.png.
- Hashes and sizes: delivery_hashes.json.
- Independent FBX reimport, per-slot counts, bounds and UV channels: round1/fbx_reimport_checks.json.
- Index: manifest_round1.json. Native final mesh paths/counts: unreal_final_meshes.json.
- Claude continuation: CLAUDE_IMPLEMENTATION_PROMPT.md.

Final Unreal assets are /Game/Art/Candidates/USA_HQ/Issue373_Round1/Meshes/SM_HQ373_Shell_Tri and SM_HQ373_Shell_LOD1_Tri. These are separate one-LOD assets, not a configured two-LOD runtime mesh. Unsuffixed packages are superseded staging imports. Materials and textures are saved in sibling Materials/ and Textures/ folders. No valid saved preview level exists.

## What was made

The approved, mast-stripped game/SM_HQ_Shell.fbx supplied the dense high surface from the nine static sources. The approved master blend was also read to restore the flagpole and roof drain/grate bars without bevel modifiers.

Bulk geometry was welded, planar-dissolved and collapse-reduced. Disconnected pieces below 20 cm remain in the high source for projection but were removed from the low mesh. This is a known fidelity risk: it does not prove that every detached part baked onto a receiving surface.

UV0 retains the inherited tiling coordinates. UV1 is a unique smart-projected/packed atlas, with about 60.15% summed triangle UV area at LOD0. LOD1 inherits it through reduction. High-to-low Cycles tangent normal and AO bakes used 12 cm cage extrusion, 25 cm ray distance, 32 samples and 2 px dilation. Normal green is DirectX/negative Y.

LOD1 uses a 5 cm weld outside protected drain surfaces, reduction, topology validation and restoration of otherwise empty material sections. It is an automated first candidate, not a hand-authored silhouette-preserving far mesh.

## Counts

| Measure | LOD0 | LOD1 |
|---|---:|---:|
| Independent Blender reimport of final FBX | 60,579 | 18,389 |
| Non-empty shell sections / slots | 34 / 34 | 34 / 34 |
| Degenerate triangles in that reimport | 0 | 0 |
| Zero tangent frames in UV0 diagnostic | 1 | 2 |
| Final native Unreal imported triangles | 59,950 | 18,389 |
| Proposed whole HQ, native counts | 142,758 | 39,130 |
| Whole HQ using conservative FBX shell count | **143,387** | **39,130** |
| Remaining against 150k / 40k, conservative | **6,613** | **870** |

The proposed whole-HQ sum includes unchanged shell detail (41,344 / 10,655), moving parts, 16 door sections, current shared flag/red beacons, approved H28 masts and #195 supports/four fixtures. It is not a measured rendered assembly. See unreal_assembly_counts.json, substituting the final shell counts from unreal_final_meshes.json. The earlier count receipt used a 59,952-triangle staging shell.

Unreal drops 629 LOD0 triangles relative to Blender’s final FBX reimport. Explicit triangulation did not resolve this. Import thresholds/slivers/export normals need investigation; do not report only the lower native number.

The proposed assembly has 81 unique mesh-slot sections per LOD when repeated meshes share instancing, against the inherited 80-section target. This is slot accounting, not a draw-call benchmark. Shell slots and order are unchanged.

New textures: **one set, two 2048×2048 maps**. Normal uses TC_Normalmap; AO uses TC_Masks and R. Both are linear, mip generation is TMGS_FromTextureGroup, NeverStream=false. Existing tiling textures are inherited unchanged and were not exhaustively re-audited. No 4K textures or Nanite.

## Material

Switch HQ373_UseShellBake defaults **false**. Parameters HQ373_BakedNormal and HQ373_BakedAO sample TexCoord index 1. True normal is normalize(TilingNormal + BakedNormal − (0,0,1)); false returns the original normal. True AO is original AO × baked R; false returns original AO. Missing original inputs use flat normal and AO=1.

The checkout has moved beyond the brief: production instances now use M_Bld_Surface, M_Bld_Coating, M_Bld_Glass, M_Bld_Operational and the two wear parents. The old production M_HQ_Final_Operational path is absent. The candidate therefore copies six current parents to preserve their graphs/tiling; the operational copy is named M_HQ_Final_Operational. Claude must review this scope difference before porting. Production parents were not changed.

All 34 shell instance copies retain their basenames, use candidate parents and enable the switch. Parent defaults were queried false and instance overrides true after saving. Receipts: current_parents.json, bake_graph_nodes.json, candidate_material_bindings.json, material_persistence_audit.json. This is same-process persistence verification, not fresh-editor certification.

## Evidence and blocker

Actual Unreal asset thumbnails only:

- asset_previews/SM_HQ373_Shell_Tri.png
- asset_previews/SM_HQ373_Shell_LOD1_Tri.png

These are 256 px asset thumbnails, not level/RTS/gameplay/rule-8 evidence. The far thumbnail shows substantial façade/roof distortion and openings; it does not establish an invisible switch.

**No rule-8 capture paths exist.** Master/game 12 m flagpole/grille views, 150 m, 450 m and the 249.6 / 270.4 m switch pair remain missing. required_capture_manifest.json records intended cameras, all marked NOT CAPTURED.

Level duplication retained an illegal private source BSP Model reference and failed saving. Historical preview dependencies also triggered dense-shell rebuilds and contained stale kit/material paths. The native New Level dialog blocked further MCP dispatch. Finally MAP SAVE FILE was explicitly rejected: **“is not allowed over MCP; ask the producer.”** No alternate console/Python route bypassed that refusal. Logs: editor_round1_clean.log and editor_round1_save.log. A valid blank candidate level prepared by the producer/integrator is needed to continue evidence with these tools.

## Known defects and next changes

1. LOD1 fails fidelity even in its thumbnail. Replace whole-shell collapse with structural retopology preserving façade frames/recesses, roof/parapet outlines, rails, cables and grille slats.
2. Audit removed sub-20 cm pieces. Detached details without a receiving low surface and silhouette contributors must remain geometry. Classify by visibility, not size alone; isolate bake groups/cages.
3. Resolve the 629-triangle native loss and 1 / 2 invalid tangent frames. Targeted UV repair failed. Proposed sliver deletion stopped because the face width was about 7 mm, exceeding the 1 mm repair limit. Failed repair attempts did not alter final exports.
4. Independently check UV1 overlaps, padding/mips and real 12 m bake fidelity. One 2K atlas may be insufficient in important regions. No larger texture/budget is assumed approved.
5. Create the valid preview, assemble current dependencies, and capture the required comparisons before claiming acceptance. Review the six-parent migration and 81-section count with Claude.
6. Automatic synchronized 260 m switching, collision/navigation/selection, motion, ownership/power and performance remain untested integrator work.

## Reproduction and preservation

New import-safe checkpoint recipes under Tools/Art/: usa_hq_373_candidate.py → usa_hq_373_finalize.py → usa_hq_373_triangulate.py → usa_hq_373_verify.py, run in background Blender. They operate in issue373; never replay them on the approved master. Other probes/failed repairs/intermediate blends are diagnostics, not integration inputs. ddc/ and shaderwork/ are temporary editor caches.

The approved master, dense shell, existing shell-detail FBXs and HANDOFF_379.md still match initial hashes (input_integrity_final.json). No production Content, original game generator/recipe or git state was changed; no commit, push or PR. Editor shutdown is recorded in editor_lifecycle.json.

Final shutdown check: owned editor PID 68092 exited normally with code 0. There are 46 saved candidate .uasset files and zero saved .umap files. Native imports of both final meshes emitted degenerate-tangent-base and nearly-zero-tangent warnings; see final_validation.json. These warnings remain defects.

Checkpoint visibility was tidied with Tools/Art/usa_hq_373_checkpoint.py (LOD1/high hidden); delivery_hashes.json was refreshed afterward. FBX/map hashes stayed unchanged. Round ended with the incomplete evidence status above.
