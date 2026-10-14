# Theme delivery validation — 2026-10-14

- PASS: USA theme has exactly all ten required colour roles, including Positive absent from the placeholder.
- PASS: All three required font roles are present.
- PASS: Body font is 14 em pixels at 1080p; Title 18 and Number 16.
- PASS: Title: valid static TTF, adjacent full SIL OFL 1.1 licence; Barlow SemiBold.
- PASS: Body: valid static TTF, adjacent full SIL OFL 1.1 licence; Barlow Regular.
- PASS: Number: valid static TTF, adjacent full SIL OFL 1.1 licence; JetBrains Mono Medium.
- PASS: Number digits 0–9 have identical 600-unit advances at 1000 em pixels; default tabular spacing, no OpenType feature dependency.
- PASS: Manifest contains exactly the six requested USA kit sprite names.
- PASS: USA.Panel: declared size matches brief, valid source-pixel 9-slice margins [24, 24, 24, 24].
- PASS: USA.PanelInset: declared size matches brief, valid source-pixel 9-slice margins [24, 24, 24, 24].
- PASS: USA.Button: declared size matches brief, valid source-pixel 9-slice margins [20, 20, 20, 20].
- PASS: USA.ProgressBar: declared size matches brief, valid source-pixel 9-slice margins [6, 6, 6, 6].
- PASS: USA.ProgressFill: declared size matches brief, valid source-pixel 9-slice margins [6, 6, 6, 6].
- PASS: USA.Divider: declared size matches brief, valid source-pixel 9-slice margins [8, 2, 8, 2].
- PASS: All ten PNGs match manifest sizes, RGBA/sRGB and filenames; no orphan or missing texture. Tint fill/divider RGB is pure white wherever alpha >0.
- PASS: All five button states are explicitly supplied.
- PASS: Text: AA contrast 14.70:1 on theme Panel and at least 14.45:1 across actual kit centres.
- PASS: MutedText: AA contrast 9.71:1 on theme Panel and at least 9.54:1 across actual kit centres.
- PASS: Disabled: AA contrast 5.71:1 on theme Panel and at least 5.61:1 across actual kit centres.
- PASS: Accent: AA contrast 8.32:1 on theme Panel and at least 8.17:1 across actual kit centres.
- PASS: Warning: AA contrast 10.33:1 on theme Panel and at least 10.15:1 across actual kit centres.
- PASS: Danger: AA contrast 7.42:1 on theme Panel and at least 7.29:1 across actual kit centres.
- PASS: Positive: AA contrast 9.76:1 on theme Panel and at least 9.59:1 across actual kit centres.
- PASS: Normal/hover/pressed/selected centres are identical and calm; state cues live in the slice borders.
- PASS: Five button states remain pixel-distinct in greyscale; native/64px review inspected.
- PASS: No UI/Layouts included, as explicitly required by the updated handbook.
- PASS: Only USA theme is replaced; other faction and neutral themes remain dependencies.
- PASS: Mockup is opaque 1920×1080, rendered with the supplied fonts in the new bottom kit.
- PASS: Previous resource/top strip is preserved pixel-for-pixel.
- PASS: Commander/topmost battlefield/minimap pixels remain unchanged from supplied prior HUD mockup.
- PASS: Minimap and map-button look preserved exactly.
- PASS: All nine mockup-only unit/building/upgrade/utility silhouettes are flat white with alpha; no illustration source is used.

## Font verification

```json
{
  "fonts": {
    "Title": {
      "file": "Fonts/Barlow-SemiBold.ttf",
      "size": 18,
      "family": [
        "Barlow",
        "SemiBold"
      ],
      "sha256": "86577cb32f8abe3673db53ca0f4221e6856751a4f6730c867e00f720f8bb1fc5"
    },
    "Body": {
      "file": "Fonts/Barlow-Regular.ttf",
      "size": 14,
      "family": [
        "Barlow",
        "Regular"
      ],
      "sha256": "95aa02c7c43096e0dd44d787ba6216864a67157e402adab59b35572e0c1577ea"
    },
    "Number": {
      "file": "Fonts/JetBrainsMono-Medium.ttf",
      "size": 16,
      "family": [
        "JetBrains Mono",
        "Medium"
      ],
      "sha256": "d16e6dc99672734698d629705f617c79f6eb6040f5113efe3a145204dc988109"
    }
  },
  "number_digit_advances_at_1000px": {
    "0": 600.0,
    "1": 600.0,
    "2": 600.0,
    "3": 600.0,
    "4": 600.0,
    "5": 600.0,
    "6": 600.0,
    "7": 600.0,
    "8": 600.0,
    "9": 600.0
  }
}
```

## Limits

This is offline asset/format/font/contrast validation and visual inspection, not a fresh Unreal runtime run. Layout assembly is the programmer's responsibility under the updated handbook. Runtime icon assignments, game data, localization/fallbacks, input and tooltip behaviour are not modified. The producer clarified there are no style-reference icons; mockup icons are original flat silhouettes. No generated illustrations are used. Existing top imagery/text remain rasterized from the supplied HUD mockup; all new bottom/tooltip/notification text uses the packaged fonts. Neutral/China/Insurgents themes are intentionally not replaced. No Common sprite entries are added because no new shared runtime pieces were requested.

**Result: PASS — checklist completed.**
