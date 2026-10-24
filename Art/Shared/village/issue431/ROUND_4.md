# #431 — Round 4 final set

**Delivered for producer approval of the set. Approval pending; stop here.** Final realism/consolidation round requested on GPT-6-Astra. Round2 HouseA remains the approved design/quality bar; Round3 was reviewed and carried into this round.

[Final gallery](../final/REVIEW.html) · [Claude implementation prompt](../final/CLAUDE_IMPLEMENTATION_PROMPT.md) · [manifest](../final/manifest.json) · [hashes/provenance](../final/hashes.json) · [village.json patch](../final/village_patch.json).

## What changed

- Four red-brown terracotta roofs (HouseA, HouseC, Farmhouse, Church), three lifted anthracite roofs. Actual CC0 roof scans, real-scale UVs, rounded ridge caps on the six new types, restrained eave moss/grime; approved HouseA geometry retained.
- Church: tower entrance, clock faces and belfry louvres on four sides, stone plinth/bands/corner quoins, tall arched nave windows with real frames, irregular torn tower rim and fallen masonry. Broader asymmetric roof breach, exposed framing and retained wall breach. Damaged church is captured on its breach-facing side in light.
- Barn: timber paired doors and loft hatch, braces/hardware/hoist beam, vertical timber grain and board-and-batten relief, retained small stable windows and plinth.
- Workshop: profiled metal lower cladding, two roller doors with guide rails/housings, personnel door, blank signboard, dark-backed glazed skylight strips, drainage.
- Houses: existing chimneys retained on all four houses; painted door/wood colours vary, small window boxes on HouseB/C and a compact HouseB bin cabinet. No extra dish. All buildings retain gutters/downpipes. Plinths receive restrained grime.
- Farmhouse/church roof tears are wider and asymmetric, with broken battens/tiles and scorch. Full wall thickness and backing retained. Existing breach debris retained.
- Destroyed bridge halves drop 36° from their pier ends. At the +6 m preview deck height the broken deck tips dip below the river surface; rebar and water debris remain. Two stub collision hulls only.

## Portable and Unreal authority

`Art/Shared/village/final/`: 23 game FBXs, 23 separate LOD1 FBXs, 23 review-master FBXs, eight baked Blender sources, portable PNGs, native material recipe/overrides/settings, collision and opening data. All game exports preserve their names and origins. Footprints/main massing retained; attached door steps, trim, bin cabinet and hoist project locally as documented geometry.

Unreal: `/Game/Art/Candidates/Village/Final/Meshes/`, `Masters/`, `Materials/`, `Textures/Scans/`, `Textures/Atlas/`, `Textures/Tint02/`, `Textures/AtlasBridge02/`, and `M_SetSurface`. Existing `/Game/Art/Candidates/Village/L_VillagePreview` remains the only preview World. Historical actors/packages are retained and parked. Early `/Final/First/` imports and root `/Final/Textures/` scans are historical; the final dependency closure does not use them. Bridge final meshes and masters are in `Meshes02/` and `Masters02/`; their revised atlas is in `Textures/AtlasBridge02/`. Farmhouse/Church damaged tint uses `Textures/Tint02/`. The manifest provides the exact per-mesh mapping. Every final mesh and texture was imported from the final portable folder; asset-registry source tags are in the manifest.

## Verification and evidence

First native captures preceded each type's refinement. Final gallery covers **every state** at 12 m, 150 m, 450 m and three-quarter; all 23 master/game comparisons at 12/150/450 m; all 23 manual 249.6/270.4 m pairs measured from the same LOD0 bounds centre; whole-set overview; five post-reload smoke views. 60° FOV throughout. These are editor views, not gameplay-camera or automatic-LOD evidence.

All 69 source/export meshes pass geometry/UV/tangent checks; all 46 game source/FBX/Unreal counts agree. 276 aperture rays pass. The Workshop damaged review master is 5,412 source / 5,411 Blender FBX re-import / 5,412 Unreal triangles; the higher count is retained. Game budgets are judged on the higher count. All 69 imported meshes have Nanite off and one native LOD; the authored LOD1 remains a separate mesh. All final instance parents/texture/scalar/vector bindings persisted through a same-editor reload. Native dependency closure is recorded and contains no earlier-round dependency. All eight saved Blender sources resolve images inside the delivery.

| Mesh/state | LOD0 triangles | LOD1 triangles | Slots | UCX hulls |
|---|---:|---:|---:|---:|
| HouseA | 6,120 | 2,376 | 6 | 2 |
| HouseA_Damaged | 6,158 | 2,988 | 6 | 2 |
| HouseA_Rubble | 1,760 | 1,568 | 4 | 2 |
| HouseB | 6,196 | 2,274 | 6 | 2 |
| HouseB_Damaged | 6,012 | 2,798 | 6 | 2 |
| HouseB_Rubble | 1,092 | 832 | 4 | 1 |
| HouseC | 4,412 | 1,670 | 6 | 2 |
| HouseC_Damaged | 4,676 | 2,219 | 6 | 2 |
| HouseC_Rubble | 1,060 | 804 | 4 | 1 |
| Farmhouse | 7,400 | 2,524 | 6 | 2 |
| Farmhouse_Damaged | 6,856 | 2,991 | 6 | 2 |
| Farmhouse_Rubble | 1,220 | 948 | 4 | 1 |
| Barn | 3,768 | 2,108 | 6 | 2 |
| Barn_Damaged | 4,324 | 2,612 | 6 | 2 |
| Barn_Rubble | 1,220 | 948 | 4 | 1 |
| Workshop | 4,544 | 2,172 | 6 | 2 |
| Workshop_Damaged | 4,844 | 2,675 | 6 | 2 |
| Workshop_Rubble | 1,156 | 892 | 4 | 1 |
| Church | 7,220 | 5,286 | 6 | 3 |
| Church_Damaged | 7,000 | 5,003 | 6 | 3 |
| Church_Rubble | 2,260 | 1,786 | 4 | 2 |
| Bridge Damaged | 564 | 564 | 3 | 4 |
| Bridge Destroyed | 984 | 984 | 4 | 2 |

## Lessons and finish-workflow check

Real projecting facade details remain geometry; fine chamfers are baked from review masters. No size-based visible-part deletion or global decimation. Openings have returns/backing; rubble walls retain 30 cm thickness. Large flat surfaces bypass geometric AO. Game triangle altitude is above the approximately 1 cm threshold; independent FBX tangents checked. Physical brick/plank/tile scale checked against the close views. Mixed debris uses the building's own materials. Explicit `FBX_SCALE_NONE` export and native centimetre bounds checks prevent 1/100 scaling. Textures are CC0/in-house PNGs at most 2K, with mipmaps retained. Glass uses the current shared parent directly; the private surface graph is needed for scan/atlas inputs and is documented. No native-dialog operation, native LOD attachment, production-level edit, commit or push.

Before changes, saved Village candidate packages were archived with hashes in `round4/archive/pre_round4_candidates.zip` and `receipt.json`. A first validated portable geometry checkpoint is also retained. Final closure was copied/imported and explicitly saved; serialized parents, slot bindings and registry dependencies were checked. No historical package was deleted: a fresh editor process was forbidden, so same-editor reload verification is the stated limit and recovery dependencies remain. Final smoke images follow reload. Editor left running.

## Integration and remaining acceptance

Producer visual approval is pending. The handoff lists the exact opening/eye changes: Barn loft 5.95 m, Workshop high windows 3.3 m, Church nave 4.4 m; Barn capacity 8 per brief. Original entry helpers remain. Decorative new doors are not new gameplay helpers. `village_patch.json` contains ready-to-merge building/mesh entries; greybox originals were not edited.

Preserve scan/atlas UV conventions, object-normal decode, Church brick UV scale, normal/AO fade 180–230 m, and whole-building LOD1 at 260 m. Final graph/overrides and all material bindings are serialized. The LOD1 near-detail atlas is for far use only; the documented fade is required.

Still untested: #432 idempotent setup/regeneration, actual production state swaps and firing, collision/navigation/selection/placement/rotations, damage and bridge gameplay, actual camera/culling/shadows/LOD switching, owner/power applicability, and representative village runtime measurements at 240 units. No production readiness or performance claim is made from these editor views.
