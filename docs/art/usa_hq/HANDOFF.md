# USA HQ continuation handoff — final Candidate22

## Final material/motion delivery — Candidate22, 2026-10-21

**Current authoritative candidate: `/Game/Art/Candidates/USA_HQ/Final`. Final milestone awaits producer visual approval; gameplay integration remains Claude's responsibility.** Earlier milestone approvals are preserved.

- Source: `Art/USA/buildings/usa_hq/candidate22/source/USA_HQ_Final.blend`.
- Portable exports: `Art/USA/buildings/usa_hq/candidate22/fbx/` (22 FBXs), matching GLBs under `source/`, nine PNGs under `textures/`.
- Single preview level: `/Game/Art/Candidates/USA_HQ/Final/L_USA_HQ_Final`.
- Complete implementation prompt: `Art/USA/buildings/usa_hq/candidate22/CLAUDE_IMPLEMENTATION_PROMPT.md`.
- Visual/motion gallery: `candidate22/REVIEW.md`; intake: `candidate22/DELIVERY.json`.
- Verified materials, ownership/light isolation, window depth, flag normals, equipment pivots, 24-second door loop, LODs, mipmapped textures, source helpers and clear geometric routes. Exact settings and inventories accompany the handoff.
- Native cleanup completed: 93 final assets, one World; no old HQ candidates, missing project dependencies or redirectors. Recovery: `candidate22/recovery/USA_HQ_PreFinal_Unreal.zip`; per-package receipt `candidate22/audit/cleanup.json`. Historical Art sources/exports/evidence preserved.
- Final save/reload and all persisted material parents verified in `candidate22/audit/final_reload.json`. An unpersisted ownership-parent dependency was repaired after cleanup; an intervening D3D12 crash required an editor restart. Final evidence supersedes earlier pre-cleanup receipts.
- Baseline: RTX4090/Ryzen7800X3D,2703×1270,300 frames per variant,~8.33ms frame/~4.3–4.4ms GPU; editor-only. 22 meshes,38 components,111 slots,172 sections;3.242M full-detail/199k lowest-detail instanced triangles;15.33MiB estimated texture chains. No gameplay-performance claim.
- Claude still implements authoritative setup/reimport, collision/navigation/selection, placement, power/player state, production/spawn/exit/door control, and representative gameplay optimization and acceptance tests. Actual production RTS camera/culling tests remain outstanding.

All entries below are historical; their Unreal paths are superseded and may now exist only in the recovery archive.

Updated 2026-10-21. **Candidate21 courtyard is visually approved by the producer: “Its approved by me” (2026-10-21).** Receipt: `Art/USA/buildings/usa_hq/candidate21/approval.json`. Architecture, tower/entrance, frames/façades/walls, roofs/communications and Candidate20 garage remain approved. The producer clarified that the garage looks good as it stands: its remaining work is implementation, not visual redesign.

## Authoritative delivery

- Review gallery: `Art/USA/buildings/usa_hq/candidate21/REVIEW.md`.
- Intake manifest: `Art/USA/buildings/usa_hq/candidate21/DELIVERY.json`.
- Reusable Blender source, with no exterior terrain: `Art/USA/buildings/usa_hq/candidate21/source/USA_HQ_Candidate21_Delivery.blend`.
- Authoring/recovery source: `candidate21/source/USA_HQ_Candidate21_Courtyard.blend`. This file retains a separate preview-only terrain collection; do not export that collection into the building.
- Final slab-only Unreal level: `/Game/Art/Candidates/USA_HQ/Candidate21/L_HQ21_Courtyard_Daylight`.
- Attached meshes under `/Game/Art/Candidates/USA_HQ/Candidate21/Meshes/`: `SM_HQ21_Courtyard`, `SM_HQ21_CourtyardDetails_Delivery`, `SM_HQ21_PlantingFinish_Delivery`, `SM_HQ21_NationalFlag_Wind`.
- FBX files: `candidate21/fbx/SM_HQ21_Courtyard.fbx`, `SM_HQ21_CourtyardDetails.fbx`, `SM_HQ21_PlantingFinish.fbx`, `SM_HQ21_NationalFlag.fbx`; matching portable GLBs are under `candidate21/source/`.
- Materials/textures: `Candidate21/Materials/MI_HQ21_*`, `M_HQ21_PavingWear`, `M_HQ21_Soil`, `M_HQ21_FlagWind`; paving and soil source PNGs under `candidate21/textures/`. Final mesh-slot bindings are recorded in `candidate21/audit/unreal_verification.json`.
- Preserved building assembly still references Candidate20's `SM_HQ20_Architecture_WallCorrected`, `SM_HQ20_RoofSystems_WallCorrected` and approved garage assets, plus Candidate19 moving roof assemblies and inherited Candidate17 materials. Keep these dependencies until Claude integrates.

## Ground and flag instructions from the producer

The building includes only the **56 × 38 m concrete slab and attached courtyard**. Map terrain may use any ground texture. No surrounding dirt is part of the reusable delivery source or final preview level. `SM_HQ21_PreviewTerrain`, its material/texture and earlier terrain screenshots are unused development context and explicitly excluded from intake. Planter soil stays attached.

The producer requested a large, naturally waving American flag. Final fabric is **3.42 × 1.80 m**, at the existing new courtyard mast; hoist top is 6.9 m and mast finial 7.1 m. `M_HQ21_FlagWind` uses two travelling waves, UV0.U squared as a pinned-edge weight, and a maximum displacement of 22.14 cm along the main wind axis. The hoist stays fixed. This is native material WPO animation, not cloth simulation. The final flag import uses UV weights because Unreal's default FBX vertex-colour mode is Ignore. Source vertex colours are supplemental, not required. Preserve UV0 on the flag through optimization. The material currently uses preview wind direction/speed; Claude can connect it to the production wind system.

## What changed and evidence

Courtyard work adds individual concrete slabs and recessed joints, constructed edge courses and stair treads/grip strips, planter panels/coping/weep sleeves, textured soil and low branched planting, an anchored flagpole base, flush drains, and restrained edge cabinets, cases, barriers and bolted bollards. Unique full-footprint paving wear avoids repeating dirt motifs. The approved courtyard layout, entrance axis, steps/plinth foundations and inherited helpers are retained. New materials do not add player tint; inherited ownership accents remain unchanged. National-flag colours are fixed.

Final actual Unreal PNGs under `candidate21/renders/`: `unreal_courtyard.png`, `unreal_paving_drainage.png`, `unreal_plinth_flagbase.png`, `unreal_planter.png`, `unreal_entrance_approach.png`, `unreal_garage_approach.png`, `unreal_service_corner.png`, `unreal_steps.png`, `unreal_top.png`, `unreal_three_quarter_front.png`, `unreal_three_quarter_reverse.png`, `unreal_rts_editor.png`, `unreal_flag.png`, plus `unreal_courtyard_reloaded.png`. Camera JSON accompanies each. The RTS-angle view is a native editor capture, not a gameplay test. Neutral daylight is inherited unchanged. `candidate21/REVIEW.md` compares composition and density against `reference/candidate11_Reference1.png`; later producer directions override surrounding terrain and flag size.

Actual sampled flag animation: `candidate21/animation/unreal_flag_wind.webp`; twelve raw native frames and metadata under `animation/frames/`. Material animation is continuous; the recording cadence is limited by the screenshot tool.

## Verified and remaining

- `audit/source_audit.json`: all 14,260 inherited mesh objects, 32 helpers and 432 protected files unchanged. Bounds remain 38 m X × 56 m Y × 17.98 m. All 165 swept-lane, 351 aperture and six pedestrian path rays clear.
- `audit/fbx_reimport.json` and `export_validation.json`: independent FBX triangle counts match, export tangent checks pass; flag UV range is 0–1 with 156 pinned corners. Adaptive subdivision stabilizes long thin bevels; microscopic bevel slivers are removed only in exports.
- `audit/unreal_verification.json`: four final native mesh bindings, inherited garage/architecture/roof bindings, materials, save/reload success, clean package states, no HQ21 import/material errors, no preview-terrain actor, and protected file hashes verified.
- Native close-ups inspected for scale, ground contact, conspicuous intersections, repeated textures, door/vehicle clearances and sightlines. No obvious floating service props or route obstructions observed. Soil/plant roots and pads intentionally contact or embed into their support surfaces.
- The four additions total 664,400 triangles; the full visual-review assembly is approximately 3,241,808 triangles. This is not runtime performance acceptance.
- **Garage remaining work: implementation only.** Claude owns authored collision and LODs, navigation, spawn/rally wiring, production door control/animation integration and setup-pipeline assembly. No open garage visual-polish request remains.
- Courtyard/runtime work still needed after visual acceptance: optimization/LODs, final collision/navigation/placement integration, material/texture-budget review and production wind hookup. Planter vegetation and flag are visual assets, not gameplay collision.

## Reproduction and safety

HQ21 scripts live under `Tools/Art/usa_hq21_*`. Build sequence: `build.py` → `clearance_fix.py` → `details.py` → `planting.py` → `flag.py` → `delivery.py`. Do not blindly rerun additive steps into an existing final source. `export.py` covers initial courtyard groups; `final_export.py` covers final details, planting and flag; `flag_export.py` applies the final flag UV policy. Texture recipes are `textures.py` and `ground_textures.py`. Native creation/import recipes are staged and not universally idempotent; consult their paths before rerunning. `review.py`, `record_flag.py`, `audit.py`, `fbx_verify.py` and `verify_unreal.py` provide evidence.

Installed background Blender was used because live Blender MCP was unavailable. All Unreal work used native MCP, without arbitrary editor Python. The game-dev CLI was unavailable; no sealed canonical game-dev package is claimed. No external/paid assets were used. No production level edits, commits, pushes or PRs.

**Candidate21 visual approval gate is satisfied. Preserve this approved candidate for Claude’s production integration.** Previous candidates and the approved garage appearance must remain preserved.

---

# Historical Candidate20 handoff

# USA HQ continuation handoff — Candidate20 garage

Updated 2026-10-21. **Candidate20 garage is visually approved: “Looking great and approved by me.”** Receipt: `Art/USA/buildings/usa_hq/candidate20/approval.json`. Architecture, tower/entrance, frames/façades/walls and roofs/communications are approved. The producer requested “Only the wall coming through the garage”; the local correction was completed and the resulting garage was approved. Courtyard/surroundings have not started. Do not commit or push; Claude owns production integration.

## Candidate20 current delivery

- Review and actual native gallery: `Art/USA/buildings/usa_hq/candidate20/REVIEW.md`.
- Final Blender source: `Art/USA/buildings/usa_hq/candidate20/source/USA_HQ_Candidate20_Garage.blend`.
- Recovery source before the local correction: `candidate20/source/USA_HQ_Candidate20_BeforeWallCorrection.blend`.
- Native preview: `/Game/Art/Candidates/USA_HQ/Candidate20/L_HQ20_Garage_Daylight`.
- Candidate-only corrected inherited architecture: `/Game/Art/Candidates/USA_HQ/Candidate20/Meshes/SM_HQ20_Architecture_WallCorrected`.
- Candidate-only corrected connector roof termination: `/Game/Art/Candidates/USA_HQ/Candidate20/Meshes/SM_HQ20_RoofSystems_WallCorrected`.
- Both corrected exports have identical basenames under `candidate20/fbx/` and `candidate20/source/` (FBX and GLB respectively). The preview replaces only its inherited architecture and static-roof bindings. Original Candidate18 and Candidate19 assets and levels are untouched. Candidate19 moving roof assemblies retain their original references and transforms.
- Garage meshes under `/Game/Art/Candidates/USA_HQ/Candidate20/Meshes/`: `SM_HQ20_GarageInterior`, `SM_HQ20_GaragePortal`, `SM_HQ20_GarageFloor`, `SM_HQ20_DoorHardware_Delivery`, `SM_HQ20_DoorSection_Delivery`, `SM_HQ20_DoorBottomSeal`, `SM_HQ20_DoorTopRollers`, `SM_HQ20_LiftCable`, `SM_HQ20_DriveShaft`.
- Those nine source/export basenames match, except DoorHardware and DoorSection omit `_Delivery` in their FBX/GLB filenames. Sixteen actor instances use the section mesh; two use the cable mesh.
- Materials and textures: `/Game/Art/Candidates/USA_HQ/Candidate20/Materials/MI_HQ20_*`, `Materials/M_HQ20_FloorWear`, `Textures/T_HQ20_Floor_BaseColor` and `Textures/T_HQ20_Floor_Roughness`. Source PNGs are in `candidate20/textures/`. Structural/service slots have no player tint; existing ownership accents retain the project CPD policy. Fixed blue lamps never tint.

## Local wall correction and preservation

The marked intrusion was the end of EastFrontLink projecting through the garage liner. Its internal terminal, including attached internal roof/trim ends, is cut and capped at Blender Y=10.60 m (Unreal Y=-10.60 m), behind the garage inner shell plane at 10.65 m. The visible exterior connector and garage façade remain. The source correction changes only 135 inherited terminal pieces: 67 trimmed and 68 entirely internal pieces removed. The remaining 11,753 inherited meshes, all fifteen original helpers and all 288 protected files are unchanged. Exact affected names: `candidate20/audit/wall_correction.json`. Before/after native views and the producer's original annotations are retained.

Source checks after the correction: 38 × 56 × 17.98 m bounds; 12.6 × 5.6 m garage aperture; seven-metre swept lane; 351 clear aperture rays and 165 clear full-bay/exit rays; no residual connector geometry beyond the trim plane; zero unexpected source/helper/file changes. Door samples and separate adjacent-leaf checks pass; the closed flexible edge has intentional 2 mm slab compression. Full-open lowest hard door structure is 6.155 m, above the 5.65 m opening head.

## Door preview and evidence

`/Game/Art/Candidates/USA_HQ/Candidate20/BP_HQ20_DoorPreview` drives `/Game/Art/Candidates/USA_HQ/Candidate20/CT_HQ20_DoorMotion_Delivery`. Source data and native Blueprint DSL: `candidate20/animation/door_motion.json`, `CT_HQ20_DoorMotion.csv` and `door_preview.dsl`. Sixteen 0.35 m leaves use exact chord poses on 0.65 m radius tracks; return height is 6.30 m. Section pivots are local to the lower hinge. Shaft/drums have a separate pivot at Unreal metres `(13.90, -17.5845184326, 6.46)`; the drum radius is 0.17 m. Existing `Pivot_GarageDoor` and spawn/exit helpers are unchanged.

The preview starts at BeginPlay: closed 2 s, opening 8 s, open 3 s, closing 8 s, closed to 24 s. Replay PIE to repeat. Actual Simulate-in-Editor recordings: `candidate20/animation/unreal_door_cycle.webp` and `unreal_mechanism_cycle.webp`; corresponding folders contain raw PNG captures and `capture.json` with actual leaf and shaft transforms. The capture tool samples at roughly one frame per second; Blueprint interpolation runs continuously.

Final actual Unreal evidence under `candidate20/renders/`: `unreal_wall_corrected.png`, `unreal_connector_exterior_preserved.png`, `unreal_exterior_open_final.png`, `unreal_exterior_closed.png`, `unreal_interior_depth.png`, `unreal_service_detail.png`, `unreal_mechanism.png`, `unreal_portal_detail.png`, `unreal_door_half.png`, `unreal_door_inside_closed.png`, `unreal_three_quarter_front.png`, `unreal_three_quarter_match.png`, `unreal_rts_editor.png`. Each has native camera metadata. The RTS view is an editor camera, not a production gameplay test. Early `unreal_first_*` / `unreal_lighting_*` images and older source diagnostic renders are development trials, not final evidence.

Native lighting is two movable, shadow-casting rect lights: 650 lm / 4200 K / 1250 cm attenuation and 240 lm / 5000 K / 850 cm attenuation. Approved baseline daylight/exposure is retained. Save/reload, bindings, material references, dirty states and protected-file receipts: `candidate20/audit/unreal_verification.json` and `wall_unreal_verification.json`. Geometry/export records are in the same audit directory.

## Reproduction and remaining work

HQ-specific recipes are under `Tools/Art/usa_hq20_*`. `inspect.py` establishes the original baseline and should not overwrite its receipt after production. Rebuild order: `build.py` → `wall_fix.py` → `export.py` → `audit.py` → `final_geometry.py`. `wall_fix.py` also produces the two corrected inherited exports using the approved exporter recipes; it does not write previous candidates. `unreal.py`, `finish_unreal.py`, `drive_assembly.py` and `door_preview.py` document native asset creation. Their creation stages are not idempotent: do not blindly rerun them into populated folders. `wall_unreal.py` imports the two corrected meshes and rebinds only Candidate20 actors; it likewise expects new asset paths. `review.py all` captures final poses; `record.py` records actual SIE cycles. All source modelling used installed background Blender because live Blender MCP was unavailable; all engine work used native Unreal tools, without arbitrary editor Python.

This is a visual-review candidate, not gameplay integration. The assembled preview is 2,577,408 triangles (612,852 added garage triangles); optimization/LODs, authored collision, spawn/rally/navigation wiring and production setup integration remain Claude's responsibility. Candidate20 automatic convex collision is removed; no gameplay collision certification is implied. No production level was edited, and no commits or pushes were made.

**Garage approval gate satisfied. Preserve Candidate20. Courtyard/surroundings is the next milestone and has not started.**

---

# Historical Candidate19 handoff

Updated 2026-10-21. **Candidate19 roofs and communications are visually approved by the producer: “Its approved by me.”** Architecture, central tower/entrance and frames/façades/walls remain approved. Approval receipt: `Art/USA/buildings/usa_hq/candidate19/approval.json`. Preserve Candidate19 as the new approved baseline. The next chat is for the garage.

## Candidate19 source and assembly

- Source: `Art/USA/buildings/usa_hq/candidate19/source/USA_HQ_Candidate19_Roofs.blend`.
- Source parent: `candidate18/source/USA_HQ_Candidate18_Garage.blend`; all 8,417 approved mesh objects and eight original helpers retained unchanged.
- Native level: `/Game/Art/Candidates/USA_HQ/Candidate19/L_HQ19_Roofs_Daylight`.
- Approved baseline asset remains `/Game/Art/Candidates/USA_HQ/Candidate18/SM_USA_HQ_Candidate18_Garage`, referenced unchanged by the Candidate19 level.
- Final additive meshes: `/Game/Art/Candidates/USA_HQ/Candidate19/Meshes/SM_HQ19_RoofSystems_Delivery`, `SM_HQ19_WingDish_Azimuth`, `SM_HQ19_WingDish_Elevation`, `SM_HQ19_Radar_Azimuth`, `SM_HQ19_WM_AHU_Fan1_Rotor`, `SM_HQ19_WM_AHU_Fan2_Rotor`, `SM_HQ19_WF_Condenser_Fan1_Rotor`, and `SM_HQ19_ER_Cooling_Fan1_Rotor`.
- The final static roof asset imports `candidate19/fbx/SM_HQ19_RoofSystems_Static.fbx`; its portable GLB has the same Static basename. Other additive mesh basenames match their FBX/GLB files. FBX vertices are baked centimetres and pivot-local for moving parts. Actor scales are one. Earlier Unreal Static, Final, Validated and Review roof imports are superseded trials, unused by the final preview; the Delivery asset is authoritative.
- New materials: `Candidate19/Materials/MI_HQ19_*`; inherited `HQ17_*` slots continue to use the original approved Candidate17 instances without edits.
- Geometry/export checks and source preservation: `candidate19/audit/`. Roof layout, support schedule, moving pivots and service paths: `candidate19/manifest.json`.

## Reproduction

Use background Blender 5.1: `usa_hq19_build.py` → `usa_hq19_tower_roofs.py` → `usa_hq19_finish_roofs.py` → `usa_hq19_views.py` → `usa_hq19_export.py` → `usa_hq19_audit.py`, all under `Tools/Art/`. Build starts from the final approved Candidate18 source and resets Candidate19; it never rebuilds Candidate17/18. The tower-roof step adds welds, basket drains, rear hatch and low maintenance pavers around the preserved equipment. The finish step places condenser louvres proud of their backing and adds the garage cable step-over. Export normally writes all eight additive assemblies; `--only-final-repairs` repeats static roofs and wing-dish elevation, while `--only-static` repeats just roofs. `usa_hq19_capture.ps1` saves individual native MCP images; `usa_hq19_review_captures.ps1` captures the fixed editor review camera set, with camera JSON.

The live Blender MCP connection was unavailable, so modelling used the installed Blender in background mode. Unreal work uses native MCP. The game-dev CLI is unavailable; no canonical sealed package is claimed. No external assets or paid generation were used.

The exporter segments long coping bevel quads before triangulation, avoiding centimetre-space tangent precision failures without changing the visible outline. It exports valid UVs and normals; Unreal uses ImportNormals with MikkTSpace to calculate the final roof tangent basis. The Delivery import has no tangent/binormal warnings. Earlier failed import warnings remain historical. The eight additions total 892,784 triangles, and the preserved baseline adds 1,086,916, for 1,979,700 triangles in the close-up review assembly. Runtime reduction remains required. `usa_hq19_fbx_diagnose.py` reads FBX vectors directly for diagnostics.

## Scope at this gate

The garage roof receives membranes, drains, solar panels, supports, inverter, conduits and service access. The approved garage crown, aperture and façade remain unchanged. Garage interior/door work and other garage production are reserved for the next chat. Courtyard placeholders are inherited.

This is a dense exterior visual-review assembly. Runtime LODs/optimization, final lightmap/texture atlas, collision, animation logic, gameplay integration and performance acceptance remain later work. The approved central dish remains in the inherited static benchmark; the newly built wing dish, radar and four HVAC fans are separate moving parts.

## Final Candidate19 evidence and next action

- Review gallery and primary-reference comparison: `Art/USA/buildings/usa_hq/candidate19/REVIEW.md`.
- Native images, under `candidate19/renders/`: `unreal_three_quarter_front.png`, `unreal_three_quarter_reverse.png`, `unreal_west_roofs.png`, `unreal_hvac_close.png`, `unreal_dish_close.png`, `unreal_east_comms.png`, `unreal_radar_close.png`, `unreal_roof_drainage.png`, `unreal_garage_roof.png`, `unreal_tower_roofs.png`, `unreal_rts_editor.png`, `unreal_top.png` and `unreal_rts_pie.png`. Each has camera/evidence JSON. Editor images use Game View to hide editor helpers and are unretouched.
- Neutral daylight retains the approved sun intensity 6 at 6500 K, rotation −45°/−135°, sky intensity 1, exposure −0.1, bloom 0.2. No global scalability changes.
- Actual PIE camera: project RTSCameraPawn, 150 m spring arm, −55° pitch, 60° FOV, MaintainYFOV and yaw 180°. The isolated art level has no gameplay HQ actor; empty HUD commands and elimination are expected. PIE is stopped.
- Native save/reload, final actor bindings, 15 roof material slots, triangle counts, bounds and import settings: `candidate19/audit/unreal_verification.json`. Final Delivery mesh and preview level report dirty=false. The mesh import and level reload report no warnings/errors relevant to the delivery.
- Source and independent FBX checks: `candidate19/audit/source_audit.json`, `fbx_assembly.json`, `export_validation.json`. Footprint/height remain 38 × 56 × 17.98 m; 8,417 approved mesh objects and eight helpers retained. All 172 protected files match their recorded hashes. All 294 sampled support contacts pass; dish/obstacle and rear-rib/reflector checks have no unintended intersections; 351 garage-aperture and 165 exit-lane rays remain clear.
- Visual inspection covers mounted equipment, pipe/duct/cable continuity, maintenance routes, membrane/coping transitions, dish clearance, roof shading and consistency with approved façades. Final export fixes include exposed condenser louvres, the garage cable step-over and short-span coping bevels. No observed floating equipment or unresolved shading defect in the delivered views.
- Reference: `Art/USA/buildings/usa_hq/reference/candidate11_Reference1.png`. Equipment locations and silhouette hierarchy follow the reference; functional spacing is adapted to the approved building. Its warm lighting, vehicles, flags, courtyard dressing and landscaping are not reproduced in this neutral roof review.

**The producer visual approval gate is satisfied. Garage production starts in a new chat.** Continue from a copy of the approved Candidate19 source and delivery assembly in a new candidate, retaining Candidate17/18 mesh and material dependencies. Preserve the approved roofs, communications and earlier milestones. No garage production was started in this chat. No production map was edited; no commits, pushes or PRs were made.

## Historical Candidate18 approved handoff

Updated 2026-10-21 with the producer's status correction. **Architecture and central tower/entrance quality are complete and approved.** Preserve the saved Candidate17 source and `SM_USA_HQ_Candidate17_StraightSupport` as the benchmark. `candidate17/approval.json` records approval of the final vertical dish support. Earlier pending-review statements below are historical, not active gates.

**Current state:** the remaining-wing / connector frame, façade and wall pass, including the garage completion, is complete and visually approved. After the final Unreal review, the producer confirmed **“Yes approved” on 2026-10-21**. Receipt: `candidate18/approval.json`. Preserve the final Candidate18 campus as approved work alongside the Candidate17 tower/entrance benchmark. **The visual approval gate is satisfied; roofs and equipment are next and have not begun.** Existing approved tower equipment is retained.

## Current deliverables

All relative art paths below start at `Art/USA/buildings/usa_hq/`.

- Review and matched Unreal comparisons: `candidate18/REVIEW.md`.
- Latest authoring source: `candidate18/source/USA_HQ_Candidate18_Garage.blend`; portable GLB beside it.
- Latest engine export: `candidate18/fbx/SM_USA_HQ_Candidate18_Garage.fbx`.
- Unreal mesh: `/Game/Art/Candidates/USA_HQ/Candidate18/SM_USA_HQ_Candidate18_Garage`.
- Preview level: `/Game/Art/Candidates/USA_HQ/Candidate18/L_HQ18_Daylight`. Actor `HQ18_Frames_Facades_Walls_Review` uses the final garage mesh, scale 1, with no component material overrides.
- Earlier `USA_HQ_Candidate18.blend` / `SM_USA_HQ_Candidate18` remain the pre-garage source/export/engine state; they are not the final delivery.
- Design rationale: `candidate18/DESIGN_NOTES.md`. Receipts: `candidate18/audit/`.

## What changed and what is preserved

Extended the tower's segmented structural frame, layered panel courses, recessed window reveals, mullions, projecting sills, head flashings, glazing, room-depth backing, joints and beveled edges across three west wings, their two short links, east rear wing, four straight core connectors and radar-support shell. Garage side/rear walls use the same solid panel system.

The final garage revision finishes the formerly white sloped crown with individual cassettes, fixings, coping, lower drip and inner flashing. It replaces the light portal with substantial segmented piers, reveals and framed header cassettes. Original crown geometry remains as recessed dark backing. The roof membrane/deck and opening are unchanged. This is the wall-to-roof transition, not the later roof-system milestone.

The 1,747 approved Candidate17 tower/entrance meshes remain unchanged. The garage revision separately verifies 8,019 non-revised meshes unchanged from the endorsed first Candidate18 source. Eight helpers, straight connector centerlines, courtyard, steps and plinth are retained. The 86 protected Candidate17 art, script and Unreal files remain hash-identical. No Candidate17 material was edited; final mesh slots refer directly to its approved material instances. Retain that dependency until Claude admits an accepted candidate into the production pipeline.

## Verified in the final delivery

- Independent FBX reimport and Unreal bounds: 38 × 56 × 17.98 m, within the approved 18 m ceiling; baked centimetres and actor scale 1.
- Garage opening 12.6 × 5.6 m: all 351 aperture rays clear. Seven-metre swept exit lane: all 165 rays clear.
- Export: 1,086,916 triangles, 560,754 vertices, 18 material slots; zero invalid tangents or collapsed UV triangles. The exporter removes microscopic bevel slivers and repairs invalid cap normals; no tangent-warning suppression is used.
- Unreal uses the final mesh with all 18 approved material assignments. Candidate18 import log search returns no tangent/binormal/degenerate warnings. Mesh and level packages explicitly saved and dirty=false; level reloaded and its final mesh binding verified. Use package paths without `.ObjectName` for reliable native dirty-state queries.
- Neutral daylight inherited from the approved preview: 6500 K sun, intensity 6, rotation −45°/−135°, sky intensity 1, exposure bias −0.1, bloom 0.2. No global scalability changes.
- Native captures include garage before/after, garage close-up, campus and tower-context comparisons, connector/window close-ups, actual PIE RTS view, and a separately labeled closer editor RTS-angle view. PNGs are unretouched native captures. Camera JSON accompanies editor images.
- Actual PIE uses the project RTS camera at a 150 m spring arm, −55° pitch, 60° FOV, MaintainYFOV; yaw is oriented to face the front. The isolated level has no gameplay HQ actor, so its HUD has no commands and reports elimination. This is visual verification, not gameplay integration. PIE is stopped at handoff; the preview level is open on the completed garage.

## Next milestone

The producer's garage / wall-façade visual approval is recorded. Roofs and equipment are the next milestone. Preserve Candidate18's approved source and engine assets; continue from a copy of `USA_HQ_Candidate18_Garage.blend` in a new isolated candidate. The existing tower equipment and garage-edge completion remain part of the preserved baseline. Maintain the approved proportions, clearances and Candidate17 tower benchmark throughout.

This dense exterior review mesh still needs runtime optimization/LODs, collision, gameplay integration and performance acceptance. UV0 uses metric 2 m planar/triangle projection for texture scale and valid tangents; a final texture/lightmap atlas is not claimed. Courtyard placeholders are retained; internal walkable circulation and garage-door animation are not part of this delivery. Nothing was placed in `L_TestMap` or another production map. No commits, pushes or PRs.

## Reproduction and source safety

Start further edits from the saved `USA_HQ_Candidate18_Garage.blend`. For full reconstruction, use background Blender 5.1 in this order: `Tools/Art/usa_hq18_build.py` → `usa_hq18_export_review.py` → `usa_hq18_garage.py` → `usa_hq18_garage_export.py` → `usa_hq18_garage_audit.py`. Source-only views use `usa_hq18_garage_views.py`; native Unreal capture uses `usa_hq18_capture.ps1`. Build/export/audit scripts are not interchangeable: the build resets its candidate source.

The build loads the final saved `candidate17/source/USA_HQ_Candidate17.blend`. Never rebuild Candidate17 from its older tower build script: approved later dish/support corrections are in the saved source. Primitive function definitions are extracted without executing that older generator. Do not run legacy `usa_hq_blender.py`.

Use native MCP imports/material tools under the Candidate18 folder only. The final garage material update took several minutes while Unreal completed its static-mesh build, then returned successfully; no editor restart was performed. Background Blender left the user's live Blender session alone. Native Blender/Unreal were used because the game-dev CLI is unavailable; no sealed package is claimed.

## Historical Candidate 15 handoff

Updated 2026-10-21 after producer connector review. **Candidate 15 is the active neutral architecture review. Await visual approval before materials/equipment.**

## Latest correction

Producer marked crossed roof coping, protruding connector end trims and the rear-left bent outline. Candidate 15 replaces all four links with straight 3.1 m-wide corridors. Front links remain diagonal into the tower; rear links now run directly across the core-to-wing gaps. Roof coping and walls run only along the two long sides, with terminals buried in host buildings. Removed the old rear-link diagonal branches that crossed the front links. Connector faces remain planar; micro-bevel detailing is deferred. All other architecture comes from Candidate 14.

- Source: `Art/USA/buildings/usa_hq/candidate15/source/USA_HQ_Candidate15.blend`; GLB beside it.
- Export: `candidate15/fbx/SM_USA_HQ_Candidate15.fbx`.
- Review: `candidate15/renders/architecture_review.png`, matched individual top/front/side/three_quarter views and connector_detail.png (Blender Workbench).
- Actual Unreal captures: `candidate15/renders/unreal_top.png` and `unreal_connector_detail.png` (perspective engine cameras).
- Unreal level: `/Game/Art/Candidates/USA_HQ/Candidate15/L_HQ15_Architecture`.
- Mesh: `/Game/Art/Candidates/USA_HQ/Candidate15/SM_USA_HQ_Candidate15`. The duplicated preview actor retains label HQ14_Architecture_Review but uses this Candidate15 mesh.
- Generator: `Tools/Art/usa_hq15_connectors.py` reads the preserved Candidate14 source, extracts helper function definitions without running its build, rebuilds four links, and executes adapted export/review scripts. `usa_hq15_sheet.py` composes the sheet.

Verified: Unreal bounds 38 × 56 × 12.36 m, mesh/level explicitly saved and dirty=false, export 86,064 triangles, no degenerate geometry or UV triangles, valid computed tangents and no Candidate15 tangent/binormal import warnings. All 23 Candidate14 files hash-identical. Garage geometry was unchanged; its prior clearance result is inherited, not rerun. Candidate14 and earlier deliveries remain preserved. No production level edits, commits or pushes.

Evidence: `candidate15/audit/export_validation.json`, `preservation.json`, `unreal_verification.json`. Temporary export UVs, runtime collision, optimization/LODs, final texture work and equipment limitations still apply. This is exterior architectural geometry; internal corridor circulation has not been modeled. Stop here for producer visual review.

## Historical Candidate 14 handoff (superseded by the correction above)

# USA HQ continuation handoff

Updated 2026-10-21. **Candidate 14 architecture is ready for producer visual review. Stop here: no detailed materials or equipment until review. It is not yet approved or production integrated.**

## Current authority

Read PRODUCTION_BRIEF.md and PRODUCTION_CHECKLIST.md. The producer explicitly superseded the old RTS-only brief: **the model leads**. Reference-first architecture must work in exterior close-ups as well as RTS gameplay. Primary appearance reference: `Art/USA/buildings/usa_hq/reference/candidate11_Reference1.png`. The two front courtyard connectors must be straight. The old Candidate 13 handoff remains historical context, not current visual authority.

Hard envelope: 56 m width × 38 m depth, 18 m maximum including future equipment. Unreal +X front and garage exit; +Y west. Blender mirrors Y. Current architecture reaches 12.36 m; the retained mast-tip helper is at 18 m. Do not add height to compensate for the absent equipment.

## Open these deliverables

- Authoring source: `Art/USA/buildings/usa_hq/candidate14/source/USA_HQ_Candidate14.blend` (named modules and eight retained socket/pivot helpers).
- Portable neutral model: `candidate14/source/USA_HQ_Candidate14.glb`.
- Engine export: `candidate14/fbx/SM_USA_HQ_Candidate14.fbx` (baked centimetres; actor scale 1).
- Four-view sheet: `candidate14/renders/architecture_review.png`; individual `top.png`, `front.png`, `side.png`, `three_quarter.png`, plus `connector_detail.png`. Orthographic camera parameters are in `renders/cameras.json`.
- Actual engine capture: `candidate14/renders/unreal_three_quarter.png`. This is Unreal; other named views are Blender Workbench. The engine capture is perspective, not pixel-identical to the orthographic sheet.
- Unreal preview level: `/Game/Art/Candidates/USA_HQ/Candidate14/L_HQ14_Architecture`.
- Final mesh: `/Game/Art/Candidates/USA_HQ/Candidate14/SM_USA_HQ_Architecture_Final`. The preview actor `HQ14_Architecture_Review` uses this mesh. Initial import trials were removed.

## Geometry decisions

Rebuilt central octagonal tiers, west three-block wing and its low links, east rear block, garage envelope, rear links and straight front links. Retained established layout schedule, garage dimensions, courtyard/step/plinth perimeters, and Candidate 13 socket/pivot transforms. Thick walls, recessed glazing, roof rims and structural ribs are architecture placeholders; no detailed materials, clutter, dish, radar, antenna array, flags or landscaping were added.

Front links have one straight centreline, width 3.1 m and roof at 4.8 m. Their starting terminals extend 1.6 m into the core to hide the end caps. Rear service links retain their established bent layout; the producer marked the two front links for straightening. Core command-storey glazing was enlarged after the first review render. Fascia areas reserve architectural sign panels, not finished faction graphics.

Reusing Candidate 13's already-beveled shells and beveling again created sliver triangles/tangent defects. Final inherited apron, garage, courtyard, steps and plinth shells were rebuilt from the same approved perimeters instead. The final import has **no degenerate-tangent, near-zero tangent or near-zero binormal warnings**. MikkTSpace remains enabled. Export checks find no degenerate triangles or collapsed UV triangles, and calculated loop tangents pass.

## Verified and still pending

Engine bounds: X −1900…1900 cm, Y −2800…2800 cm, Z 0…1236 cm at actor scale (1,1,1). Independent FBX reimport confirms metre dimensions. The 7 m-wide swept garage lane passes 165 rays from the spawn position through the exit. The opening remains 12.6 × 5.6 m. Eight original helpers remain in the .blend; this milestone's combined static FBX is not a runtime animated assembly.

The review mesh has **120,272 triangles** and five neutral materials. It exceeds the historical 60k root LOD0 target: optimization/LODs remain required after architecture acceptance. Export UV0 uses separate normalized triangle islands for stable tangents; these are temporary, not final metric texture UVs or a lightmap atlas. Source shells have planar UVs. Do not texture the temporary export as if it were the finished UV deliverable.

No runtime collision, door animation, team-color system, final lighting/material parity, equipment or gameplay integration is claimed. Nothing was placed in L_TestMap. The preview level is a separate copy of the engine Basic template with a neutral floor and default lighting; no global quality settings were raised.

Evidence: `candidate14/audit/architecture_audit.json`, `export_validation.json`, `unreal_verification.json`, and preservation receipts. All **685 protected pre-existing HQ files remain hash-identical**. Existing unrelated changes in AGENTS.md, Config, Source tests, older generators and docs predated this task and were not edited. No commit, push or PR.

## Setup and reproduction

Separate Unreal test material `/Game/Art/Candidates/USA_HQ/SetupVerification/M_HQ_SaveReopen_Test` passed create → explicit save → close editor window → reopen editor window → dirty=false. No engine restart was needed. The Unreal New Level shortcut blocked MCP until the producer dismissed its dialog; use the existing preview level or AssetTools duplicate/save/load for future work.

Blender executable: `D:/Program Files/Blender Foundation/Blender 5.1/blender.exe`. Scripts live under `Tools/Art/usa_hq14_*.py`. `usa_hq14_build.py` rebuilds architecture and calls `usa_hq14_finalize.py`, which runs refinement, inherited-shell rebuild, export, renders and audit in sequence. To retain source edits, run only the specific needed step; the build script resets the candidate. `usa_hq14_sheet.py` composes the rendered sheet using Python/Pillow. The finalizer is idempotent for its source refinements.

Never import or execute legacy `usa_hq_blender.py`: its module-level entry point previously overwrote Candidate 12. None of the new scripts imports it. Background Blender processes produced this delivery; the user's open, modified Candidate 13 Blender session was left intact.

Native Blender and Unreal tools were used; game-dev CLI was unavailable on PATH, so no sealed game-dev package is claimed. No paid generation or new external assets were used.

Working-tree observation at delivery: WorldDivide.uproject also now has additions enabling MeshTerrainMode, MeshPartitionWater, MeshPartition, PCGMeshPartitionInterop and MetaHuman. These were not written by the HQ scripts or tool calls and were left untouched; do not revert them as part of HQ intake. Their provenance was not established in this chat.

Next chat: show the sheet and actual Unreal capture, collect architecture feedback, revise Candidate 14 or fork a new candidate while preserving this review. Begin final materials/equipment only after architecture approval. Then optimize topology/LODs and integrate through the established project pipeline.
