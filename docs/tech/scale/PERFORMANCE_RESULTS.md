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
| No server frame > 33 ms on a 240-unit move order | **Met** on this PC at step 7: 20.27 ms (M20), 6.93 ms (tank) slowest frame; the order itself 1.8–2.1 ms. **Met** at step 4 at real scale: 30.12 ms (M20), 6.5 to 8.4 ms (tank) | 20.42 ms (M20), 19.76 ms (tank) slowest frame | #139, #136 |
| Avoidance mean < 1.0 ms, max < 2.0 ms (200 mixed units) | **Met** on this PC at step 2: 0.29–0.44 ms mean, 0.54–1.16 ms max in `WorldDivide.Avoidance.Mixed` (three runs) | 240 moving M20s: 0.535 ms mean, 0.872 ms max | #134 |
| Target acquisition < 0.5 ms with 240 idle armed units | **Met** on this PC at step 2: 0.013 ms (M20), 0.012 ms (tank) | 5.67 ms (M20), 5.50 ms (tank) | #134 |
| 240 `DA_TestTank`: game thread no worse than this baseline | Comparison; **measured** at step 4: idle 2.31 ms, move 4.44 ms median of five (+0.28 ms, inside the 0.6 ms run-to-run spread) | idle 8.16 ms, move 4.16 ms | #136 |
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

## Bench workflow (#167): quick runs for iterating, the long method as the gate

There are three methods. Their numbers live in `FRTSBenchPlan` (`Units/Debug/RTSBenchPlan.h`), and `WorldDivide.Debug.Bench.Plan` checks them, so the gate cannot change by accident.

| Method | Switch | Counts | Scenarios | Warm-up | Samples | Use |
|---|---|---|---|---|---|---|
| Long | `-Long` (alias `-Full`) | 1, 30, 120, 240 | idle, move | 10 s each | 60 s: three at 240, one below | **The performance gate.** Unchanged from #138 (step 6 below). |
| Quick | `-Quick` | 240 (or `-Counts`) | move | 3 s settle, 10 s after the order | one 30 s sample | **Iterating only. Never gate evidence.** |
| Short | none | 1, 30, 120, 240 | idle, move | 3 s, 1 s after the order | 120 frames | The step 0 to step 4 method, kept for continuity. |

- **Quick is the long scene, shortened.** It uses the same grid south-west of the middle, the same camera, and the same back-and-forth driving every 15 s. So its row is the same kind of number as the gate's 240-unit move rows. It writes the same CSV and table columns, and its table title says "diagnostic, not gate evidence".
- **A quick result never closes a budget.** A gate in the table above is judged only on long-method rows, run on a committed build.
- **Wall clock** on the development PC (2026-10-16), including the game's start and quit:

| Run | Wall clock |
|---|---:|
| `-Quick -Unit DA_TestM20Guardian` | 63 s |
| `-Quick -Unit DA_TestTank` | 58 s |
| `-Quick -Unit DA_TestM20Guardian,DA_TestTank`, one session | 104 s |
| `-Counts 240` short method, one set (unchanged) | 18 s |
| `-Long`, one set | about 14 min (#167) |

- **Quick against the gate**, 240 moving units, game thread mean (p95):

| Unit | Quick, alone | Quick, in a two-set session | Step 6 long method |
|---|---:|---:|---:|
| M20 Guardian | 6.16 (20.30) | 6.14 (19.75) | 6.49 (19.6) |
| `DA_TestTank` | 3.23 (4.57) | 3.34 (4.72) | 3.50 (4.93) |

  Quick lands inside the gate's spread. It is still one sample: the M20's slowest frame was 27.3 ms in one quick run and 33.8 ms in the next.
- **The long method was not re-run for #167.** Its code path is unchanged: the same numbers, now read from `FRTSBenchPlan` and checked by `WorldDivide.Debug.Bench.Plan`. A long run per unit type does not scale to a roster of over a hundred units; that is a follow-up.

### Several unit sets in one run

`-Unit` takes sets separated by `,`; `+` still joins the sizes of one mixed set. All sets run in one game session, one after another. Each set is its own run: its own camera, settings (saved and restored), warm-up, samples, statistics and `Saved/Bench/<set>/` folder. Its units are destroyed and garbage-collected, and the next set starts one second later. The order of the sets is the order given; each set's warm-up absorbs the first-use costs (shaders, streaming) that the first set pays. In the two-set quick run above, the tank, benched second, measured 3.34 ms against 3.23 ms alone. That is inside the spread.

### Stored baselines

Each run writes `bench.json` beside `bench.csv` with:
- **method:** name, version, counts, scenarios, warm-ups and samples;
- **scene:** map, unit set, resolution, render size, grid and camera;
- **machine:** CPU, GPU, driver, RHI and build configuration;
- **git:** commit, whether the worktree had changes, and the hashes of the bench code, `L_Bench` and the set's unit data;
- **session:** the sets of the run and its wall clock.

- **Storing.** `-SaveBaseline` stores each set's `bench.csv`, `bench.md` and `bench.json` under `Saved/Bench/Baselines/<commit>/<method>/<set>/`. It refuses to run on a worktree with changes, so a baseline is exactly its commit. Baselines stay on this machine (`Saved/` is not committed), because the numbers belong to the machine.
- **Selecting.** `-Compare` reads the run against the baseline of `HEAD`, the commit your uncommitted change sits on. `-Baseline <commit>` picks an older one. That commit must be `HEAD` or one of its ancestors, and the comparison is headed **OUTDATED** with the number of commits in between. There is no fallback: when the baseline is missing, the run does not start. It prints **NO BASELINE**, the stored baselines and the command that makes the missing one.
- **Validating.** Method, scene and machine must match field for field. On any difference, the run is printed but not compared, the differing fields are listed, and the command exits 1. Commit, worktree changes and content hashes are allowed to differ, since they are usually the change under test, but `compare.md` names each one.
- **Reading.** `compare.md` (in `Saved/Bench/<set>/`, also printed) gives each row's baseline → now and the difference for the frame, game thread (mean, p95, max), render, GPU, avoidance and locomotion. A game-thread difference beyond the 0.6 ms run-to-run spread is bold. One quick sample against one quick sample is a hint, not a result: confirm a gain or loss with the long method.
- **Regenerate a baseline** when `main` moves on and the new commit is your "before", when the GPU driver, GPU, CPU, RHI or build configuration changes, or when `FRTSBenchPlan::Version` is bumped (any change to a method's numbers or scene).

### Commands

```powershell
# Iterate: 240 moving M20s, one 30 s sample (diagnostic)
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Quick -Unit DA_TestM20Guardian

# Before a change, on a clean checkout of main: store quick and gate baselines of several sets
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Quick -Unit DA_TestM20Guardian,DA_TestTank -SaveBaseline
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -SaveBaseline

# While working: bench only "after" against HEAD's baseline
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Quick -Unit DA_TestM20Guardian,DA_TestTank -Compare

# The gate for the result, against HEAD's long baseline (or an older one, marked OUTDATED)
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -Compare
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Long -Unit DA_TestTank -Compare -Baseline c0d1387
```

### A second worktree for benching: not used

A second worktree could keep building and testing while the bench runs. It is not used, and benches stay serialized:
- **Contention.** The bench is CPU-bound: the game thread is the limit in every scenario with 120 units or more. A build in the other worktree runs UnrealBuildTool on every core, and the automation tests run a second engine process. Either would share the CPU, memory bandwidth and disk with the measured game, and move the numbers by far more than the 0.6 ms spread a step is read against.
- **Cost and sharing.** A second worktree needs its own `Binaries` and `Intermediate`, which is a full editor build. It would share the derived data cache and the LFS lock server with the first. Its `Saved/` and generated content would be separate, but its baseline could not be the main worktree's: a different build of the same commit.
- **What is left.** Quick runs cut the wait instead (above). Use the worktree only for work that needs no CPU, such as editing or reading code.

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

## Step 7 (#139, footprint formations, O(n log n) slots, idle-tick savings)

Same machine, build configuration and method as step 0, on 2026-10-15. "Before" is `main` at `f399471` (step 3) re-benched just before the change, in the same session.

What changed:
- **Formation spacing from the footprint.** Each unit takes its own width plus a side gap, each row is as deep as its longest unit plus a depth gap. The gaps grow with the unit: `clamp(width, 75, 250)` beside it and `clamp(length, 75, 300)` behind it, so real-size vehicles keep the approved 2.5 m and 3 m and infantry stands 75 cm apart. Units of about the same length form a size class with rows of their own (a new class below 75 % of the longest row pitch), so an M20 in the group no longer spreads every soldier to its spacing. The unit spawners use the same layout.
- **Assignment** sorts instead of the O(n³) closest-pair search: by size class, then front to back, then each row left to right.
- **Idle units** skip the ground trace (`SnapToGround` on the server, the tilt trace on clients) while they have moved no more than 1 cm since the last one, and the unit AI controllers no longer tick.

### Before and after, 240 units

| Unit | Scenario | Game ms before | Game ms after | Render ms before → after | Avoidance ms before → after |
|---|---|---:|---:|---:|---:|
| `DA_TestTank` | idle | 2.83 (3.51) | 2.13 (3.97) | 1.72 → 2.28 | 0.209 → 0.213 |
| `DA_TestTank` | move | 4.38 (15.48) | 3.99 (6.93) | 1.92 → 1.91 | 0.190 → 0.166 |
| M20 Guardian | idle | 2.74 (4.10) | 2.03 (2.67) | 4.48 → 3.97 | 0.250 → 0.258 |
| M20 Guardian | move | 21.07 (27.64) | 15.21 (20.27) | 8.33 → 7.48 | 1.119 → 0.272 |

At 120 units: tank idle 2.32 → 1.85 ms, move 3.04 (7.83) → 2.66 (4.54) ms; M20 idle 2.02 → 1.57 ms, move 9.37 (14.41) → 7.75 (11.01) ms.

- **The move-order hitch is gone.** The slowest frame of the 240-unit move fell from 15.5 to 6.9 ms (tank) and from 27.6 to 20.3 ms (M20), both under the 33 ms budget. A traced 240-unit bench move (`-Counts 240 -Trace`) puts the whole order, `Server_IssueCommand` with every unit's path request, at **2.07 ms** (tank) and **1.82 ms** (M20), of which `RTSCommands::PlanFormation` is 0.21 ms and 0.07 ms. `WorldDivide.Commands.Formation.Performance240` times the layout alone at 0.03 ms for 240 mixed units. The M20's remaining 20 ms is its render-state cost while it drives (the M20 render lane, #151 → #150 → #149).
- **Idle game thread** is 0.5–0.7 ms lower at 240 units: `ARTSUnit::TickOrders` no longer traces for units that stand still.
- **Moving M20s** cost less avoidance (1.12 → 0.27 ms): the old square pitch of 10 m left the 7.1 m hulls 2.9 m apart front to back and pushed them while they drove; the depth gap is now 3 m behind each hull and the rows do not overlap.

The full tables are in `Saved/Bench/<DataAsset>/` of the run.

### Formation sizes (`WorldDivide.Commands.Formation.Scenarios`)

Hulls as the current data gives them: infantry 31 × 70 cm, `DA_TestTank` 260 × 93 cm, M20 710 × 354 cm. Units start scattered; width is across the facing.

| Group | Width × depth |
|---|---|
| 30 infantry | 6 × 6 m |
| 30 tanks | 18 × 13 m |
| 30 M20s | 46 × 37 m |
| 30 mixed (12 infantry, 12 tanks, 6 M20s) | 22 × 26 m |
| 120 tanks | 34 × 34 m |
| 120 mixed | 46 × 42 m |
| 240 tanks | 47 × 49 m |
| 240 mixed | 64 × 69 m |
| 240 M20s | 124 × 118 m |

### `unitshots -Stress 240 -Trace` (L_TestMap)

| Stress 240, with trace | Step 0 | Step 1 | Step 2 | Step 3 | Step 7 |
|---|---:|---:|---:|---:|---:|
| M20 Guardian frame | 12.37 ms | 11.43 ms | 4.31 ms | 4.68 ms | 4.28 ms |
| `DA_TestTank` frame | 12.43 ms | 11.42 ms | 3.87 ms | 4.29 ms | 3.41 ms |

The Insights split is in [plan §7](SCALE_IMPLEMENTATION_PLAN.md#7-measurements-per-step).

## Step 4 (#136, real-scale data: units, buildings, weapons, maps, AI)

Same machine, build configuration and method as step 0, on 2026-10-16; L_Bench, its camera and the bench code are unchanged, so the bench now measures the real-size units in the same scene. The step 0 run-to-run spread is about 0.6 ms game thread and 1 ms slowest frame.

What changed: `DA_TestTank` grew from a 260 x 150 cm fit to 1037 x 370 cm (width binds), every other placeholder and building to the standard's size, weapon ranges about five times, speeds about two times, the generated maps three times (see [plan §7](SCALE_IMPLEMENTATION_PLAN.md#7-measurements-per-step)). The bench grid follows the hulls, so 240 tanks now cover about 135 x 135 m instead of 37 x 25 m.

### 240 units against the step 0 baseline

| Unit | Scenario | Step 0 game ms | Step 7 game ms | Step 4 game ms (five runs, median) | Slowest frame | Avoidance ms | Target search ms |
|---|---|---:|---:|---:|---:|---:|---:|
| `DA_TestTank` | idle | 8.16 (9.85) | 2.13 (3.97) | 2.31 (2.25 to 2.42) | 3.43 to 4.08 | 0.28 to 0.29 | 0.05 to 0.06 |
| `DA_TestTank` | move | 4.16 (19.76) | 3.99 (6.93) | **4.44** (4.24 to 4.77) | 6.53 to 8.38 | 0.28 to 0.33 | 0.000 |
| M20 Guardian | idle | 8.43 (10.71) | 2.03 (2.67) | 2.67 (one run) | 4.12 | 0.30 | 0.07 |
| M20 Guardian | move | 17.41 (20.42) | 15.21 (20.27) | 23.25 (one run) | 30.12 | 0.43 | 0.000 |

- **240 tanks, game thread against step 0 (#136 gate, measured):** idle is 72 % lower. Moving is 0.28 ms (7 %) above the 4.16 ms baseline at the median of five runs, inside the 0.6 ms run-to-run spread; the fastest run is 4.24 ms. The Insights split of a traced move (`-Counts 240 -Trace`, region `DA_TestTank_240_move`) puts the cost in hull size, not in new code: pawn `Movement` 1.36 ms and its `Collision` sweeps 0.66 ms per frame for 240 larger boxes, `URTSAvoidanceSubsystem::Separate` 0.32 ms (0.17 ms at step 7: a 10 m hull covers more 6 m cells), `ARTSUnit::TickOrders` 0.55 ms, path following 0.13 ms.
- **Slowest server frame on a 240-unit move order (provisional target 33 ms, measured):** 6.5 to 8.4 ms for tanks and 30.1 ms for M20s, both under it. The M20's margin is thin; its render-state cost is the M20 render lane (#151, #150).
- **240 moving M20s against 240 tanks:** deferred until after #150 and #151 (D-G); not judged here.
- The avoidance pass stays far under its 1.0 ms mean budget (0.28 to 0.43 ms).

### `unitshots -Stress 240 -Trace` (L_TestMap)

| Stress 240, with trace | Step 0 | Step 1 | Step 2 | Step 3 | Step 7 | Step 4 |
|---|---:|---:|---:|---:|---:|---:|
| M20 Guardian frame | 12.37 ms | 11.43 ms | 4.31 ms | 4.68 ms | 4.28 ms | 16.68 ms (game 5.13 ms, GPU 15.12 ms) |
| `DA_TestTank` frame | 12.43 ms | 11.42 ms | 3.87 ms | 4.29 ms | 3.41 ms | 6.10 ms (game 4.11 ms, GPU 4.51 ms) |

The stress grid now follows the hull (length + 3 m, width + 2.5 m, about square) on L_TestMap's north strip, so the whole 240-unit grid stands in front of the RTS camera, which still uses the pre-#137 values: more units are drawn than before, which is most of the GPU and frame rise. These numbers are not comparable with the earlier columns and are kept for continuity only; the bench above is the comparison.

## #162 (gun barrels kept out of obstacles)

Same machine, build configuration and method as step 0, on 2026-10-16, one run each; "Step 4" is the column above, `main` at `5df8edd`.

What changed: a turret's barrel (`FRTSUnitShape::Barrel`) is kept out of walls and buildings by turning the turret or raising the gun (`RTSBarrelClearance`, [vehicle collision §3.5](VEHICLE_COLLISION_AND_SELECTION.md#35-barrel-aim-not-navigation-162)). Sweeps run only while a navmesh wall comes within the barrel's reach plus its look-ahead, found by one cached `GetClearance` of a disc per turreted unit; the tank has no separate barrel and skips it.

| Unit | Scenario | Step 4 game ms | #162 game ms (max) | Avoidance ms step 4 → #162 |
|---|---|---:|---:|---:|
| `DA_TestTank` | idle | 2.31 | 2.39 (3.06) | 0.28 → 0.31 |
| `DA_TestTank` | move | 4.44 | 4.25 (9.13) | 0.28 → 0.26 |
| M20 Guardian | idle | 2.67 | 1.85 (2.88) | 0.30 → 0.21 |
| M20 Guardian | move | 23.25 | 14.42 (19.76) | 0.43 → 0.27 |

- Every row is within or under the step 0 run-to-run spread; the M20 rows are single runs, and their drop is spread, not a gain from #162. The bench grid stands in the open, so no barrel comes near a wall and the cost there is the cached reach check.
- The obstacle shots of `unitshots` log `the barrel overlaps ... by 0 cm now, 0 cm at most on the way` for the M20 at Block_North (head on, along, round the corner) and the factory, with the turret turned 45 degrees where it parks nose to the wall; the tank logs that its gun is part of its hull box.

### #162 follow-up: turret swing and obstacles left out of the navmesh

Same machine and method, 2026-10-16. Each run is a `rts.Debug.Bench DA_TestM20Guardian 240` on L_Bench, interleaved with `-dpcvars=rts.BarrelClearance=0` and `=1`.

What changed:
- The turret's swing to a clear aim is swept too (`RTSBarrelClearance::IsSwingClear`), only while the barrel is near an obstacle.
- An idle or moving turreted unit with nothing of the navmesh in reach makes one box overlap at its barrel's height (`IsAnythingInReach`). It runs on the reach cache, about once a second per unit, and the box is widened by a second's driving.

| M20 x 240 | Game ms, barrel clearance off (runs) | Game ms, on (runs) | Mean off → on |
|---|---|---|---:|
| `f03df50` idle | 2.43, 2.44, 2.34 | 2.25, 2.46, 2.30 | 2.40 → 2.34 |
| `f03df50` move | 19.69, 21.02, 19.48 | 17.61, 21.77, 21.92 | 20.06 → 20.43 |
| follow-up idle | 2.31, 2.20, 2.33, 2.23, 2.16 | 2.44, 2.19, 2.34, 2.49, 2.48 | 2.25 → 2.39 |
| follow-up move | 19.45, 18.05, 20.38, 17.61, 17.50 | 21.40, 17.30, 20.80, 20.46, 20.90 | 18.60 → 20.17 |

- **Within the spread.** Run to run, the move scenario spreads by about 4 ms (17.3 to 21.9 ms, with clearance on or off). The on/off difference of either version lies inside that spread.
- **No sweeps in the bench.** Temporary counters showed no overlap hits and no aim sweeps in the open grid. The cost there is the cached reach checks: 270 a second idle and, after widening the box, about the same driving.
- **Moving units no longer re-check five times a second.** Before the box was widened, a moving unit re-checked 5 times a second (1080 checks a second in total). That first A/B measured move at 17.0 and 17.7 ms off against 20.6 and 21.2 ms on. That is fixed.
- **The tank does not run barrel clearance.** `DA_TestTank` x 240 measured idle 2.33 ms and move 4.15 ms of game thread, against 2.39 and 4.25 ms before.

## #163 (same stop gap on every obstacle face)

Same machine and method, on 2026-10-16, one run each, `main` at `4181d3a` before and with the change.

**Cause.** `RTSHullClearance` read an obstacle as lying exactly the navmesh's erosion behind the navmesh's edge. `WorldDivide.Navigation.HullClearance.EdgeOffsets` measures where the edge actually lies off each collision face:

| Obstacle | +X, +Y faces | -X, -Y faces |
|---|---:|---:|
| Block_North, Wall_West (static meshes) | erosion + 0 cm | erosion + 25 cm (one cell) |
| Vehicle factory, other buildings (null-area boxes) | erosion + 0 cm | erosion + 0 cm |

The erosion is 200 cm for vehicles and 50 cm for infantry. Recast marks the voxel column a face lies in, so a mesh face that lies on a cell edge gains a whole cell on Recast's far side. Unreal hands geometry to Recast as (-X, Z, -Y), so that is Unreal's -X and -Y. Buildings cut the navmesh as area boxes, which Recast marks by cell centre, so they come out exact. Every face on the test map lies on the 25 cm grid. A face between grid lines gains 0 to 25 cm on either side. Path ends, the avoidance back-out and `MoveSnapStep` all read the same navmesh, so they inherited the extra cell; none of them causes it.

**Fix.** The navmesh stays the safe model. It only decides the last stretch when the true face may still be up to one cell further than it reads (`RTSHullClearance::GetNavUncertainty`, a cell plus the half-cell margin):
- When an order aims into an obstacle, the path end drives on along its last stretch while the hull's box, grown by 10 cm, overlaps nothing solid (`IsSolidClear`). That is one box overlap per step of a short bisection, once per path.
- The avoidance hull check lets the collision decide for a hull that reads between 10 cm clear and one uncertainty deep. Such a hull backs out, at least 5 cm per pass, only when its box grown by 2 cm overlaps something. Found clear, it is checked again after it has moved 2 cm.

**Stop gaps.** Hull to obstacle box, cm, `WorldDivide.Navigation.HullClearance.Drive` head on:

| Face | Before: M20 / tank / long / narrow | After: M20 / tank / long / narrow |
|---|---|---|
| Block_North +X | 37.8 / 37.1 / 39.3 / 40.2 | 37.8 / 37.1 / 39.3 / 35.8 |
| Block_North +Y | 37.8 / 37.1 / 39.3 / 40.2 | 37.8 / 37.1 / 39.3 / 35.8 |
| Block_North -X | 58.8 / 57.9 / 56.5 / 61.8 | 34.9 / 38.8 / 37.2 / 38.8 |
| Block_North -Y | 63.2 / 62.4 / 63.7 / 61.8 | 34.8 / 38.6 / 35.5 / 38.8 |
| Wall_West +X | 38.1 / 37.0 / 38.9 / 36.7 | 38.1 / 37.0 / 38.9 / 36.7 |
| Wall_West -X | 63.4 / 62.2 / 64.1 / 62.0 | 35.0 / 39.4 / 35.8 / 38.9 |
| Wall_West north end (+Y) | not driven | 38.0 / 37.8 / 38.8 / 37.4 |
| Wall_West south end (-Y), both corners at once | not driven | 38.0 / 37.8 / 38.8 / 38.9 |
| Vehicle factory, all four faces | 38.0 / 37.8 / 38.8 / 37.4 | 38.0 / 37.8 / 38.8 / 37.4 |

- **All faces now stop at 34.8–39.4 cm, against 36.7–64.1 before.** On the faces the navmesh read exactly, nothing changed. `WorldDivide.Smoke.MoveIntoObstacle`, back on all four faces of Block_North, stops tanks at 37, 37, 39 and 39 cm.
- **The residual spread between opposite faces is at most 4.6 cm, and it is not a matter of direction.** The same hull stops within a few cm on +X and -X. The spread comes from the end's 5 cm bisection step and from where path following stops inside its 25 cm acceptance radius, and that rest of about 25 cm is the same on every face.
- **No hull went inside an obstacle.** Before the change the closest a hull came on any drive (head on, along, around) was 4.2 cm; with it, 2.2 cm, on drives around a corner. The collision now keeps a 2 cm skin where the navmesh used to keep its half-cell margin. An earlier version that let the navmesh decide above zero let an M20 reach 4 cm into Wall_West's corner: there the navmesh's simplified edge cuts across the corner, so the collision now decides 10 cm above zero too.
- **Barrels:** `WorldDivide.Navigation.BarrelClearance.Drive` drives the new faces too, with no barrel deeper than 0.0 cm.

**240 units** (`ue.ps1 -Task bench -Counts 240 -Trace`):

| Unit | Scenario | Before game ms (max) | After game ms (max) | Avoidance ms before → after (max) |
|---|---|---:|---:|---:|
| `DA_TestTank` | idle | 2.44 (3.08) | 2.40 (3.19) | 0.31 → 0.31 (0.62 → 0.42) |
| `DA_TestTank` | move | 4.75 (8.58) | 4.85 (5.83) | 0.34 → 0.34 (0.54 → 0.57) |
| M20 Guardian | idle | 2.34 (3.33) | 2.11 (3.58) | 0.28 → 0.24 (0.75 → 0.80) |
| M20 Guardian | move | 20.56 (29.77) | 18.86 (28.84) | 0.39 → 0.38 (0.71 → 2.95) |

- **Every row is within the run-to-run spread.** The open bench grid has no obstacle near any hull, and the Insights capture shows no `RTSHullClearance::IsSolidClear` call at all. The game thread's `URTSAvoidanceSubsystem::Separate` averages 0.18 ms after against 0.21 ms before for the M20, and 0.24 against 0.25 ms for the tank. `GetClearance` and `AdjustPath` counts are unchanged.
- **The one 2.95 ms avoidance frame (M20 move) is a stall.** `GetClearance` and `AdjustPath` peak at 0.36 ms in the same capture against 0.035 ms before, and neither does more work than before there.
- **Near obstacles** the cost is one box overlap per frame for each hull within 10 cm of an obstacle on the navmesh (or up to one uncertainty inside it), and about five overlaps per move order into an obstacle.

## Step 5 (#137, camera retune)

Same machine and method, on 2026-10-16, one run each; "Before" is the #163 "after" column above.

What changed: the RTS camera's settings (field of view 60, arm 600 / 15000 / 45000, zoom steps a share of the arm, the tilt along the arm's logarithm; [plan step 5](SCALE_IMPLEMENTATION_PLAN.md#step-5-camera-retune)). The camera pawn does the same work per frame as before: one arm update and one downward trace. L_Bench views from its own fixed camera, so the bench sees the same scene as before.

| Unit | Scenario | Before game ms (max) | Step 5 game ms (max) | Avoidance ms before → after |
|---|---|---:|---:|---:|
| `DA_TestTank` | idle | 2.40 (3.19) | 2.30 (2.93) | 0.31 → 0.29 |
| `DA_TestTank` | move | 4.85 (5.83) | 4.98 (7.45) | 0.34 → 0.34 |
| M20 Guardian | idle | 2.11 (3.58) | 2.83 (3.86) | 0.24 → 0.34 |
| M20 Guardian | move | 18.86 (28.84) | 23.14 (28.27) | 0.38 → 0.46 |

- **The tank rows are within the run-to-run spread** (about 0.6 ms game thread).
- **The M20 rows are single runs in the M20's wide spread.** Its move scenario has measured 17.3 to 23.3 ms across the earlier runs (step 4: 23.25 ms; the #162 follow-up: 17.3 to 21.9 ms), and the camera change adds no work on that path. The slowest frame, 28.27 ms, stays under the 33 ms target.
- **`unitshots -Stress 240 -Trace`** (`DA_TestTank`): 3.84 ms frame, 3.84 ms game thread, 2.09 ms GPU, from the farthest zoom, which now shows the whole map; see [plan §7](SCALE_IMPLEMENTATION_PLAN.md#step-5-camera-retune-2026-10-16).

## Step 6 (#138, vehicle locomotion)

Same machine (Ryzen 7 7800X3D, RTX 4090, Development `-game` at 1920 × 1080), on 2026-10-16, with the **long method** #138 asks for (`ue.ps1 -Task bench -Long`):
- each scenario gets a 10 s warm-up, then 60 s samples: three at 240 units and one at 1, 30 and 120;
- the units keep driving: their move order goes back and forth every 15 s between the grid's place and a point 30 m west;
- the grid stands south-west of the middle, out of range of the enemy headquarters, so the match never ends during a run;
- the game thread's and the locomotion's 95th percentile are recorded next to the mean.

"Before" is this method on the same build with the pre-#138 data, in which every unit has legs and runs the old movement. "After" has the M20 and the tanks on tracks and the trucks and the builder on wheels. "Locomotion" is `URTSUnitMovement::TickLocomotion` summed over every unit in a frame: guidance, kinematics, collision checks and the hull's move itself.

### 240 units (mean of the three samples; p95 is the mean of the samples' 95th percentiles)

| Unit | Scenario | Game ms before (p95) | Game ms after (p95) | Locomotion ms after: mean (p95, max) | Avoidance ms before → after |
|---|---|---:|---:|---:|---:|
| M20 Guardian | idle | 2.54 (3.26) | 2.45 (3.13) | 0.055 (0.077, 3.14) | 0.33 → 0.30 |
| M20 Guardian | move | 5.98 (24.4) | 6.49 (19.6) | **0.585 (2.38, 4.91)** | 0.42 → 0.46 |
| `DA_TestTank` | idle | 2.47 (2.97) | 2.41 (3.18) | 0.062 (0.086, 1.33) | 0.34 → 0.31 |
| `DA_TestTank` | move | 3.75 (6.39) | 3.50 (4.93) | 0.486 (1.04, 4.51) | 0.49 → 0.57 |
| Mix (M20, tank, flak truck, builder) | idle | 2.39 (3.38) | 2.36 (2.98) | 0.067 (0.084, 0.70) | 0.28 → 0.26 |
| Mix (M20, tank, flak truck, builder) | move | 3.67 (9.09) | 4.41 (8.52) | 0.506 (1.33, 3.95) | 0.32 → 0.48 |

### 1, 30 and 120 moving units (one sample each)

| Unit | Game ms before → after at 1 / 30 / 120 | Locomotion ms after at 1 / 30 / 120: mean (p95) |
|---|---|---|
| M20 Guardian | 1.46 → 1.34 / 2.54 → 2.31 / 3.29 → 3.77 | 0.010 (0.020) / 0.104 (0.287) / 0.281 (1.13) |
| `DA_TestTank` | 1.38 → 1.31 / 1.69 → 1.62 / 2.61 → 2.47 | 0.008 (0.014) / 0.080 (0.150) / 0.273 (0.586) |
| Mix | 1.43 → 1.35 / 1.94 → 1.83 / 3.22 → 3.06 | 0.010 (0.020) / 0.097 (0.206) / 0.295 (0.653) |

### Against #138's budget

| Budget (240 moving vehicles) | Tank | Mix | M20 |
|---|---|---|---|
| Locomotion mean ≤ 1.0 ms | **Met** (0.49) | **Met** (0.51) | **Met** (0.59) |
| Locomotion p95 ≤ 2.0 ms | **Met** (1.04) | **Met** (1.33) | **Not met** (2.38), accepted by the producer on 2026-10-16 |
| Existing gates: no server frame > 33 ms on a 240-unit move order | Met (max 21.4 ms) | Met (22.6 ms) | Sample maxima 27.0 to 35.5 ms. The M20's single spikes were 32 to 35 ms before #138 too. Its p95 improved from 24.4 to 19.6 ms. |

**Where the M20's locomotion time goes.** The Insights capture of 240 moving M20s (`ue.ps1 -Task bench -Unit DA_TestM20Guardian -Counts 240 -Trace`) splits it:
- 78 % is `URTSUnitMovement::MoveHull`, the engine's move of the hull's root with the transforms of every component it carries: mesh, turret, gun, weapon station and instanced wheels.
- 2 % is guidance (`RTSLocomotion::Guide`).
- 2 % is the collision checks (`IsPoseClear`, 41,520 calls against 4,719 navmesh clearance queries).
- The rest is the kinematics and bookkeeping.

The old movement paid the same transform update twice per frame, once for the floating pawn's move and once for `SetActorRotation`, outside any locomotion scope. That is why the M20's game-thread p95 dropped.

Making the M20's parts cheaper to move belongs with its LODs and parts work (#150, #151).

**Other observations:**
- **Avoidance** costs 0.04 to 0.16 ms more while moving. Vehicles now slow down for and steer round one another instead of only being pushed, and stay in contact a little longer.
- **No per-frame allocation.** The path is copied to the gear once per path, and the bench's sample arrays are reserved before measuring.
- **No vehicle-pair pass** was added; avoidance's fixed-cell grid is unchanged.

### Replication (`WorldDivide.Net.Vehicle.ListenServer`)

The test plays a listen server and one client in PIE at a fixed 30 Hz step, with 240 tanks driving back and forth. It measures the server's net driver per game second:

| Run | Bytes / game s | Packets / game s |
|---|---:|---:|
| Before #138 (legs data, same test) | 70,380 | 84 |
| After, `rts.Net.LocomotionState 0` | 69,548 | 83 |
| After, gear state on | 70,403 | 84 |

- **The gear state adds 1.2 %**, inside #138's 10 %. It is `FRTSLocomotionNet`: 4 bytes, sent only when the yaw rate, steering or status changes.
- **The engine's 100 KB/s `MaxClientRate`** is not reached per game second. Because the fixed-step test runs faster than real time, it may cap the rate per real second, as it would at the real-scale bench's 240 always-relevant units.

**Convergence under 100 ms round trip and about 2 % packet loss** (`WorldDivide.Net.Vehicle.Dedicated`, dedicated server, two clients): after a stop, a reverse, a pivot and a drive into Block_North, both clients rested on the server's pose within 0.01 s of the rest reaching them, at most 0.07° and 0.00 cm apart.
