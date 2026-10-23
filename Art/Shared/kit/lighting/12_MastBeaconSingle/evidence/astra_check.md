# 12_MastBeaconSingle — independent Round 4 check

2026-10-23, issue #393. Producer approval pending. Blender art/export evidence only; no Unreal or gameplay acceptance.

Tip-mounted reference variant preserves the collar, lugs, flared base and short stem. The day view retains the LED carrier with darker red edges and stronger skirt rings. Night and 30% LowPower remain red with a compact warm core and smooth local halo. The 12 m mesh reads as a small fixture; at 150/450 m it becomes a red signal cluster. Mapped optics do not reproduce refractive depth.

| Check | Status | Finding |
|---|---|---|
| Reference fidelity | reviewed / revised | Deepened red at the dome silhouette and lower band, strengthened skirt rings, reduced whole-dome emission and tightened the smooth red halo around the compact LED column. Retained the tip-mount collar, clamp lugs, stepped stem and M-class LODs. Corrected excess UV density at the dome tip by reducing chart scale; no geometry was removed. |
| Generator / export contract | pass | All five built twice through the untouched runner, matching geometry/UV/shading digests. Independent source rebuilds and all nine FBX reimports passed. |
| Budgets | pass | LOD0 842/1200; headroom 358. LOD1 346/400; headroom 54 |
| Material slots | pass | Housing then Lens; only optical chart kinds use Lens. Seven packed runtime maps per source, byte-equal to delivered PNGs. |
| UVs / normals / topology | pass | Zero degenerate geometry/UV triangles and zero positive-area overlaps per channel and across LOD atlases. Unit normals, finite orthogonal tangents, correct chart parameterization and positive closed-shell volumes. |
| Texture / finish contract | pass | Housing 2048², Lens 1024²; one DirectX Finish_Normal per slot sampled on UV1 with UV0 tangents. Maximum measured physical lens density 1940.8 px/m, below 2048. Normal-bake coverage and actual colour-space bindings checked. |
| Fixture frame | pass | Metres/centimetres roundtrip; Emissive only, no real light. Tip mount; no side arm or building mast. Dome centre approximately (0,0,0.395) m. |
| Day / night / LowPower | reviewed | Saturated amber/red field around a compact hot core; 30% LowPower retains colour. Opaque internals remain an approximation. |
| LOD continuity | measured | No disappearing components. Projected 260 m convex-outline difference 0.0978 px; see shading differences below. No bit-identical or runtime claim. |
| Handoff / isolation | pass | Updated sources, maps, FBXs, recipes, review sheets and handoffs. 12 exports stay under candidate/. Round 3 archive hashes and protected inputs match. No git or Unreal actions. |

## Measured image differences

| Distance | Pixels changed >1/255 | Maximum display RGB delta | Source |
|---|---:|---:|---|
| 12 m | 2573 | 0.9176 | distance |
| 150 m | 24 | 0.1137 | stable |
| 260 m | 11 | 0.1490 | stable |
| 450 m | 6 | 0.0314 | distance |

1920 × 1080, 60° horizontal FOV; 150/260 m stable comparisons use 512 fixed samples without denoising/adaptive sampling. The centre region is rendered at native pixel scale. Figures include shading and antialiasing, not just geometry. Forced LOD1 at 12 m is diagnostic, not intended runtime use.

## Remaining judgement and limits

Opaque mapped glass has no true transmission/parallax. Measurable subpixel shading/brightness differences remain at the forced LOD switch; strict no-pop acceptance is still a producer/runtime review item. Producer appearance approval remains pending. No approved high-poly fixture master or master-equivalence claim is invented.

The native switch pair stays a tiny red cluster with no obvious outline loss, but the measured brightness variation prevents an unconditional strict no-pop sign-off. Review the actual pair before runtime acceptance.

## Evidence

- [Reference day](../review/reference_left_round04_right_day.png) / [night](../review/reference_left_round04_right_night.png).
- [Review index and distance sheets](../review/README.md).
- [Independent technical checks](astra_technical.json), [FBX/source verification](verification.json), [distance measurements](astra_distance_comparison.json), [determinism](rebuild_determinism.json), [final inventory](delivery.json).
