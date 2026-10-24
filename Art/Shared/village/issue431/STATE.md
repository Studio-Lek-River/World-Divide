# #431 village set — current state

- **Gate:** Round4 delivered for **producer approval of the set; approval pending**. Stop here. [ROUND_4.md](ROUND_4.md), [final gallery](../final/REVIEW.html), [Claude handoff](../final/CLAUDE_IMPLEMENTATION_PROMPT.md).
- **Authority:** producer decisions → `docs/art/village/VILLAGE_BRIEF.md` → greybox contract. `BRIEF.md` defines the four rounds. Ground-centred origins; church nave centre; bridge deck-top centre. Greybox files remain untouched for #432.
- **Portable authority:** `Art/Shared/village/final/`: eight `source/<Type>_baked.blend`, 23 LOD0 +23 LOD1 FBXs, 23 source review masters, ≤2K PNGs, `manifest.json`, `hashes.json`, `village_patch.json`, `openings_handoff.json`, `collision.json`, material recipe and evidence.
- **Native authority:** `/Game/Art/Candidates/Village/Final/Meshes/`, `Masters/`, `Materials/`, `Textures/Scans/`, `Textures/Atlas/`, `Textures/Tint02/`, `Textures/AtlasBridge02/`, `M_SetSurface`; existing `/Game/Art/Candidates/Village/L_VillagePreview`. Bridge meshes/review masters use `Meshes02/` / `Masters02/` for the final bent-rebar revision. Earlier candidates/first imports retained, parked and excluded from final dependency closure. Editor left running.

## Approvals

| Stage | State |
|---|---|
| Round1 HouseA architecture | producer approved 2026-10-23: "Approve + my fixes" |
| Round2 HouseA quality bar | producer approved 2026-10-23: "Approve + carry notes"; "The producer approved HouseA (Round 2) as the quality bar for the set." |
| Round3 set | Claude reviewed 2026-10-24; critique carried into Round4 |
| Round4 realism/consolidation/handoff | **producer approved the set 2026-10-24: "Approve the set"**; two polish notes go to #432 (barn roof tiling blotches, destroyed-bridge rebar small at 150 m) |

Latest authorization: "Round 4 of #431, the final round, now on GPT-6-Astra: realism, consolidation and the Claude handoff." No inferred approval of the final visuals.

## Verified delivery and limits

All 46 game source/FBX/native counts agree and fit budgets; 69 source/review exports pass UV/tangent QA; 276 opening rays pass. Every state has final 12/150/450 m and three-quarter editor evidence, matched master/game sets and manual switch pairs. Native parents/material overrides persisted on reload; final closure contains no earlier candidate dependency. Final registry source filenames point to the final portable files. Previous round files and greybox hashes match.

No editor restart was permitted. Historical packages were archived and retained, not deleted. Same-editor reload and post-reload smoke checks are the verification limit. No native dialogs, commits, pushes or production edits. Recovery: `round4/archive/receipt.json` and `pre_round4_candidates.zip`; restore packages only with producer-coordinated editor shutdown. Never close the running editor as cleanup.

Integration: #432 replaces greybox from final files, merges `village_patch.json`, binds the serialized material recipe, attaches LOD1 at 260 m, applies destroyed-bridge two-stub `CTF_UseSimpleAsComplex`, and tests setup/gameplay/performance. Barn brief capacity 8, loft eyes 5.95 m; Workshop 3.3 m; Church 4.4 m. Detail normal/AO fade 180–230 m required for far LOD. Production state swaps, firing, collision/navigation/placement, culling and runtime performance remain untested.

## Next single milestone

The set is accepted. #432 integrates it from `final/CLAUDE_IMPLEMENTATION_PROMPT.md`, with the two polish notes above.

**Committed:** `Art/Shared/village/final/`, this folder's `BRIEF.md`, `STATE.md` and `ROUND_*.md`, and the `Tools/Art/village_431_*` generators. The `round1/`–`round4/` working folders (about 1.8 GB) and the Unreal candidates under `/Game/Art/Candidates/Village/` stay local, as with #373; links from the ROUND reports into them resolve only on the producer's machine.

Safe reproduction: capture controls are in `Tools/Art/village_431_round4_delivery.py`; authoritative paths and values are the final manifest and saved sources. Full `round4.py` and bake runs destructively rebuild round4 outputs; do not replay on an accepted delivery merely to render. Final copy/import stages are explicit, and import skips existing packages; changed geometry needs a deliberate new revision rather than silently reusing an old import.

Continuation: Use `$world-divide-building-production` for #431. Read this STATE and ROUND_4.md. Preserve approved HouseA and the final saved set. Record the producer's decision; apply only requested corrections or hand accepted files to Claude for #432. Keep the editor running, never trigger native dialogs, do not commit or push.
