# 05_DirectionalSpot — Round 4 Claude handoff

**Producer appearance approval — 2026-10-23:** "Yes they are all approved", given per piece on the Round 4b comparison sheets (reference, day, night, LOD0/LOD1). This approves the current model and finish; production integration and runtime validation are Claude's (#392).


2026-10-23, issue #392. Independent art/export check completed; producer approval given 2026-10-23 (see above). This record supersedes Round 3. No remaining blocking Blender model/export defect identified. Producer appearance approval remains pending.

## Construction and changes

Flared cylindrical can and recessed honeycomb reflector, single slotted swivel arm, pivot hardware, two-bolt wall plate, clamp, glands and braided return cable.

Preserved the mechanical construction. Corrected the backward-facing open lens, verified its outward normal through FBX reimport, rebuilt both LOD atlas/normal bindings, refined cast finish and restored centre-weighted honeycomb emission.

Can approximately 24.6 cm axially, with a 20.4 cm flared mouth; plate 10 × 30 cm. Nominal body dimensions follow the approximate 18 × 24 cm archetype.

Round 4b: replaced grain-gated dashed edge wear with continuous irregular rubbing and sparse wider chips. Geometry, dimensions and UV layouts are unchanged.

## Import files and frame

- source/DirectionalSpot.blend: clean editable two-LOD source with seven used, packed runtime images.
- SM_Kit_DirectionalSpot.fbx / SM_Kit_DirectionalSpot_LOD1.fbx: **2466 / 718 triangles**, limits 2500 / 800, headroom 34 / 82. Exactly **Housing, Lens**, in that order. Only optical surfaces use Lens.
- textures/: seven runtime maps; material_recipe.json is the binding/value record. source/bake_inputs is authoring-only.
- source/DirectionalSpot_Preview.blend and DirectionalSpot_ReferencePreview.blend: separate Blender review scenes, never import their context or lights.

Blender metres, runner-exported centimetres. +X is out of the wall/forward; Z is up. Origin is the mounting plate back-face centre. Bounds in cm: [[0,-10.2,-15],[46.554,10.2,29.292]]. Lens bounds: [[33.337,-7.9,1.408],[41.237,7.9,15.092]]. Combined lens-centre anchor: **[37.287,0,8.25] cm**. Light direction: **[0.866025,0,-0.5]**; local pitch -30 degrees. Catalogue starting values: Spot, 600 cd, 3000 K, 2000 cm radius, cone [10,30] degrees. 

## Materials and UVs

Housing BaseColor/ORM/Finish_Normal are 2048². Lens BaseColor/ORM/Finish_Normal/LightProfile are 1024². BaseColor: sRGB on. ORM, LightProfile and normals: sRGB off. ORM = R occlusion, G roughness, B metallic. Preserve mipmaps; import normals with normal-map compression and **no green-channel flip** in Unreal (the supplied maps are DirectX).

**One final normal per slot:** T_DirectionalSpot_Housing_Finish_Normal.png and T_DirectionalSpot_Lens_Finish_Normal.png. Sample them with TexCoord 1 / FinishBake, retaining UV0 / UVMap as the tangent basis. Do not stack another normal or auto-generate lightmap UVs over channel 1. Both LODs have disjoint, explicitly authored charts in the SAME seven maps; both are painted and normal-baked. UV1 charts preserve UV0 axis orientation with positive isotropic scaling. This repairs the former LOD1 sampling of unrelated LOD0 charts. Preserve both UV channels and import the supplied normals/tangents.

Planar housing UV0 targets 512 px/m; curved/bevel density measurements are in verification.json. UV1 carries denser finish detail. Valid baked bevel normals are retained; invalid negative/grazing samples on intersecting hidden surfaces use the authored surface-detail normal on the same UV1. This correction is already inside the single final map; normal_bake_repair.json explains the pixel accounting, including unused atlas space.

Coating is dielectric, with exposed metal chips and local oxidation, strongest on 02. Housing roughness is centred near 0.48. Lens ORM roughness is clamped to 0.18–0.45 by the authoring helper; coat roughness is 0.15, coat weight 0.32, IOR 1.49 and subsurface weight 0.08 at 1.5 mm scale. Optics use opaque subsurface/clear-coat approximations, not refractive transmission.

Emission = linear tint **[1,0.44,0.16] × LightProfile.r × 6 × LampLevel**. Tint approximates 3000 K in normalized linear RGB. LampLevel 0/1 is editable in Blender; Claude connects game state. Day review uses 0.05 or 0; night-on uses 1.

## Evidence and remaining work

[Independent nine-item audit](evidence/astra_check.md), [technical audit](evidence/astra_technical.json), [FBX/source verification](evidence/verification.json), [distance measurements](evidence/astra_distance_comparison.json), [review index](review/README.md), [artifact hashes](evidence/delivery.json).

Maximum projected fixture-outline difference: 0.0408 px at 150 m / 0.0235 px at 260 m. Lens-outline difference: 0.0299 / 0.0172 px. Native-distance renders were inspected at 150/260 m; pixel shading differences are measured rather than claimed identical. Enlarged LOD sheets deliberately expose simplification at a size never used for the switch. 

No Unreal editor, catalogue, gameplay integration or git commit was performed. Claude owns production import/material/atlas adoption, collision, power/state/light logic, and actual gameplay LOD validation after art acceptance. No separate high-poly master is claimed: the distance sheets compare the delivered LOD0 and LOD1. Round 3 recovery files and generators are in archive/round03/.

## Reproduce

Use Blender 5.1.2 with --factory-startup -b --python-exit-code 1. Run Tools/Art/kit/wall_lamp_render.py -- prepare 05_DirectionalSpot; then Tools/Art/kit_blender.py (it builds every catalogue piece; the temporary Tools/Art/kit_blender.py was removed at integration); then wall_lamp_render.py -- verify / render / refmatch / sheets / compare 05_DirectionalSpot as separate stages. Run Tools/Art/kit/wall_lamp_astra_check.py for the independent source audit, and -- visual after renders. For 06, -- stable captures the additional high-sample distance evidence. The final audit may be repeated without overwriting source geometry. prepare deliberately regenerates source/maps.
