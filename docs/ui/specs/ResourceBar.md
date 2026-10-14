# ResourceBar — USA / Common, revision 3

## 1. Purpose
Show economy health in an orderly strip. Provide one future aggregate launch control for all owned superweapons; general powers remain in CommanderBar.

## 2. Anatomy
ResourceBar stays full width×40, padding [12,4,12,4]. ResourcePlates is a Fill HBox with 12 gaps: equal stretched Power, Credits, RankGroup plates, then **SuperweaponReserve**, an inert 64×32 Panel with no sprite, key or action. All four allocations are 32 high, with 4 above/below. Three plate widths are (logical width−124)/3: 534⅔ at 16:10, 598⅔ at 1920×1080, 798⅔ at 21:9. Horizontal outer margins and every gap remain 12. This round leaves the reserve blank in game; only the mockup draws the proposed button there. No superweapon Button element is in the JSON.

Existing plate contents are unchanged: centred 24-high HBoxes, spacing 12. PowerContents is 434 wide: icon cell 20, PowerLabel 78, bar cell 120, PowerText 180. PowerIcon/PowerLowIcon and PowerBar/PowerBarLow are exclusive; ratio uses player.power.ratio, icon/value use player.power.color. CreditsContents is 260: icon 20, label 96, value 120. RankContents is 256: icon 20, label 64, five-star row 148 (24 cells, 7 gaps). Text stays size 16 in 22-high boxes: Body/#B5C0CE labels, Number/#E6E9EE values except bound power colour. Icons/stars and 12-high power bar are centred; labels and values share the existing font family's baseline.

**Future superweapon control:** one 64×32 button, right 12/top 4 inside the strip (x1844,y848 at 1920×1080). It uses USA.SuperweaponButton, four 128×96 9-slice state textures, margins [16,16,16,16]. Common.Icon.SuperweaponLaunch is 64×64, white/transparent/tint=true, displayed 18×18 at local 6,4. Count badge uses Number 16, local 28,3, 30×20: e.g. **x2**, the number of launches ready across all owned buildings. Next-charge bar is local 6,26, 52×3, using existing PowerBar/PowerFill. Art has no baked text, count, icon or charge. All text remains calm and legible in every state.

## 3. States
| State | Presentation / behaviour |
|---|---|
| Normal / low power | intact / broken bolt, blue / warning ratio bar, bound power colour; no warning words in strip |
| Zero / maximum credits | 0 / 999,999 in fixed 120-wide field |
| Rank 0…5 | five silhouettes; bright earned, 40% unearned |
| No superweapon owned | future button hidden, no button hit area; blank reserve persists to keep plate geometry stable |
| Owned, 0 ready | Disabled, x0, next-charge bar; corner hatch plus zero count, no launch |
| Owned, ≥1 ready | Normal, xN, next-charge bar; one shared enabled button |
| Hover / pressed | lighter bevel / inset edge, calm count field; no layout shift |
| All owned ready | count still xN, charge bar full; tooltip says all ready |

Charge is the maximum normalized charge among not-ready owned weapons: the next launch to become ready, independent of the ready count. Return 1 if all owned weapons are ready, 0 if none owned. Thus x2 plus 65% means two launches can fire now and the next is 65% charged. Identical buildings do not create extra buttons. Ownership loss recomputes both count and charge. Low power does not fabricate a disable rule; readiness comes from gameplay. LowPowerWarning stays below the commander, visible only for player.power.low, informational and hidden at normal power (HUD.md).

## 4. Interaction
Current JSON has no strip actions. Once wired, left click on the aggregate button dispatches LaunchSuperweapon for **one** ready launch. Gameplay chooses one ready source deterministically, validates it and atomically decrements the ready pool only for an accepted launch; repeated clicks cannot spend the same launch twice. Zero/hidden controls dispatch nothing; right click has no launch action. Future tooltip after 350 ms: ready count, next-charge percent/all-ready message. Hover 100 ms, press immediate, no pulses or new hotkeys. No new timing or tooltip fields are added now.

## 5. Edge cases
Power row fits the smallest plate with 50⅓ on each side. Credits 999,999/999.999 fit 120 at Number 16. Five rank items are required; labels retain room for 30% growth. At 16:10–21:9 the reserve stays 64×32, plate widths share remaining space. Badge shows x0…x99; larger totals use localized 99+, with the exact count in the future tooltip. This display cap never limits gameplay. The mockup has three earned general powers, low power, and **one** strip superweapon button showing x2 and next charge 65%.

## 6. Accessibility
Existing text colours and font roles are retained; Body/Number baseline and player.power.color need runtime checking. Superweapon count is #E6E9EE on gunmetal; next-charge trough is visible. Disabled has hatch and x0; ready has xN; states work without hue. White icon remains distinct at 18×18. No flashing, font smaller than 14 or new font files. See Validation.md for static contrast; existing licensing is unchanged.

## 7. Requests for code
Supply **superweapon.owned** (boolean), **superweapon.ready** (nonnegative integer launch count), **superweapon.charge** (finite 0–1 ratio with semantics above), **superweapon.canLaunch** (boolean owned && ready>0), **superweapon.readyText** (localized xN / 99+ badge), and action **LaunchSuperweapon** (one launch, authoritative validation/selection/count update). Supply tooltip aggregation/localization for exact count and next charge. These are proposals only: **none is referenced in any delivered layout**. Wire the future control into SuperweaponReserve after the binding contract exists. Retain prior checks for five rank items, counters, power colours, font baselines and player.power.low availability in CommanderBar.

## Assembled layout (producer's changes after revision 3)

The layout departs from the sections above where the producer asked after playing:
- No low-power banner. Low power shows in the strip only: the PowerLow icon, the Warning-coloured bar and the value.
- Compact plates from the left: Power (306 wide) and Credits (222 wide), each sized for its longest value. The rank moved to the commander bar.
- The superweapon reserve stays at the strip's right end.
