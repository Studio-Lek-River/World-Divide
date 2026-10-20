# Authoring a map

A hand-built level becomes a lobby map in four steps. The lobby never loads the match level: it shows what `bake_map` stored in the map's data asset, a top-down preview with a marker per start spot. The minimap uses the same preview.

1. **Place the start spots.** Drop an `RTSStartSpot` (Place Actors, search "RTS Start Spot") where each base goes and set its `Spot Index`: 0 for the first, 1 for the second, and so on. Its number (`Spot Index + 1`) shows above it in the editor. Every index from 0 up must exist once. A gap or a duplicate fails the bake. Turn each spot to face the way its base should face.
2. **Place a `NavMeshBoundsVolume`** over the whole playable ground. It sets where units can path, where the camera can pan and what the preview shows: the bake squares its XY bounds around their centre. A level without one falls back to its Landscape bounds, but it will not have a navmesh. Build the navmesh and save the level.
3. **Bake** with the editor closed:

   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bakemap -Map /Game/Maps/<Level> -Render
   ```

   This creates or updates `/Game/Data/Maps/DA_Map<Name>` (`L_RiverFord` becomes `DA_MapRiverFord`; pass `-DataAsset DA_<Name>` to choose another) and its preview `/Game/Data/Maps/T_Preview_<Level>`, 512 × 512. `-Render` renders the preview and needs a GPU; without it the bake draws a flat placeholder. A new data asset is named after the level and seats a human at spot 1 and AI at every other spot. After that, change `Display Name` and `Default Seats` in the editor: a later bake keeps them and only rewrites `Map`, `Preview`, `Preview Area`, `Start Spots` and `Start Spot Positions`. If the data asset or the preview is read-only, `bakemap` stops with exit code 3 and prints the `git lfs lock` lines, as setup does.
4. **List the map in the lobby:** add `+SkirmishMaps=/Game/Data/Maps/DA_Map<Name>.DA_Map<Name>` to `[/Script/WorldDivide.RTSSettings]` in `Config/DefaultGame.ini`. The lobby lists maps in the order they appear there.

Bake again whenever a start spot, the volume or the look of the level changes. The map tests check the result: `WorldDivide.Maps.StartSpotPositions`, `Maps.PreviewArea`, `Maps.PreviewCameraAlignment`, `Maps.SavedNavMesh` and `Smoke.Maps`, which plays every lobby map with every start spot taken.

The levels `setup_testmap.py` generates go through the same bake (`bake_map.read_layout`) right after setup builds them.
