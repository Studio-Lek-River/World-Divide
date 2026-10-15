# WD_RiverCrossing_01: implementation roadmap

Each phase ends with a validation gate. A phase starts only after the gates of the phases it depends on have passed. Items marked **[P]** need producer input (see [05_PRODUCER_ACTIONS.md](05_PRODUCER_ACTIONS.md)).

## Phase 0: real-scale gameplay migration (separate issue, before the map)

**Depends on:** the producer's real-scale decision (taken 2026-10-15) and the target values **[P]**.

**Tasks**
- Set the unit fit sizes, `SelectionRadius`, `MoveSpeed`, weapon ranges and climb/air values to real scale in `Tools/Editor/setup_testmap.py`. Starting point: the planning envelope in [02_MAP_DESIGN.md](02_MAP_DESIGN.md).
- Set building fit sizes and footprints to real scale. Decide whether to keep the 1 m `GridSize` or change it.
- Scale the camera: `MinZoom`, `MaxZoom`, `DefaultZoom`, `ZoomStep`, `PanSpeed`, `TiltStartZoom`, `MinCameraHeight`, `EdgeMargin`, `FallbackMapArea`.
- Scale the AI distances (`BuildSearchDistance`, `DepotSourceDistance`, `ExitLaneLength`, ring step and spacing) and the test radii (`Smoke.AIBuildsBase` `BaseRadius`).
- Add two nav agents (infantry and vehicle) and make units select theirs.
- Scale the existing test and lobby maps (`half_size`, spots, obstacles) so all tests keep their meaning.

**Validation**
- The full `ue.ps1 -Task test` run passes.
- `L_SkirmishDuel` plays at real scale with an AI opponent.
- A tank path visibly respects the vehicle radius.

## Phase 1: asset acquisition and import

**Depends on:** the pack selection **[P]**. It can run in parallel with phase 0.

**Tasks**
- The producer adds the chosen Fab packs into the open editor.
- Move each pack to `/Game/World/Packs/<Pack>/`, or keep the pack's own root if moving it breaks its references.
- Check per asset: scale (1 uu = 1 cm), Nanite or LODs, collision, material count, texture sizes.
- Record the verified asset list in [01_ASSET_AUDIT.md](01_ASSET_AUDIT.md) (an "imported" section), with Asset Registry paths taken through MCP.
- Confirm the HighPoly tree's licence and decide whether to commit it **[P]**.
- Set up the LFS tracking and locking flow for the pack folders.

**Validation**
- Every category A–F in 01 has at least the minimum set.
- One test level (not the production map) shows the surfaces, trees and one building next to an Abrams at real scale.
- That level has no missing references.

## Phase 2: greybox landscape and blockout

**Depends on:** phase 0 (the scale numbers). The Water and PCG plugins must be enabled **[P]**.

**Tasks**
- Create `/Game/Maps/WD_RiverCrossing_01` as a non-World-Partition level with One File Per Actor turned on.
- Create the landscape: 2017², scale 125, edit layers.
- Sculpt the base pads, hills, river trench, village rise and border ring.
- Place the `PlayerStart`s (`Team0`, `Team1`), one `NavMeshBoundsVolume` (2,000 m), the supply sources, the `Water_*` nav blockers and blockout cubes for the village and bridges.
- Add `Tools/Editor/setup_rivercrossing.py`: map data asset, preview, navmesh build.
- Register `DA_MapRiverCrossing01` in `+SkirmishMaps` **[P: lobby list]**.
- Add the test `WorldDivide.Maps.RiverCrossing`.

**Validation**
- The balance checklist in 02 is measured and recorded.
- `Maps.*`, `Smoke.Maps` and `Maps.RiverCrossing` pass.
- An AI match runs from start to finish on the greybox.
- The navmesh builds in a recorded time.

## Phase 3: river, bridges and roads

**Depends on:** phase 2.

**Tasks**
- Water River body on the final spline.
- Riverbed carving.
- Shoreline materials.
- Bridge meshes with WorldStatic deck collision.
- Landscape spline roads and the RVT.

**Validation**
- Water is not walkable anywhere (test).
- Both bridge decks have navmesh, with a lane of at least 6 m after the agent radius.
- Ground traces do not hit the water surface (test or debug draw).
- A column of 12 tanks crosses each bridge without blocking.

## Phase 4: landscape material

**Depends on:** phase 1 surfaces and phase 3 roads.

**Tasks**
- `M_Landscape_WD_Temperate` with 7 layers, slope and height automation, macro variation, RVT blending.
- Landscape Grass Types.
- Paint the fields, tracks and mud banks.

**Validation**
- Readable at the minimum and maximum zoom.
- No visible tiling at the maximum zoom.
- Shader and texture budget within §5 of 03.
- Units stay distinct against every layer, checked with screenshots.

## Phase 5: village

**Depends on:** phase 1 buildings and phase 3 roads.

**Tasks**
- Lay out both village banks to the counts and street widths in 02.
- Choose the future garrison buildings and mark them in the level, for example with an actor tag.
- Set up building collision.
- Add props (fences, lamps, signs).

**Validation**
- Path tests through both halves.
- Equal garrison capacity on both banks.
- No building blocks the 10 m main street.
- Camera screenshots from both sides.

## Phase 6: vegetation (PCG)

**Depends on:** phases 3–5 for the exclusion inputs.

**Tasks**
- PCG graphs: forest, copse, hedgerow, riverbank, border ring, clutter. Exclusions as in 03 §6.
- Hand-placed hero trees.

**Validation**
- Vehicles path through the playable woods, or are blocked only where intended **[P]**.
- Tree height limits are respected near the lanes.
- Regenerating the graphs gives the same result.
- Frame time is within the budget set in phase 8.

## Phase 7: lighting and post

**Depends on:** phase 6.

**Tasks**
- Sun, SkyLight, SkyAtmosphere, VolumetricCloud, light height fog, unbound PPV with manual exposure.
- Lumen quality settings.

**Validation**
- Afternoon look approved from reference screenshots **[P]**.
- No cinematic fog.
- Units readable in shadow.

## Phase 8: performance and readability pass

**Depends on:** phase 7.

**Tasks**
- Profile with `stat fps`, `stat unit` and `stat gpu` through MCP with 200 units fighting at the village.
- Tune foliage cull distances, Lumen, VSM and Nanite.

**Validation**
- The fps target is met on the minimum spec **[P]**.
- The full test run passes.

## Phase 9: lobby integration and sign-off

**Depends on:** phase 8.

**Tasks**
- Final preview capture.
- Lobby entry with the name and preview.
- Decide whether `L_RiverFord` stays in the lobby **[P]**.
- Write the map page in `docs/world/maps/WD_RiverCrossing_01/` (final values).

**Validation**
- `Maps.LobbyMaps`, `Maps.PreviewCameraAlignment` and `Maps.StartSpotPositions` pass.
- A human-vs-AI match on each side.
- Producer sign-off.
