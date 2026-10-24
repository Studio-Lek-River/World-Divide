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
  LOD, bounds, material slots, the measured bounds of the `Lens` island and each LOD's geometry digest: an LOD whose
  digest is unchanged keeps its FBX, so a rerun leaves the kit's files as they are (#425). Nobody edits the manifest.
  Run it with Blender (`blender -b --python Tools/Art/kit_blender.py`) after a catalogue or module change, and commit its output.
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
  Materials/                            M_Kit_Lens, M_Kit_Housing, MI_Kit_Neutral_*, MI_Kit_<Piece>_<Slot>
/Game/Art/Shared/Materials/Building/    the generic building masters every building draws with (#388): M_Bld_Surface,
                                        M_Bld_Glass, M_Bld_Operational, M_Bld_Ownership, M_Bld_Coating (+ Textures/)
  Textures/                             T_Kit_* atlases, T_Kit_<Piece>_<Slot>_<Map>, T_Kit_White(Masks)
  Lighting/                             SM_Kit_*, DA_Kit_* of the lighting kit
  Identity/  Utilities/  Comms/  Structure/
/Game/Art/<Faction>/Kit/                MI_Kit_<Faction>_*, the faction's identity pieces
```

- No kit piece goes in a building's own folder (`Art/USA/buildings/...`, `/Game/Art/USA/Buildings/...`). A building only
  *mounts* pieces.
- The shared flag and the obstruction beacon moved from `Art/Shared/props/` into the kit in M7.00-04 (#389), as
  `identity/01_FlagCloth` (`SM_Kit_FlagCloth`) and `lighting/12_MastBeaconSingle` (`SM_Kit_MastBeaconSingle`, cut to class
  S: 12 segments, 234 triangles, one LOD). M7.00-08 (#393) remodelled 12 from the producer's image as a class M piece
  with `Housing` and `Lens` on the kit masters, its lens blinking on `M_Kit_Lens` (`Blink`); the old
  `M_Beacon_Obstruction` went in #438; the flag keeps `M_Flag_Cloth`, its flag set per building from `DA_FlagTypes`.

## Naming

| Asset | Name | Example |
|---|---|---|
| Mesh | `SM_Kit_<Piece>` (+ `_LOD1.fbx` in source) | `SM_Kit_WallLamp` |
| Data asset | `DA_Kit_<Piece>` | `DA_Kit_WallLamp` |
| Material instance | `MI_Kit_<Faction>_<Surface>`; a piece's own finish `MI_Kit_<Piece>_<Slot>` | `MI_Kit_Neutral_Lens`, `MI_Kit_USA_Housing`, `MI_Kit_WallLamp_Housing` |
| Texture | `T_Kit_<Atlas>_<Map>`; a piece's own map `T_Kit_<Piece>_<Slot>_<Map>` | `T_Kit_Trim_BaseColor`, `T_Kit_WallLamp_Lens_LightProfile` |
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
| `materials` | optional: slot → a shared instance by name (`MI_Kit_Neutral_Lens`); a slot it leaves out takes its faction's `MI_Kit_<Faction>_<Slot>` (`MI_Kit_Neutral_Housing` on `M_Bld_Surface`, `MI_Kit_Neutral_Lens` on `M_Kit_Lens`) |
| `light` | absent for emissive-only pieces (S class); otherwise one real light, see below |
| `light.anchor` | the named island it shines from: always `Lens`. Setup puts the light in the middle of the lens face it shines through (manifest bounds); a Rect is as wide and high as that face |
| `light.rotation` | optional: pitch, yaw, roll of the light in the fixture frame; `[0, 0, 0]` (default) shines along +X |
| `light.offset` | optional (#455): cm the light stands out from its lens along `rotation`, so a shadowless Point lighting the area round a wall lamp does not burn the wall behind it white |
| `light.type` | `Point`, `Spot` or `Rect` (`ERTSBuildingLightType`) |
| `light.tube` | optional (#456): a Point whose source is a tube (`SourceLength`) as long as its lens's face along its Z and as thick as it across its Y, a fluorescent tube's soft all-round light; turn it with `rotation` so its Z runs along the tube |
| `light.candela` | intensity, within the class maximum |
| `light.kelvin` | colour temperature of the real fixture class |
| `light.radius` | attenuation radius, cm |
| `light.cone` | inner and outer cone angle, degrees (Spot, Rect barn doors) |
| `light.group` | `Windows`, `Fixtures`, `Accents`, `GarageLamps`, `Emergency` or `Warning` |
| `light.behaviour` | `Steady`, `Flash` or `Rotate` |
| `light.priority` | `Primary`, or `Secondary`: drawn only at High scalability and above |
| `light.max_draw_distance` | cm from the camera beyond which the real light is off; the lens still glows |
| `light.profile` | optional (#440): its light distribution profile, `downlight`, `spot` or `flood` (`building_art.LIGHT_PROFILES`, a hot centre and a soft edge), imported as `LP_Kit_<Profile>`. Point and Spot only: a Rect takes none, its area already softens it |
| `beam` | optional (#440): `{"strength", "length"}`, the soft beam a Spot or Rect light throws through the air at night, see Night look |
| `glare` | optional (#440): the strength of the glare halo round the lit lens at night |
| `glare_radius` | optional (#456): the halo's radius across, cm; 1.3 × the lens's half size by default |
| `glare_shape` | optional (#456): `round` (default) or `tube`, a halo stretched along the lens's longest side to its ends plus `glare_radius` |
| `emergency_default` | the mount's `Emergency` when it does not say |
| `fault_response` | `None`, `Flicker` or `Off`: what damage does to it, see the state table |
| `mount_points` | named frames on the piece where it meets the building |
| `budgets` | LOD0 and LOD1 triangles and slots, within its class |
| `finish` | optional (#392): slot → the slot's values, for a piece delivered with its own maps. Setup imports `textures/T_<Piece>_<Slot>_<Map>.png` from the piece's folder (BaseColor, ORM, Finish_Normal; the Lens also LightProfile) and builds `MI_Kit_<Piece>_<Slot>` on `M_Kit_Housing` or `M_Kit_Lens`; a number is a scalar (`Intensity`), a list a colour (`EmissionTint`). It overrides `materials` and the faction variant for that slot |

`DA_Kit_<Piece>` (`URTSKitPieceData`, `Source/WorldDivide/Data/RTSKitPieceData.h`) holds the same values plus the
measured `LensBounds` and the light's position (`FRTSKitLight`). The class limits below are `RTSKit::GetClassLimits`.

## Mounts

A building mounts pieces with `FRTSKitMount` entries in `URTSBuildingData::Mounts`:

| Field | Meaning |
|---|---|
| `Name` | the part's name in the building (`FlagPart`, `FindPart`), and its light's `Fixture` |
| `Piece` | the `DA_Kit_<Piece>` |
| `Socket` | a building socket by name; none to place the piece by `Transform` alone |
| `Transform` | in the socket's frame, or the building's without one; its scale scales the piece |
| `MountPoint` | the piece's mount point that meets that frame; none for its first |
| `Variant` | faction variant; defaults to the building's faction |
| `Emergency` | battery-backed: stays on, dimmed, on low power; `Default` takes the piece's `emergency_default` |
| `FaultResponse` | overrides the piece's default (`bOverrideFaultResponse`) |
| `Group` | overrides the piece's light group, for its light and its lens |
| `IndicatorColor` | the colour of 09 and 14 |
| `Brightness` | scales the piece's brightness on this building alone: light, lens glow, glare and beam together; 1 by default |

The building's recipe JSON has **one line per mount**, and setup fills `Mounts` in. `transform` is location, rotation
(pitch, yaw, roll) and scale:

```json
"mounts": [
  {"name": "EntranceLight", "piece": "lighting/03_EntranceLight", "socket": "EntranceDoor", "emergency": true},
  {"name": "BayWall", "piece": "lighting/02_CageWallLamp", "transform": [[1210, -640, 420], [0, 90, 0], [1, 1, 1]], "fault": "Flicker"},
  {"name": "Beacon_MastTipCore", "piece": "lighting/12_MastBeaconSingle", "socket": "MastTipCore", "transform": [[0, 0, 0], [0, 0, 0], [1.75, 1.75, 1.75]]}
]
```

The HQ mounts its two mast beacons and its flag this way since M7.00-05 (#390); setup reads `emergency` (true or false),
`fault` (`None`, `Flicker` or `Off`), `group` and `color` (RGB) since M7.00-06 (#391), and `brightness` since M7.00-32
(#453), and a mount that leaves one out keeps its piece's. A piece that waves (the flag) carries its
`wpo_disable_distance` in the catalogue.

`RTSKit::Expand(BuildingData)` turns the mounts into parts, lights and per-instance data, a pure function: building ×
mount × the light's transform in the fixture frame, with `Fixture` filled in and group and behaviour assigned.
`ApplyParts` and the light creation use the result through their existing route; every mounted piece draws through an
instanced component, even alone, and identical pieces share one. A mount that cannot be placed (no piece, an unknown socket
or mount point) is logged and left out. **Mounting sets the mesh, its glow, the light's position, direction, intensity,
radius and behaviour with no manual step.** `WorldDivide.Buildings.KitMount` tests it.

Lens appearance per instance (on/level, group, fault phase, flash phase, indicator colour) is per-instance custom data
read by `M_Kit_Lens`, written with `SetCustomData` and one `MarkRenderInstancesDirty`; the night state stays component
custom primitive data 12 (proven in the spike, see [Per-instance and LOD](#per-instance-and-lod)). The layout is
`RTSKitLensData` (`RTSPrimitiveData.h`): 0 level, 1 group mask (Fixtures, CPD 10), 2 fault phase, 3 flash phase, 4–6
colour, 7 floats. The level carries the building's state, power included (see [States](#states)), so the lens does not
read Operational (CPD 8): a battery lens stays lit on low power. The lens samples an optional baked finish normal on UV1 (`FinishNormal`, flat by default).

## Light rule

- **Every real building light comes from a kit lamp** mounted on the building, with its light shining from the lamp's
  `Lens`. No light stands in the air, and a building has no lights of its own: its recipe gives mounts, not `lights`.
- **A building mounts lamps from its faction's lamp pack:** pieces whose `faction` is the building's or `neutral`. All
  lamps are neutral for now; a faction pack is added when a faction needs a lamp of its own look.
- **Windows glow, they do not light.** Interior light through glass is the shell's emissive only, with no real light.
- **No broad floodlighting.** A building is lit in pools at its doors, walkways and bays, not washed from below.
- **Restrained, natural brightness**, from the real fixture classes. The validator checks each piece against its class:

| Class | Max candela | Max outer cone | Max radius |
|---|---:|---:|---:|
| S | no real light (emissive only) | — | — |
| M | 400 cd | 70° | 12 m |
| L | 600 cd | 60° | 20 m |
| XL | 1,000 cd | 60° | 25 m |

The HQ's own building lights (garage lamps 400 cd, entrance glass 90–120 cd) sit inside these values; #394 moves them onto kit
lamps.

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

How it runs (#391):

- `RTSBuildingState::GetKitLook` is the table, a pure function of the state inputs and the mount (`FRTSKitPart`, which
  `RTSKit::Expand` resolves from mount and piece). Each column takes the most restrictive condition that holds, as the
  building's own look does: a damaged building low on power is as dark as one only low on power. A mount in the `Warning`
  group, or one that flashes or turns, is a warning light; one with `Emergency` (or in the `Emergency` group) is an
  emergency light; an emergency piece without a real light is an LED (the mast beacons). A construction site is on battery,
  as it is LowPower.
- `URTSBuildingStateComponent` writes every mount's lens with `ARTSBuilding::SetKitLooks` only when a look changes: one
  `SetCustomData` per instance and one `MarkRenderInstancesDirty` per component. The same call sets each real light's
  intensity (level × candela) and its colour (battery tint), so lens and light cannot disagree.
- Kit lights do not follow the group tags (`SetLightGroupOn`); their look switches them. The light subsystem's tick
  (`ARTSBuilding::UpdateKitLights`) runs only for the buildings whose lights are shown, nearest first, and switches each
  light with its lens's flash and fault phase (`RTSKit::IsLensLit`, the same sums as `LENS_HLSL`) and turns the `Rotate` ones
  at 1.5 Hz.
- The flicker and flash phases come from the mount's place among the mounts; the first two `Flicker` mounts flicker.
- `rts.Debug.BuildingState <0..5>` shows every building in one state on this machine (Idle, Producing, LowPower, Damaged,
  Critical, Destroyed; -1 off), for building shots: `ue.ps1 -Task buildingshots -Cvars "rts.Light.ForceDark=1,rts.Debug.BuildingState=2"`. Without it
  buildingshots shows Idle (#442); `-1` shows the state the game has.
- `rts.Debug.KitBrightness <scale>` scales the kit lamps' brightness on this machine, live and without a setup run (#453):
  light, lens glow, glare and beam, on top of each mount's `Brightness`. `rts.Debug.KitBrightnessOnly <name>` limits it to
  the mount of that name or the piece whose id ends in it (`03_EntranceLight`); empty scales every lamp.
  `rts.Debug.KitMountOnly <mount>` mounts only that mount's piece on every building, to picture one lamp alone, and
  `rts.Debug.KitLensSteady 1` holds the blinking mast lenses lit: `ue.ps1 -Task buildingshots -Building DA_TestKitLighting
  -Turn -Night -Cvars "rts.Debug.KitMountOnly=<Mount>,rts.Debug.KitBrightness=2"`.

## Night look

How a lit lamp reads at night beyond the pool it lights (#440). The producer compared a faked beam (route A) with real
volumetric fog (route B) on `DA_TestKitLighting` and the USA HQ at 12, 150 and 450 m
(`Art/Shared/kit/lighting/ingame/beams_440.png`, README there) and chose **A**: volumetric fog stays off the play area
([world-lighting.md](../engine/world-lighting.md)).

- **Pools.** Point and Spot lights take a light distribution profile (`light.profile`) and a source as large as half
  their lens's smaller side (`SourceRadius`), so the pool has a hot centre and a soft edge instead of the hard rim of a
  bare cone.
- **Beams.** A piece with `beam` gets `SM_Kit_<Piece>_Beam`, generated by setup (`building_art.write_beam_glb`): open,
  two-sided sides from the lens's face along its light for `length` cm, flaring a quarter of the way from the inner to the
  outer cone (a Rect 30°). It draws additive with `MI_Kit_Beam` on `M_Kit_Glow`. A round (Spot) beam is one solid body
  of light (#458): the material follows the view ray through the cone (UV1 its radius and growth, UV2 its length) and
  adds up the light inside, fading along the beam and out from its axis to nothing at the edge, cut off by whatever
  stands in it, so the mesh's facets never show (they read as stripes before). A Rect's beam keeps its glowing walls,
  brightest where they face the camera. 05 and 06 have strength 1, 01 and 03 0.25; 07 has none since #457.
- **Glare.** A piece with `glare` gets a halo, `SM_Kit_Glare` (a unit sphere) at its lens's middle, 1.3 × the lens's half
  size or `glare_radius`, on `MI_Kit_Glare`; a `tube` halo (04 and 07, #456, #457) is stretched along the lens's longest side
  (`GlareLength`).
- Beams and halos draw only at night (`ARTSBuilding::ShowKitGlows`), one instanced component per mesh per building, and
  glow with their part's look (`RTSKitGlowData`: level, fault, flash, colour), so they are out when the lamp is out (low
  power, damage, destroyed), flicker and flash with it and turn cooler on battery. `rts.Fx.KitBeams 0` turns the beams off.
- `M_Kit_Glow` is additive translucency: PERFORMANCE_GUIDELINES rule 21's exception for #440.

## Light budgets

Only **real** lights count. Emissive-only pieces (09, 12, 13, 14, and every lens glow) are free.

| Building class | Steady | Warning |
|---|---:|---:|
| Large | 8 | 2 |
| Medium | 5 | 2 |
| Small | 3 | 1 |

- `URTSBuildingData::Size` is the class (`RTSKit::GetLightBudget`); setup takes it from the recipe's `"size"`, Medium when it
  does not say. `RTSKit::CountLights` counts the building's own lights and its mounts' (the garage beacons and kit warning
  lights as warning); `WorldDivide.Buildings.KitMount.LightBudget` checks every building against its class.
- The HQ is Large and has 5 + 0 today (its garage beacons flash from the garage, not from `Lights`).
- `rts.Fx.BuildingLightMax` caps the real kit lights in view at **24**, on top of the existing
  `rts.Fx.BuildingLightMaxBuildings` (nearest buildings), `Priority=Secondary` only at High scalability and above, and
  `MaxDrawDistance` per light.
- **Beams and halos cost no light** (#440). With 240 units at night and four `DA_TestKitLighting` beside them (`ue.ps1
  -Task bench -Unit DA_TestM20Guardian@KitHQ@Night -Quick -Passes`, RTX 4090), the translucency pass goes from 0.071 to
  0.085 ms and draw calls from 1,311 to 1,355; the frame's GPU time does not move measurably. Volumetric fog (route B,
  removed) cost 0.20 ms of GPU time in its own pass on every night frame, with or without lamps in view.
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
  - Until the shared atlases exist, a piece may bring its own maps at the atlas sizes (2K housing, 1K lens) through
    its catalogue `finish` (#392: 01, 02, 03, 05, 06; #437: 04). `M_Kit_Housing` and `M_Kit_Lens` take BaseColor times
    SurfaceColor and ORM times Roughness and Metallic on UV0, FinishNormal on UV1; the lens's glow is also times its
    LightProfile (red) and EmissionTint, turning to HotCoreTint where the profile is above 0.55 by HotCore (#393). Every
    map defaults to white or flat, so an instance without maps draws as before. Moving these pieces onto the shared
    atlases is the atlas issue's work. 04's lens finish normal stays 2048² (#437): at 1K the narrow end highlight
    softens.
  - A finish's `true`/`false` values are the masters' static switches (#393), off by default. `Glass` (lens): the
    LightProfile's green marks a thin shell, drawn dithered so what is inside shows through, 1 − GlassTransmission ×
    (1 − Fresnel), its base colour times GlassDarken; the instance draws masked and two-sided. `Sweep` (both masters):
    world position offset turns part of the piece about its Z axis at SweepRate (1.5 turns a second, as the turning
    light), on the lens where the profile's green is 0, on the housing above SweepMinZ within SweepRadius (cm). 08 uses
    both for its dome and its turning reflector. A lens with `Blink` 1 blinks on its own, its phase from where it stands,
    and with `Alternate` 1 its half at U ≥ 0.5 half a cycle later (13's second dome); only lenses without a real light
    blink.
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
| S (LED) | 09, 14 | ≤ 250 | = LOD0 (nothing disappears) |
| M | 01, 02, 03, 04, 07, 12, 13 | ≤ 1,200 | ≤ 400 |
| L | 05, 06, 08, 10 | ≤ 2,500 | ≤ 800 |
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
| 04 | Ceiling strip light | `SM_Kit_StripLight` | M | Point tube | Fixtures | Flicker | — |
| 05 | Adjustable directional spotlight | `SM_Kit_DirectionalSpot` | L | Spot, aimed per mount | Accents, Secondary | None | — |
| 06 | Roof flood light (twin head) | `SM_Kit_RoofFloodLight` | L | Spot, tilted down | Fixtures | None | — |
| 07 | Recessed service-bay light | `SM_Kit_ServiceBayLight` | M | Point tube | GarageLamps | None | — |
| 08 | Amber rotating/flashing warning beacon | `SM_Kit_WarningBeacon` | L | Point, Flash or Rotate | Warning | None | `SM_HQ195_Beacon` |
| 09 | Small configurable indicator LED | `SM_Kit_IndicatorLED` | S | none (emissive) | colour per mount | None | — |
| 10 | Path/area lamp | `SM_Kit_PathLamp` | L | Spot down, Secondary | Fixtures | None | — |
| 11 | Perimeter pole lamp | `SM_Kit_PoleLamp` | XL | Spot down | Fixtures | None | — |
| 12 | Single red mast LED beacon | `SM_Kit_MastBeaconSingle` | M | none (emissive) | obstruction, battery | None | was `SM_Beacon_Obstruction` (#389) |
| 13 | Dual red mast LED beacon | `SM_Kit_MastBeaconDual` | M | none (emissive) | obstruction, battery | None | — |
| 14 | Multicolour status LED cluster | `SM_Kit_StatusCluster` | S | none (emissive) | colour/pattern per mount | None | — |

### Appearance references

What each piece looks like, for Codex's candidates (M7.00-07, M7.00-08, M7.00-12) and for the producer's approval.
Present-day military and industrial fixtures, no brand look. Light values are the catalogue's starting values.

| # | Real-world archetype | Size (cm) | Housing | Lens | Light |
|---|---|---|---|---|---|
| 01 | Cast LED wall pack with a chamfered top on a back plate, screwed bezel | 30 × 13 × 12 | dark weathered cast metal | ribbed frosted diffuser on the underside | 3000 K, 150 cd, 20/60°, 8 m |
| 02 | Oval cast bulkhead with a cast cage on a rectangular back plate | 14 × 26 × 12 | dark cast metal, rust and wear on the cage and plate edges | frosted glass globe | 3000 K, 60 cd, point, 6 m |
| 03 | Over-door box light cantilevered from a wall bracket plate | 35 × 20 × 10 | matt dark sheet steel, drip edge | flat opal diffuser on the underside | 3500 K, 200 cd, 25/55°, 7 m |
| 04 | IP65 linear LED batten on a ceiling or soffit (approved 2026-10-22) | 120 × 18.2 × 12.4 | graphite powder-coated housing, satin | neutral opal polycarbonate diffuser along the length | 3500 K, 180 cd, rect 108 × 13 cm, 6 m |
| 05 | Cylindrical spot can on a slotted swivel arm and a two-bolt wall plate, braided cable | can Ø 18 × 24, arm 25, plate 10 × 30 | dark weathered die-cast | clear glass over a honeycomb reflector | 3000 K, 600 cd, 10/30°, 20 m |
| 06 | Two rectangular LED flood heads on a U-yoke, square post and four-bolt roof base plate | 82 × 28 × 71 | dark weathered steel, rear cooling fins | three LED panels per head behind glass | 4000 K, 500 cd, 25/55°, 20 m |
| 07 | Recessed high-bay panel set flush in a bay ceiling, trim frame | 60 × 30 × 10 | white enamel frame, steel trim | prismatic acrylic panel | 4000 K, 300 cd, rect 56 × 26 cm, 12 m |
| 08 | Rotating warning beacon: amber Fresnel dome on a two-band cast collar, short stem, bolted square base plate (#393) | Ø 18 × 32 | blackened cast iron, four hex bolts | see-through amber dome over a real bulb, holder and mirror that turn (`Glass`, `Sweep`) | amber (2200 K), 80 cd, point, 8 m; Rotate 1.5 Hz or Flash 1 Hz |
| 09 | Small chamfered wall box with a ribbed diffuser, slotted end plug, two Torx screws (#393); its `Wall` mount point turns it so the diffuser, modelled underneath, faces out from the wall and the screws up, where the RTS camera sees it (#441) | 24 × 9 × 9 | worn bare steel | ribbed neutral diffuser in a screwed bezel | emissive, colour per mount |
| 10 | Bollard path light with a louvred head | Ø 20 × 100 | dark grey extruded aluminium, base plate with four anchor bolts | louvres throw light down only | 3000 K, 80 cd, 30/60°, 6 m |
| 11 | Galvanised perimeter pole with a flat "shoebox" LED head on a short arm | pole 600 tall, head 60 × 30 × 10 | galvanised steel pole and base plate, grey head | flat glass underside | 4000 K, 800 cd, 30/55°, 22 m |
| 12 | Aviation obstruction light (low-intensity, red) on a mast tip: dome, clamp-lug collar, stem with a gland nut (#393) | 46 × 46 × 56 at scale 1 | dark cast metal | red glass dome over an LED column | emissive red, blinking (`Blink`) |
| 13 | Two red obstruction lights (12's head at 0.75 ×) on a bolted horizontal bar (#393) | 80 × 35 × 45 | weathered steel bar, gland nuts | two red domes | emissive red, blinking in turn (`Alternate`) |
| 14 | Stack light: three or four LED segments on a short pole, or a cluster of four pilot lights on a plate | Ø 6 × 25 | black base, grey cap | segment lenses red, amber, green, blue/white | emissive, colour and pattern per mount |

Appearance approval is per piece: Codex delivers a candidate with close-up and RTS-camera shots, the producer approves
it, and only then does Claude add its catalogue entry. 01, 02, 03, 05 and 06 were approved on 2026-10-23 (#392), 04 on
2026-10-22 and 07, 08, 09, 12 and 13 on 2026-10-23 (#393; each piece's `STATE.md`). The game-side shots of a piece come
from `DA_TestKitLighting` in `/Game/Data/Showcase`, a plain Large block with 01, 02, 03, 05, 09 (blue), 04 and 07 on its
front and 06, 08, 12 and 13 on its roof: `ue.ps1 -Task buildingshots -Building DA_TestKitLighting`, with `-Night` for
the night (#424) and `-Cvars rts.Debug.BuildingState=2` for low power. Buildingshots shows the building Idle unless the run
sets `rts.Debug.BuildingState` (#442; L_Bench's team is short of power). The in-game shots of 01–09, 12 and 13 are in
`Art/Shared/kit/lighting/ingame/` (#392).

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

Since the shell bake (#373) and the wing dish's hand-made LOD1 (#406) the HQ draws 145,701 of its 150,000 triangles at LOD0
and 39,960 of 40,000 at LOD1, kit pieces included: its kit
fixtures (M7.00-09) have about 5,500 at LOD0 and 400 at LOD1.

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
  The holes were the shell's engine-collapsed LOD1, which the #373 bake replaced. `rts.Debug.BuildingForceLOD 1` forces
  every building to LOD1 at any distance, so `ue.ps1 -Task buildingshots -Cvars rts.Debug.BuildingForceLOD=1` pictures
  LOD1 at 12 and 150 m (#406). Seen that way, the engine's 400-triangle wing dish was a blob; it has a hand-made LOD1
  since #406. The baked shell's LOD1 still differs from LOD0 on the roofs (coping band, thinned lattice racks, vent hoods,
  the radome) and the core's glazing, which is a follow-up.

**Chosen: C2.** The building decides its LOD once on the game thread (the light subsystem's tick already visits every
building) and forces it on all its meshes, shell, parts and kit ISMs, with `SetForcedLodModel`, only when it changes.
Built in M7.00-05 (#390): `URTSBuildingData::LODSwitchDistance` (the HQ's 260 m, from its layout's `switch_distance`; 0
leaves the renderer to pick) against the nearest camera, `ARTSBuilding::SetForcedLOD`. All
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
| The HQ's LOD1 roof holes, which C2 shows from 260 m instead of about 390 m | Fixed in a follow-up issue, not by switching later. Gone with the #373 bake; the wing dish got a hand-made LOD1 in #406, the shell's remaining LOD1 differences are a follow-up. |

## Producer decisions (#392)

| Question | Decision |
|---|---|
| The producer's reference images (issue comments, now `lighting/<NN>_<Piece>/reference/`) differ from the archetype text | The images win (2026-10-23). 05 is a cylindrical spot on a swivel arm, not a U-yoke flood, and the Phase B flood is not its source. |
| 06's image is a twin roof flood light, not the shielded stairwell light | 06 becomes the roof flood light `SM_Kit_RoofFloodLight`, class L, in the Fixtures group. |

## Producer decisions (#393)

| Question | Decision |
|---|---|
| The producer's images are numbered 07 (rotating beacon) and 08 (small indicator light), the catalogue's 07 and 08 are the service-bay light and the warning beacon | The catalogue numbers stay (2026-10-23): image "07" is 08 `SM_Kit_WarningBeacon`, image "08" is 09 `SM_Kit_IndicatorLED` (a small worn-steel wall box, its colour per mount), and 07 `SM_Kit_ServiceBayLight` is modelled from its archetype text. |
| 12 versus `SM_Beacon_Obstruction` | 12 is the obstruction beacon (#386), remodelled from the producer's image; the HQ's two mast tips draw it. |
| 12 and 13 at 250 triangles read as polygons up close, and 12 stands on the HQ's mast tips at 1.75 × | 12 and 13 are class M (1,200 / 400) and cast a shadow; 13's domes are 0.75 × 12's, 80 × 35 × 45 cm. |
| 08's colour | Amber, as the archetype, not the image's red-orange; its inside is real geometry that turns (`Sweep`) behind a see-through dome (`Glass`). |

## Producer decisions (#440)

| Question | Decision |
|---|---|
| Faked beams (A) or volumetric fog (B) for the lamps' light in the air | A (2026-10-24), from the side-by-side night shots and their cost: the beams read at 12, 150 and 450 m for about 0.02 ms, the fog showed a faint glow at 12 m only for 0.2 ms on every night frame. B's trial is removed; rule 21 gets the exception for `M_Kit_Glow`. |

## Approval

<!-- APPROVAL -->
