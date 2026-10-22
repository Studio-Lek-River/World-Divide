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
  LOD, bounds, material sections and the measured bounds of the `Lens` island. Nobody edits the manifest.
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
  Textures/                             T_Kit_* atlases
  Lighting/                             SM_Kit_*, DA_Kit_* of the lighting kit
  Identity/  Utilities/  Comms/  Structure/
/Game/Art/<Faction>/Kit/                MI_Kit_<Faction>_*, the faction's identity pieces
```

- No kit piece goes in a building's own folder (`Art/USA/buildings/...`, `/Game/Art/USA/Buildings/...`). A building only
  *mounts* pieces.
- The shared flag (`SM_Flag_Cloth`) and the obstruction beacon (`SM_Beacon_Obstruction`) move from `Art/Shared/props/`
  into the kit (identity and lighting) in M7.00-04; `Art/Shared/props/` and `PROP_SLOTS`/`PROP_BUDGETS` in
  `usa_hq_recipe.py` then disappear.

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

| Field | Meaning |
|---|---|
| `id` | `<category>/<NN>_<Piece>`; unique, never reused |
| `mesh` | `SM_Kit_<Piece>` |
| `class` | `S`, `M`, `L` or `XL`: sets the triangle and light limits below |
| `faction` | `neutral` or `usa` (later `china`, `insurgents`) |
| `slots` | at most 2 material slots, named after the surface; the lit glass is always `Lens` |
| `atlas` | which shared atlas each slot samples: `Trim` or `Lens` |
| `light` | absent for emissive-only pieces (S class); otherwise one real light, see below |
| `light.anchor` | the named island it shines from: always `Lens`. The light stands inside the island's measured bounds (manifest) and points out of it |
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
read by `M_Kit_Lens`; the night state stays component custom primitive data 12. The spike (M7.00-02) proves this in UE
5.8 before production code relies on it.

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
  `lod_before`/`lod_after` shots. How a shared mesh follows each building's switch is settled in the spike (M7.00-02).
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
| 12 | Single red mast LED beacon | `SM_Kit_MastBeaconSingle` | S | none (emissive) | obstruction, battery | None | is `SM_Beacon_Obstruction` |
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
| 04 | IP65 linear LED batten on a ceiling or soffit, two clip brackets | 120 × 8 × 6 | white-grey polycarbonate body, stainless clips | opal diffuser along the length | 4000 K, 120 cd, rect 118 × 5 cm, 6 m |
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

## Producer decisions (#386)

| Question | Decision |
|---|---|
| Is the Barracks proven at Gate A on `DA_TestBarracks` or on production art? | Neither: the Barracks is not part of Gate A. Gate A proves the kit on the HQ; the Barracks mounts the kit when it is worked on. |
| Does 12 replace `SM_Beacon_Obstruction`? | `SM_Beacon_Obstruction` (the red light on the HQ's two mast tips, #351/#379) **is** piece 12: it moves into the kit as `SM_Kit_MastBeaconSingle`, no new model. |
| Starting values for the class budgets, the light budgets and the 30 % backup level | Accepted as listed above, as hypotheses for Gate A. |
| Does #199 wait for Gate A? | No extra blocking: the HQ sign-off follows the HQ lighting (M7.00-09), not the Gate A bench. |

## Approval

<!-- APPROVAL -->
