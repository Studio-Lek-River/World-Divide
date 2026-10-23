# Kit 04 StripLight — model and material delivery

## Final finish pass — supersedes earlier material notes below

**Approved by the producer on 2026-10-22:** "Yes is prima zo". Use the current final model and material finish as the accepted art delivery. Do not restart appearance review or remodel the lamp. This approval supersedes any earlier pending-approval wording below; production integration and gameplay validation are still Claude's work.

The final source is still `source/StripLight.blend`. Geometry, original UV0, transforms and material-slot layout are unchanged: 300 triangles / 176 vertices. One additional UV channel `FinishBake` (UV1) carries the baked finish. FBX files have been re-exported to include this channel, so the old byte-identical-FBX claim below applies only to the previous pass. Previous source/FBXs/review images are archived under `archive/material_pass01/`.

Use `textures/T_StripLight_Finish_Normal.png` as the final DirectX normal for BOTH slots, sampled with TexCoord index 1. The bake was explicitly converted to the original UV0 tangent basis; keep Unreal's mesh tangent basis on UV0. Do NOT combine it again with the old micro-normal: the microstructure is already included. The `NormalGL` variant is for Blender only. Physical shading radii: housing 1.2 mm, diffuser 1.8 mm. These are normal-map effects, not displaced edges. Keep original geometry and silhouette.

Housing roughness is now centred on 0.39 with a 0.30–0.51 range. At rounded edges, Blender reduces it by `0.12 * (1 - NormalGL.blue)` (at most a few hundredths), giving restrained satin edge polish. No bare-metal chips or painted scratches. Lens emission strength is now 3.2 with softer width/end attenuation in the regenerated LightProfile. Other white-polycarbonate settings remain as documented below.

Regeneration order: run `strip_light_materials.py` with bundled Python, then Blender `strip_light.py -- finish`, then `-- verify` and `-- render --4k`. The `finish` stage adds/reuses the dedicated bake UV channel and verifies a digest of geometry plus UV0 before/after. Do not use the old `materials` stage alone to reproduce the final finish: it rebuilds the earlier unbaked graphs.

The existing Unreal candidate and its captures document the preceding material pass. This final pass is delivered in the model/source maps/FBX, as requested; no production integration has been attempted. Claude must adopt the new UV1 finish map and new emission strength during integration. Final geometry receipt: `evidence/finish_verification.json`; final reimport counts: `evidence/fbx_reimport.json`.

Current scope: model/material realism only. The producer explicitly assigns all game integration to Claude. Appearance approval is pending. Do not interpret this delivery as completion of issue #393 or the entire lighting kit.

## Authoritative inputs

- `source/StripLight.blend`: current clean model with packed PBR textures and editable material graphs.
- `source/StripLight_Preview.blend`: separate lighting review scene; room and review lights are NOT asset geometry.
- `SM_Kit_StripLight.fbx`, `SM_Kit_StripLight_LOD1.fbx`: original mesh exports, unchanged bytes after material pass. Both 300 triangles, 176 vertices, slots `Housing`, `Lens` in that order. No additional parts or geometry.
- `SM_Kit_StripLight.glb`: initial geometry preview only; its old simple materials are superseded by the Blender source/material recipe.
- `material_recipe.json`, `textures/`: deterministic PBR source maps and parameter values. Housing maps 2048²; lens maps 1024². BaseColor is sRGB; Normal, ORM and LightProfile are linear. ORM = occlusion, roughness, metallic. DirectX normal maps; Blender reverses green in its graph.
- `evidence/material_only_verification.json`: before/after geometry+UV+transform digest and unchanged FBX hashes.
- `references/producer_reference.png`: visual material target, not authority to add its screws or ribs.

Model frame: metres in Blender, centimetres in Unreal. Mounting face at X=0; emission +X; length along Y; Z up. Envelope 120 × 18.2 × 12.4 cm. Ceiling mounting uses pitch -90 degrees in Unreal. Lens bounds and light anchor are recorded in `model_manifest.json`.

## Material intent

Housing is intact graphite powder coating: dielectric Metallic=0, IOR 1.5 / Specular 0.5, roughness about 0.46 with restrained variation. The metal substrate is covered by paint; do not make the painted surface metallic merely because the housing is metal. No heavy dirt, rust, scratches, decorative seams or extra geometry.

Lens is neutral opal polycarbonate: IOR 1.49, roughness about 0.43, fine frost normal. Blender uses modest transmission (0.08) and subsurface scattering (0.18, 2.5 mm scale). Unreal candidate uses the opaque Subsurface shading model as an economical diffuse-transmission approximation; it is not refractive glass. Keep the unlit base white. Smooth LightProfile modulation produces broad internal illumination and soft perimeter attenuation, without drawn LED dots or ribs.

Emission reference: 3500 K, linear tint (1, 0.779, 0.615), multiplier 5, scalar `LampLevel` 0/1. LightProfile samples UV transformed by scale (1/1.106, 1/0.146) and offset (0.5-0.5/1.106, 0.5-0.5/0.146). Housing normal UV scale 8, ORM scale 4; lens micro-normal/ORM scale 10. Existing UVs are unchanged. Preserve mipmaps and linear sampling of data maps.

One Rect light: 180 cd, 3500 K, 108 × 13 cm source, 600 cm attenuation radius, local position (10.7,0,0) cm, emitting +X. Production rules retain non-shadow-casting fixture lights and zero indirect-light intensity. Review lighting/contact shadows are presentation only, not a request to alter project rendering settings.

## Authoring pipeline

`Tools/Art/kit/strip_light_materials.py` regenerates the seven texture maps using bundled Python + NumPy/Pillow. `strip_light.py -- materials` (arguments after Blender's `--`) applies them to the existing source and checks its geometry digest before/after save/reload; FBX bytes remain untouched. `-- render --4k` generates 3840-pixel-wide day/night on/off review images. `-- build` is a full generator rebuild, not needed for this material pass; do not run it merely to apply materials. The original flat-material source is retained in `source/StripLight_FlatMaterials.blend`.

`strip_light_preview.py` uses only native Unreal MCP calls for the isolated visual candidate `/Game/Art/Candidates/Kit_StripLight/Candidate01/`. Its stages `materials`, `assembly`, `shots` are art preview utilities, not runtime code. No production level or gameplay file is part of this delivery.

## Claude integration after appearance acceptance

Add the single piece to the existing shared kit catalogue/generator/setup route. Preserve mesh, pivot, slots and two identical LODs; the model already meets the M-class LOD1 limit. Consolidate these source materials into the shared kit atlas/master-material scheme without changing their visual scale. The per-piece maps are art sources, not permission to establish a second production atlas architecture.

Connect lens level and the existing building-owned Rect light together. Implement the prescribed Fixtures group, deterministic damage flicker, power/day/night/critical/destroyed state handling and distance/scalability limits through existing systems. No new per-lamp ticking actor. Do not create gameplay code from the preview's independent light actors.

Then run `WorldDivide.Art.Kit`, verify actual gameplay views and common building LOD switching at 260 m, plus representative runtime cost. Those production checks have not been performed by the art pass. The 4K Blender images and Unreal editor captures are visual evidence, not gameplay validation.
