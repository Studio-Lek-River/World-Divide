# Shared building kit

The specification of the shared building kit (milestone `M7.00 USA Shared Building Kit`, #386): reusable lamps,
beacons, signs, roof equipment, antennas and railings that every building mounts from data, instead of modelling them
into each shell. Read it before you model a kit piece, add one to the catalogue, or mount one on a building. The budgets
for whole buildings stay in [`MODEL_PERFORMANCE.md`](MODEL_PERFORMANCE.md); this document adds the per-piece ones.

The first kit is the **USA Lighting Kit**, and its first job is to finish lighting the USA HQ
([`usa_hq/USA_HQ_SPEC.md`](usa_hq/USA_HQ_SPEC.md), Night lighting).

## What it extends

The kit is not a parallel architecture. A mounted piece becomes what a building already has:

| Existing system | Where | What the kit uses it for |
|---|---|---|
| `FRTSBuildingPart` and the ISM merge | `ARTSBuilding::ApplyParts` | Every mounted piece is a part; identical meshes batch into one instanced component |
| `FRTSBuildingSocket` | `URTSBuildingData::Sockets` | Mount points |
| `FRTSBuildingLight` and its `Fixture` | `URTSBuildingData::Lights`, `ARTSBuilding::CreateLight` | A piece's real light, non-shadow-casting, no GI, a component on the building actor |
| `URTSBuildingLightSubsystem` | one tick for all buildings | Real lights only in the dark, only on the nearest `rts.Fx.BuildingLightMaxBuildings` buildings; Flash and Rotate run here |
| `URTSBuildingMotionSubsystem` | one tick for all buildings | Moving pieces (fans, later kits) |
| `URTSBuildingStateComponent` (#196) | every building | The state that drives the light table below |
| `URTSDayNightSubsystem::IsDark()` | | The only source of day and night |

No kit piece adds an actor or a ticking component.

## One source of truth

- **`Art/Shared/kit/kit_catalogue.json` is the only hand-edited kit file.** It holds every piece: what it is, its light,
  its budgets.
- `Tools/Art/kit_blender.py` reads the catalogue, builds each piece with its module `Tools/Art/kit/<piece>.py`, and
  writes the FBX, the `_LOD1.fbx` and the **derived** manifest `Art/Shared/kit/build/kit_manifest.json`: triangles per
  LOD, bounds, material slots and the measured bounds of the `Lens` island. Nobody edits the manifest. Run it with
  Blender (`blender -b --python Tools/Art/kit_blender.py`) after a catalogue or module change, and commit its output.
- A module is the piece's name in snake case (`FlagCloth` → `flag_cloth.py`). Its `build(piece)` gets the catalogue
  entry and returns the LOD0 object and the LOD1 object, or `None` for a piece that keeps LOD0 alone (class S). Its slots
  are its Blender materials' names, in the catalogue's order; the lit glass is the material `Lens`.
- Setup (`make_kit()` in `Tools/Editor/setup_testmap.py`) imports the meshes and **generates** one `DA_Kit_<Piece>`
  (`URTSKitPieceData`) per piece from catalogue plus manifest, behind the `plan_now` hash gate, so a second run changes
  nothing. Never edit a `DA_Kit_*` by hand: the next setup overwrites it.
- `WorldDivide.Art.Kit` checks that catalogue, manifest and data assets agree.

## Folders

Every kit file lives under one root on each side. A kit asset anywhere else is a bug, and `WorldDivide.Art.Kit` fails on
it.

```
Art/Shared/kit/                         sources in git: the only place kit sources live
  kit_catalogue.json                    the one hand-edited file
  build/kit_manifest.json               derived, written by kit_blender.py
  atlases/                              the shared trim atlas, lens/LED atlas and weathering masks
  lighting/<NN>_<Piece>/                SM_Kit_<Piece>.fbx, SM_Kit_<Piece>_LOD1.fbx, references, review shots
  identity/  utilities/  comms/  structure/      the later kits, same pattern
Art/<Faction>/kit/                      faction variants only: material values and the faction's identity pieces
Tools/Art/kit_blender.py                the generator
Tools/Art/kit/<piece>.py                one module per piece

/Game/Art/Shared/Kit/                   engine side, mirrors the sources
  Materials/                            M_Kit_Lens, M_Kit_Housing, MI_Kit_Neutral_*
/Game/Art/Shared/Materials/Building/    the generic building masters every building draws with (#388): M_Bld_Surface,
                                        M_Bld_Glass, M_Bld_Operational, M_Bld_Ownership, M_Bld_Coating (+ Textures/)
  Textures/                             T_Kit_* atlases
  Lighting/                             SM_Kit_*, DA_Kit_* of the lighting kit
  Identity/  Utilities/  Comms/  Structure/
/Game/Art/<Faction>/Kit/                MI_Kit_<Faction>_*, the faction's identity pieces
```

- No kit piece goes in a building's own folder (`Art/USA/buildings/...`, `/Game/Art/USA/Buildings/...`). A building only
  *mounts* pieces.
- The shared flag and the obstruction beacon moved from `Art/Shared/props/` into the kit in M7.00-04 (#389), as
  `identity/01_FlagCloth` (`SM_Kit_FlagCloth`) and `lighting/12_MastBeaconSingle` (`SM_Kit_MastBeaconSingle`, cut to class
  S: 12 segments, 234 triangles, one LOD). Until 12 is remodelled (M7.00-08) it keeps its one `Beacon` slot, its lens told
  apart by UV0, on `M_Beacon_Obstruction`; the flag keeps `M_Flag_Cloth`, its flag set per building from `DA_FlagTypes`.

## Naming

| Asset | Name | Example |
|---|---|---|
| Mesh | `SM_Kit_<Piece>` (+ `_LOD1.fbx` in source) | `SM_Kit_WallLamp` |
| Data asset | `DA_Kit_<Piece>` | `DA_Kit_WallLamp` |
| Material instance | `MI_Kit_<Faction>_<Surface>` | `MI_Kit_Neutral_Lens`, `MI_Kit_USA_Housing` |
| Texture | `T_Kit_<Atlas>_<Map>` | `T_Kit_Trim_BaseColor`, `T_Kit_Trim_ORM` |
| Catalogue id | `<NN>_<Piece>` in its category | `lighting/01_WallLamp` |

`<Piece>` is PascalCase English and names the object, not the building it sits on.

## Factions

- A piece's `faction` is `neutral` or `usa`; `china` and `insurgents` come later.
- **Neutral** pieces carry no faction look: a lamp is a lamp. Their mesh is shared by every faction; the faction only
  picks the material variant.
- A **faction variant** is a set of `MI_Kit_<Faction>_*` with the same slots and parameters (housing colour, wear, lens
  tint), in `Art/<Faction>/kit/` → `/Game/Art/<Faction>/Kit/`. A mount takes the building's faction unless it names one.
- China and the Insurgents are added through **a variant set plus their own identity pieces** (flags, insignia, signs)
  only. No new code, no copies of neutral meshes.

## Piece schema

One entry per piece in `kit_catalogue.json`. Lights are given in the **fixture's own frame**: origin at the mounting
plate, X out of the wall (the direction the lamp faces), Z up, centimetres.

```json
{
  "id": "lighting/01_WallLamp",
  "mesh": "SM_Kit_WallLamp",
  "class": "M",
  "faction": "neutral",
  "slots": ["Housing", "Lens"],
  "atlas": {"Housing": "Trim", "Lens": "Lens"},
  "light": {
    "anchor": "Lens",
    "type": "Spot",
    "candela": 150,
    "kelvin": 3000,
    "radius": 800,
    "cone": [20, 60],
    "group": "Fixtures",
    "behaviour": "Steady",
    "priority": "Primary",
    "max_draw_distance": 15000
  },
  "emergency_default": false,
  "fault_response": "None",
  "mount_points": {"Wall": {"location": [0, 0, 0], "rotation": [0, 0, 0]}},
  "budgets": {"lod0": 1200, "lod1": 400, "slots": 2}
}
```

The catalogue file is `{"pieces": [ ... ]}`.

| Field | Meaning |
|---|---|
| `id` | `<category>/<NN>_<Piece>`; unique, never reused |
| `mesh` | `SM_Kit_<Piece>` |
| `class` | `S`, `M`, `L` or `XL`: sets the triangle and light limits below |
| `faction` | `neutral` or `usa` (later `china`, `insurgents`) |
| `slots` | at most 2 material slots, named after the surface; the lit glass is always `Lens` |
| `atlas` | which shared atlas each slot samples: `Trim` or `Lens` |
| `materials` | optional: slot → a shared instance by name (`MI_Beacon_Obstruction`); a slot it leaves out takes its faction's `MI_Kit_<Faction>_<Slot>` (`MI_Kit_Neutral_Housing` on `M_Bld_Surface`, `MI_Kit_Neutral_Lens` on `M_Kit_Lens`) |
| `light` | absent for emissive-only pieces (S class); otherwise one real light, see below |
| `light.anchor` | the named island it shines from: always `Lens`. Setup puts the light in the middle of the lens face it shines through (manifest bounds); a Rect is as wide and high as that face |
| `light.rotation` | optional: pitch, yaw, roll of the light in the fixture frame; `[0, 0, 0]` (default) shines along +X |
| `light.type` | `Point`, `Spot` or `Rect` (`ERTSBuildingLightType`) |
| `light.candela` | intensity, within the class maximum |
| `light.kelvin` | colour temperature of the real fixture class |
| `light.radius` | attenuation radius, cm |
| `light.cone` | inner and outer cone angle, degrees (Spot, Rect barn doors) |
| `light.group` | `Windows`, `Fixtures`, `Accents`, `GarageLamps`, `Emergency` or `Warning` |
| `light.behaviour` | `Steady`, `Flash` or `Rotate` |
| `light.priority` | `Primary`, or `Secondary`: drawn only at High scalability and above |
| `light.max_draw_distance` | cm from the camera beyond which the real light is off; the lens still glows |
| `emergency_default` | the mount's `Emergency` when it does not say |
| `fault_response` | `None`, `Flicker` or `Off`: what damage does to it, see the state table |
| `mount_points` | named frames on the piece where it meets the building |
| `budgets` | LOD0 and LOD1 triangles and slots, within its class |

`DA_Kit_<Piece>` (`URTSKitPieceData`, `Source/WorldDivide/Data/RTSKitPieceData.h`) holds the same values plus the
measured `LensBounds` and the light's position (`FRTSKitLight`). The class limits below are `RTSKit::GetClassLimits`.

## Mounts

A building mounts pieces with `FRTSKitMount` entries in `URTSBuildingData::Mounts`:

| Field | Meaning |
|---|---|
| `Piece` | the `DA_Kit_<Piece>` |
| `Socket` *or* `Transform` | a building socket by name, or a transform in the building's frame |
| `Variant` | faction variant; defaults to the building's faction |
| `Emergency` | battery-backed: stays on, dimmed, on low power |
| `FaultResponse` | overrides the piece's default |
| `IndicatorColor` | the colour of 09 and 14 |

The building's recipe JSON has **one line per mount**, and setup fills `Mounts` in:

```json
"mounts": [
  {"piece": "lighting/03_EntranceLight", "socket": "EntranceDoor", "emergency": true},
  {"piece": "lighting/02_CageWallLamp", "transform": [[1210, -640, 420], [0, 0, 90]], "fault": "Flicker"},
  {"piece": "lighting/12_MastBeaconSingle", "socket": "MastTipCore"}
]
```

`RTSKit::Expand(BuildingData)` turns the mounts into parts, lights and per-instance data, a pure function: building ×
mount × the light's transform in the fixture frame, with `Fixture` filled in and group and behaviour assigned.
`ApplyParts` and the light creation use the result through their existing route. **Mounting sets the mesh, its glow,
the light's position, direction, intensity, radius and behaviour with no manual step.**

Lens appearance per instance (on/level, group, fault phase, flash phase, indicator colour) is per-instance custom data
read by `M_Kit_Lens`, written with `SetCustomData` and one `MarkRenderInstancesDirty`; the night state stays component
custom primitive data 12 (proven in the spike, see [Per-instance and LOD](#per-instance-and-lod)). The layout is
`RTSKitLensData` (`RTSPrimitiveData.h`): 0 level, 1 group mask (Fixtures, CPD 10), 2 fault phase, 3 flash phase, 4–6
colour, 7 floats. The lens also goes dark with the building's Operational (CPD 8), as every other glow does, and samples
an optional baked finish normal on UV1 (`FinishNormal`, flat by default).

## Light rule

- **Every building light comes from a visible, physical fixture:** a kit `Lens` or an existing building fixture island
  (`FRTSBuildingLight::Fixture`). No light stands in the air.
- **No broad floodlighting.** A building is lit in pools at its doors, walkways and bays, not washed from below.
- **Restrained, natural brightness**, from the real fixture classes. The validator checks each piece against its class:

| Class | Max candela | Max outer cone | Max radius |
|---|---:|---:|---:|
| S | no real light (emissive only) | — | — |
| M | 400 cd | 70° | 12 m |
| L | 600 cd | 60° | 20 m |
| XL | 1,000 cd | 60° | 25 m |

The HQ's own building lights (garage lamps 400 cd, entrance glass 90–120 cd) already sit inside these values.

## States

The building's presentation state (#196) drives the kit's groups:

| State | Normal | Emergency (battery) | Warning | Mast LEDs | Fault |
|---|---|---|---|---|---|
| Idle / Producing | on at night | on at night | off (garage beacon only while the door moves) | on at night | — |
| LowPower | off | 30 %, cool, no flicker | on, slow flash | on (battery) | — |
| Damaged | on | on | off | on | only `Flicker` mounts, at most 2 per building, deterministic |
| Critical | Fixtures off | 30 % | on | on | `Flicker` mounts go out |
| Destroyed | off | off | off | off | — |

- *Normal* is every steady mount without `Emergency`; *Emergency* is a mode of a steady light, not an extra light.
- The lens glow and the real light always switch together.
- Flicker is deterministic: the same building flickers the same way on every machine, from its mount order.
- Flash and Rotate run in the light subsystem's tick only for buildings whose real lights are shown; elsewhere only the
  lens's emissive phase runs, in the material. By day nothing ticks.
- The garage keeps its door, beacons and `GarageLamps` (#192) until the HQ integration (M7.00-09) brings its beacons
  under the `Warning` behaviour, with no change in what the player sees.

## Light budgets

Only **real** lights count. Emissive-only pieces (09, 12, 13, 14, and every lens glow) are free.

| Building class | Steady | Warning |
|---|---:|---:|
| Large | 8 | 2 |
| Medium | 5 | 2 |
| Small | 3 | 1 |

- The HQ is Large and has 7 + 2 today.
- `rts.Fx.BuildingLightMax` caps the real kit lights in view at **24**, on top of the existing
  `rts.Fx.BuildingLightMaxBuildings` (nearest buildings), `Priority=Secondary` only at High scalability and above, and
  `MaxDrawDistance` per light.
- **All of these are starting hypotheses.** Gate A measures them; the producer sets the final values.

## Visual quality

- **Full PBR** on shared atlases: Base Colour, Normal and packed ORM (occlusion, roughness, metallic).
  - One **2K trim atlas** for metal housings, brackets and poles.
  - One **1K lens/LED atlas** for lenses, glass, diffusers and LEDs.
  - One shared **weathering mask set** (grime, streaks, edge wear).
  - These sizes are a starting point; an art issue may raise them with a reason. No 4K set per lamp.
  - A piece may carry a baked **finish normal on UV1** (micro-bevel and surface detail). The masters sample it with
    TexCoord 1 and keep the tangent basis on UV0; it replaces the atlas micro-normal for that piece instead of
    stacking on it. 04 StripLight is the first (`Art/Shared/kit/lighting/04_StripLight/CLAUDE_HANDOFF.md`).
- **Real dimensions** (see each piece's reference). Mounting plates, brackets, gaskets, cable glands and screws are
  geometry on L and XL pieces and normal-map detail on S and M ones (detail under 10 cm lives in the maps,
  `MODEL_PERFORMANCE.md`).
- The lit glass is its own island, named `Lens`, in its own slot.
- **Texel density matches the HQ: 512 px/m** (5.12 px/cm). The HQ's painted and coated surfaces use the 2 m coating set
  (`T_HQ17_Coating_2m_*`, 1024 px over 2 m); measured on `SM_HQ_Shell.fbx` over every coated slot, the UVs run
  0.50 per metre (median, quartiles 0.4999–0.5001), so 1024 × 0.5 = 512 px/m. The trim atlas is laid out at the same 512 px/m
  for housings; lenses and LEDs may go up to 4× that on the lens atlas, since they are the part the eye goes to.
- Small pieces (S, and every piece under 1 m) cast no shadow. No Nanite.

### Triangle budgets

| Class | Pieces | LOD0 | LOD1 |
|---|---|---:|---:|
| S (LED) | 09, 12, 13, 14 | ≤ 250 | = LOD0 (nothing disappears) |
| M | 01, 02, 03, 04, 06, 07 | ≤ 1,200 | ≤ 400 |
| L | 05, 08, 10 | ≤ 2,500 | ≤ 800 |
| XL | 11 | ≤ 4,000 | ≤ 1,200 |

- At most **2 material slots** per piece.
- Kit pieces count towards their building's budget (`MODEL_PERFORMANCE.md`), every time they are drawn.
- A piece switches to LOD1 with its building at 260 m, never on its own; the LOD1 switch shows no visible change in the
  `lod_before`/`lod_after` shots. The building forces its one LOD decision on all its meshes, kit ISMs included
  ([Per-instance and LOD](#per-instance-and-lod), option C2).
- These budgets are hypotheses, adjusted after the close-up review of the first pieces.

## USA Lighting Kit

The fourteen approved pieces. *Phase B* is the #195 fixture art that may become the piece, only after its look is
approved.

| # | Piece | Mesh | Class | Real light | Default group / mode | Fault | Phase B candidate |
|---|---|---|---|---|---|---|---|
| 01 | Standard wall lamp | `SM_Kit_WallLamp` | M | Spot down | Fixtures, Emergency possible | None | — |
| 02 | Industrial cage wall lamp | `SM_Kit_CageWallLamp` | M | Point | Fixtures | Flicker | — |
| 03 | Entrance light | `SM_Kit_EntranceLight` | M | Spot down | Fixtures, Emergency by default | None | — |
| 04 | Ceiling strip light | `SM_Kit_StripLight` | M | Rect | Fixtures | Flicker | — |
| 05 | Adjustable directional spotlight | `SM_Kit_DirectionalSpot` | L | Spot, aimed per mount | Accents, Secondary | None | `SM_HQ195_Flood` |
| 06 | Shielded stairwell light | `SM_Kit_StairwellLight` | M | Point / Spot | Emergency by default | None | — |
| 07 | Recessed service-bay light | `SM_Kit_ServiceBayLight` | M | Rect / Spot | GarageLamps | None | — |
| 08 | Amber rotating/flashing warning beacon | `SM_Kit_WarningBeacon` | L | Point, Flash or Rotate | Warning | None | `SM_HQ195_Beacon` |
| 09 | Small configurable indicator LED | `SM_Kit_IndicatorLED` | S | none (emissive) | colour per mount | None | — |
| 10 | Path/area lamp | `SM_Kit_PathLamp` | L | Spot down, Secondary | Fixtures | None | — |
| 11 | Perimeter pole lamp | `SM_Kit_PoleLamp` | XL | Spot down | Fixtures | None | — |
| 12 | Single red mast LED beacon | `SM_Kit_MastBeaconSingle` | S | none (emissive) | obstruction, battery | None | was `SM_Beacon_Obstruction` (#389) |
| 13 | Dual red mast LED beacon | `SM_Kit_MastBeaconDual` | S | none (emissive) | obstruction, battery | None | — |
| 14 | Multicolour status LED cluster | `SM_Kit_StatusCluster` | S | none (emissive) | colour/pattern per mount | None | — |

### Appearance references

What each piece looks like, for Codex's candidates (M7.00-07, M7.00-08, M7.00-12) and for the producer's approval.
Present-day military and industrial fixtures, no brand look. Light values are the catalogue's starting values.

| # | Real-world archetype | Size (cm) | Housing | Lens | Light |
|---|---|---|---|---|---|
| 01 | Cast-aluminium LED wall pack, sloped top, four screws on the face plate | 30 × 20 × 13 | dark grey powder coat, light dust in the fins | frosted polycarbonate, facing down | 3000 K, 150 cd, 20/60°, 8 m |
| 02 | Vapour-tight "jelly jar" bulkhead: round cast base, glass globe, steel wire cage | Ø 14 × 26 | olive-grey cast aluminium, galvanised cage, rust at the cage feet | clear ribbed glass globe, warm filament-look LED | 3000 K, 60 cd, point, 6 m |
| 03 | Over-door canopy light: hooded box above a door, angled downward | 35 × 20 × 12 | matt black or grey sheet steel, drip edge | flat opal diffuser on the underside | 3500 K, 200 cd, 25/55°, 7 m |
| 04 | IP65 linear LED batten on a ceiling or soffit (approved 2026-10-22) | 120 × 18.2 × 12.4 | graphite powder-coated housing, satin | neutral opal polycarbonate diffuser along the length | 3500 K, 180 cd, rect 108 × 13 cm, 6 m |
| 05 | Compact LED floodlight head on a U-yoke bracket with a tilt knuckle | head 25 × 22 × 10, bracket 15 | dark grey die-cast with rear cooling fins | flat clear glass over a reflector grid | 4000 K, 600 cd, 10/30°, 20 m |
| 06 | Vandal-resistant round bulkhead with an eyelid shield over the top half | Ø 25 × 10 | grey cast aluminium, four tamper screws | prismatic polycarbonate, lower half | 4000 K, 100 cd, 30/70°, 6 m |
| 07 | Recessed high-bay panel set flush in a bay ceiling, trim frame | 60 × 30 × 10 | white enamel frame, steel trim | prismatic acrylic panel | 4000 K, 300 cd, rect 56 × 26 cm, 12 m |
| 08 | Rotating warning beacon on a flat base, amber Fresnel dome | Ø 14 × 18 | black polycarbonate base, three bolts | amber Fresnel dome with an inner mirror (rotates in the material) | amber (2200 K), 80 cd, point, 8 m; Rotate 1.5 Hz or Flash 1 Hz |
| 09 | Panel-mount pilot light with a chrome bezel | Ø 3 × 2 | bezel only | domed LED cap | emissive, colour per mount |
| 10 | Bollard path light with a louvred head | Ø 20 × 100 | dark grey extruded aluminium, base plate with four anchor bolts | louvres throw light down only | 3000 K, 80 cd, 30/60°, 6 m |
| 11 | Galvanised perimeter pole with a flat "shoebox" LED head on a short arm | pole 600 tall, head 60 × 30 × 10 | galvanised steel pole and base plate, grey head | flat glass underside | 4000 K, 800 cd, 30/55°, 22 m |
| 12 | Aviation obstruction light (low-intensity, red) on a mast tip | the existing beacon, 44 × 56 at scale 1 | grey base, red dome | red glass dome, dark between flashes | emissive red, existing flash |
| 13 | Two red obstruction lights on a short horizontal bar bracket | 40 × 10 × 15 | galvanised bar, two grey bases | two red domes | emissive red, both steady or alternating |
| 14 | Stack light: three or four LED segments on a short pole, or a cluster of four pilot lights on a plate | Ø 6 × 25 | black base, grey cap | segment lenses red, amber, green, blue/white | emissive, colour and pattern per mount |

Appearance approval is per piece: Codex delivers a candidate with close-up and RTS-camera shots, the producer approves
it, and only then does Claude add its catalogue entry.

## Other kits (outline)

Each kit follows the lighting kit: catalogue entries, a module per piece, a Codex candidate, producer approval. They are
incremental and block no building.

| Kit | Neutral pieces | USA pieces |
|---|---|---|
| Identity | flagpole, the flag cloth, blank sign boards and plates, decal cards (masked, on ISM) | USA insignia, stencilled markings, the sign text atlas |
| Utilities | small and large HVAC units, exhaust fan (motion subsystem), vents, pipe modules on a 1 m grid, electrical cabinet, generator, tank | USA paint variants |
| Communications | whip antenna, lattice mast sections, small dish, radome, cable tray | USA paint variants; the HQ's 28/24 m masts stay HQ parts (#379) |
| Structure | caged ladder, railing, roof hatch, access panel, grating, bollard, downspout, all on a 1 m grid | USA paint variants |

## Gate A

Gate A closes the framework and the first lighting kit **on the HQ**: catalogue and generated data assets, mounts,
per-instance lens states, the state table, the light budgets, the lighting pieces the HQ needs, and the HQ lit from kit
fixtures within 7 + 2 lights. The bench measures the hypotheses above and the producer sets the budgets.

The HQ has almost no triangle headroom: 349,568 of its 350,000 at LOD0 and 39,032 of 40,000 at LOD1 (#358). Its kit
fixtures fit only after the shell bake (#373) frees room, or with the producer raising the HQ budget for them; M7.00-09
settles which before it mounts more than the phase B fixtures it replaces.

Reuse on a second building (the Barracks) is not part of Gate A: it follows when that building is worked on, and must
need no kit change.

## Per-instance and LOD

The spike (#387) ran the kit's two risky assumptions in UE 5.8, on non-Nanite instanced static meshes, before production
code relies on them. Its content is throwaway and git-ignored: `ue.ps1 -Task kitspike` builds it in
`/Game/Developers/KitSpike` (`Tools/Editor/kit_spike.py`) and runs `rts.Debug.KitSpikeLens`; `ue.ps1 -Task buildingshots
-Building DA_KitSpike_<Large|Small>_<A|B>` pictures the LOD switch, and option C with `-Cvars
rts.Debug.KitSpikeLODSync=<1|2>`. `WorldDivide.Art.KitSpike` checks the update path headless.

### Per-instance lens state

`M_KitSpike_Lens` reads seven `PerInstanceCustomData` floats per instance (0 on/level, 1 group mask, 2 fault phase, 3 flash
seed, 4-6 colour) and the component's custom primitive data 10 (Fixtures) and 12 (Night), as `ARTSBuilding::ApplyLightData`
writes them to every part. In a row of 200 lenses in one ISM, every lens shows its own state by day, at night, with the
Fixtures group off (only the gated lenses go dark), after one lens alone turned blue, and at a forced LOD1.

Changing 128 of the 200 instances in one frame, 60 frames each, game thread in a 1920x1080 game:

| How | Calls per frame | Scene proxy |
|---|---:|---|
| `SetCustomData` per instance, then one `MarkRenderInstancesDirty` | 0.006 ms | kept, 0 of 60 frames rebuilt |
| `SetCustomDataValue(..., bMarkRenderStateDirty = true)` per float | 0.144 ms | rebuilt every frame |
| `SetCustomData` per instance, then `MarkRenderStateDirty` | 0.009 ms | rebuilt every frame |
| nothing changes (reference) | — | kept |

Frame times stayed within noise (game thread 1.44–1.57 ms, render thread 1.87–1.97 ms for this small scene).

- **Production writes lens state with `SetCustomData` and one `MarkRenderInstancesDirty` per component per frame.** Never
  `SetCustomDataValue` with its dirty flag, and never `MarkRenderStateDirty`: both rebuild the scene proxy, which is the
  re-registration the kit must avoid.
- `SetNumCustomDataFloats` comes before the instances are added, since it resets the data.
- Night stays component custom primitive data 12; the instance data and the component data combine in the material
  without any extra write.

### Shared-mesh LOD sync

One kit mesh (`SM_KitSpike_Lamp`, 1.5 m, yellow at LOD0, magenta at LOD1) on two buildings: the USA HQ (38 x 56 m, 15
lamps around its box) and a 16 x 16 x 7 m box shell (6 lamps; grey at LOD0, cyan at LOD1). Both shells and the lamp have
their LOD1 screen size set for 260 m from their own bounds. The comparison sheet is `Saved/KitSpike/lod_sync_sheet.png`
after a run.

| Option | Small building | HQ | Verdict |
|---|---|---|---|
| A. Screen size from the kit mesh's own bounds | lamps and shell agree in the log, but neither switches on screen at 270 m | lamps switch from their own position: 5 of 15 a step early, 1 a step late in the log, none on screen | rejected |
| B. `BoundsScale` from the building radius | identical to A | identical to A | rejected: GPU LOD selection takes the instance radius from the mesh's bounds, not the component's; `BoundsScale` changes only culling |
| C1. The shell's LOD (game-thread estimate) forced on the kit ISMs | lamps switch at 260 m, the shell much later | lamps switch at 260 m, the shell much later | rejected |
| C2. One game-thread LOD decision forced on every mesh of the building | shell and lamps switch together between 252 and 273 m | the whole HQ and its lamps switch together between 255 and 276 m | **works** |

What the renderer does, measured with `r.StaticMeshLODDistanceScale` 1.25, 1.5 and 1.75 on the small building:

- A mesh drawn on its own (the shell) switches at **378–410 m**, not 260 m. An instanced mesh with the same calibration
  (the lamps, GPU LOD selection) switches at **410–441 m**. The two paths do not agree with each other, so no screen size
  per mesh can make a kit ISM follow its shell.
- `RTSUnitLOD` (the `buildingshots` and bench LOD log) predicts 260 m for both, so it is about 1.5x short of what is drawn.
  The HQ's LOD1 switch (#358) was checked on that log: on screen the HQ stays at LOD0 to about 390 m.
- Forced to LOD1 at 270 m (C2), the HQ's LOD1 shows holes and stray triangles on its roofs. At the renderer's own 390 m it
  shows the same; the `far_450m` shot of the production HQ has them too.

**Chosen: C2.** The building decides its LOD once on the game thread (the light subsystem's tick already visits every
building) and forces it on all its meshes, shell, parts and kit ISMs, with `SetForcedLodModel`, only when it changes. All
meshes switch on the same frame, at exactly the distance the spec names, and a kit mesh is shared by every building with
no copy and no `cover_building`. Cost: a forced LOD rebuilds each component's scene proxy once per switch, per building,
which happens only as the camera crosses 260 m; per-instance data survives it.

## Producer decisions (#386)

| Question | Decision |
|---|---|
| Is the Barracks proven at Gate A on `DA_TestBarracks` or on production art? | Neither: the Barracks is not part of Gate A. Gate A proves the kit on the HQ; the Barracks mounts the kit when it is worked on. |
| Does 12 replace `SM_Beacon_Obstruction`? | `SM_Beacon_Obstruction` (the red light on the HQ's two mast tips, #351/#379) **is** piece 12: it moves into the kit as `SM_Kit_MastBeaconSingle`, no new model. |
| Starting values for the class budgets, the light budgets and the 30 % backup level | Accepted as listed above, as hypotheses for Gate A. |
| Does #199 wait for Gate A? | No extra blocking: the HQ sign-off follows the HQ lighting (M7.00-09), not the Gate A bench. |

## Producer decisions (#387)

| Question | Decision |
|---|---|
| How does a shared kit mesh switch LOD with its building? | **C2**: one LOD decision per building on the game thread, forced on all its meshes. Kit meshes are shared; `cover_building` and per-building duplicate meshes are not used for kit pieces. Built in M7.00-05. |
| The HQ's LOD1 roof holes, which C2 shows from 260 m instead of about 390 m | Fixed in a follow-up issue, not by switching later. |

## Approval

<!-- APPROVAL -->
