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
| Screen | `-Screen` | 240 (or `-Counts`) | move | 3 s settle, 10 s after the order | one 15 s sample | **The full-roster gate's first tier (#171).** Escalates to the long method; never gate evidence on its own. |
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
- **The long method was not re-run for #167.** Its code path is unchanged: the same numbers, now read from `FRTSBenchPlan` and checked by `WorldDivide.Debug.Bench.Plan`. A long run per unit type does not scale to a roster of over a hundred units; #171 adds the gate for that (below).

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

# Among solids the navmesh leaves out (#172): the same set with the suffix @Solids, in its own folder
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Quick -Unit DA_TestM20Guardian@Solids

# The gate for the result, against HEAD's long baseline (or an older one, marked OUTDATED)
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -Compare
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Long -Unit DA_TestTank -Compare -Baseline c0d1387
```

### A second worktree for benching: not used

A second worktree could keep building and testing while the bench runs. It is not used, and benches stay serialized:
- **Contention.** The bench is CPU-bound: the game thread is the limit in every scenario with 120 units or more. A build in the other worktree runs UnrealBuildTool on every core, and the automation tests run a second engine process. Either would share the CPU, memory bandwidth and disk with the measured game, and move the numbers by far more than the 0.6 ms spread a step is read against.
- **Cost and sharing.** A second worktree needs its own `Binaries` and `Intermediate`, which is a full editor build. It would share the derived data cache and the LFS lock server with the first. Its `Saved/` and generated content would be separate, but its baseline could not be the main worktree's: a different build of the same commit.
- **What is left.** Quick runs cut the wait instead (above). Use the worktree only for work that needs no CPU, such as editing or reading code.

## Performance gate for a full roster (#171)

The long method costs about 13.5 minutes per unit set. For over a hundred unit types, that is about 23 hours. The gate therefore benches every unit cheaply and only some with the long method. It decides which sets from what changed, and it never takes a disturbed run for a measurement. The long method itself, its budgets and the `-Quick`, `-Long` and `-Compare` commands are unchanged.

### The three tiers

| Tier | What runs | Method | Evidence |
|---|---|---|---|
| 1. Screen | Every unit the change can reach, one set each | **Screen**: the long scene, 240 moving units, a 10 s warm-up and one 15 s sample | Diagnostic. A clear screen says "nothing suspicious"; it is no gate evidence for that unit on its own. |
| 2. Representatives | A few units per class, and one mixed set | **Long** (#138), unchanged | **The gate.** PASS and FAIL come from these runs. |
| 3. Change-based selection | Decides tiers 1 and 2 from the files changed since the baseline commit | — | A shared change reaches every unit; one unit's data reaches that unit. |

`ue.ps1 -Task gate` runs all three:
1. It lists the changed files and plans with the `RTSBenchGate` commandlet, which writes `Saved/Bench/Gate/plan.json`.
2. It screens the planned units in one game session.
3. It judges the screens. A screen that was not a valid measurement runs once more; a suspicious one is escalated.
4. It benches the representatives, the mixed set and the escalated units with the long method, in one session.
5. It judges everything against `Saved/Bench/Baselines/<commit>/` and the budgets, and writes `gate.md` and `gate.json`.

The decision logic is `RTSBenchGate` (`Units/Debug/RTSBenchGate.h`). Its automation tests feed it synthetic runs: `WorldDivide.Debug.Bench.Gate.*`.

### Representative units

`RTSBenchGate::SelectRepresentatives` reads the roster from the unit data under `/Game/Data/Units`. Each unit gets a class (tracked, wheeled, infantry, air), its parts (hull, turret, gun, vehicle parts), LOD0 triangles, footprint and every `/Game` package it is made of. For each class it picks:
- **the pinned units**, `DA_TestM20Guardian` and `DA_TestTank`, so the history in this file stays comparable;
- **the costliest unit**: by its last valid screen when the whole class has one, otherwise by LOD0 triangles times parts;
- **the costliest unit of each size**, when the class spans more than one: under 4 m², 4 to 20 m², over 20 m² of footprint.

- **a unit with a long baseline in a near tie**: when the costliest by the screen is less than 0.3 ms (or 10 % of triangles times parts) ahead of a unit that has a valid long baseline, the gate keeps that unit. The first end-to-end run showed why. Once the screen baselines existed, the flak truck read 2.58 ms against the builder's 2.43 ms, and the representative switched to the truck, which had no long baseline. Every gate would then be INCONCLUSIVE until the next nightly run.

The mixed set takes one representative of each class: a pinned one first, then one with a long baseline. Today's roster gives five representatives: `DA_TestM20Guardian`, `DA_TestTank`, `DA_TestBuilder`, `DA_TestGrenadier` and `DA_TestJet`. Its mixed set is `DA_TestM20Guardian+DA_TestBuilder+DA_TestGrenadier+DA_TestJet`. A roster of 120 units in four classes gives at most twelve representatives plus the pinned ones (`WorldDivide.Debug.Bench.Gate.Representatives`).

**A representative subset covers kinds of cost, not every unit.** A unit benched only with the screen has no gate evidence of its own. The screen is what catches a unit that costs far more than its class's representative, and it escalates that unit to the long method.

Aircraft run in the bench: the jet takes off and flies by its own rules, so fewer of them stay in the camera's view than ground units. Its game thread is measured like any other. At 6.9 ms for 240 moving jets, the jet is the costliest unit of the screen.

### Change-based selection

`RTSBenchGate::ClassifyPath` gives each changed file a scope:

| Changed | Scope | Benched |
|---|---|---|
| `docs/`, `*.md`, `Source/WorldDivide/Tests/`, `Tools/`, maps other than `L_Bench`, the gate's own `RTSBenchGate*` | nothing the bench runs | none |
| `Content/Data/Units/<Unit>.uasset` | that unit | its screen, plus the long method if it is a representative or in the mixed set, or once the screen escalates it |
| Any other `Content/` package (mesh, material, texture, animation) | every unit made of it, from the asset registry's dependencies | as one unit's data, for each of them |
| A `Content/` package no unit is made of | shared: code may load it (a game mode, the HUD, a shared material) | broad |
| `Source/` (movement, navigation, avoidance, combat, rendering, components), `Config/`, `Plugins/`, `UI/`, `*.uproject` | shared | **broad**: every unit screened, every representative and the mixed set long |
| `RTSBenchCommand.cpp`, `RTSBenchPlan.h`, `L_Bench` | the bench itself | broad, and `gate.md` asks for every baseline to be regenerated |

- **A shared change benches more than a change to one unit's data**: all 9 units and 6 long sets today, against 1 screen and at most 2 long sets.
- **An unchanged unit asset is no proof that its cost is unchanged.** Any code, config or shared-content change makes the gate broad. The nightly or milestone run (`-Scope broad`) benches every unit whatever changed.
- **Content hashes.** `bench.json` now hashes every content file the set's units are made of (the packages of the last `plan.json`), and `compare.md` names each one that changed.

### Baseline compatibility

A run is judged only against a baseline of the same method, scene, machine and engine. `RTSBenchGate::CompareMetadata` compares bench.json's `method`, `scene` and `machine` field by field, and the engine version of the `run` section. Any difference makes the set INCONCLUSIVE and lists the fields. A baseline must also be a valid measurement itself. A baseline stored before #171 has no validity record, so the gate does not read against it; `-Compare` still does. Baselines live under `Saved/Bench/Baselines/<commit>/<method>/<set>/`, as in #167.

### Contamination detection

Every bench run checks its own samples (`RTSBenchGate::CheckRun`) and writes the result to bench.json's new `run` section, with `valid` and `flags`. bench.csv gains the columns `sample_s`, `max_frame_ms`, `stall_frames` and `freeze_frames` at its end; older CSVs still parse.

| Check | Limit | Catches |
|---|---|---|
| Sample length | at least 98 % of the planned length | interrupted or cut-short samples |
| Frame rate | at least 10 fps over the sample | #167's disturbed run, 66 frames in 30 s (2.2 fps) |
| Freezes | no frame over 250 ms | the process descheduled or paging |
| Stalls | at most 3 frames over 100 ms | repeated hitches from outside the game. A normal sample's slowest frame is 5 to 23 ms. |
| Samples agree | the samples of one scenario within 0.6 ms game thread | intermittent load during one sample. A regression in the code moves all samples alike. |
| Other load | other processes under 1.0 core on average while the set ran | another game, a build, a test run, a virus scan beside the bench |
| Shader compilation | no `ShaderCompileWorker` while the set ran | first-use shader compiles inside a sample |
| Unexplained slowdown | a frame 1.5 times slower than the baseline's must be at least half explained by the game, render, RHI or GPU time | time lost outside the game, such as GPU contention from another program |
| Setup | no Unreal Insights trace, a Development build, a result for every set | trace overhead, a wrong build, a failed run |

- **ue.ps1 measures the other load.** While the game runs, it reads the CPU time of every other process every 5 s, leaving out the game and itself. It writes the mean and peak cores that overlap each set's run into that set's bench.json (`environment`). On the development PC, background load while benching is 0.65 to 0.78 cores (VS Code, the terminal, the browser).
- **`-SaveBaseline` refuses** a run that is not valid or that ran beside other load. It prints `BASELINE REFUSED`, names the reasons and exits 1, so a contaminated run is never stored.
- **A real regression is not contamination.** The checks look at the environment and the run's own consistency, never at the difference to the baseline alone. A run whose frames got slower because its game thread did is valid, and it fails. `WorldDivide.Debug.Bench.Gate.Contamination` covers both cases.
- **Measured on the real game** (2026-10-18). A screen of `DA_TestTank` ran beside 14 busy-loop processes. The game's own checks found nothing: 4,125 frames, no stalls. Its game thread read 3.62 ms against about 2.8 ms on an idle machine, which would look like a +0.8 ms regression. The environment record showed 10.1 cores of other load, so the run is flagged and neither stored nor judged as a measurement.

### PASS, FAIL and INCONCLUSIVE

| Outcome | When |
|---|---|
| **PASS** | Every required long run is valid, within the budgets below, and no more than 0.6 ms game thread above its valid, compatible baseline in any scenario. Every screen is valid and clear, or its unit's long run passed. |
| **FAIL** | A valid long run violates a budget, even without a baseline, or is more than 0.6 ms game thread above its baseline. |
| **INCONCLUSIVE** | A run is invalid, missing or interrupted; a baseline is missing, incompatible or not itself valid; or a screen stayed invalid after its rerun. **Never a pass.** |

The budgets the gate reads are the approved ones, unchanged. They apply at 240 units, mean of the samples:
- **Move order:** the slowest game-thread frame of the move scenario is under 33 ms.
- **Locomotion:** mean ≤ 1.0 ms and p95 ≤ 2.0 ms, for sets with a tracked or wheeled unit.
- **Avoidance:** mean under 1.0 ms.

The replication budget (`WorldDivide.Net.Vehicle.*`), the formation and path-search budgets (`WorldDivide.Commands.Formation.Performance240`, `WorldDivide.Navigation.LongPath.Cost`) and the avoidance test budgets stay with their automation tests. Run them with `ue.ps1 -Task test`.

The screen never fails, because it is no gate evidence. It escalates a unit to the long method when:
- its game thread is more than 0.3 ms above its screen baseline (half the gate's 0.6 ms, see the statistics below);
- its locomotion reaches 80 % of a budget;
- its slowest frame passes 33 ms;
- or it has no valid, compatible screen baseline.

`gate.md` gives the scope, every change and its scope, each set's verdict with its reasons, the checks it passed, and a **Next** list: the exact command to rerun, to trace or to store a missing baseline. `gate.json` has the same for CI. ue.ps1 exits 0 PASS, 1 FAIL and 4 INCONCLUSIVE.

### Statistics: can a shorter sample screen?

**Method.** Five separate game sessions (2026-10-18, commit 226068d's code, the development PC). Each benched M20 and `DA_TestTank` at 240 moving units in the long scene. Each had one 60 s sample after the usual 10 s warm-up, with every frame written (`-Frames -SampleSeconds 60`). The windows are the first 15, 30, 45 and 60 s of each sample. They are multiples of the scene's 15 s move leg, because a window that ended mid-leg would weigh driving and standing frames differently. The spread is the largest minus the smallest session mean of the game thread.

| Unit | Window | Session means (game ms) | Mean | SD | **Spread** | p95 spread |
|---|---|---|---:|---:|---:|---:|
| M20 | 15 s | 3.95, 3.93, 3.96, 3.94, 3.93 | 3.94 | 0.012 | **0.03** | 0.22 |
| M20 | 30 s | 4.01, 3.98, 4.01, 4.01, 4.00 | 4.00 | 0.014 | **0.03** | 0.22 |
| M20 | 45 s | 4.07, 4.04, 4.08, 4.08, 4.07 | 4.07 | 0.016 | **0.04** | 0.17 |
| M20 | 60 s | 4.09, 4.06, 4.12, 4.12, 4.14 | 4.11 | 0.031 | **0.08** | 0.17 |
| `DA_TestTank` | 15 s | 2.76, 2.81, 2.78, 2.78, 2.76 | 2.78 | 0.019 | **0.05** | 0.11 |
| `DA_TestTank` | 30 s | 2.83, 2.83, 2.87, 2.81, 2.81 | 2.83 | 0.022 | **0.06** | 0.12 |
| `DA_TestTank` | 45 s | 2.84, 2.84, 2.89, 2.84, 2.86 | 2.85 | 0.022 | **0.05** | 0.09 |
| `DA_TestTank` | 60 s | 2.87, 2.87, 2.91, 2.87, 2.87 | 2.87 | 0.017 | **0.04** | 0.07 |

Other load in those sessions: 0.65 to 0.78 cores.

- **A 15 s sample keeps the game thread's run-to-run spread at 0.05 ms or less, far within the 0.6 ms requirement.** The screen method uses it.
- **Shorter windows read lower.** The mean rises with the window (M20 3.94 → 4.11 ms from 15 to 60 s), probably as the grid spreads over the first legs. A screen is therefore read only against a screen baseline, never against quick or long rows: `method.sample_s` differs, so `CompareMetadata` refuses the pair.
- **The screen does not replace the long method.** It replaces nothing in the gate. It is a cheaper first tier than the quick method's 30 s, with the same spread.
- **The escalation threshold is 0.3 ms, not the measured 0.05 ms.** Five sessions in one evening do not show how the machine varies from day to day, so the threshold keeps a margin and stays half the gate's 0.6 ms.
- **The long method's own spread** is within 0.13 ms between the three 240-unit samples of every stored long run. Five long runs of different commits from #168 to #174 put M20 move at 4.12 to 4.21 ms. No repeated long runs were made for #171: the long method is unchanged.

### Wall clock

Measured on the development PC (2026-10-18):

| Run | Wall clock |
|---|---:|
| Screen of all 9 units, one session (`-Task bench -Screen -Unit <9 units>`) | 280 s: about 29 s per unit and 20 s of start-up |
| Long method, per set | about 812 s (13.5 min) |
| Planner or judge (`RTSBenchGate` commandlet) | about 9 s per call, three calls per gate |
| Gate on a docs-only change (`-Task gate`, nothing to bench) | 18 s: the planner and the judge, PASS |
| Broad gate of today's roster: 9 screens and 6 long sets (below) | 87.2 min: 280 s of screens, 4,841 s of long runs, the rest planning and judging |

**Estimate for a roster of 100 units**, from those figures:

| Change | Work | Wall clock |
|---|---|---:|
| Docs, tests, tools | none | under 1 min |
| One unit's data or meshes | 1 screen, plus up to 2 long sets if it is a representative or in the mix | 1 to 28 min |
| Shared code, config or content | 100 screens (49 min) and the representatives plus the mixed set: 9 long sets (122 min) to 13 (176 min) | **about 3 to 3.7 hours** |
| Each escalated unit | one more long set | +13.5 min |

That is against about 23 hours for a long run of every unit type. The broad gate stays one evening's or one night's run, and most changes are cheaper.

### Recommended use

- **While developing:** the quick or screen method against HEAD's baseline (`-Compare`). It is fast and diagnostic.
- **Before committing a change:** `-Task gate` against the baseline of the commit it sits on. Only the sets the change reaches run.
- **Nightly or after a merge to `main`:** `-Task gate -Scope broad -SaveBaseline` on a clean checkout. It judges against the previous baseline (`-Baseline <commit>`) and stores every valid run as the new commit's baseline.
- **Milestone acceptance:** `-Task gate -Scope broad -Baseline <the last milestone's commit>`. It benches every representative and screens every unit against the last milestone. Escalated units get their long runs. Add a long run (`-Task bench -Long -Unit ...`) of every unit changed since that milestone, and the full `-Task test`.
- **Regenerate the baselines** after a change to the bench itself (`gate.md` says so), a new GPU driver, engine or machine, and after the screen and long baselines of a new `main` commit are needed.

```powershell
# Gate a change against the baseline of the commit it sits on
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task gate

# Gate every unit against an older baseline (OUTDATED by the commits in between)
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task gate -Scope broad -Baseline 226068d

# Nightly on a clean main: judge against the last night's commit and store this commit's baselines
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task gate -Scope broad -Baseline <last night's commit> -SaveBaseline

# Store the baselines by hand on a clean checkout: screens of every unit, the long method of the representatives
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Screen -Unit DA_TestAbrams,DA_TestBuilder,DA_TestFlakTruck,DA_TestGrenadier,DA_TestHarvester,DA_TestInfantry,DA_TestJet,DA_TestM20Guardian,DA_TestTank -SaveBaseline
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Long -Unit DA_TestBuilder,DA_TestGrenadier,DA_TestJet,DA_TestM20Guardian,DA_TestTank,DA_TestM20Guardian+DA_TestBuilder+DA_TestGrenadier+DA_TestJet -SaveBaseline

# Method study: every frame of a 60 s sample (never a baseline)
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Quick -Unit DA_TestM20Guardian,DA_TestTank -Frames -SampleSeconds 60
```

### End to end (2026-10-18)

1. **Baselines.** On a clean checkout of 226068d (the gate's first commit): the screens of all 9 units (`-Task bench -Screen ... -SaveBaseline`) and the long method of the 6 planned sets (`-Task bench -Long ... -SaveBaseline`). Every run was valid, with 0.75 to 0.82 cores of other load, and stored.
2. **Broad gate.** On 353993f: `-Task gate -Scope broad -Baseline 226068d`. Every changed file read "nothing the bench runs", but `-Scope broad` benches every unit anyway. **PASS** in 87.2 minutes.

The screens against their baselines, about 1.5 hours apart, game thread at 240 moving units:

| Unit | Baseline → gate | | Unit | Baseline → gate |
|---|---:|---|---|---:|
| `DA_TestAbrams` | 2.81 → 2.86 (+0.05) | | `DA_TestInfantry` | 2.70 → 2.67 (-0.03) |
| `DA_TestBuilder` | 2.43 → 2.49 (+0.06) | | `DA_TestJet` | 6.86 → 6.87 (+0.01) |
| `DA_TestFlakTruck` | 2.58 → 2.51 (-0.07) | | M20 Guardian | 3.99 → 4.01 (+0.02) |
| `DA_TestGrenadier` | 2.81 → 2.74 (-0.07) | | `DA_TestTank` | 2.81 → 2.77 (-0.04) |
| `DA_TestHarvester` | 2.54 → 2.51 (-0.03) | | | |

Every unit is within 0.07 ms of its baseline, so all 9 unit types fall inside the five-session spread above.

The long method, 240 units, mean of three samples (game thread, p95 locomotion where it applies):

| Set | Move: baseline → gate | Idle: baseline → gate | Slowest move frame | Locomotion p95 |
|---|---:|---:|---:|---:|
| M20 Guardian | 4.23 → 4.24 | 1.95 → 1.99 | 21.6 ms | 1.21 ms |
| `DA_TestTank` | 2.95 → 2.98 | 2.07 → 2.10 | 20.4 ms | 1.00 ms |
| `DA_TestBuilder` | 2.54 → 2.55 | 1.88 → 1.89 | 18.5 ms | 0.93 ms |
| `DA_TestGrenadier` | 2.73 → 2.71 | 1.86 → 1.87 | 18.5 ms | — |
| `DA_TestJet` | 6.96 → 6.96 | 1.75 → 1.77 | 23.0 ms | — |
| Mixed set | 4.22 → 4.24 | 1.87 → 1.93 | 19.2 ms | 0.54 ms |

- **Every scenario at every count is within 0.12 ms**: the long method's run-to-run spread, measured across 6 sets.
- **The bench changes did not move the long method.** 226068d's M20 and tank baselines read 4.23 and 2.95 ms moving and 1.95 and 2.07 ms idle. Their #168 baselines (a4e8364) read 4.17, 2.92, 1.94 and 2.04 ms, all within 0.06 ms.
- **The jet's game thread is the roster's highest** at 240 moving units, 6.96 ms, more than the M20's 4.24 ms. It flies by its own rules, so its locomotion figure is 0 and its cost sits in the aircraft component. This is worth its own look once more aircraft join the roster.

### Remaining limitations

- **The representatives are a heuristic.** A unit whose cost is unusual but under 0.3 ms over its own screen baseline is covered only by its class's representatives. The milestone run should bench every changed unit with the long method.
- **The other-load check sees processes, not the GPU.** GPU contention shows only as a slowdown the game's times do not explain. Protected system processes the user cannot read are not counted, and a process that starts and ends between two samples is missed.
- **The 1.0-core limit sits about 0.25 cores above the measured background.** A busy editor or browser beside the bench can mark a run invalid. The gate then runs the screen once more and reports INCONCLUSIVE rather than a pass.
- **One machine.** Every figure is the development PC's, and baselines never cross machines.
- **The screen threshold has one evening's evidence** (five sessions). Repeat the five-session study after a driver or hardware change.
- **Content dependencies include data references.** `DA_TestBuilder` lists 75 packages, because it refers to the buildings it can build. A change to a building therefore screens the builder too; this is conservative, not wrong.

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
| Locomotion p95 ≤ 2.0 ms | **Met** (1.04) | **Met** (1.33) | **Not met** (2.38), accepted by the producer on 2026-10-16; **met** after #165 (1.18, see below) |
| Existing gates: no server frame > 33 ms on a 240-unit move order | Met (max 21.4 ms) | Met (22.6 ms) | Sample maxima 27.0 to 35.5 ms. The M20's single spikes were 32 to 35 ms before #138 too. Its p95 improved from 24.4 to 19.6 ms. |

**Where the M20's locomotion time goes.** The Insights capture of 240 moving M20s (`ue.ps1 -Task bench -Unit DA_TestM20Guardian -Counts 240 -Trace`) splits it:
- 78 % is `URTSUnitMovement::MoveHull`, the engine's move of the hull's root with the transforms of every component it carries: mesh, turret, gun, weapon station and instanced wheels.
- 2 % is guidance (`RTSLocomotion::Guide`).
- 2 % is the collision checks (`IsPoseClear`, 41,520 calls against 4,719 navmesh clearance queries).
- The rest is the kinematics and bookkeeping.

The old movement paid the same transform update twice per frame, once for the floating pawn's move and once for `SetActorRotation`, outside any locomotion scope. That is why the M20's game-thread p95 dropped.

Making the M20's parts cheaper to move belongs with its LODs and parts work (#150, #151). #165 cut the running gear out of the hull's move (below).

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

## #165 (M20 hull move: running gear placed once a frame)

Same machine and build configuration as step 6, on 2026-10-16. "Before" is `d7ec54f`, stored as a long baseline (`-SaveBaseline`). "After" is the #165 commit, run with `ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -Compare -Baseline d7ec54f`; the comparison is marked OUTDATED by the one commit under test. The Insights figures come from `-Counts 240 -Trace` captures of both builds, exported with `UnrealInsights -ExecOnAnalysisCompleteCmd` (`ExportTimerCallees`, `ExportTimingEvents`).

### Baseline and bottleneck

- **The before figure had moved since step 6.** At `d7ec54f`, 240 moving M20s measured a locomotion mean of 0.51 ms and a p95 of 1.83 to 1.87 ms, against step 6's 0.59 ms and 2.38 ms, so the p95 was already just inside the 2.0 ms budget. The commits in between (#163, #164) changed driving and stopping.
- **Locomotion has two modes.** In the frames where all 240 units drive, it costs 1.69 ms; in the frames where they stand, 0.08 ms. The mean is low because the bench's units rest for most of each 15 s leg. The p95 is the cost of a driving frame.
- **Of a driving frame, `MoveHull` is 1.48 ms**: 6.2 µs per unit, spent carrying the 10 components the root box holds. The three instanced wheel sets (road wheels, idlers, sprockets) took 1.9 µs of that, 0.8 µs of it in `UInstancedStaticMeshComponent::CalcBoundsImpl`. The root box took 1.4 µs, the hull 1.0 µs, the turret, gun and weapon station 0.9 µs, and the tracks 0.4 µs.
- **The wheel sets were updated twice per driving frame.** They were carried by the hull's move, then rewritten by `ARTSUnit::UpdateRunningGear` with `bMarkRenderStateDirty`. That flag rebuilt the render proxy of every set of every M20 each frame: `DeferredRenderUpdates_GameThread` cost 5.28 ms per driving frame, with `UStaticMesh::GetUsedMaterials` and texture-streaming registration on every rebuild. This sat outside the locomotion figure but was the M20's largest game-thread cost while driving.
- **No redundant root move.** There is one `MoveHull` per unit per frame. Collision and overlap work is small (`UpdateOverlaps` 0.2 µs per move).

### What changed

- **The wheel sets are placed in world space.** They stay attached to the hull, so they show, hide and take the team colour with it. They are absolute (location, rotation, scale) at the world's origin, and the engine skips absolute children in `UpdateChildTransforms`, so the hull's move no longer carries them.
- **`UpdateRunningGear` writes each wheel's world transform** (roll × rest × hull) once a frame, whenever the wheels rolled or the hull moved for any reason: drive, push, tilt, fire kick or server correction.
  - It writes without a render-state rebuild: the ISM's instance data manager sends the changed transforms to the renderer.
  - It then updates the set's bounds and render transform.
  - A standing unit costs one transform comparison.
  - The per-call `TMap` and arrays are gone.
- **`URTSRunningGearSubsystem`** runs that update for every vehicle with wheels in one tick function in `TG_PostUpdateWork`. That is after the actor ticks and after avoidance's pushes (a tickable), and before the frame's render updates, so the wheels are never a frame behind the hull.
- `GetPartTransform` computes a wheel from the hull as it stands, so gameplay and tests never read a stale instance.
- The tests are `WorldDivide.Units.RunningGear.FollowsHull` (new) and `WorldDivide.Units.RunningGear.Spin`. `WorldDivide.Units.CollisionMatchesHull` now steps the gear after grounding, as the frame does.

### Before and after, 240 units, long method (three 60 s samples)

| Unit | Scenario | Game ms (p95) before → after | Locomotion ms before → after: mean (p95) | Render ms before → after |
|---|---|---|---|---|
| M20 Guardian | move | 5.33 (14.47) → **4.19 (8.06)** | 0.52 (1.85) → **0.44 (1.18)** | 4.84 → 4.26 |
| M20 Guardian | idle | 1.92 (2.27) → 1.93 (2.29) | 0.05 (0.07) → 0.05 (0.07) | 4.38 → 4.37 |
| `DA_TestTank` | move | 2.93 (4.16) → 2.92 (4.12) | 0.44 (0.97) → 0.44 (0.96) | 1.80 → 1.80 |

At 30 and 120 moving M20s, the game thread went from 2.09 to 1.58 ms and from 3.08 to 2.56 ms, and the locomotion p95 from 0.27 to 0.19 ms and from 0.88 to 0.61 ms. The tank has no wheel sets and is unchanged within the spread. The slowest sample frames of the M20 move rows went from 18.5–23.6 ms to 17.2–21.0 ms.

**Insights, per driving frame of 240 M20s (`-Counts 240 -Trace`):**

| | Before | After |
|---|---:|---:|
| Locomotion (`TickLocomotion`) | 1.69 ms | **0.92 ms** |
| `MoveHull` | 1.48 ms (6.2 µs per unit) | **0.75 ms (3.1 µs per unit)** |
| `DeferredRenderUpdates_GameThread` | 5.28 ms | **2.29 ms** |
| Running gear (`UpdateRunningGear`) | about 0.9 ms, in the actor tick | about 1.0 ms, in `URTSRunningGearSubsystem::PlaceAll` |
| Render thread frame (mean over the capture) | 4.95 ms | 4.26 ms |

### Against the budget

| Budget (240 moving M20s) | Before (`d7ec54f`) | After |
|---|---|---|
| Locomotion mean ≤ 1.0 ms | Met (0.52) | **Met (0.44)** |
| Locomotion p95 ≤ 2.0 ms | Met by 0.15 ms (1.85); not met at step 6 (2.38) | **Met (1.16–1.19)** |
| Game thread no worse | — | **1.1 ms better in mean, 6.4 ms better at p95** |

### Remaining limitations and later work

- **The root box is now the largest part of a move** (about 1.1 µs of the 3.1 µs): the physics body's transform, as every unit pays. Then come the hull, and the turret, gun and weapon station, which must stay separate for their articulation.
- **`DeferredRenderUpdates` still costs 2.3 ms per driving frame of 240 M20s.** This is the render-transform update of the 7 components that still move with the hull (box, hull, two tracks, turret, gun, weapon station) and of the 3 wheel sets. Fewer components per vehicle is a matter for #150 and #149, not #165.
- **#151 (track animation):** the tracks are untouched. `UpdateRunningGear` already owns the wheels' roll each frame and runs after every hull move, so it is the place to drive the tracks' scroll in step with the wheels.
- **#149 (LODs):** the wheel sets keep their per-instance LOD selection; their bounds follow their instances.
- **#150 (materials):** no material change; each set keeps its team-colour material instances.
- **Motion vectors of the wheels:** they now come from instance updates instead of a proxy rebuilt each frame. Nothing smeared in the checks so far; a close look on a vehicle whose wheels show (the M20's skirts hide most of its own) is part of the manual check.

## #168 (hulls stopped at solids the navmesh leaves out)

Same machine and build configuration as #165, on 2026-10-17. "Before" is `a4e8364` (the #165 follow-up HEAD), stored as a long baseline (`-SaveBaseline`). "After" is the #168 change, run with `ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -Compare`. The comparison names `RTSBenchCommand.cpp` as changed: it only gained the solid counters below.

### Root cause and what changed

- **Why hulls went through.** `URTSUnitMovement::IsPoseClear` asked the collision only within 10 cm of a navmesh wall. A wall or prop whose navigation is off does not cut the navmesh, so the hull read as clear and drove in.
- **Broad phase.** Each vehicle keeps an `FRTSSolidNeighbourhood`: a single `OverlapMultiByObjectType` of a level box round the hull, as wide as its half diagonal plus a margin (max of 200 cm or one second of driving). It keeps only components that block pawns and are not navigation relevant. The ground, ramps, blocks and buildings are left to the navmesh. The neighbourhood is reused until the hull leaves the margin or about a second passes, staggered per unit. The kept list is inline, so normal driving allocates nothing.
- **Narrow phase.** Only where the neighbourhood holds a solid: `OverlapComponent` of the hull's real box against those components alone.
  - The box is tilted to the ground normal and starts at the agent's step height, so slopes and ramps stay traversable.
  - A hull already in a solid may only go no deeper (`ComputePenetration`).
  - A step longer than the hull's half width is checked on the way too, so a thin wall cannot be jumped.
- **Movement integration.** The check runs inside `IsPoseClear`, so the gear's existing blocked shares, pivots and back-off stop it. Realized motion and track/wheel travel stay what was driven, and #165's running gear is untouched. Avoidance drops a vehicle push that would end in such a solid. The navmesh-band `IsSolidClear` box now also follows the ground's slope.

### Before and after, 240 units, long method (three 60 s samples)

| Unit | Scenario | Game ms (p95) before → after | Locomotion ms before → after: mean (p95) |
|---|---|---|---|
| M20 Guardian | move | 4.17 (7.85) → 4.21 (7.86) | 0.44 (1.15) → 0.45 (1.16) |
| M20 Guardian | idle | 1.94 (2.33) → 1.94 (2.34) | 0.05 (0.07) → 0.05 (0.07) |
| `DA_TestTank` | move | 2.92 (4.12) → 2.98 (4.22) | 0.44 (0.97) → 0.46 (1.01) |

Every row is inside the run-to-run spread of about 0.6 ms. At 1, 30 and 120 moving M20s the locomotion p95 is 0.02, 0.18 and 0.62 ms, as before.

### Solid queries (from the bench's new "Solids" table, 240 moving units, per frame)

| Unit | Broad overlaps | Reused | Narrow checks | Broad ms | Narrow ms |
|---|---:|---:|---:|---:|---:|
| M20 Guardian | 0.83 | 99.4% | 0 | 0.0056 | 0 |
| `DA_TestTank` | 0.60 | 99.7% | 0 | 0.0055 | 0 |

A broad overlap costs about 7 µs. L_Bench has no solids that navigation leaves out, so no narrow check runs there; standing units make no query at all.

### Against the budget

| Budget (240 moving M20s) | Before (`a4e8364`) | After |
|---|---|---|
| Locomotion mean ≤ 1.0 ms | Met (0.44) | **Met (0.44–0.45)** |
| Locomotion p95 ≤ 2.0 ms | Met (1.15) | **Met (1.15–1.16)** |

### Collision results (`WorldDivide.Navigation.VehicleOffNavmesh`, 30, 60 and 120 Hz)

- **Flat-out drives at a 1 m wall that navigation leaves out.** The M20 approaches head-on, at 30° and from the far side; the placeholder tank head-on; the Abrams head-on and at -30°; the flak truck head-on. A turret is turned to the rear first, so the barrel does not stop the hull (#162). Every hull stops 0.3–12.9 cm short, never inside.
- **A 10 cm wall at top speed:** the M20 and the truck stop 1.3–4.3 cm short, with no tunnelling.
- **Pivot beside a wall 15 cm off the side:** turns until a corner meets it, never more than 1 cm in, and holds.
- **Along a wall 30 cm off the side:** arrives, never more than 1 cm in.
- **A group of six M20s past a pillar:** no hull more than 1 cm into it.
- **Over the ridge:** the Abrams and the truck still climb it and arrive.
- **The neighbourhood itself:** open ground and Block_North give no solid; the prop is found. The neighbourhood expires on moving past the margin or after its lifetime. A tilted box on the ridge's ramp is clear where a level one would cut in.
- **`WorldDivide.Navigation.VehicleBlocked`** now drives into a real wall that navigation leaves out, instead of the removed `TestObstacles` stand-in.

### Remaining limitations

- **A gun over the front stops a hull at the barrel's reach** when the turret cannot turn aside, as #162 designed. The 0–50 cm gap applies to the hull itself.
- **Narrow-phase cost is not measured at scale:** L_Bench has no such solids. Measured since in #172's obstacle scene (below).
- **No Insights capture for #168.** The bench counters time the broad and narrow phases directly; the trace scopes `FRTSSolidNeighbourhood::Update` and `::IsClear` are in place for one; #172 captured them.
- **A nav-relevant prop placed at runtime** relies on the navmesh rebuild, as before: up to the existing 1 s cache expiry.

## #169 (long paths over the search node limit)

Same machine and build configuration as #168, on 2026-10-17. Measurements come from `WorldDivide.Navigation.LongPath.Cost` (L_LongPath, fixed 30 Hz step, `-nullrhi`), and the gate from `ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -Compare -Baseline a4e8364`. That is #168's "before" baseline, marked OUTDATED because it is one commit behind HEAD.

### Root cause and what changed

- **Why long orders were refused.** Recast's A* stops at the query filter's node limit, `RECAST_MAX_SEARCH_NODES` (2048). A path that runs out of nodes comes back partial with `DidSearchReachedLimit()` set. `RequestMove` and `RTSMoveDestination::FindReachable` disallow partial paths, so the order was refused as if the goal were cut off.
- **Telling the two apart.** A partial path *without* that flag means the search ran out of navmesh before it met the goal, which proves the goal unreachable (another island, an enclosure). A partial path *with* the flag only means the search ran out of nodes.
- **Strategy: one bounded retry** (`RTSPathSearch::FindFullPath`). The first search is unchanged, so ordinary routes cost what they cost before. Only a search that ran out of nodes runs once more, with a copy of its filter raised to `rts.Nav.MaxSearchNodes` (default 16384; Detour's ceiling is 65534). The result is one of:
  - **Full**: the path is used.
  - **Unreachable**: proven; the order is refused as in #164.
  - **Inconclusive**: even the raised limit ran out. The order is refused with a Warning naming the CVar, never reported as a proven dead end.
- **Where it applies.** It covers every move of an `ARTSUnitAIController` (`FindPathForMoveRequest`): move, attack-move, queued legs, builders and harvesters. It also covers each candidate of `RTSMoveDestination::FindReachable` and of `RTSApproach::FindReachableSpot`. A move's path keeps the raised limit for the repaths the navigation system runs when a building changes the navmesh (`KeepLimitOnRepath`), so a long repath does not end partial for lack of nodes.
- **Diagnostics** (`LogWorldDivide` Verbose): the first search running out of nodes, the retry, a full path found by it, and a proven unreachable goal. A search that exhausts the maximum limit logs a Warning. Ordinary searches log nothing.
- **Memory.** Each navmesh's game-thread query grows its Detour node pool to the raised limit on its first retry and keeps it: about 0.9 MB of nodes plus 0.13 MB of open list at 16384 nodes, per agent navmesh.

### Search costs (`WorldDivide.Navigation.LongPath.Cost`, 20 searches each)

| Agent | Route | Outcome | Searches | Nodes visited | Worst ms | Mean ms |
|---|---|---|---:|---:|---:|---:|
| Infantry | short trip (50 m) | full | 1 | 5 | 0.028 | 0.003 |
| Infantry | long route (503 m) | full | 2 | 3,579 | 0.781 | 0.501 |
| Infantry | island (proven unreachable) | unreachable | 2 | 3,616 | 0.553 | 0.441 |
| Vehicle (tank) | short trip | full | 1 | 5 | 0.009 | 0.002 |
| Vehicle (tank) | long route (510 m) | full | 2 | 5,693 | 1.092 | 0.788 |
| Vehicle (tank) | island (proven unreachable) | unreachable | 2 | 5,739 | 0.940 | 0.697 |

- **Peak for simultaneous orders.** One group order sends 12 units (6 infantry, 6 tanks) across: 11.1 ms on the server, against 0.16 ms for the same group on the short trip. Each unit searches twice.
- **Search budget.** The maximum limit reached is 16384, and L_LongPath's whole connected navmesh fits under it (5,739 nodes for the vehicle agent), so every answer there is conclusive.
- **#164's budget holds.** `WorldDivide.Navigation.MoveIntoObstacle.Cost` worst search is 0.055–0.064 ms per hull, within its 5 ms budget; L_TestMap never reaches the limit.

### Movement bench, 240 units, long method (three 60 s samples, a4e8364 → #169)

| Unit | Scenario | Game ms (p95) before → after | Locomotion ms before → after: mean (p95) |
|---|---|---|---|
| M20 Guardian | move | 4.17 (7.85) → 4.12 (7.65) | 0.44 (1.15) → 0.44 (1.13) |
| M20 Guardian | idle | 1.94 (2.33) → 1.93 (2.27) | 0.05 (0.07) → 0.05 (0.07) |
| `DA_TestTank` | move | 2.92 (4.12) → 2.99 (4.27) | 0.44 (0.97) → 0.45 (1.00) |

Every row is inside the run-to-run spread of about 0.6 ms. L_Bench's short moves never reach the node limit, so they run one search as before. The locomotion budget (mean ≤ 1.0 ms, p95 ≤ 2.0 ms) is still met.

### Test coverage

- **`WorldDivide.Navigation.LongPath.Search`**, for each agent's navmesh:
  - The engine's default search on the long route runs out of nodes, so the map still tests what it should.
  - `FindFullPath` finds the route on its second search, and the short trip needs one search.
  - The enclosure and the island come back unreachable.
  - With the limit cut to the default, the long route is reported inconclusive (with its Warning).
  - A move into the middle of `Wall_Baffle` finds a spot.
- **`WorldDivide.Navigation.LongPath.Drive`**, with the existing obstacle, reversal, arrival and refusal checks:
  - Infantry, the tracked tank and M20, and the wheeled flak truck each move, and then attack-move, across. All arrive, and none enters a wall or pillar.
  - A group of six closes in on the goal.
  - A tank's queued chain (a short trip, the long route, the island, a final leg) drives every reachable leg and skips the island with one refusal.
  - Groups sent into the enclosure and onto the island stay put with one refusal each.
- **`WorldDivide.Navigation.LongPath.Cost`**: the table above, with a 5 ms budget per search and 30 ms for the group order.

### Remaining limitations

- **The retry is synchronous on the game thread.** About 0.5–1 ms per unit on a long route here, so a group order of 12 costs about 11 ms in that frame and a much larger selection costs proportionally more. A shared path per group, or time-slicing the retries over frames, would bound that if real maps show it.
- **A connected navmesh larger than `rts.Nav.MaxSearchNodes` polygons** can still give an inconclusive answer for a far or cut-off goal. It is logged as a Warning and the order is refused; raise the CVar (at most 65534).
- **Vehicles got stuck rounding the ends of thin (2 m) walls**, at the corner of an L of thin walls, or beside a thin wall end with navmesh on both sides. This was found while building L_LongPath and is fixed by #173 (below). L_LongPath keeps its 8 m walls.
- **A repath that comes back partial because the goal really became unreachable** is still accepted by the engine and ends the move short; reporting that belongs to #170.

## #173 (vehicles rounding thin walls)

Same machine and build configuration as #169, on 2026-10-17. The gate is `ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -Compare -Baseline a4e8364`, against the same OUTDATED baseline as #169.

### Root cause and what changed

Reproduced on L_ThinWalls (a free-standing 2 m wall and a 2 m L junction, `setup_testmap.py`), on a 2 m copy of L_LongPath's first layout, and on L_TestMap, whose `Wall_East` is 2 m thick.

- **Clearance read inside out at the start of a session** (`RTSHullClearance`). Which side of a navmesh wall is the navmesh was learned from the first clearance query whose hull centre lay clear of the walls. Until then it was taken to be +1, while our navmeshes are -1. `AdjustPath` checks a corner with the hull centre on the navmesh's vertex, so it never learned it, and the first paths of a session were fitted inside out: the M20, about 1 m clear of the free wall's end, read 2 m deep, and its path corner moved 2 m toward the wall. The leg into it ran the hull's nose into the wall's end; the M20 backed off, paths again and switched between forward and reverse four times. The side is now found once, a cell to either side of the first wall long enough: two navmesh projections per process.
- **Tracks arced through sharp corners** (`RTSLocomotion::Guide`). §4 of `VEHICLE_LOCOMOTION.md` has tracks stop and pivot at corners sharper than 30°; the pivot only started beyond `PivotAngle` (60°). A thin wall's end has two corners 6 m apart (2 m wall plus 2 m erosion each side), less than an M20 or tank is long: the arc from the first swung wide, and the hull cut toward the second across the wall's top. Tracks now pivot onto the way on after such a corner.
- **A blocked arc had no way on** (`URTSUnitMovement::CommitStep`). Riding along the wall's top about 20 cm off it, the turn back onto the path swings the hull's rear into it, and every fallback kept that turn. A blocked step now tries its distance straight on before the smaller shares.
- **Backed off, tracks drove back into the same corner** (`URTSUnitMovement`). From a standstill both tracks speed up alike, so after `UpdateBlockedMove`'s back-off a tank set off straight on its old heading. On L_TestMap a tank pushed by its group against `Wall_East`'s end gave its move up this way (`WorldDivide.Smoke.ListenServer`). Tracks now pivot to face their new path after a back-off.

Not reproduced on HEAD before the change: the 17–22 cm group penetration and the 43–49 cm stops at the junction that #173 reports. The group drives below stay within the 2 cm tolerance.

### Movement bench, 240 units, long method (three 60 s samples, a4e8364 → #173)

| Unit | Scenario | Game ms (p95) before → after | Locomotion ms before → after: mean (p95) | Avoidance ms |
|---|---|---|---|---|
| M20 Guardian | move | 4.17 (7.85) → 4.18 (7.77) | 0.44 (1.15) → 0.45 (1.14) | 0.38 → 0.40 |
| M20 Guardian | idle | 1.94 (2.33) → 1.91 (2.24) | 0.05 (0.07) → 0.05 (0.07) | 0.24 → 0.23 |
| `DA_TestTank` | move | 2.92 (4.12) → 2.90 (4.08) | 0.44 (0.97) → 0.45 (0.96) | 0.47 → 0.47 |

Every row is inside the run-to-run spread of about 0.6 ms, and the locomotion budget (mean ≤ 1.0 ms, p95 ≤ 2.0 ms) is met. The new checks run only on blocked steps, after a back-off or past a sharp corner; on L_Bench's open ground none of them runs, and the solid-collision counters are unchanged (broad overlaps 0.56–0.59 per frame at 240 moving tanks, 99.7 % reused).

### Test coverage

- **`WorldDivide.Navigation.ThinWalls.FirstFit`**: with the walls' side not yet known, as in a new session, the M20's path round the free wall's end keeps its corner, and the hull on it reads clear.
- **`WorldDivide.Navigation.ThinWalls.Drive`**, from a session's first hull fit, with the obstacle, reversal (at most two switches), arrival and refusal checks: the M20, the tank and the flak truck each round the free wall's end both ways, in a hairpin, and round the junction both ways; a group of six rounds the end on one order. All arrive, none comes more than 2 cm into a wall, and no move is given up.
- `WorldDivide.Navigation.HullClearance.*`, `MoveIntoObstacle.*`, `LongPath.*`, the vehicle movement, avoidance and `WorldDivide.Net.Vehicle.*` tests, and the full run: 243 of 243 pass.

## #174 (path-search cost of large group orders)

Same machine and build configuration as #173, on 2026-10-17. Measurements come from `WorldDivide.Navigation.LongPath.Cost` (L_LongPath, fixed 30 Hz step, `-nullrhi`). The movement gate is `ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestTank -Compare -Baseline a4e8364`, against the same OUTDATED baseline as #169 and #173.

### Baseline (f4925cd, before)

Each group order ran every unit's path search and hull fit in the frame of the order. These numbers come from timers added to `Server_IssueCommand`, `RTSPathSearch::FindFullPath`, `RTSHullClearance::AdjustPath` and the destination search for one measuring run. The timers were then taken out again. The mixed group takes infantry, the tracked tank and M20, and the wheeled flak truck in turn.

| Group | Route | Order frame ms | Searches | Search ms (wasted 2048-node first pass) | Hull fit ms |
|---|---|---:|---:|---:|---:|
| 12 mixed | long | 10.9 | 24 | 9.3 (2.9) | 1.5 |
| 50 mixed | long | 48.6 | 100 | 41.2 (12.3) | 6.7 |
| 100 mixed | long | 109.3 | 200 | 85.0 (24.9) | 23.0 |
| 100 infantry | long | 76.8 | 200 | 66.3 (24.9) | 9.4 |
| 100 tanks | long | 121.7 | 200 | 85.7 (23.5) | 34.6 |
| 100 flak trucks | long | 106.4 | 200 | 88.2 (24.0) | 16.9 |
| 100 mixed | short trip | 0.7 | 100 | 0.23 | 0.08 |

- Every unit had its path in the order's frame (0 frames of latency), so the whole cost landed in a single frame.
- The worst single search was 1.28 ms. Nodes visited were 3,579 (infantry) and 5,693 (vehicle) on the long route.
- **Root cause.** Each member searched alone across the pillar-filled pocket. Each search also ran twice, because the first 2048-node pass always ran out (about 28 % of the search time). On top of that came a per-unit hull fit, and all of it was synchronous inside `RTSCommands::Dispatch`.

### What changed

- **Budgeted move requests** (`URTSMoveRequestSubsystem`, server only, one tickable for the world):
  - `ARTSUnit::StartCommand` queues the move or attack-move of its front order there instead of searching its path at once.
  - A request runs straight away while the frame has budget left, so short orders start in their own frame as before. The rest run oldest first in the next frames, and the issuing players (an AI or a null issuer is a player of its own) take turns.
  - **Budget.** `rts.Nav.MoveRequestBudgetMs` defaults to **2.0 ms** of path work (search plus hull fit) per frame. A request starts only while the frame's spend plus the costliest request it ran so far fits the budget. Every frame with requests waiting runs at least one, so each order gets its result.
  - **A request is never split.** A frame therefore runs over by at most one request that alone costs more than the estimate. This is not a guarantee that no frame exceeds the budget: a single search or hull fit costs what it costs, and on a larger map one 16k-node search can cost several ms. Bounding that needs sliced Detour queries or the engine's asynchronous path finding, which is multithreaded work outside this issue. A request over the budget is logged (`LogWorldDivide` Verbose).
  - **While a request waits.** The unit stops its old move (a vehicle brakes, and a back-off finishes). The order stays at the front of its queue, `NotifyMoveFinished` and `UpdateAttackMoveOrder` leave it alone, and a back-off's pivot onto the new path waits for that path.
  - **Stale requests.** A new order, a Stop, a dropped order, a destroyed unit or the end of the match makes the waiting request stale through the unit's ticket, and it is skipped at no cost. A request runs its result exactly as before: moving, at the goal (the order is done), or refused.
- **Node-limit hint** (`RTSPathSearch::FRaisedStart`). Requests of one group order share a set of navmeshes on which a search ran out of the default 2048 nodes. Later members on those navmeshes start at the raised limit, which gives the same path and outcome without the wasted first search. `FindFullPath` outside a hint behaves as in #169.
- **One refusal per group** (`FRTSCommand::OrderId`, server only and `NotReplicated`). `Dispatch` gives a group's units one id, and `ARTSPlayerController::NotifyOrderRefused` tells each order once on top of the 1 s cooldown, so a refused group whose searches take longer than the cooldown still gives one message. Refusals of stuck vehicles (#170) are unchanged.
- **No new replication, RPCs or per-unit ticks.** The decision stays on the server, and clients see the units start through movement replication.

### After (`WorldDivide.Navigation.LongPath.Cost`)

| Group | Route | Order call ms | Path work ms | Frames | Peak frame ms (spent before its last request) | Worst request ms | Longest wait | Searches (started raised) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 12 mixed | long | 1.84 | 8.8 | 6 | 1.81 (0.81) | 1.00 | 5 frames, 0.17 s | 14 (10) |
| 50 mixed | long | 2.11 | 36.5 | 23 | 2.06 (1.23) | 1.30 | 22 frames, 0.73 s | 52 (48) |
| 100 mixed | long | 1.82 | 82.3 | 49 | 3.42 (1.25) | 3.32 | 48 frames, 1.60 s | 102 (98) |
| 100 mixed | short trip | 0.72 | 0.63 | 1 | 0.63 | 0.04 | 0 frames | 100 (0) |

- **Peak per frame.** Before, 100 units on the long route cost 109–122 ms in one frame. Now no frame starts a request past 1.25 ms of spend; frames end at about 2 ms, and the order's own call takes 1.8–2.1 ms. The peak frame of the 100-unit order is 3.4 ms, once in 49 frames: three vehicle requests at the back of the formation cost 2.1–3.2 ms each alone, reproducibly.
- **Total work** dropped by about a quarter (82 against 109 ms for 100 mixed), because 98 of 102 searches start raised: one search per unit, plus one wasted pass per navmesh.
- **Latency.** The first two units start in the order's frame. The whole group has its paths after 5 frames (12 units), 22 frames (50) and 48 frames (100) at the 30 Hz test step, which is 0.17 s, 0.73 s and 1.6 s. At 60 Hz the frame counts are the same and the times halve. Short orders of 100 units still start completely in their own frame.
- **Whole frames** in the test, with every unit and the editor ticking, peak at 14–36 ms for 12–100 units under way. That is steady-state locomotion and avoidance of the moving group, not path work, which the table above separates.

### Movement bench

240 units, long method (three 60 s samples, a4e8364 → #174). L_Bench's orders are short moves, which all run in their order's frame.

| Unit | Scenario | Game ms (p95) before → after | Game max | Locomotion ms before → after: mean (p95) | Avoidance ms |
|---|---|---|---|---|---|
| M20 Guardian | move | 4.17 (7.85) → 4.18 (7.84) | 17.6 → 18.5 | 0.44 (1.15) → 0.45 (1.17) | 0.38 → 0.39 |
| M20 Guardian | idle | 1.94 (2.33) → 1.94 (2.29) | 17.5 → 17.6 | 0.05 (0.07) → 0.05 (0.07) | 0.24 → 0.23 |
| `DA_TestTank` | move | 2.92 (4.12) → 2.88 (4.08) | 18.2 → 18.3 | 0.44 (0.97) → 0.44 (0.96) | 0.47 → 0.47 |

Every row is inside the run-to-run spread of about 0.6 ms. The locomotion budget (mean ≤ 1.0 ms, p95 ≤ 2.0 ms) is still met, and the overall game-thread and locomotion budgets are unchanged.

### Gameplay checks

- A group of **100 vehicles** across L_LongPath jams at the pocket's mouth and in the 12 m gap. Some hulls come up to 60 cm into pillars, two or three units never finish, and one gives up its move. This happens the same with the budget turned off (`rts.Nav.MoveRequestBudgetMs 0`, the old synchronous behaviour). It is crowd collision at that density, not path search, and is left for a follow-up. The 100-unit drive test uses infantry.

### Test coverage

- **`WorldDivide.Navigation.LongPath.Cost`**: the searches of #169, and the table above. It asserts:
  - no frame starts a request past the budget;
  - the peak frame stays within the budget plus one request;
  - the worst request is within 5 ms;
  - the order's call is within the budget, one request and 1 ms of queuing;
  - every unit drives a full path on its own agent's navmesh to its own formation slot;
  - at most one wasted search per navmesh;
  - a short group order runs completely in its own frame.
- **`WorldDivide.Navigation.LongPath.GroupDrive`**: 100 infantry move across and close in on the far side; twelve mixed hulls attack-move on the short trip and, Shift-queued, across. Both carry the drives' obstacle, arrival and refusal checks.
- **`WorldDivide.Navigation.LongPath.Pending`**, with the budget cut to one request a frame:
  - 50 infantry sent onto the island are all refused and stay put, and the player hears of it once over 1.6 s of refusals;
  - a Stop makes 30 waiting requests stale and nobody moves;
  - orders replaced on consecutive frames leave only the last order's paths;
  - units destroyed while waiting are skipped and the rest get their paths;
  - a Shift-queued leg waits behind its group's first leg;
  - a player's and an AI's groups ordered at once start in turn.
- **`WorldDivide.Net.Vehicle.ListenServer` and `.Dedicated`** (100 ms round trip, about 2 % loss, two clients): both players order 30 vehicles at once with the budget cut to one request a frame, and team 0 re-orders on the next frame. Both groups start in turn, team 0 heads for its second order's slots, every unit drives a full path, no refusals, and every client sees all 60 under way.
- **Robustness:** all 51 `WorldDivide.Smoke.*` tests also pass with the budget cut to one request a frame.
- `LongPath.Search`, `LongPath.Drive` and `MoveIntoObstacle.*` are unchanged and pass. The full run passes 245 of 245.

### Remaining limitations

- **One request cannot be split.** A vehicle's request here costs up to 3.3 ms. A larger map, a 16k-node search or a long hull fit can exceed the budget in its frame, and bounding that needs sliced or asynchronous path finding.
- **Other moves still search at once:** attack orders on a unit or a building (`UpdateAttackOrder`), harvesters, builders, and the engine's repaths when a building changes the navmesh. A 100-unit attack order on a far target would still spike.
- **Latency grows with the group.** About 0.5 frames per long-route vehicle at 2 ms, so the back of 100 vehicles starts about 1.6 s after the front at 30 Hz. A shared route per group and agent (#174's option A) would shorten that.
- **The budget is real time,** so on a slower machine more requests wait. The simulation's timing then depends on CPU speed, as with any real-time budget.

## #179 (AIvsAI independent of the real-time move budget)

Same machine, on 2026-10-18. The code is in 498df5f. The measurements below were taken with #181 on top, because the match only ends with a winner since then.

### Root cause and what changed

- **Machine-dependent schedule.** #174's `URTSMoveRequestSubsystem` spends a real-time budget (`rts.Nav.MoveRequestBudgetMs`, 2 ms) of path work per frame. Under the fixed 1/30 s test step, a faster or busier CPU therefore ran a different number of move requests per frame. The same test then played a different match each run: `WorldDivide.Smoke.AIvsAI` failed 1 run in 4 on `4a2a706` and passed with the budget off.
- **Under `-RTSTestFixedStep`:**
  - the requests run `rts.Nav.MoveRequestsPerFrame` at a time (`URTSMoveRequestSubsystem::TestRequestsPerFrame`, 4) instead of the ms budget;
  - the fixed step applies from the first PIE frame;
  - navmesh rebuilds finish in the frame they start (`RTSTests::CompleteNavigationBuild`).
  
  The engine's step, the request count and the navmesh hook are restored when PIE ends.
- **The vehicle network tests keep real time** (`RTSTests::UseRealTimeStep`). They never took the fixed step themselves, and their checks run against real-time network emulation (100 ms round trip, packet loss). Once every PIE session started at the fixed step, game time outran the emulated lag: `WorldDivide.Net.Vehicle.Dedicated` saw only 1 to 17 of 60 vehicles under way on the clients, in every run. They now run in real time again, as before 498df5f.
- **Production is unchanged.** `rts.Nav.MoveRequestsPerFrame` defaults to 0, which keeps the 2 ms real-time budget outside the tests.
- **Stalled AI build orders and waves:**
  - the AI passes over build-order steps nothing it has left can make, such as builders once its headquarters is gone (`WorldDivide.AI.BuildOrderOutOfReach`);
  - an army that can no longer grow goes once the wave interval has passed (`WorldDivide.AI.WavesOutOfCredits`).
  
  #181 later weighs those waves against the enemy's defences.
- **`ue.ps1 -Task test -Repeat N`** runs a filter N times, each in a fresh editor. It prints each run's result and wall-clock time, plus the tests' info lines that differ between runs. `WorldDivide.Smoke.AIvsAI` reports the match length and an end-state digest (every unit's position to the centimetre and every building's health), so identical runs show as identical lines.

### Determinism

- **Before #181,** the default setting played the same match on every run. The end-state digest `f550da9a` was identical across separate runs, and every one was the stand-off that #181 resolves.
- **With #181,** `ue.ps1 -Task test -Filter WorldDivide.Smoke.AIvsAI -Repeat 10` passes 10 of 10 in 302 s wall-clock, 30 s each. Every run reports the same match (352 game seconds, digest `5cd35b8e`).
- **Each `rts.Nav.MoveRequestsPerFrame` value is its own fixed trajectory** (#181's table). That is expected: the request count changes when units start their paths.

### Remaining limitations

- Determinism holds for the fixed-step tests, not for real-time play. With the 2 ms budget a slower machine still runs fewer requests per frame, as #174 notes.

## #181 (AIvsAI matches that end with both sides bankrupt)

Same machine, on 2026-10-18. Every run is `WorldDivide.Smoke.AIvsAI` on L_SkirmishDuel under the fixed 30 Hz test step of #179 (`-RTSTestFixedStep=30`, `-nullrhi`). A setting other than the default is passed with `ue.ps1 -Task test -Filter WorldDivide.Smoke.AIvsAI -Cvars rts.Nav.MoveRequestsPerFrame=N`. Each setting plays one fixed match.

### Root cause

L_SkirmishDuel has one 5000 supply source per base and 10000 starting credits per side. Both sides run dry after about 300 game seconds. In the stand-offs one side had already won the field and destroyed the other's headquarters, depot and factory. A tower (81 m range, 500 health) and the power plant beside it were left. The power plant keeps its team in the match, because defences alone do not.

- **Waves spent on the tower.** Wave units attack-moved to the nearest building and shot the power plant or whatever else stood nearest. Meanwhile the tower, which outranges rifles (35 m) and tanks (65 m), destroyed them one by one. In one diagnosed run, 19 units fell to a tower that lost 160 health. #179's rule that an army which can no longer grow goes once the interval has passed then sent the last one or two units into it as well.
- **Build-order steps without income.** An AI whose depot was destroyed waited for a 1500 depot with 650 credits and no harvester at work, instead of training the army it could still pay for.
- **Supplies out of reach.** Idle harvesters only look within their 200 m search radius. The other base's source, 360 m away, still held supplies at the end of a stand-off but was never used.

### What changed (`ARTSAIPlayer`)

- **The assault estimate** (`RTSAI::CanOvercome`) runs a short simulation of the fight. The attackers close in at their move speed and fire once within their own range. The defence fires at the attacker with the shortest range first, and never at one that outranges it. The estimate counts the defence's damage plus that of every other defence in range of it. Its inputs are the units' current health, weapon damage, reload, range and speed, and the defences' health, damage and range. Only constructed, powered defences count, and those are the ones whose weapon fires. Nothing hidden is used: every player sees these buildings.
- **Wave targets** (`ChooseWaveTarget`) are taken nearest the army first:
  - An enemy building no defence covers is attacked as before.
  - A building a defence covers is attacked through that defence, if the wave can destroy it with `AssaultMargin` (1.5×) of its health to spare. Without the margin the next building is tried.
  - With no building left that it can take, the army keeps gathering instead of setting off.
  - Once the army can no longer grow (no credits for an army unit, none queued, no harvester at work), the margin drops to 1×. It then goes as soon as it can win at all.
- **Defences in the fight** (`EngageDefences`) are weighed again each think, once a defence fires on wave units:
  - **The wave can still win** (1×, counting units already in range from where they stand and the rest from its range's edge): the units in the fight that are not fighting enemy units attack the defence, instead of another building in their range.
  - **It cannot**: the units within `AssaultReach` of the fight fall back to the gather point and rejoin the army. They set off in no new wave before they arrive.
- **Waves still attack-move**, so enemy units on the way and around the target are fought first. Only units wasting fire on a building that cannot hurt them are switched to the defence.
- **Build-order recovery:** a step the credits do not cover is passed over, like an out-of-reach step (#179), while no own harvester is at work. With one at work the AI still saves up for it.
- **Far sources:** an idle harvester with no source in its search radius, while an own depot stands, is ordered to the nearest source elsewhere that no enemy defence covers (its range plus `SafeHarvestMargin`, 10 m). This is the same Harvest order a player can give.
- No change to credits, costs, unit or building data, the victory conditions or the 1800 s limit of the test. Orders still take `RTSCommands::Issue`.
- `ue.ps1 -Cvars name=value` passes console variables to a test run (`-dpcvars`), to replay the match on another fixed-step trajectory.

### Results

| `rts.Nav.MoveRequestsPerFrame` | Before (498df5f) | After |
|---|---|---|
| 4 (default) | stand-off at 1800 s | team 1 wins at 352 s, 10 of 10 runs identical |
| 1 | team 1 wins at 360 s | team 1 wins at 345 s |
| 2 | team 1 wins at 395 s | team 1 wins at 653 s |
| 3 | team 1 wins at 353 s | team 1 wins at 454 s |
| 5 | stand-off | team 1 wins at 341 s |
| 6 | stand-off | stand-off |
| 7 | team 1 wins at 360 s | team 1 wins at 657 s |
| 8 | team 1 wins at 361 s | team 0 wins at 682 s |

- `ue.ps1 -Task test -Filter WorldDivide.Smoke.AIvsAI -Repeat 10` passes 10 of 10 in 302 s wall-clock (30 s a run). Every run reports the same info lines (end-state digest `5cd35b8e`).
- With `-Cvars`, settings 1, 2, 3, 5, 7 and 8 each end with a winner, in 30 to 42 s wall-clock each. Settings 9, 10, 12 and 16, run once with the same code during development, also end with a winner.
- A stand-off runs the full 1800 game seconds, which takes about 80 s wall-clock.

### Test coverage

- **`WorldDivide.AI.CanOvercome`**: the estimate against the duel's tower:
  - five rifles walking in fall short, but five already beside it win;
  - two tanks fall short and three win, though not with a 1.5× margin;
  - six tanks have that margin;
  - a weapon that outranges the tower always wins.
- **`WorldDivide.AI.WavesWeighDefences`**:
  - two light tanks hold back from a building a strong tower covers;
  - they go for a building no defence covers once there is one;
  - six strong tanks set off at the covered building.
- **`WorldDivide.AI.WavesEngageDefences`**: six strong tanks under a tower's fire attack it together, and two light tanks fall back to the gather point and leave the wave.
- **`WorldDivide.AI.BuildOrderWithoutIncome`**: a 1500 step with 650 credits is passed over for infantry without income, and waited for while a harvester is at work. `WorldDivide.AI.BuildOrder` now has a harvester at work, so its "waits rather than skipping ahead" check covers the case where waiting is still right.
- **`WorldDivide.AI.HarvestFarSource`**: an idle harvester goes to a source outside its search radius, but not to one an enemy tower covers.

### Remaining limitations

- **A genuine stand-off can still happen** (setting 6). Both armies trade around team 1's tower until about 500 s. Team 0 ends with 7 damaged units against a 460-health tower, which the estimate rightly calls a loss. Team 1 has no army, and neither side has credits or supplies left. The AI now holds rather than feeding units to the tower, so the match runs to the limit with no winner. The game has no rule that ends such a match. An explicit draw (for example, the match ends drawn once no team has an armed unit, a production building it can pay for or a harvester at work for N seconds) is a gameplay decision for a separate issue. It is not counted as a win here.
- **The estimate is a model.** It ignores enemy units defending around the defence, terrain, and a defence's habit of keeping its current target. A wave can still lose a fight the estimate called. It then falls back once the fight turns.
- **No flanking:** a wave does not look for a firing position outside a defence's range, such as tanks shooting a building the tower covers only on its near side.
- **A far source on the way through the enemy army** is still used if no defence covers it. A harvester that cannot reach it is re-ordered each think.

## #172 (hull checks against solids off the navmesh, at scale)

Same machine and build configuration as #168, on 2026-10-18, code at `186e43f` plus the #172 change. L_Bench and the open scene are unchanged. The measurement is one long session, `ue.ps1 -Task bench -Long -Counts 240 -Unit DA_TestM20Guardian,DA_TestM20Guardian@Solids` (773 s wall clock). It runs the open scene and the obstacle scene back to back in one game session, so the two are the same build and the same machine state. Both runs are valid (#171), with 0.72 cores of other load. `-Compare` refuses to pair the two scenes (`scene.solids` differs), so they are compared in the tables below rather than in `compare.md`.

### The obstacle scene

- **Reproduce:** add the suffix `@Solids` to a set's name: `ue.ps1 -Task bench -Quick -Unit DA_TestM20Guardian@Solids`. The set gets its own folder (`Saved/Bench/DA_TestM20Guardian@Solids/`) and its own baselines. Without the suffix the bench is exactly as before; `FRTSBenchPlan::Version` is unchanged and the stored baselines stay valid.
- **Generated, not edited.** `RTSBenchScene::MakeSolids` lays the solids out at a fixed seed (172) when each count's grid stands, and they are cleared with its units. L_Bench and `setup_testmap.py` are untouched. `bench.json`'s `scene.solids` records the layout version, the seed and the count of each kind. Only an `@Solids` run has that field, so its runs never match an open-scene baseline.
- **The area** is the ground the grid stands on and drives over, both legs, plus 10 m: 178 × 144 m for 240 M20s. The layout scales with the grid, so smaller counts get the same density.
- **The solids** are engine shapes that block pawns, with navigation off (`SetCanEverAffectNavigation(false)`), as the River Crossing plan sets tree trunks ([03_TECHNICAL_PLAN.md](../../world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md), §6):

| Kind | Size | Density | At 240 M20s |
|---|---|---|---:|
| Tree trunks (cylinders) in a copse, the area's west 40% by its north 60% | 50–80 cm across, 8 m tall | one per 200 m² of copse | 31 |
| Boulders (boxes) over the whole area | 1.2–2.5 m sides, 1–1.5 m tall | one per 1200 m² | 21 |
| Low walls, alternately along and across the drive | 6–12 m long, 0.4 m thick, 1.2 m tall | one per 4000 m² | 6 |

- **Overall** that is 58 solids, one per 443 m²: an open field with a sparse copse and field-edge walls. No solid stands within 50 cm of a hull where it spawns, or within 3 m of another solid. Trunks and walls along the drive fit the 2.5 m gaps between the grid's columns, and are passed. Walls across the drive, and solids where the formation's slots land, are met.
- **Density was tuned once.** A first layout twice as dense (100 solids: trunks one per 100 m², boulders one per 800 m²) stopped 23–32% of the units short of 20 m per leg, and 109 of 240 met a solid: a collision pile rather than a map. The layout above keeps 84–86% of the units driving each leg.

### Collision correctness (`WorldDivide.Debug.Bench.Solids`, L_Bench, fixed 30 Hz, headless)

The bench's own grid of 240 M20s, among the same solids, drives the long scene's two 15 s legs, west and back:

| Check | Result |
|---|---|
| Units that drove 20 m or more, leg 1 / leg 2 | 206 / 201 of 240 (at least 75% required) |
| Units with solids in their neighbourhood, leg 1 / leg 2 | 199 / 197 |
| Units that met a solid (within 10 cm) / passed one without meeting it | 75 / 161 |
| Deepest any hull went into a solid | **0.86 cm** (at most 1 cm, as #168) |
| Narrow phase per frame | 97 hull checks, 188 component queries |

`WorldDivide.Debug.Bench.SolidLayout` checks the layout itself: the same grid gives the same solids, every kind is there with walls both ways, no solid stands on a hull or beside another, and none is denser than planned.

### 240 M20s, long method (mean of three 60 s samples; p95 is the mean of the samples' 95th percentiles)

| Scenario | Scene | Game ms (p95) | Slowest game frame ms | Locomotion ms (p95, max) | Avoidance ms |
|---|---|---|---:|---|---:|
| move | open | 4.13 (7.76) | 19.49 | 0.444 (1.153, 2.95) | 0.376 |
| move | **solids** | **4.76 (8.03)** | 19.53 | **0.743 (1.683, 2.89)** | 0.545 |
| idle | open | 1.92 (2.27) | 19.84 | 0.050 (0.070) | 0.228 |
| idle | solids | 1.95 (2.30) | 18.03 | 0.051 (0.070) | 0.229 |

- **Moving among the solids costs 0.63 ms more game thread** (p95 +0.27 ms): locomotion +0.30 ms and avoidance +0.17 ms. The rest is spread over the frame.
- **The worst frame does not change** (19.5 ms in both, under the 33 ms budget). It is the M20's render-state cost while it drives (#151 → #150 → #149), not collision.
- **Standing units cost the same:** an idle hull makes no solid query at all.
- **The open scene matches #168:** game 4.13 against 4.21 ms, locomotion 0.44 (1.15) against 0.45 (1.16) ms.

### Solid queries (the bench's "Solids" table, 240 moving units, per frame, mean of three samples)

| Scene | Broad overlaps | Reused | Hull checks | Component queries | Broad ms | Narrow ms | Solids total ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| open | 0.83 | 99.4% | 0 | 0 | 0.0053 | 0 | 0.005 |
| solids | 0.93 | 99.5% | 90.4 | 186.1 | 0.0064 | 0.1293 | 0.136 |

- **A hull check costs about 1.4 µs** for 2.1 component queries on average. A broad overlap costs about 7 µs, as in #168.
- **The broad phase barely moves:** neighbourhoods are reused 99.5% of the time, and an overlap that finds solids costs about the same as one that finds none.

### Unreal Insights (`ue.ps1 -Task bench -Quick -Trace`, region `<set>_240_move_1`, game thread)

Captured in separate quick runs of both scenes, never in the measured long session. Exported with `UnrealInsights -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics <csv> -threads=GameThread -timers=* -region=<region>"`. The trace's own overhead is in these numbers, so read them as a split, not against the budget:

| Scope (ms per frame, calls per frame) | Open | Solids |
|---|---|---|
| `URTSUnitMovement::TickLocomotion` (inclusive) | 0.443 | 0.749 |
| `FRTSSolidNeighbourhood::IsClear` | — | 0.124 (87.6 calls) |
| `FRTSSolidNeighbourhood::Update` | 0.005 (0.8 calls) | 0.007 (0.9 calls) |
| `RTSBarrelClearance::IsClear` (the gear's barrel watch, #162) | — | 0.178 (98.7 calls) |
| `URTSUnitMovement::IsPoseClear` (inclusive) | 0.021 | 0.100 |
| `URTSAvoidanceSubsystem::Separate` (inclusive) | 0.322 | 0.539 |

- **The narrow hull check is not the largest new cost in locomotion.** The barrel sweeps of #162 are larger: 0.18 ms against 0.12 ms. A gun over the front sweeps against a solid near the hull while the gear turns. Together they are the 0.30 ms locomotion rise.
- **Avoidance rises by 0.22 ms.** Each vehicle push is checked against the pushed hull's solids (`IsClearOfSolids`), and, probably, hulls held at a solid stay close to their neighbours for longer.

### Against the budget (#138, 240 moving M20s, long method)

| Budget | Open | Among solids |
|---|---|---|
| Locomotion mean ≤ 1.0 ms | Met (0.44) | **Met (0.72–0.76)** |
| Locomotion p95 ≤ 2.0 ms | Met (1.15) | **Met (1.68–1.69)** |
| Non-zero narrow-phase work | none (no such solids) | **90 hull checks and 186 component queries per frame** |

The budget holds, so no optimization is proposed. The p95 has 0.31 ms of headroom left. If a denser map ever closes that gap, the first place to look is the barrel watch, not the hull check: its sweep could reuse the solid neighbourhood's few components, as the hull check does, instead of a world sweep. That is a suggestion for a later issue, not part of this change.

### Test runtime

- `WorldDivide.Debug.Bench.SolidLayout` and `WorldDivide.Debug.Bench.Solids`: 28 s together, PIE start included.
- The full run: 259 of 259 pass in 7 min 19 s, under the 10-minute target.

### Remaining limitations

- **One machine, one unit:** the M20 is the representative with a barrel over its front. A hull without a barrel would see less locomotion cost here.
- **Paths do not know the solids** (by design, #168). A hull that meets one stops until its next order, so 14–16% of the units stand each leg. That lowers the locomotion cost a little against a scene where every unit keeps driving.
- **Formation slots** that land on a solid make the units there stop short; that is counted as meeting it.
- **The gate does not bench `@Solids`.** `-Task gate` plans open-scene sets only. A change to collision or locomotion should add a quick `@Solids` run by hand.

## #151 (M20 track animation)

Same machine (Ryzen 7 7800X3D, RTX 4090), on 2026-10-18. Both variants ran in one session of one build, scene, camera and settings: `ue.ps1 -Task bench -Long -Unit DA_TestM20Guardian,DA_TestM20Guardian@RigidTracks`. The `@RigidTracks` set runs with `rts.Vehicle.TrackAnimation 0`: no phase, and the tracks drawn with `MI_M20_Track_Static`, the material without the link shift. The long method is 10 s of warm-up per count; three 60 s samples at 240 units, one at the other counts.

The bench camera stands 170 m from the grid, past the 60 m animation distance (`rts.Vehicle.TrackAnimationDistance`). This is how tracks are seen at the RTS camera's default zoom of 150 m. A quick run with the distance at 0 measures the worst case, every track animated.

The bench has new columns for this measurement: render-thread and GPU p95, and the running-gear pass (`URTSRunningGearSubsystem::PlaceAll`), mean and p95.

### 240 moving M20s, long method (mean of three samples; p95 is the mean of the samples' 95th percentiles)

| | Rigid | Animated | Difference |
|---|---|---|---|
| Game thread, mean (p95) | 4.15 (7.91) ms | 4.21 (7.87) ms | +0.06 (−0.04), within the 0.6 ms spread |
| Render thread, mean (p95) | 4.24 (4.84) ms | 4.24 (4.84) ms | 0 |
| GPU, mean (p95) | 4.32 (4.50) ms | 4.34 (4.50) ms | +0.01 (0) |
| Draw calls | 3,638 | 3,639 | +1, sampling noise (both 3,932 when standing) |
| Triangles drawn | 45.72 M | 45.77 M | +0.1 %, sampling noise |
| Running gear, mean (p95) | 0.403 (1.105) ms | 0.418 (1.095) ms | **+0.015 (−0.010)** |
| Locomotion, mean (p95) | 0.44 (1.16) ms | 0.44 (1.16) ms | 0; #165 had 0.44 (1.18) |

### 1, 30 and 120 moving M20s (one sample each), rigid → animated

| Units | Game ms, mean (p95) | Render ms, mean (p95) | GPU ms, mean (p95) | Running gear ms, mean (p95) |
|---:|---|---|---|---|
| 1 | 1.11 (1.70) → 1.10 (1.68) | 1.98 (2.25) → 1.99 (2.26) | 1.78 (1.83) → 1.79 (1.83) | 0.005 (0.009) → 0.004 (0.009) |
| 30 | 1.51 (2.27) → 1.51 (2.55) | 2.36 (2.60) → 2.35 (2.58) | 2.16 (2.21) → 2.16 (2.20) | 0.053 (0.135) → 0.055 (0.139) |
| 120 | 2.54 (4.72) → 2.50 (4.62) | 3.04 (3.62) → 3.04 (3.61) | 3.15 (3.22) → 3.15 (3.23) | 0.214 (0.656) → 0.215 (0.630) |

Draw calls and triangles match at every count (108, 573/574 and 2,016 moving).

### Worst case: every track animated (quick method, one 30 s sample at 240, diagnostic)

`ue.ps1 -Task bench -Quick -Unit DA_TestM20Guardian,DA_TestM20Guardian@RigidTracks -Cvars rts.Vehicle.TrackAnimationDistance=0` (bench `-Cvars` is new; such a run is never stored as a baseline):

| | Rigid | Animated | Difference |
|---|---|---|---|
| Game thread, mean (p95) | 4.05 (7.89) ms | 4.07 (8.07) ms | +0.02 (+0.18) |
| Render thread, mean (p95) | 4.26 (4.82) ms | 4.36 (4.84) ms | +0.10 (+0.02) |
| GPU, mean (p95) | 4.32 (4.58) ms | 4.31 (4.57) ms | 0 |
| Draw calls | 3,646 | 3,645 | 0 |
| Running gear, mean (p95) | 0.395 (1.117) ms | 0.518 (1.536) ms | +0.12 (+0.42) |

Here all 480 tracks get their phase every frame they move: about 0.25 µs per track. The p95 is 0.02 ms over the 0.4 ms budget in this single sample. On a real screen the 60 m distance keeps that to the tracks near a close camera, a few dozen at most.

### Against #151's budget (240 visible moving M20s)

| Budget | At the 60 m distance (long method) | Every track animated (quick, diagnostic) |
|---|---|---|
| Running-gear CPU ≤ +0.2 ms mean / ≤ +0.4 ms p95 | **Met** (+0.015 / −0.010) | Mean met (+0.12); p95 +0.42, just over |
| GPU ≤ +0.5 ms mean / ≤ +1.0 ms p95 | **Met** (+0.01 / 0) | **Met** (0 / 0) |
| Zero additional draw calls | **Met** | **Met** |
| Distance-disabled shift costs no GPU beyond noise | **Met**: +0.01 ms against a sample spread of 0.03 ms | — |

### Remaining limitations

- **The GPU cost of the shift is measured from 170 m.** There every track is a few pixels high, and the vertex work does not show against the frame. A close camera over many tracks has not been benched: the bench's scene has a single fixed camera.
- **The worst-case CPU is the primitive-data update**, one render command per moving track and frame. If close views of large armies become common, the phase could go to the material less often, every other frame for example, before anything else.
- **The full-roster gate was not run** (#171), as asked. #150 and #149 change the M20's rendering next; the final M20-against-placeholder gate stays theirs.

## #150 (shared team-colour materials)

Same machine (Ryzen 7 7800X3D, RTX 4090), on 2026-10-18, same build configuration and method. "Before" is `main` at `da9a7b1` plus only the bench additions below, benched in the same session as "after".

### What changed

**Before.** `ARTSUnit::ApplyTeamColor` and `ARTSBuilding::ApplyTeamColor` made a dynamic material instance (MID) for every material slot of every part and set its `Color` vector parameter. An M20 has 9 drawn parts and 10 slots, so it had 10 MIDs: 2,400 at 240 tanks. Each MID is its own material render proxy. Mesh draw commands of different units therefore never matched, and the renderer could not merge (dynamically instance) them.

**After.** The team colour is custom primitive data (CPD). `M_Vehicle` and `M_PlaceholderTeam` read their `Color` parameter from CPD 4 to 7, and every unit and building part keeps the material instances of its mesh (`MI_M20_*`, `MI_<Placeholder>_*`). `ApplyTeamColor` calls `SetCustomPrimitiveDataVector4(RTSPrimitiveData::TeamColor, colour)` on each drawn part, the wheel ISMs included. This is a primitive-data update, as the track phase is (#165): no render-state rebuild. It runs only when the unit's data or team arrives (spawn, `OnRep_UnitData`, `OnRep_TeamId`), so the existing replication carries it and there is no RPC. Materials without team colour (`M_VehicleLamp`, `MI_M20_Track_Static`) no longer get a pointless MID either. No game code makes a team-colour MID any more; the placement ghost and the shot effects keep their own, unrelated to teams.

### Custom primitive data layout (`Units/RTSPrimitiveData.h`)

| Index | Value | Read by | Written by |
|---:|---|---|---|
| 0 | Track phase through one link | `M_Vehicle` with `TrackAnimation` on | `ARTSUnit::UpdateRunningGear` (#151), while the track moves and is seen |
| 1 | Last frame's phase, unwrapped | the same, for motion vectors | the same |
| 2, 3 | Free | — | — |
| 4 to 7 | Team colour, RGBA | `M_Vehicle`, `M_PlaceholderTeam` (`Color`) | `ARTSUnit::ApplyTeamColor`, `ARTSBuilding::ApplyTeamColor` |

A vector parameter reads four floats from its index, so the team colour starts at 4 and the two never overlap. `WorldDivide.Art.TeamColorMaterials` checks the materials against the header, and `WorldDivide.Units.RunningGear.Spin` checks that writing either one leaves the other as it is.

### The bench additions

- Every bench row now counts what the units draw with at the end of the sample: their primitive components, the material slots that are MIDs, and the distinct materials (`primitives`, `dynamic_materials`, `distinct_materials` in `bench.csv`; a third table in `bench.md`).
- A set with the suffix `@MixedTeams` spreads its units over four teams in turn. This is the mixed-team scenario: units of one kind in four team colours. Their weapons are idled so neighbours do not fight, and the move goes straight through `RTSCommands::Dispatch`, since the player's own path only orders the player's units.
- `rts.Debug.UnitShots` (`ue.ps1 -Task unitshots`) adds four views of one M20 per team, side by side: at the default and the closest RTS zoom, from an inspection camera, and from that camera under a low sun (pitch −10°).

### Resources (240 M20s, every scenario)

| | Before | After |
|---|---:|---:|
| Primitive components | 2,160 | 2,160 |
| Material slots that are MIDs | 2,400 | **0** |
| Distinct materials the units draw with | 2,400 | **5** |

The five are `MI_M20_Hull`, `MI_M20_Turret`, `MI_M20_Running`, `MI_M20_Track` and `M_VehicleLamp`, at every count and team mix. The counts are the same in the `@MixedTeams` set.

### Short method, 1 to 240 M20s (`ue.ps1 -Task bench -Unit DA_TestM20Guardian,DA_TestM20Guardian@MixedTeams`, 59 s)

One team (`DA_TestM20Guardian`); before → after.

| Units | Scenario | Draw calls | Render ms | GPU ms (p95) | Game ms (p95) | Running gear ms (p95) |
|---:|---|---|---|---|---|---|
| 1 | idle | 108 → 107 | 1.97 → 1.99 | 1.77 (1.80) → 1.77 (1.80) | 1.05 (1.59) → 1.00 (1.22) | 0.001 → 0.001 |
| 1 | move | 108 → 108 | 2.00 → 2.00 | 1.80 (1.85) → 1.79 (1.83) | 1.11 (1.60) → 1.12 (1.68) | 0.007 → 0.013 |
| 30 | idle | 573 → **370** | 2.30 → 2.26 | 2.10 (2.14) → 2.06 (2.29) | 1.19 (1.81) → 1.16 (1.77) | 0.003 → 0.003 |
| 30 | move | 572 → **369** | 2.31 → 2.26 | 2.13 (2.17) → 2.07 (2.11) | 1.81 (2.97) → 1.78 (2.80) | 0.104 (0.200) → 0.113 (0.254) |
| 120 | idle | 1,673 → **994** | 3.12 → 2.98 | 2.89 (2.93) → 2.78 (2.80) | 1.48 (2.17) → 1.39 (1.83) | 0.011 → 0.011 |
| 120 | move | 1,750 → **1,039** | 2.60 → 2.71 | 2.93 (3.25) → 2.80 (3.00) | 4.00 (5.24) → 3.85 (4.66) | 0.487 (0.690) → 0.466 (0.665) |
| 240 | idle | 2,785 → **1,620** | 3.96 → 3.83 | 3.74 (3.95) → 3.62 (3.86) | 1.90 (2.23) → 1.80 (2.08) | 0.022 → 0.021 |
| 240 | move | 2,905 → **1,689** | 4.05 → 4.08 | 3.82 (4.05) → 3.70 (3.94) | 7.12 (8.76) → 6.56 (7.45) | 1.015 (1.290) → 0.956 (1.182) |

Four teams (`@MixedTeams`), 240 units: idle 2,785 → **1,620** draw calls, move 2,902 → **1,689**; GPU 3.74 → 3.61 ms idle, 3.83 → 3.71 ms moving. The mixed-team rows equal the one-team rows at every count, after as before: team colours do not split the draws.

### Quick method, 240 moving M20s (`-Quick`, one 30 s sample each, diagnostic)

| | Before | After, one team | Before, four teams | After, four teams |
|---|---:|---:|---:|---:|
| Draw calls | 3,646 | **2,101** | 3,646 | **2,100** |
| Render thread, mean (p95) | 4.29 (4.80) | 4.16 (4.64) | 4.29 (4.81) | 4.22 (4.68) |
| GPU, mean (p95) | 4.30 (4.56) | 4.18 (4.45) | 4.31 (4.57) | 4.18 (4.44) |
| Game thread, mean (p95) | 4.09 (7.86) | 3.87 (7.39) | 3.96 (8.18) | 3.88 (8.03) |
| Running gear, mean (p95) | 0.411 (1.121) | 0.390 (1.079) | 0.423 (1.245) | 0.401 (1.195) |

Against #151's long-method figure of 3,639 draw calls, 240 moving M20s now draw about **2,100: −42 %**. The issue's 2,556 was the older `unitshots -Stress 240` scene; it was not re-run. GPU time is down about 0.12 ms, about 3 %. The render thread is down 0.07 to 0.13 ms, within one sample's spread. The game thread and the running gear do not change beyond the run-to-run spread, as expected: the change touches only what is drawn.

### Where the draw calls go now

Draw calls were taken apart at 240 standing M20s (short method), with diagnostic cvars on and with a temporary switch that brought the per-slot MIDs back on today's materials. The temporary switch was not committed.

| 240 idle M20s | Draw calls | Without occlusion queries (`r.AllowOcclusionQueries 0`) |
|---|---:|---:|
| One scene unit (baseline of the level) | 107 | 102 |
| With per-unit MIDs (as before #150) | 2,785 | 1,300 |
| With shared materials (now) | 1,620 | **110** |

- **The meshes' draws are now independent of the unit count:** 102 → 110 from 1 to 240 M20s. With MIDs they grew by 5 per tank. `r.MeshDrawCommands.LogDynamicInstancingStats` reports 1,386 base-pass mesh draw commands in 12 instancing buckets: every part of every M20, the wheel sets included, merges with the same part of the others.
- **What is left is about 1,500 hardware occlusion queries:** about one per visible primitive, 9 primitives per M20. They counted the same before. They cost the RHI and render thread, not the GPU: without them the render thread at 240 idle tanks fell from 3.83 to 1.68 ms. Turning them off is a project-wide renderer choice, not part of this issue → follow-up.
- **Shadows** add about 30 draw calls at 240 tanks (`r.ShadowQuality 0`); the cascades already merge.
- **Further hull instancing would not pay.** Merging the hull, turret, gun and weapon station of a tank would remove no draw: they already merge across tanks. It would remove only occlusion queries, and the turret, gun and station must stay separate components to aim. The wheels are already one ISM per wheel mesh per tank, which merges across tanks. Nothing was added.

### Unreal Insights

`ue.ps1 -Task bench -Quick -Trace -Unit DA_TestM20Guardian` writes `Saved/Traces/Bench_DA_TestM20Guardian.utrace` (region `DA_TestM20Guardian_240_move_1`, 2,099 draw calls under tracing). Nothing new appears on the game thread. `ApplyTeamColor` no longer allocates MIDs on spawn, but spawning is outside the measured region.

### Visual comparison

`unitshots` and `trackshots` were captured before and after on the same build. They are compared pixel by pixel (largest RGB channel difference, 0 to 255):

| Captures | Mean difference before → after | Two identical "after" runs |
|---|---|---|
| 15 unit shots (default, closest and farthest zoom, close-up, turret, driving, obstacles, the four-team row at both zooms, close and low sun) | 0.37 to 0.77 | — |
| 168 track frames (forward, reverse, pivots, turns, braking, wraps, far, blocked) | 0.61 to 2.58, median 1.06 | 0.44 to 2.18, median 1.22 |

Before → after stays within the variation between two identical runs (temporal anti-aliasing and the moving tracks). The team panels, weathering, normals, shadows and the track links are unchanged under both lights. Four M20s on teams 0 to 3 stand side by side in four colours: no colour carries over between units.

### Implications for #149 (LODs)

- An LOD's material slots can use the same `MI_M20_*` instances, or new ones of `M_Vehicle`, and the team colour comes along with no code: it is per primitive, not per material. The far track LODs can use `MI_M20_Track_Static`, as #151 planned.
- Draw calls are now set by the mesh/material pairs per pass and by the occlusion queries, not by the unit count. LODs will cut triangles (40 M at 240 tanks), not draw calls. Fewer separate parts at far LODs (one merged rigid far mesh) would cut occlusion queries and component cost.

### Material conventions for vehicles and other team-coloured content

- **Team colour is per primitive.** Read it in the master material from the `Color` vector parameter with *Use Custom Primitive Data* on, index `RTSPrimitiveData::TeamColor` (4). Never make a MID for it, and never set it per material instance.
- **One master per kind of surface, one constant instance per texture set.** USA, China and Insurgent vehicles are new `MI_*` instances of `M_Vehicle` with their own textures, or of a faction master built the same way. The faction look lives in the textures and the instance parameters (`TeamStrength`, `TeamBrightness`); the team lives in the CPD.
- **Every CPD index is in `RTSPrimitiveData.h`.** A new per-unit value (damage, a highlight) takes a free index there, and the Python material builders use the same number. 2 and 3 are free; 8 and up after the team colour.
- **Per-unit values go through CPD, written only when they change**, as a primitive-data update; never a render-state rebuild (#165).
- **Materials that change per frame on one actor only** (the placement ghost, shot glows) may keep their own MID: they are one actor each, not an army.

### Remaining limitations

- **Hardware occlusion queries now make up most of the draw calls** (about 1,500 of 1,620 at 240 standing M20s) and about half the render thread. Disabling them, or making unit parts skip them, needs measuring across the maps → follow-up.
- **Unset CPD reads as black.** A team-tinted mesh drawn by anything other than `ARTSUnit` or `ARTSBuilding` (a future portrait capture, a level prop) must set CPD 4 to 7 itself, or use an instance with `TeamStrength`/`TintStrength` 0.
- **No long-method run** was made, as asked. The quick and short methods show the draw-call and GPU change at every count. The final M20 against placeholder gate stays with #149.
- **Late join** is covered by the vehicle network test's tank that replicates only after it has driven. Joining a match already under way, in the lobby, was not tested by hand.
