# RTS movement and performance at real scale

Status: **audit and proposal**, Phase 2A (2026-10-15). The framework stays: an `APawn` with `UFloatingPawnMovement`, a per-unit `AAIController` with path following, and server-side separation. Nothing here replaces it. Each proposal is a local change that can be measured.

## 1. What runs per unit per frame today

| Work | Where | Runs on | Cost driver |
|---|---|---|---|
| Actor tick: orders, `SnapToGround` (one line trace), facing, turret, tilt, fire kick, running gear | `Units/RTSUnit.cpp:617-675` | Orders and the snap on the server; cosmetics on every machine | One ground trace per unit per frame on the server, even when idle |
| `UFloatingPawnMovement` tick | engine | Server, plus replicated simulation | Sweep of the root (never blocks) |
| `UPathFollowingComponent` and `AAIController` ticks | engine, `Units/RTSUnitAIController.cpp` | Server | One controller actor and one component per unit |
| **Weapon target acquisition** | `Combat/RTSWeaponComponent.cpp:79-145` | Server, every frame, every armed unit **and** building | **`TActorIterator` over all units, then all buildings, while the unit has no valid target** |
| Separation | `Units/RTSAvoidanceSubsystem.cpp:39-182` | Server, once per frame | A grid sized by the largest radius |
| Replication | `RTSUnit.cpp:159-163` | Server | `NetUpdateFrequency` 30, `bAlwaysRelevant` |

## 2. Measurements we have

| Scene (RTX 4090, Development editor `-game`, 1080p) | Game thread | Source |
|---|---:|---|
| L_TestMap baseline (38 units) | 2.9 ms | `report_ue5.md:131`, `Saved/Logs/UnitShots.log` |
| + 240 M20 | 11.5 ms | `report_ue5.md:132` |
| + 240 `DA_TestTank` | 10.8 ms | `report_ue5.md:133` |
| First run: M20s overlapping on a 5 m grid | **24 ms in avoidance alone** | `report_ue5.md:148-150` |
| `RTSAvoidanceTests`: 200 small units at 220 cm | Avoidance pass 0.32–0.38 ms mean, 1.23 ms max | `Tests/RTSAvoidanceTests.cpp` log |

**What we can and cannot conclude**
- The model does not drive the game-thread cost: the M20 and the placeholder cost the same. **The per-unit framework drives it**, at about 33 µs per unit per frame.
- That run had no profiler capture, so the split between ticks, AI, acquisition and avoidance is **unmeasured**.
- The 24 ms case is explained by §3.1. Units packed inside each other's radius produce push work for every pair, every frame, in one 3×3 grid neighbourhood that holds almost everyone.

**Step zero for every change below:** an Unreal Insights capture (`-trace=cpu,frame`) of the `-Stress 240` run, with `SCOPE_CYCLE_COUNTER`/`TRACE_CPUPROFILER_EVENT_SCOPE` added to `ARTSUnit::TickOrders`, `URTSWeaponComponent::UpdateWeapon` and `URTSAvoidanceSubsystem::Separate`. Each optimisation is accepted only with a before/after capture.

## 3. Findings and proposals, in expected impact order

### 3.1 Avoidance grid sized by the largest unit (confirmed in code)

- **Today:** `CellSize = 2 × MaxRadius` (`RTSAvoidanceSubsystem.cpp:72`). One M20 (radius 400) makes 8 m cells for every infantryman, so each 3×3 neighbourhood holds dozens to hundreds of units. In tight groups that tends toward O(n²).
- **Today's push clamp:** each push is clamped to the unit's radius per **frame** (`:154`). An M20 can be teleported up to 4 m in one frame. That is the visible "popping" of packed large units, and with every pair overlapping each frame it is also the cost.
- **Proposal:**
  1. **Fixed cell size (about 600 cm).** A unit whose footprint reaches further than one cell is inserted into every cell its bounding circle covers. That is a few cells for a tank and one for infantry. Pairs are deduplicated by index order, as today. This keeps the cost near O(n·k) with k set by local density, not by the largest unit.
  2. **The stadium footprint** from [collision and selection §3.4](VEHICLE_COLLISION_AND_SELECTION.md#34-avoidance-and-formation-footprint-the-stadium). Tanks side by side no longer "overlap" across 4 m of empty air, so there are far fewer pairs.
  3. **Clamp the push per second, not per frame:** at most about `MoveSpeed × 0.5 × DeltaTime`. Crowds settle over a few frames without teleporting.
  4. **Raycast against the unit's own nav data** (vehicle or infantry agent), not `GetDefaultNavDataInstance` (`:146`).
- **Kept:** server-only, the `rts.Avoidance` cvar, yield and sidestep, `ArriveInCrowd`, the navmesh clamp and `LastSeparateSeconds`.

### 3.2 Target acquisition is a full scan every frame (confirmed in code)

- **Today:** `FindNearestInRange<T>` (`RTSWeaponComponent.cpp:79-101`) iterates every `ARTSUnit`, and then every `ARTSBuilding`, for each armed unit or building that has no target in range. It does this **every frame**, and the code comment already says "a spatial query replaces it once armies grow".
  - With 240 idle armed units and no enemy nearby, that is about 2 × 240 × 280 checks per frame. This is the prime suspect for a large share of the 10.8 ms. The Insights capture must confirm it.
  - At real scale, ranges grow about 5× and an engagement involves more units, so the cost only rises.
- **Proposal:**
  1. **Rescan interval:** the search runs at about 5 Hz per weapon, staggered by unit index. Keeping and validating a current target stays per frame.
  2. **Spatial query:** the avoidance subsystem already builds a per-frame cell structure of ground units. It exposes a `QueryRadius(Center, Radius)` that the weapon uses instead of `TActorIterator`. Buildings are few and static, so they use a per-team list kept by the game state.

### 3.3 Idle units pay full price (confirmed in code)

- **Today:** `SnapToGround` traces every frame for every ground unit (`RTSUnit.cpp:594-615`), even when the unit has not moved. The running gear already early-outs on "no movement" (`report_ue5.md:105`). The ground snap and the tilt do not.
- **Proposal:**
  - Skip the trace when the actor has not moved more than 1 cm since the last snap.
  - Do the same for the cosmetic tilt trace on clients.
- **Optional, measured first:** lower the actor tick rate of idle units without targets (`SetActorTickInterval(0.1)` while idle, 0 when ordered).
  - Turret aim and fire kick need per-frame updates only while firing.
  - **Risk:** tick-interval changes alter timing in tests that step frames. It goes behind its own issue.

### 3.4 AI controllers and path following (to measure)

- **Today:** 240 units means 240 `AAIController` actors and 240 path-following components ticking. `ARTSUnitAIController` overrides only `OnMoveCompleted`.
- **Proposal:**
  - Disable the controller **actor** tick, since nothing in `ARTSUnitAIController` needs it. Path following ticks as its own component.
  - Measure the rest.
  - Do **not** move to Mass or a custom movement framework unless the capture shows the engine path-following tick dominates after §3.1–3.3. Nothing measured so far points there.

### 3.5 Formation slot assignment is O(n³) (confirmed in code)

- **Today:** greedy "globally closest free pair" assignment (`Commands/RTSFormation.cpp:42-84`). A select-all move of 240 units is about 14 M distance checks in one server frame, roughly a 20–40 ms hitch.
- **Spacing:** `max(radius) × 2.5` in both directions (`RTSCommandDispatch.cpp:72`). With the M20 that is a 10 m grid even for infantry, and a square grid wastes space for long vehicles.
- **Proposal:**
  - **Spacing:** use the [stadium footprint](VEHICLE_COLLISION_AND_SELECTION.md#34-avoidance-and-formation-footprint-the-stadium): column spacing = max W + 2.5 m, row spacing = max L + 3 m.
  - **Assignment:** sort units by their projection on the facing axis into rows (front units take front rows), then sort each row by the side axis, then assign in order. That is O(n log n), and units seldom cross paths, which is what the greedy pass approximates today.
  - **Mixed selections:** group by class (infantry, vehicles), with vehicles in front. That is optional and a design choice.
  - **Not proposed:** speed matching (a formation moving at its slowest unit). It is a design question for later.

### 3.6 Vehicle turning and acceleration (confirmed in code)

- **Today:**
  - The facing interpolates toward the **velocity** at `TurnRate` (`RTSUnit.cpp:662-681`).
  - Velocity follows the path direction regardless of facing (`MoveToLocation(..., bCanStrafe=true)`, `:920-923`).
  - A 9.8 m tank reversing its course moves sideways and backwards at full speed while its hull turns.
  - Acceleration and deceleration are the `UFloatingPawnMovement` defaults (4000 / 8000 cm/s²), so a 60 t tank reaches full speed in a quarter of a second.
- **Proposal:** a small `URTSUnitMovement : UFloatingPawnMovement`, which is the one new class this plan adds. It overrides only `ApplyControlInputToVelocity` and does what an RTS vehicle needs:
  - **Tracked vehicles** (`bPivotTurn = true`) turn the hull toward the **path direction**. Speed is scaled by alignment, `max(0, cos(heading error))`. Beyond about 60° of error the vehicle stops and pivots in place. Velocity always runs along the hull.
  - **Wheeled vehicles** (`MinTurnRadius > 0`) cap the yaw rate at `speed / MinTurnRadius` and slow for sharp corners. Below a crawl speed they may pivot slowly, so they never orbit a waypoint.
  - **Infantry** (both unset) behave exactly as today.
  - **Data:** `Acceleration` and `Deceleration` per unit in `URTSUnitData`, copied to the movement component in `ApplyUnitData` as `MaxSpeed` already is.
  - **Multiplayer:** the server only, through replicated movement. Clients do not simulate it.
  - **Risks:**
    - Arrival with a deceleration distance overshoots a 25 cm acceptance radius, so it grows to 50 cm.
    - Attack-move approaches need the vehicle to face its route first, which adds about 1 s for MBTs.
    - Tests that time moves need new tolerances.

## 4. Draw calls (noted, out of scope)

`ARTSUnit` creates one dynamic material instance per slot per unit for team colour, which stops instancing across units (`report_ue5.md:144-147`). It is graphics work and stays out of this phase. It is listed in the implementation plan as a follow-up next to LODs.

## 5. Performance acceptance targets

The same machine and the same `ue.ps1 -Task unitshots -Stress 240` method are used throughout.

| Scene | Target |
|---|---|
| 240 M20 at real-scale spacing | Game thread ≤ the 240-tank number measured on the same build; no avoidance frame above 1 ms |
| `RTSAvoidanceTests` with 200 mixed units, 20 of them M20-sized | Avoidance pass mean < 1.0 ms (today's budget), max < 2 ms |
| Select-all move of 240 units | No server frame above 33 ms |
| 240 idle armed units, no enemies | Weapon acquisition < 0.5 ms per frame in total |
