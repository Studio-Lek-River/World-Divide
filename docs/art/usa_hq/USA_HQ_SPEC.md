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

- Slab 38 m along Unreal X × 56 m along Y, 17.98 m to the top of the central tower. Origin on the ground at the slab's centre; +X is the front and the garage exit, +Y is west.
- Footprint 38 × 56 cells on the 100 cm grid. MeshScale (1, 1, 1): no fit stretch.
- Garage opening 12.6 × 5.6 m, a 7 m clear vehicle lane, bay 15.044 m deep. Sockets (cm, building frame):

| Socket | Location |
|---|---|
| GarageSpawn | (742.2, −1758.5, 5) |
| GarageExit | (1494.4, −1758.5, 5) |
| GarageDoorPivot | (1474.4, −1758.5, 565) |
| FlagCourtyard / FlagCore / MastTip | (1115.3, 0, 1700) / (−1238.7, −694.5, 1210) / (−1080.0, 0, 1800) |
| RadarPivot, WingDish pivots, DriveShaftPivot | see `usa_hq.json` |

## Assembly

- Mesh: `SM_HQ20_Architecture_WallCorrected`. The other 37 components are `URTSBuildingData::Parts`, attached to Mesh so they rise and scale with the construction visual and the rebuild.
- The 16 door sections, bottom seal, top rollers, lift cables and drive shaft stand at the closed pose of the delivered door cycle (time 0). The radar, wing dish and four fans stand at rest. They are flagged movable for #192 and #194.
- Small parts (fan rotors, lift cables, rollers, seal, drive shaft) stop drawing beyond 120 m. The four fan rotors cast no shadow. The flag's bounds scale is 1.2 for its wind.

## Materials

The 10 delivery masters are rebuilt by `building_art.py` (the unused `HQ14_Paving` is dropped), with the 49 instances at the delivery's exact values (`usa_hq.json` → `material_instances`):

- `M_HQ_Final_Ownership`: PlayerColor from custom primitive data 4–7 (`RTSPrimitiveData::TeamColor`). Only `MI_HQ17_TeamPanel` uses it.
- `M_HQ_Final_Operational`: EmissionColor × Operational from custom primitive data 8 (`RTSPrimitiveData::LampOn`), 1 on and 0 off. `ARTSBuilding` writes 1 for now; #196 drives it from power.
- `M_HQ17_Coating`: roughness clamp(Roughness + CoatingRoughness.R − 0.67, 0.06, 0.98), with the 2 m coating texture set.
- `M_HQ21_FlagWind`: world position offset wind (two waves, speed 1.55, 18 cm, pinned by UV0.U²). It is two-sided, with a derivative normal. The national colours are fixed and never the owner's.
- `ARTSBuilding::ApplyTeamColor` writes the same custom primitive data to Mesh and every part. Every HQ shares the same instances, so there are no dynamic instances.

## Collision, navigation and selection

Simple boxes in `URTSBuildingData::CollisionBoxes`, BlockAllDynamic with a NavArea_Null cut. The meshes have no collision. Values in metres, building frame:

| Box | X | Y | Z |
|---|---|---|---|
| Rear | −19 … −0.749 | −28 … 28 | 0 … 18 |
| FrontWest | −0.749 … 19 | −10 … 28 | 0 … 8.4 |
| FrontEast | −0.749 … 19 | −28 … −25.169 | 0 … 7.6 |
| GarageBay (garage) | −0.749 … 14.944 | −25.169 … −10 | 0 … 7.6 |

- The apron in front of the garage door (X 14.944 … 19, Y −25.169 … −10) has no box, so the navmesh reaches the door.
- The garage bay is solid in #191. #192 opens it for the vehicle leaving.
- Selection and placement keep the 38 × 56 footprint. The placement ghost shows the architecture mesh only.
- Units still leave at the centre of the +X edge until #192 routes them through the garage.

## LODs and budgets

- LODs are made at import with the vehicles' switch distances (`vehicle_art.LOD_DISTANCES`: 35, 110 and 260 m from the RTS camera).
- The architecture, roofs, courtyard and garage interior are so large that the engine caps their screen sizes. They keep full detail up to about 108 m and draw LOD2 at the default zoom (150 m).
- Meshes under 2,000 triangles keep one LOD. Nanite is off.

| | LOD0 | LOD1 | LOD2 | LOD3 |
|---|---|---|---|---|
| Whole HQ, triangles (every part counted) | 3,241,808 | 428,225 | 118,123 | 26,753 |
| Budget asserted by `WorldDivide.Art.USAHeadquarters` | n/a | ≤ 450k | ≤ 150k | ≤ 40k |

The full-detail model is kept for close-ups. Performance acceptance with several HQs is #199.
