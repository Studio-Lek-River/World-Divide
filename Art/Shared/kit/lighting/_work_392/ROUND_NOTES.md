
# Round 1 — 2026-10-23

Built all five assigned fixtures from the producer images as deterministic Tools/Art/kit/<module>.py build(piece) pairs. Delivered first full PBR maps, Cycles finish-normal bakes, packed clean sources, separate review blends, both runner FBXs, recipes, four day/night on/off views, enlarged LOD sheets, actual-distance 260m pairs and per-piece STATE / CLAUDE_HANDOFF / evidence. Ready for Claude's between-round review; no producer approval claimed.

| Piece | LOD0 / LOD1 triangles | Reference-angle Cycles renders | Open review items |
|---|---:|---|---|
| 01_WallLamp | 360 / 128 | [Day](../01_WallLamp/review/pbr_day_on.png) / [Night](../01_WallLamp/review/pbr_night_on.png) | Review the small Torx/retainer normal detail and the cast-paint wear at close-up scale. |
| 02_CageWallLamp | 732 / 388 | [Day](../02_CageWallLamp/review/pbr_day_on.png) / [Night](../02_CageWallLamp/review/pbr_night_on.png) | Review frost translucency, fastener clarity and the distribution of edge rust. Frost is an opaque subsurface approximation, not volumetric glass. |
| 03_EntranceLight | 428 / 148 | [Day](../03_EntranceLight/review/pbr_day_on.png) / [Night](../03_EntranceLight/review/pbr_night_on.png) | Review the bezel finish and weathering balance against the producer image. |
| 05_DirectionalSpot | 1564 / 660 | [Day](../05_DirectionalSpot/review/pbr_day_on.png) / [Night](../05_DirectionalSpot/review/pbr_night_on.png) | Review the honeycomb/reflector and cast-paint finish. The review area light approximates illumination; Claude must validate the prescribed Spot beam in Unreal. |
| 06_RoofFloodLight | 2072 / 784 | [Day](../06_RoofFloodLight/review/pbr_day_on.png) / [Night](../06_RoofFloodLight/review/pbr_night_on.png) | Review the LED/glass and metal finish. LOD1 has only 16 triangles spare; preserve the budgets during later refinements. Review uses two area lights for presentation; the draft catalogue specifies one combined fixture light. |

Construction changes: 01 has its top chamfer, backplate and recessed ribbed underside. 02 has the capsule globe, open cast frame and three flat cage straps (twists corrected). 03 has the cantilever plate/head, clear opal aperture and drip lip. 05 has the flared can, real slotted fork, knuckle hardware, bracket feet and curved braided cable with seated glands. 06 has twin three-panel heads, eight horizontal fins each, real pivot axles, U-yoke, post, four-bolt base and curved feeds seated into the crossbar. Head axes are −30° and −15° respectively.

Validation: the untouched run_pieces.py built all five cleanly twice. Both runs produced matching geometry digests. Independently reimported all ten exported FBXs; checked counts, Housing/Lens slot order, metre/centimetre roundtrip, zero origin, mesh/lens bounds, UV0 and FinishBake channels, zero degenerate triangles/UV triangles and zero positive-area UV overlaps per atlas. Reopened all five sources: exactly the two LOD meshes and at least seven packed material images each. Nine delivered maps per piece, actual dimensions/hashes recorded: Housing ≤2048², Lens ≤1024².

Every source has an editable material graph. Painted housing is dielectric except restrained exposed wear; frosted/prismatic lens finishes are separate islands. Final DirectX finish normals use UV1 with UV0 tangents. Recipes and handoffs explain the original micro-normal versus baked finish choice and light anchors.

Review paths in every piece: review/pbr_day_on.png, pbr_day_off.png, pbr_night_on.png, pbr_night_off.png; review/rts_LOD0_left_LOD1_right.png; review/switch_260m_lod0.png, switch_260m_lod1.png and switch_260m_crop_8x_LOD0_left_LOD1_right.png. review/README.md is the visual index. Evidence: verification.json, cameras.json, lod_comparison.json and delivery.json in each evidence folder. The 260m pairs differ by >1/255 in 1 / 1 / 1 / 3 / 5 pixels respectively; these are Blender rendering comparisons, not a claim of bit-identical geometry or runtime acceptance.

Round 1 limitations: first material/fastener/glass finish remains subject to Claude and producer review. Cycles review lights are presentation approximations; no Unreal/editor/gameplay validation was performed. No catalogue, shared manifest, runner, engine files or other pieces were edited, and no git commits/pushes were made. The issue runner alone wrote its authorized temporary _work_392/manifest_392.json. Claude owns subsequent coding/integration and actual gameplay-distance validation after art acceptance.

Next: Claude reviews this Round 1 delivery and sends notes for Round 2. Keep the reference-driven construction and existing approval decisions; do not treat this round report as producer acceptance.

# Round 2 — 2026-10-23

Responded to Claude's Round 1 fidelity review while preserving the accepted technical contract. Reopened all producer references and generated comparison sheets (reference LEFT / Round 2 RIGHT). Current STATE.md and CLAUDE_HANDOFF.md in every piece supersede the Round 1 records; historical sources, renders, maps and generator copies are in archive/round01/. No producer appearance approval is implied.

| Piece | LOD0 / LOD1 | Triangle headroom | Matched close-ups | Open review items |
|---|---:|---:|---|---|
| 01_WallLamp | 624 / 208 | 576 / 192 | [Day](../01_WallLamp/review/ref_match_day.png) / [Night](../01_WallLamp/review/ref_match_night.png) | Review the cast-grain scale, worn-edge contrast and side Torx appearance against the matched photos. |
| 02_CageWallLamp | 1180 / 396 | 20 / 4 | [Day](../02_CageWallLamp/review/ref_match_day.png) / [Night](../02_CageWallLamp/review/ref_match_night.png) | Review final frost/core balance, cast-bar cross-sections and edge rust. The frosted glass uses an opaque subsurface/emissive approximation. |
| 03_EntranceLight | 348 / 124 | 852 / 276 | [Day](../03_EntranceLight/review/ref_match_day.png) / [Night](../03_EntranceLight/review/ref_match_night.png) | Review the corrected projection, bracket proportions and final metal/prismatic finish. |
| 05_DirectionalSpot | 2466 / 718 | 34 / 82 | [Day](../05_DirectionalSpot/review/ref_match_day.png) / [Night](../05_DirectionalSpot/review/ref_match_night.png) | Review the single-arm/can proportions and final reflector/braid/paint detail. Only 34 LOD0 triangles remain available. |
| 06_RoofFloodLight | 2172 / 788 | 328 / 12 | [Day](../06_RoofFloodLight/review/ref_match_day.png) / [Night](../06_RoofFloodLight/review/ref_match_night.png) | Review the glass/LED balance and base/post wear. The glass is represented as a clear-coated emissive backing, without a refractive material. LOD1 has 12 triangles spare. |

Changes by piece:

- **01:** larger top/front chamfer; thin all-around backplate; smoother geometric side Torx bosses; four normal/colour bezel screws and two front case screws; stronger fine rib structure.
- **02:** rounded backplate with four mapped Torx screws, thicker oval cast ring and screw bosses, five-ring smooth globe, thicker round-section cage bars and pointed meridian/frame connections. Higher segment counts use the spare M budget. Frost normal and dirt/core profile use physical-space bakes, reducing UV-stretched detail; strongest rust bloom of the five.
- **03:** rebuilt at 35cm long along +X / 20cm wide, with rear vertical plate, short support block, low plate Torx, side Torx, four bezel screws and a downward prismatic lens.
- **05:** single slotted flat arm instead of double fork; rounded slot ends, 40-segment can/rolled mouth, smooth knuckles, rear cap profile, clamp band/block, rear-top gland, braided cable and reflector 5.1cm behind the mouth. Rest pitch remains −30°.
- **06:** retained the −15° twin-head tilt, real fins on BOTH backs (eight each), pivot axles/yoke/post/base; added cover islands over the three-panel fields, smooth hinge washers, glass-coat reflections and stronger base/post wear. review/rear_fins.png shows both fin arrays.

All pieces now use darker charcoal coating with separately controlled cast grain, sparse pits, bright chipped metal edges/fasteners, recess dust and rust. BaseColor/ORM and finish-normal content carry the changes; no microscopic weathering geometry was added. Lens structure remains visible in the lit profiles: ribs/prisms, frosted core and specks, deep honeycomb, and cool LED dot fields under a reflected cover. Fixture temperatures remain 3000/3000/3500/3000/4000 K.

**One final normal per slot:** only *_Housing_Finish_Normal.png and *_Lens_Finish_Normal.png are current runtime normal files. Surface/fastener/optical detail and bevel normals are combined in the bake on dense FinishBake UV1; UV0 remains the tangent basis. No stacking. The former *_Normal.png files were removed from current textures after preservation in the Round 1 archive. Authoring-only Micro_Normal inputs live under source/bake_inputs. Seven runtime maps per piece, all ≤2048² Housing / ≤1024² Lens.

Validation: the untouched run_pieces.py rebuilt all five twice; both runs produced matching geometry digests. Reimported all ten FBXs and checked triangle budgets, Housing/Lens order, metre/centimetre roundtrip, origins/bounds and both UV channels. Zero degenerate geometry/UV triangles and zero positive-area UV0/UV1 overlaps per material atlas. Reopened the clean sources: exactly two mesh objects, seven packed runtime images each, byte-for-byte equal to their PNGs. Verified one final normal binding per slot. Final image/source/FBX hashes, generator versions and reference hashes are recorded in each evidence/delivery.json.

All requested review files are refreshed: ref_match_day.png / ref_match_night.png; four pbr day/night on/off images; reference_left_round02_right_day/night.png; LOD comparison sheet; actual 260m frames and 8× crop. At 1920×1080 / 60° FOV / 260m, pixels differing by >1/255 are 2 / 4 / 6 / 6 / 11 respectively. These are Blender comparisons, not bit-identical geometry or runtime acceptance. Camera/crop metadata and numerical receipts accompany them.

Remaining: Claude's visual review before Round 3, then producer acceptance through the prescribed rounds. Final grain/rust/optical balance remains a visual judgment. Frost and clear-covered LED fields use economical opaque/subsurface/clear-coat approximations; no refractive game material was authored. All engine import/material/state/LOD integration and actual gameplay-distance validation stay with Claude after art acceptance. No catalogue, protected runner/shared manifest, engine files or other pieces were edited; no Unreal editor work, git commit or push was performed. The authorized temporary manifest was written only by run_pieces.py.


## Round 3 — final art refinement, 2026-10-23

Implemented Claude's Round 2 notes. This is the last Codex art round; the separate final check and producer approval remain outstanding. Round 2 source, maps, reviews, evidence, FBXs, records and generator copies were preserved under each piece's archive/round02/.

Across all five, rebaked dark cast-metal PBR with bright silver edge chips and worn fastener heads. Removed the broad orange/brown edge wash; oxidation now sits locally around fasteners, at selected low edges and in short gravity-directed streaks, strongest on 02. Preserved the single final UV1 Finish_Normal per slot, UV0 tangents, texture sizes and Housing/Lens order. Warmer 01/02/03/05 centre-weighted profiles keep their ribs/frost/honeycomb visible. 06 retains cool 4000 K.

All daylight views were rebuilt with a neutral sky environment, a separate neutral SUN and visible concrete wall/roof support. Day-on/reference-day uses LampLevel 0.05, day-off uses 0; night-on uses 1. Day and night are separately rendered files. Reference cameras keep the main-product angles; night keeps the isolated comparison setup.

| Piece | Round 3 change | LOD0 / LOD1 | Budget left LOD0 / LOD1 | Reference renders |
|---|---|---:|---:|---|
| 01_WallLamp | Kept the accepted geometry and UVs. Rebalanced edge chips and Torx heads to bright silver, with oxidation confined to fasteners, low edges and short drips. Increased lit diffuser rib contrast and added a warmer centre-to-edge light profile. | 624 / 208 | 576 / 192 | [day](../01_WallLamp/review/ref_match_day.png), [night](../01_WallLamp/review/ref_match_night.png) |
| 02_CageWallLamp | Reallocated LOD1 detail to a 24-segment rounded globe; both LODs share a 12-point cage bend centreline. Restored chamfered LOD1 plate corners by removing concealed plate and screw-boss backs. LOD0 omits buried screw-boss caps to fund the smoother bend. Frost is fine and even, with sparse faint bottom marks; the oval frame has silver edge wear and localized fastener rust. | 1188 / 398 | 12 / 2 | [day](../02_CageWallLamp/review/ref_match_day.png), [night](../02_CageWallLamp/review/ref_match_night.png) |
| 03_EntranceLight | Kept the accepted projecting head, bracket and UVs. Replaced broad rust edges with silver wear and local fastener stains. Increased prismatic rib contrast and added a warmer centre-to-edge profile. | 348 / 124 | 852 / 276 | [day](../03_EntranceLight/review/ref_match_day.png), [night](../03_EntranceLight/review/ref_match_night.png) |
| 05_DirectionalSpot | Kept the accepted single slotted arm, can, mouth lip, cable, honeycomb and UVs. Rebalanced silver edge wear and local oxidation, preserving the honeycomb modulation under the warmer centre-weighted profile. | 2466 / 718 | 34 / 82 | [day](../05_DirectionalSpot/review/ref_match_day.png), [night](../05_DirectionalSpot/review/ref_match_night.png) |
| 06_RoofFloodLight | Both heads now have 12 tapered cast cooling fins spanning the full rear width and height, identical in both LODs; only buried fin roots are open. Replaced round LED dots with a dense square grid behind a smoother clear-coated cover, with a distinct glass reflection. Retained the 15-degree-down tilt. Rebalanced base/post and frame wear to silver with localized rust. | 2124 / 740 | 376 / 60 | [day](../06_RoofFloodLight/review/ref_match_day.png), [night](../06_RoofFloodLight/review/ref_match_night.png) |

The cage LOD1 globe now uses 24 perimeter segments instead of 12. Both LODs share a 12-point vertical cage bend path. LOD1 pays for this through concealed plate/boss backs, a simpler frame and three-sided smoothly shaded cage sections; chamfered plate corners are retained; LOD0 frees buried boss caps. The flood has 12 full-width tapered cast fins on each head in both LODs, with only buried roots omitted; review/rear_fins.png shows both stacks. Its optical maps now show a dense square LED grid with a smooth, reflective clear-coated cover. Accepted geometry/UV layout for 01, 03 and 05 was retained.

Validation: the supplied run_pieces.py rebuilt all five twice with matching geometry digests. All ten FBXs were independently reimported; counts, budgets, slot order, origins, metre/centimetre bounds, both UV channels, zero UV0/UV1 positive-area overlaps and zero degenerate geometry/UV triangles passed. Clean sources reload with exactly two meshes and seven packed runtime images each; packed bytes match delivered PNGs. Each slot samples exactly one combined Finish_Normal on UV1 with UV0 tangents. Final SHA256 receipts and image dimensions are in evidence/delivery.json.

Refreshed every current review: reference-angle day/night, four context day/night on/off views, producer-left / round03-right comparisons, enlarged orthographic LOD pairs, actual 260m frames and 8× crop sheets. At 1920×1080, 60° horizontal FOV and 260m, pixels differing by >1/255 are 2 / 3 / 6 / 8 / 12 for 01 / 02 / 03 / 05 / 06 respectively; these are measured Blender comparisons, not a runtime acceptance claim. Camera/light settings and numerical receipts are in evidence/. Removed superseded quick checks and prior-round comparison files from the current review folders; archives retain prior evidence.

### Unfinished / final-check attention

- 01, 03, 05: no requested Blender fix deliberately deferred; final reviewer should judge the silver-wear and optical-profile balance against the producer images.
- 02: rounded globe and shared cage bend path are corrected. The enlarged LOD sheet still exposes the intentionally simplified LOD1 frame and bar cross-sections; the separate final checker must assess the small remaining visual difference. Frost retains the existing economical opaque subsurface/emissive approximation.
- 06: both 12-fin stacks and square LED fields are complete. Glass remains the existing clear-coated emissive backing approximation, without refractive transmission; inspect its visible daylight reflection and night grid balance.
- All five: separate final fidelity review and producer approval are pending. Unreal import, master material/atlas adoption, collision, runtime light/state/LOD integration and gameplay-camera validation remain Claude's assigned work after acceptance and were not performed here. No known blocking build/export/UV/budget defect remains.

Updated STATE.md, CLAUDE_HANDOFF.md, material recipes, review indexes and evidence per piece. No Unreal editor work, production integration, git commit or push. No protected catalogue/runner, shared production manifest, engine files or other pieces were edited. The authorized temporary manifest was produced only by run_pieces.py.


## Round 4 — final check, 2026-10-23

Independent reference, source, generator and FBX audit of all five fixtures. Fixed discovered defects and regenerated the delivery. **No producer approval is claimed.** Prior Round 3 deliveries and generator copies were preserved under each piece’s archive/round03/.

| Piece | Final LOD0 / LOD1 | Triangle headroom | Projected fixture-outline difference at 150 / 260 m | Detailed nine-item check |
|---|---:|---:|---:|---|
| 01_WallLamp | 624 / 208 | 576 / 192 | 0.0164 / 0.0094 px | [audit](../01_WallLamp/evidence/astra_check.md) |
| 02_CageWallLamp | 924 / 388 | 276 / 12 | 0.0381 / 0.0220 px | [audit](../02_CageWallLamp/evidence/astra_check.md) |
| 03_EntranceLight | 348 / 124 | 852 / 276 | 0.0221 / 0.0128 px | [audit](../03_EntranceLight/evidence/astra_check.md) |
| 05_DirectionalSpot | 2466 / 718 | 34 / 82 | 0.0408 / 0.0235 px | [audit](../05_DirectionalSpot/evidence/astra_check.md) |
| 06_RoofFloodLight | 2288 / 800 | 212 / 0 | 0.0225 / 0.0130 px | [audit](../06_RoofFloodLight/evidence/astra_check.md) |

### Corrections

- **All five:** LOD1 previously packed its charts independently but sampled textures authored only for LOD0. Both LODs now have disjoint charts in the same seven runtime maps, and both are painted and independently normal-baked. This removes the misplaced LOD1 fasteners, finish and lens shading. Housing UV0 still targets 512 px/m; maps stay at 2K Housing / 1K Lens, with exactly Housing then Lens.
- **All five:** narrowed blurred edge wear, reduced oversized pits, retained local oxidation, and repaired the misplaced centre-weighted optical-profile code. Recipes now report the actual 0.18 lower clamp on lens ORM roughness. Display tints use normalized linear approximations at 3000 / 3000 / 3500 / 3000 / 4000 K; removed 06’s blue override.
- **Normals:** independent auditing exposed backward-facing open spotlight glass, hidden by Cycles’ double-sided beauty renders. Open lens/trim faces now retain their intended winding. 05 and 06 emitting-face normals align with their light axes in both source and reimported FBXs. Invalid negative/grazing bevel-bake samples caused by intersecting hidden construction fall back to the authored surface-detail normal on the same UV1, inside the one final normal map per slot. Normal-bake repair receipts distinguish atlas pixels (including unused space) from geometry counts.
- **02:** matched plate corner profiles; fixed asymmetric capsule perimeter sampling; shared a four-ring globe profile, with 48/24 perimeter vertices for close-up smoothness and far silhouette. Replaced round bars with flat cast straps and explicit width axes. LOD1 horizontal hoops keep the front apex so they cannot cut through the globe. Ray checks prove the vertical and both horizontal bars remain in front of the lens in both LODs. Only the concealed LOD1 globe back cap is omitted to fund visible shape. Refined physical-space frost and localized cage corrosion.
- **06:** replaced triangular spikes with **16 full-width, 3 mm thick rectangular fins per head in both LODs**. The rear view shows the two complete stacks. Removed buried duplicate LED planes behind the opaque clear-coated cover; each cover still shows three LED fields and physical front separators. Reduced concealed LOD1 rail/bolt faces and cable sections to meet the 800-triangle limit exactly.
- **01/03/05 construction:** preserved the reference-driven wall pack, cantilever and mechanical spot assembly; corrected finish, binding and/or optical issues without changing their triangle counts.

### Verification and evidence

The untouched run_pieces.py rebuilt all five twice after the final geometry changes; recorded manifest digests match. Separately built each module twice after read_factory_settings; source geometry, UV and smoothing digests match the generated meshes. All ten FBXs reimport with correct counts, slot order, origins, centimetre/metre roundtrip and UV channels. Custom normals survive export; tangents are finite, unit and orthogonal. Closed components have positive volume. Zero degenerate geometry/UV triangles and zero positive-area UV0/UV1 overlaps, including the combined two-LOD atlases. Seven used packed images per source match the delivered PNG bytes; texture colour spaces, dimensions and one-final-normal bindings pass.

Refreshed reference-angle day/night and four context on/off views, producer-left / Round-4-right comparisons, enlarged orthographic LOD pairs, native 12/150/260/450 m frames and labelled-path crop pairs. These compare delivered LOD0 and LOD1; they do not imply an approved high-poly master or Unreal/gameplay evidence. At native 150/260 m there is no discernible silhouette pop; measured projected outline differences remain below 0.041 px. Shading differences are explicitly reported, not described as identical pixels.

The flood’s initial 32-sample denoised 260 m pair had a maximum display difference of 0.192. A diagnostic 512-sample pair without denoising reduces that to 0.0235 (150 m: 0.0314), with identical lens outlines. Both sets are retained. The high-sample images render the documented centre 128 × 128 region at native 1920 × 1080 / 60° pixel scale; pixels outside that region are unrendered.

Each piece has current STATE.md, CLAUDE_HANDOFF.md, evidence/verification.json, evidence/astra_check.md, evidence/astra_technical.json, distance measurements and an updated artifact/hash inventory. Reference originals are unchanged. Earlier comparison sheets are retained in archive/round03 rather than mixed with current Round 4 evidence.

### Open item and handoff

OPEN: dimensional authority for 06. The appearance table’s approximate 80 × 40 × 75 cm envelope conflicts with the detailed head/base dimensions. This delivery retains the detailed brief and measures 82.4 × 27.9 × 70.9 cm (width/depth/height). A producer clarification was requested; no answer or approval is assumed. Do not stretch the export silently.

No remaining blocking generator/export/UV/budget defect was identified. Producer appearance approval remains pending for all five; optics are documented opaque/subsurface/clear-coat approximations. Claude owns all Unreal/material/atlas, collision, runtime state/light and gameplay-camera/LOD integration. No Unreal editor work, catalogue edit, production integration or git commit/push was performed. The temporary _work_392 manifest was written only by the supplied runner.


## Round 4b — fix pass

2026-10-23. Limited to the three producer-requested corrections. Producer approval remains pending.

- **01/03/05/06:** replaced fine-noise-gated, stitched edge highlights with continuous irregular rubbing and sparse wider chips. Geometry, dimensions and both UV channels are unchanged (source/generator digests checked).
- **02:** all 13 corresponding mounting-plate charts now share LOD0/LOD1 UV0 and FinishBake regions (maximum coordinate difference 5.97e-8). Restored the rear cast-frame lip and globe closure to remove the orange indirect-light leak; retained a rounded capsule top. LOD1 uses a 14-vertex frame perimeter, 20-vertex globe perimeter and a two-triangle front closure behind the vertical strap. Final count **924 / 398**, within 1200 / 400.
- **06:** replaced the peach emission with producer-matched neutral-white display tint **[0.89, 0.94, 1]**, keeping the specified **4000 K** and strength 5. Lens BaseColor/ORM/LightProfile are byte-identical to Round 4; LightProfile RGB channels are exactly equal and modulate intensity only. Preview fixture lights use the same corrected tint.

Rebuilt all five twice with the untouched run_pieces.py: matching digests; all ten FBX reimports passed. Counts: **01 624/208; 02 924/398; 03 348/124; 05 2466/718; 06 2288/800**. Factory-reset determinism, slot order, per-LOD UV non-overlap, normals/tangents and normal bindings passed. Refreshed affected reference/context/LOD/distance renders, 06 rear and stable-distance evidence, verification and artifact receipts, and affected astra_check/STATE/handoff entries. Earlier Round 4 delivery is preserved in each piece’s archive/round04/. Other decisions, including the existing 06 dimension-authority open item, remain unchanged. No Unreal, catalogue or integration edits; no commits.
