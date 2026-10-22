# Issue 379 — mast review handoff

**Chosen and visually approved: H28 — core 28 m, east 24 m; beacon scale 1.75.** Producer receipt (2026-10-22): “I opt for the middle (core 28, east 24)”. Source and saved preview use this option. Earlier stages remain approved: “All earlier HQ stages are approved; preserve every design except the masts.” Integration remains with Claude.

[150 m options](renders/issue379/Height_Options_150m.png) · [Proportion overview](renders/issue379/Height_Options_Overview.png)

| Option | Core / east tip Z (cm) | Beacon scale XYZ | Evidence |
|---|---:|---:|---|
| H24 | 2400 / 2100 | 1.5 | [Master/game](renders/issue379/H24_review.png), [260 m pair](renders/issue379/H24_switch.png), [reference](renders/issue379/H24_reference_comparison.png) |
| H28 | 2800 / 2400 | 1.75 | [Master/game](renders/issue379/H28_review.png), [260 m pair](renders/issue379/H28_switch.png), [reference](renders/issue379/H28_reference_comparison.png) |
| H32 | 3200 / 2700 | 2 | [Master/game](renders/issue379/H32_review.png), [260 m pair](renders/issue379/H32_switch.png), [reference](renders/issue379/H32_reference_comparison.png) |

Core foot is **(-898.712, 0, 1151.000) cm**; east foot is **(-1090.000, -1995.000, 770.000) cm** in building space. Place meshes at these feet with identity rotation and scale 1. Tip XY equals foot XY; tip Z is above. Existing `SM_Beacon_Obstruction` is placed at each tip and scaled uniformly per table. The chosen beacon geometry extends 98 cm above its tip (56 cm source height × 1.75); cover at least Z=2898 cm when rebuilding the whole-building bounds. Reset inherited production bounds extensions before recalculating them. Its existing red emissive material is reused.

`MastTipCore` / `MastTipEast` empties sit on the rod tips; FBX helper nodes use the `SOCKET_` prefix. Local pivot is the mounting foot, not the mesh bounds centre. The east mounting pads extend slightly below that datum. **Drop the stale `MastTip` (-1080, 0, 1800) during integration**: it identifies neither new tip. Do not retain it as a third beacon attachment.

## Portable delivery

[Final source](source/USA_HQ_Final.blend); [all SHA-256 hashes](issue379/delivery_hashes.json); [exact per-LOD bounds, slots, tips and hashes](issue379/mast_manifest.json); [independent FBX reimport checks](issue379/fbx_reimport_checks.json). Recovery originals are in `issue379/baseline/`.

| FBX (under this folder) | LOD | Triangles | SHA-256 |
|---|---:|---:|---|
| [SM_HQ22_MastCore_H24](fbx/SM_HQ22_MastCore_H24.fbx) | 0 | 3,772 | `72242054ab06ecf3cea2b89fe502861e646ae610ff08f5799d5490efe71a3926` |
| [SM_HQ22_MastCore_H24_LOD1](fbx/SM_HQ22_MastCore_H24_LOD1.fbx) | 1 | 1,404 | `c6fee5e703f351bf446f421df99ebd6c45efea48d0b000012ad0eca055424687` |
| [SM_HQ22_MastEast_H24](fbx/SM_HQ22_MastEast_H24.fbx) | 0 | 5,780 | `144123c4f17ad8c20896fef525da5db1dd44ee1bc735ef19980d24bc6972d389` |
| [SM_HQ22_MastEast_H24_LOD1](fbx/SM_HQ22_MastEast_H24_LOD1.fbx) | 1 | 2,700 | `7a629a880a8800d1befa8e2bb6912f2eb836b6820834411388c8a177c27a994c` |
| [SM_HQ22_MastCore_H28](fbx/SM_HQ22_MastCore_H28.fbx) | 0 | 3,772 | `81894717f5946176a270cf8b0307669e25e6607ec7e03766b5ad3ce6f50eaef0` |
| [SM_HQ22_MastCore_H28_LOD1](fbx/SM_HQ22_MastCore_H28_LOD1.fbx) | 1 | 1,404 | `87bea7b7efc32ee7e1a2cb1a475a729086d4b95e05a6ef14d19894fa411d05cd` |
| [SM_HQ22_MastEast_H28](fbx/SM_HQ22_MastEast_H28.fbx) | 0 | 5,780 | `b3093a8a2e197cb48abce2155e650d764f0e801c43604187bd56176eadaf1434` |
| [SM_HQ22_MastEast_H28_LOD1](fbx/SM_HQ22_MastEast_H28_LOD1.fbx) | 1 | 2,700 | `df7f77e9e778ef2a71839d865c532f7bc47613e823af59055a82b3615827e87d` |
| [SM_HQ22_MastCore_H32](fbx/SM_HQ22_MastCore_H32.fbx) | 0 | 3,772 | `ec541650d71ac51d7f25399f32880baa5c9e229c66296b86436293c090d0502f` |
| [SM_HQ22_MastCore_H32_LOD1](fbx/SM_HQ22_MastCore_H32_LOD1.fbx) | 1 | 1,404 | `c82f9f9460c4486cec1add653ca9bc4e69b36df70050de303f8c740f669bfc70` |
| [SM_HQ22_MastEast_H32](fbx/SM_HQ22_MastEast_H32.fbx) | 0 | 5,780 | `7b45b202b56ba2b4a28ae72e099265d3c69ff9dfbd9d69fb2f8a88bf0ce5dc82` |
| [SM_HQ22_MastEast_H32_LOD1](fbx/SM_HQ22_MastEast_H32_LOD1.fbx) | 1 | 2,700 | `61d42c6649af15ccd88c2cee00eac4703199b9a8a7ffe03889b580fbb8efdfbc` |

Each mast/LOD uses the same three slots, in order: `HQ17_DarkStructure`, `HQ17_FramePaint`, `HQ17_TeamLight`, bound to the corresponding `MI_` instance under `/Game/Art/USA/Buildings/USAHQ/Materials/`. FramePaint and TeamLight remain; other original mast surfaces share DarkStructure. This mast-only material consolidation adds six sections to the established 74-section assembly, reaching its existing 80-section limit when production instancing is preserved. The preview uses individual actors and is not a draw-call benchmark; see `issue379/preview_dependencies.json`. No new textures. Existing texture sizes/settings are inherited, not re-authored.

Both hand-made LOD1s retain every mast component, rung, rod and panel; radial cylinders become four-sided prisms, boxes remain. No automated collapse of the masts. Independently reimported FBXs have matching counts, pivots, helper positions and slots, one UV layer and zero degenerate mast triangles. Non-mast master meshes: **20,161 unchanged geometry/transform/material hashes**.

## Shell and budget

`Tools/Art/usa_hq_game_blender.py` now removes the recorded original mast surfaces before the shell/detail split and LOD generation; the three `game/` FBXs were re-exported. The surface mask accommodates triangulated/subdivided roof exports and rejects area mismatches; it does not remove a broad bounding box. The two fixed rooftop feeder-return pieces remain.

| Mesh / measure | Old | New |
|---|---:|---:|
| Shell dense input FBX | 2,938,860 | 2,915,364 |
| Shell game LOD0 (engine reduction) | 274,000 | 267,000 |
| Shell game LOD1 (engine reduction) | 22,000 | 19,000 |
| ShellDetail LOD0 | 42,096 | 41,344 |
| ShellDetail LOD1, final reimported FBX | 11,878 | 10,655 |
| Separate core LOD0 / LOD1 | merged | 3,772 / 1,404 |
| Separate east LOD0 / LOD1 | merged | 5,780 / 2,700 |
| Whole HQ LOD0, drawn instances | 347,768 | **349,568** |
| Whole HQ LOD1, drawn instances | 39,151 | **39,032** |

Dense FBX input is the reduction source, not the runtime mesh. The budgeted near/far shell reductions reserve room for the separate masts within 350,000 / 40,000 triangles. Counts include every door section and both beacons; per-component receipts are `issue379/counts_game.json` and `counts_far.json`. Use these measured counts instead of the older import report table. `issue379/baseline_unreal_counts.json` records the live old asset counts. The initial pre-export detail count included 369 faces discarded by FBX tessellation. Final counts now come from a round-trip of the exported mesh.

## Unreal evidence and integration boundary

**Known preview failure — not LOD acceptance:** the direct dense-to-19,000-triangle shell reduction loses exterior panels and produces a visible colour/surface change at the far switch. The supplied stripped dense shell and approved mast geometry are intact. Native `generate_lods` cannot set the production pipeline’s reduced LOD0 / progressive LOD1 settings; its property/UI tools did not expose those controls. Claude must rebuild through the existing `building_art.import_mesh` progressive path at 267,000 / 19,000 and revalidate the switch; this is not a claim that the progressive result is already proven. The captured far views are retained as failed evidence. Producer approval here covers the 28/24 m height choice, not this far-shell defect.

Candidate preview: `/Game/Art/Candidates/USA_HQ/Candidate22/L_HQ379_Masts`. Final masts: `Candidate22/FinalMasts/`. It reuses read-only production materials and unchanged moving meshes; it is not a self-contained production package. Native MCP lacks hand-authored LOD-file import, so LOD0/LOD1 mast FBXs are separate preview assets and all pieces are manually switched together. `Shells/SM_HQ379_ShellReview` contains dense master at index 0 and near reduction at index 1; `SM_HQ379_ShellFar` contains dense master at index 0 and far reduction at index 1. **Do not integrate these staging LOD indices as runtime settings.** Claude should import each selected pair as runtime LOD0/LOD1, reduce the stripped shell to the measured budgets, and update shared bounds/switch thresholds for the new tallest tip.

Captures are actual **Unreal editor views**, not PIE/gameplay. Raw PNGs and camera JSON are beside the review images. Native capture renders horizontal FOV 90; a centred pixel crop gives effective FOV 60 at 16:9 for RTS views; proportion overviews retain the full vertical context. 12 m is a mast-tip detail camera, 150/450 m target the building, and the switch pair uses arms 249.6/270.4 m. This demonstrates selected LOD appearances, not the automatic bounds-distance transition. `Tools/Art/usa_hq_379_preview.py` controls candidate-only poses/captures; `usa_hq_379_evidence.py` makes labelled crops/layouts without synthesis.

Unverified / integration work: actual runtime camera, automatic synchronized 260 m switch with expanded bounds, beacon blink/power/ownership logic, collision/navigation/selection and representative GPU performance. Native socket serialization is not exposed by this MCP; FBX helpers were reimport-verified, while candidate beacon placement uses the documented transforms. Final mast packages and their material bindings were checked after a fresh editor process; the completed assembly is checked by saving and reloading its candidate level. The reported shell-detail tangent/binormal warnings were repaired: 65 UV faces corrected and 412 needle faces narrower than 1 mm removed; surviving face positions and material assignments are unchanged. Independent FBX reimport recomputes zero invalid tangent frames. See `issue379/tangent_repair.json`, `tangent_diagnostics.json`, and the fresh Unreal import receipt `unreal_tangent_import_check.json`. The candidate uses `Shells/SM_HQ_ShellDetail_LOD1_Final`; the older warning-producing staging asset is superseded. No production setup, recipe/spec update, commit or push was performed.

Claude integration starter (chosen H28): “Use `$world-divide-building-production` for USA HQ #379. Read `Art/USA/buildings/usa_hq/HANDOFF_379.md`; use the approved H28 pair (core 28 m, east 24 m) and preserve all prior approvals. Integrate the supplied mast LOD pairs, stripped shells, new tip transforms, existing beacon/material bindings and updated budgets/bounds. Retire stale MastTip, update recipe/spec and validate runtime switch and beacon behaviour.”

## Mast bounds

LOD0 local bounds in metres below; exact LOD1 bounds are in the linked manifest. Convert to Unreal cm and mirror Y when mapping the exported source coordinates.

| Mesh | Local minimum (m) | Local maximum (m) |
|---|---|---|
| SM_HQ22_MastCore_H24 | (-1.755, -2.130, 0.000) | (1.755, 2.130, 12.490) |
| SM_HQ22_MastEast_H24 | (-1.020, -1.590, -0.134) | (1.020, 1.590, 13.300) |
| SM_HQ22_MastCore_H28 | (-2.048, -2.485, 0.000) | (2.047, 2.485, 16.490) |
| SM_HQ22_MastEast_H28 | (-1.103, -1.855, -0.164) | (1.103, 1.855, 16.300) |
| SM_HQ22_MastCore_H32 | (-2.340, -2.840, 0.000) | (2.340, 2.840, 20.490) |
| SM_HQ22_MastEast_H32 | (-1.261, -2.120, -0.194) | (1.261, 2.120, 19.300) |

Final warning fix: fresh native Unreal import of `SM_HQ_ShellDetail_LOD1_Final` passed with **zero tangent/binormal warnings**, 10,655 triangles. The selected H28 far captures were refreshed after this cleanup; H24/H32 far images retain the earlier 11,067-triangle detail mesh. The full shell generator was not rerun after the final sliver cleanup; its updated repair function was run on the delivered export and all 15 delivery FBXs were independently reimport-checked.
