# Performance results: real scale

Every number on this page was measured with `ue.ps1 -Task bench` (`rts.Debug.Bench`) on L_Bench, unless it says otherwise. The bench is step 0 of the real-scale plan (#132) and the comparison method for every later step: same build, same scene, same camera, same method.

## Conditions of the step 0 baseline

| | |
|---|---|
| Date | 2026-10-15 |
| Code | `main` at `280c810` plus the bench commit of #132 |
| CPU | AMD Ryzen 7 7800X3D 8-core (the development PC) |
| GPU | NVIDIA GeForce RTX 4090, driver 617.42, D3D12 |
| Build | Development editor build in `-game`, an offscreen window at 1920 × 1080 |
| Rendering | Dynamic resolution off and `r.ScreenPercentage 100`, so the internal render resolution is 1920 × 1080 in every row. VSync off, no frame-rate cap. |
| Camera | One fixed camera for every scenario: pitch -55°, 170 m from a point 30 m ahead of the grid centre, FOV 60°. |
| Averaging | Each value is a mean over 120 frames. Runs without `-Trace`; the Insights capture adds overhead. |

**What these numbers are not.** They are RTX 4090 and Ryzen 7800X3D figures. The provisional minimum-spec target (1080p / 60 FPS with 200 active units on RTX 3060-class hardware) **has not been measured**, and neither has an eight-player battle.

## Provisional targets against measurements

A **provisional target** is a budget from plan §6 item 7; it is not a result until a bench run on the named machine meets it. Figures are game-thread milliseconds unless stated.

| Budget | Status | Step 0 measured (RTX 4090 PC) | Gate |
|---|---|---|---|
| 1080p / 60 FPS (16.7 ms frame) with 200 active units on RTX 3060 class | Provisional target, **not measured** | — | #152, #160 |
| Eight-player battle | **Not measured** | — | #152 |
| No server frame > 33 ms on a 240-unit move order | Provisional target | 20.42 ms (M20), 19.76 ms (tank) slowest frame | #139, #136 |
| Avoidance mean < 1.0 ms, max < 2.0 ms (200 mixed units) | **Met** on this PC at step 2: 0.29–0.44 ms mean, 0.54–1.16 ms max in `WorldDivide.Avoidance.Mixed` (three runs) | 240 moving M20s: 0.535 ms mean, 0.872 ms max | #134 |
| Target acquisition < 0.5 ms with 240 idle armed units | **Met** on this PC at step 2: 0.013 ms (M20), 0.012 ms (tank) | 5.67 ms (M20), 5.50 ms (tank) | #134 |
| 240 `DA_TestTank`: game thread no worse than this baseline | Comparison | idle 8.16 ms, move 4.16 ms | #136 |
| 240 moving M20s ≤ 240 moving tanks | **Deferred** until after #150 and #151 | 17.41 ms against 4.16 ms | after #151 |
| GPU memory | **Not measured**: the bench records process physical memory only | — | — |

## Method

- **Units and grid:** for each count (1, 30, 120 and 240), that many of one unit type stand facing +X in a roughly square grid. The grid is spaced like a formation: hull length + 3 m along X, width + 2.5 m along Y.
- **Idle:** the units stand still for 3 s, then 120 frames are measured.
- **Move:** one move order goes to all of them through `Server_IssueCommand`, the player's own path, toward a point 60 m ahead. After 1 s, 120 frames are measured while they drive.
- **Game ms (max):** the slowest game-thread frame. For the move scenario it includes the frame of the order itself, so it shows the dispatch hitch.
- **Avoidance:** `URTSAvoidanceSubsystem::Separate`, timed per frame.
- **Target search:** the time all weapons spend searching for a target (`FindNearestInRange` up to step 1; the unit query and the building list from step 2), summed per frame.
- **Memory:** the process's used physical memory at the end of the scenario, and its peak so far in the run. GPU memory is not recorded.
- **Teams:** L_Bench gives each of two teams a headquarters in opposite corners, about 170 m from the grid. The benchmark units therefore have an enemy, but none in range.
- **Reproduce:** `ue.ps1 -Task bench -Unit <DataAsset> [-Counts 1,30,120,240] [-Trace]`. The table and a CSV go to `Saved/Bench/<DataAsset>/`, headed by the CPU, GPU, driver, RHI and build. `-Trace` writes an Unreal Insights capture with one region per scenario.

## Step 0 baseline (#132, before any real-scale change)

### M20 Guardian (`DA_TestM20Guardian`, 89,396 triangles, LOD0 only)

| Units | Scenario | Frame ms | Game ms (max) | Render ms | RHI ms | GPU ms | Draw calls | Triangles | Avoidance ms (max) | Target search ms | Memory MB (peak) |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | idle | 2.03 | 1.25 (2.28) | 2.03 | 0.77 | 1.76 | 105 | 0.20 M | 0.007 (0.009) | 0.009 | 2505 (2505) |
| 1 | move | 2.08 | 1.37 (2.85) | 2.09 | 0.85 | 1.81 | 107 | 0.20 M | 0.008 (0.062) | 0.000 | 2510 (2510) |
| 30 | idle | 2.33 | 1.46 (2.81) | 2.33 | 0.81 | 2.09 | 565 | 6.25 M | 0.014 (0.021) | 0.179 | 2619 (2619) |
| 30 | move | 3.19 | 3.19 (5.34) | 2.22 | 0.89 | 2.07 | 572 | 6.10 M | 0.023 (0.208) | 0.000 | 2653 (2653) |
| 120 | idle | 3.66 | 3.66 (4.38) | 1.87 | 0.91 | 2.78 | 1,502 | 20.70 M | 0.043 (0.085) | 1.779 | 2770 (2770) |
| 120 | move | 9.43 | 9.43 (11.52) | 4.30 | 0.95 | 3.05 | 1,632 | 21.22 M | 0.246 (0.527) | 0.026 | 2776 (2776) |
| 240 | idle | 8.43 | 8.43 (10.71) | 2.09 | 0.97 | 3.60 | 2,514 | 36.56 M | 0.088 (0.151) | 5.674 | 2807 (2807) |
| 240 | move | 17.40 | 17.41 (20.42) | 7.16 | 1.06 | 4.66 | 2,777 | 38.92 M | 0.535 (0.872) | 0.263 | 2819 (2819) |

### Placeholder tank (`DA_TestTank`, today's 2.6 m fit)

| Units | Scenario | Frame ms | Game ms (max) | Render ms | RHI ms | GPU ms | Draw calls | Triangles | Avoidance ms (max) | Target search ms | Memory MB (peak) |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | idle | 2.07 | 1.29 (2.25) | 2.07 | 0.81 | 1.74 | 84 | 0.01 M | 0.009 (0.207) | 0.009 | 2455 (2455) |
| 1 | move | 2.10 | 1.25 (2.72) | 2.09 | 0.82 | 1.77 | 84 | 0.01 M | 0.008 (0.058) | 0.000 | 2459 (2460) |
| 30 | idle | 2.09 | 1.58 (3.06) | 2.04 | 0.79 | 1.79 | 229 | 0.30 M | 0.015 (0.097) | 0.200 | 2546 (2546) |
| 30 | move | 2.08 | 1.58 (3.45) | 2.08 | 0.83 | 1.80 | 229 | 0.30 M | 0.019 (0.078) | 0.000 | 2546 (2547) |
| 120 | idle | 3.69 | 3.69 (4.71) | 1.69 | 0.91 | 1.88 | 683 | 1.24 M | 0.042 (0.087) | 1.756 | 2704 (2704) |
| 120 | move | 2.74 | 2.74 (4.91) | 1.85 | 0.86 | 1.93 | 679 | 1.20 M | 0.049 (0.147) | 0.000 | 2737 (2737) |
| 240 | idle | 8.16 | 8.16 (9.85) | 1.89 | 0.94 | 2.51 | 1,103 | 2.21 M | 0.080 (0.142) | 5.498 | 2745 (2745) |
| 240 | move | 4.16 | 4.16 (19.76) | 1.94 | 0.95 | 1.98 | 1,193 | 2.33 M | 0.090 (0.188) | 0.000 | 2746 (2746) |

### The earlier method (`ue.ps1 -Task unitshots -Stress 240`, L_TestMap)

This was repeated for continuity with `report_ue5.md`. It uses the engine's default dynamic resolution, a stress grid spaced by selection radius, and the RTS camera at its farthest zoom.

| Scene | Game thread | GPU | Draw calls | Triangles | `report_ue5.md` |
|---|---:|---:|---:|---:|---|
| L_TestMap baseline (38 units) | 2.90 ms | 1.57 ms | 313 | 0.86 M | 2.9 ms, 1.6 ms, 313, 0.86 M |
| + 240 M20 | 11.98 ms | 3.99 ms | 2,556 | 51.2 M | 11.5 ms, 4.0 ms, 2,556, 51.2 M |
| + 240 `DA_TestTank` | 12.78 ms | 2.81 ms | 953 | 2.86 M | 10.8 ms, 2.7 ms, 952, 2.9 M |

### What the baseline shows

**CPU (game thread) is the limit in every scenario with 120 units or more. The GPU never is:** its highest figure is 4.7 ms on an RTX 4090.

1. **Idle armed units: target search dominates.** 240 idle units spend 5.5–5.7 ms of their 8.2–8.4 ms game thread in `FindNearestInRange`. That is a full actor scan per weapon per frame while nothing is in range. The cost is the same for both unit types and grows with n², from 1.8 ms at 120 units to 5.5–5.7 ms at 240. → #134 (5 Hz staggered rescan and spatial query).
2. **Moving M20s: render-state updates dominate.** 240 moving M20s cost 17.4 ms on the game thread, against 4.2 ms for the placeholder. The Insights capture of the 240-unit move region (game thread, per frame) breaks it down:
   - `SendAllEndOfFrameUpdates` → `DeferredRenderUpdates_GameThread`: about 6.7 ms. Every tank's three wheel ISMs are marked render-state dirty each frame by `UpdateRunningGear` (`BatchUpdateInstancesTransforms`, `bMarkRenderStateDirty`). Their proxies are therefore recreated each frame, with `UStaticMesh::GetUsedMaterials` (246k calls) and texture-streaming registration (`FStreamingTextureLevelContext`, about 1.9 ms) every time.
   - `Movement` and `Collision`: about 2.4 ms and 3.0 ms inclusive. Every moved tank carries nine attached primitive components whose transforms and overlaps update.
   - Render thread: 7.2 ms against 1.9 ms for the placeholder, for the same reason.

   The capture is from the first bench run on 2026-10-15 (same build and method); a fresh `-Trace` run of the same day reproduces the move cost (17.2 ms).

   → #150 (component and render-state overhead) and #151 (the track animation must not add to it).
3. **Draw calls and triangles: about 10.5 draw calls and 150k triangles per M20.** Shadow and depth passes are included. There are no LODs and no merging across tanks, because each tank has its own MIDs. → #149 (LODs) and #150 (shared materials).
4. **The move-order hitch is about 20 ms for 240 units**, for both unit types: the slowest game-thread frame of the move. The O(n³) slot assignment is the suspect. → #139.
5. **Avoidance is cheap when units stand apart**: at most 0.5 ms mean and 0.9 ms peak at 240 moving M20s (1.3 ms peak in an earlier run of the same day). The 24 ms reported in `report_ue5.md` came from overlapping units (see `RTS_MOVEMENT_PERFORMANCE.md` §3.1). → #134.

### Run-to-run variation

The same build was benched twice on 2026-10-15 (an earlier run during the bench's development and the baseline above). The 240-unit rows differ by up to about 0.6 ms game thread and 0.4 ms target search; the slowest-frame column varies by up to about 1 ms. A later step's change should be read against that spread, not against a single row.

## Step 1 (#133, box collision and selection bounds)

Same machine, same build configuration and method as step 0, on 2026-10-15. "Before" is `main` at `8544bd3` re-benched just before the change, so before and after share the session.

### The bench grid follows the hull now

The bench sizes its grid from the unit's component bounds at rest. Up to step 0 those included the collision capsule, which was 8 m wide for the M20, so the step 0 M20 grid was wider than the method above says. With the box, the grid is what the method says. The tank's grid shrinks a little as well:

| Unit | Before (cm, X × Y) | After |
|---|---:|---:|
| M20 Guardian | 1150 × 1050 | 1103 × 604 |
| `DA_TestTank` | 560 × 430 | 560 × 343 |

At 604 cm apart, the M20's avoidance circles (`SelectionRadius` 400 cm, unchanged until #134) overlap their neighbours'. The idle M20s are then pushed apart for the whole measurement, so they move. That costs avoidance time and the render-state updates of moving M20s (see the step 0 finding 2). The tanks' 90 cm circles stay clear at 343 cm, so the tank rows compare like for like.

### Before and after, 240 units

| Unit | Scenario | Game ms before | Game ms after | Avoidance ms before → after | Target search ms before → after | Draw calls before → after |
|---|---|---:|---:|---:|---:|---:|
| `DA_TestTank` | idle | 9.47 (12.72) | 8.18 (10.40) | 0.104 → 0.080 | 6.42 → 5.46 | 1,103 → 1,183 |
| `DA_TestTank` | move | 4.29 (20.55) | 4.18 (16.08) | 0.091 → 0.117 | 0.00 → 0.00 | 1,193 → 1,283 |
| M20 Guardian | idle | 10.52 (13.31) | 12.84 (15.63) | 0.165 → 1.610 | 6.76 → 6.00 | 2,514 → 2,791 |
| M20 Guardian | move | 18.93 (26.71) | 18.37 (24.22) | 0.593 → 1.093 | 0.40 → 0.39 | 2,791 → 3,076 |

- **Tank:** no regression; every row is within the run-to-run spread or better. The extra draw calls come from more units in the camera's view on the narrower grid.
- **M20 idle:** +2.3 ms game thread. The cause is the scene, not the code: avoidance (+1.4 ms) and render time (2.53 → 5.06 ms) rise because the idle M20s now overlap and move. #134 sizes avoidance from the footprint (a 1.85 m half width), which stands this grid clear again.
- **Code cost of the change:** the fixed-spacing `unitshots` run below, whose grid follows `SelectionRadius` and did not change, shows none.

The full tables are in `Saved/Bench/<DataAsset>/` of the run.

### `unitshots -Stress 240 -Trace` (L_TestMap)

| Stress 240, with trace | Step 0 | Step 1 |
|---|---:|---:|
| M20 Guardian frame | 12.37 ms | 11.43 ms |
| `DA_TestTank` frame | 12.43 ms | 11.42 ms |

The Insights split is in [plan §7](SCALE_IMPLEMENTATION_PLAN.md#7-measurements-per-step).

## Step 2 (#134, fixed-cell avoidance and throttled acquisition)

Same machine, build configuration and method as step 0, on 2026-10-15. "Before" is `main` at `233c900` (step 1) re-benched just before the change, in the same session. The grid is the step 1 grid (M20 1103 × 604 cm).

What changed: avoidance sizes each unit by its stadium footprint (`ARTSUnit::GetFootprint`) instead of `SelectionRadius`, in fixed 600 cm cells, and pushes no faster than half the unit's speed. Weapons without a target search five times a second, each on its own frames, through `URTSAvoidanceSubsystem::QueryRadius` and the per-team building list of `ARTSGameState`; a target they hold is still checked every frame.

### Before and after, 240 units

| Unit | Scenario | Game ms before | Game ms after | Render ms before → after | Avoidance ms before → after | Target search ms before → after |
|---|---|---:|---:|---:|---:|---:|
| `DA_TestTank` | idle | 8.41 (9.82) | 2.45 (3.17) | 1.85 → 1.77 | 0.087 → 0.104 | 5.639 → 0.012 |
| `DA_TestTank` | move | 4.31 (17.78) | 4.19 (16.30) | 1.90 → 1.88 | 0.123 → 0.118 | 0.000 → 0.000 |
| M20 Guardian | idle | 13.40 (16.67) | 2.46 (3.60) | 4.60 → 3.96 | 1.679 → 0.137 | 6.185 → 0.013 |
| M20 Guardian | move | 19.47 (25.55) | 16.63 (20.06) | 7.16 → 6.95 | 1.262 → 0.651 | 0.453 → 0.008 |

At 120 units: M20 idle 5.12 → 1.75 ms, tank idle 3.67 → 1.89 ms; target search 1.89 → 0.006 ms and 1.76 → 0.005 ms.

- **The M20 idle regression of step 1 is gone.** Side by side at 604 cm, the M20s' 354 cm wide footprints stand 250 cm clear, so nothing pushes them: avoidance 1.679 → 0.137 ms, and render time drops back because the M20s stand still. `WorldDivide.Avoidance.ParallelTanks` checks it: two M20s 450 cm apart moved 0.0 cm in 2 s.
- **Acquisition** fell from about 5.6–6.2 ms to about 0.01 ms per frame for 240 idle armed units, under the 0.5 ms target by a wide margin. The bench units have an enemy, but none in range, so every weapon keeps searching; that is the worst idle case.
- **Avoidance with nothing to push** costs a little more than step 0 (0.10–0.14 ms against 0.08–0.09 ms): every pass now indexes all units, aircraft included, for `QueryRadius`.
- **M20 move** is still 16.6 ms against 4.2 ms for the tank. Its game thread waits on rendering (render 6.95 ms, 41.7 M triangles); that is the M20 render lane (#151 → #150 → #149), not this step.

The full tables are in `Saved/Bench/<DataAsset>/` of the run.

### Mixed crowd (`RTSAvoidanceTests`, L_TestMap in PIE)

Not a bench scene: `WorldDivide.Avoidance.Mixed` walks 180 tanks and infantry plus 20 M20s across L_TestMap, sampled for 4 s with the editor's test time dilation. Three runs on 2026-10-15:

| Run | Avoidance pass mean | Max | Units walking (mean) |
|---|---:|---:|---:|
| 1 | 0.294 ms | 0.543 ms | 75 of 200 (end of sample) |
| 2 | 0.442 ms | 1.158 ms | 111 of 200 |
| 3 | 0.348 ms | 0.654 ms | 113 of 200 |

`WorldDivide.Avoidance.FrameTime` (200 tanks and infantry from a packed block) gives 0.53–0.80 ms mean and 0.91–1.95 ms max over the same runs: units that start overlapping now part at half speed over several frames instead of jumping apart, so the first second of that test has more pushes, each with a navmesh raycast. It stays under its 1.0 ms mean budget; its max is not a gate.

### `unitshots -Stress 240 -Trace` (L_TestMap)

| Stress 240, with trace | Step 0 | Step 1 | Step 2 |
|---|---:|---:|---:|
| M20 Guardian frame | 12.37 ms | 11.43 ms | 4.31 ms |
| `DA_TestTank` frame | 12.43 ms | 11.42 ms | 3.87 ms |

The Insights split is in [plan §7](SCALE_IMPLEMENTATION_PLAN.md#7-measurements-per-step).

## Step 3 (#135, infantry and vehicle nav agents, hull clearance)

Same machine, build configuration and method as step 0, on 2026-10-15. "Before" is the step 2 "after" column above, benched earlier the same day; read the differences against the run-to-run spread of step 0 (about 0.6 ms game thread, 1 ms slowest frame).

What changed: two navmeshes (Infantry radius 40 / height 180, Vehicle 190 / 350); each unit walks the narrowest agent wide enough for its hull. `RTSHullClearance` fits each new path to the unit's stadium footprint, and the avoidance pass backs a hull out of an obstacle when the navmesh edge comes inside it. The placement of a production building is refused when its units cannot leave the exit.

### Before and after, 240 units

| Unit | Scenario | Game ms before | Game ms after | Render ms before → after | Avoidance ms before → after | Target search ms before → after |
|---|---|---:|---:|---:|---:|---:|
| `DA_TestTank` | idle | 2.45 (3.17) | 2.74 (3.69) | 1.77 → 1.68 | 0.104 → 0.200 | 0.012 → 0.013 |
| `DA_TestTank` | move | 4.19 (16.30) | 4.31 (16.62) | 1.88 → 1.89 | 0.118 → 0.189 | 0.000 → 0.000 |
| M20 Guardian | idle | 2.46 (3.60) | 2.68 (4.60) | 3.96 → 3.93 | 0.137 → 0.254 | 0.013 → 0.011 |
| M20 Guardian | move | 16.63 (20.06) | 16.95 (22.73) | 6.95 → 7.03 | 0.651 → 0.782 | 0.008 → 0.008 |

- **Game thread** is within the run-to-run spread in every row (+0.1 to +0.3 ms); the slowest move frame stays under the 33 ms budget (22.7 ms M20, 16.6 ms tank).
- **Avoidance** costs about 0.1 ms more: `Separate` now checks the hull of every unit it moved against the navmesh edge (`RTSHullClearance::GetClearance`, cached per unit for a short time and skipped while the unit's slack covers the move). It stays far under the 1.0 ms mean budget.
- **Target search and rendering** did not change.

The full tables are in `Saved/Bench/<DataAsset>/` of the run.

### `unitshots -Stress 240 -Trace` (L_TestMap)

| Stress 240, with trace | Step 0 | Step 1 | Step 2 | Step 3 |
|---|---:|---:|---:|---:|
| M20 Guardian frame | 12.37 ms | 11.43 ms | 4.31 ms | 4.68 ms |
| `DA_TestTank` frame | 12.43 ms | 11.42 ms | 3.87 ms | 4.29 ms |

The obstacle shots log `the hull overlaps Block_North by 0 x 0 cm` and `the hull overlaps DA_TestVehicleFactory by 0 x 0 cm` for both units. The navmesh build figures and the Insights split are in [plan §7](SCALE_IMPLEMENTATION_PLAN.md#7-measurements-per-step).
