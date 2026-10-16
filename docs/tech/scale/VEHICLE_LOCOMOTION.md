# Vehicle locomotion: tracks and wheels (#138)

Status: implemented in #138. This replaces the heading-only proposal of [movement performance §3.6](RTS_MOVEMENT_PERFORMANCE.md#36-vehicle-turning-and-acceleration) and plan step 6.

**Principle.** Tracks or wheels decide how a vehicle moves. The navmesh decides where it wants to go.

## 1. Three layers

| Layer | Where | What it does |
|---|---|---|
| Navigation | `URTSPathFollowingComponent`, `RTSHullClearance::AdjustPath` | Finds the path and fits it to the hull, as before. For vehicles, path following no longer moves the pawn. It hands every path to the gear (`URTSUnitMovement::SetPath`). It counts a path point as reached once the gear's guidance has passed it. Its own block detection is off; the unit has its own (§5). |
| Locomotion (server) | `URTSUnitMovement`, `RTSLocomotion` | Guidance turns the path into a command (a speed and a curvature, or a pivot). The gear's kinematics turn the command into an arc. The arc is checked against obstacles and the hull moves along as much of it as is clear. |
| Presentation (every machine) | `FRTSRealizedMotion` | Says what the gear actually did. #151 animates the M20's tracks from it. Animation never moves the hull. |

Infantry (`Legs`) and aircraft are unchanged:
- Infantry keeps the floating pawn movement, with the hull turned toward its velocity.
- Aircraft keep `URTSAircraftComponent`.

## 2. Data: `URTSUnitData::Locomotion` (`FRTSLocomotionProfile`)

| Field | Tracked | Wheeled | Meaning |
|---|---|---|---|
| `Type` | `Tracked` | `Wheeled` | `Legs` (the default) keeps the old movement. |
| `MoveSpeed` (unit) | ✓ | ✓ | Top speed forward, cm/s. |
| `TurnRate` (unit) | ✓ | ✓ | Fastest yaw, degrees/s. |
| `ReverseSpeed` | ✓ | ✓ | Top speed backward. 0 means the vehicle never reverses. |
| `Acceleration`, `Braking` | ✓ | ✓ | cm/s² toward a faster speed, and toward a slower one or a stop. |
| `MaxLateralAcceleration` | ✓ | ✓ | Grip. A turn slows the vehicle to keep v²·κ within it. |
| `TrackGauge` | ✓ | | Distance between the middles of the tracks. |
| `bAllowPivot`, `PivotAngle` | ✓ | | Turn on the spot when the way lies more than `PivotAngle` (default 60°) off the heading. |
| `Wheelbase`, `AxleTrack` | | ✓ | Axle to axle; left wheels to right wheels. |
| `MaxSteerAngle`, `SteerRate` | | ✓ | Steering lock and how fast the wheels steer. |
| `MinTurnRadius` | | ✓ | Optional. The effective radius is `max(MinTurnRadius, Wheelbase / tan(MaxSteerAngle))`. |

**Validation:**
- `FRTSLocomotionProfile::Validate` requires finite, positive values for the fields the type uses.
- It runs in the editor's data validation (`URTSUnitData::IsDataValid`) and when a unit applies its data.
- At runtime an invalid profile logs a warning and the unit walks on legs.

**Adding a vehicle means adding a profile; it needs no code.** The test data from `setup_testmap.py`:

| Asset | Type | Fwd / rev cm/s | Accel / brake cm/s² | Geometry |
|---|---|---|---|---|
| `DA_TestM20Guardian` | Tracked | 1250 / 400 | 400 / 1000 | gauge 264 (its track parts) |
| `DA_TestTank` | Tracked | 1100 / 350 | 350 / 900 | gauge 290 |
| `DA_TestAbrams` | Tracked | 1000 / 300 | 300 / 850 | gauge 300 |
| `DA_TestFlakTruck`, `DA_TestHarvester` | Wheeled | 1400 or 1300 / 500 | 350 / 900 | wheelbase 330, axle 210, 35° at 60°/s: R<sub>min</sub> 4.7 m |
| `DA_TestBuilder` | Wheeled | 1100 / 400 | 300 / 900 | wheelbase 400, axle 250, 35°: R<sub>min</sub> 5.7 m |

## 3. Equations and conventions

The axes and yaw follow Unreal: X forward, Y right, Z up. Yaw is positive to the right (clockwise seen from above). Units are cm, s and radians inside `RTSLocomotion`.

**Tracked (differential drive).** Track speeds `vL` and `vR`, signed:

```
v      = (vL + vR) / 2
yawRate = (vL − vR) / gauge        (positive: turning right)
```

#138 writes `yawRate = (vR − vL) / gauge` with yaw positive to the left; it is the same turn. Equal speeds drive straight, unequal speeds turn, and opposite speeds pivot.

A command (speed `v*`, curvature `κ`) sets the targets:
- yaw `ω* = v*·κ`, clamped to `TurnRate`;
- `vL* = v* + ω*·gauge/2` and `vR* = v* − ω*·gauge/2`.

Both targets are scaled down together, keeping the curvature, until neither track runs faster than `MoveSpeed` forward or `max(ReverseSpeed, TurnRate·gauge/2)` backward.

**Wheeled (bicycle model):**

```
steer target = atan(κ · wheelbase), clamped by the steering lock and the turning circle
yawRate      = v · tan(steer) / wheelbase
```

Steering moves toward its target at `SteerRate`. A wheeled hull cannot turn while standing. Reversing turns it the other way. The yaw limit is kept by easing the steering at speed, which widens the turn and never tightens it.

**Ramps.** Every speed (each track, or the wheels' speed) moves toward its target:
- at `Acceleration` when getting faster;
- at `Braking` when getting slower;
- through zero first when it changes sign.

The distance driven over an update is the exact integral of that ramp.

**Integration.** Over an update the hull drives the distance `ds` and turns `dθ`. In its own frame at the start it ends at the exact chord of that arc:

```
x = ds · sin(dθ)/dθ,   y = ds · (1 − cos dθ)/dθ
```

There is a series near `dθ = 0`. Results at 30, 60 and 120 updates a second agree within 1 % and 1° (`WorldDivide.Units.VehicleTurning`).

**Reference point.** The kinematics act on the actor's origin, the middle of the hull. For tracks that is exact. For wheels it means the bicycle model's rear axle sits at the hull's middle, so a wheeled hull has no sideslip angle (§9).

## 4. Guidance (`RTSLocomotion::Guide`)

- **Pursuit.**
  - The pursued point lies `0.5 s × speed + 1 m` (between 1.5 and 15 m) toward the next point of the hull-fitted path, never past it; for wheels at least 0.75 R<sub>min</sub>.
  - The curvature is that of the arc tangent to the hull through the point: `κ = 2·y / d²`.
- **Speed.** The lowest of:
  - the top speed times avoidance's share;
  - `√(2·b·d)` to stop exactly at the goal, or at a sharp corner for tracks;
  - the grip limit `√(a_lat/κ)` and the yaw limit `ω_max/κ`;
  - for every corner within reach, `√(v_c² + 2·b·d)`, with `v_c = √(a_lat·R)`.
- **Tracks:**
  - They brake to a stop and pivot when the way lies more than `PivotAngle` off the heading. The pivot ends within 8° of the heading.
  - They do the same at a path corner sharper than 30°: `AdjustPath` already clears the hull at every heading on those points.
  - Speed is scaled by `cos(heading error)` while the heading swings onto the way.
- **Reverse.** A goal less than 1.5 hull lengths behind, on the last stretch, is reached in reverse when the profile reverses. A goal further behind is pivoted to (tracks) or swung round to on the turning circle (wheels).
- **Wheels and a goal inside their turning circle.**
  - The vehicle backs out of the circle, or swings out of it without reverse, then drives on.
  - It does this at most 3 times. After that guidance fails and the move ends as blocked (§5).
  - The acceptance distance is never widened.
- **Arrival.** Path following keeps its acceptance distances (25 cm for moves). At the moment path following finishes, the gear's speed is about `√(2·b·0.25 m)`, so it brakes to a stop at the path's end. Stopping a path (Stop, arrival, an attack in range) lets the gear brake. Only the end of the match and teleports stop it dead (`URTSUnitMovement::HaltImmediately`).
- **Standing.** A standing tracked unit whose target its hull must face (no turret, or a turret whose barrel is blocked) pivots toward it. A wheeled one cannot.

## 5. Collision and blocking

Every update the server checks the arc's end pose before it moves the hull (`URTSUnitMovement::CommitStep`):

1. **Hull against obstacles**, the same way avoidance keeps hulls out of them (#163). The pose is clear when:
   - the hull's clearance cache says so; or
   - the navmesh clearance (`RTSHullClearance::GetClearance`) is 10 cm or more.

   Closer than that the navmesh may read an obstacle nearer than it is: by a cell beside a mesh's face, and by up to (√2 − 1) of its erosion beside a box obstacle's corner. In that band the collision decides (`IsSolidClear`):
   - The hull box grown by 2 cm overlapping nothing solid above step height is clear.
   - Touching nothing is clear too, but is checked again every update.
   - A hull moving along or away from what it touches may graze it by at most 1 cm. Avoidance backs a hull out of anything it touches.

   No physics sweep of the root is made. The root box ignores the world on purpose: a box resting on the ground would count as penetrating it.
2. **Ground support.** A pose off the navmesh is refused. Where the navmesh ends without anything solid (the edge of the map), the ground must be under all four of the hull's corners. Slopes and steps are what the navmesh allows.
3. **Turning.** An update that swings the hull's corners more than 10 cm is checked halfway too, so a pivot cannot swing through a corner.
4. **Barrel (#162).** A barrel near an obstacle that is clear now may not be swung or driven into it. A barrel already in one may only drive straight out. This replaces the hull-turn revert in `TickOrders` and the drive guard in avoidance for vehicles.
5. **A step that is not clear.**
   - Tracks turn on the spot instead, if that is clear: first toward the path point they head for, else parallel to a wall they drive into at a slant (up to 30°). They drive on along it the next update and never slide sideways.
   - Otherwise the gear tries ½, ¼, ⅛, 1/16 and 1/32 of the step, so at top speed it stops within about a centimetre of the obstacle. Then it stops: its speeds are set to zero and it reports `Blocked`. It does not try the same way straight again until something else moved it or it is told to go another way, so driven travel stops in that update.

A pose that is not clear may still be taken when it lies no deeper in an obstacle than the hull already is and does not move toward it. A straight move along or away from an obstacle never counts as deeper. So a vehicle can always back out of a tight spot.

**Blocked moves** (`ARTSUnit::UpdateBlockedMove`):
- A vehicle stuck for 0.6 s backs off and paths again:
  - tracks back off straight for 0.4 s at 2 m/s;
  - wheels back off for 1.5 s, steering so their nose swings toward the way.
- Stuck again within 3 s, or with guidance failed, it gives the move up where it stands.
- It backs off at most once per move, so it does not rock back and forth (`WorldDivide.Navigation.VehicleBlocked`).

**Avoidance (`URTSAvoidanceSubsystem`):**
- Pushes still part overlapping hulls, but the gear reports them as recovery, not driving.
- A walking vehicle pushed back against its way is told to slow down (`SetAvoidance`).
- Two vehicles head on steer right on a 20 m curve instead of stepping sideways.
- Hull back-out is unchanged.

**Holding short of new buildings.** `HoldShortOf` looks ahead by the braking distance plus 1 m.

## 6. Realized-motion API for #151

```cpp
const FRTSRealizedMotion& ARTSUnit::GetRealizedMotion() const;   // every machine
bool URTSUnitMovement::UsesLocomotion() const;
```

| `FRTSRealizedMotion` field | Meaning |
|---|---|
| `ForwardSpeed` | cm/s along the hull, after collision; negative when reversing. |
| `YawRate` | Degrees/s, positive to the right. |
| `SteerAngle` | Front wheels, degrees, positive to the right; 0 for tracks. |
| `Status` | `Idle`, `Driving`, `Reversing`, `Pivoting`, `Braking` or `Blocked`. |
| `DrivenDistance`, `TurnedRadians` | Running totals of driven travel and turn. |
| `LeftTravel`, `RightTravel` | Running totals of each side's travel, as below. |
| `CommandedSpeed`, `CommandedYawRate` | Server only: what the gear was told (the command, not the outcome). |
| `RecoveryDisplacement`, `RecoveryCount` | Server only: what moved the hull that the gear did not drive. |

Each side's travel, with `spacing` the track gauge or the wheels' axle track:

```
dLeft  = ds + spacing · dθ / 2
dRight = ds − spacing · dθ / 2
```

**Animate from differences of the running totals.** On every machine:
- The server adds only what the gear drove.
- Clients add `ForwardSpeed · dt` and `YawRate · dt` from the replicated state (§7).
- Pushes, back-outs, teleports and network corrections never count.
- A late joiner starts its totals at zero.

`ARTSUnit::UpdateRunningGear` already rolls the M20's wheels this way.

## 7. Replication

- Locomotion runs only on the server. Clients never move a vehicle's hull.
- The hull's pose and velocity replicate as before: `ReplicatedMovement` at 30 Hz, no client prediction.
- The velocity is the realized forward speed along the hull.
- Clients also get `ARTSUnit::LocomotionNet` (`FRTSLocomotionNet`): 4 bytes, sent only when they change.
  - yaw rate in ¼°/s;
  - steering in ½°;
  - status.
- Nothing per track link or per wheel is replicated, and there is no RPC.
- `rts.Net.LocomotionState 0` stops writing it, for traffic comparisons.

Tests:
- `WorldDivide.Net.Vehicle.ListenServer`;
- `WorldDivide.Net.Vehicle.Dedicated`: two clients, 100 ms of round trip, about 2 % loss.

## 8. Performance

- Per vehicle and update: one guidance pass over its path, two speed ramps, and at most a few clearance queries. Those queries happen only once the hull may have closed the gap its clearance cache allows, so in open ground they are rare.
- The hull moves with a single `MoveUpdatedComponent` without a sweep.
- Nothing allocates per frame. The path is copied once per path.
- Measurements: [PERFORMANCE_RESULTS.md, step 6](PERFORMANCE_RESULTS.md#step-6-vehicle-locomotion-138).

## 9. Known limitations

- **Obstacles the navmesh does not know**, such as props left out of navigation, do not stop a hull. The hull is only checked against solid geometry within 10 cm of a navmesh wall, as before #138. Barrels are checked against everything (#162).
- **No sideslip for wheels.** The bicycle model acts on the hull's middle. Wheeled vehicles have no lateral grip loss, suspension, drivetrain or terrain effect beyond the navmesh's slopes.
- **Wheels in tight spaces.** `AdjustPath` fits corners for a hull that turns on the spot. A wheeled vehicle rounds them on its turning circle, swinging wide of them. In a passage too tight for its circle it backs off once and then gives the move up (§5). Docking uses the same guidance.
- **No corner cutting.** Guidance heads for each path point in turn rather than pursuing a point past it: cutting a corner would take the hull toward what it turns round. Vehicles therefore follow the path's polyline, braking for its corners.
- **Clients snap to the replicated pose** at 30 Hz, as before; there is no new smoothing. The gear's realized travel on clients is smooth regardless.
- **The M20 at 240 moving units** misses #138's locomotion p95 budget: 2.38 ms against 2.0 ms. The mean, 0.59 ms, is within budget. Most of it is the engine moving the hull with all its parts' transforms ([performance](PERFORMANCE_RESULTS.md#step-6-vehicle-locomotion-138)); it belongs with the M20's parts and LOD work (#150, #151).
- **The Abrams on a machine with its local art** keeps its own copy of `DA_TestAbrams` (`skip-worktree`), so it walks on legs there until setup rewrites it on a machine without that art.
- **Pushes from avoidance** still move vehicles sideways while their hulls overlap, as recovery. Two vehicles that meet head on in a passage one hull wide are not resolved beyond what avoidance does.
