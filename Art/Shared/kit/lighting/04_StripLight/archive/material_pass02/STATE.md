# 04 StripLight — current art state

## Latest: final finish pass

**Producer appearance approval — 2026-10-22:** "Yes is prima zo". This approves the current lamp shape and final material finish shown in this conversation. Art review is complete; Claude's production integration and runtime validation remain outstanding. This approval supersedes the pending-review statements in the historical record below.

Current source is `source/StripLight.blend`, refined from the previous pass. Original geometry and UV0 are unchanged; a new UV1 channel contains the baked micro-bevel/frost/paint normals. Still 300 triangles, 176 vertices and two slots. The FBXs are refreshed to carry UV1. They are no longer byte-identical to the previous pass, whose files are preserved in `archive/material_pass01/`.

Final model verification: `evidence/finish_verification.json`. The final DirectX map is `textures/T_StripLight_Finish_Normal.png`, sampled on UV1 with the original UV0 tangent basis. Refined housing roughness and lower diffuser emission are described at the top of `CLAUDE_HANDOFF.md`. Latest `pbr_*` renders show the final finish; `ue_*` images and candidate assets remain the PREVIOUS pass. Integration stays with Claude. Appearance review remains pending.

## Previous pass record

Material realism pass complete for producer review, 2026-10-22. Shape accepted as the base by the producer; final material appearance approval pending. Scope is model, materials and art preview only. Claude owns all production integration.

Authoritative model: `source/StripLight.blend`. PBR settings: `material_recipe.json`. Delivery/integration context: `CLAUDE_HANDOFF.md`.

Geometry, topology, UVs, surface assignments and transforms are unchanged after save/reload. Both original FBX files are byte-identical to their pre-material-pass versions. LOD0 and LOD1 each have 300 triangles and the two original slots, Housing and Lens. Receipt: `evidence/material_only_verification.json`.

Seven generated maps: 2K housing BaseColor/Normal/ORM and 1K lens BaseColor/Normal/ORM/LightProfile. Microstructure is deliberately restrained and clean. No added fasteners, ribs, geometry, dirt or damage. Shader-based frosted scattering is approximate, not a volumetric simulation of internal LED hardware.

Review evidence:
- `review/pbr_day_on.png`, `pbr_day_off.png`, `pbr_night_on.png`, `pbr_night_off.png`: 3840 × 2742 Blender Cycles renders.
- `review/ue_day_on.png`, `ue_day_off.png`, `ue_night_on.png`, `ue_night_off.png`: native Unreal editor captures from isolated candidate.
- `evidence/unreal_verified.json`: saved mesh bindings, instance parents and on/off values verified after candidate level reload in the same process.

Unreal preview: `/Game/Art/Candidates/Kit_StripLight/Candidate01/L_StripLight`. No production level edits or runtime code. Rect fixture light is 180 cd / 3500 K. Preview manual exposure compensation is -5 EV; review key/fill lights are presentation assets only.

Remaining: producer visual review; Claude's catalogue/shared-atlas adoption, runtime control, actual gameplay-distance validation and WorldDivide.Art.Kit test. Do not claim issue #393 closed or production integration complete.
