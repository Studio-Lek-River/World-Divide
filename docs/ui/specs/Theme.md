# Theme — USA core HUD kit

## 1. Purpose
Establish reusable materials, typography and states for later USA panels. Preserve the accepted HUD top strip while giving every lower panel the same command-console treatment.

## 2. Anatomy
USA art rules (eight lines):
1. Use opaque dark gunmetal and deep navy, with calm flat centres.
2. Keep silhouettes angular: shallow chamfers, crisp thin bevels, no rounded cards.
3. Confine cool-blue faction light to edges and selected frames.
4. Put fixed corner seams, rivets and bevel ends inside the slice margins.
5. Use tiny chevrons/brackets sparingly; never bake letters or numbers into art.
6. Use fine diagonal hatching only for disabled/warning treatment, outside icon centres.
7. No glass, neon, holograms, flags, eagles or other insignia.
8. Faction accent is not team colour; team markers stay white/alpha for engine tinting.

**Sprites:** Panel (256², margin 24 each) is the main backplate. PanelInset (256², margin 24) reverses the bevel for recessed grids/portraits. Button (128², margin 20) supplies all five states. ProgressBar/ProgressFill (256×24, margin 6) make a trough and white/alpha tintable fill. Divider (256×8, margins 8/2/8/2) is white/alpha tintable. Names resolve as USA.<name>; manifests contain only this kit.

**Fonts:** Title = Barlow SemiBold 18; Body = Barlow Regular 14; Number = JetBrains Mono Medium 16. Sizes are em pixels at 1080p. Use one-line boxes at least 24/19/21 high respectively (round Number to 22). Number has tabular digits by default: all ten advances measured equal; no feature flag needed. Each unmodified TTF has its full matching *.LICENSE.txt, SIL OFL 1.1.

Downloads: [Barlow Regular](https://raw.githubusercontent.com/google/fonts/main/ofl/barlow/Barlow-Regular.ttf), [Barlow SemiBold](https://raw.githubusercontent.com/google/fonts/main/ofl/barlow/Barlow-SemiBold.ttf), [Barlow licence](https://github.com/google/fonts/blob/main/ofl/barlow/OFL.txt); [JetBrains Mono Medium](https://raw.githubusercontent.com/JetBrains/JetBrainsMono/master/fonts/ttf/JetBrainsMono-Medium.ttf), [JetBrains licence](https://github.com/JetBrains/JetBrainsMono/blob/master/OFL.txt).

## 3. States
| State | Cue that survives greyscale |
|---|---|
| Normal | single thin bevel; quiet icon field |
| Hover | brighter edge plus corner brackets; no centre glow |
| Pressed | inset top/left shadow and bottom highlight; no movement |
| Selected | second inset frame and corner marks; faction-blue edge |
| Disabled: prerequisite / funds / cooldown | same desaturated hatch border; show reason in existing tooltip; do not dim text below Disabled colour |
| Empty | omit icon, keep calm inset; no new decoration |
| Low power / attack | existing alert icon plus wording; Warning/Danger colour supplements shape |

Icons and text are separate overlays. Disabled icons may desaturate; keep labels at full opacity. Button centres carry no hatching across text or icons.

## 4. Interaction
Mouse hover/click; right click has no new kit action. Proposed existing-widget timings: hover 100 ms, press immediate, tooltip dwell 350 ms, no pulses. Show only engine-supplied hotkeys. Tooltip is informational; actual controls intercept clicks. Claude assembles placement in existing layouts; this delivery introduces no binding key or action.

## 5. Edge cases / mockup
Reference is 1920×1080. Keep the supplied top region/resource strip; bottom panels remain 460 / flexible centre / 300 wide and 196 high. At 16:10–21:9 expand only the middle, scale by height. Keep text ≥14 and allow 30% longer strings with wrap/ellipsis, not shrinking. Use empty/one/many engine lists without changing the kit. Nine-slice edges must stretch without scaling corner details.

Mockup: five selected units; six command buttons, Armour plating hovered, Field repair disabled, Hold position selected. Production header contains five queue entries, current 42%, 0:12. Attack notification is 360×56 at 16,160; tooltip 460×240 at 1444,588. All new icons are flat white silhouettes, per the producer's correction. Their contents are illustrative, not game-data edits. Tooltip/notification are transient; persistent frame budget is unchanged. No UI/Layouts files ship under the updated handbook.

## 6. Accessibility
Panel is opaque #101820, so battlefield colour cannot change text contrast. All permitted text colours meet 4.5:1 even at Body 14:

| Text colour | Contrast on Panel |
|---|---|
| Text #E6E9EE | 14.70:1 |
| MutedText #B5C0CE | 9.71:1 |
| Disabled #8493A5 | 5.71:1 |
| Accent #75B9DF | 8.32:1 |
| Warning #F1BC65 | 10.33:1 |
| Danger #F28A7E | 7.42:1 |
| Positive #95CCA5 | 9.76:1 |

Primary #385A73 is edge-only and Secondary #0A1119 is surface-only: neither is a text colour. All seven text colours also pass against the brightest actual panel/button centre (Validation.md). State geometry, hatch, progress, icon and words supplement hue. Verify localized glyph coverage against the shipped files; use an approved OFL fallback for uncovered scripts.

## 7. Requests for code

