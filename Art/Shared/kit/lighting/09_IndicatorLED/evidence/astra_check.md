# 09_IndicatorLED — independent Round 4 check

2026-10-23, issue #393. Producer approval pending. Blender art/export evidence only; no Unreal or gameplay acceptance.

The box chamfer, plug, front screws and ribbed underside match the reference construction. Night review lighting now reveals worn steel; maps remain unchanged and neutral. The elevated RTS camera sees the top of the housing and occludes the downward-facing diffuser; 150/450 m elevated views therefore do not show a bright indicator. The reference-angle blue/red/green views demonstrate the actual emitting face.

| Check | Status | Finding |
|---|---|---|
| Reference fidelity | reviewed / revised | Retained the chamfered worn-steel box, slotted end plug, two front screws and recessed ribbed underside diffuser. Increased only night review lighting for readable steel. BaseColor and LightProfile are exactly achromatic; colour variants change emission tint only. |
| Generator / export contract | pass | All five built twice through the untouched runner, matching geometry/UV/shading digests. Independent source rebuilds and all nine FBX reimports passed. |
| Budgets | pass | LOD0 222/250; headroom 28 |
| Material slots | pass | Housing then Lens; only optical chart kinds use Lens. Seven packed runtime maps per source, byte-equal to delivered PNGs. |
| UVs / normals / topology | pass | Zero degenerate geometry/UV triangles and zero positive-area overlaps per channel and across LOD atlases. Unit normals, finite orthogonal tangents, correct chart parameterization and positive closed-shell volumes. |
| Texture / finish contract | pass | Housing 2048², Lens 1024²; one DirectX Finish_Normal per slot sampled on UV1 with UV0 tangents. Maximum measured physical lens density 1843.2 px/m, below 2048. Normal-bake coverage and actual colour-space bindings checked. |
| Fixture frame | pass | Metres/centimetres roundtrip; Emissive only, no real light. Wall mounting frame faces +X; the actual underside diffuser faces −Z. Optical centre (0.047,0,-0.047) m. |
| Day / night / LowPower | reviewed | Neutral maps verified exactly achromatic; blue/red/green change only emission Tint. Worn steel is readable under revised night review lighting. |
| LOD continuity | not applicable | Class S has only LOD0. |
| Handoff / isolation | pass | Updated sources, maps, FBXs, recipes, review sheets and handoffs. 12 exports stay under candidate/. Round 3 archive hashes and protected inputs match. No git or Unreal actions. |

## Remaining judgement and limits

The underside lens is physically hidden from the elevated RTS test camera. No artificial upper emitting surface was added. The product-angle tint views, not the top view, establish optical appearance. Producer appearance approval remains pending. No approved high-poly fixture master or master-equivalence claim is invented.

## Evidence

- [Reference day](../review/reference_left_round04_right_day.png) / [night](../review/reference_left_round04_right_night.png).
- [Review index and distance sheets](../review/README.md).
- [Independent technical checks](astra_technical.json), [FBX/source verification](verification.json), [distance measurements](astra_distance_comparison.json), [determinism](rebuild_determinism.json), [final inventory](delivery.json).
