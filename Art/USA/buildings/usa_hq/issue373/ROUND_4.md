# USA HQ #373 — Round 4 time-limit handoff

ready for integration: no — the two paving-edge tangent failures remain unresolved.

Work stopped at the producer's time limit. This report records existing artifacts; no modelling, baking, importing or capture work continued after the wrap-up instruction.

## Changes completed

Refined the Round 3 restart candidate and retained its real façade depth. Reprojected frames, pilasters, coping and roof surfaces against isolated master components, with filtered normals and local 6 cm AO. The affected architectural pass records 5,785,872 samples and zero unresolved samples; the roof pass records 3,766,363 and zero unresolved samples. These figures certify those passes, not every inherited texture pixel.

Restored small sill/drip/portal receiver faces and the west condenser housing cap. Transferred main paving UV0 from the dense master and bypassed the shell bake on paving, floor wear and walkpads to address tint/tile-line contrast and broad AO blotches. Restored simple warm backing strips behind the recessed window glass, grouped by façade run so they do not bridge courtyard openings. The moving garage door remains the same unchanged part; portal/return shading was corrected around it. Small bevel/fastener and under-fascia shading differences remain; indistinguishability is not claimed.

Candidate material instances now use fresh copies of the current M_Bld_Surface, M_Bld_Coating, M_Bld_Glass, M_Bld_Operational and two wear parents. The default-false HQ373_UseShellBake switch and HQ373_BakedNormal / HQ373_BakedAO parameters are preserved. Five instances bypass the bake, including the restored interior backing. See [overrides](round4/candidate_material_overrides.json).

## Counts and exact disk state

| Measure | LOD0 | LOD1 |
| --- | ---: | ---: |
| Last verified counts supplied by Claude at stop: shell | **62,071** | **19,257** |
| Last verified counts supplied by Claude at stop: whole HQ | **144,879** | **39,998** |
| Whole HQ unique instanced material sections | **80** | **80** |
| Ordered shell slots / sections | **34** | **34** |

Those supplied totals leave 5,121 near triangles for #394. Four 2048×2048 maps form two normal/AO sets. Two mesh UV channels remain: UV0 tiling/tangent basis, UV1 bake sampling.

**Disk-state discrepancy:** the existing on-disk fbx_reimport_counts.json and native_final_audit.json instead record shell **62,077 / 19,255**, implying whole **144,885 / 39,996** with unchanged assembly 82,808 / 20,741. These are not silently relabelled as Claude's figures. No new count audit or geometry correction was run during wrap-up. Claude must reconcile the selected checkpoint before integration. The 80-section measure assumes the accepted combined mast batch and instancing of repeated parts; the loose preview actors have 142 actor-slot instances and are not a draw-call performance benchmark.

**The portable files contain the unfinished UV edit, not a clean last-validated export.** Before the stop, final_edge_uv.log records saving HQ373_Round4_Final.blend and re-exporting both FBXs. final_edge_uv_fix.json reports ten near edge faces edited and no far edge faces. The subsequent existing FBX receipt still reports two invalid tangent/bitangent loops per LOD, despite zero zero-area geometry and UV0 faces. The edit was not successfully validated or re-imported after that export. HQ373_Round4_Final.blend1 is the adjacent pre-edit backup; it has not been restored or certified in wrap-up. Current byte hashes are in [wrapup_hashes.json](round4/wrapup_hashes.json).

Portable paths, relative to this folder:

- round4/HQ373_Round4_Final.blend — unfinished edge-UV revision.
- round4/SM_HQ373_R4_Shell_Cm.fbx and round4/SM_HQ373_R4_Shell_LOD1_Cm.fbx — same unfinished revision.
- round4/T_HQ373_R4_Set0_NORMAL.png, T_HQ373_R4_Set0_AO.png, T_HQ373_R4_Set1_NORMAL.png, T_HQ373_R4_Set1_AO.png.
- round4/texture_set_mapping.json, candidate_material_overrides.json, source_names.json.
- round2/SM_HQ373_R2_Fixtures.fbx remains the unchanged fixture batch.

## Unreal evidence

Approved level: **/Game/Art/Candidates/USA_HQ/Issue373/L_HQ373_Preview**. Final candidate input meshes live under **/Game/Art/Candidates/USA_HQ/Issue373/Meshes/Release/** as SM_HQ373_R4_Shell and SM_HQ373_R4_Shell_LOD1. These are two generator input meshes; the comparison selects LOD manually and does not certify automatic synchronized runtime switching.

All following images are **Unreal captures**, in the approved preview level, with the same neutral daylight and unchanged common parts. close_12m uses the 42 m arm; default_150m and far_450m use 156/456 m. Raw native frames and camera sidecars are authoritative for the latest pre-stop capture pass. Existing labelled sheets precede the last raw refresh and were not regenerated during wrap-up. Their 60-degree framing is a central pixel crop of the native 90-degree view, without resampling or retouching. [Capture manifest](round4/unreal/capture_manifest.json) · [sheet index](round4/unreal/image_index.json).

| View | Master raw | Game raw | Existing Unreal sheet |
| --- | --- | --- | --- |
| close_12m | [master](round4/unreal/close_12m_master_raw.png) | [game](round4/unreal/close_12m_game_raw.png) | [labelled sheet](round4/unreal/close_12m_60deg_Unreal_comparison.png) |
| default_150m | [master](round4/unreal/default_150m_master_raw.png) | [game](round4/unreal/default_150m_game_raw.png) | [labelled sheet](round4/unreal/default_150m_60deg_Unreal_comparison.png) |
| far_450m | [master](round4/unreal/far_450m_master_raw.png) | [game](round4/unreal/far_450m_game_raw.png) | [labelled sheet](round4/unreal/far_450m_60deg_Unreal_comparison.png) |
| facade_frame_12m | [master](round4/unreal/facade_frame_12m_master_raw.png) | [game](round4/unreal/facade_frame_12m_game_raw.png) | [labelled sheet](round4/unreal/facade_frame_12m_60deg_Unreal_comparison.png) |
| drain_grilles_12m | [master](round4/unreal/drain_grilles_12m_master_raw.png) | [game](round4/unreal/drain_grilles_12m_game_raw.png) | [labelled sheet](round4/unreal/drain_grilles_12m_60deg_Unreal_comparison.png) |
| parapet_edge_12m | [master](round4/unreal/parapet_edge_12m_master_raw.png) | [game](round4/unreal/parapet_edge_12m_game_raw.png) | [labelled sheet](round4/unreal/parapet_edge_12m_60deg_Unreal_comparison.png) |

Switch pair: [249.6 m LOD0 raw](round4/unreal/lod_before_249.6m_game_raw.png), [270.4 m LOD1 raw](round4/unreal/lod_after_270.4m_game_raw.png), [labelled Unreal pair](round4/unreal/switch_249p6_270p4_Unreal_comparison.png). Far views apply existing 120 m part culling; use the unculled totals above for budget accounting.

Before reference: round3_restart/comparisons/*_Blender_comparison.png. Earlier round4/comparisons/ Blender sheets predate the native paving/backing corrections and are intermediate evidence, not final Unreal results.

## Open defects and integration gate

1. Two paving-edge faces on the 3.9 mm FloorWear edge still produce invalid tangent frames. Exact FBX coordinates, face indices and UVs: [final_bad_tangent_faces.json](round4/final_bad_tangent_faces.json). The attempted edge correction did not clear them.
2. Import-setting finding: the original native importer used FBXNIM_ImportNormals, discarding stored tangents. Changing legacy import data to ImportNormalsAndTangents did not clear the warning: UI reimport ran through Interchange and replaced import data with InterchangeAssetImportData. This is an unresolved pipeline finding, not a verified fix. Unreal still reports nearly-zero tangents/binormals. Stored FBX tangent-vector validation alone is insufficient; recalculated Blender frames still fail on the edge.
3. Small bevels, fasteners, coping junctions and some under-fascia shading differ from the dense master. Exact 12 m parity has not been accepted.
4. Supplied stop counts and current disk receipts differ as documented above. Runtime LOD switching remains an integration check.

## Cleanup and shutdown

Round 1 cleanup deleted 44 packages only after their native referencer lists were empty. Kept Issue373_Round1/Textures/T_HQ373_Shell_AO and T_HQ373_Shell_NORMAL because six Round 2 parents reference them. [Deletion/retention receipt](round4/round1_cleanup.json). The 46-package pre-cleanup archive and hashes are in round4/archive_Issue373_Round1/ and round1_archive_receipt.json.

Kept Round 2 baseline/fixture dependencies, Issue195_PhaseB combined mast assets, current Issue373 candidate parents/material instances/textures/Release meshes/preview, and superseded Issue373 Meshes root, Final, Delivery, Verified and Diagnostics staging plus old parent copies. These staging packages are not integration inputs. No further cleanup ran at the time limit.

The approved preview level was saved through Save Current Level; its dirty marker cleared and the log validated that exact candidate level. The owned editor PID 47444 was then closed through its window close control. At the shutdown Save Content prompt, remaining asset changes were not saved; the preview level was already saved. See wrapup_shutdown.json for completion verification. No production map, git state, or protected generator was modified during wrap-up.
