# Astra independent check — 08_WarningBeacon, Round 4b

2026-10-23, issue #393. 08 only; producer review pending. No git or Unreal. The interrupted attempt had left no edits: all 80 Round 4 artifact hashes and its generator hashes matched before work. That delivery is preserved in ../archive/round04/ and its copied artifacts were hash-verified. The producer-approved 09/12/13 and protected 07 were not touched; protected_round04b.json checks 1,241 files including their archives and the pre-existing shared helpers.

## Reference and construction

Compared the producer image with the actual day, night and LowPower renders. The reference is red-orange; the brief requires amber. Retained external stem, flange, base, bolts, overall envelope and two cast bands. The cast seating shoulder is now 16.4 cm versus a 14 cm dome body and 14.2 cm lower lip: 1.155× widest glass. The shoulder's top overhang grows from 15.8 to 16.4 cm; the outer bands were normalized to that target rather than growing the overall fixture.

The former mapped interior was rejected for floating/angle inconsistency. Both LODs now contain real Housing holder/socket and rear bracket, plus Lens bulb and concave mirror. Holder and bracket begin at z=.178 m and penetrate the .180 m top plate. Bulb bottom .231 overlaps socket top .232 by 1 mm. Bracket reaches .238 and intersects the mirror at (-.027875,0,.235). Connected-component measurements and the intersection witness are in construction_round04b.json. Front, three-quarter, side and rear views confirm coherent occlusion and a grounded assembly. The rear view hides the bulb behind the real mirror; it does not repeat a painted front view.

Dome surface: thin amber transmission with darker grazing edges, bright reflected strips, vertical moulded ribs, sparse scratches/scuffs in the baked finish normal/roughness, and a real flared bottom lip. Day preserves transparent depth; night has a compact near-white/yellow filament, a visible curved golden reflector and amber scatter through to the rim. LowPower uses the same geometry and shading at LampLevel .3 and retains the filament/reflector separation. The glass is a thin-wall approximation, not a solid-volume refraction simulation. The lower-budget mesh and subtle wear remain simpler than the detailed producer photograph; no photographic-equivalence claim.

Review: [day](../review/reference_left_round04b_right_day.png), [night](../review/reference_left_round04b_right_night.png), [LowPower](../review/reference_left_round04b_right_lowpower.png), [attachment angles](../review/construction_angles_day.png). The close views use modest review-only bloom and vertical reflection cards. Distance evidence has no compositor bloom.

## Technical checks

- Final scoped runner executed twice: 2312/2500 LOD0 and 784/800 LOD1, headroom 188/16. Matching digests in rebuild_determinism.json. All other manifest entries unchanged.
- Independent factory-reset builds match each other and the clean source. Independent FBX reimport confirms counts, bounds, Housing then Lens, UV channels, unit normals/tangents and no degenerate geometry/UVs.
- Exactly two clean source meshes, seven packed images byte-matching the external maps. Housing 2048²; Lens 1024²; LightProfile RGB 16-bit linear. Only BaseColor is sRGB.
- UV0 and UV1 are non-overlapping, including combined LOD atlases in delivery.json. One DirectX Finish_Normal per slot on FinishBake with UV0 tangents. Every final-normal face-centre coverage sample passed. Maximum lens density 1954.45 px/m (limit 2048); housing median approximately 512 px/m.
- Lens chart kinds are only amberglass, beacon_bulb and beacon_mirror. Holder/bracket stay Housing. Two intentionally open components per LOD: shell and concave reflector sheet. No negative closed-component volumes.
- R-only LightProfile emission, G shell mask, B unused. Hot-core range .55–.90, strength 24; linear amber [1,.22,.001] and core [1,.92,.48]. No projected interior remains. Material recipe and handoff describe thin transmission and the necessary rigid internal material rotation.

## Distance / LOD limits

Native review is 1920×1080 at 60° horizontal FOV. Night 150 m has 12 amber-classified pixels with peak RGB approximately [.588,.373,.047]; 450 m has 4, peak [.204,.067,0]. Day-off is deliberately unlit and does not retain an amber signal at those subpixel distances. These are Blender results, not Unreal visibility guarantees.

The final stable 512-sample comparison at 260 m has 6 pixels differing by more than 1/255, maximum display-channel difference .05098. Projected convex-outline difference is .01530 pixel. At 150 m: 11 pixels, maximum .06275. There is no resolved silhouette jump in the native 260 m review, but the images are not pixel-identical; small intensity differences remain. LOD1's coarse interior is visibly simpler when deliberately enlarged at 12 m and is not intended for that range. See stable_distance_comparison.json, astra_distance_comparison.json and the Astra LOD sheets.

Unreal transparency/sorting, shared-phase rigid rotation, runtime bloom and producer acceptance remain for Claude/producer. The old opaque Lens integration must not be reused. This pass delivers the art and evidence without claiming engine or producer approval.
