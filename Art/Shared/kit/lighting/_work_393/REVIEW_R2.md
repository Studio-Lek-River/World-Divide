# Claude's review of Round 2 — notes for Round 3 (last Sol round)

Good progress: 07's tray and latches, 09's worn steel and chamfer, 12's collar with lugs, 13's bar and 08's housing
are on the right track. **The domes went the wrong way**, and they are the deciding element of 08, 12 and 13.
Round 3 target: **domes that read like the references, day and night**. Everything else gets polish only.

## Domes (08, 12, 13) — the main fix

What is wrong now:
- **08 day:** the dome is a dark brown/black opaque shell with an amber "window" cut out, through which you see a
  bulb. It reads as a decal on a dark cup.
- **08 night:** a white rectangle inside a black dome.
- **12/13:** dark maroon-brown domes, with the LED column as a wide square-dot grid like a display panel.

What the references show, and what is required:
- The **whole dome** is one piece of evenly coloured translucent glass: bright, saturated amber (08) or signal red
  (12/13) over its entire surface by day. The colour is lightest in the middle of the body and slightly deeper at
  the silhouette edges, never dark brown or black. No window, no frame, no hard-edged region.
- The interior is seen **through all of the glass, tinted by it**, with soft edges: no sharp cutout. For 08: the
  bulb with its filament, holder and the curved mirror disc. For 12/13: a **compact, narrow cylindrical LED
  cluster** (about 1/5 of the dome width) on a round pedestal; not a wide grid.
- At night the whole dome glows in its own colour: a hot near-white/yellow core at the bulb or LED, falling
  off smoothly into saturated amber/red that fills the entire dome to its rim. **No white light** outside the core;
  08's glow is amber, 12/13's is red.
- 12/13: add the **ribbed skirt band** at the bottom of the dome (horizontal rings visible in the reference), plus
  the vertical Fresnel/reflection lines.
- Check the dome in the 150 m and 450 m shots. By day it must read as a red/amber cap; at night as a coloured point
  with a hot centre, not white.

Practical: drive BaseColor, LightProfile and emission from one consistent painted "seen-through-glass" image per
dome
UV, as one continuous field around the dome, with the interior image repeated or faded around the circumference so
no side shows a frame. `M_Kit_Lens` stays opaque; describe in the handoff how you expect the tint and light profile
to be used.

## 08 housing

The collar must be visibly **wider than the dome** (about 1.15×), as in the reference. The upper band is thinner,
with its two screws. The lower band is taller and heavier. The edge wear is a bit uniform: add stronger silver
chipping on the band edges and the base plate rim.

## 07

Fine. Polish only:
- At night, give the panel a centre-weighted profile with slightly darker edges, not flat white.
- The tray walls should catch the panel's light (pale enamel), not stay black.

## 09

Construction and steel are right. Polish only:
- Stronger wear contrast: darker pits, brighter edge scratches.
- The ribbed diffuser should have a hot white core with saturated tinted edges when lit, as the reference shows.

## 12 / 13

- Domes as above.
- 13: show the stems with their **gland nuts** under the bar, as in the reference.
- Give the bar's large bolt heads more relief.
- The bar is a little clean: add the reference's weathered plate texture, with pits, chips and rust at the bolts.

## Deliverables

As before. Archive Round 2 in `archive/round02/`, refresh all review and evidence files and the
`reference_left_round03_right_*` sheets, and append Round 3 to `ROUND_NOTES.md`. This is the last Sol round, so make
the handoffs complete for the Astra check.
