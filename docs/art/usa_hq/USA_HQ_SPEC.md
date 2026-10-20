# USA Default HQ: visual reference and technical spec

Status: **approved** by the producer at gate 2 (2026-10-20, #185). This is the single source for the USA HQ's look, size, modules and
UE5 setup; every downstream issue (#121, #186 to #192, #196) builds to it. A deviation from the image or from a number marked
*decided* needs a recorded producer decision in §14.

Values marked *decided* are producer decisions (2026-10-18 and 2026-10-20, §14) or come from the
[world scale standard](../../tech/scale/WORLD_SCALE_STANDARD.md). Values marked *proposal* are working positions that the
issue named next to them confirms; changing one beyond its stated tolerance needs a producer decision.

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
| 7 | `MeshScale` | **(1, 1, 1)**: the final mesh is modelled at size; no `building_fit` stretch | decided (rule 5) | Standard rule 5; `setup_testmap.py` `building_fit` stretches placeholders only | #191 must not run the fit on the final mesh |
| 8 | Built mass inside footprint | Mesh plus apron props within ±1400 cm on X and Y; **the paved slab edge is the 28 m footprint edge** | decided | Test "mesh fills its footprint" (`RTSScaleTests.cpp:314`): mesh X and Y ≤ footprint + 1 cm, and the mesh spans **≥ 0.9 of the footprint on at least one axis (≥ 2520 cm)** | OK: the slab spans 2800 cm on both axes |
| 9 | Design vehicle | Engineering vehicle **6.5 × 3.0 × 3.2 m** (L × W × H) | decided | Standard §3 engineering vehicle | Envelope for every garage number |
| 10 | Current builder mesh (measured) | 446.2 × 300.0 × 273.2 cm (see §5) | measured | Test `UnitStandards` builder width 300 ± 10 % | Smaller than the envelope on L and H; same W |
| 11 | Exit lane width | **≥ 7.0 m** (3.0 + 2 × 2 m) | decided | Standard rule 1; test `ExitLaneMargin` 200 | Proposed lane 7.0 m centred on the door (§5) |
| 12 | Vehicle nav gap | **≥ 6.0 m** (agent r 190 cm → 3.8 m + ~2 m) | decided | Standard rule 4; agent radius 190, height 350 | Door 7.0 m clear ≥ 6.0 m; lane 7.0 m ≥ 6.0 m |
| 13 | Garage door clear width | **7.0 m** | decided | Rules 1 and 4 | OK: meets both with the same 7 m |
| 14 | Garage door clear height | **4.5 m** | decided | 3.2 m vehicle; nav agent height 3.5 m | 1.3 m over the vehicle, 1.0 m over the agent height |
| 15 | Garage bay depth | **8.5 m** clear, door plane to back wall | decided | 6.5 m vehicle | 1.0 m at the back + 1.0 m behind the door leaf |
| 16 | Garage bay interior clear height | 5.0 m (roof module ~7.5 m) | decided | Door + 0.5 m for the rolled door leaf | — |
| 17 | Garage lateral position | Door centre at **local Y = -600 cm** from the footprint centreline | decided (#186 confirms to ±1 m) | `GetExitLocation` has no lateral term | #192 adds the offset (§5) |
| 18 | Exit clearance | 200 cm beyond the footprint edge + the unit's longer half | code | `RTSBuildPlacement::ExitClearance = 200`; test lane checks | Unchanged |
| 19 | Exit lane length | 3 × half length + 200: **1175 cm** for the 6.5 m envelope, **869 cm** for today's builder mesh | code | `RTSBuildPlacement::GetExitLaneLength` | §5 |
| 20 | Gaps between modules meant for vehicles | none: only the garage is vehicle-accessible; the courtyard is pedestrian | proposal | Rule 4 | Courtyard has no nav requirement for vehicles |

Why the garage values hold (rows 13–16, approved 2026-10-20):

- **Width 7.0 m.** The dozer is 3.0 m wide; rule 1 asks 2 m either side for the lane and rule 4 asks ≥ 6 m for any vehicle gap.
  Making the door as wide as the lane means the hull never narrows its clearance on the way out, and the vehicle nav agent
  (3.8 m) passes the door with 1.6 m each side. The image's door reads about this wide against the garage block.
- **Height 4.5 m.** 3.2 m vehicle + 1.3 m for the raised blade, roof clutter and camera readability; it also clears the
  350 cm nav agent height by 1.0 m. No navmesh is generated inside the bay: the building's collision covers the whole
  footprint, so the dozer leaves the bay on a scripted drive-out path (§5), not on navigation.
- **Depth 8.5 m.** 6.5 m vehicle + 1.0 m to the back wall + 1.0 m between the blade and the door leaf when closed. The
  concept shows the dozer fully inside the bay with the door open; 8.5 m keeps that true for the envelope, not only for
  today's smaller mesh.

## 4. Module map (annotated reference)

Positions are local cm (§2). The image proposals were confirmed or moved by #186 (round 6, 2026-10-20), fitting the
concept's apron margins: about 4 m of open slab behind and beside the buildings and an open forecourt towards the (+X, +Y)
corner. The values below are the confirmed ones, as built by `Tools/Art/usa_hq_blender.py`. Heights are roof level unless
"tip".

| ID | Module | Image location | Local position (centre) and extent | Heights | Notes |
|---|---|---|---|---|---|
| M1 | **Central command core** | Centre, octagonal block with the large eagle plaque and lit entrance | centre (-350, +50), 12 × 12 m octagon (apothem 600; its (+X, -Y) face clears the garage's rear corner); drum apothem 390 | roof 1400 (two storeys: a double-height lobby and one office floor), drum 1650, antenna cluster **tip 2400** | Eagle plaque over the entrance on the (+X, +Y) diagonal face; blue light strips either side of the door; rooftop AC units and equipment boxes ring the drum |
| M2 | **Left administration / communications wing** | Left, two linked blocks with eagle plates, a rooftop dish and antenna masts | along +Y round the core: rear block X -1000 … -550, Y +700 … +1050 (dish tower X -905 … -645, Y +770 … +980); link X -550 … +80, Y +750 … +1050; front block X +80 … +500, Y +640 … +1050 | roof 900 (link 700), dish tower 1100, dish top ~1450, masts tip 1700 | Panel antenna array on the rear block; windows with warm interior light; supply crates along the walls |
| M3 | **Right bulldozer garage** | Right, broad flat-roofed hall with open bay, dozer inside | X +360 … +1250 (door plane at X = +1250), Y -1100 … -100; bay X +400 … +1250 | roof 750 with a 100 cm 45° eave; door 700 × 450 clear | Hazard-striped door posts, blue light strip over the opening, interior lights, rooftop solar panels and vents, blue accent panel on the side |
| M4 | **Central courtyard** | Front centre, between core, wing and garage | forecourt from the core's +X flat and entrance face to X +1100, Y +100 … +900, notched round the wing's front block; raised 60 cm, steps X +1100 … +1220, Y +350 … +650 | — | Two planters with shrubs, steps up to the core entrance, central eagle plinth with flagpole; pedestrian only |
| M5 | **Flag (courtyard)** | Courtyard centre, on the eagle plinth | (+700, +500) | pole tip 900 | US-style flag, moving cloth (§6) |
| M6 | **Flag (core rear right)** | Behind the core towards the garage | (-831, -431), on the rear annex off the core's (-X, -Y) face (160 cm deep, roof 600) | pole tip 1600 | Second flag in the image; kept |
| M7 | **Rooftop radar (phased array)** | Right tower behind the garage, flat array plate on top | (-100, -800), tower 4 × 4 m (unchanged; 50 cm from the core) | tower roof 1100, array top 1350 | Eagle plate on the tower face; array turns (§6) |
| M8 | **Satcom** | Lattice tower with a white radome, behind the radar tower | (-650, -850), lattice 3 × 3 m (200 cm inboard) | radome top 1800, masts tip 2000 | Blue beacons on the lattice |
| M9 | **Secondary mast** | Thin mast with beacons, right of the satcom | (-350, -1100), three guys (inboard from (-200, -1250)) | tip 1700 | Warning beacons |
| M10 | Connector corridors | Glazed links core ↔ wing and core ↔ garage | core +Y flat ↔ wing link: X -500 … -110, Y +650 … +750; core (+X, -Y) face ↔ garage rear wall: Y -540 … -200 | roof 600 | Blue strip lights along the glazing |
| M11 | Apron and markings | Paved slab with grid lines and white edge markings | whole footprint, ±1400 | ground, ~5 cm | Exit lane markings in front of the door (§5) |
| M12 | Checkpoint posts | Two low barrier blocks with lamp masts, front of the apron | on the +X apron, clear of the exit lane | lamp tips ~400 | Blue beacons; jersey barriers |
| M13 | Fuel tanks | Twin horizontal tanks next to the right checkpoint | +X apron, +Y of the exit lane | ~250 | Must stay outside the 7 m lane |
| M14 | Supply stacks | Olive crates and containers around wing and garage | along walls | ≤ 250 | Static, merged into the root |
| M15 | Loader | Small olive loader beside the garage | front of M3, +Y of the door | ~250 | Static prop in the root, outside the lane; kept as drawn (§14) |
| M16 | Light masts | Floodlight masts at the wing corners and on the apron | corners | tips ~600–900 | Lamp heads emissive |

## 5. Garage, spawn point and exit path

### Builder bounds, measured

Read on 2026-10-20 from the imported assets with the editor pythonscript commandlet, editor closed (read-only, saves
nothing). To reproduce, save the script below outside the repo and run:

```
"D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<repo>\WorldDivide.uproject" -run=pythonscript "-script=<abs path>\read_bounds.py" -DisablePlugins=ModelContextProtocol,EditorToolset,SlateInspectorToolset,WorldDivideMCP -unattended -nosplash -nullrhi -nosound -stdout -FullStdOutLogOutput
```

```python
import unreal
for path in ["/Game/Data/Units/DA_TestBuilder", "/Game/Data/Buildings/DA_TestHeadquarters"]:
    da = unreal.load_asset(path)
    mesh = da.get_editor_property("mesh")
    sc = da.get_editor_property("mesh_scale")
    bb = mesh.get_bounding_box()
    size = bb.max - bb.min
    unreal.log_warning("HQBOUNDS %s mesh=%s size=(%.2f,%.2f,%.2f) scale=(%.4f,%.4f,%.4f) scaled=(%.1f,%.1f,%.1f)" % (
        path, mesh.get_path_name(), size.x, size.y, size.z, sc.x, sc.y, sc.z,
        size.x * sc.x, size.y * sc.y, size.z * sc.z))
```

The results are the log lines starting with `HQBOUNDS`.

| Asset | Mesh | Mesh bounds (cm) | `MeshScale` | In the world (L × W × H, cm) |
|---|---|---|---|---|
| `DA_TestBuilder` | `SM_Builder` | 247.01 × 166.06 × 151.20 | 1.8066 uniform | **446.2 × 300.0 × 273.2** |
| Design envelope (standard §3) | — | — | — | **650 × 300 × 320** |
| `DA_TestHeadquarters` (placeholder) | `SM_Headquarters` | 136 × 136 × 288 | (19.5588, 19.5588, 7.6389) | 2660 × 2660 × 2200 |

The garage is sized to the **envelope**, which bounds today's mesh on every axis (it is 2.04 m shorter and 0.47 m lower;
the width binds at 300). The placeholder HQ is 26.6 m (28 m less the 5 % `building_fit` border) and 22 m high.

### Positions (decided; #186 confirms to ±1 m)

| Point | Local (X, Y, Z) cm | How |
|---|---|---|
| Door plane | X = +1250, Y -950 … -250, Z 0 … 450 | 1.5 m apron to the footprint edge |
| Bay | X +400 … +1250, Y -950 … -250 | 8.5 m deep, 7.0 m wide |
| **Interior spawn point** (socket `Spawn_Garage`) | **(+825, -600, 0)**, yaw 0 (facing +X) | Bay centre; the envelope's 6.5 m hull fits with 1.0 m front and rear |
| Footprint exit edge | X = +1400, Y = -600 | `GetFootprintExtent(...).X` |
| Spawn location today (`GetExitLocation`) | (1400 + halfLength + 200, **0**, 0): **(1925, 0, 0)** envelope, (1823, 0, 0) today's mesh | No lateral term today |
| Spawn location with #192's offset | **(1925, -600, 0)** | Lateral offset -600 cm |
| Lane end / rally (`GetExitLaneLength`) | X = 1400 + 1175 = **2575** (envelope); 1400 + 869 = 2269 with today's mesh. **Y = 0 today**; Y = -600 only once #192 adds the offset | Rally at the lane end (`RTSBuilding.cpp`) |

**Exit path (decided).** The door opens first; then the dozer appears at `Spawn_Garage` and follows a **scripted
drive-out path** (not navigation: there is no navmesh inside the bay, since the building's collision covers the whole
footprint) straight +X through the door (7.0 m clear), across the 1.5 m apron, out of the footprint at X = +1400 and
along Y = -600 to the lane end, where it returns to normal navigation. The lane is 7.0 m wide
(Y -950 … -250), the same width as the door, and nothing in the mesh or the props stands in it from X = +400 to the
footprint edge (M12, M13 and M15 sit at Y > -250 or Y < -950).

**Lateral exit offset for #192: -600 cm on local Y**, read from the image (the door centre sits about 6 m from the face
centre towards the right corner, ±1 m); #186's blockout confirms the value. #192 adds it to `GetExitLocation` (and the
placement check `CanUnitsLeave` and the lane test must use the same offset), rotated with the building's yaw. Two code
facts #192 must handle:

- `CanUnitsLeave` returns `true` early when the widest exit unit uses the narrowest nav agent, so it checks nothing for
  such units; the offset check must not rely on it alone.
- `GetExitLocation` snaps the exit point's XY to the navmesh (`ProjectPoint`, extent 200 × 200 cm), which can pull an
  offset point sideways towards the nearest nav polygon; the offset lane end must be checked after the snap.

## 6. Animated parts

All cosmetic and client-side; none changes gameplay state (server-authoritative rules unaffected).

| Part | Motion | Pivot (local cm, proposal) | How (proposal) | Driven by |
|---|---|---|---|---|
| Garage door | Rolls up: the leaf translates +Z 0 → 450 and rolls into a drum, ~1.5 s | hinge line X +1250, Z 470, Y -600 | Separate movable mesh | #196 state controller; opens before #192 spawns the dozer |
| Flag (×2) | Cloth wave | pole tops M5, M6 | World-position-offset material (no simulation) | Always on |
| Main radar array (M7) | Yaw, slow full turn (~6 s per rev) | tower top (-100, -800, 1100) | Separate movable mesh | #196 |
| Satcom radome (M8) | **Static** (a radome hides its dish) | — | In the root | — |
| Wing dish (M2) | Slow yaw sweep ±40° | dish base on the wing tower (-775, 875, 1100) | Separate movable mesh | #196 |
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

One static root mesh plus separate movable parts. A JSON next to the mesh lists the parts and sockets by name with
`[x, y, z]` offsets in cm in the local frame of §2 (+X = garage face). This is a **new layout** (`root_mesh`, `parts`,
`sockets`) modelled on `m20_guardian.json`, not identical to it: the M20 file holds flat named offsets (`turret_offset`,
`wheels`, ...) read by the M20 importer, so #191 needs a reader for this layout. Proposed file
`Art/USA/buildings/usa_hq/usa_hq.json` (#191):

```json
{
  "root_mesh": "SM_USA_HQ",
  "reference_length": { "axis": "Z", "cm": 2400.0 },
  "parts": {
    "USA_HQ_GarageDoor": { "pivot": [1250.0, -600.0, 470.0], "motion": "translate_z", "travel_cm": 450.0 },
    "USA_HQ_Radar":      { "pivot": [-100.0, -800.0, 1100.0], "motion": "yaw" },
    "USA_HQ_WingDish":   { "pivot": [-775.0, 875.0, 1100.0], "motion": "yaw_sweep", "range_deg": 40.0 }
  },
  "sockets": {
    "Spawn_Garage": [825.0, -600.0, 0.0],
    "Exit_Door":    [1250.0, -600.0, 0.0],
    "Flag_Courtyard": [700.0, 500.0, 900.0],
    "Flag_Core":      [-831.0, -431.0, 1600.0],
    "Mast_Core_Tip":  [-350.0, 50.0, 2400.0]
  }
}
```

- The root `SM_USA_HQ` holds the core, wing, garage shell, courtyard, apron, satcom, all masts (including the 24 m core
  mast, so `GetMeshHeight` measures 2400), all static props and the flag poles.
- Parts are attached to sockets on the root mesh component (not to the actor root) so they follow its transform (§11).
- Pivots and sockets are proposals; #186 confirms positions, #191 writes the final JSON and checks the 2400 cm reference
  length on import (standard rule 5).

## 9. Budgets (approved 2026-10-20)

| Budget | Limit |
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
| #121 USA faction data | Display name **"Headquarters"**, footprint (28, 28), mesh `SM_USA_HQ`, `MeshScale` (1, 1, 1), max_count 1, build time 12 s, produces the builder; the widest exit unit is the 6.5 × 3.0 m engineering vehicle |
| #186 Blockout | Confirm every §4 position and the -600 cm door offset to ±0.5 m; keep the 24 m tip and 14 m core roof; mass within ±1400 |
| #187 Structure | Static modules and props in one root mesh, modelled at size (rule 5) |
| #188 Garage | Door 7.0 × 4.5 m clear, bay 8.5 m deep, 5.0 m interior height, door plane X = +1250; door leaf a separate part with the §8 pivot; lane area free of props |
| #189 Materials | §7 categories, team-colour strength 1 only on blue accent and eagle plates, emissive never tinted; §9 texture budget |
| #190 Rooftop equipment | Radar and wing dish as separate parts with their pivots; satcom radome static; core mast in the root, tip at 2400 |
| #191 Import | `usa_hq.json` per §8; reference length 2400 cm on Z; no `building_fit`; `GetMeshHeight` must read 2400 ± 10 %; LODs and collision per §9 |
| #192 Garage production | Add the lateral offset **-600 cm on local Y** to `GetExitLocation`, `CanUnitsLeave` and the lane test; open the door first, spawn inside at `Spawn_Garage` and drive the scripted path to the lane end (no navmesh in the bay); `CanUnitsLeave` returns early for the narrowest nav agent and `GetExitLocation` snaps XY to the navmesh (§5) |
| #196 State controller | One ticking component per HQ; sets team colour on the movable parts; drives door, radar, dish, beacons, lights |
| #191 / #196 construction | `ApplyConstructionVisual` scales the mesh component in Z with build progress. **Decided:** the movable parts (door, radar, flag, satcom) scale with the mesh during construction, so they attach to sockets on the mesh component, not the actor root (where they would float) |

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
| 2026-10-20 | The dozer in the bay in the image is the produced unit, not part of the HQ mesh |
| 2026-10-20 | Q1: the paved slab edge is the 28 m footprint edge |
| 2026-10-20 | Q2: lateral exit offset -600 cm on local Y; #186's blockout confirms it to ±1 m |
| 2026-10-20 | Q3: garage door 7.0 × 4.5 m clear, bay 8.5 m deep, 5.0 m interior height |
| 2026-10-20 | Q4: the dozer spawns inside the bay at `Spawn_Garage` and leaves by a scripted drive-out path to the lane end, the door opening first (#192); no navmesh inside the bay |
| 2026-10-20 | Q5: movable parts (door, radar, flag, satcom) scale with the mesh during construction (#191 / #196) |
| 2026-10-20 | Q6: keep the second flag and the loader as drawn |
| 2026-10-20 | Q7: display name "Headquarters" (#121) |
| 2026-10-20 | Q8: the budgets in §9 |
| 2026-10-20 | #186 round 6: §4 positions confirmed or moved to the concept's apron margins (wing pulled in, core 12 m across the flats with two storeys at the decided ~14 m roof, forecourt, satcom and mast inboard, 4 m connectors); door, bay, lane, 24 m tip and footprint unchanged; wing dish pivot (-775, 875, 1100) |

## 15. Open questions

None. Q1 to Q8 were answered at gate 2 on 2026-10-20 and are recorded in §14.

---

Approved by producer (TemplarGeneral) on 2026-10-20 at gate 2; decisions in §14.
