# USA HQ: as-built specification

Status, 2026-10-21: the final art (Candidate22) is accepted by the producer, and #187–#190 are closed against it. #191 imports it through setup. The garage sequence (#192), flag variation (#193), equipment motion (#194), lighting (#195), state controller (#196), VFX/audio (#197), icon (#198) and sign-off (#199) build on this.

## Sources

| What | Where |
|---|---|
| Blender source | `Art/USA/buildings/usa_hq/source/USA_HQ_Final.blend` |
| 22 FBX meshes, 9 textures | `Art/USA/buildings/usa_hq/fbx/`, `textures/` |
| Layout the game is built from | `Art/USA/buildings/usa_hq/usa_hq.json`, written by `Tools/Art/usa_hq_recipe.py` from the delivery's engine records in `recipe/` |
| Door curves, equipment motion | `Art/USA/buildings/usa_hq/animation/` (`CT_HQ20_DoorMotion.csv`, `door_motion.json`, `equipment_motion.json`) |
| Unreal import | `Tools/Editor/building_art.py`, `make_usa_hq` in `Tools/Editor/setup_testmap.py`, into `/Game/Art/USA/Buildings/USAHQ/{Textures,Materials,Meshes}` |
| Data | `DA_TestHeadquarters` (asset and id unchanged) |
| Final review captures (preview level, before integration) | `Art/USA/buildings/usa_hq/renders/final_*.png`, `animation/*.webp` |
| Import report | `Art/USA/buildings/usa_hq/report_ue5.md` |

Earlier candidates, recovery archives and the blockout are gone from the repository. They are in git history and the producer's local backups only.

## Dimensions and coordinates

- Slab 38 m along Unreal X × 56 m along Y, 13.43 m to the top of the central tower's roof; the communication masts rise to 28 m (core) and 24 m (east roof) (#379). Origin on the ground at the slab's centre; +X is the front and the garage exit, +Y is west.
- Footprint 38 × 56 cells on the 100 cm grid. MeshScale (1, 1, 1): no fit stretch.
- Garage opening 12.6 × 5.6 m, a 7 m clear vehicle lane, bay 15.044 m deep. Sockets (cm, building frame):

| Socket | Location |
|---|---|
| GarageSpawn | (742.2, −1758.5, 5) |
| GarageExit | (1494.4, −1758.5, 5) |
| GarageDoorPivot | (1474.4, −1758.5, 565) |
| FlagCourtyard / FlagCore | (1115.3, 0, 1700) / (−1238.7, −694.5, 1210), as delivered |
| MastTipCore / MastTipEast | (−898.7, 0, 2800) / (−1090, −1995, 2400), the tops of the masts' antenna rods (#379; the delivered MastTip, 1.8 m off the old rod, is dropped) |
| RadarPivot, WingDish pivots, DriveShaftPivot | see `usa_hq.json` |

## Assembly

- Mesh: `SM_HQ20_Architecture_WallCorrected`. The other 37 components are `URTSBuildingData::Parts`, attached to Mesh so they rise and scale with the construction visual and the rebuild.
- The 16 door sections, bottom seal, top rollers, lift cables and drive shaft stand at the closed pose of the delivered door cycle (time 0). The radar, wing dish and four fans stand at rest. They are flagged movable for #192 and #194.
- Small parts (fan rotors, lift cables, rollers, seal, drive shaft) stop drawing beyond 120 m. The four fan rotors cast no shadow. Every mesh's bounds cover the whole building (#358), which also holds the flag's wind; no part needs a bounds scale.

## Materials

The 10 delivery masters are rebuilt by `building_art.py` (the unused `HQ14_Paving` is dropped), with the 49 instances at the delivery's exact values (`usa_hq.json` → `material_instances`):

- `M_HQ_Final_Ownership`: PlayerColor from custom primitive data 4–7 (`RTSPrimitiveData::TeamColor`). Only `MI_HQ17_TeamPanel` uses it.
- `M_HQ_Final_Operational`: EmissionColor × Operational from custom primitive data 8 (`RTSPrimitiveData::LampOn`), 1 on and 0 off. `ARTSBuilding` writes 1 for now; #196 drives it from power.
- `M_HQ17_Coating`: roughness clamp(Roughness + CoatingRoughness.R − 0.67, 0.06, 0.98), with the 2 m coating texture set.
- The delivered flag (`SM_HQ21_NationalFlag_Wind`, `M_HQ21_FlagWind` and its three colour instances) is replaced by the shared flag (#351, below).
- `ARTSBuilding::ApplyTeamColor` writes the same custom primitive data to Mesh and every part. Every HQ shares the same instances, so there are no dynamic instances.

## Equipment motion, beacons and flag (#351)

All values are data: `Tools/Art/usa_hq_recipe.py` (`MOTION`, `BEACONS`, `FLAG_*`) → `usa_hq.json` → `URTSBuildingData::Parts` (`FRTSPartMotion`, `WPODisableDistance`), and `Tools/Editor/building_art.py` (`FLAG_PARAMETERS`, `BEACON_PARAMETERS`) for the shared materials' parameter defaults.

| Part | Motion (about its own axes) | Per-HQ jitter |
|---|---|---|
| ER_Cooling_Fan1_Rotor | spin 240°/s yaw | ±10 % |
| WF_Condenser_Fan1_Rotor | spin 205°/s yaw | ±10 % |
| WM_AHU_Fan1_Rotor | spin 280°/s yaw | ±10 % |
| WM_AHU_Fan2_Rotor | spin 255°/s yaw | ±10 % |
| Radar_Azimuth | spin 72°/s yaw (a turn in 5 s) | ±5 % |
| WingDish_Azimuth | sweep ±12° yaw over 24 s | ±10 % |
| WingDish_Elevation | sweep ±4° pitch over 24 s about `WingDishElevationPivot` (+20 cm), riding on WingDish_Azimuth | ±10 % |

- `URTSBuildingMotionSubsystem` (one `FTickFunction` in `TG_PostUpdateWork`, no tick per HQ) turns them: relative transform = turn about the pivot × rest × the followed part's turn about its pivot, so placement rotation and the construction scale come from `Mesh`. Each HQ picks its rates and phases from where it stands. A part is posed only while its HQ was drawn in the last 0.2 s and a camera is within the part's draw distance; the angles run on regardless. Nothing on a dedicated server. `rts.Fx.BuildingMotion 0` freezes them.
- Masts (#379): `SM_HQ22_MastCore_H28` and `SM_HQ22_MastEast_H28`, out of the shell as static parts `MastCore` / `MastEast`, each at its foot ((−898.7, 0, 1151) and (−1090, −1995, 770)) unturned, with a hand-made LOD1 (3,772 / 1,404 and 5,780 / 2,700 triangles). The producer chose the 28 / 24 m option of three (`Art/USA/buildings/usa_hq/HANDOFF_379.md`).
- Beacons: `SM_Beacon_Obstruction` (shared prop, 44 × 56 cm, 314 triangles, no shadow) at MastTipCore and MastTipEast, scaled 1.75 with the masts, drawn as one instanced component. `M_Beacon_Obstruction` (used with instanced meshes, or a game draws the beacons grey): the lens, dark red glass between flashes, flashes red (1 Hz, on 18 % of the cycle, ±8 % rate and its own phase per beacon from its world position), emissive only, times `Operational` (CPD 8, `RTSPrimitiveData::LampOn`). No light components.
- Flag: `SM_Flag_Cloth` (shared prop, 40 × 24 quads, 1 × 1 m, origin at the hoist's top, U hoist → fly, V top → bottom) at (1150, 10.5, 690), yaw 90, flying along +Y from just outside the pole (13 cm, top at 7.1 m), its top 20 cm under the pole's. Size: hoist 2.2 m, fly 4.18 m (10:19). `URTSBuildingData::FlagType` (`USA`) is looked up in `DA_FlagTypes` (`URTSFlagTypes`, written by setup from `Art/Shared/flags/flags.json`); `ARTSBuilding::ApplyParts` puts its instance (`MI_Flag_USA` on `M_Flag_Cloth`) on `FlagPart` and sets the cloth's width to its height × the flag's aspect. Adding a flag: a PNG in `Art/Shared/flags/`, an entry in `flags.json`, setup.
- `usa.png` (2048 × 1024, drawn by `Tools/Art/flag_textures.py`): Executive Order 10834 — 13 stripes, 50 stars in nine rows of six and five, the union over seven stripes, 0.5385 × 0.76 of the hoist, at the hoist.
- `M_Flag_Cloth`: two-sided, derivative world normal, colours from `FlagTexture`. World position offset: waves travelling hoist → fly (1.35 per flag length, 0.85 Hz), growing as U^1.5 to 24 cm at the fly, plus a faster harmonic; flutter (4.5 per length, 2.6 Hz, U³, 5 cm); sag U² up to 22 cm, more in a lull; gusts over 7 s swell amplitude by 45 % and wave speed by 30 %; per-flag phases from `ObjectPositionWS`; nothing at the hoist, so the cloth never leaves or crosses the pole. The wind fades out from 90 to 120 m from the camera, and the part's `WPODisableDistance` (125 m) stops it beyond.

Hooks for #196 (no state logic yet):

- `ARTSBuilding::SetEquipmentMotionScale(float)`: 1 full speed, 0 stopped in place, between slower; the subsystem integrates the angles, so changes never jump. Low power stops the fans and radar with 0.
- Beacons and lit windows follow CPD 8 (`LampOn`/`Operational`), which `ARTSBuilding::ApplyTeamColor` writes as 1 today; write 0 for off.
- `ARTSBuilding::SetLightGroupOn(Group, bOn)`: switches a lighting group (`Windows`, `Fixtures`, `Accents`), its glow and its real lights (see Night lighting).

## Night lighting (#195)

Rule: **light only comes from a lamp or window the model has.** No light component stands in the air; every one names the fixtures it shines from (`FRTSBuildingLight::Fixture`, islands of `Art/USA/buildings/usa_hq/issue195/fixtures.json`, written by `Tools/Art/usa_hq_light_probe.py` from the game shell's lamp and glass slots) and stands within their bounds (`WorldDivide.Art.USAHeadquarters` checks both). A lamp the model lacks is art first (`issue195/BRIEF_floodlights.md`).

- Glow: `M_HQ_Final_Operational` multiplies EmissionColor × Operational × its group's switch × lerp(1, `NightGain`, Night). Groups by instance (`usa_hq_recipe.LIGHT_GROUPS`): `Windows` (InnerLight, InteriorWarm; night ×4), `Fixtures` (FixtureWarm; ×4), `Accents` (FixtureBlue, TeamLight; ×3). CPD 9–11 are the group switches (1 on, the default), CPD 12 the dark (`RTSPrimitiveData::LightWindows`..`LightNight`); the building writes all four together.
- Real lights (`URTSBuildingData::Lights`, `usa_hq_recipe.LIGHTS`), all non-shadow-casting, no GI, tagged `Light.<Group>`:

| Light | Group | From | Type |
|---|---|---|---|
| GarageLampsSouth / North | GarageLamps | the four ceiling-edge lamps along each side wall of the bay, 6.16 m up | Rect 1154 × 21 cm, 65° down into the bay, 400 cd, 18 m; square barn doors (45 cm) keep it off the wall behind |
| CommandWindows | Windows | the entrance's door glass and flanking windows | Rect 654 × 272 cm on the glass plane, facing the courtyard, 300 cd, 14 m |
| 2 façade floodlights | Fixtures | lamp heads to be modelled (Phase B of #195) | Spot |

  Budget: 5 steady lights per HQ (3 now) plus at most 2 garage beacons. Shown only in the dark (`URTSDayNightSubsystem::IsDark`) and only for the `rts.Fx.BuildingLightMaxBuildings` (4) constructed buildings nearest a camera (`URTSBuildingLightSubsystem`); the rest only glow, and by day no light is on.
- Garage lamps: the `GarageLamps` group is on only while the door is not closed (`URTSGarageComponent`): without shadows, a closed door would not stop their light reaching the apron. The lamps' glow stays on.
- Garage beacons: the `GarageBeacons` group of `Lights`, which `URTSGarageComponent` flashes in turn while the door is not closed. The model has no amber beacon lamp yet, so the HQ has none (the brief asks for them).

## Collision, navigation and selection

Simple boxes in `URTSBuildingData::CollisionBoxes`, BlockAllDynamic with a NavArea_Null cut. The meshes have no collision. Values in metres, building frame:

| Box | X | Y | Z |
|---|---|---|---|
| Rear | −19 … −0.749 | −28 … 28 | 0 … 18 |
| FrontWest | −0.749 … 19 | −10 … 28 | 0 … 8.4 |
| FrontEast | −0.749 … 19 | −28 … −25.169 | 0 … 7.6 |
| GarageBay (garage) | −0.749 … 14.944 | −25.169 … −10 | 0 … 7.6 |

- The apron in front of the garage door (X 14.944 … 19, Y −25.169 … −10) has no box, so the navmesh reaches the door.
- The garage bay stays solid and cut out of the navmesh (#192): the vehicle leaving it drives a scripted lane, not a path. Nothing else can path into the bay.
- Selection and placement keep the 38 × 56 footprint. The placement ghost shows the architecture mesh only.

## Garage sequence (#192)

`URTSGarageComponent` presents what `URTSProductionComponent` completes; cost, queue, progress, low-power rate and cancelling stay production's.

1. While the front of the queue is a vehicle due within the door's opening time (3 s, plus 0.5 s), the warning lights come on and the door opens, so it is open the frame the vehicle completes. Low on power the door does not open early: it opens once the vehicle has completed, and the vehicle waits for it. A vehicle cancelled while the door opens for it leaves none, and the door closes again.
2. Each completed vehicle waits in the garage, one entry per completed item, until the door is fully open and nothing stands in the bay or on its lane. It then appears at `GarageSpawn`, facing out of the door, one at a time.
3. It drives straight out at half its top speed (`URTSUnitMovement::SetCommandOverride` through nav obstacles; avoidance leaves it alone). It stops while any unit stands on the lane ahead of it. Once its hull is where a building without a garage puts the unit (footprint edge + the hull's longer half + `ExitClearance`, X 2425 cm for the builder), it goes to the rally point.
4. With nothing more to send out and the bay and apron clear for 1 s, the door closes. A vehicle due again opens it again, even halfway down. The door never closes, or starts closing, with a hull in the bay or on the apron.

Blocked exit: the door stays open and the next vehicle is held in the garage while any unit stands on the lane, out to the lane's end. The owner's idle units there are sent straight on past the lane's end (`ARTSUnit::StepAside`); other units are waited for.

Destroyed HQ: the vehicles still waiting are lost with the building and nothing more comes out. A vehicle on its way out stops its scripted drive and goes to the rally point; one already out keeps going.

Lane: `RTSBuildPlacement::GetExitLaneOffset` puts the exit lane on the garage's Y (−1758.45 cm, turning with the building). `GetExitLocation`, the rally point (lane end, X 3075 cm for the builder) and `CanUnitsLeave` all use it, and `GetExitLocation`'s navmesh nudge moves the point along the lane only.

Door motion: `CT_HQ20_DoorMotion` is imported as a curve table (`GarageDoorMotion`). The game plays its opening, 2 to 10 s of the delivered cycle, in `GarageDoorSeconds` (3 s), and closing plays it backward. Rows `Sxx_X/Z/P` pose the sixteen door instances, the bottom seal follows S00, the lift cables hang from S00 with `Cable_P` and the length scale `Cable_L`, and the drive shaft turns by `Drive_P`. The door's phase and start time replicate, and every machine but a dedicated server poses the door from the server clock.

Warning lights: the data's `GarageBeacons` lights at the portal's beacon lamps (#195), flashing in turn while the door is not closed. The free-standing amber point lights that stood in are gone; until the model has beacon lamps there are none.

## LODs and budgets

- LODs are made at import with the vehicles' switch distances (`vehicle_art.LOD_DISTANCES`: 35, 110 and 260 m from the RTS camera).
- The architecture, roofs, courtyard and garage interior are so large that the engine caps their screen sizes. They keep full detail up to about 108 m and draw LOD2 at the default zoom (150 m).
- Meshes under 2,000 triangles keep one LOD. Nanite is off.

| | LOD0 | LOD1 | LOD2 | LOD3 |
|---|---|---|---|---|
| Whole HQ, triangles (every part counted) | 3,241,808 | 428,225 | 118,123 | 26,753 |
| Budget asserted by `WorldDivide.Art.USAHeadquarters` | n/a | ≤ 450k | ≤ 150k | ≤ 40k |

The full-detail model is kept for close-ups. Performance acceptance with several HQs is #199.
