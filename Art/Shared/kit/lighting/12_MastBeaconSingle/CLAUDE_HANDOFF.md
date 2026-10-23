# 12_MastBeaconSingle — Round 4 handoff

Issue #393, 2026-10-23. Supersedes Round 3. Producer approval and Unreal integration remain pending.

Deepened red at the dome silhouette and lower band, strengthened skirt rings, reduced whole-dome emission and tightened the smooth red halo around the compact LED column. Retained the tip-mount collar, clamp lugs, stepped stem and M-class LODs. Corrected excess UV density at the dome tip by reducing chart scale; no geometry was removed.

- Clean editable source: source/MastBeaconSingle.blend; 2 mesh object(s), seven packed runtime images.
- FBX: candidate/SM_Kit_MastBeaconSingle.fbx and candidate/SM_Kit_MastBeaconSingle_LOD1.fbx.
- Class M; triangles LOD0 842/1200 (headroom 358); LOD1 346/400 (headroom 54).
- Generator: Tools/Art/kit/mast_beacon_single_393.py; scoped lighting_393_* helpers. Protected #392 helpers unchanged.
- source/MastBeaconSingle_Preview.blend is Blender review context only. Optical masters, micro-normal inputs and bake_inputs are authoring assets, never extra runtime maps.

Metres in Blender, centimetres in FBX; Z up, +X forward, origin at base underside centre. Bounds and emitting-face checks are in evidence/verification.json. Emissive only, no real light. Tip mount; no side arm or building mast. Dome centre approximately (0,0,0.395) m.

Exactly Housing then Lens. Only optical islands use Lens. Housing has 2048² BaseColor, ORM and Finish_Normal; Lens has 1024² BaseColor, ORM, Finish_Normal and 16-bit LightProfile. BaseColor is sRGB; all other maps are linear/Non-Color. ORM = occlusion / roughness / metallic. Preserve mipmaps. No Nanite or fixture shadows. The building controls the 260 m LOD decision; do not switch each fixture separately.

Use ONE delivered DirectX Finish_Normal per slot on FinishBake / TexCoord 1, with UVMap / TexCoord 0 tangents. No normal stacking, no green flip in Unreal, no generated lightmap over UV1. The Blender authoring graph inverts green only to display the DirectX map in Blender. Both LODs own separate non-overlapping charts in the same maps. Physical texel-density measurements and final-normal coverage are in evidence/astra_technical.json.

Shader: p = LightProfile.r; hot = saturate((p − 0.55) / 0.35); colour = lerp(Tint, HotCoreTint, hot); emission = p × LampLevel × 14 × colour. Linear Tint [1,0.001,0.0004]; HotCoreTint [1,0.22,0.035]. LampLevel: off 0, night 1, LowPower 0.3; review day-on 0.05. These are linear material values, not photometric candela. Exact roughness/coat values and source recipe: material_recipe.json.

The Lens remains opaque with mapped internal optics; no transmission or changing parallax is claimed. Compact hot emission fades through a saturated coloured field. Review views have no compositor bloom; review studio/fixture lights are illustrative and do not establish Unreal photometric equivalence.

Opaque mapped glass has no true transmission/parallax. Measurable subpixel shading/brightness differences remain at the forced LOD switch; strict no-pop acceptance is still a producer/runtime review item.

Review: review/README.md and evidence/astra_check.md. Technical/export proof: verification.json, astra_technical.json, rebuild_determinism.json and delivery.json in evidence/. Round 3 is preserved and hash-verified in archive/round03/; protected live 12 root FBX and mast_beacon_single.py remain unchanged.

Reproduce with Blender 5.1 background mode and bytecode disabled: lighting_393_review.py -- prepare 12_MastBeaconSingle; run the untouched _work_393/run_pieces.py for all five twice; then verify, render, stable, sheets and audit. Run lighting_393_astra_check.py -- check and -- sheets. Use --python-exit-code 1. Sources are clean; preview scenes never enter the FBX export. Claude owns all Unreal material/state/light/LOD integration. No git or Unreal actions were performed.
