# M20 Guardian — Unreal Engine 5 integration, phase 1

The v5 export (`fbx/`, `textures/`, `m20_guardian.json`) is integrated unchanged as the test unit `DA_TestM20Guardian`.
No mesh, UV or texture was edited. `blender -b --python Tools/Art/m20_guardian_blender.py -- --verify` passes on every
FBX before the import.

Everything is generated from code by `Tools/Scripts/ue.ps1 -Task setup`, like every other asset in the project. Nothing
was made by hand in the editor.

## What was built

| Piece | Where |
|---|---|
| Import of the 9 parts, 12 maps and the materials | `Tools/Editor/vehicle_art.py`, `make_m20_guardian()` in `Tools/Editor/setup_testmap.py` |
| Meshes, textures, material instances | `Content/Art/USA/Vehicles/M20Guardian/{Meshes,Textures,Materials}` |
| Master materials (any vehicle from this pipeline) | `Content/Materials/M_Vehicle`, `M_VehicleLamp` |
| Unit data | `Content/Data/Units/DA_TestM20Guardian` |
| Test placement | two M20s for team 0 in `L_TestMap`, beside the west base at (-5200, -2600); the two army smoke tests leave them out (`RTSTests::IsArmyUnit`) |
| HUD | a generated placeholder icon `Icon.M20Guardian` (marked as placeholder) and a description, as every unit needs |
| Gun, add-on parts, rolling wheels | `URTSUnitData` (`GunMesh`, `GunOffset`, `GunMuzzleOffset`, gun limits, `Parts`) and `ARTSUnit` |
| Debug commands | `rts.Debug.AimTurret <yaw> <pitch>` / `off` on the selection; `rts.Debug.UnitShots <UnitData> [StressCount]` |
| Screenshots and stats | `ue.ps1 -Task unitshots -Unit DA_TestM20Guardian -Stress 240`, output in `Saved/UnitShots/DA_TestM20Guardian/` |
| Tests | `WorldDivide.Units.VehicleParts.Attach`, `WorldDivide.Units.RunningGear.Spin`, `WorldDivide.Combat.Turret.GunElevation`, `WorldDivide.Art.M20Guardian` |

**How the unit fits the existing framework.** It uses the existing `ARTSUnit` pawn. Selection is the same capsule trace,
movement the same navmesh AI controller and orders the same command path. The hull is `Mesh` and the turret is `Turret`,
as before.

The additions are optional data fields. Units that leave them empty behave exactly as before, which the full test run
confirms. The additions are:
- A gun on the turret that elevates toward the target's height, between -8° and +20° at 30°/s. Shots leave from the gun's
  muzzle.
- `Parts` mounted on the hull, the turret or the gun. The RWS rides the turret and the tracks the hull.
- Hull wheels. The wheels of each mesh are drawn by one instanced component: 3 components for 16 wheels.
- The hull's ground contact now counts the hull-mounted parts. The M20 hull floats 36 cm above the ground and its tracks
  reach z = 0, so the tracks stand on the ground.

## Geometry

| Part | Triangles | Per tank |
|---|---:|---:|
| Hull (incl. suspension) | 28,268 | 28,268 |
| Turret | 19,168 | 19,168 |
| Gun | 4,500 | 4,500 |
| RWS | 2,540 | 2,540 |
| Track L / R | 6,120 | 12,240 |
| Road wheel (×12) | 1,456 | 17,472 |
| Sprocket (×2) | 1,208 | 2,416 |
| Idler (×2) | 1,396 | 2,792 |
| **Total** | | **89,396** |

Import results:
- Every mesh keeps its exported triangle count except the gun and the RWS. `WorldDivide.Art.M20Guardian` checks this
  against `export_triangles.json`. Unreal's mesh build drops degenerate (zero-area) triangles that the export contains:
  24 in `SM_M20_Gun` (4,476 of 4,500) and 20 in `SM_M20_RWS` (2,520 of 2,540). Nothing visible is lost. The art pipeline
  should clean them up on the next export.
- The scale is right: the hull is 710.2 cm long. The import checks this, so a units mix-up fails setup.
- The pivots are where the JSON puts them.
- Interchange mirrors Y the way the JSON expects: the exported left wheel's outboard face lands on −Y.

Mesh settings: Nanite off, no collision, imported normals and tangents kept, and LOD1 to LOD3 since #149 (see LODs).

**Components per tank: 9 meshes** (hull, turret, gun, RWS, 2 tracks, 3 instanced wheel sets) **and 10 material
sections**. The hull has two sections, its paint and its lamps. The v2 report expected 22 components; instancing the
wheels brings that down to 9.

## LODs (#149)

Every part has LOD1 to LOD3 after its LOD0, which is the import as before, triangle for triangle.

| Part | LOD0 | LOD1 | LOD2 | LOD3 | Per tank |
|---|---:|---:|---:|---:|---:|
| Hull | 28,268 | 9,984 | 2,596 | 560 | 1 |
| Turret | 19,168 | 6,998 | 1,600 | 380 | 1 |
| Gun | 4,476 | 1,592 | 348 | 80 | 1 |
| RWS | 2,520 | 698 | 120 | 64 | 1 |
| Track L / R | 6,120 | 2,400 | 680 (belt) | 144 (belt) | 2 |
| Road wheel | 1,456 | 300 | 120 | 64 | 12 |
| Sprocket | 1,208 | 300 | 120 | 64 | 2 |
| Idler | 1,396 | 300 | 120 | 64 | 2 |
| **Tank** | **89,352** | **28,872** | **7,944** | **2,396** | |

The hull's LOD2 and LOD3 lose the lamp section: the lamps reduce to nothing at that size, and the hull draws one section
there instead of two.

**How they are made.** `vehicle_art.build_lods` runs in setup after each part's import:
- The engine's quadric reduction makes each LOD from LOD0 at the part's triangle target (`make_m20_guardian` in
  `setup_testmap.py`). It keeps the imported normals and all UV channels, the tracks' animation channels included.
- The tracks' LOD2 and LOD3 are not reduced. Their 85 links are separate boxes, and a reduction can only shrink them into
  gaps. `blender -b --python Tools/Art/m20_guardian_blender.py -- --lods` writes a closed belt along the same centre line
  instead, to `fbx/lods/SM_M20_Track_{L,R}_LOD{2,3}.fbx`: a segment per link at LOD2, a segment per 30 degrees of bend at
  LOD3. Its faces take the UVs of a link's outer, inner and side faces. The script reads the saved .blend and writes nothing
  else; LOD0's FBX are never re-exported.
- A commandlet has no static mesh editor subsystem. `URTSEditorScripting::LoadEditorModule("StaticMeshEditor")` loads it.
- The LODs are part of each mesh's setup hash. A second setup writes nothing.

**Switching.** Every part changes LOD at the same distance from the camera: LOD1 at 35 m, LOD2 at 110 m and LOD3 at
260 m (`vehicle_art.LOD_DISTANCES`):
- Each part's screen size is the engine's own (`ComputeBoundsScreenSize`) for its own radius at that distance, so a small
  part has a small threshold.
- The radius is the bounds sphere. For the instanced wheels it is the box's half diagonal, which GPU LOD selection uses per
  wheel.
- The lens is the RTS camera's: 60 degrees across at 16:9 with the vertical field of view kept. 1080p, 1440p and
  ultrawide therefore pick the same LOD in the same view.

| Range | LOD | Tank length at 1080p | RTS camera |
|---|---|---|---|
| under 35 m | 0 | over about 340 px | close inspection |
| 35–110 m | 1 | 340 to 107 px | zoomed in, up to about a 100 m arm |
| 110–260 m | 2 | 107 to 45 px | the default zoom (tanks 128 to 204 m away) |
| over 260 m | 3 | under 45 px | zoomed out to the farthest |

The distance is measured from each part's own middle, so parts up to 3.5 m from the hull's middle switch up to that much
earlier or later:
- at 35 m that is about ±10 %: a few wheels and the gun switch in the next zoom step;
- at 110 m and 260 m all 22 pieces switch within ±4 % of each other.

`rts.Debug.UnitShots ... lods` logs the LOD of every piece per shot. There is no hysteresis and no dithered transition
(the engine has none for static meshes). In the captures, the switches cannot be told apart from the 4 % change in size
around them.

**Materials and team colour.** The reduced LODs draw with the same `MI_M20_*` instances as LOD0. The team colour (CPD 4
to 7) and the track phase (CPD 0 and 1) belong to the component, so every LOD has them, and there are no per-unit
instances: the bench counts 0 dynamic material slots and 6 materials at 240 tanks. The tracks' extra slot
`M20_Track_Static` (`MI_M20_Track_Static`) is the only new one.

**Unchanged:** LOD0, the bounds (the hull is still 710.2 cm long), the collision box, the footprint, the navigation, the
turret, gun and RWS articulation and the wheels' roll. LODs only change what is drawn.

**Captures:** `ue.ps1 -Task unitshots -Unit DA_TestM20Guardian -LODs` writes to
`Saved/UnitShots/DA_TestM20Guardian/LODs`:
- every LOD forced from the side, front, rear and front three-quarter;
- the RTS camera inside each LOD's range (close, tactical, default and farthest zoom);
- the RTS lens 4 % inside and outside each switch;
- the four team colours at each LOD.

**For the next vehicles:**
- Set budgets per tank, not per part.
- Reduce the large shells (hull, turret) least by share and the small repeated parts (wheels) most.
- Never reduce a mesh made of many separate small pieces (tracks, links, chains) below about half. Give it a hand-built
  proxy instead.
- Keep the switch distances in `vehicle_art.LOD_DISTANCES` the same for every vehicle, so mixed armies switch alike.
- The engine's reduction stops at about 64 triangles for a wheel, so very small parts cannot go lower without a proxy.

## Materials

`M_Vehicle` is opaque and default lit, with three texture samples and no other cost:
- BaseColor = lerp(BC, luminance(BC) × team `Color` × 2, BC.a × `TeamStrength`). The panels under the BC alpha mask take
  the team colour that `ARTSUnit` already sets on every slot.
- Normal = N.
- AO, roughness and metallic = ORM.r, .g and .b.

`M_VehicleLamp` is emissive from the vertex colour.

There is one instance per texture set: `MI_M20_Hull`, `MI_M20_Turret` (turret, gun and RWS), `MI_M20_Running` and
`MI_M20_Track`.

Texture settings:

| Map | sRGB | Compression | Notes |
|---|---|---|---|
| BC | on | Default (BC3) | alpha kept |
| N | off | Normalmap (BC5) | no green flip; the maps are DirectX |
| ORM | off | Masks | |

The game set is 2K, with the track at 1K: **43.3 MB** with mips, before streaming.

**Departure from the brief.** The brief asked for separate instances for painted armour, blue accents, tracks and rubber,
mechanical metal, and optics. The v5 pipeline bakes all of these looks into each set's BC and ORM, so they cannot be split
without cutting the meshes into more sections. That would mean more draws for every tank. The looks carry through the
four set instances as baked. The close-ups show tan paint, cobalt team panels, dark rubber and track steel, gunmetal
fittings and dark coated optics, with nothing grading over them.

## Behaviour

- **Turret and gun.** The turret traverses toward the weapon's target at 45°/s, and the gun elevates toward the target's
  height. `rts.Debug.AimTurret` holds both for inspection.
  - The RWS turns with the turret.
  - The gun's mantlet and barrel move as one mesh.
  - Gun pitch is replicated, like turret yaw.
- **Wheels.** Each frame the unit takes how far it drove along its heading and how far it turned. Each wheel rolls by the
  ground speed on its own side: driving forward rolls the tops forward, reversing rolls them back, and a pivot turn rolls
  the two sides in opposite directions. **A unit that stands still costs one transform compare and nothing else.**
  Nothing new ticks: this runs in the unit's existing tick.
- **Track texture scrolling is not done.** The track is built from separate link meshes that share one link's atlas UVs.
  Its U axis does not run along the track, so panning U would slide the atlas islands across fixed links. Scrolling needs
  one of these from the art side:
  - a track built as a continuous strip with U along its length; or
  - a link-offset World Position Offset along the track path.

  Both are deferred, and the tracks stay rigid. The rolling wheels carry the motion at RTS distance.
- **Selection and movement** are verified in the game by the unit shots run:
  - The unit is picked by the cursor's own trace (`GetUnitAlongRay`), selected (`SelectUnits`), and ordered to move
    through `Server_IssueCommand`, the same path as a right click.
  - It drove 12.2 m in 2.5 s at a top speed of 5 m/s, and its wheels rolled.
  - The `driving` shot shows the selection brackets, the health bar and the selection panel on the HUD.

## Performance (measured)

Measured with `ue.ps1 -Task unitshots -Stress 240` on the development machine (RTX 4090):
- 1920×1080 window, offscreen, Development editor build in `-game`.
- The engine's default dynamic resolution (73% render resolution under load).
- Averaged over 120 frames.

The stress grid is spaced by the unit's selection radius, so the avoidance does not keep pushing the units.

| Scene | Frame (game thread) | GPU | Draw calls | Triangles drawn |
|---|---:|---:|---:|---:|
| L_TestMap, default RTS view | 2.9 ms | 1.6 ms | 313 | 0.86 M |
| + 240 M20 Guardians | 11.5 ms | 4.0 ms | 2,556 | 51.2 M |
| + 240 placeholder tanks (`DA_TestTank`, same grid) | 10.8 ms | 2.7 ms | 952 | 2.9 M |

The stress view is the RTS camera at its farthest zoom (70 m) over the grid. The 24 × 10 grid of real-size tanks is
larger than the view, so the counts cover what is in view and in the shadows' range, not all 240 tanks.

What the measurements show:
- **The game thread is the limit, not the M20.** 240 units cost about the same 10.8–11.5 ms with either model. That cost
  is the unit framework: ticks, AI controllers and avoidance.
- **The M20 costs about 1.4 ms more GPU than the placeholder, with 2.7× the draw calls and 18× the triangles.** At about
  51 M triangles over every pass (depth, base and shadow), the 89k-triangle LOD0 drawn in each pass is what costs. LODs
  are the first fix.
- **Draw calls don't merge across tanks.** `ARTSUnit` gives every unit its own dynamic material instance per slot for the
  team colour, which keeps identical meshes from being instanced together across units. Sharing one instance per team,
  or passing the team colour as custom primitive data, would let the renderer merge them. This applies to every unit,
  not only the M20.
- **Avoidance with large units.** A first stress run with the grid at 5 m spacing overlapped the 4 m-radius M20s, and the
  avoidance pushing them cost 24 ms per frame. Real-size vehicles in tight groups are a game-thread risk worth profiling
  separately.

No target is claimed. These numbers come from one machine, one scene, no combat, and an editor build.

## Verification

- **Build:** `ue.ps1 -Task build` succeeds.
- **Setup:** `ue.ps1 -Task setup -Render` writes the 29 new or changed assets. A second run writes nothing.
- **Tests:** `ue.ps1 -Task test` passes all 177 `WorldDivide` tests, the four new ones included.
  - `Smoke.TestMap` and `Smoke.ListenServer` count and move the armies L_TestMap gives each team, 18 units each. They
    now leave the two inspection M20s out through `RTSTests::IsArmyUnit`. Without that, the 8 m tanks fail those tests'
    "all within 8 m of the target" check.
- **Screenshots** (`Saved/UnitShots/DA_TestM20Guardian/`):

  | Shot | View |
  |---|---|
  | `rts_default` | the player's RTS camera at its default zoom (30 m arm, −55°) |
  | `zoom_in_max` | the closest zoom (1.75 m arm, −8°) |
  | `zoom_out_max` | the farthest zoom (70 m) |
  | `closeup` | an inspection camera at about 12 m with a 45° lens |
  | `turret_rotated` | the turret at 60° and the gun at +10° |
  | `driving` | the unit selected and on a move order, with the HUD |
  | `stats_one`, `stress_240` | the stats views above |

- **Checked on the pictures:**
  - All parts sit in place: wheels inside the tracks, the turret on its ring, the gun in the mantlet, the RWS on the
    cupola.
  - Nothing floats or detaches when the turret turns.
  - The tracks stand on the ground.
  - Shading is normal: no inverted normals, the lamps glow, and the team panels take team blue.
  - At the default RTS distance the tank reads clearly. Its blue panels and silhouette separate it from the ground.

## Remaining issues

1. **Scale.** At real size the M20 is about 2.5× the other test units (`DA_TestAbrams` fits 2.9 m). Its selection radius
   is 4 m, and capsules of that size cross the 2 m gaps between L_TestMap's obstacles poorly. The game's unit scale has to
   be decided before more real-size art comes in.
2. **Collision proxies** are not made: the unit collides with its box. The LODs came with #149 (see LODs).
3. **The track does not scroll** (see Behaviour).
4. **No suspension travel and no track sag.** The road wheels stay at their exported height.
5. **The RWS does not traverse on its own** and has no weapon. It is a fixed part on the turret.
6. **Team-colour instances block draw merging** across units (see Performance).
7. **The L_TestMap ground ends at ±100 m.** The farthest zoom near the edge shows the void beyond it, which is a map
   matter, not the tank.
8. **Placeholder HUD icon.** The M20 carries the generated placeholder icon until painted art comes from the UI designer.
9. **Selection brackets sit high.** The HUD draws the brackets around the actor's middle. For a unit whose capsule is 8 m
   tall, because the capsule height follows the selection radius, they float above the hull. The capsule's half height
   should follow the mesh rather than the radius for flat, wide units.
10. **Degenerate triangles in the export** (gun 24, RWS 20; see Geometry).
