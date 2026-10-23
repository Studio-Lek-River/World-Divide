# USA HQ #373 — ROUND 2

**Review candidate delivered; not visually accepted or ready for production integration.** Background Blender comparisons replace Unreal level evidence this round, as instructed. No level-save attempt or L_TestMap edit was made.

## Delivery

Paths below are relative to this issue373 folder.

- Editable mesh/bake checkpoint: [round2/HQ373_Round2_Final.blend](round2/HQ373_Round2_Final.blend).
- Matched comparison scene: [round2/HQ373_Round2_Comparison.blend](round2/HQ373_Round2_Comparison.blend).
- Final shell FBXs: [round2/SM_HQ373_R2_Shell_Cm.fbx](round2/SM_HQ373_R2_Shell_Cm.fbx), [round2/SM_HQ373_R2_Shell_LOD1_Cm.fbx](round2/SM_HQ373_R2_Shell_LOD1_Cm.fbx).
- Unchanged fixture batch: [round2/SM_HQ373_R2_Fixtures.fbx](round2/SM_HQ373_R2_Fixtures.fbx).
- Bake maps: round2/T_HQ373_R2_Set{0,1}_{NORMAL,AO}.png — four PNGs, each 2048×2048.
- [Manifest](manifest_round2.json), [SHA-256 hashes](round2/delivery_hashes.json), [independent FBX checks](round2/fbx_reimport_checks.json), [saved native candidate](round2/native_final_candidate.json).

Final Unreal meshes:

- LOD0: /Game/Art/Candidates/USA_HQ/Issue373_Round2/Meshes/SM_HQ373_R2_Shell_Delivery
- LOD1: /Game/Art/Candidates/USA_HQ/Issue373_Round2/Meshes/SM_HQ373_R2_Shell_LOD1_Delivery
- Fixed fixtures: /Game/Art/Candidates/USA_HQ/Issue373_Round2/Meshes/SM_HQ373_R2_Fixtures_Batch

These are separate one-LOD review assets. They are not a configured runtime LOD pair. Materials and final bake textures are in sibling Materials/ and Textures/ folders under /Game/Art/Candidates/USA_HQ/Issue373_Round2/. No candidate umap was authored; the producer/integrator will arrange the final-round level save.

## What was made

The mast-stripped game/SM_HQ_Shell.fbx is the dense bake source. Structural faces come from the approved USA_HQ_Final.blend. LOD1 uses planar roof/façade surfaces, recesses and parapet profiles, with local reduction confined to curved equipment and small hardware. There is no whole-shell collapse or 5 cm weld. The H28 masts, moving parts, shared flag/beacons and shell-detail geometry remain outside the shell edit. Two small static DoorPaint/Rubber slot surfaces retain the existing Round 1 geometry; they are copied identically into both LODs.

The parapet inspection exposed broad roofs incorrectly rejected by a face-centre visibility test. The final rebuild protects 474 matching approved roof/trim planes (996 input triangles before local simplification). See [roof restoration](round2/roof_restore_receipt.json). Roof plane positions are inherited, not averaged.

LOD1 reuses 15015 exact LOD0 structural triangles, including their UV0/UV1, instead of independently projecting each corner across atlas islands. Curved details use one source island per far triangle. This removes the diagonal atlas streaks seen during the intermediate check. See [far UV receipt](round2/far_uv_final.json). Protected-plane normals are compared with approved source normals in [geometry audit](round2/final_geometry_audit.json), with zero reversed structural faces in either LOD.

## Counts

| Measure | LOD0 | LOD1 |
| --- | ---: | ---: |
| Final Blender FBX reimport | 56,006 | 18,961 |
| Final native Unreal import | 56,006 | 18,961 |
| Shell non-empty sections / slots | 34 / 34 | 34 / 34 |
| Degenerate triangles / zero UV0 tangent loops, delivered FBX space | 0 / 0 | 0 / 0 |
| Proposed whole HQ | **138,814** | **39,702** |
| Headroom against 150k / 40k | 11,186 | 298 |

Whole-HQ totals add unchanged assembly geometry (82,808 / 20,741 triangles), including all instances, from the existing assembly receipt. They are assembly accounting, not a rendered-performance measurement. The near reserve exceeds the requested 5,000 triangles for #394.

**80 unique instanced mesh-slot sections** per LOD. The shell keeps all 34 names and their order. The four static #195 fixtures retain 352 triangles and their visible materials, but share one batch with three sections (DarkStructure, FixtureWarm, BeaconAmber), replacing four unique mesh-slot sections. No different visible materials were merged. The section budget is fully used. The fixture FBX independently reimports as 352 triangles / 3 sections.

**Two bake sets, four active 2K textures; no 4K maps.** Set 0 prioritises façade/frame/glass/drain material slots; set 1 carries the remaining roof, hardware and ground slots. [Exact slot-to-set mapping](round2/texture_set_mapping.json). Existing tiling maps are inherited. Native final textures use linear sampling, TC_Normalmap / TC_Masks, TMGS_FromTextureGroup and NeverStream=false; Nanite is off.

### Import loss and tangents

The 629-triangle Round 1 loss is isolated to FBX unit/space representation. Re-exporting the unchanged 60,579-triangle Round 1 mesh with centimetre control points preserves all 60,579 in Unreal. The test removed no faces. [Unit comparison receipt](round2/native_unit_test.json).

Use apply_scale_options=FBX_SCALE_NONE and bake_space_transform=True. The delivered FBXs use that representation; the existing production exporter must preserve it during integration. Near/far UV0 repairs and sub-1 mm needle removal are recorded in [tangent repairs](round2/tangent_repairs.json). The final native shell imports emit no tangent/binormal warnings. Tangents are checked in the centimetre FBX control-point space used by Unreal. A destructive diagnostic that applies a metre-space float32 transform introduced 16 near-zero recalculated near loops; that altered representation is not the delivery. Retessellating the long thin strips would make downstream unit conversion more robust.

## Removed-piece visibility audit

The audit checks 4607 small source objects against the requested 42/156 m and 12 m detail cameras. 89 pieces without a demonstrated low receiver were restored in both LODs. The final omissions are 627 near / 2748 far pieces; every remaining omitted piece has a demonstrated low receiving surface. The decision uses visibility and projected pixels, not a blanket 20 cm cutoff.

Removed families are attached anchors, panel/frame fixings, washers, bolts and similar supported hardware. The full per-piece list includes bbox, requested-view visibility, projected size, near/far decision and receiving source/position: [removed pieces](round2/removed_piece_receivers.json), [family counts](round2/removed_piece_summary.json), [restored pieces](round2/restoration_receipt.json). Slats and the flagpole remain geometry. The 24 mm radome equatorial seam remains near geometry and is omitted only at far distance (about 0.15 px vertical profile at 260 m), with the radome as receiver: [extra far omission](round2/far_extra_omissions.json).

## UV1 and bake

| Region | Area-weighted cm/texel |
| --- | ---: |
| Main façades | 5.45 |
| Front +X façades | 5.45 |
| Roof drain grilles | 2.91 |
| Courtyard drain crossbars | 3.46 |
| Courtyard floor | 7.70 |

[Region measurements](round2/texel_density_regions.json) are geometric density, not a claim that sub-texel bevels survived. The second 2K set was needed and is used. Façades and grilles receive priority over the courtyard; retained micro hardware occupies a small atlas strip. Packing remains inefficient around disconnected thin islands and needs improvement. At 12 m the façade/frame and parapet bevel response is still softer than the master.

High-to-low normals are baked in object space on UV1, then converted per texel into the exported UV0 MikkTSpace basis, with DirectX negative green. AO is a local high-object bake with 30 cm reach, avoiding a second whole-building ambient shadow. Three-pixel dilation is applied. UV1 is unique within each material-assigned texture set; the LODs reuse the maps. [Map audit](round2/map_conversion_report.json) records atlas coverage, boundary-pixel overlaps and flat-normal fallbacks: 55,006 / 279,732 texels rejected as back-facing/missed projections for sets 0 / 1. Those fallbacks remain a quality defect.

## Material scope

The current production parents are M_Bld_Surface, M_Bld_Coating, M_Bld_Glass, M_Bld_Operational and the two wear parents. The candidate uses copies of those six graphs, carrying the approved Round 1 bake branch. The operational copy now uses its current M_Bld_Operational name. Production parents were not edited.

HQ373_UseShellBake defaults false on every copied parent and is true only on the 34 candidate shell instance copies. HQ373_BakedNormal and HQ373_BakedAO sample UV1. The true normal branch normalises TilingNormal + BakedNormal − (0,0,1); AO multiplies original AO by baked R. Parameter names are unchanged. [Parent defaults](round2/parent_switch_audit.json), [final saved overrides/settings](round2/final_persistence_audit.json), [material bindings](round2/candidate_materials.json). Verification is in the same editor session, not a fresh-process material certification.

## Blender comparison image index

**All images are labelled Blender comparison. None is an Unreal capture.** Sheets are unscaled side-by-side renders; raw 1920×1080 master/game images and [camera parameters](round2/comparisons/camera_manifest.json) remain beside them. The production PNGs in Saved/BuildingShots/DA_TestHeadquarters were used for framing by eye. Horizontal FOV is 60°; close_12m uses a 42 m arm and −35° pitch; default_150m / far_450m use 156 / 456 m and −55° pitch. The switch arms are 249.6 / 270.4 m. Additional detail arms are 12 m.

Neutral daylight and identical proxy material values are used on both sides. Unchanged parts are held constant at portable-source density to isolate the shell; these images are not production shading, runtime LOD synchronisation, flag animation or gameplay evidence. The flag has the same neutral proxy on both sides.

| View | Path |
| --- | --- |
| parapet_edge_12m | [round2/comparisons/parapet_edge_12m_Blender_comparison.png](round2/comparisons/parapet_edge_12m_Blender_comparison.png) |
| close_12m | [round2/comparisons/close_12m_Blender_comparison.png](round2/comparisons/close_12m_Blender_comparison.png) |
| default_150m | [round2/comparisons/default_150m_Blender_comparison.png](round2/comparisons/default_150m_Blender_comparison.png) |
| far_450m | [round2/comparisons/far_450m_Blender_comparison.png](round2/comparisons/far_450m_Blender_comparison.png) |
| lod_before_249.6m | [round2/comparisons/lod_before_249.6m_Blender_comparison.png](round2/comparisons/lod_before_249.6m_Blender_comparison.png) |
| lod_after_270.4m | [round2/comparisons/lod_after_270.4m_Blender_comparison.png](round2/comparisons/lod_after_270.4m_Blender_comparison.png) |
| switch_same_260m_LOD0 | [round2/comparisons/switch_same_260m_LOD0_Blender_comparison.png](round2/comparisons/switch_same_260m_LOD0_Blender_comparison.png) |
| switch_same_260m_LOD1 | [round2/comparisons/switch_same_260m_LOD1_Blender_comparison.png](round2/comparisons/switch_same_260m_LOD1_Blender_comparison.png) |
| drain_grilles_12m | [round2/comparisons/drain_grilles_12m_Blender_comparison.png](round2/comparisons/drain_grilles_12m_Blender_comparison.png) |
| flagpole_12m | [round2/comparisons/flagpole_12m_Blender_comparison.png](round2/comparisons/flagpole_12m_Blender_comparison.png) |
| facade_frame_12m | [round2/comparisons/facade_frame_12m_Blender_comparison.png](round2/comparisons/facade_frame_12m_Blender_comparison.png) |
| switch_249p6_270p4 | [round2/comparisons/switch_249p6_270p4_Blender_comparison.png](round2/comparisons/switch_249p6_270p4_Blender_comparison.png) |
| switch_same_260m | [round2/comparisons/switch_same_260m_Blender_comparison.png](round2/comparisons/switch_same_260m_Blender_comparison.png) |

The fixed 260 m pair separates the mesh switch from the arm-distance change. It is supplied for visual review; an indistinguishable switch is not claimed as accepted.

## Known defects and next changes

1. Close façade/frame/parapet bevels remain softer than the dense master, with local bake blotches/aliasing. Improve island packing and per-component cages, then spend part of the near reserve on the visible bevel geometry. Preserve at least 5k for #394.
2. Curved far details use a clamped single-island approximation. Structural streaks and roof spikes are repaired, but small equipment shading/silhouette changes remain to judge in the fixed-camera pair. Shell max Z is 13.433 m near versus 13.208 m far; the curved-detail extrema still need matching. Do not describe this as visual acceptance.
3. Projection fallbacks and tiny hardware UVs need refinement; coarse mips and real Unreal sampling are not yet validated. No larger-than-2K texture approval is assumed.
4. Long needlelike strips are sensitive to destructive metre-space reprocessing. Preserve the centimetre exporter flags; retessellate if the integration path needs another representation.
5. Actual Unreal level/captures are deferred to the final round per the critique. Runtime 260 m switching, lighting/tiling parity, motion, collision/navigation/selection and performance remain integrator checks.

## Housekeeping, reproduction and preservation

issue373/ddc and issue373/shaderwork were moved out of Art and are absent. Their contents are under Saved/Codex373/Round2/round1_ddc and round1_shaderwork; this editor used Saved/Codex373/Round2/ddc and shaderwork. [Move receipt](round2/cache_move_receipt.json).

Superseded staging packages were retained as permitted: 46 in Issue373_Round1 and 25 in Issue373_Round2. [Exact package list](round2/staging_packages.json). Some copied parent default sampler references still point to the historical Round 1 texture defaults; active instance overrides use the final Round 2 maps. Do not delete dependencies during this round.

New generators only were written under Tools/Art/usa_hq_373_r2_*. The sequence is structural extraction → roof-plane restoration → local build/visibility restoration → UV/bake finish → UV0 normal conversion/local AO → exact far structural UV reuse → centimetre FBX verification → background render/layout. The native dispatcher recipe is usa_hq_373_r2_native.mjs. Diagnostic probes are not production inputs. Neither usa_hq_game_blender.py nor usa_hq_recipe.py was edited.

The approved master, dense shell, shell-detail files and HANDOFF_379.md retain their hashes: [input integrity](round2/input_integrity.json). No production Content or L_TestMap was edited, and no git operation, commit or push was performed. Owned editor PID 6804 was closed after candidate saves; [lifecycle receipt](round2/editor_lifecycle_round2.json). Blender thumbnail-cache warnings did not prevent the delivered files or labelled renders from being saved.
