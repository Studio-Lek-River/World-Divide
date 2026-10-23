
## Round 1 — 2026-10-23 — gpt-6.1-sol

Issue #393. Built all five assigned pieces and delivered the first full art/export set for Claude's between-round review. **No producer approval or Unreal/gameplay acceptance is claimed.**

| Piece | LOD0 / LOD1 triangles | Headroom | Reference comparison | Open review item |
|---|---:|---:|---|---|
| 07_ServiceBayLight | 270 / 110 | 930 / 290 | [day](../07_ServiceBayLight/review/reference_left_round01_right_day.png), [night](../07_ServiceBayLight/review/reference_left_round01_right_night.png) | Spec-led recessed panel; LEFT is approved 04 family styling, not a silhouette reference. |
| 08_WarningBeacon | 1740 / 608 | 760 / 192 | [day](../08_WarningBeacon/review/reference_left_round01_right_day.png), [night](../08_WarningBeacon/review/reference_left_round01_right_night.png) | Judge the amber glass, mapped bulb/mirror and first finish; the optical backing is opaque clear-coat, without refractive inner geometry. |
| 09_IndicatorLED | 172 / none | 78 | [day](../09_IndicatorLED/review/reference_left_round01_right_day.png), [night](../09_IndicatorLED/review/reference_left_round01_right_night.png) | Judge Torx, slotted plug and rib detail. Blue night comparison changes emission tint only; maps remain neutral. |
| 12_MastBeaconSingle | 196 / none | 54 | [day](../12_MastBeaconSingle/review/reference_left_round01_right_day.png), [night](../12_MastBeaconSingle/review/reference_left_round01_right_night.png) | Tip mount only; assess the mapped inner stack/glass and first cast-metal finish. |
| 13_MastBeaconDual | 244 / none | 6 | [day](../13_MastBeaconDual/review/reference_left_round01_right_day.png), [night](../13_MastBeaconDual/review/reference_left_round01_right_night.png) | 8-sided S-budget heads; assess the close-up compromise and the dimension conflict below. |

### Construction and finish

- **07:** 60 × 30 cm graphite trim frame, four mapped corner screws, stepped inner tray and ribbed opal rectangle. The rear 10 cm box is behind mounting plane X=0; only the trim reaches 1.5 cm proud. Lens faces +X and the ceiling mount supplies pitch. 4000 K Rect intent.
- **08:** 14 cm amber dome with rounded shoulder, two screwed cast bands, short cylindrical stem/flange and an 18 cm square four-hex-bolt base, 32 cm overall. The bulb/mirror are in the lens maps; mirror facing is +X at rest. The dome is one continuous UV island, U beginning at +X and increasing toward +Y. The exact per-LOD atlas U remap/V rows are recorded in verification.json for Claude's material sweep.
- **09:** chamfered 24 cm rectangular box, two front Torx heads, a round slotted end plug and recessed four-screw underside diffuser. Neutral white/frosted BaseColor and achromatic LightProfile; [blue](../09_IndicatorLED/review/tint_blue.png), [red](../09_IndicatorLED/review/tint_red.png), [green](../09_IndicatorLED/review/tint_green.png) differ by emission tint only. No real light.
- **12:** 44 × 44 × 56 cm tip-mount envelope, short stem, flared cast underside and cylindrical screwed collar under the red LED dome. No side arm, pole clamp or building mast. Source uses 12 perimeter segments. The candidate FBX is ONLY under candidate/; the existing root production FBX and live mast_beacon_single.py were preserved.
- **13:** shared dome/collar profiles on two individual stems over a bolted bar, with 8 perimeter segments. Negative-Y dome UV0 U=0.025..0.465; positive-Y U=0.525..0.965, including their apex fans. Material can alternate the two halves. No real light. Concealed stem caps are omitted where the bar closes them; no visible part disappears.

Reused the approved #392 deterministic Builder, two-LOD chart packing, graphite/silver wear, local rust/dust, cast grain and normal-bake toolchain without changing its files. Added only the five permitted modules and lighting_393_common.py / lighting_393_finish.py / lighting_393_review.py. Every fixture has exactly Housing then Lens; only optical islands use Lens. Seven runtime maps each: Housing 2048² BaseColor/ORM/Finish_Normal; Lens 1024² BaseColor/ORM/Finish_Normal/LightProfile. One combined DirectX Finish_Normal per slot is sampled on FinishBake UV1 using UV0 tangents. Authoring Micro_Normal inputs stay in source/bake_inputs. Both M/L LODs occupy separate charts in the same maps, and both are painted and Cycles finish-baked.

During author review, corrected the dual UV remap after triangulation, changed the mast collar from an over-conical shape to a cylindrical band over a flared underside, rounded the dome shoulders, moved flange/bar screw bakes onto their visible final bands, padded coloured optical chart margins to remove pale seams, and narrowed the amber bulb core. These corrections are already in the current sources/maps/reviews.

### Verification and evidence

The untouched _work_393/run_pieces.py completed **all five twice**, both exit codes 0, with matching geometry digest arrays. Final logs: ../07_ServiceBayLight/evidence/runner_checked_a.log and runner_checked_b.log. Runner alone wrote _work_393/manifest_393.json. Each piece's rebuild_determinism.json retains both complete records.

Independently reimported all seven FBXs. Counts/budgets, Housing/Lens order, metre/centimetre roundtrip bounds, zero origins, UVMap + FinishBake, zero degenerate geometry/UV triangles, zero positive-area overlaps on both channels, and finite orthogonal tangents passed. Combined M/L two-LOD atlas checks also have zero overlaps. Reopened all five clean sources: exactly two meshes for 07/08, one for 09/12/13, and exactly seven used packed runtime images each, byte-equal to delivered PNGs. Saved-source geometry/UV/attribute digests match the runner. Actual texture sizes, hashes and final-normal bindings are in verification.json; final artifact and generator hashes are in delivery.json. Protected input hashes were rechecked without changes.

Delivered source/<Piece>.blend, runner FBX(s), textures, material_recipe.json, STATE.md, CLAUDE_HANDOFF.md, review/README.md and evidence per piece. Separate *_Preview.blend files contain Blender review context only. All current reference-angle day/night, four lit/unlit context views, producer-LEFT / Round-1-RIGHT comparisons and native 12/150/450 m frames are present. 07/08 also have forced LOD pairs, an enlarged orthographic pair and native 260 m pairs/crops. 08/12/13 have 30% LowPower night views. S-piece crops enlarge native 48 × 48 pixel centres 8×; they are not enlarged fixtures at the nominal distance.

At 1920 × 1080, 60° horizontal FOV, 128 Cycles samples without distance denoising: pixels differing by more than 1/255 at 260 m are **07: 0; 08: 3** (maximum display RGB deltas 1/255 and 3/255 respectively). Numerical measurements for 12/150/260/450 m are in each M/L piece's lod_comparison.json. These are Blender comparisons of the delivered game LODs, not an approved high-poly master or actual gameplay-camera proof.

### Open items for Round 2

- **13 dimensions:** keeping the same 44 cm collars as 12 conflicts with the requested approximate 20 cm whole depth and 80 cm width. This delivery preserves the shared heads and a 2 cm separation: **90 × 44 × 56 cm**. This is an explicit unresolved dimension-authority item, not producer approval. Do not silently flatten the dome/collar.
- **Optical realism:** 08/12/13 use economical opaque clear-coated optical backings, with the bulb/mirror/LED stacks represented in their maps. They do not reproduce refractive interior depth. Their first finish, product-angle match and glass/LED readability need Claude's next visual review against the producer images.
- **S silhouette:** 13 spends 244/250 triangles on both complete heads and the bar; its enlarged 8-sided outline remains visible. Judge it together with the actual 12/150/450 m shots. 09 and 12 retain 78 and 54 triangles of headroom.
- No remaining blocking runner, export, UV, packed-texture or budget failure was found. Passing those checks is not appearance acceptance. The independent Round 4 realism/technical check and producer approval remain outstanding.

No git commands, commits/pushes, Unreal editor work, production integration, protected catalogue/runner/helper edits or writes outside the assigned delivery/helper scope were performed. Claude owns catalogue/code/material/state/light/LOD integration after art acceptance. Next: review this Round 1 set and supply Round 2 notes.

## Round 2 — 2026-10-23 — gpt-6.1-sol

Implemented Claude's REVIEW_R1 across all five pieces. Round 1 delivery files, references and generator snapshots are preserved per piece under **archive/round01/**; 230 archived artifact hashes and all generator snapshots match their original Round 1 receipts. Producer approval remains pending.

The updated catalogue is authoritative over the older BRIEF S-class statements: **12 and 13 are now M, 1200/400, with real LOD1s**. 09 remains S with no LOD1.

| Piece | LOD0 / LOD1 | Headroom | Producer LEFT / Round 2 RIGHT |
|---|---:|---:|---|
| 07_ServiceBayLight | 452 / 164 | 748 / 236 | [day](../07_ServiceBayLight/review/reference_left_round02_right_day.png), [night](../07_ServiceBayLight/review/reference_left_round02_right_night.png) |
| 08_WarningBeacon | 1412 / 724 | 1088 / 76 | [day](../08_WarningBeacon/review/reference_left_round02_right_day.png), [night](../08_WarningBeacon/review/reference_left_round02_right_night.png) |
| 09_IndicatorLED | 222 / none | 28 | [day](../09_IndicatorLED/review/reference_left_round02_right_day.png), [night](../09_IndicatorLED/review/reference_left_round02_right_night.png) |
| 12_MastBeaconSingle | 778 / 346 | 422 / 54 | [day](../12_MastBeaconSingle/review/reference_left_round02_right_day.png), [night](../12_MastBeaconSingle/review/reference_left_round02_right_night.png) |
| 13_MastBeaconDual | 1184 / 376 | 16 / 24 | [day](../13_MastBeaconDual/review/reference_left_round02_right_day.png), [night](../13_MastBeaconDual/review/reference_left_round02_right_night.png) |

### Changes by piece

- **07:** set the panel **19 mm behind** the main trim, added real sloping white-enamel reflector walls, stepped steel rim, four corner screws and two folded spring-latch clips on the long sides. Replaced the ribs with a small diamond/pyramid prismatic finish, faint underlying LED dots and a centre-weighted 4000 K profile. Reduced panel emission from 6 to 3.5 so the night view retains pattern instead of clipping flat white. The rear box remains behind X=0; main trim reaches +1.5 cm, clips +1.9 cm.
- **08:** near-black weathered cast iron with silver edge wear, added metric pitting/scratches; collar overhangs the 14 cm amber dome and has a thin upper screwed band, taller lower band and a groove. Stem is about 10 cm of the 32 cm overall height, with a bottom flange. Four real hex bolts/washers remain geometry. The rounded crown uses a shallow pole fan, **32/16 meridians** and no pinched peak. The optical maps show the bulb envelope, coiled filament/supports, socket/stem and actual concave mirror.
- **09:** changed the black coating to worn bare/galvanised steel with dark pits, scratches and silver wear. Added the large geometric front-top chamfer and raised two-screw bezel. The slotted end plug now has a baked recess and the two front Torx heads remain readable. Strong neutral ribs and a centre-heavy profile produce the hot core / tinted edge. [Blue](../09_IndicatorLED/review/tint_blue.png), [red](../09_IndicatorLED/review/tint_red.png), [green](../09_IndicatorLED/review/tint_green.png) change emission tint only; no blue is baked into BaseColor/profile. Emitting-face normal is −Z.
- **12:** M-class head with a real LOD1, **32/16 dome/collar meridians**, rounded shoulders, four geometric clamp lugs/bolt tabs, cast band, flared underside and stepped stem/gland nut. Taller-than-wide dome; 44 cm collar / 46.2 cm tab envelope / 56 cm height. Baked LED chips and carrier, round pedestal and lower ring replace the old full-dome bars. Tip mount retained; no pole or side arm. Both FBXs are under candidate/; root production FBX and live mast_beacon_single.py are untouched.
- **13:** two shared heads at **0.75 scale**, **24/16 dome/collar meridians**, individual glands/stems through a bolted steel bar with large geometric bolt heads. The decided dimensions are implemented: **80 cm wide × 45 cm tall**, **33 cm collars**, **34.7 cm full depth including clamp tabs**. The Round 1 dimension conflict is closed. Both LODs keep −Y dome U<0.5 and +Y dome U>=0.5, with separate V rows so normal bakes do not overlap.

### Glass / finish method

The opaque Unreal Lens contract is retained. New editable normalized **optical-master** scenes use detailed bulb/filament/mirror or LED/pedestal geometry and thin coloured-window reflections. Cycles day/night renders are projected into each runtime dome's BaseColor and LightProfile. 13 uses the shared 12 head master. Dark coloured fields/rims, compact hot cores, vertical Fresnel ribs, the lower horizontal rib band and fine scuffs/scratches are authored into the optical maps and the single baked finish normal. This replaces the Round 1 blurry streak and full-height/full-width glow patterns.

The profile PNGs are now **16-bit achromatic linear** images to avoid the former quantized concentric contours. Housing runtime maps remain 2048², Lens 1024², seven maps per piece. Exactly Housing then Lens. Each slot uses ONE DirectX Finish_Normal on FinishBake UV1 with UV0 tangents; no normal stacking. Both LODs are explicitly painted and finish-baked in separate charts. Source optical-master images/blends are authoring inputs, not extra runtime maps or hidden game geometry.

The last glass colour/specular pass preserves the geometry, UVs and existing Cycles finish-normal bakes. It adjusts dark-field saturation and reduces the grey reflective veil (glass coat .22, roughness .06, specular IOR level .20). It does not add runtime translucency/refraction. The baked internal view remains an optical approximation with limited changing parallax.

### Technical and review evidence

The untouched **run_pieces.py built all five cleanly twice**, with matching geometry digest arrays and both exit codes 0. Final logs: ../07_ServiceBayLight/evidence/round02_final_a.log and round02_final_b.log; each piece has rebuild_determinism.json with both records. Runner alone wrote the issue-local manifest.

All **nine FBXs** were independently reimported. Counts/budgets, Housing/Lens order, metre/centimetre roundtrip bounds, origin, two UV channels, zero degenerate geometry/UV triangles, zero positive-area UV0/UV1 overlaps and finite orthogonal tangents passed. 07's +X and 09's −Z emitting normals passed. Both dual LODs passed the U-half blink contract. Clean sources reload with exactly two meshes for each M/L piece, one for 09, and **seven used packed runtime images each**, byte-equal to delivered PNGs. Saved-source geometry/UV/shading digests match the runner. Combined two-LOD atlas checks have zero overlaps.

Refreshed all current reference-angle day/night and lit/unlit context views, Round-2 comparison sheets, native 12/150/450 m shots, forced 260 m pairs for all four M/L pieces, enlarged LOD/crop diagnostics, LowPower views for 08/12/13 and indicator colour variants. Separate Preview blends reopen at the product camera. Every piece has refreshed STATE.md, CLAUDE_HANDOFF.md, material_recipe.json, review/README.md, verification/delivery receipts, archive integrity and a REVIEW_R1-resolution checklist. Exact current image sizes, bit depths, hashes and generator versions are sealed in delivery.json.

Native 1920 × 1080 / 60° horizontal FOV comparisons use 128 Cycles samples without distance denoising. Additional **512 fixed-sample** 150/260 m checks disable adaptive sampling and denoising, rendering the centre 128 × 128 region at native pixel scale (outside pixels unrendered). At 260 m, changed pixels >1/255 and maximum display deltas are:

| Piece | 128-sample pixels / max delta | 512-sample pixels / max delta |
|---|---:|---:|
| 07 | 1 / 0.00784 | 3 / 0.00784 |
| 08 | 2 / 0.08627 | 5 / 0.03922 |
| 12 | 9 / 0.11373 | 10 / 0.08235 |
| 13 | 15 / 0.17255 | 15 / 0.09412 |

The counts include shading, optical texture filtering and antialiasing, not just outline. These are measured Blender comparisons, **not claims of identical pixels or accepted gameplay LODs**. Ordinary and stable receipts are retained separately.

### Review focus / next round

No REVIEW_R1 construction request was intentionally deferred, and no blocking build/export/UV/texture/budget failure remains. Claude should judge the current dark glass/rim saturation, baked optical depth and reflection, first weathered-metal balance and the native-distance mast LOD pairs before Round 3. The enlarged LOD views deliberately expose meridian/fastener simplification; they are not the 260 m on-screen size. The internal optical projection is view-dependent and does not create actual refractive depth or arbitrary-angle parallax.

Producer appearance approval, the separate Round 4 realism/technical check and Unreal/runtime material/light/state/LOD integration remain outstanding. No git commands or Unreal/editor work were performed. Protected #392 helpers, the live single-beacon module/root FBX, catalogue and runner were not edited. The new catalogue change came from Claude before this round; all other comparable protected hashes still match Round 1. Writes stayed in the five permitted art folders, permitted art modules/helpers and this appended notes section, with the runner's authorized issue-local manifest output.


## Round 3 — gpt-6.1-sol — 2026-10-23

Read BRIEF.md and REVIEW_R2.md completely. Archived Round 2 per piece under archive/round02/, including sources, maps, exports, reviews, evidence, records and generator snapshots. Every archived artifact was checked against the original Round 2 delivery hashes; all nine generator snapshots match. References and protected root 12 FBX remain in place.

### Changes and counts

| Piece | LOD0 / budget | Headroom | LOD1 / budget | Headroom |
|---|---:|---:|---:|---:|
| 07 | 452 / 1200 | 748 | 164 / 400 | 236 |
| 08 | 1828 / 2500 | 672 | 724 / 800 | 76 |
| 09 | 222 / 250 | 28 | none (S) | — |
| 12 | 842 / 1200 | 358 | 346 / 400 | 54 |
| 13 | 1184 / 1200 | 16 | 376 / 400 | 24 |

- **07:** centre-weighted prismatic panel with darker edges; panel-aligned review Rect illuminates pale enamel tray even in isolated night view. Accepted tray/rim/latches retained. [Day](../07_ServiceBayLight/review/reference_left_round03_right_day.png), [night](../07_ServiceBayLight/review/reference_left_round03_right_night.png). Left image is approved 04 family style, not panel silhouette.
- **08:** replaced full-beauty projection/background with a continuous amber field; only raytraced interior luminance is softly integrated for bulb/socket/curved mirror. No black cup/window or white rectangular emission. Rounded 64-meridian crown; 16.2 cm collar vs 14 cm dome (1.157x), thinner upper/heavier lower band, broken silver band/base chips. Strength 36 improves distant signal. [Day](../08_WarningBeacon/review/reference_left_round03_right_day.png), [night](../08_WarningBeacon/review/reference_left_round03_right_night.png).
- **09:** darker pits/brighter broken scratches; neutral ribbed maps with compact white-hot centre and saturated tint edges. Blue/red/green change only emission Tint. [Comparison](../09_IndicatorLED/review/reference_left_round03_right_night.png).
- **12:** continuous signal-red field; narrow cylindrical carrier (~20% dome width), rounded caps, curved metal reflection and connected round pedestal. Three continuous skirt rings and vertical Fresnel lines. Rounder crown; accepted cast collar/lugs/tip mount retained. [Day](../12_MastBeaconSingle/review/reference_left_round03_right_day.png), [night](../12_MastBeaconSingle/review/reference_left_round03_right_night.png).
- **13:** shared red optics at decided .75 head scale. Raised 80x18x6 cm bar exposes actual hex glands; front bolts stand 16 mm proud. Pits/chips/local bolt oxide. An intermediate LOD0 bolt-anchor error was caught visually and corrected before final builds; actual bolt bounds and gland vertices now checked. Decided 80 cm width, 45 cm height, 33 cm collars, ~34.7 cm clamp depth retained. [Day](../13_MastBeaconDual/review/reference_left_round03_right_day.png), [night](../13_MastBeaconDual/review/reference_left_round03_right_night.png).

### Materials and evidence

12/13 remain M class with real LOD1. Housing/Lens in order; seven runtime maps per piece, Housing 2048² and Lens 1024², 16-bit linear profiles. Separate LOD charts in shared maps. One DirectX finish normal per slot genuinely rebaked in Cycles to FinishBake UV1 using UV0 tangents. Final colour/emission polish preserved those bakes.

M_Kit_Lens stays opaque. Continuous coloured glass/interior field; no projected master background. 08 retains editable raytraced interior inputs. Masts no longer depend on old full-beauty projection. Shader: p=LightProfile.r; hot=saturate((p-.55)/.35); colour=lerp(Tint,HotCoreTint,hot); emission=p*LampLevel*strength*colour. Only the compact core approaches yellow/white. Exact tints, strengths, sweep/dual-half UV contract and light anchors are in recipes/handoffs.

Protected run_pieces.py built **all five twice, exit 0, matching digest arrays**. Final logs: ../07_ServiceBayLight/evidence/round03_delivery_a.log and round03_delivery_b.log; per-piece rebuild_determinism.json stores both records. Runner alone wrote its manifest. All nine FBXs independently reimported: budgets, slots, metre bounds, UV0/UV1, zero degenerates/overlaps and orthogonal tangents passed. Clean source reloads have two meshes (one for 09), seven used packed runtime images each byte-equal to PNGs. Source digests match runner; combined LOD atlas overlap zero. Emitting normals, dual U halves in both LODs, compact-core graph and bar/gland bounds passed.

Refreshed reference day/night, on/off context, Round 3 sheets, native 12/150/450 m views, day-off 150/450 views, forced 260 m M/L pairs, diagnostic enlargements, LowPower and indicator tints. Separate Preview blends preserve product camera. Directly compared every final piece with its reference and far-distance crops; hashed observations in evidence/visual_comparison.json. Caps/points retain hue; 08 is very small and subdued at 450 m without bloom.

Actual 260 m differences (pixels >1/255 / maximum display-channel delta), not identical-pixel claims:

| Piece | 128 samples | 512 fixed samples |
|---|---:|---:|
| 07 | 5 / .03922 | 4 / .01569 |
| 08 | 2 / .14118 | 3 / .09804 |
| 12 | 10 / .21569 | 9 / .03137 |
| 13 | 14 / .19216 | 12 / .12157 |

Stable captures disable adaptive sampling/denoising; only centre 128x128 at native 1920x1080 / horizontal FOV 60 degrees is rendered. Ordinary distance frames use 128 samples without denoising. Both measurements retained.

### Handoff and open judgement

All REVIEW_R2 requests have implementation changes and evidence. Refreshed STATE, full CLAUDE_HANDOFF, recipes, review indexes, resolution/visual checklists, verification/delivery receipts, archive/protected checks. Final inventories seal current files, dimensions, bit depths and generator hashes. Opaque backing lacks true transmission/parallax; close-up optical depth differs from producer photography. Astra must judge that appearance and measured far LOD transitions. No producer approval or runtime acceptance claimed.

No git or Unreal actions. Protected #392 helpers, catalogue/runner, live single-beacon module and root 12 FBX unchanged. Writes restricted to permitted art folders/modules/helpers and this appended section, plus runner-authorized issue-local manifest.


## Round 4 — independent Astra check and fixes — 2026-10-23

Read BRIEF, Rounds 1–3, REVIEW_R3, the shared production lessons and all five #392 astra_check precedents. Independently compared all five pieces with their reference/style image and current product/distance evidence. Round 3 is preserved under each piece’s archive/round03/, including generator snapshots; every prior delivery hash checked without mismatch. **Producer approval is pending.**

| Piece | LOD0 / LOD1 triangles | Remaining budget | Round 4 evidence |
|---|---:|---:|---|
| 07_ServiceBayLight | 452 / 164 | 748 / 236 | [day](../07_ServiceBayLight/review/reference_left_round04_right_day.png), [night](../07_ServiceBayLight/review/reference_left_round04_right_night.png), [check](../07_ServiceBayLight/evidence/astra_check.md) |
| 08_WarningBeacon | 1828 / 724 | 672 / 76 | [day](../08_WarningBeacon/review/reference_left_round04_right_day.png), [night](../08_WarningBeacon/review/reference_left_round04_right_night.png), [check](../08_WarningBeacon/evidence/astra_check.md) |
| 09_IndicatorLED | 222 | 28 | [day](../09_IndicatorLED/review/reference_left_round04_right_day.png), [night](../09_IndicatorLED/review/reference_left_round04_right_night.png), [check](../09_IndicatorLED/evidence/astra_check.md) |
| 12_MastBeaconSingle | 842 / 346 | 358 / 54 | [day](../12_MastBeaconSingle/review/reference_left_round04_right_day.png), [night](../12_MastBeaconSingle/review/reference_left_round04_right_night.png), [check](../12_MastBeaconSingle/evidence/astra_check.md) |
| 13_MastBeaconDual | 1184 / 376 | 16 / 24 | [day](../13_MastBeaconDual/review/reference_left_round04_right_day.png), [night](../13_MastBeaconDual/review/reference_left_round04_right_night.png), [check](../13_MastBeaconDual/evidence/astra_check.md) |

### Changes and retained work

- **07:** retained the recessed panel, enamel returns, trim, latches and centre-weighted prismatic diffuser. Refreshed review/evidence; 04 remains a family-style reference, not the panel silhouette.
- **08:** reduced the washed-out whole-dome glow, retained the filament/mirror in the night image, and balanced a low amber field for the off-axis RTS view. Emission strength 24, linear Tint [1,.10,.002], hot core [1,.48,.08]. Widened the collar from 16.2 to 16.8 cm against the unchanged 14 cm dome: 1.20×. The inherited collar was already 1.157×; the revision is deliberately modest. LowPower remains amber.
- **09:** retained the worn-steel maps and construction; increased night review illumination so the housing is readable. BaseColor and LightProfile were independently checked exactly achromatic. Blue/red/green remain emission-tint-only examples.
- **12/13:** deeper red silhouette/base band, stronger skirt rings, reduced broad emission and a smoother local halo around compact hot LED cores. Strength 14, linear Tint [1,.001,.0004], hot core [1,.22,.035]. Day, full night and 30% LowPower remain distinct. Shared heads and decided dimensions retained.
- **Independent UV-density repair:** area-based UV0 measurement exposed excessive texel concentration at the cylindrical dome tips: 08 previously peaked at about 4653 px/m and 12 at 2426 px/m, above the 2048 lens limit. Reduced those two chart scales, preserving the continuous 08 angular island, geometry and material contracts, then rebaked and rechecked both LODs. Final maxima: **08 1954.45 px/m; 12 1940.79 px/m**. All five now pass the explicit lens-density assertion.
- **13 LOD:** tested two 400-triangle rounded-crown reallocations. Neither improved the measured 260 m comparison; retained the original **376-triangle** LOD1, with no dropped fixture components. The final source, runner exports and evidence all use that retained geometry.

### Technical delivery

The untouched run_pieces.py built **all five twice**, both exit codes 0 and matching geometry/UV/shading digest arrays. Final logs: ../07_ServiceBayLight/evidence/round04_retained_a.log and round04_retained_b.log. Each piece’s rebuild_determinism.json contains both complete final records. Runner alone wrote its authorized issue-local manifest.

All nine final FBXs independently reimported: budgets, Housing/Lens order, metre/centimetre bounds, origins, two UV channels, zero geometry/UV degenerates, zero positive-area overlaps and finite orthogonal tangents passed. Independent source rebuilds match saved geometry; seven used packed runtime images per clean source are byte-equal to the PNGs. Actual PNG dimensions/bit depths and texture hashes are recorded. Each slot has one DirectX Finish_Normal on FinishBake UV1 with UV0 tangents. Final bake coverage, physical chart correspondence and positive closed-shell volumes passed. The 08 angular chart and 13 U-half alternation contracts are documented; 12 exports remain only in candidate/, with its protected root FBX unchanged.

Refreshed all product day/night and on/off views, Round-4 reference sheets, native 12/150/260/450 m evidence, LowPower, indicator tints, astra distance sheets, independent astra_check.md / astra_technical.json / astra_distance_comparison.json, visual observations, recipes, STATE and scoped handoffs. The indicator has LOD0-only sheets; no fictitious LOD1. Preview blends are separate from clean export sources. Final delivery inventories seal current files and generator hashes.

### LOD judgement and appearance limits

512 fixed samples, no adaptive sampling or denoising, centre 128×128 region at native 1920×1080 / 60° horizontal FOV:

| Piece | 260 m changed pixels >1/255 | Maximum display RGB delta | Projected outline difference (px) |
|---|---:|---:|---:|
| 07 | 4 | 0.01961 | 0.00756 |
| 08 | 5 | 0.02745 | 0.01530 |
| 12 | 11 | 0.14902 | 0.09781 |
| 13 | 18 | 0.19216 | 0.06433 |

The pair shows no disappearing components or obvious silhouette loss, but **12/13 retain measurable subpixel brightness variation**. Strict no-pop acceptance is still a producer/runtime review item; this pass does not certify identical pixels. The forced 12 m LOD1 sheets deliberately expose simplification and are not normal runtime LOD use.

Opaque mapped optics are still flatter/softer than the refractive producer photographs and have no true transmission or changing parallax. 08 remains a subdued subpixel signal at 450 m from the off-axis RTS view without bloom. 09’s downward diffuser is physically hidden from the elevated RTS camera; its product-angle tint views demonstrate its emission. No extra upper glow surface was added to disguise that construction. No approved high-poly fixture master or master-equivalence proof is invented.

The requested check/fix artifacts are delivered; producer appearance approval, strict mast LOD no-pop acceptance, and Claude’s Unreal material/light/state/LOD integration remain pending. No git commands, commits/pushes or Unreal actions were performed. Protected inputs match their prior hashes; writes stayed in the allowed piece folders, scoped modules/helpers and this notes section, plus the runner-authorized issue-local manifest.


## Round 4b — 08_WarningBeacon only (gpt-6-astra, 2026-10-23)

Producer notes expanded the 08 revision to moulded glass, grounded optics, a hot night core and collar width. No interrupted edits were present: Round 4 matched its receipt and is hash-verified in 08/archive/round04/. Replaced the painted floating interior with real Housing holder/socket/bracket and Lens bulb/concave mirror in both LODs. Added thin amber transmission, darker edges, vertical ribs/reflections, baked glass wear and a moulded lower lip. The collar seating shoulder is 16.4 cm against the 14.2 cm lip (1.155×); the external stem, base and bolts are retained. Day, night and 30% LowPower were reviewed, plus front/oblique/side/rear attachment views.

Both final runs of run_pieces.py -- 08_WarningBeacon match: **2312/2500 LOD0, 784/800 LOD1**, headroom 188/16. Source/export, slots, UV0/UV1, packed maps, normals and combined LOD atlases pass. Stable 260 m comparison: 6 pixels >1/255, maximum channel delta .05098, outline difference .01530 px; small intensity differences remain, not an identical-pixel claim. The handoff documents R-only emission, G shell transmission and rigid material rotation of the actual interior; the old opaque/projected shader must be replaced.

[Day](../08_WarningBeacon/review/reference_left_round04b_right_day.png) · [Night](../08_WarningBeacon/review/reference_left_round04b_right_night.png) · [LowPower](../08_WarningBeacon/review/reference_left_round04b_right_lowpower.png) · [Attachment angles](../08_WarningBeacon/review/construction_angles_day.png) · [Astra check](../08_WarningBeacon/evidence/astra_check.md) · [Handoff](../08_WarningBeacon/CLAUDE_HANDOFF.md). Final delivery inventory and matching-build receipts are in 08/evidence/.

All 1,241 protected file hashes remain unchanged. **07, 09, 12 and 13 were untouched**; the producer's approvals of 09/12/13 stand. 08 is delivered for producer review; Unreal transparency/rotation evaluation remains Claude's integration work. No git or Unreal actions.
