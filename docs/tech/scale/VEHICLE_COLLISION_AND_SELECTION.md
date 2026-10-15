# Vehicle collision, selection and footprint

Status: **audit and proposal**, Phase 2A (2026-10-15).

## 1. How it works today

`ARTSUnit` (`Units/RTSUnit.h:28`) is an `APawn`.
- **Root:** a `UCapsuleComponent` named `Collision` (`RTSUnit.cpp:116`), which is also the `UpdatedComponent` of a `UFloatingPawnMovement` (`:143-147`).
- **Collision responses** (`:121-129`): profile `Pawn`. It ignores WorldStatic and WorldDynamic, overlaps Pawn and blocks Visibility. Overlap events are off and it does not affect navigation.
- **Physical contact:** units never block anything physically. Only `URTSAvoidanceSubsystem` keeps them apart.

One data field, `URTSEntityData::SelectionRadius` (`Data/RTSEntityData.h:49`, commented "collision, selection brackets and formation spacing"), sizes everything:

| Use | Where |
|---|---|
| Capsule radius, and through `max()` the capsule half height | `RTSUnit.cpp:244-257` |
| Actor height above ground (`GetGroundOffset` = capsule half height) | `:524-532`, `:609` |
| HUD brackets and health bar | `Player/RTSHUD.cpp:181-222` |
| Avoidance circle | `Units/RTSAvoidanceSubsystem.cpp:58`, `:101` |
| Formation spacing (`max radius × 2.5`) | `Commands/RTSCommandDispatch.cpp:72` |
| Spawner spacing (`radius × 2.5`) | `Units/RTSUnitSpawner.cpp:37` |
| Building exit (`radius + 80`) | `Buildings/RTSBuilding.cpp:347` |
| Weapon target edge (`GetClosestFootprintPoint`) | `RTSUnit.cpp:518-522` |
| Dock and build stand-off, `HoldShortOf`, builder and harvester reach | `RTSUnit.cpp:707`, `:720`, `:802`; `RTSBuilderComponent.cpp`; `RTSHarvesterComponent.cpp` |

## 2. The M20 bracket bug

```cpp
const float HalfHeight = FMath::Max(MeshHalfHeight, Radius);   // RTSUnit.cpp:256
Collision->SetCapsuleSize(Radius, HalfHeight);
```

- **Capsule size.** A capsule's half height can never be less than its radius. `UCapsuleComponent::SetCapsuleSize` clamps it as well. With the M20's 400 cm radius, the capsule is **8 m tall and 8 m wide**, around a hull about 3.4 m tall.
- **Origin height.** The actor origin is the capsule centre, **4 m above the ground**.
- **Brackets.** `GetUnitScreenCircle` centres the brackets on `GetActorLocation()`, so they float above the hull. The health bar floats with them.
- **Marquee.** `SelectInBox` (`RTSPlayerController.cpp:656-681`) tests the same origin, so a drag box has to cover the air above the tank.
- **Picking.** Picking uses the Visibility trace against the capsule (`:247-262`), so clicking empty air up to 8 m above or beside the hull selects the M20, and the sphere-like ends catch clicks meant for neighbours.
- **Width.** A real tank is about 2.7× as long as it is wide. Any single radius is either too wide at the sides (radius = half length) or too short at the ends (radius = half width).

**Conclusion: a capsule cannot describe a large elongated vehicle.** No `SelectionRadius` value works for brackets, picking, avoidance and formation at the same time. The fix is to separate the concepts, not to tune the number.

## 3. Five separate concepts

| Concept | Purpose | Shape | Source |
|---|---|---|---|
| **Visual bounds** | Camera framing, screenshots, culling | Mesh bounds (hull, turret, gun, parts) | Engine, automatic |
| **Physical collision** (root) | Picking, the movement component, the overlap test in building placement | **Box**, the hull box | `GetHullBox` × `MeshScale`, automatic |
| **Selection bounds** | Brackets, health bar, marquee | Ground-anchored box: hull box plus turret, minus the gun | Derived, automatic |
| **Nav footprint** | Pathfinding clearance | Cylinder of the nav agent | Picked from width: infantry or vehicle agent |
| **Avoidance and formation footprint** | Separation, formation gaps, exits, target edge | 2D oriented **stadium** (a capsule lying flat) | Derived from hull length and width |

`SelectionRadius` keeps only its **interaction** meaning: dock stand-off, build reach and `HoldShortOf`. Its comment changes to say so.

### 3.1 Physical collision: box root

- **Replace the capsule root with a `UBoxComponent`** whose extent is the hull box (`GetHullBox` × `MeshScale`). The box is fitted in `ApplyUnitData`, as the capsule is today.
- **Same collision responses** as the capsule: ignore WorldStatic and WorldDynamic, overlap Pawn, block Visibility.
  - Picking becomes exact to the hull, and the turret or gun no longer matters.
  - Building placement's Pawn overlap (`RTSBuildPlacement.cpp:99-112`) now sees the real footprint.
- **`UFloatingPawnMovement` still works.** It accepts any `UPrimitiveComponent` as `UpdatedComponent`. Its sweeps never block, because the root ignores the world and only overlaps pawns, so the shape does not change movement.
- **`GetGroundOffset()` returns the box half Z.** The origin moves to hull mid-height, about 1.7 m for the M20 instead of 4 m. `SnapToGround`, `GetNavAgentLocation`, the tilt pivot (`:589`), the spawner (`RTSUnitSpawner.cpp:50`), the building exit (`RTSBuilding.cpp:355`), the aircraft pad and cruise Z (`RTSAircraftComponent.cpp:85`, `:245`, `:332`), the UI test state and the debug grid all keep working: they already go through `GetGroundOffset()`. The pivot uses the capsule directly and changes to the box.
- **Infantry** gets a narrow box (about 50 × 50 × 180). A capsule would also do, but one root type keeps the code simple.
- **Aircraft** get the same box. They fly by `URTSAircraftComponent`, and the box only matters for picking.
- **Multiplayer:** nothing changes. Replicated movement sends location, rotation and velocity, not the shape. The box extent comes from `UnitData`, which already replicates once (`COND_InitialOnly`), and every machine runs `ApplyUnitData`.

**Alternative considered:** keep the capsule but cap its radius at half the hull width. It still leaves picking spheres at the ends, misses the hull's front and rear in picking, and still needs a separate footprint for everything else. Rejected.

### 3.2 Selection bounds

- **Add `FBox ARTSUnit::GetSelectionBounds() const`.** It returns a world-space box: the hull box plus turret, from the ground to the roof, rotated with the actor.
- **`ARTSHUD`:**
  - `GetUnitScreenCircle` becomes `GetUnitScreenRect`, which projects the 8 corners and takes their screen rect, with a minimum of 16 px.
  - `DrawUnitMarker` draws the brackets on that rect. The health bar sits above the rect's top edge, as now.
- **Marquee:** `SelectInBox` tests the **bounds centre**. Alternatively it can test any overlap of the screen rect with the drag box, which is Generals' feel; the producer chooses.
- Buildings already use footprint corners (`RTSHUD.cpp:246-259`), so units and buildings then work the same way.

### 3.3 Nav footprint: two agents

Today one default agent (radius 34 cm) serves every unit, so a 3.7 m wide tank plans paths through 0.7 m gaps and clips corners. `03_TECHNICAL_PLAN.md` §9 recommends two agents. This document confirms the numbers for the real-scale widths:

| Agent | Radius | Height | Used by |
|---|---:|---:|---|
| Infantry | 40 cm | 180 cm | Units with a hull half width ≤ 60 cm |
| Vehicle | 190 cm | 350 cm | Every other ground unit (the widest is 3.7 m, so its half width is 185 cm) |

- **Configuration:** `[/Script/NavigationSystem.NavigationSystemV1] +SupportedAgents=(...)` in `Config/DefaultEngine.ini`. Each agent gets its own `ARecastNavMesh` in each level.
- **How a unit picks its agent:** `ApplyUnitData` sets `Movement->NavAgentProps.AgentRadius` and `AgentHeight` from the hull width. `AAIController::MoveTo` then picks the matching nav data through `GetNavDataForProps`, with no other code change.
- **Code that names the default nav data has to use the unit's own:**
  - `RTSAvoidanceSubsystem.cpp:146` uses `GetDefaultNavDataInstance` for its raycast clamp. It must use the unit's nav data, or a tank could be pushed into an infantry-only gap.
  - Anything else that calls `ProjectPointToNavigation` or `Raycast` without the props (to be listed in the implementation issue by searching for `GetDefaultNavDataInstance` and `GetNavigationSystem`).
- **Building placement** keeps checking the infantry nav data (the finest). Separately, it checks that the vehicle agent can reach the exit of a factory placement.

### 3.4 Avoidance and formation footprint: the stadium

Each ground unit gets a 2D footprint: a segment along its heading of length `L - W` (clamped at 0), swept by radius `W / 2`.
- A tank (9.8 × 3.7) is a 6.1 m segment with a 1.85 m radius.
- Infantry (0.5 × 0.5) is a point with a 0.25–0.4 m radius, which is a circle.

- **Cost:** the segment-segment closest-points distance is closed-form, a few dot products and two clamps. Overlap means distance < rA + rB. The push direction is the closest-points direction. That is a few more flops than today's circle test, with no allocation.
- **Fit:** a 9.8 × 3.7 tank is covered within about 0.4 m at the corners, without the 4.9 m sideways reach that a 4.9 m circle gives.
- **Turning:** turning a stadium never lets the hull pass through a neighbour, because the footprint turns with the actor.
- **Where it is also used:**
  - `GetClosestFootprintPoint` returns the closest point on the stadium edge, so weapon range is measured to the hull, not to a circle.
  - **Formation** spacing (`RTSCommandDispatch.cpp:72`): column spacing = max W + 2.5 m, row spacing = max L + 3 m. Spacing is no longer max radius × 2.5 in both directions.
  - **Spawner and building exit:** half L + clearance along the exit direction.
- **No new hand-written fields.** L and W come from the hull box. One optional override, `FootprintPadding` (cm, default 0), is added only if the playtests ask for it.

## 4. Is a capsule plus avoidance enough?

| Question | Answer |
|---|---|
| Is a capsule enough for elongated vehicles? | **No.** It cannot be flatter than a sphere, and one radius cannot fit both length and width. |
| Is a box root plus a stadium footprint enough? | **Yes**, for an RTS with server-side separation, no physics contact and navmesh paths. It covers picking, brackets, separation, exits and range edges. |
| Is a per-vehicle convex hull or multi-shape collision needed? | No. Nothing in the game needs contact physics. |
| Is a new movement framework needed (Mass, Chaos vehicles, crowd)? | No (see [movement performance](RTS_MOVEMENT_PERFORMANCE.md)). |
| Multiplayer | Unchanged. Everything is derived from replicated data on every machine. Separation and pathfinding stay on the server. |

## 5. Data change summary

| Field | Change |
|---|---|
| `URTSEntityData::SelectionRadius` | Interaction only: dock, build reach, hold short. Buildings keep it as is. The comment changes. |
| (derived) hull L, W, H | Computed once in `ApplyUnitData` from `GetHullBox` × `MeshScale` and cached on the unit. |
| `URTSUnitData::Acceleration` / `Deceleration` / `MinTurnRadius` / `bPivotTurn` | New; see the movement document. |
| `FootprintPadding` | Only if needed. |
