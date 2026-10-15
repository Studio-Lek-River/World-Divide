# Shared Foundation Recovery Audit

**Status:** approved by the producer on 2026-10-15, with the decisions recorded in §8.1. **Date:** 2026-10-15.
**Scope:** milestone #7 (M1b First true unit: M20 Guardian) and milestone #8 (M1c First map: WD_RiverCrossing_01).
**Method:**
- Read-only review of GitHub issues #132–#161.
- Review of `main` at `280c810` plus the uncommitted work in the tree.
- Review of the scale documents in `docs/tech/scale/` and the map documents in `docs/world/maps/WD_RiverCrossing_01/`.
- Latest logs in `Saved/`.

The audit itself changed nothing except this file. The approved decisions were applied afterwards (§8.1).

---

## 1. Executive findings

1. **The closed duplicates were never implemented.** #141–#148 (23a–23h) were closed by hand between 10:41:31Z and 10:41:49Z on 2026-10-15. Each has only the comment "Duplicate of #13N". No commit references them, and no test results are attached.
2. **#132–#139 (S0–S7) are the canonical series.** They follow `SCALE_IMPLEMENTATION_PLAN.md` step by step and carry the shared "Rules for every scale step". Each closed twin adds a few requirements, now tracked nowhere (§2). They should be folded into the open issues.
3. **Only step 0 is done, and it is not pushed.**
   - `280c810` ("Add the real-scale profiling baseline with Insights scopes", `Closes #132`) is local. `main` is three commits ahead of `origin/main`.
   - Steps 1–7 are not in the code at all.
4. **The benchmark work is orphaned.** The tree holds about 700 uncommitted lines that implement #141's extra scope:
   - `rts.Debug.Bench`, `L_Bench`, `ue.ps1 -Task bench` and `PERFORMANCE_RESULTS.md`.
   - It cites the closed numbers (#141, #143, #148) and the Phase 2B numbers (#149–#151), not #132–#139.
5. **The M20 cannot meet the step 4 performance gate with steps 1–4 alone.**
   - 240 moving M20s cost **17.59 ms** of game thread, against **4.20 ms** for 240 placeholder tanks.
   - The cause is render-state churn from three wheel ISMs and nine attached parts (`PERFORMANCE_RESULTS.md`). Avoidance and pathing are not the cause.
   - #136 asks for "240 M20s ≤ 240 tanks". That needs #151 (WPO tracks) and #150 (shared materials), which the plan places after step 7.
6. **River Crossing is blocked on the shared foundation and on its own numbers.**
   - Greyboxing (#154) needs steps 1, 3 and 4.
   - The map design uses a different scale assumption from the world standard: weapon ranges about 3× longer, larger buildings, a different vehicle nav agent and different camera coverage. These must be reconciled before #154 starts (§6, §8).

---

## 2. Duplicate issue reconciliation matrix

The open issue is canonical in every pair. "Missing from canonical" lists requirements that exist only in the closed twin. **Regressions:** none found. No code for steps 1–7 exists, so none could regress; the latest full run (`Saved/Logs/AutomationRun.log`, 2026-10-15 12:38) is 177/177 green.

| Pair | Step | Implemented on main | Missing from canonical (only in the closed twin) | Recommendation |
|---|---|---|---|---|
| #132 ↔ #141 | S0 profiling baseline | **#132: yes**, `280c810`, local and unpushed: scopes in `TickOrders`, `UpdateWeapon`, `FindNearestEnemyInRange`, `Separate`; `ue.ps1 -Task unitshots -Trace`; plan §7 baseline. **#141 extras: uncommitted only:** `RTSBenchCommand.cpp`, `L_Bench.umap`, `-Task bench`, `UpdateRunningGear` scope, search timer, `PERFORMANCE_RESULTS.md`. | Bench mode: 1/30/120/240, idle and move, fixed camera, dynamic resolution off. Render, GPU, memory and draw-call metrics. CSV and markdown output. `PERFORMANCE_RESULTS.md`. RTX 4090 labelling, RTX 3060 marked unmeasured. | **Retain #132, extend its scope** to cover the bench, then close it with two commits: `280c810` and a bench commit. Keep #141 closed. |
| #133 ↔ #142 | S1 box collision, footprint, selection | No. Capsule root at `RTSUnit.cpp:117-126`, sized from `SelectionRadius` (`:245-258`). The M20 gets an 8 × 8 m capsule with its origin 4 m above the ground. | "Keep the same collision responses". Selection bounds = "hull plus turret, from the ground up". "Infantry selection behaves as before". | **Retain #133** and add the three items to its acceptance. |
| #134 ↔ #143 | S2 avoidance and acquisition | No. Cell size 2 × MaxRadius (`RTSAvoidanceSubsystem.cpp:75`), push clamped per frame (`:157`), default nav data (`:149`). Per-frame O(n²) acquisition. | Owner of the per-team building list fixed as `ARTSGameState`. Measurement through the bench as well as Insights. | **Retain #134**, settle the building-list owner as `ARTSGameState`, and accept bench figures. |
| #135 ↔ #144 | S3 navigation agents | No. One default agent (`Config/DefaultEngine.ini:17-26`, no `SupportedAgents`). | "Placement stays on the infantry navmesh". "Placement checks that vehicles can reach the factory exit". | **Retain #135** and add both placement rules. Agent size needs a decision first (D-D). |
| #136 ↔ #145 | S4 data rescale | No. Placeholder sizes still at about 1/3 scale (`setup_testmap.py:1424-1484`). | Concrete factors: maps 3× half-size, gaps ≥ 8 m, infantry fitted to 180 cm, buildings about 0.6–0.7×. Tests are rewritten to read data and are **not loosened**. | **Retain #136**, add the factors and the "not loosened" rule, and re-baseline its performance gate (D-G). |
| #137 ↔ #146 | S5 camera | No. `RTSCameraPawn.h:64-101` unchanged (zoom 175/3000/7000). | Nothing substantive. #146 states the values as final, while #137 correctly waits for the producer's screenshot choice. | **Retain #137 as is.** |
| #138 ↔ #147 | S6 vehicle movement | No. `UFloatingPawnMovement` with default acceleration (`RTSUnit.cpp:144`, `:268`). | `MoveAcceptanceRadius` 25 → 50 unconditionally. #138 makes it conditional on overshoot. | **Retain #138** and keep the conditional wording; the unconditional version is not needed. |
| #139 ↔ #148 | S7 formation and idle savings | No. Square grid, spacing `SelectionRadius × 2.5` (`RTSCommandDispatch.cpp:72`), O(n³) `AssignSlots`. | "No server frame over 33 ms on a 240-unit move." This is in plan §6 item 7, but not in #139's own acceptance. | **Retain #139** and add the 33 ms gate explicitly. |

**Overall recommendation:** keep #141–#148 closed and keep #132–#139 open. The producer copies the "missing" column into the open issues (D-A). Do not reopen the closed series; two live trackers for the same step caused this situation.

---

## 3. Current implementation status

Status of `main` at `280c810` plus the uncommitted tree.

| System | State today | Target step |
|---|---|---|
| **World scale** | 1 uu = 1 cm, no scale constants in code. Placeholder sizes come from `fit_scale()` in `Tools/Editor/setup_testmap.py:325`; the tank is 260 × 150 cm, about 1/3.5 real. The M20 is real size (710.2 cm hull, checked in `make_m20_guardian`) but runs on placeholder data: radius 400, speed 500, turn 150. | S4 #136 |
| **Collision** | `UCapsuleComponent` root (`Units/RTSUnit.cpp:117-126`) with the Pawn profile; parts have no collision. Capsule = `(Radius, max(MeshHalfHeight, Radius))` (`:245-258`). Ground offset = half height (`:527-529`). | S1 #133 |
| **Selection** | Screen circle from the radius (`Player/RTSHUD.cpp:181`). The marquee tests the actor origin (`Player/RTSPlayerController.cpp:656`). No selection decals. | S1 #133 |
| **Movement** | `UFloatingPawnMovement`, default acceleration and deceleration, `bCanStrafe=true`. Facing interpolates to velocity at `TurnRate` (`RTSUnit.cpp:685`). No pivot turning, no turn radius. | S6 #138 |
| **Avoidance** | Custom server-side `URTSAvoidanceSubsystem` (`rts.Avoidance`). Cell size 2 × MaxRadius gives 8 m cells with an M20 present. The push is clamped per frame, so an M20 can jump up to 4 m. Default nav data raycast. | S2 #134 |
| **Acquisition** | Full scan every frame. 240 idle units: 7.92 ms on `L_TestMap` (step 0), 5.8–5.9 ms on `L_Bench`. It is the largest idle cost. | S2 #134 |
| **Navmesh** | One default agent (engine radius 34, height 144), engine tile and cell sizes, `RuntimeGeneration=Dynamic`, `CrowdManagerClass=None`. | S3 #135 |
| **Camera** | `RTSCameraPawn.h:64-101`: zoom 175/3000/7000, pitch -55, pan 3000, edge margin 500, FOV 90. | S5 #137 |
| **Formation** | `Commands/RTSFormation.h`: square grid with one spacing, greedy O(n³) assignment, which causes a 20–40 ms hitch at 240 units. | S7 #139 |
| **Unit AI** | `ARTSUnitAIController`, one per unit, only overrides `OnMoveCompleted`; the actor tick is still on. Skirmish AI in `AI/RTSAIPlayer` uses small-scale distances (`BuildingGap` 300, `GatherDistance` 1500, `DefenseRadius` 2500). | S4 #136, S7 #139 |
| **Performance tooling** | Committed: Insights scopes and `unitshots -Trace` (#132). Uncommitted: `rts.Debug.Bench`, `L_Bench` (300 × 300 m), `ue.ps1 -Task bench`, the `UpdateRunningGear` scope and the `URTSWeaponComponent::TakeSearchSeconds()` timer. | S0 #132 |
| **M20 art** | Meshes and materials committed (`0c42869`, local). `DA_TestM20Guardian`, test `WorldDivide.Art.M20Guardian` green. LOD0 only, 89k triangles, about 10.5 draw calls per unit. | #149–#151 |
| **River Crossing level** | Does not exist. Design docs 01–05 only. Today's maps are ±80 to ±120 m. | #154+ |

**Tests that exist today:**
- Avoidance: `Crowd`, `Gap`, `FrameTime`.
- Formation: `Offsets`, `AssignSlots`.
- Camera: `PitchFollowsZoom`, `ZoomLimit`, `GroundClearance`, `PanArea`.
- Selection and UI smoke tests.
- `Maps.SavedNavMesh`, which the uncommitted work extends with `L_Bench`.
- Movement and obstacle smoke tests.
- `Art.M20Guardian`.

**Tests the plan requires that do not exist yet:** `Units.Footprint`, `UI.SelectionBracketsOnHull`, `Navigation.VehicleAgent`, `Units.VehicleTurning`, `Maps.RiverCrossing`.

**Latest performance figures** (`PERFORMANCE_RESULTS.md`, RTX 4090, 1080p, `L_Bench`):

| 240 units | M20 Guardian | Placeholder tank |
|---|---:|---:|
| Idle, game thread | 8.53 ms | 8.75 ms |
| Moving, game thread | **17.59 ms** (max 21.68) | 4.20 ms (max 20.05) |
| Avoidance, moving (mean / max) | 0.544 / 1.341 ms | — |
| Target search, idle | 5.78 ms | 5.94 ms |
| GPU | ≤ 4.74 ms | — |

The GPU is never the limit; the game thread is. The RTX 3060 target has not been measured.

---

## 4. Shared dependency map

### 4.1 Classification

| Requirement | Class | Issue | Why |
|---|---|---|---|
| 1 uu = 1 cm, real-size units, uniform `MeshScale`, per-unit gameplay values | **SHARED** | #136 | The M20 is the reference unit, and the map is laid out for real-size units. |
| Box collision root, ground offset, `GetSelectionBounds()`, stadium footprint | **SHARED** | #133 | It fixes the M20 bracket bug, and every map system that measures gaps or exits uses the footprint. |
| Two nav agents (infantry 40/180, vehicle about 190/350), tile about 6400, cell 25–30 | **SHARED** | #135 | M20s must refuse sub-6 m gaps; bridges and village lanes are designed for the vehicle agent; a 2 km navmesh needs large tiles. |
| Avoidance: fixed cell about 600 cm, stadium distance, per-second push clamp, unit's own nav data | **SHARED** | #134 | M20 columns on bridges; with per-frame clamps an M20 can be pushed off a bridge lane. |
| Acquisition: 5 Hz staggered rescan, `QueryRadius`, per-team building list | **SHARED** | #134 | Ranges grow about 5× at real scale, so per-frame scans become far more expensive. |
| Formation spacing (column W + 2.5 m, row L + 3 m), O(n log n) assignment, idle savings | **SHARED** | #139 | M20 formation footprint; 12-tank bridge columns; 200-unit fights. |
| Vehicle movement: tracked pivot, wheeled turn radius, acceleration | **SHARED** | #138 | M20 believability; turning on bridges and in village streets. |
| Camera retune (FOV 60, zoom 600/15000/30000) | **SHARED** | #137 | M20 readability at 5–6 % of screen width; map readability, cull distances and tree heights depend on the max arm. |
| AI distances (`BuildingGap`, `GatherDistance`, `DefenseRadius`, `BuildSearchDistance`, `BaseRadius`, `ExitLaneLength`) | **SHARED** | #136 | The skirmish AI must work on both the test maps and River Crossing. |
| Bench and performance harness, budget for 200–240 units | **SHARED** | #132, #152 | Both milestones gate on it. |
| M20 mesh fixed, 710.2 cm import check, `Art.M20Guardian` green | M20 ONLY | all steps | The asset is final. |
| `DA_TestM20Guardian` values (radius becomes interaction-only, speed / turn 1250 / 100, turret 45 °/s) | M20 ONLY | #136 | Data only. |
| Moving cost from render-state churn (wheel ISMs, attached parts) | M20 ONLY | #151, #150 | The 17.59 ms figure. |
| LODs (LOD1 25–35k, LOD2 6–10k, LOD3 1–2.5k triangles) | M20 ONLY | #149 | About 150k triangles per unit today. |
| UI real-scale brief | M20 ONLY (feeds UI) | #140 | Needs final camera and selection values. |
| Fab packs, tree licence | RIVER ONLY | #153 | — |
| Landscape 2.52 km, starts, supply, navmesh bounds | RIVER ONLY | #154 | — |
| River not walkable (slope + `NavArea_Null`), water collision ignores WorldStatic traces | RIVER ONLY | #155 | — |
| Bridges: 12 m carriageway, ramps ≤ 8°, a column of 12 tanks crosses each bridge | RIVER ONLY (gate needs #134, #138, #139) | #155 | — |
| Landscape material, village, PCG vegetation, lighting | RIVER ONLY | #156–#159 | — |
| Readability and performance pass (200 units at the village), lobby, sign-off | RIVER ONLY (needs shared budget) | #160, #161 | — |

### 4.2 Dependency graph

```
#132 S0 baseline + bench ──► #133 S1 collision/selection ──┬─► #134 S2 avoidance/acquisition ──┬─► #139 S7 formation
                                                            │                                     │
                                                            ├─► #135 S3 nav agents ───────────────┤
                                                            │                                     ▼
                                                            │                           #136 S4 rescale ──┬─► #137 S5 camera ──► #140 UI brief
                                                            │                                             ├─► #138 S6 movement
                                                            │                                             └─► #154 greybox (River lane)
                                                            └─► #151 tracks ─► #150 materials ─► #149 LODs  (M20 render lane)

#153 Fab audit (no dependency; can start now)
#154 ─► #155 bridges (column gate also needs #138 + #139) ─► #156 ∥ #157 ∥ #158 ∥ #159 ─► #160 (needs #137, #149, #150) ─► #161
Everything ─► #152 final benchmark
```

---

## 5. Recommended issue execution order

There is one sequence, organised in gates. A gate passes when its acceptance criteria are met, the full suite is green, and the bench numbers for that step are recorded. Milestones stay as they are.

| Gate | Issue(s) | Depends on | Acceptance gate (summary) | Parallel with |
|---|---|---|---|---|
| **G0** | #132 S0, extended with the bench (D-B) | — | `280c810` pushed. Bench committed. `ue.ps1 -Task bench` produces the full matrix for the M20 and `DA_TestTank`. Issue numbers in `PERFORMANCE_RESULTS.md` corrected. Suite green. | #153 |
| **G1** | #133 S1 | G0 | `Units.Footprint` and `UI.SelectionBracketsOnHull` pass. A click 1 m beside the hull does not select. Infantry selection unchanged. `Art.M20Guardian` green. Listen-server smoke test green. | #153, M20 render lane |
| **G2** | #134 S2 and #135 S3 | G1; for #135 also D-D | #134: 200 mixed units at mean < 1 ms and max < 2 ms; 240 idle units acquire in < 0.5 ms. #135: `Navigation.VehicleAgent` passes (refuses 3 m, passes 6 m); both agents saved in every level. | Each other, if one session holds the LFS locks (only #135 touches levels) |
| **G3** | #139 S7 | #133, #134 | Formation tests pass; 240-unit dispatch < 5 ms; no server frame > 33 ms on a 240-unit move. | #135 or #136, worked one after the other on `RTSUnit.cpp` |
| **G4** | #136 S4 | #133, #134, #135 | Every placeholder within ±10 % on its binding axis. Vehicles fit exit and dock lanes. AI vs AI skirmish completes. Setup idempotent. Tests read data and are not loosened. Performance gate as decided in D-G. | M20 render lane |
| **G5** | #137 S5 and #138 S6 | G4 (#138 also #133) | #137: producer picks values from the screenshot sheet; default view about 170 m across. #138: `Units.VehicleTurning` passes. | Each other; River lane starts |
| **River lane** | #154 → #155 → #156 ∥ #157 ∥ #158 ∥ #159 → #160 → #161 | #154 needs G4 and D-E. The #155 column gate needs #138 and #139. #160 needs #137, #149, #150. | Per issue; #155 includes a 12-tank column per bridge. | M20 render lane, #140 |
| **M20 render lane** | #151 → #150 → #149 | G1 (#151 touches the running gear in `RTSUnit.cpp`; schedule it away from G3 and G4 sessions) | #151: `RunningGear.Spin` behaviour kept and moving cost measured. #150: team colours unchanged. #149: 4 LODs within budget. | River lane |
| **Close-out** | #140, then #152 | #140 after #137 and #139; #152 after everything | #140: values only from code. #152: full comparison, no unmeasured hardware claims. | — |

**Why this order:**
- #133 is the single unblocker for every other step and fixes the most visible M20 bug.
- #135 must land before #136 because both re-save every level under LFS.
- #139 can move earlier than its number because it only needs S1 and S2. Doing it early removes the 20–40 ms dispatch hitch before the map's bridge column tests.
- The M20 render lane only touches art, materials and running-gear code, so it can run beside the gameplay steps. It must finish before the M20 performance comparison in D-G is judged.
- River Crossing art work (#153, and #156–#159 once greyboxed) does not wait for camera or movement. Its readability and performance sign-off does.

**Work that can run at the same time without file conflicts:**
- #153 alongside everything.
- #134 and #135. They share only the avoidance raycast line; land #134 first, or have #135 rebase.
- #137 and #138.
- The M20 render lane and the River art issues.
- Avoid running two sessions that edit `Source/WorldDivide/Units/RTSUnit.cpp` at the same time. #133, #136, #138, #139 and #151 all touch it.

---

## 6. Remaining risks and blockers

| # | Risk or blocker | Impact | Mitigation |
|---|---|---|---|
| R1 | Three commits are unpushed (`280c810`, `0c42869`, `6b8363e`), and about 700 lines of bench work are uncommitted. | Work could be lost; #132 cannot close; another session could build on the wrong base. | Producer pushes; the bench is committed under #132 (D-B, D-C). |
| R2 | **M20 moving cost of 17.59 ms against 4.20 ms for tanks.** | The #136 gate cannot pass; River Crossing's "200 units at the village" gate is at risk if the M20 is the main unit. | Pull #151 and #150 forward (D-H) and set the M20 comparison after #151 (D-G). |
| R3 | Stale or exceeded targets: plan §6 says 10.8 ms (re-measured at 12.78 ms); `RTS_MOVEMENT_PERFORMANCE.md` sets a 1 ms avoidance cap (1.341 ms measured). | Gates that fail on noise or a changed method. | Use "same build, same method" wording (D-G). |
| R4 | Vehicle nav agent size differs: 190/350 (scale docs) against 200–250/300 (map tech plan). A height of 300 is below the M20's 3.36 m. | Bridges and lanes designed for the wrong agent; navmesh rebuilt twice. | Decide before #135 (D-D). |
| R5 | The map design numbers assume a different scale from the world standard: MBT range 180–220 m against 60–75 m, factory 30×40 m against 24×24 m, 2 against 3 min base to base, max-zoom coverage 500–600 m against about 350 m. | The greybox is laid out to the wrong numbers; sightlines and base pads are wrong. | Reconcile before #154 (D-E). |
| R6 | `WORLD_SCALE_STANDARD.md` is still "proposal"; the plan built on it is "approved". | Ambiguous source of truth. | Mark it approved (D-F). |
| R7 | The two documents list different AI constants: the audit has `BuildingGap`, `GatherDistance`, `DefenseRadius`; the map docs have `BuildSearchDistance`, `DepotSourceDistance`, `ExitLaneLength`, ring step, `BaseRadius`. | #136 misses some, and the AI fails on River Crossing. | Add the union of both lists to #136 (D-A). |
| R8 | LFS locks: #135 and #136 re-save every level, and #154 adds a new one. | Lock contention blocks parallel sessions. | Lock per memory rule, unlock after push; never run #135, #136 and #154 at the same time. |
| R9 | The RTX 3060 / 1080p / 60 fps minimum spec has never been measured, and neither has an eight-player battle. | Performance sign-off has no hardware basis. | Producer sets the target and provides a test machine (D-J). |
| R10 | River Crossing producer items are open: Water and PCG plugins (D8), tree licence, Fab packs, woods blocking vehicles (D4). | #153, #155 and #158 blocked. | D-K. |
| R11 | `RTSUnit.cpp` is touched by #133, #136, #138, #139 and #151. | Merge conflicts between sessions. | Work on it one session at a time (§5). |
| R12 | `PERFORMANCE_RESULTS.md` and the bench code comments cite closed issue numbers. | Future sessions follow the wrong tracker. | Renumber when the bench is committed under #132 (D-B). |

---

## 7. Proposed Claude Code working rules

These are lightweight rules for every implementation session. They extend the existing `/fix-issue` skill and the "Rules for every scale step" block in #132–#139.

1. **One issue per session.** Start with `/fix-issue <N>`. Do not pick up a second issue in the same session, even if it looks small.
2. **Read before writing.**
   - Read the issue, its plan step in `SCALE_IMPLEMENTATION_PLAN.md`, the relevant standard, and **the current code**.
   - Check this report's §2 "missing" column for the issue.
   - Confirm that the dependencies in §5 are merged on `main`.
3. **Propose, then wait.** Present the change, the affected files and the test plan. Implement only after the producer approves.
4. **No unrelated changes.** No drive-by refactors, formatting or doc edits outside the issue. If something else is wrong, write it down (rule 8), do not fix it.
5. **Test and measure.**
   - `ue.ps1 -Task build`.
   - The full `ue.ps1 -Task test` suite green.
   - `ue.ps1 -Task setup` twice when generated content changes; the second run writes nothing.
   - For scale and performance steps, `ue.ps1 -Task bench` for `DA_TestM20Guardian` and `DA_TestTank`, with the numbers recorded next to the baseline.
   - Every acceptance criterion is a pass/fail check or a number, never "looks fine".
6. **Manual test steps first.** Before asking about any commit or push, give the producer the manual test steps. Claude runs build and setup; the producer only opens the project and the level.
7. **One scoped commit per issue**, with `Closes #N` and only the issue's files. No push without the producer's approval. Take the LFS lock when needed and release it after the push.
8. **Write down blockers and decisions.** When a requirement is unclear, conflicts with a document, or needs a producer choice:
   - stop that part;
   - add a comment on the issue (or a line in this report's §8 if it affects several issues);
   - continue only with what is unambiguous.
9. **Stop when done.** After the commit, report the outcome (test results, numbers, anything skipped) and suggest the next issue from §5 with its command. Do not start it.

---

## 8. Producer decisions requiring approval

| ID | Decision | Recommendation | Blocks |
|---|---|---|---|
| **D-A** | Canonical series and duplicate scope | Keep #132–#139 open as canonical and #141–#148 closed. Copy §2's "missing" column into the open issues. Add the union of AI constants (R7) to #136. | All steps |
| **D-B** | Scope of #132 | Extend #132 to cover the bench (#141's scope). Commit the uncommitted bench work as a second S0 commit. Renumber issue references in `PERFORMANCE_RESULTS.md` and the bench code to #132–#139 and #149–#152. | G0 |
| **D-C** | Push the local commits | Push `280c810`, `0c42869` and `6b8363e` after the manual test steps, then close #132 once the bench commit lands. | G0 |
| **D-D** | Vehicle nav agent size | Radius 190 cm, height 350 cm (scale docs). 300 cm is below the M20 hull. Update the River Crossing tech plan §9 to match. Cell size 25–30. | #135, #154 |
| **D-E** | Map design against world standard | The world standard is the source of truth. Revise `02_MAP_DESIGN.md` ranges, building sizes, pacing and camera coverage to the standard (or change the standard deliberately) before #154. | #154 |
| **D-F** | Status of `WORLD_SCALE_STANDARD.md` | Mark it approved (2026-10-15), matching the plan. | All steps |
| **D-G** | Performance gate wording | Replace fixed millisecond targets (10.8 ms; 1 ms avoidance max) with "same build, same bench method" comparisons. In #136, compare placeholder tanks against the step 0 baseline, and move the "240 M20s ≤ 240 tanks" comparison to after #151. | #136, #139, #152 |
| **D-H** | Timing of #149–#151 | Pull the M20 render lane (#151 → #150 → #149) forward to run in parallel after G1, instead of after step 7. | D-G, #160 |
| **D-I** | Marquee rule | Bounds centre (simpler, matches #133's current text), unless the producer prefers rectangle overlap for large vehicles. | #133 |
| **D-J** | Minimum spec and fps target | Set the target (e.g. 1080p / 60 fps with 200 units on RTX 3060 class) and name a test machine; until then, results stay labelled "RTX 4090, minimum spec not measured". | #152, #160 |
| **D-K** | River Crossing items D3–D8 | Decide season, woods against vehicles, garrisoning, relation to `L_RiverFord`, Lumen hardware ray tracing, and the Water and PCG plugins, plus the `SM_HP_Tree` licence, before the issues that need them. | #153, #155, #157, #158, #159 |

### 8.1 Producer decisions (approved 2026-10-15)

The producer approved the recovery with these decisions. Where they differ from the recommendations above, the decision wins.

| ID | Decision | Applied in |
|---|---|---|
| **D-A** | #132–#139 are canonical; #141–#148 stay closed and are not reopened or recreated. The "missing" requirements of §2 move into the open issues; #136 gets the complete list of AI distance constants. | Issue bodies #133–#136, #139 |
| **D-B** | #132 includes the benchmark (#141's scope). Existing bench work is kept; issue references use canonical numbers; the bench gives comparable results for the M20 and the placeholder tank. | #132, `PERFORMANCE_RESULTS.md` |
| **D-C** | The three local commits and the uncommitted work are inspected and kept, with their history. No reset, overwrite or discard. Nothing is pushed until the producer approves. | — |
| **D-D** | Vehicle nav agent radius 190 cm, height 350 cm (the standard). River Crossing's technical plan is reconciled; the implementation stays in #135. | `WORLD_SCALE_STANDARD.md` rule 4, `03_TECHNICAL_PLAN.md` §8–§9, #135 |
| **D-E** | `WORLD_SCALE_STANDARD.md` is authoritative for world scale. River Crossing's design is reconciled to it, keeping its gameplay, symmetry, base-building space, village combat and crossings. The map is not built now. | `02_MAP_DESIGN.md` |
| **D-F** | `WORLD_SCALE_STANDARD.md` is approved. 1 uu = 1 cm. Future world-scale changes need explicit producer approval. | `WORLD_SCALE_STANDARD.md` |
| **D-G** | Measurable budgets are **kept**, not replaced by relative comparisons only. Provisional targets are kept apart from verified measurements. Comparisons use the same build, scene, camera and method. #136 does not fail on the M20's missing render optimisations; final M20 moving acceptance waits for #150 and #151. RTX 3060 and eight-player results are not claimed. | Plan §6 item 7, #136 |
| **D-H** | M20 render optimisation (#151 → #150 → #149) may start after the collision foundation (#133) is stable, separate from shared gameplay work and never concurrent with another session on `RTSUnit.cpp`. Not implemented in the recovery session. | Plan §1, milestone M1b |
| **D-I** | Marquee selection uses the bounds-centre rule in #133; infantry selection stays as it is. | #133 |
| **D-J** | Provisional minimum-spec design target: 1080p / 60 FPS with 200 active units on RTX 3060-class hardware. It is a target, not a result; actual hardware and conditions are recorded with every measurement. | Plan §6 item 7, `PERFORMANCE_RESULTS.md` |
| **D-K** | River Crossing's open producer choices (Fab assets, biome, garrisons, vehicle-blocking woods, lighting, plugins) stay open. | — |
