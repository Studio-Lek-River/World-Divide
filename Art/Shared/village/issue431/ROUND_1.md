# #431 — Round 1: HouseA architecture

**Ready for the producer's architecture review. Stop here; no approval inferred.**
Requested lane: GPT-6.1-Sol, round 1 of its 3 rounds (round 1 of 4 overall). 2026-10-23.

Open [the visual review gallery](round1/REVIEW.html), especially its 150 m row, then the three-quarter row.
The review question is HouseA's proportions, roof, openings and damage silhouette. Round 2 has not started.

## Delivered

- [Authoritative Blender source](round1/source/HouseA_round1.blend): three named state collections, each at the same local origin. Pristine is visible by default; toggle collections to inspect the others. Textures link relatively to `../textures/`.
- Six FBXs in [round1/fbx/](round1/fbx/): pristine, damaged and rubble, each with a separate first-pass `_LOD1.fbx`.
- Twelve unchanged 2K CC0 PNGs in [round1/textures/](round1/textures/).
- [Manifest](round1/manifest.json), [source QA](round1/evidence/source_qa.json), [live Unreal verification](round1/evidence/unreal_verified.json) and [SHA-256 receipt](round1/evidence/delivery_receipt.json).
- Generators: `Tools/Art/village_431_housea.py`, `village_431_preview.py`, `village_431_sourceqa.py`, `village_431_report.py`. Existing greybox helpers are read/imported safely, not rebuilt or changed.

Unreal preview: `/Game/Art/Candidates/Village/L_VillagePreview`.
The actual surviving mesh assembly is **`/Game/Art/Candidates/Village/Round1/Revision04/`**.
Its materials and textures are in **`/Game/Art/Candidates/Village/Round1/Revision02/`**; do not take the highest numbered folder as a standalone dependency closure.
The editor remains running. No production level edits, commit, push or PR.

## Architecture and changes

The original design follows the written modern Dutch/Belgian/Rhineland brief: two brick storeys and an attic, gable facing +X, 11 × 9 m wall footprint, eaves at 5.8 m and structural ridge at 9.6 m. Walls extend to −0.5 m. Overhangs, ridge caps, a brick chimney with a recessed flue, white verge boards, U-profile PVC gutters, connected downpipes, a dark plinth and a modern recessed door/step complete the envelope. The chimney cap reaches 10.48 m; the exported bounds also include the roof overhangs and front step. No terrain enters the reusable FBXs.

Windows are real cuts in a 30 cm shell. Frames, mullions, sills and reveal returns remain geometry. Inset glazing has a separate slot, with closed dark room boxes behind it. The game mesh is deliberately the architecture model at this gate; there is no claimed dense-master bake yet.

Damage removes roughly 4.35 m of one roof slope, including a break in the ridge, with stepped lower edges, exposed rafters and an attic floor. A 3.3 m breach removes most of the centre bay of the long wall facing Unreal −Y, with broken brick teeth, blown glazing, scorch and debris. The other slope survives. Rubble keeps low perimeter stumps (under 3 m), three fallen roof slabs, beams and a structural heap. Both the hole and the reduction to rubble remain visible at 150 m.

Captures were made before the finishing pass. Initial diagnostics exposed a 100× import-scale error; switching from FBX unit metadata to `FBX_SCALE_NONE` fixed it. Unreal now reports full centimetre bounds at actor scale 1. The damaged close camera was turned to the breached slope, the Abrams turret was raised to its data asset's recorded offset, template billboards were hidden, and the final cameras were recaptured. Earlier diagnostic folders are **not** acceptance evidence.

## Counts and bindings

All three measurements agree: source, independently reimported FBX in Blender, and native Unreal import.
Budgets are judged on the higher count. No Nanite.

| State | LOD0 source / FBX / UE | LOD1 source / FBX / UE | UE sections LOD0 / LOD1 | Imported collision hulls |
|---|---:|---:|---:|---:|
| Pristine | 4,884 / 4,884 / 4,884 | 2,800 / 2,800 / 2,800 | 6 / 6 | 2 |
| Damaged | 5,124 / 5,124 / 5,124 | 2,799 / 2,799 / 2,799 | 6 / 6 | 2 |
| Rubble | 1,128 / 1,128 / 1,128 | 1,128 / 1,128 / 1,128 | 4 / 4 | 2 |

Portable slot order: **Brick, Roof, Trim, Glass, Wood, Rubble**. Unreal strips the rubble's unused Trim/Glass slots, leaving **Brick, Roof, Wood, Rubble**, identically for its two separate imports. Bind by slot name. Damaged Brick/Roof use `MI_Brick_Damaged` / `MI_Roof_Damaged`; all other slot instances are `MI_<slot>`.

Trim and Glass use the current shared `M_Bld_Surface` / `M_Bld_Glass` directly. The scanned surfaces use `M_Village_Scanned`, a candidate copy of the current shared Surface parent, extended because that parent has no scanned albedo/packed-ORM inputs. The extension connects BaseColor, Normal and ORM (R=AO, G=roughness, B=metallic), plus a tint and actor-relative scorch sphere. It changes no shared parent. Scorch uses an instance parameter rather than a seventh material or baked maps. The shader recipe is in `village_431_preview.py` (`materials`, `scorch`); persisted parent/texture overrides are in `unreal_verified.json`.

The two UCX boxes per LOD0 preserve coarse footprint collision, including rubble. They are not navigation or garrison collision acceptance. First-pass LOD1 FBXs are separate portable meshes; **the Unreal review actors use LOD0 at every distance**. Final hand-tuned LOD1, attachment at 260 m and switch comparisons belong to Round 2.

## Textures and provenance

| CC0 surface | Use | Delivered files | Size |
|---|---|---|---|
| ambientCG Bricks085 | Walls, chimney, brick fragments | `Bricks085_BaseColor.png`, `_Normal.png`, `_ORM.png` | Each 2048² |
| ambientCG RoofingTiles006 | Pantile slopes, ridge, fallen slabs | `RoofingTiles006_BaseColor.png`, `_Normal.png`, `_ORM.png` | Each 2048² |
| ambientCG Planks023A | Dark backing, rafters and beams | `Planks023A_BaseColor.png`, `_Normal.png`, `_ORM.png` | Each 2048² |
| ambientCG Concrete036 | Plinth, step, broken structural debris | `Concrete036_BaseColor.png`, `_Normal.png`, `_ORM.png` | Each 2048² |

Copied byte-for-byte from `Saved/Surfaces/<Name>/`, the project's CC0 preparation identified in `Tools/Editor/free_assets.json`. Source/copy hashes and dimensions are in the manifest. No ignored marketplace or local reference textures are delivered. BaseColor uses sRGB/default compression, Normal uses linear/normal-map compression, ORM uses linear/masks compression. Mips and texture streaming are preserved; native settings are recorded in the verification receipt. The Abrams is preview-only local reference art, not part of the delivery.

## Openings contract

No opening moves or additions. These are the unchanged **Unreal firing positions in cm**, not the wall plane; the wall is 50 cm farther out. Entry remains `(850, 0, 0)`. Names/yaws/arcs and source aperture dimensions are also in the manifest/source QA.

| Opening | Existing firing position | Round 1 |
|---|---|---|
| DoorPX00 | (500, 0, 150) | Door aperture retained |
| WindowPX01 | (500, 150, 150) | Retained |
| WindowPX02 | (500, −150, 150) | Retained |
| WindowPX03 | (500, 150, 450) | Retained |
| WindowPX04 | (500, −150, 450) | Retained |
| WindowNX05 | (−500, 150, 150) | Retained |
| WindowNX06 | (−500, −150, 150) | Retained |
| WindowNX07 | (−500, 150, 450) | Retained |
| WindowNX08 | (−500, −150, 450) | Retained |
| WindowPY09 | (−300, −400, 150) | Retained |
| WindowPY10 | (0, −400, 150) | Pristine retained; damaged aperture expands into breach |
| WindowPY11 | (300, −400, 150) | Retained |
| WindowPY12 | (−300, −400, 450) | Retained |
| WindowPY13 | (0, −400, 450) | Pristine retained; damaged aperture expands into breach |
| WindowPY14 | (300, −400, 450) | Retained |
| WindowNY15 | (−300, 400, 150) | Retained |
| WindowNY16 | (0, 400, 150) | Retained |
| WindowNY17 | (300, 400, 150) | Retained |
| WindowNY18 | (−300, 400, 450) | Retained |
| WindowNY19 | (0, 400, 450) | Retained |
| WindowNY20 | (300, 400, 450) | Retained |
| WindowPX21 | (500, 0, 750) | Attic aperture retained; inherited height mismatch below |
| WindowNX22 | (−500, 0, 750) | Attic aperture retained; inherited height mismatch below |

All 23 apertures in both standing states passed rays through their wall plane, sampled off the mullion. This checks actual cuts rather than painted glass. Rubble is the destroyed cover state, not a usable garrison shell.

## Final Unreal evidence

Each cell links an unmodified `EditorAppToolset.CaptureViewport` image. Camera JSON sits beside every PNG. Native captures are 1487 × 910 with 16:9 camera content letterboxed inside; horizontal FOV is 60°. RTS arm lengths are exact, measured to the ground target. Pitch is −35° at 12 m and −55° at 150/450 m; yaw 145°. Three-quarter views use 29 m and −24°, aimed 4 m above ground between house and tank. Lighting is the candidate's neutral afternoon sun, pitch −42° / yaw 135°. These are **editor preview views, not gameplay**.

| State | 12 m close zoom | 150 m primary | 450 m far | Three-quarter |
|---|---|---|---|---|
| Pristine | [PNG](round1/evidence/final/pristine_12m.png) | [PNG](round1/evidence/final/pristine_150m.png) | [PNG](round1/evidence/final/pristine_450m.png) | [PNG](round1/evidence/final/pristine_threequarter.png) |
| Damaged | [PNG](round1/evidence/final/damaged_12m.png) | [PNG](round1/evidence/final/damaged_150m.png) | [PNG](round1/evidence/final/damaged_450m.png) | [PNG](round1/evidence/final/damaged_threequarter.png) |
| Rubble | [PNG](round1/evidence/final/rubble_12m.png) | [PNG](round1/evidence/final/rubble_150m.png) | [PNG](round1/evidence/final/rubble_450m.png) | [PNG](round1/evidence/final/rubble_threequarter.png) |

At an exact 12 m ground-target zoom this 10 m-tall house exceeds the frame: those views are close detail, not a claim to fit the entire building and Abrams. The three-quarter views supply that whole-asset scale comparison. At 150/450 m the three states and tanks are visible together, with the named state at the camera's target.

## Lessons check and remaining work

Checked against `references/modelling-lessons.md` before delivery: substantial trim/reveals remain geometry; room backing/returns/caps close unintended gaps; no AO bake on floors; exported LOD0 triangles have valid finite unit tangents and a minimum altitude about 2 cm; all six FBXs have zero degenerate geometry/UV triangles; source/FBX/UE counts are reported; current shared parents are used or the necessary candidate extension is identified; only portable CC0 PNGs ship; only the authorised Village preview was created/saved; caches were not directed into Art; first captures preceded the refinement and final recapture.

The meshes, named materials and parents were saved and reloaded in the same live editor. Rubble's two convex hulls were inspected. No HouseA tangent/degenerate import warning was found. The recorded log includes corrected tool-call diagnostics (attempted reimport of an existing asset, the first incorrect SphereMask property name, and an unsupported texture-property query); these are not surviving mesh/shader errors. A duplicate triangle introduced by first-pass LOD reduction was removed before the validated final export.

Open items for review / Round 2:

1. Approve or revise architecture. The breach/roof edges and rubble are still deliberately coarse; broken brick variation, joints and weathering need the quality pass. No visual approval exists yet.
2. The inherited attic aperture is at sill 6.0 m / head 7.1 m while its greybox firing eye is 7.5 m. This round preserves the original aperture and firing coordinates; reconcile that mismatch explicitly before gameplay intake rather than silently moving it.
3. First-pass decimated LOD1 is within budget but has not earned silhouette/switch acceptance. Dense master, high-to-low bake, master/game comparisons and ±4% around 260 m are deferred to Round 2 by the issue's round-specific brief.
4. Coarse UCX hulls retain collision but do not prove projectile, selection, navigation, squad firing, placement, slope or runtime state-swap behaviour. Claude handles integration after art acceptance.
5. No editor restart or packaged/runtime benchmark was performed; the shared editor was left running. Older diagnostic imports are retained, not cleaned or force-deleted.

## Safe continuation

`village_431_housea.py --output-root <issue431 subfolder>` rebuilds only its named portable outputs in a fresh background Blender process. It is a full Round 1 recipe, not a safe way to preserve later hand edits. `village_431_sourceqa.py` validates and relinks the existing Round 1 source without rebuilding geometry. `village_431_report.py` validates receipts and rebuilds the gallery/manifest. Preview `imports` intentionally refuses existing asset names; use a new candidate revision for a later import and rebind by name. Do not replay `prepare` or `presentation` on a different/shared level. The preview helper checks the exact Village level before editing.

New-chat starter: Use `$world-divide-building-production` for River Crossing village #431. Read `Art/Shared/village/issue431/STATE.md` and `ROUND_1.md`. Round 1 is ready for architecture review, with no visual approval yet. Preserve Revision04 meshes and Revision02 material dependencies. Incorporate the producer/Claude review before starting Round 2. Do not commit or push; leave the editor running.
