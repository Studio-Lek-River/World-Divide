# USA HQ #373 — ROUND 3 (restarted)

**Façade-depth candidate for review.** Restarted from the accepted Round 2 candidate; the interrupted Round 3 run is not an input. No Unreal import or editor launch this round. Round 2 files and native packages remain unchanged.

## What changed

The façade fix is geometry. Restored approved-position side faces, end caps, reveals and backing planes for pilasters/primary piers, vertical and horizontal frame members, projecting sills, rebates, panel steps, roof fascia and coping. Glass remains at the master’s recessed positions. Buried backs can remain omitted; exposed members now have actual returns and cast shadows. The central pilaster opening is closed. A remaining window-seal sliver and one recessed header backing were replaced with complete master boxes.

LOD0 gained **5,539 triangles**: 2,439 initial returns, 2,988 additional returns/backings, 88 paving receiver triangles, and two 12-triangle jamb/header boxes. Geometry came from source/USA_HQ_Final.blend, retaining its depths and existing materials. Fine bevels remain map detail; broad returns use simple planar faces.

LOD1 gained 560 triangles on the largest pier/fascia/parapet returns. Removing 282 triangles of garage lining behind the closed door keeps the far budget. Other far geometry stays at its Round 2 positions. The new near-only small returns and paving repairs do not change the outer envelope.

Static junction louvres gained side faces. The moving garage door and its geometric slats remain the unchanged common asset on both comparison halves. West condenser/AHU/fan housing normals were reprojected against their own master components; the nearly black condenser cap is repaired.

44 approved courtyard paving tops had been rejected because their centres lay beneath planters. Restored those receivers, closing the dark/green holes around the planters. Existing UV0 is retained; source pavers without UV0 use the same world-XY fallback as Round 2.

## Bake and material handling

Rebaked the affected architectural groups against isolated matching master components. Frames, piers, coping, panels and reveals cannot project onto a neighbouring member. The per-component cages are 1.5–12 cm, chosen from bevel width; curved equipment uses a separate 25 cm reach. The two final box repairs use 2 cm cages. A missed straight ray at a bevel edge uses the nearest matching-component surface within that cage, rather than a flat-normal fallback.

The final architectural pass projects **893,409 / 134,128 texels** in sets 0 / 1, with **zero unresolved texels in both sets**. The equipment pass also has zero unresolved texels. The jamb/header passes abort on a projection miss and completed successfully. These are affected-group results, not certification of every inherited, untouched Round 2 bake pixel.

AO is restricted to the receiving component with a 6 cm reach. Inter-member contact shadows in the comparison come from the restored geometry. Large floors/paving/walkpads bypass the bake branch, preserving their original tiling surface. Paving footprints in the maps are neutral normal / white AO.

The façade atlas was repacked with connected UV islands and whole-island weighting, preserving the micro-detail strip. Unchanged set-0 detail was resampled into the new layout; affected façade detail was rebaked. LOD1 UV1 was transferred without changing its geometry: 7,333 exact triangles and 581 same-source, single-island transfers. UV0 is retained. Final exports have exactly two UV channels.

Area-weighted median density is **5.13 cm/texel** on the main/front façades and **2.74 cm/texel** on roof drain grilles (Round 2: 5.45 / 2.91). Courtyard crossbars remain 3.46 cm/texel. Small inner box-repair charts are coarse; their depth and shadow come from geometry. No extra texture set or 4K map was introduced.

Current production material scope remains M_Bld_Surface / M_Bld_Coating / M_Bld_Glass / M_Bld_Operational and the two wear parents. Their existing candidate copies retain the default-false **HQ373_UseShellBake**, with **HQ373_BakedNormal** and **HQ373_BakedAO**, sampled on UV1 in the UV0 MikkTSpace basis (DirectX negative green).

[Portable candidate overrides](round3_restart/candidate_material_overrides.json) specify 30 shell instances with the bake on, and these four with it off: MI_HQ14_Paving, MI_HQ19_Walkpad, MI_USA_HQ_HQ20_FloorWear, MI_USA_HQ_HQ21_PavingWear. The Blender preview applies those exclusions. No native material instances were changed; the saved Unreal Round 2 candidate still has its Round 2 textures/overrides.

## Exact delivery and counts

All paths below are relative to issue373/.

- Mesh/bake source checkpoint: [round3_restart/HQ373_Round3_Final.blend](round3_restart/HQ373_Round3_Final.blend).
- Bound proxy-material preview: [round3_restart/HQ373_Round3_Comparison.blend](round3_restart/HQ373_Round3_Comparison.blend).
- LOD0: [round3_restart/SM_HQ373_R3_Shell_Cm.fbx](round3_restart/SM_HQ373_R3_Shell_Cm.fbx).
- LOD1: [round3_restart/SM_HQ373_R3_Shell_LOD1_Cm.fbx](round3_restart/SM_HQ373_R3_Shell_LOD1_Cm.fbx).
- Maps: round3_restart/T_HQ373_R3_Set{0,1}_{NORMAL,AO}.png — **four 2048×2048 textures / two sets**.
- [Manifest](manifest_round3.json), [slot-to-set mapping](round3_restart/texture_set_mapping.json), [delivery hashes](round3_restart/delivery_hashes.json), [independent FBX checks](round3_restart/fbx_reimport_counts.json).
- Unchanged fixture batch remains round2/SM_HQ373_R2_Fixtures.fbx; all other unchanged assembly inputs remain as delivered in Round 2.

| Measure | Round 2 LOD0 / LOD1 | Round 3 LOD0 / LOD1 |
| --- | ---: | ---: |
| Shell, independently reimported FBX | 56,006 / 18,961 | **61,545 / 19,239** |
| Whole HQ, including unchanged assembly | 138,814 / 39,702 | **144,353 / 39,980** |
| Shell sections / ordered slots | 34 / 34 each | **34 / 34 each** |
| Whole HQ sections | 80 each | **80 each** |
| Zero-area faces / UV0 faces / invalid tangent frames | — | **0 / 0 / 0 each** |

Whole totals add the accepted unchanged assembly accounting of 82,808 / 20,741 triangles. They are not counts of the portable-density comparison scene. LOD0 leaves **5,647 triangles** under 150k for #394; LOD1 leaves **20** under 40k. All 34 slot names and their order match Round 2. No visible material slots were merged. Export retains the centimetre FBX representation: FBX_SCALE_NONE, bake_space_transform=True, X forward / Z up. Round 3 counts have not been checked in Unreal, as requested.

## Blender comparison — before / after index

Every sheet is labelled **Blender comparison**, never Unreal. Only the five requested game views were rendered. The unchanged approved-master raw pixels from Round 2 are reused on the left, with the same cameras, neutral daylight, proxy material values and common parts. Sheets preserve the original 1920×1080 pixels per side (3840×1080 total). Close_12m retains the 42 m production-style arm; detail arms are 12 m; default_150m uses 156 m. [Camera manifest](round3_restart/comparisons/camera_manifest.json) · [Provenance](round3_restart/comparisons/evidence_provenance.json).

| View | Before (Round 2) | After (Round 3) |
| --- | --- | --- |
| facade_frame_12m | [sheet](round2/comparisons/facade_frame_12m_Blender_comparison.png) | [sheet](round3_restart/comparisons/facade_frame_12m_Blender_comparison.png) |
| close_12m | [sheet](round2/comparisons/close_12m_Blender_comparison.png) | [sheet](round3_restart/comparisons/close_12m_Blender_comparison.png) |
| drain_grilles_12m | [sheet](round2/comparisons/drain_grilles_12m_Blender_comparison.png) | [sheet](round3_restart/comparisons/drain_grilles_12m_Blender_comparison.png) |
| parapet_edge_12m | [sheet](round2/comparisons/parapet_edge_12m_Blender_comparison.png) | [sheet](round3_restart/comparisons/parapet_edge_12m_Blender_comparison.png) |
| default_150m | [sheet](round2/comparisons/default_150m_Blender_comparison.png) | [sheet](round3_restart/comparisons/default_150m_Blender_comparison.png) |

## Scoped checks and remaining defects

The façade ray-depth check uses an eight-pixel grid in the inspected WingFront E4 region. See-through samples fell from **107 to zero**. Three cap/drip samples still differ in ray depth by 4.5–9.2 cm: RoofDrip_Section1 and two PrimaryPier caps. This is a targeted gap check, not a whole-building visibility re-audit. [Receipt](round3_restart/facade_gap_check.json).

1. Small bevels and fasteners remain harder/faceted or aliased compared with the dense master, especially at parapet/coping junctions. Some dark under-fascia seams and small cap profiles still differ. Depth is restored; exact master parity is not claimed.
2. Paving tile-edge contrast remains somewhat stronger than the master. The large floor bake is bypassed, and restored receiving tops remove the planter-adjacent holes; the remaining difference needs edge/lighting judgement in the realism round.
3. The repack preserves grille geometry and gives it slightly better density, but fine mip behaviour and production material blending are untested in Unreal. Some inherited, unaffected equipment/hardware bake pixels remain from Round 2.
4. Round 2’s approved switch comparison is the baseline. Round 3 adds far returns and transfers UV1; its automatic synchronized 260 m switch needs final-round validation. No 450 m/switch/flagpole sheet was regenerated this round.
5. Native import/count agreement, candidate level save and Unreal captures remain for the final round. No production readiness or new visual approval is asserted.

## Reproduction and scope

The canonical files are listed above and in manifest_round3.json. Other checkpoints/logs and failed probe variants under round3_restart/ are diagnostics, not alternate deliveries. New generators are Tools/Art/usa_hq_373_r3_restart_*.py; the staged sequence is extract_v2 → depth_v2 (starts from Round 2) → equipment → extract_v5 → closure_v3 → floor_probe_v2 → floor_v2 → jamb → head_backing → verify_final / render_final / density. These are staged transforms; begin from Round 2 when rebuilding, rather than replaying them against the delivered final.

No git operation, commit, push, production Content edit or L_TestMap edit was performed. No editor process was started or stopped. No DDC/shader cache was created in Art. Native staging packages and all production assets remain unchanged.
