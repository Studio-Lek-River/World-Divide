# Claude's review of Round 1 — notes for Round 2

The toolchain, contract and evidence are in good shape. The appearance is not yet at the approved #392 level: the
pieces read as clean CG primitives next to the producer images. Round 2 target: **match each reference's construction
and finish, and make the glass read as glass.**

## Budget change (Claude, applies now)

`catalogue_393.json` now puts **12 and 13 in class M: ≤1,200 LOD0 / ≤400 LOD1**, so both return a real LOD1 again.
250 triangles gave 8–12-sided domes that read as polygons in close-up, and 12 sits on the HQ's mast tips at 1.75×
scale. Use the budget for round silhouettes (24–32 segments on domes, collars and flanges at LOD0; 16 at LOD1 with
no visible change at 260 m), not for microscopic parts. 09 stays class S.

## 13 dimensions (decided)

Keep the shared head design, scaled to **0.75 × the 12 head** (collar ≈ 33 cm). Overall about 80 wide × 33 deep ×
45 tall, bar centred over the origin. That resolves the conflict; record it in the handoff.

## All three domes (08, 12, 13): glass, not plastic

`M_Kit_Lens` is opaque in Unreal, so the interior has to live in the lens maps. Right now the domes read as milky,
opaque plastic, with a blurry streak inside.
- Body colour: **saturated, dark-cored transparent glass**, bright where light passes through, dark at the silhouette
  edges with a coloured rim. Amber for 08, signal red for 12/13 (not maroon, not brown).
- Paint the interior clearly into BaseColor, LightProfile and normal: for 08, the bulb with its filament, the bulb
  holder/stem, and the curved mirror disc behind it. For 12/13, the vertical LED column on its round pedestal, with
  the small collar ring at the dome's base. It should read in the reference camera at day exposure.
- Vertical Fresnel ribs and a horizontal ribbed band near the bottom of the dome (both in the references). Fine
  scratches and scuffs on the glass, as in the 08 close-up.
- Night: no concentric banding (the 08 night render shows rings) and no full-height stripes. 12/13 currently show
  horizontal bars across the whole dome. The glow is a compact hot core (filament or LED column) falling off smoothly
  into coloured glass.
- 08's dome apex is pinched into a point: round it off cleanly (pole fan / normals).

## 08 WarningBeacon

- The housing must be **blackened, weathered cast iron** as in the reference: near-black with silver edge wear and
  pitting. It is currently mid-grey galvanised.
- Proportions from the reference:
  - The collar is wider than the dome and overhangs it.
  - The collar has a thin upper band with two visible screws and a taller lower band, split by a groove.
  - The stem is about a third of the total height and has a flange at the bottom.
  - The square base plate has four **hex bolts as geometry**; there are 760 triangles of headroom.

## 07 ServiceBayLight

It reads as a picture frame. Use the headroom (930/290):
- Set the panel **1.5–2 cm back** behind the trim, with visible inner walls: the white-enamel reflector tray from
  the spec.
- Make the trim a stepped steel profile with the 04 family's dark graphite finish and silver edge wear, two
  spring-latch clips on the long sides and the corner screws.
- Panel: a **prismatic** (small pyramid/diamond) pattern, not the 04 ribs. At night, show a faint LED dot grid behind
  it and a centre-weighted 4000 K profile. The current night panel is flat clipped white.

## 09 IndicatorLED

The construction is right, but the finish is wrong.
- The reference is **worn bare/galvanised steel**: light grey with dark pits, scratches and silver edges, not black
  powder coat.
- The big chamfer along the front-top long edge must be clearly visible.
- The diffuser sits in a **raised, screwed bezel plate** (two screws at the lens ends), with strong rib contrast. Lit,
  it has a hot white core and a saturated tinted edge.
- The slotted round plug on the end face and the two front Torx screws must read at the reference angle.

## 12 MastBeaconSingle

- Make the collar a round cast ring with **clamp lugs / bolt tabs** around it, as in the reference. Under it, a
  flared underside, then a stem with a cable-gland nut.
- Make it look like the reference (tip mount stays). The dome is taller than it is wide, with rounded shoulders.

## 13 MastBeaconDual

- Heads as 12, at 0.75 scale.
- The bar is a **bolted rectangular steel bar** with large bolt heads and plate texture, as in the reference.
- Under each head: a stem with a gland nut through the bar.

## Deliverables

Same set as Round 1. Refresh every `reference_left_round02_right_{day,night}.png`, and keep the Round 1 files in
`archive/round01/`. Append Round 2 to `ROUND_NOTES.md`.
