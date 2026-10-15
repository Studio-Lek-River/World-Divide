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
| Avoidance mean < 1.0 ms, max < 2.0 ms (200 mixed units) | Provisional target; the mixed scene is not in the bench yet | 240 moving M20s: 0.535 ms mean, 0.872 ms max | #134 |
| Target acquisition < 0.5 ms with 240 idle armed units | Provisional target, **not met** | 5.67 ms (M20), 5.50 ms (tank) | #134 |
| 240 `DA_TestTank`: game thread no worse than this baseline | Comparison | idle 8.16 ms, move 4.16 ms | #136 |
| 240 moving M20s ≤ 240 moving tanks | **Deferred** until after #150 and #151 | 17.41 ms against 4.16 ms | after #151 |
| GPU memory | **Not measured**: the bench records process physical memory only | — | — |

## Method

- **Units and grid:** for each count (1, 30, 120 and 240), that many of one unit type stand facing +X in a roughly square grid. The grid is spaced like a formation: hull length + 3 m along X, width + 2.5 m along Y.
- **Idle:** the units stand still for 3 s, then 120 frames are measured.
- **Move:** one move order goes to all of them through `Server_IssueCommand`, the player's own path, toward a point 60 m ahead. After 1 s, 120 frames are measured while they drive.
- **Game ms (max):** the slowest game-thread frame. For the move scenario it includes the frame of the order itself, so it shows the dispatch hitch.
- **Avoidance:** `URTSAvoidanceSubsystem::Separate`, timed per frame.
- **Target search:** the time all weapons spend in `FindNearestInRange`, summed per frame.
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
