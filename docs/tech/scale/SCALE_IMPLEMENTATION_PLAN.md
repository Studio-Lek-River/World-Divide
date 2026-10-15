# Scale implementation plan

Status: **proposal**. Nothing is implemented until the producer approves. The plan is based on the [standard](WORLD_SCALE_STANDARD.md), the [placeholder audit](PLACEHOLDER_SCALE_AUDIT.md), [collision and selection](VEHICLE_COLLISION_AND_SELECTION.md) and [movement performance](RTS_MOVEMENT_PERFORMANCE.md).

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

- Steps 2 and 3 can run in parallel.
- **WD_RiverCrossing_01** can start greyboxing after step 4. Its map numbers in `02_MAP_DESIGN` stay provisional until then (`03_TECHNICAL_PLAN.md` §14).
- LOD generation and the shared team-colour material are **not** part of this plan. They follow as art and graphics issues.

## 2. Files and systems per step

### Step 0: profiling baseline
- `Units/RTSUnit.cpp` (`TickOrders`), `Combat/RTSWeaponComponent.cpp` (`UpdateWeapon`), `Units/RTSAvoidanceSubsystem.cpp` (`Separate`): add `TRACE_CPUPROFILER_EVENT_SCOPE`.
- `Tools/Scripts/ue.ps1` `unitshots`: an optional `-Trace` that passes `-trace=cpu,frame`.
- Output: a baseline capture of `-Stress 240` for the M20 and for `DA_TestTank`, with the numbers added to this document.

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
7. Performance (same machine and method as `report_ue5.md`):
   - 240 M20s at real-scale spacing: game thread ≤ today's 240-tank figure (10.8 ms);
   - avoidance pass mean < 1.0 ms with 200 mixed units including 20 M20s;
   - no server frame > 33 ms on a 240-unit move order.
8. Tracked vehicles do not move sideways faster than 50 cm/s, and pivot when ordered behind.
9. The default camera shows about 170 m across, and an MBT reads at about 5–6 % of the screen width.
10. A listen-server smoke test passes: client units move, select and show brackets identically to the server.
11. The full automation suite is green and setup is idempotent.
