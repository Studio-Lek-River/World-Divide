# CommanderBar and MapButtons — USA / Common, revision 3

## 1. Purpose
Identify the commander and show only earned general powers only. Let the visible plate follow the list instead of reserving empty slots.

## 2. Anatomy
`CommanderBar` is a transparent Fill root in the HUD's 580×136 reservation. `CommanderPlate` is a TopLeft Panel with CommanderBar sprite, padding [8,8,8,8], no fixed size and no Fill anchor. Its intrinsic `CommanderContents` HBox has spacing 8: `CommanderIdentity`, then `Powers`. This uses handbook size=content, not count bindings or width expressions.

`CommanderIdentity` is 184×80. `PortraitFrame`/`Portrait` remain 80×80; `CommanderName` is 96×64 at 88,8, Title 16, up to three lines with ellipsis. `Powers` is an intrinsic HBox with spacing 1, repeat=commander.powers, **no count or fixed size**. There is no visibleWhen on Powers: commander.powers is a list used only by repeat, not a scalar condition. A zero-length repeat creates no cells. Each actual item gets one 30×64 UsePower button: `PowerArt` 22×22, `PowerLock` 20×20, `Charge` 3 high, `LevelPips` with the existing equality banks. Pips are 3×3 with 1 gap, at most 19 wide. Superweapons never enter commander.powers or this bar. The mockup shows three earned general powers.

Width is **208 at zero**, otherwise 16+184+8+30N+(N−1) = **207+31N**: 238 at 1, 300 at 3, 362 at 5, **579 at 12**, within the 580-unit slot. At zero the identity, outer padding and the existing 8-unit container gap remain; no power slot is reserved. Height stays 96. Only 8 end padding follows the last item; unused reservation neither paints nor catches clicks. The separate `LowPowerWarning` sibling at 0,104, 220×32, visibleWhen=player.power.low does not size the commander plate. Notifications starts at screen y160.

`MapButtons` stays 280×48: three equal buttons, spacing 4, each 90⅔×48 with padding [8,4,8,4]. Each icon is 16×16 at top; Body 14 label occupies the bottom 18-high box, centred, single line/ellipsis. Inner width is 74⅔. The 10×10 disabled lock stays upper right. All four MapButton PNGs stay 384×96 with original margins; Disabled hatching is confined to the upper 16 source pixels, clear of the label. Actions/enabled bindings are unchanged.

## 3. States
| State / reason | Presentation |
|---|---|
| Ready | existing selected double frame/full charge, action enabled |
| Charging | disabled hatch, actual art, progress/pips, no lock |
| Locked | disabled hatch and lock; hide art/progress/pips |
| Hover / pressed | lighter bevel / inset line, same calm centre |
| Empty list | portrait/name only; no placeholders |
| 1…12 items | exact list, natural growth to 579 |
| Superweapons | excluded; one aggregate strip button is specified in ResourceBar.md |
| Low power | separate conditional warning, no invented disable rule |
| Map disabled | upper-corner hatch/lock, calm readable label, no action |

Locked must imply ready=false. The readiness frame is not a targeting-selection claim. No additional disabled reasons exist in bindings.md.

## 4. Interaction
Left click sends UsePower for the earned general-power item. Right click is consumed with no secondary action. Existing tooltips provide item/action metadata. Unchanged proposals: tooltip 350 ms, hover fade 100 ms, press 0 ms, no pulses or new hotkey text. Warning is informational/hitTest=false; transparent reservation is click-through. No unsupported animation fields.

## 5. Edge cases
At 0/1/5/12 entries there is no reserved capacity. Twelve fits without wrap; 30-wide mouse targets are the tradeoff for keeping the 80-pixel portrait inside 580. Names use three lines/ellipsis; item names remain in tooltips. Do not silently truncate >12: agree overflow in code. Pip banks still cover levelMax 1–5 and level 0…maximum; levelMax=0 paints none. Longer map translations use ellipsis/tooltip, never overflow. Test actual fonts and 30% longer strings. Corner anchors and 16:10–21:9 scaling are unchanged.

## 6. Accessibility
Name/labels #E6E9EE, map icons #75B9DF, warning #F1BC65. Hatch/lock, progress and double frame distinguish disabled/charging/ready without hue. Pips use luminance. No font smaller than Body 14. See Validation.md and the four-state map-button review; existing fonts/licences remain dependencies.

## 7. Requests for code
Keep commander.powers restricted to earned general powers. No item.kind discriminator is requested or used; superweapon aggregation belongs to the resource strip. Confirm player.power.low is visible in CommanderBar's context: it already exists in bindings.md but was listed under ResourceBar; its use here is explicitly requested. Confirm intrinsic desired-size propagation through Panel→HBox→repeat ; no list-valued visibility conditions. Agree >12 overflow and retain the prior request for a pip list/widget if levelMax>5. Existing tooltips/localization remain required.

## Assembled layout (producer's changes after revision 3)

The layout departs from the sections above where the producer asked after playing:
- No name text. The general is one button (PowerSlot frame, 80x92): the portrait with the five rank stars under it. A click opens the general powers (`OpenGeneralPowers`); the Powers map button is gone.
- Ability slots are 48x48 squares: the power icon in the middle, the charge as a thin bar inside the bottom edge while charging, the level pips inside the top edge.
- Locked powers are hidden (`!item.locked`), so the bar grows with each earned power; with none only the general shows.
