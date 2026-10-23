# 13_MastBeaconDual — Round 4 handoff

Issue #393, 2026-10-23. Supersedes Round 3. Producer approval and Unreal integration remain pending.

Applied the shared red optical changes to both heads. Retained the decided .75 head scale, raised 80 cm bolted bar, exposed gland nuts, 45 cm height and approximately 34.7 cm clamp depth. Both LODs retain the separate U halves for alternating flashes.

- Clean editable source: source/MastBeaconDual.blend; 2 mesh object(s), seven packed runtime images.
- FBX: SM_Kit_MastBeaconDual.fbx and SM_Kit_MastBeaconDual_LOD1.fbx.
- Class M; triangles LOD0 1184/1200 (headroom 16); LOD1 376/400 (headroom 24).
- Generator: Tools/Art/kit/mast_beacon_dual.py; scoped lighting_393_* helpers. Protected #392 helpers unchanged.
- source/MastBeaconDual_Preview.blend is Blender review context only. Optical masters, micro-normal inputs and bake_inputs are authoring assets, never extra runtime maps.

Metres in Blender, centimetres in FBX; Z up, +X forward, origin at base underside centre. Bounds and emitting-face checks are in evidence/verification.json. Emissive only, no real light. Dome centres approximately (0,±0.225,0.32625) m. Negative-Y head U<0.5; positive-Y head U≥0.5, in both LODs.

Exactly Housing then Lens. Only optical islands use Lens. Housing has 2048² BaseColor, ORM and Finish_Normal; Lens has 1024² BaseColor, ORM, Finish_Normal and 16-bit LightProfile. BaseColor is sRGB; all other maps are linear/Non-Color. ORM = occlusion / roughness / metallic. Preserve mipmaps. No Nanite or fixture shadows. The building controls the 260 m LOD decision; do not switch each fixture separately.

Use ONE delivered DirectX Finish_Normal per slot on FinishBake / TexCoord 1, with UVMap / TexCoord 0 tangents. No normal stacking, no green flip in Unreal, no generated lightmap over UV1. The Blender authoring graph inverts green only to display the DirectX map in Blender. Both LODs own separate non-overlapping charts in the same maps. Physical texel-density measurements and final-normal coverage are in evidence/astra_technical.json.

Shader: p = LightProfile.r; hot = saturate((p − 0.55) / 0.35); colour = lerp(Tint, HotCoreTint, hot); emission = p × LampLevel × 14 × colour. Linear Tint [1,0.001,0.0004]; HotCoreTint [1,0.22,0.035]. LampLevel: off 0, night 1, LowPower 0.3; review day-on 0.05. These are linear material values, not photometric candela. Exact roughness/coat values and source recipe: material_recipe.json.

The Lens remains opaque with mapped internal optics; no transmission or changing parallax is claimed. Compact hot emission fades through a saturated coloured field. Review views have no compositor bloom; review studio/fixture lights are illustrative and do not establish Unreal photometric equivalence.

Alternation: on TexCoord 0, negative-Y head has U 0.025…0.465; positive-Y head has U 0.525…0.965, including cap triangles. This split holds in both LODs and both channels. LOD charts use separate V rows (.025 and .525 starts); select the head with U < 0.5 / U ≥ 0.5, without moving texture coordinates. Both heads use the shared mast optical field at .75 scale.

Opaque mapped glass has no true transmission/parallax. Measurable subpixel shading/brightness differences remain at the forced LOD switch; strict no-pop acceptance is still a producer/runtime review item.

Review: review/README.md and evidence/astra_check.md. Technical/export proof: verification.json, astra_technical.json, rebuild_determinism.json and delivery.json in evidence/. Round 3 is preserved and hash-verified in archive/round03/; protected live 12 root FBX and mast_beacon_single.py remain unchanged.

Reproduce with Blender 5.1 background mode and bytecode disabled: lighting_393_review.py -- prepare 13_MastBeaconDual; run the untouched _work_393/run_pieces.py for all five twice; then verify, render, stable, sheets and audit. Run lighting_393_astra_check.py -- check and -- sheets. Use --python-exit-code 1. Sources are clean; preview scenes never enter the FBX export. Claude owns all Unreal material/state/light/LOD integration. No git or Unreal actions were performed.
