
## Round 1 — 2026-10-23

Rebaked kit 04 only. Archived approved source, preview, tiled maps, original recipe and records in [material_pass02](../04_StripLight/archive/material_pass02/). Added Tools/Art/kit/strip_light_rebake.py; saved packed source/StripLight.blend; rewrote recipe, STATE and CLAUDE_HANDOFF. Root FBXs and all integration files were left untouched.

Delivered exactly seven PNG maps: three 2048² housing maps; 1024² lens BaseColor/ORM/16-bit linear LightProfile; 2048² lens Finish_Normal. Both slot finish normals are byte-identical to the approved DirectX bake. The 2K lens map is retained because the [1K test](../04_StripLight/review/rebake/lens_end_1k.png) softens the narrow end highlight relative to [2K](../04_StripLight/review/rebake/lens_end_2k.png). No micro-normal stacking. UV0 maps sample at scale 1, no offset; finish normals sample FinishBake UV1 with UV0 tangents. LightProfile uses wrap and red-channel emission tint [1, 0.779, 0.615], strength 3.2.

Verification: 300 triangles, 176 vertices, two slots Housing/Lens; geometry digest without UV0 equal before/after reload (0dd185e55fd2030e54a4c651e5e944f06ea24329f6405ef12e04e664106953bc); UV1 equal (d67543995f683c84028dbb173674ba27305b2716f3299c779f095efa7f7e9528); housing UV0 equal (7f43bdc3e00449231dc9e5b1e15fa1de8158246a8e947c7d0967f8a6fce6f8f5). Lens UV0 uniformly scaled 0.8970953888 about (0.5, 0.5); min [0.003906274, 0.003906274], max [0.99609375, 0.99609375], four 1K texels of margin. Maximum tangent/sign component drift 1.78814e-7. Fresh-process strip_light.py build(piece) succeeds for both LODs. All seven PNG sizes, bit depths and SHA256s independently checked; all seven graph images packed; UV bindings checked after reload. [Full receipt](../04_StripLight/evidence/rebake_verification.json).

Cycles comparisons use archived approved preview cameras/lighting, 64 samples, AgX, 3840 × 2742 per view. Sheets are approved BEFORE on the left, REBAKE on the right. Mean absolute RGB difference is measured over all display-encoded pixels, scale 0–1 (and 0–255 in parentheses):

| View | Mean absolute difference | Review |
|---|---|---|
| Day on | 0.0000808581 (0.0206188) | [before/after](../04_StripLight/review/rebake/before_after_day_on.png) |
| Day off | 0.0000645621 (0.0164633) | [before/after](../04_StripLight/review/rebake/before_after_day_off.png) |
| Night on | 0.0000671815 (0.0171313) | [before/after](../04_StripLight/review/rebake/before_after_night_on.png) |
| Night off | 0.0000301748 (0.00769458) | [before/after](../04_StripLight/review/rebake/before_after_night_off.png) |

Details: [lens end / attenuation](../04_StripLight/review/rebake/lens_end.png), [housing edge / finish normal](../04_StripLight/review/rebake/housing_edge.png). The matched sheets and close-ups were inspected: no visible change in housing colour, roughness or roundover, or lens brightness/glow shape; no added dots or banding observed.

Open issues / limitations: the approved Blender housing graph used a small additional UV1-blue roughness subtraction (0.12 × (1 − normal.blue)). The new graph follows shared-master UV0 ORM sampling and omits this graph-only correction; no visible regression was observed in the supplied views. This approximation is explicitly recorded for Claude’s integration review. Blender reported an unavailable user-profile OptiX cache; Cycles completed successfully without changing cache settings or writing caches into Art. No new producer approval or Unreal acceptance is claimed. Claude retains FBX export, catalogue/setup, Unreal and production validation.
