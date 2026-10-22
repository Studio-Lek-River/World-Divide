# #195 Phase B: lamp heads for the HQ's night lighting

**Why.** The producer's rule for the HQ at night is that light only comes from a lamp or window the model has. Phase A (Claude) lit the garage from its eight ceiling-edge lamps and the courtyard from the entrance windows. Two kinds of lamp the lighting needs are not in the model yet:

1. **Two façade floodlights**, which light the command façade and the emblem panel above the entrance (the `MI_HQ17_TeamPanel` panel, centred about X −130, Y 0 cm, building space). Mount them at the courtyard's edge or on a roof edge, for example the garage's front roof edge near (1500, −1000, 760) and the mirrored edge of the west block. Each one aims at the emblem from about 12–20 m, with nothing between the lamp and the panel.
2. **Two amber warning beacons** on the garage portal, one on each side of the 12.6 m opening near the top of the pillars. The pillars stand at X 1560, Y −2438 and −1078. Their blue light strips (`MI_HQ20_FixtureBlue`) stay as they are. These beacons replace free-standing stand-in lights, which have been removed.

## Constraints

- Follow `.agents/skills/world-divide-building-production/SKILL.md`. Work in a candidate preview level under `/Game/Art/Candidates/USA_HQ/`. Preserve every approved HQ design, including the H28 masts (`HANDOFF_379.md`).
- **Floodlight head:** a compact housing of about 35 × 25 × 20 cm, on a bracket or yoke with a mounting plate. The lens face is a separate surface on slot `HQ20_FixtureWarm`, and the housing and bracket are on `HQ17_DarkStructure`.
- **Beacon:** a short amber dome about 15 cm across on a base. The dome needs a new instance `MI_HQ195_BeaconAmber` on `M_HQ_Final_Operational`: EmissionColor amber at roughly (2.4, 1.0, 0.1), and the scalar `GroupFixtures` = 0, because the beacons flash from the game, not from a group. The base is on `HQ17_DarkStructure`.
- **Helpers:**
  - `SOCKET_Flood_West` and `SOCKET_Flood_East` sit at the centre of each lens, with their +X along the beam.
  - `SOCKET_GarageBeacon_South` and `SOCKET_GarageBeacon_North` sit at the centre of each dome.
  - Coordinates are in building space, in the delivery frame used by `usa_hq_game_blender.py`.
- **Budget:** the whole HQ stays within 350,000 / 40,000 triangles (LOD0/LOD1) and 80 material sections (`docs/art/MODEL_PERFORMANCE.md`). The heads add at most about 2,000 / 400 triangles in total. Put them in the existing `ShellDetail` export if they fit its slots; otherwise use one part mesh shared by both floods and one shared by both beacons, so each draws instanced.
- **Evidence:** the 12 m, 150 m and 450 m RTS views, plus one night view per head showing the lens, in the standard labelled capture set.

## Handoff to Claude

Deliver the updated FBX/LODs and hashes, the helper transforms, the new instance values and the counts, together with an implementation prompt in this folder. Claude then reruns `Tools/Art/usa_hq_light_probe.py`, adds the two `Fixtures` spot lights (from the flood helpers) and the two `GarageBeacons` point lights (at the domes) to `usa_hq_recipe.LIGHTS`, and closes #195 with day and night captures and the 8-HQ night frame-time check.

## Codex starter

> Use `$world-divide-building-production` for USA HQ #195 Phase B. Read `Art/USA/buildings/usa_hq/issue195/BRIEF_floodlights.md` and `HANDOFF_379.md`. Model two façade floodlight heads and two amber garage-portal beacons as specified, with their helpers, within the HQ budgets. Preview them in a candidate level and hand them back to Claude with an implementation prompt.
