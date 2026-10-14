# Selection delivery validation — 2026-10-14

- PASS: Exactly the 11 requested USA/Common sprite entries, no existing kit entries duplicated.
- PASS: Common.CountBadge: exact requested size [64, 64], RGBA/sRGB, valid slice margins where declared.
- PASS: Common.HealthBar: exact requested size [128, 16], RGBA/sRGB, valid slice margins where declared.
- PASS: Common.HealthFill: exact requested size [128, 16], RGBA/sRGB, valid slice margins where declared.
- PASS: Common.ConstructionOverlay: exact requested size [256, 256], RGBA/sRGB, valid slice margins where declared.
- PASS: Common.StatusSlot: exact requested size [48, 48], RGBA/sRGB, valid slice margins where declared.
- PASS: Common.AmmoPip: exact requested size [16, 16], RGBA/sRGB, valid slice margins where declared.
- PASS: Common.Icon.LowPowerSlow: exact requested size [48, 48], RGBA/sRGB, valid slice margins where declared.
- PASS: USA.PortraitFrame: exact requested size [320, 320], RGBA/sRGB, valid slice margins where declared.
- PASS: USA.UnitTile: exact requested size [128, 128], RGBA/sRGB, valid slice margins where declared.
- PASS: USA.QueueSlot: exact requested size [128, 128], RGBA/sRGB, valid slice margins where declared.
- PASS: USA.SelectionEmpty: exact requested size [920, 560], RGBA/sRGB, valid slice margins where declared.
- PASS: All 16 runtime PNGs are present and manifested; no extra/missing texture.
- PASS: UnitTile supplies all and only three requested states.
- PASS: QueueSlot supplies all and only four requested states, with no unauthorized extra state filename.
- PASS: PortraitFrame has exactly 12 full-width source-pixel rows of opaque pure-white team band; centre remains transparent.
- PASS: Whole PortraitFrame is not marked tint=true: tint is confined to the white band by the presenter overlay.
- PASS: HealthFill, AmmoPip and LowPowerSlow are pure-white/alpha tintable assets.
- PASS: ConstructionOverlay has uniform 102/255 = 40% alpha baked in; no additional global 40% opacity should be applied.
- PASS: Hazard pattern is toroidally periodic in X/Y; 256-pixel repeats tile without a phase break.
- PASS: SelectionEmpty roundel centre measures about 8% white over the base; not 8% total panel opacity.
- PASS: SelectionEmpty is a direct 920×560 image, not 9-sliced.
- PASS: No layouts, themes or fonts are delivered; existing HUD kit/font roles are reused.
- PASS: ProductionQueue: every named data/list/item binding is in bindings.md; no invented item.empty/selected/progressText key.
- PASS: ProductionQueue: all seven required handbook sections present.
- PASS: Selection: every named data/list/item binding is in bindings.md; no invented item.empty/selected/progressText key.
- PASS: Selection: all seven required handbook sections present.
- PASS: Selection spec covers all three source-confirmed modes and required aircraft/construction/upgrades/group/200-unit states.
- PASS: Queue spec covers five-slot repeat, visibility/reflow, slowdown and actual last-of-kind cancellation semantics.
- PASS: 12-type regular grid fits 436-wide content and the 280-high panel.
- PASS: Compact 12-type grid fits the existing 196-high HUD without increasing the bottom band.
- PASS: Five regular queue slots fit the parent content area; first active plus three waiting leaves one empty.
- PASS: Compact five-slot queue fits the 196-high profile.
- PASS: Count 200 fits the 28-wide badge text area at Number 14; 189+11 one-unit types totals 200 in stress review.
- PASS: Number 14 mm:ss readout fits even compact active cell.
- PASS: Opaque 920×560 mockup contains factory, active 42%, three waiting, one empty and smaller three-type/count comparison.
- PASS: Mockup and native state/contact sheet visually inspected; flat silhouettes follow the producer-approved console style.
- PASS: Existing theme text colours used here pass AA against the brightest reused frame/content background pixel; count plate and time plate remain dark.

## Text contrast

Worst measured contrast against actual reused BottomPanel centre / CountBadge centre. Labels use full-opacity theme text; team and health fills are not text colours.

| Text | Worst contrast |
|---|---|
| Text #E6E9EE | 13.17:1 |
| MutedText #B5C0CE | 8.70:1 |
| Warning #F1BC65 | 9.25:1 |
| Disabled #8493A5 | 5.11:1 |

## Integration limits

Offline validation and visual review, not a new Unreal test. Existing kit, theme/fonts, entity icons and tooltip host remain dependencies. No layout, new key or new action ships. Engine presentation helpers are documented for numeric ammo→pips, band-only tint, full-text/empty-cell tooltips and hidden queue height propagation. Source confirms None/Single/Multi, health thresholds and last-queued-of-kind cancellation; UX does not falsely promise clicked-index cancellation. Exact-index behaviour is a request against the existing action. Percent42% is a design annotation derived from sample progress; existing timeLeft is the implementable live readout. PortraitFrame overrides the existing same-name sprite at320×320; inspect all consumers after merging. The regular280-high and compact196-high profiles are alternatives; do not grow the entire HUD without producer review.

**Result: PASS — checklist complete.**
