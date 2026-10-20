# USA Default HQ: visual reference and technical spec

Status: **draft for producer approval** (#185). Once approved, this is the single source for the USA HQ's look, size, modules and
UE5 setup; every downstream issue (#121, #186 to #192, #196) builds to it. A deviation from the image or from a number marked
*decided* needs a recorded producer decision in §14.

Values marked *proposal* are mine and wait for the producer's approval at gate 2. Values marked *decided* are producer
decisions (2026-10-20) or come from the [world scale standard](../../tech/scale/WORLD_SCALE_STANDARD.md).

## 1. Approved reference

![USA HQ approved concept](../refs/USA_HQ_Default_Approved.png)

| | |
|---|---|
| File | [`docs/art/refs/USA_HQ_Default_Approved.png`](../refs/USA_HQ_Default_Approved.png) (Git LFS) |
| Size | 1402 × 1122 px, PNG, 3,552,255 bytes |
| SHA-256 | `c110c1d67aefe43c5dddb0404abbc8d05d2fef1d788200ce517bc63ae7a5c8e4` |
| Source | Producer-supplied compact campus concept, approved 2026-10-18 |

The image is the visual source of truth. It is an isometric concept, so proportions are read from it and the numbers in §3
fix them; where the image and a number disagree, the number wins and the image keeps the look.

## 2. Orientation and local frame (*decided*)

- 1 uu = 1 cm, Z up, origin at the footprint centre on the ground (standard §1).
- **Local +X = the garage face**: the slab's **lower-right edge** in the image. Production exits on +X
  (`ARTSBuilding::GetExitLocation`).
- Facing +X, local **+Y is to the right**: the slab's lower-left edge in the image (the administration wing's side).
  **-Y** is the upper-right edge (radar and satcom side). **-X** is the upper-left edge (the rear).
- Image corners in local terms: bottom corner = (+X, +Y), right corner = (+X, -Y), top corner = (-X, -Y), left corner = (-X, +Y).
- The concept draws the ceremonial axis (core entrance, steps, courtyard, flag) on the diagonal towards the bottom corner
  (+X, +Y). That stays as drawn: it is the building's "front" to the camera, not its exit.

## 3. Dimensions, cross-checked against the standard and the tests

Each line names the rule it is checked against. "Test" means `Source/WorldDivide/Tests/RTSScaleTests.cpp`.

| # | Item | Value | Status | Checked against | Result |
|---|---|---|---|---|---|
| 1 | Unit of measure | 1 uu = 1 cm, +X forward, Z up | decided | Standard §1 | OK |
| 2 | Footprint | **28 × 28 m** (2800 × 2800 cm, 28 × 28 cells of 1 m) | decided | Standard §3 Buildings (HQ 28 × 28); `RTSBuildPlacement::GridSize = 100`; test `BuildingStandards` `FIntPoint(28, 28)` | OK, whole metres |
| 3 | Overall height | **24.0 m** to the tallest mast tip (core antenna cluster), whole mesh | decided | Standard §3 (18–25 m); test band 1800–2500 cm, `SizeTolerance` 0.1 → passes 1620–2750 cm | OK: 2400 inside the strict band |
| 4 | How height is measured | Whole root-mesh bounding box Z × `MeshScale.Z` (`GetMeshHeight`), so mast tips count | decided | Test `GetMeshHeight` | The 24 m mast **must be in the static root mesh** (see §11) |
| 5 | Command core roof | ~14.0 m (upper drum to ~16.5 m) | decided (~) | Proportion only | — |
| 6 | Compression | Modelled at compressed size, about 0.6–0.7× a real HQ campus | decided | Standard §2, rule 5 | OK |
| 7 | `MeshScale` | **(1, 1, 1)**: the final mesh is modelled at size; no `building_fit` stretch | proposal | Standard rule 5; `setup_testmap.py` `building_fit` stretches placeholders only | #191 must not run the fit on the final mesh |
| 8 | Built mass inside footprint | Mesh plus apron props within ±1400 cm on X and Y; paved slab edge = footprint edge | proposal | Footprint box of test `GetFootprintBox` | Q1 in §15 |
| 9 | Design vehicle | Engineering vehicle **6.5 × 3.0 × 3.2 m** (L × W × H) | decided | Standard §3 engineering vehicle | Envelope for every garage number |
| 10 | Current builder mesh (measured) | 446.2 × 300.0 × 273.2 cm (see §5) | measured | Test `UnitStandards` builder width 300 ± 10 % | Smaller than the envelope on L and H; same W |
| 11 | Exit lane width | **≥ 7.0 m** (3.0 + 2 × 2 m) | decided | Standard rule 1; test `ExitLaneMargin` 200 | Proposed lane 7.0 m centred on the door (§5) |
| 12 | Vehicle nav gap | **≥ 6.0 m** (agent r 190 cm → 3.8 m + ~2 m) | decided | Standard rule 4; agent radius 190, height 350 | Door 7.0 m clear ≥ 6.0 m; lane 7.0 m ≥ 6.0 m |
| 13 | Garage door clear width | **7.0 m** | proposal | Rules 1 and 4 | OK: meets both with the same 7 m |
| 14 | Garage door clear height | **≥ 4.5 m** (proposed 4.5 m) | proposal | 3.2 m vehicle; nav agent height 3.5 m | 1.3 m over the vehicle, 1.0 m over the agent height |
| 15 | Garage bay depth | **≥ 8.5 m** (proposed 8.5 m clear, door plane to back wall) | proposal | 6.5 m vehicle | 1.0 m at the back + 1.0 m behind the door leaf |
| 16 | Garage bay interior clear height | 5.0 m (roof module ~7.5 m) | proposal | Door + 0.5 m for the rolled door leaf | — |
| 17 | Garage lateral position | Door centre at **local Y = -600 cm** from the footprint centreline | proposal (offset decided to exist) | `GetExitLocation` has no lateral term | #192 adds the offset (§5) |
| 18 | Exit clearance | 200 cm beyond the footprint edge + the unit's longer half | code | `RTSBuildPlacement::ExitClearance = 200`; test lane checks | Unchanged |
| 19 | Exit lane length | 3 × half length + 200: **1175 cm** for the 6.5 m envelope, **869 cm** for today's builder mesh | code | `RTSBuildPlacement::GetExitLaneLength` | §5 |
| 20 | Gaps between modules meant for vehicles | none: only the garage is vehicle-accessible; the courtyard is pedestrian | proposal | Rule 4 | Courtyard has no nav requirement for vehicles |

Why the garage proposals hold (rows 13–16):

- **Width 7.0 m.** The dozer is 3.0 m wide; rule 1 asks 2 m either side for the lane and rule 4 asks ≥ 6 m for any vehicle gap.
  Making the door as wide as the lane means the hull never narrows its clearance on the way out, and the vehicle nav agent
  (3.8 m) passes the door with 1.6 m each side. The image's door reads about this wide against the garage block.
- **Height 4.5 m.** 3.2 m vehicle + 1.3 m for the raised blade, roof clutter and camera readability; it also clears the
  350 cm nav agent height by 1.0 m, so the navmesh can be generated through the doorway if #192 lets units drive out
  on nav (Q4).
- **Depth 8.5 m.** 6.5 m vehicle + 1.0 m to the back wall + 1.0 m between the blade and the door leaf when closed. The
  concept shows the dozer fully inside the bay with the door open; 8.5 m keeps that true for the envelope, not only for
  today's smaller mesh.

## 4. Module map (annotated reference)

Positions are local cm (§2), *proposals* read from the image; #186's blockout confirms them to the metre. Heights are roof
level unless "tip".

| ID | Module | Image location | Local position (centre) and extent | Heights | Notes |
|---|---|---|---|---|---|
| M1 | **Central command core** | Centre, octagonal block with the large eagle plaque and lit entrance | centre (-150, +150), ~10 × 10 m octagon | roof ~1400, upper drum ~1650, antenna cluster **tip 2400** | Eagle plaque over the entrance on the (+X, +Y) diagonal face; blue light strips either side of the door; rooftop AC units and equipment boxes ring the drum |
| M2 | **Left administration / communications wing** | Left, two linked blocks with eagle plates, a rooftop dish and antenna masts | +Y half, L-shaped, X -1150 … +700, Y +450 … +1250 | roof ~900, dish tower block ~1100, dish top ~1400, masts tip ~1700 | Panel antenna array on the rear block; windows with warm interior light; supply crates along the walls |
| M3 | **Right bulldozer garage** | Right, broad flat-roofed hall with open bay, dozer inside | X +400 … +1250 (door plane at X = +1250), Y -1100 … -100 | roof ~750; door 700 × 450 clear | Hazard-striped door posts, blue light strip over the opening, interior lights, rooftop solar panels and vents, blue accent panel on the side |
| M4 | **Central courtyard** | Front centre, between core, wing and garage | towards the (+X, +Y) corner, ~10 × 8 m, ground level, raised ~0.6 m with steps | — | Two planters with shrubs, steps up to the core entrance, central eagle plinth with flagpole; pedestrian only |
| M5 | **Flag (courtyard)** | Courtyard centre, on the eagle plinth | courtyard centre, ~(+650, +450) | pole tip ~900 | US-style flag, moving cloth (§6) |
| M6 | **Flag (core rear right)** | Behind the core towards the garage | ~(-500, -350), on the connector roof | pole tip ~1600 | Second flag in the image; kept |
| M7 | **Rooftop radar (phased array)** | Right tower behind the garage, flat array plate on top | ~(-100, -800), tower ~4 × 4 m | tower roof ~1100, array top ~1350 | Eagle plate on the tower face; array turns (§6) |
| M8 | **Satcom** | Lattice tower with a white radome, behind the radar tower | ~(-650, -1050), lattice ~3 × 3 m | radome top ~1800, masts tip ~2000 | Blue beacons on the lattice |
| M9 | **Secondary mast** | Thin mast with beacons, right of the satcom | ~(-200, -1250) | tip ~1700 | Warning beacons |
| M10 | Connector corridors | Glazed links core ↔ wing and core ↔ garage | between the modules | roof ~600 | Blue strip lights along the glazing |
| M11 | Apron and markings | Paved slab with grid lines and white edge markings | whole footprint, ±1400 | ground, ~5 cm | Exit lane markings in front of the door (§5) |
| M12 | Checkpoint posts | Two low barrier blocks with lamp masts, front of the apron | on the +X apron, clear of the exit lane | lamp tips ~400 | Blue beacons; jersey barriers |
| M13 | Fuel tanks | Twin horizontal tanks next to the right checkpoint | +X apron, +Y of the exit lane | ~250 | Must stay outside the 7 m lane |
| M14 | Supply stacks | Olive crates and containers around wing and garage | along walls | ≤ 250 | Static, merged into the root |
| M15 | Loader | Small olive loader beside the garage | front of M3, +Y of the door | ~250 | Static prop in the root, outside the lane; Q6 |
| M16 | Light masts | Floodlight masts at the wing corners and on the apron | corners | tips ~600–900 | Lamp heads emissive |

## 5. Garage, spawn point and exit path

### Builder bounds, measured

Read on 2026-10-20 from the imported assets, editor closed, with the read-only commandlet
`UnrealEditor-Cmd.exe WorldDivide.uproject -run=pythonscript -script=<read_bounds.py> -unattended -nosplash -nullrhi -nosound`.
The script loads each data asset and prints `Mesh.get_bounding_box()` and `mesh_scale`.

| Asset | Mesh | Mesh bounds (cm) | `MeshScale` | In the world (L × W × H, cm) |
|---|---|---|---|---|
| `DA_TestBuilder` | `SM_Builder` | 247.01 × 166.06 × 151.20 | 1.8066 uniform | **446.2 × 300.0 × 273.2** |
| Design envelope (standard §3) | — | — | — | **650 × 300 × 320** |
| `DA_TestHeadquarters` (placeholder) | `SM_Headquarters` | 136 × 136 × 288 | (19.5588, 19.5588, 7.6389) | 2660 × 2660 × 2200 |

The garage is sized to the **envelope**, which bounds today's mesh on every axis (it is 2.04 m shorter and 0.47 m lower;
the width binds at 300). The placeholder HQ is 26.6 m (28 m less the 5 % `building_fit` border) and 22 m high.

### Positions (proposals)

| Point | Local (X, Y, Z) cm | How |
|---|---|---|
| Door plane | X = +1250, Y -950 … -250, Z 0 … 450 | 1.5 m apron to the footprint edge |
| Bay | X +400 … +1250, Y -950 … -250 | 8.5 m deep, 7.0 m wide |
| **Interior spawn point** (socket `Spawn_Garage`) | **(+825, -600, 0)**, yaw 0 (facing +X) | Bay centre; the envelope's 6.5 m hull fits with 1.0 m front and rear |
| Footprint exit edge | X = +1400, Y = -600 | `GetFootprintExtent(...).X` |
| Spawn location today (`GetExitLocation`) | (1400 + halfLength + 200, **0**, 0): **(1925, 0, 0)** envelope, (1823, 0, 0) today's mesh | No lateral term today |
| Spawn location with #192's offset | **(1925, -600, 0)** | Lateral offset -600 cm |
| Lane end / rally (`GetExitLaneLength`) | X = 1400 + 1175 = **2575**, Y = -600 (envelope); 1400 + 869 = 2269 with today's mesh | Rally at the lane end (`RTSBuilding.cpp`) |

**Exit path.** The dozer appears at `Spawn_Garage`, drives straight +X through the open door (door clear 7.0 m), crosses
the 1.5 m apron, leaves the footprint at X = +1400, and continues along Y = -600 to the lane end. The lane is 7.0 m wide
(Y -950 … -250), the same width as the door, and nothing in the mesh or the props stands in it from X = +400 to the
footprint edge (M12, M13 and M15 sit at Y > -250 or Y < -950).

**Lateral exit offset for #192: -600 cm on local Y**, read from the image (the door centre sits about 6 m from the face
centre towards the right corner, ±1 m); #186's blockout confirms the value. #192 adds it to `GetExitLocation` (and the
placement check `CanUnitsLeave` and the lane test must use the same offset), rotated with the building's yaw.

## 6. Animated parts

All cosmetic and client-side; none changes gameplay state (server-authoritative rules unaffected).

| Part | Motion | Pivot (local cm, proposal) | How (proposal) | Driven by |
|---|---|---|---|---|
| Garage door | Rolls up: the leaf translates +Z 0 → 450 and rolls into a drum, ~1.5 s | hinge line X +1250, Z 470, Y -600 | Separate movable mesh | #196 state controller, timed with #192's spawn |
| Flag (×2) | Cloth wave | pole tops M5, M6 | World-position-offset material (no simulation) | Always on |
| Main radar array (M7) | Yaw, slow full turn (~6 s per rev) | tower top (-100, -800, 1100) | Separate movable mesh | #196 |
| Satcom radome (M8) | **Static** (a radome hides its dish) | — | In the root | — |
| Wing dish (M2) | Slow yaw sweep ±40° | dish base on the wing tower | Separate movable mesh | #196 |
| Rooftop fans | Spin | each fan hub | Rotator material (WPO), no component | Always on, off when unpowered (#196) |
| Warning beacons | Blink / rotate | beacon bodies | Emissive material parameter | #196 |
| Mast lights | Slow blink (aviation) | mast tips | Emissive material parameter | #196 |
| Garage interior lights | On while the door is open | bay ceiling | Emissive + at most one light (§9) | #196 |

## 7. Materials and team colour

| Category | Where | Team colour |
|---|---|---|
| Concrete | Walls of core, wing, garage, planters, steps, jersey barriers | No |
| Painted metal (light grey) | Panels, parapets, cladding, equipment boxes | No |
| Bare metal | Masts, lattice, railings, pipes, fuel tanks | No |
| Armoured steel (dark) | Door frames, garage door leaf, plating at the corners | No |
| Glass | Windows, glazed corridors (warm interior behind) | No |
| Roofing | Flat roofs, solar panels, vents | No |
| Rubber / grates | Floor grates, door seals, mats, garage floor | No |
| **USA-blue accent** | Blue panels on walls and towers | **Yes** |
| **Eagle plates** | Eagle plaques and emblems (core, wing, tower, plinth) | **Yes** |
| Emissive lamps | Blue light strips, beacons, floodlights, mast lights | **Never** (blue stays blue for every team) |
| Olive props | Crates, containers, loader (the dozer in the bay is the produced unit, not part of the HQ mesh) | No |

`ARTSBuilding::ApplyTeamColor` writes one custom primitive data vector on the building's mesh component; every material
reads it, and the strength per slot lives in the material instance (the `OWN_MODEL_TINT` pattern in `setup_testmap.py`).
So the rule above is enforced in the material instances: strength 1 on USA-blue accent and eagle plates, 0 everywhere else
(#189). Separate movable parts are their own components and do not get the vector today; #196 sets it on them too, or they
carry no team-colour slot.

## 8. UE5 hierarchy

One static root mesh plus separate movable parts, in the `m20_guardian.json` shape: a JSON next to the mesh lists the parts
and sockets by name with `[x, y, z]` offsets in cm in the local frame of §2 (+X = garage face). Proposed file
`Art/USA/buildings/usa_hq/usa_hq.json` (#191):

```json
{
  "root_mesh": "SM_USA_HQ",
  "reference_length": { "axis": "Z", "cm": 2400.0 },
  "parts": {
    "USA_HQ_GarageDoor": { "pivot": [1250.0, -600.0, 470.0], "motion": "translate_z", "travel_cm": 450.0 },
    "USA_HQ_Radar":      { "pivot": [-100.0, -800.0, 1100.0], "motion": "yaw" },
    "USA_HQ_WingDish":   { "pivot": [-700.0, 900.0, 1100.0], "motion": "yaw_sweep", "range_deg": 40.0 }
  },
  "sockets": {
    "Spawn_Garage": [825.0, -600.0, 0.0],
    "Exit_Door":    [1250.0, -600.0, 0.0],
    "Flag_Courtyard": [650.0, 450.0, 900.0],
    "Flag_Core":      [-500.0, -350.0, 1600.0],
    "Mast_Core_Tip":  [-150.0, 150.0, 2400.0]
  }
}
```

- The root `SM_USA_HQ` holds the core, wing, garage shell, courtyard, apron, satcom, all masts (including the 24 m core
  mast, so `GetMeshHeight` measures 2400), all static props and the flag poles.
- Parts are attached to sockets on the root mesh component (not to the actor root) so they follow its transform (§11).
- Pivots and sockets are proposals; #186 confirms positions, #191 writes the final JSON and checks the 2400 cm reference
  length on import (standard rule 5).

## 9. Budgets (proposals for the producer)

| Budget | Proposal |
|---|---|
| Triangles, LOD0 | ≤ 60k root + ≤ 10k for all movable parts |
| LOD1 / LOD2 / LOD3 | ≤ 25k / ≤ 8k / ≤ 2k (LOD3 a silhouette block-out, masts as cards) |
| Material slots | ≤ 9 on the root (one per §7 category), ≤ 2 per movable part |
| Texture sets | 4: structure trim sheet 2048², props atlas 2048², emblems/decals 1024², emissive mask 1024²; ORM packed |
| Dynamic lights | ≤ 2 (garage interior and door), none elsewhere; everything else emissive |
| Shadow-casting lights | 0 |
| Ticking components per HQ | ≤ 1 (the #196 state controller drives the door, radar and dish); flags, fans and lamps animate in materials |
| Collision | Simple boxes per module (≤ 16), not per-poly; the garage bay is open (#191) |

## 10. Mandatory details and forbidden elements

**Mandatory** (from the image): octagonal command core with the eagle plaque and lit entrance; tall antenna cluster on
the core; two-block administration wing with rooftop dish and panel antenna; broad garage with open bay, hazard posts,
blue strip light and rooftop solar panels; courtyard with steps, planters, eagle plinth and flag; second flag at the
core's rear right; phased-array radar tower with eagle plate; satcom radome on a lattice tower; secondary mast with
beacons; glazed connector corridors; paved apron with markings; checkpoint posts, jersey barriers, fuel tanks, olive
supply stacks; blue accent panels and blue lamps; worn concrete and grey painted metal.

**Forbidden**: tents and field shelters; sprawling wings or anything beyond the 28 × 28 m footprint; sci-fi forms (domes
beyond the radome, floating parts, curved alien shells, holograms); bright neon or saturated glow (lamps stay the
restrained blue of the image); anything taken from C&C Generals (names, emblems, shapes); real-world insignia beyond the
generic eagle-and-star emblem and the flag.

## 11. Constraints for downstream issues

| Issue | Constraint |
|---|---|
| #121 USA faction data | Name "USA Headquarters" (Q7), footprint (28, 28), mesh `SM_USA_HQ`, `MeshScale` (1, 1, 1), max_count 1, build time 12 s, produces the builder; the widest exit unit is the 6.5 × 3.0 m engineering vehicle |
| #186 Blockout | Confirm every §4 position and the -600 cm door offset to ±0.5 m; keep the 24 m tip and 14 m core roof; mass within ±1400 |
| #187 Structure | Static modules and props in one root mesh, modelled at size (rule 5) |
| #188 Garage | Door 7.0 × 4.5 m clear, bay 8.5 m deep, 5.0 m interior height, door plane X = +1250; door leaf a separate part with the §8 pivot; lane area free of props |
| #189 Materials | §7 categories, team-colour strength 1 only on blue accent and eagle plates, emissive never tinted; §9 texture budget |
| #190 Rooftop equipment | Radar and wing dish as separate parts with their pivots; satcom radome static; core mast in the root, tip at 2400 |
| #191 Import | `usa_hq.json` per §8; reference length 2400 cm on Z; no `building_fit`; `GetMeshHeight` must read 2400 ± 10 %; LODs and collision per §9 |
| #192 Garage production | Add the lateral offset **-600 cm on local Y** to `GetExitLocation`, `CanUnitsLeave` and the lane test; spawn inside at `Spawn_Garage` and drive out, or keep spawning at the exit (Q4); open the door before the unit appears |
| #196 State controller | One ticking component per HQ; sets team colour on the movable parts; drives door, radar, dish, beacons, lights |
| #191 / #196 construction | `ApplyConstructionVisual` scales the mesh component in Z with build progress. Parts attached to sockets on that component scale with it (squashed while building); parts on the actor root would float. Decide: attach to the mesh and accept the squash, or hide the parts until construction completes (Q5) |

## 12. Cosmetic parts and the server

Movable parts, lights and effects are client-side presentation. The server's building has only the gameplay state it has
today (footprint, construction, production); the door and lights follow replicated state (#196), they do not drive it.

## 13. What is not decided by this spec

Gameplay values (health, cost, build time beyond #7/#38), the dozer's own mesh, damage states, and destruction are out of
scope.

## 14. Producer decisions recorded

| Date | Decision |
|---|---|
| 2026-10-18 | The compact campus concept is the approved visual source |
| 2026-10-20 | Footprint 28 × 28 m; height 24 m to the tallest mast tip, whole mesh; core roof ~14 m |
| 2026-10-20 | Local +X = the garage face (lower-right face in the image); garage off-centre, so #192 adds a lateral exit offset |
| 2026-10-20 | Design envelope 6.5 × 3.0 × 3.2 m; measured `DA_TestBuilder` bounds recorded as evidence |
| 2026-10-20 | Team colour on blue accent panels and eagle plates only; lamps and every other slot none |
| 2026-10-20 | Door ≥ 4.5 m and bay ≥ 8.5 m are proposals; budgets are proposals |

## 15. Open questions

| # | Question | Routed to |
|---|---|---|
| Q1 | Is the paved slab with its edge markings the footprint edge (proposed), or a ground decal beyond the 28 m footprint? | Producer, then #186 |
| Q2 | Lateral offset -600 cm: approve the value, to be confirmed in the blockout | Producer, then #186 / #192 |
| Q3 | Garage door 7.0 × 4.5 m clear and bay 8.5 m deep: approve | Producer, then #188 |
| Q4 | Does the dozer spawn inside the bay at `Spawn_Garage` and drive out on the navmesh, or spawn at the exit as today with the door animation only? | #192 |
| Q5 | During construction: movable parts squash with the Z-scaled mesh, or stay hidden until complete? | #191 / #196 |
| Q6 | The loader beside the garage and the second flag: keep both as drawn (proposed)? | Producer, then #190 |
| Q7 | Display name of the building | #121 |
| Q8 | Budgets in §9: approve | Producer |

---

Approved by producer: ____ date ____
