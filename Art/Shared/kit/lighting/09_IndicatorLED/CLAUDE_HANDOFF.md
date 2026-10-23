# 09_IndicatorLED — Round 4 handoff

Issue #393, 2026-10-23. Supersedes Round 3. Producer approval and Unreal integration remain pending.

Retained the chamfered worn-steel box, slotted end plug, two front screws and recessed ribbed underside diffuser. Increased only night review lighting for readable steel. BaseColor and LightProfile are exactly achromatic; colour variants change emission tint only.

- Clean editable source: source/IndicatorLED.blend; 1 mesh object(s), seven packed runtime images.
- FBX: SM_Kit_IndicatorLED.fbx.
- Class S; triangles LOD0 222/250 (headroom 28).
- Generator: Tools/Art/kit/indicator_led.py; scoped lighting_393_* helpers. Protected #392 helpers unchanged.
- source/IndicatorLED_Preview.blend is Blender review context only. Optical masters, micro-normal inputs and bake_inputs are authoring assets, never extra runtime maps.

Metres in Blender, centimetres in FBX; Z up, +X forward, origin at mounting-face centre. Bounds and emitting-face checks are in evidence/verification.json. Emissive only, no real light. Wall mounting frame faces +X; the actual underside diffuser faces −Z. Optical centre (0.047,0,-0.047) m.

Exactly Housing then Lens. Only optical islands use Lens. Housing has 2048² BaseColor, ORM and Finish_Normal; Lens has 1024² BaseColor, ORM, Finish_Normal and 16-bit LightProfile. BaseColor is sRGB; all other maps are linear/Non-Color. ORM = occlusion / roughness / metallic. Preserve mipmaps. No Nanite or fixture shadows. The building controls the 260 m LOD decision; do not switch each fixture separately.

Use ONE delivered DirectX Finish_Normal per slot on FinishBake / TexCoord 1, with UVMap / TexCoord 0 tangents. No normal stacking, no green flip in Unreal, no generated lightmap over UV1. The Blender authoring graph inverts green only to display the DirectX map in Blender. Both LODs own separate non-overlapping charts in the same maps. Physical texel-density measurements and final-normal coverage are in evidence/astra_technical.json.

Shader: p = LightProfile.r; hot = saturate((p − 0.55) / 0.35); colour = lerp(Tint, HotCoreTint, hot); emission = p × LampLevel × 10 × colour. Linear Tint [1,1,1]; HotCoreTint [1,1,1]. LampLevel: off 0, night 1, LowPower 0.3; review day-on 0.05. These are linear material values, not photometric candela. Exact roughness/coat values and source recipe: material_recipe.json.

Neutral BaseColor and LightProfile have zero RGB channel difference. Change only Tint per mount; HotCoreTint stays white. Blue/red/green review examples are not three separate texture sets.

The underside lens is physically hidden from the elevated RTS test camera. No artificial upper emitting surface was added. The product-angle tint views, not the top view, establish optical appearance.

Review: review/README.md and evidence/astra_check.md. Technical/export proof: verification.json, astra_technical.json, rebuild_determinism.json and delivery.json in evidence/. Round 3 is preserved and hash-verified in archive/round03/; protected live 12 root FBX and mast_beacon_single.py remain unchanged.

Reproduce with Blender 5.1 background mode and bytecode disabled: lighting_393_review.py -- prepare 09_IndicatorLED; run the untouched _work_393/run_pieces.py for all five twice; then verify, render, stable, sheets and audit. Run lighting_393_astra_check.py -- check and -- sheets. Use --python-exit-code 1. Sources are clean; preview scenes never enter the FBX export. Claude owns all Unreal material/state/light/LOD integration. No git or Unreal actions were performed.
