# 13_MastBeaconDual — independent Round 4 check

2026-10-23, issue #393. Producer approval pending. Blender art/export evidence only; no Unreal or gameplay acceptance.

Two .75-scale shared heads, raised bolted bar and exposed glands retain the decided construction and envelope. Both heads remain signal red at night and LowPower, with compact hot columns. At 150 m two light points are readable; at 450 m their footprints approach/merge into a tiny red cluster. The original 376-triangle LOD1 is retained: tested rounder-crown reallocations increased the measured switch difference.

| Check | Status | Finding |
|---|---|---|
| Reference fidelity | reviewed / revised | Applied the shared red optical changes to both heads. Retained the decided .75 head scale, raised 80 cm bolted bar, exposed gland nuts, 45 cm height and approximately 34.7 cm clamp depth. Both LODs retain the separate U halves for alternating flashes. |
| Generator / export contract | pass | All five built twice through the untouched runner, matching geometry/UV/shading digests. Independent source rebuilds and all nine FBX reimports passed. |
| Budgets | pass | LOD0 1184/1200; headroom 16. LOD1 376/400; headroom 24 |
| Material slots | pass | Housing then Lens; only optical chart kinds use Lens. Seven packed runtime maps per source, byte-equal to delivered PNGs. |
| UVs / normals / topology | pass | Zero degenerate geometry/UV triangles and zero positive-area overlaps per channel and across LOD atlases. Unit normals, finite orthogonal tangents, correct chart parameterization and positive closed-shell volumes. |
| Texture / finish contract | pass | Housing 2048², Lens 1024²; one DirectX Finish_Normal per slot sampled on UV1 with UV0 tangents. Maximum measured physical lens density 1649.1 px/m, below 2048. Normal-bake coverage and actual colour-space bindings checked. |
| Fixture frame | pass | Metres/centimetres roundtrip; Emissive only, no real light. Dome centres approximately (0,±0.225,0.32625) m. Negative-Y head U<0.5; positive-Y head U≥0.5, in both LODs. |
| Day / night / LowPower | reviewed | Saturated amber/red field around a compact hot core; 30% LowPower retains colour. Opaque internals remain an approximation. |
| LOD continuity | measured | No disappearing components. Projected 260 m convex-outline difference 0.0643 px; see shading differences below. No bit-identical or runtime claim. |
| Handoff / isolation | pass | Updated sources, maps, FBXs, recipes, review sheets and handoffs. 12 exports stay under candidate/. Round 3 archive hashes and protected inputs match. No git or Unreal actions. |

## Measured image differences

| Distance | Pixels changed >1/255 | Maximum display RGB delta | Source |
|---|---:|---:|---|
| 12 m | 3418 | 0.9216 | distance |
| 150 m | 39 | 0.3843 | stable |
| 260 m | 18 | 0.1922 | stable |
| 450 m | 9 | 0.0588 | distance |

1920 × 1080, 60° horizontal FOV; 150/260 m stable comparisons use 512 fixed samples without denoising/adaptive sampling. The centre region is rendered at native pixel scale. Figures include shading and antialiasing, not just geometry. Forced LOD1 at 12 m is diagnostic, not intended runtime use.

## Remaining judgement and limits

Opaque mapped glass has no true transmission/parallax. Measurable subpixel shading/brightness differences remain at the forced LOD switch; strict no-pop acceptance is still a producer/runtime review item. Producer appearance approval remains pending. No approved high-poly fixture master or master-equivalence claim is invented.

The native switch pair stays a tiny red cluster with no obvious outline loss, but the measured brightness variation prevents an unconditional strict no-pop sign-off. Review the actual pair before runtime acceptance.

## Evidence

- [Reference day](../review/reference_left_round04_right_day.png) / [night](../review/reference_left_round04_right_night.png).
- [Review index and distance sheets](../review/README.md).
- [Independent technical checks](astra_technical.json), [FBX/source verification](verification.json), [distance measurements](astra_distance_comparison.json), [determinism](rebuild_determinism.json), [final inventory](delivery.json).
