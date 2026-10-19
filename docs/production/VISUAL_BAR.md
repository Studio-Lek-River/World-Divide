# World Divide: the visual bar

What the visual reviewer (`wd-visual-reviewer`, run by `/director`) judges captures against. The producer owns this file: every line is a decision, not a suggestion. Keep it short and concrete; when a look is signed off in PIE, add the capture that proves it under **Approved looks**, so the next review compares against a picture instead of a description.

## Direction

- Grounded modern warfare, realistic materials and lighting, read from RTS distance. Not stylised, not cartoon colours.
- Original IP only: no names, art or UI from C&C Generals or any other game. References are for quality and behaviour, never for copying.
- Gameplay readability beats spectacle: an effect may never hide a unit's position, its team colour or the ground it stands on for longer than a moment.

## Distances

From `docs/tech/scale/WORLD_SCALE_STANDARD.md` section 6 (60° FOV, pitch -55°):

| Zoom | Arm | Ground across the screen | Must read |
|---|---|---|---|
| Default | about 150 m | about 170 m | Unit type, team colour, firing, damage state, every effect's purpose |
| Farthest | 450 m | about 520 m | Unit positions and fights; effects may simplify but not vanish abruptly |
| Close-up | `unitshots` close-up | | Material quality, no stretched textures, no floating parts |

Scale: units at real size, buildings about 0.6 to 0.7 times real. The M20 hull is 710.2 cm.

## Effects

| Effect | Reference (behaviour, not copy) | Must |
|---|---|---|
| Tank muzzle smoke | World in Conflict tank fire | Dense blast cloud along the bore and sideways, then a larger, thinner cloud that drifts and fades over a few seconds; soft, textured, sun-lit; on every shot (#303) |
| Muzzle flash | (producer to add) | Reads as a real firing, short, no lingering sprite (#294) |
| Exhaust, dust | (producer to add) | Follows motion and speed, by surface (#222, #224) |
| Lights | (producer to add) | Day/night policy, no bloom blowout at default zoom (#223) |

All effects: cosmetic only, nothing on a dedicated server, pooled and capped, scaled by quality and distance.

## Approved looks

Pictures the producer signed off, newest first. Reference images live in `docs/art/refs/`.

| Look | Capture | Signed off |
|---|---|---|
| M20 Guardian model | `Art/USA/vehicles/m20_guardian/_v4/renders/m20_rts_default.png` | (producer to confirm) |

## Never

- Flat billboards that read as cards from the side; hard sprite edges; looping patterns visible at default zoom.
- Effects that pop in or out at a distance boundary in the default view.
- Pure black or pure white areas in a lit scene.
