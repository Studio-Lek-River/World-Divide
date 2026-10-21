# USA HQ: Unreal import report (#191)

`ue.ps1 -Task setup` builds the HQ headless from this folder through `Tools/Editor/building_art.py` and `make_usa_hq` in `Tools/Editor/setup_testmap.py`. The output goes to `/Game/Art/USA/Buildings/USAHQ/{Textures,Materials,Meshes}`. Each asset carries the setup hash of its sources and code, so a rerun rebuilds only what changed. The full-detail import, reductions included, takes about 20 minutes on a Ryzen 7 7800X3D.

## Import

- **Textures:** 9 PNGs with the delivery's compression, sRGB, LOD group and build size. Paving is 3072² in source and built at 2048².
- **Masters:** 9, rebuilt from the delivery's graphs. `HQ14_Paving` was unused and is dropped.
- **Instances:** 49, with the delivery's exact parameter values, and the parent checked after it is set.
- **Meshes:** 22, through Interchange.
  - Static mesh only, normals and tangents as exported, no materials or textures from the FBX, no collision, no Nanite.
  - Slots are bound by name from `usa_hq.json`.
  - Setup fails if a mesh's length along X or its triangle count differs from the delivery's records.

## LODs

Switch distances are the vehicles' (`vehicle_art.LOD_DISTANCES`): 35, 110 and 260 m from the RTS camera, measured with the camera's lens. Screen sizes come from each mesh's own bounds.

The architecture, roof systems and courtyard are about 35 m in radius, so their screen size passes 1 before 110 m, and the engine keeps 1 at most. They show full detail up to about 108 m, LOD2 from there (the default zoom is 150 m) and LOD3 beyond about 260 m.

| Mesh | Drawn | LOD0 | LOD1 | LOD2 | LOD3 |
|---|---|---|---|---|---|
| Architecture_WallCorrected | 1 | 1,074,804 | 150,000 | 44,999 | 10,000 |
| RoofSystems_WallCorrected | 1 | 870,276 | 100,000 | 24,999 | 4,000 |
| Courtyard | 1 | 624,040 | 60,001 | 15,001 | 2,999 |
| GarageInterior | 1 | 327,552 | 30,000 | 6,000 | 1,000 |
| DoorHardware | 1 | 33,628 | 8,000 | 2,000 | 500 |
| CourtyardDetails | 1 | 19,320 | 6,000 | 1,500 | 400 |
| GaragePortal | 1 | 14,652 | 5,000 | 1,500 | 400 |
| DoorSection | 16 | 14,308 | 3,000 | 800 | 150 |
| PlantingFinish | 1 | 12,136 | 4,000 | 1,200 | 300 |
| WingDish_Elevation | 1 | 9,236 | 3,000 | 800 | 200 |
| NationalFlag_Wind | 1 | 8,904 | 5,300 | 2,700 | 1,200 |
| Radar_Azimuth | 1 | 8,224 | 3,000 | 800 | 200 |
| GarageFloor | 1 | 4,548 | 2,000 | 600 | 150 |
| DriveShaft | 1 | 2,636 | 1,000 | 300 | 80 |
| Small parts: wing dish azimuth, seal, rollers, 2 cables, 4 fans | 9 | 3,060 total | single LOD | | |
| **Whole HQ** | 38 | **3,241,808** | **428,225** | **118,123** | **26,753** |

At the default zoom an HQ draws about 118k triangles, down from 3.24M. `WorldDivide.Art.USAHeadquarters` asserts at most 450k / 150k / 40k triangles at LOD1, LOD2 and LOD3.

## Decisions

- **Nanite stays off.** The translucent placement ghost cannot draw Nanite (`WorldDivide.Art.PlaceholderWithoutNanite`), the door and equipment parts move, and the glass is translucent.
- **Collision:** four simple boxes in `URTSBuildingData::CollisionBoxes` (`USA_HQ_SPEC.md`), not the meshes.
  - The garage bay is a box of its own, marked `garage`, and solid until #192 opens it for the vehicle leaving.
  - The apron in front of the door has no box.
  - The footprint centre lies inside a box, so the navmesh hole check is unchanged.
- **Shadows:** the four fan rotors cast none. Small parts stop drawing beyond 120 m.
- **Ownership and power:** custom primitive data on every component (team colour 4–7, operational 8 = 1). No dynamic material instances.
- **Placement ghost:** shows the architecture mesh, which carries the full slab silhouette.
- **Base layouts:** the starting builders stand 36 m behind each start spot (was 24 m), clear of the 56 m long HQ on the diagonal skirmish spots.

## Not yet done (later issues)

- Garage spawn, exit route and door control: #192.
- Flag phase variation and WPO disable distance: #193.
- Equipment motion: #194.
- Lights and power on CPD 8: #195/#196.
- Multi-HQ performance and sign-off: #199.
