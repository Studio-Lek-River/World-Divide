# HUD validation — revision 3 — 2026-10-14

- PASS: CommanderBar: valid JSON, unique names, known scalar keys/actions/sprites; list bindings only in repeat.
- PASS: HUD: valid JSON, unique names, known scalar keys/actions/sprites; list bindings only in repeat.
- PASS: MapButtons: valid JSON, unique names, known scalar keys/actions/sprites; list bindings only in repeat.
- PASS: ResourceBar: valid JSON, unique names, known scalar keys/actions/sprites; list bindings only in repeat.
- PASS: All ten HUD slots and their names preserved.
- PASS: Both original modals preserved.
- PASS: No proposed superweapon key or LaunchSuperweapon action occurs in any layout.
- PASS: ResourceBar contains no superweapon Button or other Button; only an inert reserve.
- PASS: 64×32 superweapon reserve is blank, non-interactive and unbound.
- PASS: SuperweaponButton: exact 128×96 art for all four required states.
- PASS: Common.Icon.SuperweaponLaunch: 64×64 and tint=true.
- PASS: 30 runtime PNGs / 20 sprite entries: exact declared dimensions, sRGB, valid 9-slice margins, white/alpha tints and transparent frame centres.
- PASS: Every runtime texture is manifested; no missing or orphan texture.
- PASS: Changed original files are limited to requested commander/resource layouts, two manifests, two specs, mockup and delivery metadata.
- PASS: All 25 previously delivered runtime textures are byte-for-byte unchanged.
- PASS: HUD layout, low-power reservation, map-button layout and HUD spec preserved byte-for-byte.
- PASS: All previous sprite entries preserved; new manifests only add SuperweaponButton and Icon.Superweapon.
- PASS: Commander layout change is exactly removal of Powers.visibleWhen; no unrelated structural change.
- PASS: Mockup commander data contains exactly three earned general powers and no superweapons.
- PASS: 0 general powers: intrinsic plate 208×96; no list-valued condition and no reserved cells.
- PASS: 1 general powers: intrinsic plate 238×96; no list-valued condition and no reserved cells.
- PASS: 3 general powers: intrinsic plate 300×96; no list-valued condition and no reserved cells.
- PASS: 12 general powers: intrinsic plate 579×96; no list-valued condition and no reserved cells.
- PASS: All four superweapon states have an identical calm centre; disabled hatching and press edges stay out of icon/count fields.
- PASS: Resource width 1728: equal 534.667-wide plates plus 64 reserve, 12 margins/gaps; power row fits.
- PASS: Resource width 1920: equal 598.667-wide plates plus 64 reserve, 12 margins/gaps; power row fits.
- PASS: Resource width 2520: equal 798.667-wide plates plus 64 reserve, 12 margins/gaps; power row fits.
- PASS: Resource width 2580: equal 818.667-wide plates plus 64 reserve, 12 margins/gaps; power row fits.
- PASS: Badge x0/x2/x99/99+ fits 30-wide Number 16 preview field.
- PASS: Maximum credits and comma/dot variants still fit their 120-wide field.
- PASS: Resource labels/values retain size 16, equal 22-high boxes and 4-unit vertical plate insets.
- PASS: All four unchanged map-button states fit the original 280×48 allocation.
- PASS: Mockup is opaque 1920×1080; three general powers, low-power warning and one x2 superweapon button.
- PASS: Static text colours meet 4.5:1 against brightest delivered backplate centre, including new superweapon states.

## Text contrast

Brightest actual backplate centre pixel, including all new button states; decorative edges excluded.

| Colour | Worst contrast |
|---|---|
| Text #E6E9EE | 13.17:1 |
| MutedText #B5C0CE | 8.70:1 |
| Warning #F1BC65 | 9.25:1 |

## Limits and pending code

Offline checks and Python preview, not a new Unreal run. Binding validation now distinguishes list names from scalar keys; list names are accepted only in repeat. The existing player.power.low use in CommanderBar is retained from the explicit prior revision; its availability there remains a documented integration dependency. Proposed superweapon.owned/ready/charge/canLaunch/readyText and LaunchSuperweapon appear only in the spec, never in layout JSON. SuperweaponReserve is inert and blank in the actual layout; the mockup deliberately illustrates the future wired button (x2, next charge 65%). No superweapon entries appear in CommanderBar mock data or scope. Existing fonts, scalar colour values, desired-size handling, widgets and tooltip support remain runtime dependencies. At zero items the unchanged parent HBox gap gives a 208-wide identity plate; no empty power cell is drawn. Persistent coverage remains bounded by the same previous maximum frame and strip; transient warning coverage is unchanged.

**Result: PASS — static checklist and revision checks.**
