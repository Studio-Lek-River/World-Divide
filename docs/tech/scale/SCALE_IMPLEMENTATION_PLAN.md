# Scale implementation plan

Status: **approved** (2026-10-15), in progress; results per step in [§7](#7-measurements-per-step). LODs and rendering optimisation are a separate follow-up phase that starts only after every step's regression tests and performance measurements are done. The plan is based on the [standard](WORLD_SCALE_STANDARD.md), the [placeholder audit](PLACEHOLDER_SCALE_AUDIT.md), [collision and selection](VEHICLE_COLLISION_AND_SELECTION.md) and [movement performance](RTS_MOVEMENT_PERFORMANCE.md).

## 1. Recommended sequence

The work is split into seven issues, each built, tested and committed on its own. **The order puts the fixes that work at any scale first**, so the data rescale (step 4) lands on systems that already handle real-size vehicles.

| # | Issue | Depends on | Size |
|---|---|---|---|
| 0 | **Profiling baseline:** Insights capture and named CPU scopes | — | S |
| 1 | **Collision and selection split:** box root, ground offset, selection bounds, HUD and marquee, footprint derivation | 0 | M |
| 2 | **Avoidance:** fixed grid, stadium footprint, per-second push clamp; plus **acquisition** rescan and spatial query | 1 | M |
| 3 | **Navigation agents:** two `SupportedAgents`, tile and cell sizes, saved navmesh rebuild | 1 | M |
| 4 | **Data rescale:** placeholders, buildings, weapons, maps, AI and gameplay constants | 1, 2, 3 | L |
| 5 | **Camera retune** | 4 | S |
| 6 | **Vehicle movement:** `URTSUnitMovement` (pivot, turn radius, acceleration) | 1, 4 | M |
| 7 | **Formation:** spacing from the footprint, O(n log n) assignment; idle-tick savings | 1, 2 | S |

- Issues (M1b): step 0 #132, 1 #133, 2 #134, 3 #135, 4 #136, 5 #137, 6 #138, 7 #139; the UI designer's requirements brief after step 7 is #140. #141–#148 are closed duplicates; #132–#139 are canonical (recovery decision D-A, [recovery audit](../../production/SHARED_FOUNDATION_RECOVERY.md)).
- Steps 2 and 3 can run in parallel. Step 7 needs only steps 1 and 2 and may run before step 4.
- **M20 render lane** (#151 track animation → #150 shared materials → #149 LODs) may start once step 1 is stable (D-H). It stays separate from the shared gameplay work, and no two sessions edit `Units/RTSUnit.cpp` at the same time.
- **WD_RiverCrossing_01** can start greyboxing after step 4. Its map numbers in `02_MAP_DESIGN` stay provisional until then (`03_TECHNICAL_PLAN.md` §14).
- LOD generation and the shared team-colour material are **not** part of this plan. They follow as art and graphics issues (the M20 render lane above).

## 2. Files and systems per step

### Step 0: profiling baseline
- `Units/RTSUnit.cpp` (`TickOrders`), `Combat/RTSWeaponComponent.cpp` (`UpdateWeapon`), `Units/RTSAvoidanceSubsystem.cpp` (`Separate`): add `TRACE_CPUPROFILER_EVENT_SCOPE`.
- `Tools/Scripts/ue.ps1` `unitshots`: an optional `-Trace` that passes `-trace=cpu,frame`.
- Output: a baseline capture of `-Stress 240` for the M20 and for `DA_TestTank`, with the numbers added to this document.
- **Benchmark** (#141's scope, folded into #132): `Units/Debug/RTSBenchCommand.cpp` (`rts.Debug.Bench`), the flat `L_Bench` level from `setup_testmap.py`, and `ue.ps1 -Task bench -Unit <DA> [-Counts] [-Trace]`. 1, 30, 120 and 240 units, idle and on a move order, one fixed camera, 1920 × 1080, dynamic resolution off. Results in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md). From step 0 on, the bench is the comparison method for every step's performance gate.

### Step 1: collision and selection split
- `Units/RTSUnit.h/.cpp`:
  - `UCapsuleComponent Collision` becomes `UBoxComponent`.
  - `ApplyUnitData` fits the box to `GetHullBox × MeshScale` and caches the hull L, W and H.
  - `GetGroundOffset`, the tilt pivot (`:589`) and `GetNavAgentLocation` follow the box.
  - New: `GetSelectionBounds()` and `GetFootprint()` (segment plus radius).
  - `GetClosestFootprintPoint` uses the stadium.
  - `GetSelectionRadius()` keeps its interaction role.
- `Player/RTSHUD.h/.cpp`: `GetUnitScreenCircle` becomes a screen rect from the 8 corners of `GetSelectionBounds()`, with a minimum size. The brackets and health bar use it.
- `Player/RTSPlayerController.cpp`: `SelectInBox` uses the bounds centre. Picking is unchanged, because the box blocks Visibility.
- `Data/RTSEntityData.h`: the `SelectionRadius` comment is narrowed to interaction.
- `Units/RTSUnitSpawner.cpp`, `Buildings/RTSBuilding.cpp` (exit): spacing from the footprint.
- `Commands/RTSCommandDispatch.cpp`: formation spacing from the footprint (the assignment changes in step 7).
- **Tests:**
  - new `WorldDivide.Units.Footprint` (box extent, ground offset, selection bounds within the hull for M20, tank and infantry);
  - new `WorldDivide.UI.SelectionBracketsOnHull` (the projected rect centre within 10 % of the hull centre);
  - update `RTSVehicleTests` and `RTSArtTests` where they read the capsule.

### Step 2: avoidance and acquisition
- `Units/RTSAvoidanceSubsystem.h/.cpp`:
  - fixed `CellSize`, with multi-cell insertion;
  - the stadium distance;
  - the push clamped per second;
  - per-agent nav data for the raycast;
  - `QueryRadius()` for other systems.
- `Combat/RTSWeaponComponent.h/.cpp`: a staggered 5 Hz rescan; `FindNearestInRange` uses `QueryRadius`; buildings come from a per-team list.
- `Core/RTSGameState.h/.cpp` (or the building subsystem, whichever owns buildings today): the per-team building list.
- **Tests:**
  - `RTSAvoidanceTests` gains a mixed case: 200 units, 20 of them M20-sized.
  - A new two-tank parallel-lane case: two tanks side by side at 4.5 m apart do **not** push each other.
  - `RTSCombatTests`: acquisition still picks the nearest enemy, prefers a unit over a building, and still acquires within at most 0.2 s.

### Step 3: navigation agents
- `Config/DefaultEngine.ini`:
  - `[/Script/NavigationSystem.NavigationSystemV1]` with `+SupportedAgents` for Infantry (radius 40, height 180) and Vehicle (radius 190, height 350).
  - `[/Script/NavigationSystem.RecastNavMesh]` with `TileSizeUU` about 6400 and `CellSize` about 25–30 **(to validate: build time and how the dynamic building holes look)**.
- `Units/RTSUnit.cpp` (`ApplyUnitData`): `Movement->NavAgentProps` from the hull width.
- Every `GetDefaultNavDataInstance` and props-less projection or raycast in `Source/` is reviewed, for example `RTSAvoidanceSubsystem.cpp:146` and `Buildings/RTSBuildPlacement.cpp`.
- `Tools/Editor/setup_testmap.py`: the map build saves both navmeshes. `URTSEditorScripting::BuildNavigation` builds all agents.
- **Tests:**
  - `Maps.SavedNavMesh` checks both agents in every level.
  - New: `WorldDivide.Navigation.VehicleAgent`. A vehicle refuses a 3 m gap that infantry takes, and passes a 6 m gap.

### Step 4: data rescale
- `Tools/Editor/setup_testmap.py`:
  - unit fit targets, speeds, turn rates and weapons (`:1389-1426`);
  - building footprints and fits (`:1427-1453`);
  - a new uniform `fit_height`;
  - map half-sizes, starts, obstacles and supply sources (`:704-905`);
  - `TEST_SPOTS` (`:801`);
  - the stress grid in `Units/Debug/RTSUnitDebugCommands.cpp`, which follows the footprint.
- `Tools/Editor/import_local_art.py`: the Abrams refit (`:38`, `:109`).
- `AI/RTSAIPlayer.h`: `BuildingGap`, `GatherDistance`, `DefenseRadius`.
- `Units/RTSUnit.cpp`: `DockStandOff`, `MoveSnapStep`/`MoveSnapDistance`, `MoveAcceptanceRadius`.
- `Buildings/RTSBuilding.cpp`: `SpawnClearance`, `DefaultRallyDistance`.
- `Data/RTSHarvesterData.h`, `Data/RTSBuildingData.h`, `Data/RTSAircraftData.h`: the C++ defaults follow the generated values, so a new asset starts sensible.
- **Content:** every generated `DA_Test*` asset and every generated map. They are rewritten by `ue.ps1 -Task setup`; the maps need LFS locks (taken and released by the producer flow).
- **Tests:** every test that hard-codes distances, for example:
  - `RTSTestMapSmokeTest.cpp` ("within 8 m");
  - `RTSAvoidanceTests.cpp` (220 cm spacing);
  - `RTSBuildingTests.cpp`, `RTSEconomyTests.cpp` (dock and exit distances);
  - `RTSAirTests.cpp` (altitude);
  - `RTSMapTests.cpp` (start spot and preview positions).

  The values are expressed through data (radius, footprint, range) wherever the test allows, so the next tuning does not break them again.

### Step 5: camera retune

**Today** (`Player/RTSCameraPawn.h:64-105`):
- arm 3000, range 175–7000, pitch -55°, FOV 90°;
- `PanSpeed` 3000 at the default arm, `EdgeMargin` 500, fallback ±15000.
- At the default arm, about 60 m of ground is visible across the screen. A real 9.8 m tank fills about 16 % of the screen width, so the player sees one squad, not a battle.

**Readability target** (Generals-like): an MBT is 5–6 % of the screen width at the default zoom, and the player sees two to three building footprints across. That gives:

| Setting | Today | Proposed | Why |
|---|---:|---:|---|
| `Camera->FieldOfView` | 90 | **60** | Less perspective distortion over a wide area. Units at the screen edge keep their shape. |
| `DefaultZoom` | 3000 | **15000** | 2 × 15000 × tan 30° ≈ 173 m across, so a tank is about 5.7 %. |
| `MinZoom` | 175 | **600** | The close inspection view of a single vehicle stays available. |
| `MaxZoom` | 7000 | **30000** | About 350 m across. Matches `03_TECHNICAL_PLAN.md`'s 300 m maximum arm. |
| `ZoomStep` | 400 | **1500** | About ten steps from default to max |
| `TiltStartZoom` | 3000 | **6000** | The default and wider zooms stay at -55°; only close zoom tilts. |
| `PanSpeed` | 3000 | **8500** | The same screen widths per second as today |
| `EdgeMargin` | 500 | **2000** | |
| `FallbackMapArea` | ±15000 | **±50000** | |
| `MinCameraHeight` | 200 | 200 | Unchanged |

- **Unchanged behaviour:** the modern C&C-style behaviour stays (edge scroll, drag rotate, zoom-dependent tilt, terrain-following arm).
- **Infantry at the default zoom:** about 9 px wide at 1080p. Infantry readability relies on the HUD (brackets, health bars, team colour), not on mesh scale (standard rule 2). **Risk to watch in playtests;** the fallback is a later squad-icon or outline treatment, not resizing.
- **Rendering at the new distances** (shadow distance, VSM resolution, LOD switch distances) is graphics work and out of scope. It is noted for the LOD issue.
- **Validation:** `rts.Debug.UnitShots` with `SetZoomNow` at the min, default and max zoom on the M20, a tank, infantry and a vehicle factory. A side-by-side sheet goes to the producer before the values are fixed.
- **Tests:** `Maps.PreviewCameraAlignment` and any test that reads the camera defaults.

### Step 6: vehicle movement
- New `Units/RTSUnitMovement.h/.cpp` (`URTSUnitMovement : UFloatingPawnMovement`, overrides `ApplyControlInputToVelocity`).
- `Units/RTSUnit.cpp`:
  - creates it instead of `UFloatingPawnMovement`;
  - `TickOrders` faces the **path direction** for vehicles;
  - `ApplyUnitData` copies the acceleration fields.
- `Data/RTSUnitData.h`: `Acceleration`, `Deceleration`, `bPivotTurn`, `MinTurnRadius`.
- `setup_testmap.py`: tracked vehicles get `bPivotTurn`. Trucks and the builder get `MinTurnRadius` of about 800–1000.
- **Tests:** new `WorldDivide.Units.VehicleTurning`.
  - A tracked tank ordered behind itself pivots first and never moves sideways faster than 50 cm/s.
  - A truck's yaw rate stays within `speed / MinTurnRadius`.
  - Infantry behaviour is unchanged.

### Step 7: formation and idle ticks
- `Commands/RTSFormation.h/.cpp`: row and column spacing; sorted assignment.
- `Commands/RTSCommandDispatch.cpp`: passes the two spacings.
- `Units/RTSUnit.cpp`: the "unmoved" skip for `SnapToGround` and the tilt.
- `Units/RTSUnitAIController.cpp`: actor tick off.
- **Tests:** existing formation tests and new ones:
  - every unit gets a slot;
  - no two slots are closer than their footprints;
  - a 240-unit dispatch completes under 5 ms;
  - front units take front slots.

## 3. Dependencies

| Dependency | Why |
|---|---|
| Step 1 → everything | Footprint, ground offset and selection bounds are used by avoidance, nav-agent choice, formation and data. |
| Step 3 before step 4 | Rescaled maps are built once with both agents, so the levels are not re-saved twice under LFS. |
| Step 4 → step 5 | The camera is tuned against real-size scenes. |
| Steps 2, 3, 4 → WD_RiverCrossing_01 | Its bridge width, agent radius and camera arm come from here (`03_TECHNICAL_PLAN.md` §9, §14). |
| M20 assets | Untouched by every step. Only `DA_TestM20Guardian` data values change (its radius role, speed, turn rate). |

## 4. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Box root changes picking or overlap behaviour in subtle ways (placement overlap, spawn checks) | Medium | Same collision responses as the capsule; the step 1 tests; full suite run |
| Two nav agents double the navmesh build time and memory; clients also build it (`bAllowClientSideNavigation`) | Medium | Bigger tiles; measure on L_SixPeaks and RiverCrossing; clients could use only the infantry nav data for the placement ghost |
| Dynamic building holes look worse with bigger cells | Medium | Validate both cell sizes before fixing them; keep `RuntimeGeneration=Dynamic` |
| Saved navmeshes in every level must be rebuilt (`Maps.SavedNavMesh`) | Low | `ue.ps1 -Task setup` regenerates them; LFS locks per the usual flow |
| Distance-based tests break in many places at once in step 4 | High (effort) | Express test distances through data; land step 4 as one change with the full run green |
| Real-scale infantry hard to read | Medium | The HUD carries readability; playtest at the default zoom; no mesh exaggeration |
| Pivot turns make vehicles feel slow to respond | Medium | `TurnRate` and the pivot threshold are data; tune in playtests |
| Acquisition throttling delays the first shot | Low | 5 Hz staggered gives at most 0.2 s; the test bounds it |
| AI behaviour shifts with new distances (build placement density, defence radius) | Medium | Skirmish AI vs AI smoke run after step 4 |
| Weapon effects (`EffectScale`) look too small at real scale | Low | Flagged for the art and graphics pass; out of scope here |

## 5. Regression tests (every step)

- `ue.ps1 -Task build`, then `ue.ps1 -Task test` (the full `WorldDivide` suite, 177+ tests): all green.
- `ue.ps1 -Task setup` run twice: the second run writes nothing.
- `WorldDivide.UI.Validate` (HUD layouts).
- `Smoke.TestMap`, `Smoke.ListenServer` (multiplayer movement and replication), `Smoke.Maps`, `Smoke.Terrain`, `Smoke.SolidObstacles`.
- `ue.ps1 -Task unitshots -Unit DA_TestM20Guardian -Stress 240` and the same for `DA_TestTank`: numbers recorded in this document per step.
- **Manual (producer):** open L_TestMap.
  1. Select an M20 by click and by drag: the brackets hug the hull.
  2. Move a mixed group through the obstacle gaps.
  3. Move 20 tanks into a crowd: no popping.
  4. Zoom default, min and max.

## 6. Acceptance criteria

1. The M20 mesh and parts are unchanged (`WorldDivide.Art.M20Guardian` green). The M20 is 7.1 m hull and about 8 m overall in-game.
2. Every placeholder unit and building matches the standard's dimensions within ±10 % on its binding axis.
3. The M20 selection brackets enclose the hull and turret on screen. The rect centre is within 10 % of the hull's screen centre at every zoom. The health bar sits directly above the hull.
4. A click on empty ground 1 m beside a tank's hull does not select it. A click anywhere on the hull does.
5. Two tanks side by side at 4.5 m centre distance do not push each other. Two tanks overlapping end to end separate without moving more than `MoveSpeed × 0.5` per second.
6. Vehicles never path through gaps narrower than 6 m. Infantry still use 1 m gaps.
7. Performance (D-G). Budgets stay measurable. Every comparison uses the same build, the same bench scene (`L_Bench`), the same camera and the same method; a figure is either a **provisional target** (a budget not yet met or not yet measured) or a **measured** result with its machine and date in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md).
   - Provisional target: no server frame > 33 ms on a 240-unit move order (#139, #136).
   - Provisional target: avoidance pass mean < 1.0 ms and max < 2.0 ms with 200 mixed units including 20 M20s (#134).
   - Provisional target: target acquisition < 0.5 ms per frame with 240 idle armed units (#134).
   - `DA_TestTank` at 240 units: game thread no worse than the step 0 bench baseline, re-measured on the same build (#136).
   - 240 moving M20s: game thread ≤ 240 moving placeholder tanks. **Deferred** until after #150 and #151; the M20's render-state cost does not fail #136.
   - Provisional minimum-spec design target (D-J): 1080p / 60 FPS with 200 active units on RTX 3060-class hardware. It is a design target, **not** a verified result; neither it nor an eight-player battle has been measured.
8. Tracked vehicles do not move sideways faster than 50 cm/s, and pivot when ordered behind.
9. The default camera shows about 170 m across, and an MBT reads at about 5–6 % of the screen width.
10. A listen-server smoke test passes: client units move, select and show brackets identically to the server.
11. The full automation suite is green and setup is idempotent.

## 7. Measurements per step

The bench figures for every step (`ue.ps1 -Task bench`, `L_Bench`) are in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md), next to the step 0 bench baseline. The tables below are the earlier `unitshots` method, kept for continuity with `report_ue5.md`.

Method: `ue.ps1 -Task unitshots -Unit <Unit> -Stress 240 [-Trace]` on L_TestMap, Development editor `-game`, 1920×1080 offscreen, RTX 4090 machine of `report_ue5.md`. Every number is the mean over the 120 measured frames of the `stress_240` view (278 units: the map's 38 plus 240). The scoped timers come from the Insights capture, exported for the `stress_240` region with `UnrealInsights.exe -OpenTraceFile=<trace> -NoUI -AutoQuit -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics <csv> -threads=GameThread -timers=* -region=stress_240"`.

### Step 0: baseline (2026-10-15)

| Stress 240 | M20 Guardian | `DA_TestTank` |
|---|---:|---:|
| Frame, no trace (two runs) | 13.36 / 13.83 ms | 12.70 / 12.91 ms |
| Frame, with trace | 12.37 ms | 12.43 ms |
| Avoidance pass (log) | 0.10–0.13 ms, max 0.25 ms | 0.10–0.12 ms, max 0.33 ms |
| GPU | 4.4 ms | 1.9–3.0 ms |
| Draw calls / triangles | 2556 / 51.2 M | 952 / 2.9 M |

Game thread per frame in the `stress_240` region (Insights, inclusive):

| Scope | Calls per frame | M20 Guardian | `DA_TestTank` |
|---|---:|---:|---:|
| `UWorld_Tick` | 1 | 10.00 ms | 9.98 ms |
| `URTSWeaponComponent::UpdateWeapon` | 280 | 7.94 ms | 7.96 ms |
| of which `FindNearestEnemyInRange` (full scan) | 274 | **7.92 ms** | **7.94 ms** |
| `ARTSUnit::TickOrders` | 278 | 0.65 ms | 0.59 ms |
| `URTSAvoidanceSubsystem::Separate` | 1 | 0.10 ms | 0.12 ms |
| Slate (HUD) | 1 | 0.98 ms | 1.05 ms |

**Findings.**
- Target acquisition is about **64 % of the frame** with 240 idle armed units: 274 full scans per frame at about 29 µs each. That confirms [movement performance §3.2](RTS_MOVEMENT_PERFORMANCE.md#32-target-acquisition-is-a-full-scan-every-frame-confirmed-in-code); the 5 Hz staggered rescan and the spatial query of step 2 are the biggest single gain on the table.
- `TickOrders` (orders and the ground snap) is about 2.2 µs per unit; path following and the AI controllers do not show up.
- The avoidance pass is cheap on the stress grid because the grid spacing keeps units apart; the packed-crowd case is measured by `RTSAvoidanceTests` in step 2.
- The M20 and the placeholder tank cost the same on the game thread. The M20 costs more on the GPU (51 M triangles, 2556 draw calls: Nanite off and one material instance per slot per unit), which belongs to the LOD and rendering phase, not this plan.

### Step 1: box collision and selection bounds (2026-10-15)

Same method as step 0, `-Trace` on.

| Stress 240 | M20 Guardian | `DA_TestTank` |
|---|---:|---:|
| Frame, with trace | 11.43 ms | 11.42 ms |
| Avoidance pass (log) | 0.09 ms, max 0.21 ms | 0.10 ms, max 0.22 ms |
| GPU | 4.0 ms | 2.5 ms |
| Draw calls / triangles | 2556 / 51.2 M | 952 / 2.9 M |

Game thread per frame in the `stress_240` region (Insights, inclusive):

| Scope | Calls per frame | M20 Guardian | `DA_TestTank` |
|---|---:|---:|---:|
| `UWorld_Tick` | 1 | 9.15 ms | 9.14 ms |
| `URTSWeaponComponent::UpdateWeapon` | 280 | 7.20 ms | 7.32 ms |
| `ARTSUnit::TickOrders` | 278 | 0.62 ms | 0.53 ms |
| `URTSAvoidanceSubsystem::Separate` | 1 | 0.09 ms | 0.10 ms |
| Slate (`DrawWindows`) | 1 | 0.93 ms | 0.95 ms |

**Finding.** No scope got slower. The box root, the stadium edge and the projected brackets cost nothing measurable; every figure is at or under step 0. The bench comparison and its M20 grid change are in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md#step-1-133-box-collision-and-selection-bounds).

### Step 2: fixed-cell avoidance and throttled acquisition (2026-10-15)

Same method as step 0, `-Trace` on.

| Stress 240 | M20 Guardian | `DA_TestTank` |
|---|---:|---:|
| Frame, with trace | 4.31 ms | 3.87 ms |
| Avoidance pass (log) | 0.16 ms, max 0.24 ms | 0.10 ms, max 0.17 ms |
| GPU | 4.1 ms | 1.7 ms |
| Draw calls / triangles | 2561 / 51.2 M | 957 / 2.9 M |

Game thread per frame in the `stress_240` region (Insights, inclusive):

| Scope | Calls per frame | M20 Guardian | `DA_TestTank` |
|---|---:|---:|---:|
| `UWorld_Tick` | 1 | 2.00 ms | 1.78 ms |
| `URTSWeaponComponent::UpdateWeapon` | 281 | 0.025 ms | 0.021 ms |
| of which `FindNearestEnemyInRange` (grid query) | 6.0 (M20), 5.5 (tank) | 0.016 ms | 0.013 ms |
| `ARTSUnit::TickOrders` | 278 | 0.66 ms | 0.57 ms |
| `URTSAvoidanceSubsystem::Separate` | 1 | 0.16 ms | 0.10 ms |
| Slate (`DrawWindows`) | 1 | 0.85 ms | 0.88 ms |

**Findings.**
- Acquisition went from 7.2–7.3 ms (step 1) to about 0.02 ms per frame. Searches per frame fell from 274 to about 6: each weapon without a target searches once per 0.2 s, and at about 230 frames per second that is one frame in 46. At 60 fps it would be about 23 searches, roughly 0.06 ms at the measured 2–3 µs each.
- `UWorld_Tick` fell from about 9.1 ms to 1.8–2.0 ms; `TickOrders` is now the largest gameplay scope.
- `Separate` rose from 0.09–0.10 ms to 0.10–0.16 ms: the pass now indexes every unit, aircraft included, for `QueryRadius`, and the M20's footprint covers several 600 cm cells. The bench comparison is in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md#step-2-134-fixed-cell-avoidance-and-throttled-acquisition).

### Step 3: infantry and vehicle nav agents, hull clearance (2026-10-15)

**Navmesh per level.** `ue.ps1 -Task setup`, `URTSEditorScripting::BuildNavigation` in the editor commandlet: both agents built together, `TileSizeUU` 6400, `RuntimeGeneration=Dynamic`. Memory is the built tiles (`LogMemUsed`) and the compressed tile cache the dynamic navmesh keeps to rebuild tiles around new buildings, per agent.

| Level | Tiles per agent | Infantry MB (tile cache) | Vehicle MB (tile cache) | Build, both agents: CellSize 25 | CellSize 30 |
|---|---:|---:|---:|---:|---:|
| L_TestMap | 48 | 0.01 (0.02) | 0.02 (0.02) | 0.13 s | 0.09 s |
| L_Bench | 75 | 0.02 (0.03) | 0.02 (0.03) | 0.30 s | 0.20 s |
| L_Skirmish | 48 | 0.01 (0.02) | 0.01 (0.02) | 0.15 s | 0.09 s |
| L_SkirmishDuel | 48 | 0.01 (0.02) | 0.01 (0.02) | 0.16 s | 0.09 s |
| L_RiverFord | 27 | 0.02 (0.02) | 0.02 (0.02) | 0.12 s | 0.08 s |
| L_QuarryCross | 48 | 0.02 (0.03) | 0.02 (0.03) | 0.17 s | 0.11 s |
| L_SixPeaks | 48 | 0.02 (0.03) | 0.03 (0.04) | 0.21 s | 0.13 s |

**CellSize is 25.** At 30 the build is about a third faster, but both are a fraction of a second per level and memory does not change at two decimals. At 30 the navmesh edges around blocks and building holes move by up to a cell, and the clearance tests fail: `WorldDivide.Navigation.HullClearance.Measure` finds 8 hulls overlapping an obstacle that the clearance misses, and `HullClearance.Drive` stops the long narrow test vehicle 50.8 cm from the obstacle, over the 50 cm gap. At 25 both pass.

**Unitshots**, same method as step 0, `-Trace` on.

| Stress 240 | M20 Guardian | `DA_TestTank` |
|---|---:|---:|
| Frame, with trace | 4.68 ms | 4.29 ms |
| Avoidance pass (log) | 0.31 ms, max 0.47 ms | 0.21 ms, max 0.47 ms |
| GPU | 4.1 ms | 1.7 ms |
| Draw calls / triangles | 2560 / 51.2 M | 957 / 2.9 M |
| Obstacle shots: hull overlap with Block_North / the vehicle factory | 0 × 0 cm / 0 × 0 cm | 0 × 0 cm / 0 × 0 cm |

Game thread per frame in the `stress_240` region (Insights, inclusive):

| Scope | Calls per frame | M20 Guardian | `DA_TestTank` |
|---|---:|---:|---:|
| `UWorld_Tick` | 1 | 2.40 ms | 2.05 ms |
| `URTSWeaponComponent::UpdateWeapon` | 281 | 0.030 ms | 0.023 ms |
| of which `FindNearestEnemyInRange` (grid query) | 6.7 (M20), 6.1 (tank) | 0.021 ms | 0.014 ms |
| `ARTSUnit::TickOrders` | 278 | 0.67 ms | 0.54 ms |
| `URTSAvoidanceSubsystem::Separate` | 1 | 0.31 ms | 0.21 ms |
| of which `RTSHullClearance::GetClearance` | 27 (M20), 2.3 (tank) | 0.054 ms | 0.002 ms |
| Slate (`DrawWindows`) | 1 | 0.94 ms | 0.97 ms |

**Findings.**
- `Separate` rose from 0.10–0.16 ms to 0.21–0.31 ms. The hull check itself is small (0.05 ms for the M20, whose 1103 cm hull reaches the navmesh edges of its stress grid more often); the rest of the rise is spread over the pass and not attributed to one scope.
- `UWorld_Tick` rose by 0.3–0.4 ms against step 2, about half of it `Separate`; `TickOrders` is unchanged. Path fitting runs once per new path and does not show up on the stress grid, where nobody repaths.
- The bench comparison is in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md#step-3-135-infantry-and-vehicle-nav-agents-hull-clearance).

### Step 7: footprint formations, sorted slot assignment, idle-tick savings (2026-10-15)

Same method as step 0, `-Trace` on.

| Stress 240 | M20 Guardian | `DA_TestTank` |
|---|---:|---:|
| Frame, with trace | 4.28 ms | 3.41 ms |
| Avoidance pass (log) | 0.35 ms, max 0.78 ms | 0.20 ms, max 0.45 ms |
| GPU | 4.0 ms | 1.8 ms |
| Draw calls / triangles | 2560 / 51.2 M | 957 / 2.9 M |

Game thread per frame in the `stress_240` region (Insights, inclusive):

| Scope | Calls per frame | M20 Guardian | `DA_TestTank` |
|---|---:|---:|---:|
| `UWorld_Tick` | 1 | 1.50 ms | 1.29 ms |
| `URTSWeaponComponent::UpdateWeapon` | 281 | 0.031 ms | 0.021 ms |
| of which `FindNearestEnemyInRange` (grid query) | 6.2 (M20), 4.8 (tank) | 0.022 ms | 0.012 ms |
| `ARTSUnit::TickOrders` | 278 | **0.048 ms** | **0.029 ms** |
| `URTSAvoidanceSubsystem::Separate` | 1 | 0.35 ms | 0.20 ms |
| of which `RTSHullClearance::GetClearance` | 27 (M20), 2 (tank) | 0.060 ms | 0.001 ms |
| Slate (`DrawWindows`) | 1 | 0.82 ms | 0.89 ms |

A 240-unit move order on L_Bench (`ue.ps1 -Task bench -Counts 240 -Trace`), the frame of the order:

| Scope | M20 Guardian | `DA_TestTank` |
|---|---:|---:|
| `Server_IssueCommand` (formation and every unit's path request) | 1.82 ms | 2.07 ms |
| of which `RTSCommands::PlanFormation` | 0.07 ms | 0.21 ms |

**Findings.**
- `TickOrders` fell from 0.67 / 0.54 ms (step 3) to 0.05 / 0.03 ms: the stress grid stands still, and a unit that has not moved more than 1 cm no longer traces the ground. `UWorld_Tick` fell by 0.8–0.9 ms accordingly.
- The unit AI controllers no longer tick. The capture has no per-actor tick scopes (no named events), so that saving is not split out; `WorldDivide.Units.IdleGrounding` checks the tick is off, and the smoke tests check that move orders still complete.
- The whole 240-unit order costs about 2 ms on the frame it is given, under the 5 ms of #139's acceptance; the formation itself is a tenth of that. The bench's slowest move frame is 6.9 ms (tank) and 20.3 ms (M20), under the 33 ms budget. The bench comparison is in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md#step-7-139-footprint-formations-on-log-n-slots-idle-tick-savings).
- `Separate` is within the run-to-run spread of step 3 (0.31 → 0.35 ms M20, 0.21 → 0.20 ms tank).
