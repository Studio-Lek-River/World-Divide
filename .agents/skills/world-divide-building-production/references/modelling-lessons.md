# Modelling lessons for Codex rounds

Mistakes that cost whole Codex rounds, distilled after each round by Claude's review. Read this before every modelling
round and check your delivery against it before you report. New lessons are appended after each round; the issue that
taught each one is in brackets.

## Geometry and baking

| Mistake | Do instead |
| --- | --- |
| Façade members proud or recessed by more than about 10 cm (pilasters, frames, reveals, sills, coping) were flattened into the normal map, so the façade read as a floating texture. [#373] | Keep them geometry. A normal map carries detail under about 10 cm only. |
| Pieces were removed from the game mesh by size (< 20 cm), and visibility was tested by face centre, which dropped large panels whose edges showed. [#373] | Decide by visibility at the intended views (12 m and RTS), tested over the whole face, not by size. |
| Missing backing and return faces let the ground show through as green strips; a cap was missing. [#373] | Close every opening that is not meant to be seen through: backing behind glass, return faces in reveals, caps on boxes. |
| AO baked onto large flat floors made blotches. [#373] | Large flat floors and paving bypass the AO bake. |
| Invalid tangent frames on millimetre-thin edge faces could not be repaired late. [#373] | No faces narrower than about 1 cm in the game mesh; validate tangents on every export, not at the end. |
| Unreal dropped triangles against the Blender FBX count. [#373] | Report both counts; budgets are judged on the higher one. |

| Tiling textures were applied at twice their real scale (brick courses of 13 cm instead of 6.5 cm). [#431] | Check the texel scale against a real-world unit (a brick course, a pantile, a plank) at the 12 m view before the first capture. |
| Damage cut as clean rectangles, debris in one pale material. [#431] | Blast outlines are irregular; debris mixes the building's own materials (brick, tile, timber, plaster dust) and is darker than the intact walls. |
| Rubble walls modelled as thin panels. [#431] | Broken walls keep their full thickness and show it at the break. |
| Rectangular roof holes came back on new types after HouseA was fixed. [#431] | Apply every approved fix of the quality-bar asset to each new type, and check the list in this file per type, not once per round. |
| The set read near-black at 150 m: one roof colour for most types; a damaged capture was shot against the sun. [#431] | Judge a set together in one 150 m overview before refining single types; capture each state from the lit side the player sees first. |
| An FBX imported at 1/100 scale through FBX unit metadata. [#431] | Import with `FBX_SCALE_NONE` and check the bounds in cm right after import. |

## Materials and Unreal

| Mistake | Do instead |
| --- | --- |
| Candidate material copies went stale against the current shared parents (`M_Bld_*`), skewing comparisons. [#373] | Use the current shared parents directly in the candidate; copy only when a change to the parent is required, and say so. |
| Git-ignored pack content (`/Game/Surfaces`, `/Game/PolyHaven`, marketplace packs) cannot ship. [#431] | Preview with it if useful, but the delivery's textures are PNGs under the building's `Art/` folder from a CC0 or in-house source. |
| The MCP refused to save a level without producer approval; a command-launched editor stuck on modal dialogs; recompiling a visible material instance crashed D3D12. [#373] | Use the candidate preview level Claude prepared; never create or duplicate levels over MCP. Close material editors before recompiles. |
| "Import LOD Level 1" in the mesh editor opened a native file dialog that blocked the game thread and the MCP; only the producer could cancel it. [#431] | Never trigger editor UI that opens a native dialog. Candidates keep LOD1 as a separate mesh; compare it as a separate actor at the switch distances. Claude's setup attaches LOD1 at 260 m in production. |
| Editor DDC and shader caches were written into `Art/`. [#373] | Keep caches in `Saved/` or `DerivedDataCache/`; `Art/` holds only deliverables and their sources. |

## Process

| Mistake | Do instead |
| --- | --- |
| A round ended without the required captures, so it could not be reviewed. [#373] | Capture the fixed cameras first with whatever exists, then refine; a round without the captures is not done. |
| A time-limit stop left the portable files on an unvalidated edit. [#373] | Validate before every export; keep the last validated export alongside any work in progress. |
