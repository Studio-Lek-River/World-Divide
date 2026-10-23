# 06_RoofFloodLight — Round 4 Claude handoff

**Producer appearance approval — 2026-10-23:** "Yes they are all approved", given per piece on the Round 4b comparison sheets (reference, day, night, LOD0/LOD1). This approves the current model and finish; production integration and runtime validation are Claude's (#392).


2026-10-23, issue #392. Independent art/export check completed; producer approval given 2026-10-23 (see above). This record supersedes Round 3. OPEN: dimensional authority for 06. The appearance table’s approximate 80 × 40 × 75 cm envelope conflicts with the detailed head/base dimensions. This delivery retains the detailed brief and measures 82.4 × 27.9 × 70.9 cm (width/depth/height). A producer clarification was requested; no answer or approval is assumed. Do not stretch the export silently.

## Construction and changes

Twin 30 × 10 × 38 cm heads, each with three LED fields beneath an opaque clear-coated cover, sixteen full-width rectangular rear fins, pivots, U-yoke, post, base fasteners and cable feeds.

Replaced sparse triangular fin spikes with sixteen 3 mm thick rectangular fins on EACH head in BOTH LODs. Removed buried duplicate LED planes; the clear-coated covers carry the three fields, with physical front separators retained. Simplified concealed LOD1 rail faces, bolt backs and cable sections to stay at 800 triangles. Preserved emitting-face winding and restored the producer-matched neutral-white display tint for the specified 4000 K floodlight.

Detailed brief dimensions retained: heads 30 × 10 × 38 cm, base 40 × 25 cm, yoke about 80 cm. Measured complete envelope is 82.4 cm wide × 27.9 cm deep × 70.9 cm high; BUILDING_KIT instead lists an approximate 80 × 40 × 75 cm envelope.

Round 4b: replaced grain-gated dashed edge wear with continuous irregular rubbing and sparse wider chips. Geometry, dimensions and UV layouts are unchanged. Restored the reference-matched neutral-white 4000 K appearance with linear emission tint [0.89,0.94,1]; Lens BaseColor and achromatic LightProfile remain unchanged.

## Import files and frame

- source/RoofFloodLight.blend: clean editable two-LOD source with seven used, packed runtime images.
- SM_Kit_RoofFloodLight.fbx / SM_Kit_RoofFloodLight_LOD1.fbx: **2288 / 800 triangles**, limits 2500 / 800, headroom 212 / 0. Exactly **Housing, Lens**, in that order. Only optical surfaces use Lens.
- textures/: seven runtime maps; material_recipe.json is the binding/value record. source/bake_inputs is authoring-only.
- source/RoofFloodLight_Preview.blend and RoofFloodLight_ReferencePreview.blend: separate Blender review scenes, never import their context or lights.

Blender metres, runner-exported centimetres. +X is out of the wall/forward; Z is up. Origin is the base plate underside centre. Bounds in cm: [[-15.417,-41.2,0],[12.5,41.2,70.867]]. Lens bounds: [[-0.188,-32.3,34.072],[8.302,32.3,65.754]]. Combined lens-centre anchor: **[4.057,0,49.913] cm**. Light direction: **[0.965926,0,-0.258819]**; local pitch -15 degrees. Catalogue starting values: Spot, 500 cd, 4000 K, 2000 cm radius, cone [25,55] degrees. The combined anchor lies between the two heads. Preview scenes use two area sources; Claude retains the single catalogue light contract.

## Materials and UVs

Housing BaseColor/ORM/Finish_Normal are 2048². Lens BaseColor/ORM/Finish_Normal/LightProfile are 1024². BaseColor: sRGB on. ORM, LightProfile and normals: sRGB off. ORM = R occlusion, G roughness, B metallic. Preserve mipmaps; import normals with normal-map compression and **no green-channel flip** in Unreal (the supplied maps are DirectX).

**One final normal per slot:** T_RoofFloodLight_Housing_Finish_Normal.png and T_RoofFloodLight_Lens_Finish_Normal.png. Sample them with TexCoord 1 / FinishBake, retaining UV0 / UVMap as the tangent basis. Do not stack another normal or auto-generate lightmap UVs over channel 1. Both LODs have disjoint, explicitly authored charts in the SAME seven maps; both are painted and normal-baked. UV1 charts preserve UV0 axis orientation with positive isotropic scaling. This repairs the former LOD1 sampling of unrelated LOD0 charts. Preserve both UV channels and import the supplied normals/tangents.

Planar housing UV0 targets 512 px/m; curved/bevel density measurements are in verification.json. UV1 carries denser finish detail. Valid baked bevel normals are retained; invalid negative/grazing samples on intersecting hidden surfaces use the authored surface-detail normal on the same UV1. This correction is already inside the single final map; normal_bake_repair.json explains the pixel accounting, including unused atlas space.

Coating is dielectric, with exposed metal chips and local oxidation, strongest on 02. Housing roughness is centred near 0.48. Lens ORM roughness is clamped to 0.18–0.45 by the authoring helper; coat roughness is 0.025, coat weight 1, IOR 1.49 and subsurface weight 0.08 at 1.5 mm scale. Optics use opaque subsurface/clear-coat approximations, not refractive transmission.

Emission = linear tint **[0.89,0.94,1] × LightProfile.r × 5 × LampLevel**. Tint matches the producer’s neutral-white 4000 K appearance. The neutral Lens BaseColor is unchanged; LightProfile is achromatic and modulates intensity only. LampLevel 0/1 is editable in Blender; Claude connects game state. Day review uses 0.05 or 0; night-on uses 1.

## Evidence and remaining work

[Independent nine-item audit](evidence/astra_check.md), [technical audit](evidence/astra_technical.json), [FBX/source verification](evidence/verification.json), [distance measurements](evidence/astra_distance_comparison.json), [review index](review/README.md), [artifact hashes](evidence/delivery.json).

Maximum projected fixture-outline difference: 0.0225 px at 150 m / 0.0130 px at 260 m. Lens-outline difference: 0.0000 / 0.0000 px. Native-distance renders were inspected at 150/260 m; pixel shading differences are measured rather than claimed identical. Enlarged LOD sheets deliberately expose simplification at a size never used for the switch. The additional stable_150m/260m frames use 512 samples without denoising and a documented native-resolution centre render region. [Rear view](review/rear_fins.png) shows both sixteen-fin stacks.

No Unreal editor, catalogue, gameplay integration or git commit was performed. Claude owns production import/material/atlas adoption, collision, power/state/light logic, and actual gameplay LOD validation after art acceptance. No separate high-poly master is claimed: the distance sheets compare the delivered LOD0 and LOD1. Round 3 recovery files and generators are in archive/round03/.

## Reproduce

Use Blender 5.1.2 with --factory-startup -b --python-exit-code 1. Run Tools/Art/kit/wall_lamp_render.py -- prepare 06_RoofFloodLight; then Tools/Art/kit_blender.py (it builds every catalogue piece; the temporary Tools/Art/kit_blender.py was removed at integration); then wall_lamp_render.py -- verify / render / refmatch / sheets / compare 06_RoofFloodLight as separate stages. Run Tools/Art/kit/wall_lamp_astra_check.py for the independent source audit, and -- visual after renders. For 06, -- stable captures the additional high-sample distance evidence. The final audit may be repeated without overwriting source geometry. prepare deliberately regenerates source/maps.
