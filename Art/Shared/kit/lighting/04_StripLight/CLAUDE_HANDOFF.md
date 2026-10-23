# Kit 04 StripLight — Claude handoff

## Issue #437 Round 1 rebake — 2026-10-23

Use source/StripLight.blend: packed textures, object SM_Kit_StripLight, slots Housing then Lens, UV0 UVMap and UV1 FinishBake. Fresh-process strip_light.py build(piece) succeeds. Geometry remains 300 triangles / 176 vertices; LOD1 stays identical. Root FBXs are untouched; Claude exports through kit_blender.py.

The seven PNGs, all in textures/ and prefixed T_StripLight_:

| Suffix | Size | Sampling |
|---|---|---|
| Housing_BaseColor | 2048² | sRGB, UV0 |
| Housing_ORM | 2048² | linear, UV0 |
| Housing_Finish_Normal | 2048² | DirectX linear, UV1 |
| Lens_BaseColor | 1024² | sRGB, UV0 |
| Lens_ORM | 1024² | linear, UV0 |
| Lens_Finish_Normal | 2048² | DirectX linear, UV1 |
| Lens_LightProfile | 1024², 16-bit | linear red, UV0 |

All texture scales are 1, zero offset, wrap sampling. ORM = occlusion/roughness/metallic. Keep tangents on UV0. Both finish-normal files are byte-identical copies of the approved bake, including paint/frost microstructure: do not stack another normal. Blender reverses DirectX green in its graph. Lens UV0 is uniformly scaled 0.8970953888 about (0.5, 0.5), bounds 0.003906–0.996094; housing UV0 and UV1 are unchanged.

Catalogue values: Housing has no overrides ({}); Lens EmissionTint [1, 0.779, 0.615] and Intensity 3.2. No profile transform. Optical authoring values remain IOR 1.5 housing / 1.49 lens, lens transmission 0.08 and subsurface 0.18 at 2.5 mm. Fixture/light placement and runtime behaviour remain Claude’s responsibility.

Lens finish remains 2K: compare the [2K end close-up](review/rebake/lens_end_2k.png) with the [1K test](review/rebake/lens_end_1k.png), where the narrow return highlight becomes softer. This retains the accepted 1.8 mm shading roundover. Housing roundover remains 1.2 mm; neither changes silhouette.

Reproduce with background Blender --python Tools/Art/kit/strip_light_rebake.py -- rebake, then render, verify, compare. The optional render --lens-test runs only the resolution experiment. Rebake reads archived approved inputs, avoiding compounded UV transforms. It never writes FBX or Unreal assets. Hashes and per-view differences: [verification](evidence/rebake_verification.json); all map values: [recipe](material_recipe.json); [review gallery](review/rebake/).

Open technical note: the approved housing graph subtracted 0.12 * (1 - finishNormal.blue) from roughness. The shared master samples UV0 ORM only, so the new graph samples ORM directly. The supplied housing-edge and four matched comparisons show no visible regression; no shared-master edit is requested. No new producer approval or Unreal acceptance is claimed. Claude owns catalogue, exports, setup, Unreal and tests.

## Historical — superseded material/export instructions

The complete [previous handoff](archive/material_pass02/CLAUDE_HANDOFF.md), approved source, preview, tiled maps and recipe are archived under material_pass02. Its tiled scales, shared finish filename, profile transform, old emission multiplier, FBX statements and pending-review wording describe earlier passes. Producer approval of the archived shape and final finish was recorded on 2026-10-22: “Yes is prima zo”.
