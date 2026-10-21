# USA HQ production checklist

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

- [x] Final material/motion, consolidation, archival cleanup and save/reload evidence.
- [x] Complete verified-path Claude implementation handoff.
- [ ] Producer approval of the final milestone.
- [ ] Claude production integration, regeneration/reimport and gameplay acceptance.

## Candidate21 — courtyard and slab, visually approved, 2026-10-21

- [x] Read instructions, production records, saved reference, latest Blender source and actual Unreal baseline.
- [x] Preserve approved architecture and garage; isolated Candidate21 source and level only.
- [x] Finish paving/joints, concrete edges/steps, plinth/flag base, planter construction/soil/planting, drainage, restrained service props and surface wear.
- [x] Producer clarification: reusable building ends at concrete slab; no exterior dirt in delivery source or final level. Map supplies surrounding ground.
- [x] Producer direction: enlarge American flag to 3.42 × 1.80 m and verify native wind animation with fixed hoist.
- [x] Preserve 56 × 38 m footprint, 17.98 m height ceiling, all 14,260 inherited meshes and 32 helpers; 432 protected files unchanged.
- [x] Verify 165 swept-garage-route rays, 351 full-aperture rays and six pedestrian rays clear.
- [x] Independent FBX import, tangent checks and final flag UV weights verified.
- [x] Deliver actual Unreal courtyard close-ups, entrance/garage approaches, top, both three-quarter and RTS-angle views under neutral daylight; compare with saved reference.
- [x] Inspect scale, supporting contacts, obvious intersections, wear repetition and sightlines.
- [x] Save/reload native assets and level; verify bindings, clean package states, no HQ21 import/material errors and terrain exclusion.
- [x] Clarify garage carry-forward: appearance approved; implementation only remains (collision/LODs, navigation/spawn/rally, production door control and setup integration).
- [x] Update `candidate21/REVIEW.md`, `DELIVERY.json` and current handoff with exact paths/evidence.
- [x] Producer visual approval of Candidate21 courtyard — “Its approved by me”, 2026-10-21; receipt: `candidate21/approval.json`.
- [ ] Claude's production integration and runtime acceptance after approval.

Authoritative source: `Art/USA/buildings/usa_hq/candidate21/source/USA_HQ_Candidate21_Delivery.blend`. Native preview: `/Game/Art/Candidates/USA_HQ/Candidate21/L_HQ21_Courtyard_Daylight`. See current handoff for exact mesh/material paths and evidence.

Older “courtyard not started” entries below are historical and superseded by this section.


## Candidate20 — garage, approved 2026-10-21

- [x] Read root/team instructions, production records, saved primary reference, latest Blender source and actual Unreal baseline.
- [x] Reconfirm the four approved milestones; preserve Candidate19 and all earlier candidates.
- [x] Build isolated Candidate20 source and candidate-only preview level.
- [x] Refine existing portal rebates, pier protection, controls, photoeyes and fixed-blue accents without changing the approved envelope.
- [x] Complete interior wall liners, columns/beams, corrugated ceiling, conduits, fixtures, benches/cabinets, compressor and limited side storage.
- [x] Add floor material/wear, flush drainage and lane markings.
- [x] Construct sixteen separate rigid door sections, curved guide channels, return rails, hangers, seals, rollers, spring/gear housing, lift cables and separate rotating shaft/drums.
- [x] Preserve the approved appearance and fifteen inherited helpers; 288 protected files unchanged. The producer's subsequent wall correction affects only 135 EastFrontLink terminal parts in Candidate20 (67 trimmed, 68 wholly internal pieces removed); the other 11,753 inherited mesh objects are unchanged.
- [x] Correct the marked connector wall protruding through the garage liner. Cut/cap at Blender Y=10.60 m, behind the 10.65 m inner shell plane; zero residual connector protrusions and zero unexpected source changes.
- [x] Check 351 aperture rays and 165 full-depth bay/exit rays; all clear in the fully open state. Parked door structure remains above 6.155 m, exceeding the 5.65 m opening head.
- [x] Sample door travel and adjacent rigid-leaf intersections; no unintended intersections. Closed rubber edge compresses 2 mm into the inherited slab, an intentional contact.
- [x] Independently reimport all nine FBX assemblies and match triangle counts; retain MikkTSpace and imported normals.
- [x] Build and run native Unreal Blueprint open/hold/close preview; check actual actor transforms and runtime log.
- [x] Finish final native gallery, save/reload receipt and handoff.
- [x] Producer visual approval of the garage and wall correction — “Looking great and approved by me”; receipt: `candidate20/approval.json`.
- [x] Courtyard and surroundings — subsequently completed and visually approved as Candidate21; see active records above.

Preview evidence does not certify gameplay integration, vehicle spawn logic, collision, LODs or runtime performance. See `candidate20/REVIEW.md` and the current handoff.

## Candidate19 — roofs and communications, approved 2026-10-21

- [x] Read AGENTS.md, production brief/checklist/handoff, and inspect the final Candidate18 Blender source, actual Unreal baseline and saved primary reference.
- [x] Reconfirm producer approvals for architecture, tower/entrance, and campus frames/façades/walls. Retain Candidate18's approved garage façade.
- [x] Create isolated Candidate19 source and `/Game/Art/Candidates/USA_HQ/Candidate19/L_HQ19_Roofs_Daylight`.
- [x] Add membrane rolls/welds, parapet/abutment flashing, coping joints, internal drainage baskets and reinforced roof penetrations.
- [x] Add varied HVAC/vents, connected ducts and pipes, cable trays, equipment supports, maintenance hatches and service routes.
- [x] Add rear-west dish station, front-west directional radio array, east relay/radome, radar and garage-roof solar service bank; retain approved core mast/dish.
- [x] Separate four fan rotors, wing-dish azimuth and elevation, and radar azimuth, retaining original helper transforms.
- [x] Check source preservation, roof contacts, dish clearances, footprint/height and garage aperture/lane; inspect source views and repair detected defects.
- [x] Complete final native Unreal material, assembly and visual verification; save/reload and record exact bindings. Delivery roof mesh: `SM_HQ19_RoofSystems_Delivery`; MikkTSpace retained, no final import warnings; mesh/level dirty=false.
- [x] Deliver twelve native Unreal editor views and an actual project RTS-camera PIE capture; compare with primary reference in `candidate19/REVIEW.md`.
- [x] Finalise handoff, preservation and export receipts. All 8,417 approved meshes and 172 protected files preserved; 38 × 56 × 17.98 m; 294 contact samples pass, dish/rib clearance checks pass, 351 aperture and 165 lane rays clear.
- [x] Producer visual approval of Candidate19 roofs and communications — “Its approved by me”, 2026-10-21. Receipt: `candidate19/approval.json`. **Garage starts in a new chat, preserving this approved candidate.**

Older "next milestone / not started" notes below describe the Candidate18 handoff and are superseded by this active checklist.

## Current milestone status — producer correction, 2026-10-21

- [x] Architecture milestone complete and approved, including straight connectors and Candidate16 tower proportions.
- [x] Candidate17 central tower and entrance quality milestone complete and approved, including final vertical dish support; see `candidate17/approval.json`.
- [x] Preserve Candidate17 as the visual benchmark; start a separate Candidate18 for the remaining campus.
- [x] Extend structural frames, wall panels, recessed openings, window frames/sills/glazing, construction joints and roof-edge transitions across remaining wings and connectors.
- [x] Record producer feedback: “Only the garage is missing. The rest is looking awesome.” Preserve the endorsed non-garage work.
- [x] Complete garage sloped crown cladding, coping, flashing and substantial portal frame; preserve 8,019 meshes outside the garage revision.
- [x] Verify clean bevels, approved proportions and garage clearances: 38 × 56 × 17.98 m; 351 clear aperture rays and 165 clear rays through the 7 m lane.
- [x] Verify Candidate18 in Unreal neutral daylight, close-ups and RTS views; compare directly with the approved tower. Final mesh: `SM_USA_HQ_Candidate18_Garage`.
- [x] Explicitly save final mesh and `L_HQ18_Daylight`; package dirty=false; reload level and verify final mesh binding.
- [x] Update delivery evidence, preservation results and continuation handoff; see `candidate18/REVIEW.md` and `audit/unreal_verification.json`.
- [x] Producer visual approval of Candidate18 wall/façade milestone, including garage — “Yes approved”, 2026-10-21; see `candidate18/approval.json`.
- [x] Roofs-and-equipment milestone — subsequently completed and approved as Candidate19; see the active records above.

Current delivery: `candidate18/source/USA_HQ_Candidate18_Garage.blend`, matching GLB and `fbx/SM_USA_HQ_Candidate18_Garage.fbx`. Final export has 1,086,916 triangles, 18 reused material slots, zero invalid tangents and zero collapsed UV triangles. No Candidate18 tangent/binormal/degenerate import warnings were found. This is a dense exterior visual-review asset; runtime optimization, LODs, collision and gameplay integration remain later work. The default RTS screenshot is actual PIE; the closer RTS-angle screenshot is explicitly an editor view. Candidate17's 1,747 tower/entrance meshes and 86 protected source/engine/script files are preserved.

## Historical architecture checks (review gates now satisfied)

- [x] Read root AGENTS.md, designer handbook and AI_TEAM.md.
- [x] Read USA_HQ_SPEC.md and Candidate 13 handoff.
- [x] Inspect primary appearance reference and producer connector correction.
- [x] Record model-first override of former RTS-only acceptance.
- [x] Verify separate Unreal material creation, explicit save, close and reopen; dirty state false.
- [x] Preserve baseline hashes before geometry work (685 files).
- [x] Build Candidate 14 neutral architecture with straight front connectors.
- [x] Check dimensions, clear garage and connection penetration (165 clear lane rays; buried connector terminals).
- [x] Render matched top, front, side and three-quarter views.
- [x] Import and save candidate in isolated Unreal preview level.
- [x] Capture actual Unreal view and verify engine bounds (3800 × 5600 × 1236 cm; scale 1).
- [x] Recheck protected file hashes and update handoff (zero changed files).
- [x] Producer architecture visual review — approved; superseded by current milestone status above.

Setup evidence: `/Game/Art/Candidates/USA_HQ/SetupVerification/M_HQ_SaveReopen_Test`; MaterialTools.create_material returned object; AssetTools.save_assets returned true; Slate closed its dedicated editor window; OpenEditorForAsset reopened it; GetOpenAssets listed it; AssetTools.is_dirty returned false. This is editor close/reopen verification, not an engine-process restart test.

Final engine mesh: `SM_USA_HQ_Architecture_Final`. Its import log contains no tangent/binormal warnings. Initial failed import trials were removed after switching the preview actor to the final mesh. No MikkTSpace disabling or warning suppression was used. Final mesh and preview level both report dirty=false.

After approval: final metric texture UVs, detailed materials/equipment, runtime mesh optimization and LODs, collision and gameplay integration. Current 120,272-triangle review mesh exceeds the legacy 60k LOD0 target; it is not a production-ready RTS asset.

## Candidate 15 connector correction — 2026-10-21

- [x] Preserve Candidate 14 (23 files, zero hash changes).
- [x] Replace crossed/bent link arrangement with four straight corridors and open building terminals.
- [x] Generate matched four-view sheet and actual Unreal top/detail captures.
- [x] Verify engine bounds and save isolated Candidate15 mesh/level; both dirty=false.
- [x] Validate export tangents and zero degenerate geometry/UV triangles (86,064 triangles).
- [x] Producer architecture approval, including connector correction — confirmed 2026-10-21.

Candidate15 entries are historical. Approved tower/entrance benchmark: `Candidate17/SM_USA_HQ_Candidate17_StraightSupport`. Current work: Candidate18 walls/façades.
