# Claude's review of Round 3 — notes for Round 4 (gpt-6-astra: realism and technical check)

Round 3 fixed the domes by day: 08, 12 and 13 now read as coloured glass with their interior visible. 07 and 09 are
close to done. Round 4 is an **independent check and fix pass**, as `evidence/astra_check.md` was for #392: audit
every piece against its reference and the brief's contract, fix what you find, and keep what already works.

## Known items to fix

1. **Night domes are washed out.**
   - 08 at night is a flat pale peach/salmon dome with no visible core.
   - 13 at night is orange rather than red, with the LEDs as thin white dotted lines and no glow around them.
   - Required: saturated amber (08) or signal red (12/13) across the whole dome, with a hot, compact core (filament
     with mirror for 08, the LED cluster for 12/13) that blooms smoothly into the coloured glass. Near-white only at
     the very core. Compare `ref_match_night` against the producer images side by side.
   - Check that LowPower (30 %) still reads as the right colour.
2. **12/13 day:** the LED column is good. The glass may gain a little depth: slightly deeper red toward the
   silhouette and the base band, and the ribbed skirt band a touch stronger.
3. **08 collar:** still about as wide as the dome. The reference collar is clearly wider (about 1.15×). Widen it
   without changing the dome.
4. **09 night review:** the night comparison renders the steel housing almost black, while the reference keeps it
   readable as worn steel at night. Adjust the review lighting (not the maps) so the housing reads, and confirm the
   maps are still light worn steel.

## Your independent audit (record it in each piece's `evidence/astra_check.md`)

- Fidelity to the reference: silhouette, proportions, construction, finish, and the glass at day, night and
  LowPower.
- Realism at 12 m close-up, and the read at 150 m and 450 m. LOD0/LOD1 show no visible change at 260 m (07, 08,
  12, 13).
- The contract: Housing then Lens; only optical islands on Lens; budgets (07/12/13 M 1200/400, 08 L 2500/800, 09 S
  250); UV0/UV1 without overlaps; one DirectX Finish_Normal per slot on UV1; texture sizes; 08's dome UV running
  from +X around; 13's lens split at U 0.5; 12's FBX only under `candidate/`.
- Handoffs complete enough for Claude to integrate without guessing: emission strength and tint per piece, light
  origin and direction, LightProfile use, and the 08 sweep and 13 alternation layout.

## Deliverables

Archive Round 3 in `archive/round03/`. Rebuild all five twice through `run_pieces.py` with matching digests, and
refresh the reviews, evidence and handoffs. Write `reference_left_round04_right_{day,night}.png` and the
astra_12m/150m/260m/450m LOD sheets, as #392 did. Append your Round 4 section to `ROUND_NOTES.md`. Do not claim
producer approval.
