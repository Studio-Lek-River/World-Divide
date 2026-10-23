# 07_ServiceBayLight — independent Round 4 check

2026-10-23, issue #393. Producer approval pending. Blender art/export evidence only; no Unreal or gameplay acceptance.

Spec-led panel: recessed depth, real enamel returns and trim/latches read clearly. The 04 image establishes the metal/diffuser family, not this panel silhouette. At 12 m the frame remains readable; at 150 m the lit rectangle persists and at 450 m it reduces to a small pale signal.

| Check | Status | Finding |
|---|---|---|
| Reference fidelity | reviewed / revised | Retained the recessed 60 × 30 cm panel, stepped graphite trim, enamel reflector, corner screws and latch clips. The prismatic neutral diffuser remains centre-weighted; the rear tray sits behind the mounting plane. |
| Generator / export contract | pass | All five built twice through the untouched runner, matching geometry/UV/shading digests. Independent source rebuilds and all nine FBX reimports passed. |
| Budgets | pass | LOD0 452/1200; headroom 748. LOD1 164/400; headroom 236 |
| Material slots | pass | Housing then Lens; only optical chart kinds use Lens. Seven packed runtime maps per source, byte-equal to delivered PNGs. |
| UVs / normals / topology | pass | Zero degenerate geometry/UV triangles and zero positive-area overlaps per channel and across LOD atlases. Unit normals, finite orthogonal tangents, correct chart parameterization and positive closed-shell volumes. |
| Texture / finish contract | pass | Housing 2048², Lens 1024²; one DirectX Finish_Normal per slot sampled on UV1 with UV0 tangents. Maximum measured physical lens density 1290.2 px/m, below 2048. Normal-bake coverage and actual colour-space bindings checked. |
| Fixture frame | pass | Metres/centimetres roundtrip; Rect, 300 cd, 4000 K, attenuation 12 m. Aperture 0.532 × 0.232 m; anchor (-0.004,0,0) m, direction +X. Mount pitch places it on the ceiling. |
| Day / night / LowPower | reviewed | Neutral centre-weighted prismatic panel, enamel returns and lit/unlit context retained. |
| LOD continuity | measured | No disappearing components. Projected 260 m convex-outline difference 0.0076 px; see shading differences below. No bit-identical or runtime claim. |
| Handoff / isolation | pass | Updated sources, maps, FBXs, recipes, review sheets and handoffs. 12 exports stay under candidate/. Round 3 archive hashes and protected inputs match. No git or Unreal actions. |

## Measured image differences

| Distance | Pixels changed >1/255 | Maximum display RGB delta | Source |
|---|---:|---:|---|
| 12 m | 1397 | 0.3373 | distance |
| 150 m | 14 | 0.0235 | stable |
| 260 m | 4 | 0.0196 | stable |
| 450 m | 2 | 0.0157 | distance |

1920 × 1080, 60° horizontal FOV; 150/260 m stable comparisons use 512 fixed samples without denoising/adaptive sampling. The centre region is rendered at native pixel scale. Figures include shading and antialiasing, not just geometry. Forced LOD1 at 12 m is diagnostic, not intended runtime use.

## Remaining judgement and limits

No producer image for the panel itself; appearance remains spec-led. Blender camera evidence is not Unreal/gameplay acceptance. Producer appearance approval remains pending. No approved high-poly fixture master or master-equivalence claim is invented.

## Evidence

- [Reference day](../review/reference_left_round04_right_day.png) / [night](../review/reference_left_round04_right_night.png).
- [Review index and distance sheets](../review/README.md).
- [Independent technical checks](astra_technical.json), [FBX/source verification](verification.json), [distance measurements](astra_distance_comparison.json), [determinism](rebuild_determinism.json), [final inventory](delivery.json).
