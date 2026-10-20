# HUD — USA / Common frame

## 1. Purpose
Keep selection and commands available while leaving the battlefield dominant. This delivery establishes the frame and preserves engine-owned content slots.

## 2. Anatomy
All numbers are HUD units at 1920×1080. `HUD` is a transparent, non-blocking composition root.

| Element | Anchor / offset | Size / role |
|---|---|---|
| LeftColumn | top left 16,16 | vertical, 8 gap |
| CommanderBar | LeftColumn | 580×136 transparent reservation; content-sized plate up to 579×96 and conditional warning |
| Notifications | below commander reservation, y160 | 480×200 reserved, engine Notifications |
| RightColumn | top right 16,16 | vertical, 8 gap |
| MinimapFrame / Minimap | RightColumn | 280×280, 6 inset, engine Minimap |
| MapButtons | below map, y304 | 280×48, MapButtons layout |
| Scoreboard | top right 16,376 | 360×300 reserved, engine Scoreboard |
| Tooltip | bottom right 16,252 | 460×240 reserved, engine Tooltip |
| BottomFrame | bottom stretch | full width×236 |
| ResourceBar | top of BottomFrame | full width×40 |
| BottomPanels | remaining BottomFrame | full width×196 |
| SelectionFrame / Selection | left | 460×196, 8 inset, Selection layout |
| CommandsFrame / Commands | flexible centre | 1160×196 at reference, 8 inset, CommandPanel layout |
| UpgradesFrame / Upgrades | right | 300×196, 8 inset, UpgradePanel layout |

`MatchResult` and `InGameMenu` remain the only modals, supplied by the existing game. Queue stays inside Selection; no clock is added. Bottom panels still use 196 of the permitted 280 units. Resource strip plus panels occupy 21.85%; maximum commander plate, map and buttons add about 7.12%: **28.98% at 16:9**, **29.77% at 16:10**. Unused commander reservation paints nothing. Transient warning, notification, scoreboard and tooltip rectangles can temporarily raise coverage.

`LowPowerWarning` lives in CommanderBar.layout.json at local 0,104, screen 16,120. Its 220×32 ResourceSlot plate contains `WarningIcon` (Common.Icon.PowerLow) and `WarningText` (LOW POWER); the whole element has visibleWhen=player.power.low. It is left-aligned with the commander, 8 below its 96-high plate. With normal power it paints nothing and has no hit area. The transparent reservation extends to 136 so Notifications starts at y160, 8 below the warning; other alerts retain their 480×200 area. Avoid duplicating this warning in engine Notifications content.

## 3. States
| State | Behaviour |
|---|---|
| No selection / one / many | engine widgets own content; frame dimensions stay fixed |
| Normal | restrained gunmetal, thin blue bevel, no large glow |
| Hover / pressed / disabled / selected | delegated to child layouts and engine widgets |
| Low power | warning below commander has broken bolt and wording; strip retains ratio bar, broken bolt and power colour without warning words |
| Scoreboard / tooltip absent | slots paint nothing; reserved rectangles do not block |
| Modal | whole screen and HUD dimmed; all battlefield input intercepted |

## 4. Interaction
Sprite panels and buttons intercept clicks; LowPowerWarning explicitly uses hitTest=false. Empty root, column containers, slot wrappers and the unused commander reservation do not block clicks. Engine widgets intercept only visible content, except Minimap, which owns its full playable area. Left/right click in control backplates does not issue battlefield orders. Tooltip is non-blocking. No new hotkeys: Esc opens/closes the menu, and over MatchResult opens the menu above it. Proposed hover fade 100 ms, press 0 ms and tooltip dwell 350 ms remain engine-owned, not invented JSON properties.

Edge scroll (#339, camera rule for every HUD panel): within `EdgeScrollMargin` (12 px) of a screen edge the cursor scrolls the camera towards that edge, except over a HUD panel, where it belongs to the panel. The screen's outermost pixel row and column always scroll, even under a HUD bar, so an edge fully covered by a bar still scrolls. The panels are only looked up (`ARTSPlayerController::IsOnHUDPanel`) inside the margin, never while the cursor rests over the battlefield. Panels may therefore run flush to a screen edge; one that needs its outermost pixel for a control should leave it to the scroll.

## 5. Edge cases
Scale by height/1080 in the supplied aspect range. At 16:10 the 1080-high logical width is 1728 and Commands is 968 wide; at 21:9 width 2520, Commands is 1760. Corners stay 16 in; the centre expands; strip stretches continuously. At 2560×1440 scale is 1.333, at 3840×2160 it is 2.0. At 3440×1440 logical width is 2580. Tooltip stays 16 above the strip; scoreboard ends at y676, tooltip begins y588: engine must suppress or reposition tooltip while the scoreboard is shown. Long entity names, command density and 30% expansion remain the respective engine widgets' responsibility; do not squeeze text below 14. Mockup bottom content is illustrative placeholder content, not entity data added to layouts.

## 6. Accessibility
Layout labels use explicit text colours; no new themes or fonts ship. Text #E6E9EE, secondary #B5C0CE, warning #F1BC65 sit on near-black gunmetal. See validation report for measured worst-case contrast. Frame edges are decoration, not the only state cue. Disabled uses hatching; selected uses a second frame; warnings have a broken bolt and words. Existing theme Title/Body/Number fonts remain dependencies; Number must retain tabular figures and existing font licensing. Arial is used only to rasterize the review mockup.

## 7. Requests for code
Retain previous requests for the 196-high engine content, hidden hit geometry, Tooltip/Scoreboard collision and font/localization verification. Confirm the existing player.power.low key is available in CommanderBar's context: the producer explicitly requests it there, while original bindings.md lists it under ResourceBar. No new key name is introduced. Avoid a duplicate LOW POWER banner from the Notifications widget.
