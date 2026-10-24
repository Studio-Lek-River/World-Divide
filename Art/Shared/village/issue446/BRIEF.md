# #446 rebar polish: SM_Bridge_Span_Destroyed

Codex rounds on one accepted asset: three on GPT-6.1-Sol, then one realism round on GPT-6-Astra (`STATE.md` holds each round's target). The producer approved the #431 set on 2026-10-24 ("Approve the set")
with this note, carried from #432 to #446: the destroyed bridge's exposed rebar is too small to read at the 150 m RTS view.

Use `.agents/skills/world-divide-building-production/SKILL.md` and check the delivery against
`references/modelling-lessons.md` before you report. Do not change any other mesh, state, material value or the
approved geometry around the breaks.

## Target

At the 150 m view (pitch -55°, 60° FOV, `final/evidence/final/SM_Bridge_Span_Destroyed_150m.json`) the reinforcement
at both broken ends reads as a bristle of bent bars against the concrete and the water, not as a few dark pixels.
Real-world bars are 16-25 mm; a game asset may exaggerate them for the RTS view: thicker bars (about 4-6 cm), longer
exposed lengths (0.8-1.5 m past the break), grouped in visible fans, bent downwards along the 36° slab drop.

## Keep

- Origin at deck-top centre, +X along the span, Z up, the same as the damaged state and the intact span.
- Slots `Brick`, `Roof`, `Trim` (rebar), `Rubble`, in that order; the rebar stays on `Trim` with the dark steel/rust
  SurfaceTint (0.10, 0.075, 0.055).
- Exactly two UCX pier-stub convex hulls; collision unchanged.
- LOD0 and the separate LOD1 within a combined increase of at most 600 triangles each; LOD1 keeps the same slots and
  the silhouette at 260 m.
- Rebake that mesh's own atlas (`SM_Bridge_Span_Destroyed_Tint`, `_BakeNormal`, `_BakeAO`) from its master as round 4
  did; no other atlas changes.

## Deliver

Replace in `Art/Shared/village/final/`: `fbx/SM_Bridge_Span_Destroyed.fbx`, `fbx/SM_Bridge_Span_Destroyed_LOD1.fbx`,
`source/SM_Bridge_Span_Destroyed_MASTER.fbx`, `source/Bridge_baked.blend`, the three atlas PNGs, and update
`hashes.json` and the mesh's `manifest.json` entries (triangles, bounds). Captures at 12, 150 and 450 m and the
249.6/270.4 m pair of the new state, game against master, and a before/after pair at 150 m, saved to
`Art/Shared/village/issue446/round<N>/`. Candidate Unreal assets only under `/Game/Art/Candidates/Village/`.

Do not touch `Art/Shared/village/*.fbx`, `village.json`, production assets under `/Game/Art/Shared/` or setup scripts;
Claude copies the delivery in and reruns setup. No native dialogs, no commits, no pushes, never close the editor.
Report in `Art/Shared/village/issue446/round<N>/REPORT.md`: counts, paths, hashes, captures and anything left open.
