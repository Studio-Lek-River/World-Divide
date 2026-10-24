# #446 destroyed-bridge rebar — current state

- **Scope:** `SM_Bridge_Span_Destroyed` only, its LOD1, master and atlas. [BRIEF.md](BRIEF.md) is the contract; this file
  holds each round's target and the review of the last one.
- **Rounds:** 1–3 on GPT-6.1-Sol (iterate), 4 on GPT-6-Astra (realism and the final handoff). Rounds 1–3 work in
  `Art/Shared/village/issue446/round<N>/` (exports, blend, captures, `REPORT.md`) and import candidates under
  `/Game/Art/Candidates/Village/Rebar446/R<N>/`. They do **not** write `Art/Shared/village/final/`; round 4 writes the
  accepted result there as the brief's Deliver section says.
- **Baseline ("before"):** the accepted #431 files in `Art/Shared/village/final/` (game mesh 984 / LOD1 984 / master
  2,760 triangles) and their captures in `final/evidence/final/SM_Bridge_Span_Destroyed_*`. Candidates of #431:
  `/Game/Art/Candidates/Village/Final/Meshes02/` and `Masters02/`. Do not modify either.
- **Editor:** running, MCP at http://127.0.0.1:8000/mcp. Use the existing preview level
  `/Game/Art/Candidates/Village/L_VillagePreview`; never create levels, never close the editor, no native dialogs.

## Facts the rebar has to work with

- In River Crossing the deck top is 6 m above the water (deck top z = 0 in the mesh, water surface at **z = -600 cm**),
  the same as the #431 preview. The two halves drop 36° and their broken tips reach z ≈ -944 cm, so **the broken ends
  are under water**. Rebar placed only at the tips is invisible or shows as a few dots on the water: that is most of
  why the round-4 rebar does not read at 150 m (see `final/evidence/final/SM_Bridge_Span_Destroyed_12m.png`, where six
  short stubs poke out of the water in a row).
- The 150 m camera: pitch -55°, 60° FOV, arm 150 m to (0, 0, 600 cm), sun (-42, 135, 0); see
  `final/evidence/final/SM_Bridge_Span_Destroyed_150m.json`. Seen from there a 5 cm bar is under a pixel wide; a bar
  reads through length, grouping, contrast against the pale concrete and the water, and its shadow.

## Round 1 target (GPT-6.1-Sol)

Make the reinforcement read at 150 m while the geometry around the breaks stays as approved:

1. Put the visible rebar **above the waterline**: bars that run out of the slab where it crosses z = -600 cm and above,
   bars torn out along the exposed side faces of the dropped slabs, and bars sticking up out of the water from the
   submerged tips, bent in the direction the slab fell. Real broken slabs show a mat of bars peeled away from the
   concrete, not single pins.
2. Exaggerate for the RTS view within the brief: bars about 4–6 cm, 0.8–1.5 m exposed, in fans of 6–12, bent, with a
   slightly lighter rust edge so they separate from the dark water. Keep them on `Trim` with the brief's tint.
3. Stay within +600 triangles on LOD0 and on LOD1; LOD1 may merge a fan into a few cards or prisms if it keeps the
   silhouette at 260 m.
4. Capture first, then refine: before/after at 150 m from the fixed camera, plus 12 m, 450 m and the 249.6/270.4 m
   pair, all into `round1/`. Report counts, paths and what is left open in `round1/REPORT.md`.

## Review log

### Round 1 (Claude, 2026-10-24) — not readable yet

Process was good: captures first, counts agree, protected files intact, collision and slots kept. The visual target was
missed:

- **150 m:** the after image is indistinguishable from the before. Nothing new reads; 6 cm bars are a third of a pixel.
- **12 m:** the nine-bar mats read as a row of identical croquet hoops / staples standing on the slab, and the side-face
  fans read as a ladder or a railing. Uniform spacing, identical shape and a pale tan rust make them look manufactured,
  not torn. They also sit *on* the slab face rather than coming *out of* broken concrete.
- **Budget:** 588 of 600 triangles spent on 42 closed triangular prisms (14 triangles a bar). That geometry cannot get
  thicker or longer within the ceiling, so round 2 has to spend the triangles differently.

## Round 2 target (GPT-6.1-Sol, resume)

Rebuild the added reinforcement (keep the inherited submerged stubs and everything else). Claude widens the brief's
exaggeration for the RTS read:

1. **Size:** bars 10–14 cm across and 1.5–2.5 m exposed. At 150 m that is about a pixel wide and 8–12 px long, which is
   the minimum that reads as a line. Check the 12 m view still reads as heavy rebar, not as pipes.
2. **Cheaper bars:** spend the triangles on length and count, not on round cross-sections. Use open, double-sided
   ribbons or crossed two-quad strips (2–4 triangles per segment), oriented so their wide face looks up towards the
   -55° camera; 3–4 bent segments a bar. Rebuild from the accepted mesh so all 600 triangles are available again.
3. **Where:** bristle **out over the water** along the line where each dropped slab enters the water (z ≈ -589/-600),
   and along the top of each slab's exposed side faces near that line. Bars leave the concrete, splay outward and
   droop into the water, so most of their length is silhouetted against the water, not lying on the slab.
4. **Torn, not made:** irregular spacing, varied lengths and bend angles, some crossing each other, a few bent back on
   themselves, two or three short transverse bars tangled across them. No two adjacent bars alike; no hoops, ladders or
   railings.
5. **Contrast:** dark rust-brown to near-black steel against the water and the pale concrete (keep the `Trim` tint; drop
   the light tan edge, or keep it to a thin highlight). Where a bar leaves the slab, a small dark spall or stain on the
   concrete atlas around its root helps the root read.
6. LOD1: the same bars may merge into fewer, wider ribbons; keep the silhouette at 270 m.
7. Judge the 150 m before/after **at full size, cropped 2× around each slab end**, and include those crops in
   `round2/`. If it still does not read, say so plainly and propose what would.

### Round 2 (Claude, 2026-10-24) — reads at 150 m; close-up needs work

- **150 m: pass.** Both slab ends now carry a dark, irregular fringe over the water; the 2× crops show distinct bars and
  shadows. Keep this density, length and splay; round 3 must not lose it.
- **12 m:** the bars read as flat cardboard strips, pale grey-tan rather than dark rusted steel, and several groups form
  letter-like glyphs (K, M, V, zigzags) because their bends lie in the horizontal plane.
- **Roots:** the bars leave a perfectly clean, straight slab edge at the waterline; nothing says the concrete broke there.
  The root stains are not visible.

## Round 3 target (GPT-6.1-Sol, resume)

Keep the round-2 layout and the 150 m silhouette; refine the close read within the same triangle budget:

1. **Profile:** replace the flat double-sided strip (4 triangles a segment) with a single-sided V or inverted-V angle
   profile, apex up (also 4 triangles a segment), so the top-down camera sees a lit and a shaded facet: a rounded bar
   with a highlight ridge instead of a plank. The camera is never below the bars, so backfaces are not needed.
2. **Bends:** bars droop mainly in their vertical plane (out, then down into the water) with only a mild sideways splay,
   so from above they radiate as lines. Keep at most three or four hooked or crossed bars per end and no in-plane
   zigzags; nothing may read as a letter.
3. **Colour:** the bars must read dark rust-brown to near black at 12 m as well as at 150 m. Check the atlas texels the
   new bars sample and the bake; a thin warm highlight on the ridge is fine.
4. **Broken root line:** along the line where each slab enters the water, paint into this mesh's atlas a 30–60 cm ragged
   band of spalled, darker, rougher concrete (Tint, BakeNormal and BakeAO) with the bars emerging from it, so the line
   reads as a fracture, not a clean edge. No new concrete geometry unless the spare triangles allow a few chunks.
5. Same counts limit (+600 per game LOD), same captures plus the 2× crops and 12 m before/after against round 2.

### Round 3 (Claude, 2026-10-24) — 150 m held; close-up much better

- **150 m: pass, held.** Same fringe density and reach as round 2 in both 2× crops.
- **12 m:** ridged profile and dark steel work; the bars now read as bent steel, not cards. The fracture band is there.
- Remaining: (a) a few neighbouring bars still meet at their tips and form Λ / K / N shapes; (b) the fracture band reads
  as an even dotted spray stencil of constant width along a ruler-straight line, not as broken concrete; (c) on some
  bars the lit facet goes pale grey, which makes those bars read as wooden sticks.

## Round 4 target (GPT-6-Astra, resume) — realism and final handoff

1. **Separation:** no two bars meet or touch at their tips; crossings only where a transverse bar lies across others.
   Nudge tips apart within the existing layout; keep the 150 m fringe.
2. **Fracture:** make the root band read as broken concrete: varied width (20–70 cm), a ragged outline with bigger
   angular chips and a few deeper spalls where bar groups leave, darker and rougher than the slab, and a thin darker wet
   line just above the water. No regular dot pattern. The 20 spare triangles may go to two or three small concrete
   lumps still clinging to bars.
3. **Steel:** keep the lit facet in dark rust-brown; no pale grey highlights. Slightly vary rust between bars.
4. **Deliver** as `BRIEF.md` says into `Art/Shared/village/final/` (fbx, LOD1, master, `Bridge_baked.blend`, the three
   atlas PNGs, `hashes.json`, the mesh's `manifest.json` entries), with the round's captures and the final before
   (accepted #431) / after pair at 150 m plus crops and 12 m in `round4/`, and `round4/REPORT.md`.

### Round 4 (Claude, 2026-10-24) — delivered to final, awaiting producer approval

- **150 m: pass, held**; tips separated, no glyphs. **12 m:** dark rusted bars drooping from a ragged fracture band with a
  wet line at the water. Counts 1,564 / 1,564 / 3,340, collision and slots unchanged, nine `final/` files replaced and the
  other 614 byte-identical. Claude copied the two FBXs to `Art/Shared/village/`, updated `village.json` (triangles,
  bounds) and reran setup for #446. Producer approval of the 150 m read is the remaining acceptance item.
