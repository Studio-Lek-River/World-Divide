# USA HQ — Candidate 11 layout specification

Status: **Candidate 11 layout approved by the producer on 2026-10-20**. This revision supersedes the old 28 × 28 m and Candidate 9/10 40 × 40 m layouts, the 24 m height, diagonal entrance and mandatory 8 m connectors. Producer authorized the new dimensions and Image #2 precedence in this chat on 2026-10-20. The producer subsequently confirmed: "the layouts now is correct".

## Reference authority and coordinates

- Image #2 top view governs horizontal layout, elevations guide heights, 3/4 view checks spatial reading. Image #1 only supplies compatible information.
- 56 m width × 38 m depth × 18 m tallest mast. Origin is ground-centred. Unreal +X is front and garage exit; +Y is west; Z up. Bounds X ±19 m, Y ±28 m, Z 0…18 m. Blender mirrors Y as in earlier candidates.
- West band Y +10…+28 m; central band −10…+10 m; east band −28…−10 m. Entrance, plinth and courtyard flag share Y=0; both entrance and garage face +X.
- Raw reference calibration is the full slab rectangle at pixels (873,74)…(1506,505) in the unmodified 1536 × 1024 sheet. Pixel-to-metre transform: X=(v−74)×38/431−19; Y=28−(u−873)×56/633. The concept aspect differs from 56/38 by approximately 0.3%; this global calibration is applied once, never per building.

## Explicit departures and uncertainty

The picture is internally inconsistent. These adjustments implement the producer's confirmed dimensions and centred axis; they are not a claim that every source silhouette matches within 0.5 m.

1. Core/courtyard axis at source u=1172 is shifted right 17.5 pixels / **1.548 m** to the slab centre. Core tiers, plinth, steps, planters and inner link ends follow together.
2. Source garage inner edge u=1280 crosses the central band. Its inner edge moves **1.994 m east** to Y=−10, retaining the outer edge. This narrows the hall; the clear door remains 12.6 m wide.
3. Two occluded connector terminals extend into their host walls to remove disconnected point contacts. The overlay distinguishes these from raw traced outlines.
4. Radar tower position behind the garage is inferred from the 3/4 view. The top sheet does not resolve it reliably. West block and east-rear outlines are traced without shifts; rear service clutter and wall thickness beneath overhanging roofs are not authoritative measurements.
5. Roof elevations are estimates anchored to the confirmed 18 m tip, not dimensions supplied by the reference. Core tiers: 8.2 / 11.5 / 12.3 m. Garage: 7.6 m. The 15 mm roof membranes sit above scheduled parapets.

## Primary coordinate schedule

All numbers in metres. Bounds do not replace polygon outlines: exact traced vertices, raw pixels, normalized pixels and notes live in `Art/USA/buildings/usa_hq/candidate11_manifest.json`.

| Module | Local X bounds | Local Y bounds | Base … roof |
|---|---|---|---|
| CoreBase | -16.355 … -1.719 | -7.785 … 7.785 | 0.050 … 8.200 |
| CoreUpper | -17.060 … -5.070 | -6.104 … 6.104 | 8.215 … 11.500 |
| CoreDrum | -13.798 … -7.803 | -3.008 … 3.008 | 11.515 … 12.300 |
| WingRear | -14.768 … -4.805 | 14.464 … 25.434 | 0.050 … 8.400 |
| WingMiddle | -3.483 … 3.659 | 15.172 … 24.992 | 0.050 … 6.800 |
| WingFront | 4.981 … 14.592 | 12.253 … 24.373 | 0.050 … 7.200 |
| WingLinkRear | -4.805 … -3.483 | 16.057 … 23.754 | 0.050 … 4.900 |
| WingLinkFront | 3.659 … 4.981 | 16.145 … 23.223 | 0.050 … 4.900 |
| EastRear | -14.768 … -4.452 | -23.754 … -11.899 | 0.050 … 7.800 |
| WestRearLink | -11.682 … -1.367 | 3.627 … 14.464 | 0.050 … 4.800 |
| WestFrontLink | -3.747 … 5.863 | 5.750 … 15.172 | 0.050 … 4.800 |
| EastRearLink | -11.682 … -1.014 | -11.899 … -3.804 | 0.050 … 4.800 |
| EastFrontLink | -5.422 … 2.954 | -10.660 … -3.804 | 0.050 … 4.800 |
| Garage | -0.749 … 14.944 | -25.169 … -10.000 | 0.050 … 7.600 |
| Courtyard | -1.719 … 16.619 | -9.555 … 9.555 | 0.050 … 0.600 |
| Plinth | 9.742 … 14.680 | -3.893 … 3.893 | 0.600 … 1.800 |
| Step0 | 16.619 … 17.060 | -4.777 … 4.777 | 0.050 … 0.450 |
| Step1 | 17.060 … 17.501 | -5.043 … 5.043 | 0.050 … 0.300 |
| Step2 | 17.501 … 17.942 | -5.308 … 5.308 | 0.050 … 0.150 |
| WestPlanter | 2.865 … 7.009 | 4.512 … 8.139 | 0.600 … 1.400 |
| EastPlanter | 2.865 … 7.009 | -8.139 … -4.512 | 0.600 … 1.400 |
| RadarTower | -3.835 … -0.749 | -14.022 … -10.572 | 0.050 … 9.200 |

## Garage and integration handoff

- Clear opening **12.6 × 5.6 m**, clear bay depth **15.044 m**, ceiling **6.65 m** above slab. These exceed the retained minima 7 × 4.5 × 8.5 m. A 7 m wide swept exit path is checked through the full bay to X=19 m.
- Spawn socket: (7.4224, -17.5845, 0.0500) m. Door exit: (14.9443, -17.5845, 0.0500) m. All eight helpers are in the FBX and manifest; door, radar and dish retain separate pivots.
- Claude must update the gameplay footprint to **38 × 56 cells in local X/Y**, width/depth handling in placement, the lateral exit offset to **-1758.45 cm**, and spawn/rally tests. At yaw zero the 6.5 m vehicle's previous clearance formula gives an external spawn X=2425 cm and lane end X=3075 cm; rotate these with building yaw. Audit nav projection and narrow-agent early returns.
- Import at MeshScale (1,1,1), no fitting stretch. Mast tip remains in static geometry. Production geometry, collision, LODs, animation and runtime materials are not changed here.
- The combined FBX is a modular review export, not an engine-ready merged root. Claude must rebuild collision/LODs and integrate the final accepted structure. The door is stored closed and hidden for open-bay review renders; roll-up mechanics remain unresolved from Candidate 10.

## Acceptance evidence and preserved work

- Source checks: 36 passed. Independent FBX re-import checks: 128 passed. Bounds tolerance 1 cm; module/pivot checks 1 mm. Root triangles 5,488; movable triangles 460.
- Raw trace agreement ≤0.5 m applies only to the six unadjusted west/east-rear outlines. Adjusted and inferred geometry is explicitly listed above. This is manual tracing, not a photogrammetric reconstruction.
- Review the top overlay, matched review sheet, orthographic elevations and actual RTS views. The 30 m close RTS camera intentionally crops this large building; the full campus is assessed in the orthographic and dedicated 3/4 views.
- Candidate 10 sources were copied from the existing usa-hq-realism worktree; Candidate 11 is a separate generator branch. Historic round 8 model, FBX and renders in this checkout remain intact, and Candidates 9/10 in the original worktree remain unchanged. Hash audit protects 360 files. Before-change generator/spec copies are under `source/candidate11_baseline/`.
- Existing Candidate 10 4K maps, cloth animation, LODs and material work remain preserved in that worktree for the later detail pass. Candidate 11 uses simple materials and static flag poses only. No texture downgrade or overwrite was applied to Candidate 10.
- Existing material/team-colour policy and triangle ceilings remain applicable to the final asset. The prior complete spec is archived under `source/candidate11_baseline/main_round8/docs/art/usa_hq/USA_HQ_SPEC.md` for historical non-layout requirements.

No Unreal asset, Source/, Data/, gameplay code, commit, push or PR was changed. Layout acceptance is recorded. Candidate 12 begins the wall-material pass; its finish remains under review. See `Art/USA/buildings/usa_hq/candidate12_README.md`.
