# #431 — Round 3: the village set

**Delivered for Claude review. Review/approval pending; stop here.** Requested lane: GPT-6.1-Sol, round 3 of 3 Sol rounds. The producer approved Round2 HouseA as the set's quality bar. Round4 has not started.

Open the [final Unreal gallery](round3/REVIEW.html), [manifest](round3/manifest.json) and [opening/firing handoff](round3/openings_handoff.json).

## Delivered

HouseA's four carry fixes plus **HouseB, HouseC, Farmhouse, Barn, Workshop and Church in all three states**, and `SM_Bridge_Span_Damaged` / `SM_Bridge_Span_Destroyed`. There are 23 LOD0 meshes, 23 separate authored LOD1 meshes, 23 source/review masters and eight type-specific baked Blender sources. Round1, Round2 and the greybox deliveries remain unchanged; protected portable hashes were checked.

The six-slot construction and bake pipeline follow approved HouseA. Substantial reveals, frame profiles, sills, lintels, backing, roof thickness, drainage and damage framing remain geometry. Fine stable-edge 20 mm master chamfers are baked into normal/AO at UV1; UV0 retains real-scale scanned surfaces. Narrow profiles are excluded from beveling, and sub-5 mm master junction remnants are dissolved. Geometric normals are baked without adding the scan normal twice; ray outliers are repaired to the receiving normal. Large horizontal surfaces bypass AO. LOD1 uses hand-authored profiles, backing planes, merged coplanar wall strips and wedge debris; whole-face containment removes internal returns, with no global decimation.

### Architecture and variation

- **HouseA:** approved architecture and damage arrangement retained. Rubble brick is darker and dusted while keeping the standing wall's course scale. Fascia/drainage use zinc/PVC colour rather than brown timber; narrow sill/lintel profiles are slimmer and concrete is calmer. Roof macro variation falls to 3.5%, down from Round2's stronger blotches. The dish and number 18 remain on HouseA only.
- **HouseB:** 14 × 9 m, 6/10 m eaves/ridge, paired long-side doors and mirrored bays, warm render, anthracite roof and a central party-wall line. The contracted +X gable entry remains as a service entry in addition to the two long-side front doors.
- **HouseC:** 9.5 m square, 5.6/9 m, brown brick and red-brown roof. The +X ground-floor window at along=-1.5 m is widened into a small shop frontage, with enough masonry left beside the door.
- **Farmhouse:** 18 × 11 m, low 4.8 m eaves and 10.5 m ridge, warm render and a long red-brown roof. Upper long-wall openings and the shorter eave projection clear the inherited 4.5 m firing eye.
- **Barn:** 20 × 13 m, 5/11 m, dark scanned timber cladding, a 4 × 4.5 m entry and wooden hay doors above. Loft openings retain their 5.2 m sill and clear the main entry; their proposed firing eyes rise to 5.95 m. Dark timber uses Planks023A / Planks037A.
- **Workshop:** 16 × 12 m, 5.5/6.6 m shallow roof, two 3.5 × 4 m roller doors, high windows and cool render. The retained high windows require raised firing positions, listed below.
- **Church:** 22 × 11 m nave, 8/15 m roof, 6.5 m tower at -X, 26 m tower and 36 m spire. Older Bricks097, slate-coloured roof, buttresses, tower bands and recessed louvres distinguish the landmark. Damaged loses the spire/top to a nominal 20 m jagged tower; rubble keeps a nominal 8.67 m tower, with broken teeth slightly higher. Opposite-direction 150 m evidence is included.

All new damaged buildings have a stepped breach through the contracted -Y long wall, a broken roof outline on that slope, rafters/battens, partial glazing/frame loss, localized soot and mixed debris. Rubble has closed 30 cm stumps, varied heights below 3 m, fallen gable/roof fragments and mixed material heaps; the church tower is the explicit height exception.

### Bridge

Both states keep the intact span's deck-top-centre origin and 30 × 14 m frame. Damaged has an octagonal shallow carriageway crater through the asphalt/slab top, a nominal 6 m parapet gap and deck chunks. Destroyed retains two 11 m sections hinged at the pier ends at 24°, a roughly 10 m middle gap, irregular concrete break teeth, exposed reinforcement and partly submerged debris. Concrete031 / Concrete036 supply the concrete; asphalt is an in-house flat material. For the shared six-slot pipeline, bridge **Brick=concrete, Roof=asphalt, Trim=reinforcement, Rubble=broken concrete**; these named mappings replace the greybox's two-slot material assumption in integration.

Only two pier-end stub boxes retain collision on the destroyed state. Its native BodySetup uses **CTF_UseSimpleAsComplex** so complex traces also use the stub hulls; carry that setting into setup. The preview raises the span 6 m above a simple river plane and uses two separate support blocks; those supports, water and terrain are staging only and are absent from the FBXs. No intact-span dressing or production bridge/physics integration was attempted.

## Counts and slots

Game source, independent FBX reimport and Unreal counts agree for all 46 LOD0/LOD1 meshes. Counts below are the higher verified values. LOD0 ≤12k, LOD1 ≤3k, ≤6 slots; church also fits its ≤25k/6k allowance. Every candidate has Nanite disabled and one native LOD: the reduced mesh is deliberately separate.

| State | LOD0 | LOD1 | Native LOD0 slots | UCX hulls |
| --- | ---: | ---: | ---: | ---: |
| HouseA | 6,120 | 2,376 | 6 | 2 |
| HouseA_Damaged | 6,158 | 2,988 | 6 | 2 |
| HouseA_Rubble | 1,760 | 1,568 | 4 | 2 |
| HouseB | 6,108 | 2,204 | 6 | 2 |
| HouseB_Damaged | 5,908 | 2,712 | 6 | 2 |
| HouseB_Rubble | 1,092 | 832 | 4 | 1 |
| HouseC | 4,348 | 1,618 | 6 | 2 |
| HouseC_Damaged | 4,596 | 2,151 | 6 | 2 |
| HouseC_Rubble | 1,060 | 804 | 4 | 1 |
| Farmhouse | 7,384 | 2,508 | 6 | 2 |
| Farmhouse_Damaged | 6,824 | 2,959 | 6 | 2 |
| Farmhouse_Rubble | 1,220 | 948 | 4 | 1 |
| Barn | 2,312 | 1,016 | 6 | 2 |
| Barn_Damaged | 2,936 | 1,572 | 6 | 2 |
| Barn_Rubble | 1,220 | 948 | 4 | 1 |
| Workshop | 3,428 | 1,362 | 6 | 2 |
| Workshop_Damaged | 3,772 | 1,907 | 6 | 2 |
| Workshop_Rubble | 1,156 | 892 | 4 | 1 |
| Church | 4,340 | 2,236 | 6 | 3 |
| Church_Damaged | 4,164 | 2,347 | 6 | 3 |
| Church_Rubble | 1,684 | 1,348 | 4 | 2 |
| Bridge Damaged | 564 | 564 | 3 | 4 |
| Bridge Destroyed | 840 | 840 | 4 | 2 |

Portable semantic slot order is **Brick, Roof, Trim, Glass, Wood, Rubble**. `Brick` is the facade slot even on render, timber and concrete variants; map by name using the manifest. Native import strips unused slots per state; LOD0 and LOD1 use matching surviving slots. The very cheap bridge LOD1 retains the same topology/count; no reduction is needed to fit its far budget. Masters remain review/source assets.

All 69 portable meshes pass geometry, UV and tangent checks. One source-only review master, Workshop_Damaged_MASTER, is **4,224 source / 4,223 FBX** triangles; the higher value is retained in the record. No game mesh loses triangles. [Source/FBX QA](round3/evidence/source_qa.json), [native verification](round3/evidence/unreal_verified.json), [bake and normal-repair receipt](round3/evidence/bake_receipt.json).

## Openings and firing integration

All original opening locations are retained or explicitly reshaped in [openings_handoff.json](round3/openings_handoff.json), including original firing-position names. **276 opening/eye ray tests** pass across pristine/damaged states. Glass/leaves/backing sit behind the wall plane; breaches replace the affected apertures. No gameplay garrison test is claimed.

- HouseB/HouseC/Farmhouse attic heads extend to 7.8 m where needed, preserving the inherited 7.5 m eye.
- Farmhouse long-side upper windows move from sill 3.9 m to 3.15 m and become 1.5 m high; the 4.5 m eye remains clear. The short eave projection prevents roof occlusion along that horizontal ray.
- HouseC shop window: sill 0.5 m, width 1.65 m, height 1.8 m; same along position. Its original 1.5 m eye remains inside.
- Barn loft eyes: **4.5 → 5.95 m**. Workshop high-window eyes: **1.5 → 3.3 m**. Church nave-window eyes: **1.5 → 4.4 m**. Claude must apply these proposed eye heights in integration; this delivery does not edit `village.json` or gameplay data.
- Barn's brief capacity is **8**; the greybox says 10 but provides only eight openings. The handoff records brief=8 / contract=10, following the brief's precedence. Other capacities match the contract. Church belfry louvres are visual openings, not added garrison positions.

## Materials, textures and exact authority

Final meshes: `/Game/Art/Candidates/Village/Round3/Final/`; review masters for the church and HouseB spot check: `/Round3/Masters/`. Instances: `/Round3/Materials/MI_<MeshName>_<Slot>[_MASTER]`. Parent `/Round3/M_SetSurface` is a candidate copy of the approved Round2 extension, with defaults repointed to Round3 textures. The shared surface cannot express the scan/atlas graph, so this copy is required; shared parents and previous candidates are untouched. Glass uses the current shared `M_Bld_Glass` directly.

97 delivered PNGs are ≤2048²: **27 unchanged CC0 scan maps** from Bricks085, Bricks097, RoofingTiles013A, Planks023A, Planks037A, Plaster001, Plaster006, Concrete031 and Concrete036; **69 in-house normal/AO/tint atlases**; one white data fallback. Copies match `Saved/Surfaces/<Name>/` by hash. All eight baked sources resolve their external images inside `round3/textures/` ([dependency audit](round3/evidence/source_dependencies.json)). No git-ignored pack is needed for the building materials. Mips and streaming remain enabled in the candidate. See [hash/provenance receipt](round3/evidence/delivery_receipt.json).

Native scans are under `/Round3/Textures/`; final atlases under `/Round3/Textures/Atlas/`, **except Barn, which uses `/Round3/Textures/AtlasBarn02/`** after its hay-door refinement. Old Barn atlas imports are diagnostic history. Church's older English-bond brick uses **ScanUVScale=(0.625,1)** on the facade slot, giving a 1.6 m horizontal / 1 m vertical repeat (roughly 10 cm headers, 20 cm stretchers and 6–6.5 cm courses). Other instances keep the identity default. This scales only UV0 scan sampling; bake UV1 is untouched. [Calibration receipt](round3/evidence/church_brick_scale.json). Normal atlases encode object-space vectors with UE's Y mirror and use linear mask compression; do not import them as tangent normal maps. Source Blender materials show colour intent; the saved native graph/overrides are the final shading authority.

The approved Round2 material policy carries forward: game BakeStrength **0.35** except concrete/Rubble **0.08**, master **0**. The concrete exception calms excess normal/AO contrast on thin sills/lintels while keeping the scan's grain. Geometric atlas normal/AO fades from **180 to 230 m camera-to-bounds-centre** to avoid merged LOD1 chart interpolation. Scanned colour/normal/ORM and tint remain. Keep that graph in Claude's setup. Candidates use **separate LOD1 actors**; Claude attaches the authored files at **260 m**. The 249.6/270.4 m captures are manual substitutions, not an automatic switch implementation. No native LOD attachment or native-dialog UI was used.

## Evidence and persistence

Per the Round3 STATE brief: all three building states have final **150 m and three-quarter** views beside Abrams, one pristine **12 m** view per type, and a **249.6/270.4 m pair** per type. The bridge uses its damaged state for the pair because no intact revision was requested. Church has matched master/game **12/150/450 m** views for all states; HouseB pristine is the additional spot check. All pristine buildings share a whole-set **150 m overview**. Camera receipts record poses, arm lengths and 60° horizontal FOV / 16:9 framing. These are native **editor views, not gameplay**. On larger buildings, the 12 m arm aims at an exterior facade detail rather than the footprint centre, which otherwise puts the camera on or inside a wall/roof; master and game use the same target. These detail views intentionally crop the asset.

First captures were taken per type before refinement and remain in `evidence/first/`. Bridge first views were restaged above water to make the collapsed geometry visible. Final views correct the church's initial cropped spire and show doors/shopfronts/breaches from useful directions. `checks/` and `warmup/` are diagnostics. The [gallery](round3/REVIEW.html) indexes final evidence only.

Final assets and the existing `/Game/Art/Candidates/Village/L_VillagePreview` were saved, the preview reloaded in the same running editor, and all 162 material parents/texture/scalar/vector overrides checked again. Native counts, centimetre bounds, slots, collision hull counts, texture settings and dependencies are recorded. Switch cameras measure 249.6/270.4 m from the LOD0 whole-building bounds centre, shared by both actors; each receipt includes the independently calculated distance. Post-reload smoke captures cover HouseA rubble, Barn, damaged Church and destroyed Bridge; the saved viewport ends on the pristine overview. The editor was neither closed nor restarted by this round. Log history contains one early folder-save error, corrected by saving explicit asset paths; final saves and binding checks succeeded.

## Lessons check, limits and next action

Checked against current modelling lessons: real-scale scans; substantial profiles retained; sealed backing/returns; baked fine edges; horizontal AO bypass; game triangles above the 1 cm width rule; source/FBX/UE counts reported; irregular damage and mixed debris; full-thickness stumps; FBX_SCALE_NONE and centimetre import checks; portable CC0/in-house maps; current shared parents used or explicitly extended; no caches directed into Art; captures before refinement; validated exports; no UI operation that opens a native dialog. LOD1 stays separate per the newest lesson and producer decision.

Claude's visual review is the next gate. Weathering strength, the simple planar roof treatment and remaining master/game shading differences are exposed in the evidence for review. The normal/AO distance fade remains an integration requirement. Production state swaps, LOD attachment, eye-height/capacity updates, collision/navigation/placement, gameplay camera and runtime performance are **not verified** here. Historical candidate imports are retained; consolidation and the final Claude implementation prompt belong to Round4. No commits, pushes, PRs or production edits.

## Sources / continuation

Authoritative geometry/masters/UVs: `round3/source/<Type>_baked.blend` (Bridge included). Recovery checkpoints: `<Type>_geometry.blend`. Individual game/LOD1 FBXs: `round3/fbx/`; master FBXs: `round3/source/*_MASTER.fbx`; textures: `round3/textures/`. Generators are guarded under `Tools/Art/village_431_round3*.py`. The full geometry generator is a destructive rebuild of its named Round3 outputs; do not replay it over reviewed edits merely to export. The bake and QA scripts operate on type-specific checkpoints. Native preview recipes use only the existing Village level. Replaying into occupied native paths requires a fresh revision path; do not invoke a native import dialog.

Use `$world-divide-building-production` for #431. Read `STATE.md` and this report. HouseA Round2 is approved; Round3 is delivered for Claude review, with the documented eye-height/capacity proposals and separate LOD1 policy. Apply the next reviewer/producer decision; do not start Round4 on inferred approval. Keep round1/round2 and the running editor. Do not commit or push.
