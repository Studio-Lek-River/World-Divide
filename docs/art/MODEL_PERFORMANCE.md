# Model performance: budgets and LODs

Read this before you model a building or a unit for World Divide. A delivery over these budgets is not accepted.

The goal is the one of Command & Conquer: Generals – Zero Hour and Tempest Rising: a building looks the same the whole
time it is on screen. The player never sees detail pop in or out, and never sees a blurry texture sharpen. We get there
by authoring each asset for the RTS camera, cheap enough to draw at full detail everywhere, not by stacking LODs.

## The camera you author for

- The RTS camera's arm runs from 12 m (closest zoom) to 450 m (farthest), and a match starts at 150 m
  (`Config/DefaultGame.ini`, `[/Script/WorldDivide.RTSCameraSettings]`).
- The lens is 60 degrees across at 16:9 (`Tools/Editor/vehicle_art.py`, `CAMERA_FOV`).
- Most play happens at 100 to 200 m. The 12 m zoom is for a close look, not for normal play.

## Buildings

1. **Author for the RTS camera, not for close-ups.**
   - LOD0 is the game mesh. It is drawn at full detail from 12 m out to 260 m.
   - Detail smaller than about 10 cm (bolts, seams, panel lines, small bevels) lives in the normal and AO maps, not in the
     geometry. No microscopic bevels.
   - The dense visual-review master stays in the source folder only. It is never the game mesh.
2. **Two LODs: LOD0 and LOD1, no Nanite.**
   - Building shells do not use Nanite: at 240 units a Nanite HQ cost more than twice an HQ with LODs, and its fallback
     breaks up without Nanite (#349, `docs/tech/scale/PERFORMANCE_RESULTS.md`).
   - LOD1 switches in at 260 m, the same far distance the vehicles use (`vehicle_art.LOD_DISTANCES`), where a building is
     small on screen.
   - The whole building switches at once: one switch distance measured from the building's bounds, not from each mesh's
     own, so its pieces never switch one after another.
   - LOD1 keeps the exact silhouette and uses the same material slots, in the same order, and the same textures, so the
     switch cannot be seen.
   - Buildings have no LOD2 or LOD3.
3. **Budgets for the whole building:**

   | Building | LOD0 | LOD1 | Material slots |
   |---|---:|---:|---:|
   | Large (HQ, factory, airfield) | ≤ 150,000 | ≤ 40,000 | ≤ 8 |
   | Small (power plant, barracks, defences) | ≤ 25,000 | ≤ 6,000 | ≤ 8 |

   - The large budget is the one #349 benched with the USA HQ: about 0.2 ms of GPU per HQ among 240 units.
   - The USA HQ has LOD0 ≤ 350,000, raised by the producer (rule 7): its delivered façade is a dense master without a
     baked game mesh, and at 150,000 its bevelled frames tore apart at 12 m (#358). A new delivery keeps to the table.
   - Share the budget out per mesh by what reads at the RTS camera, not by triangle count. Small details that show at
     150 m (solar cells, grilles, door lights, planters) keep their own triangles; a flat cut lost the HQ's solar cells
     at LOD0.
   - Each moving part (door, radar, dish, fan, flag) is at most 10,000 triangles at LOD0. Its triangles count towards the
     building's budget.
   - Far fewer material sections than the current HQ (172): each slot is one section per mesh where possible.
   - Glass is its own mesh, or at least its own slot, so it can be drawn translucent or opaque without touching the rest.
   - Every material slot becomes an instance of a project master material in setup (`M_Vehicle` for vehicles). Name the
     slots after their surface (concrete, metal, glass) so Claude can map them.
4. **Nothing vanishes or appears.** Between LOD0 and LOD1 no part is removed. Antennas, railings, ladders, flags and
   grilles get a simpler mesh with the same outline. Build it by hand where the engine's reduction would shrink it into
   gaps (anything made of many small separate pieces), and deliver it as `<mesh>_LOD1.fbx` next to the LOD0 FBX.
5. **Few pieces.**
   - All static geometry is merged into one mesh, or into as few as the material limit allows.
   - Only moving parts are separate meshes, each with its own named pivot.
   - Small parts cast no shadow. No Nanite (rule 2).
   - Collision is Claude's. Do not deliver collision meshes beyond what the building's handoff asks for.
6. **Textures that never pop.**
   - At most 2K (2048 × 2048) per texture set. 4K only with the producer's approval.
   - Keep the mipmaps. Claude imports building and unit textures as non-streamed, always in memory, so the faction set
     has to fit: this limit is what makes that possible.
7. **The look must not change.**
   - The game mesh is made from the approved master: its fine detail is baked into the normal and AO maps (high-to-low
     bake), not deleted.
   - Acceptance is a side-by-side of the master and the game mesh at the 12 m, 150 m and 450 m RTS views.
   - If the 12 m view visibly loses detail, ask the producer to raise that building's budget. Do not accept a worse look.
8. **Evidence in every delivery:**
   - The manifest lists LOD0 and LOD1 triangles, material sections and texture sizes, counted from the re-imported FBX.
   - Captures in the candidate preview level at the 12 m, 150 m and 450 m RTS views, and at 4 % inside and outside the
     260 m switch. The pair at the switch must look identical.

## Vehicles

Vehicles follow the M20 Guardian (`Art/USA/vehicles/m20_guardian/report_ue5.md`, "LODs (#149)"):

- LOD0 to LOD3, switching at 35, 110 and 260 m from the camera for every vehicle, so mixed armies switch alike. The
  captures of #149 show these switches cannot be told apart at the RTS camera.
- Budgets per vehicle, not per part: LOD0 ≤ 90,000, LOD1 ≤ 30,000, LOD2 ≤ 8,000, LOD3 ≤ 2,500 triangles.
- Claude makes the reduced LODs in setup with the engine's reduction (`vehicle_art.build_lods`). Deliver a clean LOD0,
  and hand-built proxies for parts made of many separate pieces (tracks, links, chains), as
  `m20_guardian_blender.py --lods` does for the tracks.
- Rules 1, 4, 5 and 6 above apply to vehicles too.
