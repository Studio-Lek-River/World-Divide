# Selection — USA / Common

## 1. Purpose
Identify the current selection, its condition and relevant capabilities. Keep a production building's queue inside the bottom-left selection panel, below its portrait and details.

## 2. Anatomy / placement
Local 1080p HUD units, 460×280 maximum. Reuse BottomPanel, PanelInset, Button, ProgressBar/ProgressFill. New sprites: PortraitFrame, UnitTile, SelectionEmpty; Common.CountBadge, Common.HealthBar, Common.HealthFill, Common.ConstructionOverlay, Common.StatusSlot, Common.AmmoPip, Common.Icon.LowPowerSlow.

| Element | Local rectangle x,y,w,h | Content |
|---|---|---|
| SelectionPortrait | 12,12,128,128 | selection.icon, PanelInset, then transparent PortraitFrame |
| TeamStripe | bottom 6 pixels of portrait | flat Panel bound to selection.teamColor; cover ONLY the white frame band, never tint the whole frame |
| SelectionName | 152,12,296,24 | selection.name, Title 18, one line/ellipsis |
| SelectionDescription | 152,40,296,20 | selection.description, Body 14, one line/ellipsis |
| HealthBar | 152,72,296,10 | selection.health, selection.healthColor, Common.HealthBar/HealthFill |
| HealthText | 152,88,296,22 | selection.healthText, Number 16, e.g. 3500 / 3500 |
| FutureStatus | 152,116,104,20 | four empty 20×20 StatusSlot frames, 6 gaps; no fake bindings or interactions |
| UpgradeIcons | x 152, natural row below details | repeat selection.upgrades: item.icon and item.name; 24-square icons/4 gaps |
| QueueSlot | 12,164,436,116 | ProductionQueue layout inside this panel; bottom row, not a side column |

PortraitFrame replaces its previous same-name entry with 320×320. Lower 12 source rows are white: 6 fixed HUD units at 2x. Overlay only that band with a colour-bound Panel, avoiding white fringes/whole-frame tint. Keep future slots reserved. Upgrade overflow wraps/scrolls in reclaimed space without overlapping queue or inventing counts.

## 3. States
The existing source confirms selection.mode has exactly None, Single, Multi.

| State | Presentation |
|---|---|
| None | SelectionEmpty background, faint ~8% star roundel; no stale portrait, numbers or queue |
| Single unit | portrait/name/description/health; no production queue unless its own source says visible |
| Single production building | common header plus QueueSlot below it; completed upgrades may appear in UpgradeIcons |
| Building with upgrades | repeat actual selection.upgrades, no fabricated research actions; queue remains independent |
| Construction site | selection.isConstructing: tile ConstructionOverlay over icon at its baked 40% opacity; label Under construction, ProgressBar bound to selection.constructionProgress at 152,164,296,10; CancelButton at 152,188,160,32 sends CancelConstruction, enabled by selection.canCancelConstruction |
| Aircraft | selection.isAircraft: AmmoText from selection.ammo / selection.ammoMax at 152,140,296,22; pips below; selection.isRearming adds REARMING wording, never a guessed countdown |
| Multi | generic Selected groups heading; 6×2 grid, origin 12,44, cells 68×84, gap 4; repeat selection.groups, up to 12 actual type groups |
| Group hover / selected | brighter UnitTile edge / double frame; selected is local activation feedback, not a new data binding |
| Disabled cancel | keep readable Disabled-colour label, no action; no unsupported reason binding |

Each group has 64-square UnitTile, item.icon, 32-square CountBadge with item.count (Number 14, 28-wide inner text), item.name below (Body 14/ellipsis), and 52×5 health using item.health/item.healthColor. Multiple units of one type are Multi with one tile; exactly one unit becomes Single. Two hundred units remain grouped, not 200 tiles. A badge fits 200; show exact counts. No list name is a visibleWhen condition.

## 4. Interaction
Left Group click sends SelectGroup: replaces selection with that type's living units, without camera movement. Right click is consumed. Tooltips show full item.name/count or upgrade name. CancelConstruction retains server validation/refunds. Proposed hover 100 ms/tooltip 350 ms; no new hotkeys/pulses. Pressed uses Normal fallback. Full name/description tooltips need the existing tooltip host, not an unsupported Text.tooltip property.

## 5. Edge cases
Hidden queue space reflows (ProductionQueue.md). Existing 196-high compact Single profile: portrait 96 at 12,8; details x 116, name 252×24/y 8, description 332×20/y 36, health/y 64, health text/y 76; three 20-square future slots at 380,8; upgrade icons 152,100; QueueSlot 12,116,436×80 with 48-square frames. Compact Multi: grid 12,28, 6×2 cells 68×76/gap 4; tile 56, badge 32, health 44×4, name below in 19-high box. This ends at 184, leaving 12 bottom. Do not enlarge the whole HUD automatically. At 16:10–21:9 keep width 460/height scaling. Body ≥14; 30% longer strings ellipsize with full tooltip. Ammo: one bullet per remaining round, empty StatusSlot per spent round, 12 per row/two rows; larger capacity uses exact numeric fallback. Zero shows 0 / 0, no pips.

## 6. Accessibility
Length, health numbers and monochrome 30%/60% ticks supplement Positive/Warning/Danger (source thresholds ≥60%, ≥30%, below 30%). No invented health-state string. Group bars also use ticks. CountBadge is Text on dark plate. Labels retain full-opacity AA theme colours; Validation.md. Empty status frames imply no bonuses.

## 7. Requests for code
Presentation helpers: scalar ammo→pips, non-button tooltips, stripe-only tint. Collapse hidden child QueueSlot/reflow without binding queue.visible in Selection scope. Agree scrolling beyond 12 groups. No invented key/action. Replace old 13-pixel labels during assembly; no layouts delivered.
