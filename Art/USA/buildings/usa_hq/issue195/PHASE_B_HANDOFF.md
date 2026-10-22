# USA HQ #195 Phase B — fixture appearance review

Status: **art stage ready for producer appearance review**. Fixture source, FBX checks, actual Unreal day/night captures and the saved-level reload audit are complete. Fixture appearance has not yet been approved. Stop here; production integration is not authorised by this delivery.

The existing `source/USA_HQ_Final.blend`, `game/`, H28 mast FBXs, production recipe and production packages are preserved. #351 is closed and the inspected recipe/live meshes already contain the selected H28 core/east masts and their 1.75-scale red obstruction beacons. The source's inherited historical variants and legacy helpers are not new production inputs.

Producer receipt, 2026-10-22: “Allow rebatching only; preserve all geometry”. This allows the two unchanged H28 mast surfaces to share one static export. No approved surface was simplified, removed or moved to make space.

Scope clarification from producer: “Focus only on the moddeling. If it are code changes, let it know in the prompt for claude code”. This delivery stops at modelling and its art preview. All production lighting, flashing/power behaviour and runtime culling work belongs in the Claude prompt.

## Changes for review

1. Two 35 × 25 × 20 cm floodlight housings: chamfered front, separate warm glass lens, tapered rear housing, U yoke, tilt axle, mounting plate and short fixed roof standoff. The same 104-triangle head mesh is placed twice. Lens slot `HQ20_FixtureWarm`; metal slot `HQ17_DarkStructure`.
2. Two upright amber garage domes, 15 cm across (15.5 cm gasket/base), 12.4 cm high, with dark bases and small fixed wall shelves near the pillar tops. The same 72-triangle beacon mesh is placed twice. Existing blue strips are untouched.
3. The four fixed mounting supports add 48 triangles and share the masts' existing dark section. The approved H28 masts are rebatted together, retaining all their original triangles/materials and world positions.
4. Four exact building-space helpers. Flood +X points toward the command panel/eagle. A nine-point grid across the panel is visible from each final lens: 18/18 rays first hit the panel or emblem, with no intervening roof or parapet. The west position was moved forward during authoring to clear the lower panel.

## Authoritative inputs

- Complete additive source: [phase_b/USA_HQ_PhaseB.blend](phase_b/USA_HQ_PhaseB.blend). All 20,173 inherited mesh objects were checked unchanged against the approved source.
- Reusable fixture templates: [phase_b/Fixtures_Library.blend](phase_b/Fixtures_Library.blend).
- Current assembly, exact source quaternions and helpers: [phase_b/fixture_manifest.json](phase_b/fixture_manifest.json), field `preferred_instanced_delivery`.
- Export hashes: [phase_b/delivery_hashes.json](phase_b/delivery_hashes.json).
- Independent FBX reimport: [phase_b/fbx_verification.json](phase_b/fbx_verification.json).
- Useful beam visibility: [phase_b/facade_visibility.json](phase_b/facade_visibility.json). `beam_probes.json` is an earlier placement diagnostic.
- Measured pre-edit meshes: [phase_b/baseline_unreal.json](phase_b/baseline_unreal.json).
- Saved-level reload audit, exact Unreal actor transforms, package/source map and persisted materials: [phase_b/final_unreal_audit.json](phase_b/final_unreal_audit.json).
- Native import geometry warnings: [phase_b/import_warning_review.json](phase_b/import_warning_review.json) — none found.

Use these six preferred FBXs in `phase_b/fbx/`; do not integrate the superseded `StaticEquipment*` or `Fixtures` assembly exports:

| Mesh | LOD0 | LOD1 file/count | Slots in order | Placement |
|---|---:|---:|---|---|
| `SM_HQ195_MastsFixed` | 9,600 | `_LOD1`: 4,152 | `HQ17_DarkStructure`, `HQ17_FramePaint`, `HQ17_TeamLight` | Identity, building origin; replaces both separate H28 mast parts |
| `SM_HQ195_Flood` | 104 per head | `_LOD1`: 104 | `HQ17_DarkStructure`, `HQ20_FixtureWarm` | Two transforms in manifest |
| `SM_HQ195_Beacon` | 72 per dome | `_LOD1`: 72 | `HQ17_DarkStructure`, `HQ195_BeaconAmber` | Two transforms in manifest |

The fixture LODs deliberately retain the same very small geometry. No fixture part disappears at the far switch. The mast LODs are the accepted hand-authored H28 pair, concatenated without reduction.

| Whole HQ | Before, measured live | Phase B | Limit |
|---|---:|---:|---:|
| LOD0 triangles, including drawn instances | 349,568 | 349,968 | 350,000 |
| LOD1 triangles, including drawn instances | 39,032 | 39,432 | 40,000 |
| Material sections with existing production instancing | 79 | 80 | 80 |

Section accounting: 79 − 6 separate mast sections + 3 combined mast/support sections + 2 shared flood sections + 2 shared beacon sections = 80. Preserve instancing during integration. Preview actors are individual placements and are not a draw-call benchmark. No new textures; existing production texture dimensions, streaming and mip settings are inherited unchanged.

FBX reimport finds zero missing/changed mast triangles, zero degenerate triangles and zero invalid UV faces. Maximum mast vertex round-trip error is 0.0024 mm; maximum corner normal difference is 0.087 degrees. The shared-part split retains every additive fixture triangle within float32 round-trip tolerance.

## Helpers and materials

Unreal building centimetres below; source Blender uses metres and mirrored Y. All transforms are relative to the building origin. The helper positions are lens/dome centres, distinct from each reusable mesh's placement pivot.

| Exact helper | X | Y | Z | Beam +X, Unreal XYZ |
|---|---:|---:|---:|---|
| `SOCKET_Flood_West` | 799.767 | 1284.678 | 749.956 | (-0.582512, -0.805388, -0.109683) |
| `SOCKET_Flood_East` | 1199.689 | -1049.754 | 804.946 | (-0.777613, 0.614182, -0.134535) |
| `SOCKET_GarageBeacon_South` | 1568.000 | -2438.452 | 649.600 | Upright dome |
| `SOCKET_GarageBeacon_North` | 1568.000 | -1078.452 | 649.600 | Upright dome |

Target panel centre: approximately (-129.4, 0, 575) cm. Final lens-to-centre distances: 15.95 m west, 17.09 m east. Keep the existing H28 mast feet/tips and red beacon positions/scales from `HANDOFF_379.md` and the integrated recipe, including the 2898 cm whole-building height. Do not restore the stale 18 m mast helper.

`MI_HQ195_BeaconAmber` uses the existing production `M_HQ_Final_Operational` parent. Overrides: `EmissionColor=(2.4,1.0,0.1,1)`, `SurfaceColor=(0.32,0.095,0.007,1)`, `Roughness=0.25`, `Metallic=0`, `GroupFixtures=0`, `GroupWindows=0`, `GroupAccents=0`. This is an opaque emissive amber lens. Beacons retain a separate component state from masts and floods. Other slots reuse the corresponding existing `MI_` production instances.

## Unreal preview and evidence

Candidate: `/Game/Art/Candidates/USA_HQ/Issue195_PhaseB/L_HQ195_Fixtures`. Candidate meshes/materials live alongside it. It references unchanged production shells, moving equipment, flag, red obstruction beacon and production materials read-only. The dense shell comparison uses Candidate22's `SM_HQ379_ShellReview` read-only, with matching material overrides on this candidate's shell component. It is not an integration input.

Current support packages are `Meshes/SM_HQ195_MastsFixed_Final` and `Meshes/SM_HQ195_MastsFixed_Final_LOD1`, imported from the two portable `SM_HQ195_MastsFixed` FBXs. The native importer refuses overwrite, so the earlier support imports are staging packages. Shared fixtures use `Meshes/SM_HQ195_Flood`, `Meshes/SM_HQ195_Flood_LOD1`, `Meshes/SM_HQ195_Beacon` and `Meshes/SM_HQ195_Beacon_LOD1`. The final audit maps every portable FBX to its actual package. No Nanite is enabled on any of these six packages. The fixture components cast no shadows.

Evidence directory: `../renders/issue195/phase_b/`. Raw captures and camera JSON remain beside labelled review sheets. These are actual Unreal **editor** views, with manually synchronised LOD selection; they do not certify the gameplay camera, automatic switching, motion or performance. Native 90° captures are cropped without resampling to effective 60° horizontal FOV, 16:9.

- [Day fixture close-ups](../renders/issue195/phase_b/Fixtures_Day.png)
- [Night fixture close-ups](../renders/issue195/phase_b/Fixtures_Night.png)
- [12 / 150 / 450 m master/game comparison](../renders/issue195/phase_b/RTS_Comparison.png)
- [249.6 / 270.4 m switch pair](../renders/issue195/phase_b/LOD_Switch.png)
- [Day/night at 150 m](../renders/issue195/phase_b/Day_Night_150m.png)
- [Night facade and garage context](../renders/issue195/phase_b/Night_Context.png)

The night preset uses four temporary candidate-only, non-shadowing lights at these helpers. It demonstrates fixture placement and glow; it is not the production seven-steady-plus-two-beacon lighting implementation. Day fixture captures use an OFF pose; night captures show flash ON. No blinking controller is delivered.

## Issues and remaining work

- Appearance acceptance is pending. The compact fixtures are visible in close-ups; at 150 m their light/glow is the main cue, rather than the small housing details. Keep their specified physical sizes unless the producer requests a modelling revision.
- Geometry headroom is only 32 triangles at LOD0 and 568 at LOD1; all 80 material sections are used. Preserve shared-part instancing and the approved rebatching when integrating.
- The editor intermittently culled the north beacon at night. A targeted capture with occlusion queries disabled showed the complete unchanged mesh. Normal occlusion was restored, and the candidate north beacon component uses `boundsScale=2`; its corrected night close-up uses normal occlusion. This is a preview component setting, not a geometry change or a certified runtime fix. Claude must validate bounds/culling with the production assembly and all rotations.
- Nested custom primitive data updates in the native editor tool can leave stale preview render state; the art preview script refreshes the small mesh proxies. Claude should drive production material state through the intended runtime APIs rather than copy that capture workaround.
- Four exact helper empties and their transforms are delivered; native Unreal mesh-socket serialization remains unverified. Claude must explicitly create/use the supplied named helper transforms.
- Production lighting, beacon flashing, power states, automatic LOD switching and the eight-HQ night benchmark remain for Claude after art approval. They are not additional modelling tasks.

Preview-only controls: flood spots 500 lm, inner/outer angles 9°/16°, radius 2300 cm; beacon points 8 lm, radius 180 cm. Night uses fixed manual exposure and a cool fill; day restores the saved daylight and disables the preview exposure volume. The nested CPD property tool needs a render-proxy refresh for the small shared meshes after an ON/OFF change; the preview script handles it. These controls are not a proposed production lighting implementation. Four exact helper empties are independently verified in FBX; native `StaticMeshSocket` serialization is not certified by this toolset, so integrate the supplied helper transforms explicitly.

Recovery: a dense reference duplication triggered an expensive derived-data rebuild. Only this task's editor was restarted from saved art; candidate checkpoint packages/hashes are in `phase_b/recovery/saved_checkpoint.json`. The final review uses the original dense reference read-only and does not depend on the unfinished duplicate. No prior candidate or production package was deleted.

## Reproduction and next step

`Tools/Art/usa_hq_195_fixtures.py` is import-safe. Explicit Blender stages: `build` writes this task's additive source/staging exports; `parts` writes the preferred shared-part/mast-support exports; `facade_probe` inspects approved source geometry. Run `usa_hq_195_verify.py` afterward. These never rewrite approved source or prior FBXs.

`usa_hq_195_preview.py` uses only discovered native Unreal tools. `setup` and `instanced_setup` are one-time initialisation stages guarded against replay on a completed assembly. `pose game|far|master`, `set_lighting day|night`, captures and `final_audit` operate only on this candidate. `refresh_final` updates this task's changed mount exports/transforms. `usa_hq_195_evidence.py` only crops and labels captured pixels.

Next milestone: producer appearance review of these four fixtures. Preserve all earlier HQ approvals. After acceptance, use [CLAUDE_IMPLEMENTATION_PROMPT.md](CLAUDE_IMPLEMENTATION_PROMPT.md). Production light wiring, garage sequence/power behaviour, automatic LOD/bounds validation, placement rotations and the eight-HQ night benchmark remain with Claude. No production code, recipe, setup, commit, push or PR is part of this art stage.

Fresh chat starter: “Use `$world-divide-building-production` for World Divide #195 Phase B. Read `Art/USA/buildings/usa_hq/issue195/PHASE_B_HANDOFF.md` and its manifests/evidence. Continue the producer's fixture appearance review, preserving the approved HQ and H28 masts. Stop before production integration.”
