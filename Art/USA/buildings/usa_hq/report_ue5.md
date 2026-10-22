# USA HQ: Unreal import report (#191)

`ue.ps1 -Task setup` builds the HQ headless from this folder through `Tools/Editor/building_art.py` and `make_usa_hq` in `Tools/Editor/setup_testmap.py`. The output goes to `/Game/Art/USA/Buildings/USAHQ/{Textures,Materials,Meshes}`. Each asset carries the setup hash of its sources and code, so a rerun rebuilds only what changed. The full-detail import, reductions included, takes about 20 minutes on a Ryzen 7 7800X3D.

## Import

- **Textures:** 9 PNGs with the delivery's compression, sRGB, LOD group and build size. Paving is 3072² in source and built at 2048².
- **Masters:** 9, rebuilt from the delivery's graphs. `HQ14_Paving` was unused and is dropped.
- **Instances:** 49, with the delivery's exact parameter values, and the parent checked after it is set.
- **Meshes:** 15 (the game shell's two and the delivery's 13 moving parts and flag), through Interchange.
  - Static mesh only, normals and tangents as exported, no materials or textures from the FBX, no collision, no Nanite.
  - Slots are bound by name from `usa_hq.json`.
  - Setup fails if a mesh's length along X or its triangle count differs from its record.

## LODs (#358)

The HQ has two LODs and no Nanite (`docs/art/MODEL_PERFORMANCE.md`, Buildings). The whole building switches to LOD1 at 260 m from the RTS camera at once.

- **Game shell:** `Tools/Art/usa_hq_game_blender.py` merges the nine meshes that never move into two meshes under `game/`. Each slot is named after its instance, so slots that share an instance become one.
  - `SM_HQ_Shell` is the bulk, at LOD0 as delivered with the delivery's normals. Setup reduces it to 274,000 triangles with the engine's reduction, which keeps those normals and UVs.
  - `SM_HQ_ShellDetail` holds the small details that read at the RTS camera: solar cells, team lights and panels, RF panels, markings, chips, door light strips, leaves and the planters' soil, which the engine's reduction sank below the rims. LOD0 keeps them whole. An engine reduction over the whole shell lost the solar cells (#349).
  - Each has a hand-made LOD1 (`*_LOD1.fbx`), reduced per source mesh by collapse decimation with those details weighted. Loose pieces under 20 cm (about a pixel at the switch) are left out, and the planters keep a third of their leaves.
  - Rerun the generator, then `python Tools/Art/usa_hq_recipe.py`, whenever the delivery's FBX change.
- **Moving parts and flag:** setup reduces them with the engine to their budgets in `usa_hq_recipe.py`. Parts under 2,000 triangles keep LOD0 only.
- **One switch:** setup extends every mesh's bounds to cover the building's box from each place it is drawn (`building_art.cover_building`). Every part then measures its LOD from about the building's middle. LOD1's screen size is the one for 260 m at those bounds. The door sections cover the box from all 16 of their places, so they switch about 2 % later (radius 3,577 cm against the shell's 3,501). The radar and the dish turn their box with them.
- **Instancing:** parts that share a mesh (the 16 door sections, the 2 lift cables) draw as one instanced component (`ARTSBuilding::ApplyParts`).

| Mesh | Drawn | LOD0 | LOD1 | Sections |
|---|---|---|---|---|
| SM_HQ_Shell | 1 | 267,000 (of 2,915,364) | 19,000 | 33 |
| SM_HQ_ShellDetail | 1 | 41,344 | 10,655 | 12 |
| MastCore, MastEast (hand-made LOD1, #379) | 2 | 9,552 | 4,104 | 6 |
| DoorSection | 16 | 1,200 | 70 | 4 (instanced) |
| Radar_Azimuth | 1 | 3,000 | 400 | 4 |
| WingDish_Elevation | 1 | 3,000 | 400 | 5 |
| NationalFlag_Wind | 1 | 4,000 | 999 | 3 |
| DriveShaft | 1 | 1,000 | 150 | 2 |
| Small parts: wing dish azimuth, seal, rollers, 2 cables, 4 fans | 9 | 3,060 total | LOD0 only | 12 |
| **Whole HQ** | 35 | **349,568** | **39,032** | **79** (was 172) |

The large-building budget is 150,000 at LOD0. The producer raised the HQ's to 350,000 for its 12 m look: at 150,000 the façade's bevelled frames tore apart (#358). `WorldDivide.Art.USAHeadquarters` asserts 350,000 / 40,000, every mesh's own budget, bounds that cover the building, one switch distance and at most 80 sections.

## Decisions

- **Nanite stays off.** The translucent placement ghost cannot draw Nanite (`WorldDivide.Art.PlaceholderWithoutNanite`), the door and equipment parts move, and the glass is translucent. At 240 units a Nanite HQ also cost more than twice an HQ with LODs (#349).
- **Collision:** four simple boxes in `URTSBuildingData::CollisionBoxes` (`USA_HQ_SPEC.md`), not the meshes.
  - The garage bay is a box of its own, marked `garage`, and solid until #192 opens it for the vehicle leaving.
  - The apron in front of the door has no box.
  - The footprint centre lies inside a box, so the navmesh hole check is unchanged.
- **Shadows:** the four fan rotors cast none. Small parts stop drawing beyond 120 m.
- **Ownership and power:** custom primitive data on every component (team colour 4–7, operational 8 = 1). No dynamic material instances.
- **Placement ghost:** shows the game shell's bulk, which carries the full slab silhouette.
- **Base layouts:** the starting builders stand 36 m behind each start spot (was 24 m), clear of the 56 m long HQ on the diagonal skirmish spots.

## Not yet done (later issues)

- Garage spawn, exit route and door control: #192.
- Flag phase variation and WPO disable distance: #193.
- Equipment motion: #194.
- Lights and power on CPD 8: #195/#196.
- Multi-HQ performance and sign-off: #199.
