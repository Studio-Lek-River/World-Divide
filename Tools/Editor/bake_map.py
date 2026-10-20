"""Bakes what the lobby and the minimap know of a map from its level (#289): the square its preview covers (the level's
NavMeshBoundsVolumes, else its Landscape, RTSMaps::FindMapArea), the top-down preview image over it (map_preview.py) and its
start spots (the RTSStartSpots, or PlayerStarts tagged Team<N>, RTSMaps::FindStartSpots), into the map data asset
/Game/Data/Maps/DA_Map<Name> and the texture T_Preview_<Level>. A start spot used twice or left out fails the bake.

The data asset is created when missing; otherwise only Map, Preview, PreviewArea, StartSpots and StartSpotPositions change,
and a hand-set DisplayName and DefaultSeats stay. A new map is named after its level and seats a human at start spot 1
against AI at every other spot.

setup_testmap.py bakes the levels it builds through read_layout and baked_properties. For a hand-authored level, run
Tools/Scripts/ue.ps1 -Task bakemap -Map /Game/Maps/<Level> [-Render], which runs this file as a commandlet script with the
level in WD_BAKE_MAP and the data asset name, when given, in WD_BAKE_MAP_DATA (docs/world/maps/AUTHORING_A_MAP.md). If the
texture or the data asset exists read-only (LFS lockable, not locked), nothing is written and the files are listed in
Saved/Setup/ReadOnlyAssets.txt, as setup does.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import map_preview  # noqa: E402

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
CONTENT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
READ_ONLY_REPORT = os.path.join(PROJECT_DIR, "Saved", "Setup", "ReadOnlyAssets.txt")

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def level_name(level_path):
    return level_path.rsplit("/", 1)[1]


def map_name(level_path):
    """L_RiverFord -> RiverFord."""
    name = level_name(level_path)
    return (name[2:] if name.startswith("L_") else name).replace("_", "")


def data_asset_path(data_asset_name):
    return f"{map_preview.MAP_DATA_DIR}/{data_asset_name}"


def read_layout(level_path):
    """Loads level_path and returns its preview area, (min x, min y, max x, max y), and the location of start spot N at
    index N. Raises when the level has no area, no start spots, or a start spot used twice or left out."""
    if not levels.load_level(level_path):
        raise RuntimeError(f"Could not load {level_path}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    area, spots, problems = unreal.RTSEditorScripting.read_map_layout(world)
    if problems:
        raise RuntimeError(f"Cannot bake {level_path}: " + "; ".join(str(problem) for problem in problems))
    return (area.min.x, area.min.y, area.max.x, area.max.y), list(spots)


def baked_properties(level_path, area, spots):
    """The properties of the map data asset that come from the level."""
    preview = map_preview.map_preview_path(level_path)
    return {
        "map": unreal.SoftObjectPath(f"{level_path}.{level_name(level_path)}"),
        "start_spots": len(spots),
        "preview": unreal.SoftObjectPath(f"{preview}.{level_name(preview)}"),
        "preview_area": map_preview.preview_box(area),
        "start_spot_positions": [unreal.Vector2D(*map_preview.preview_position(area, spot.x, spot.y)) for spot in spots],
    }


def default_seats(spot_count):
    seats = []
    for spot in range(spot_count):
        seat = unreal.RTSSeat()
        seat.set_editor_property("type", unreal.RTSSeatType.HUMAN if spot == 0 else unreal.RTSSeatType.AI)
        seat.set_editor_property("start_spot", spot)
        seats.append(seat)
    return seats


def read_only_files(paths):
    """The files of the assets at paths that exist but are read-only, relative to the repository."""
    files = [os.path.join(CONTENT_DIR, path[len("/Game/"):] + ".uasset") for path in paths]
    return [os.path.relpath(file, PROJECT_DIR).replace("\\", "/") for file in files
            if os.path.exists(file) and not os.access(file, os.W_OK)]


def bake_map(level_path, data_asset_name=None):
    """Bakes level_path's preview, preview area and start spots into DA_Map<Name> (or data_asset_name) and its preview
    texture, and saves both. Returns False, writing nothing, when one of them is read-only."""
    data_asset_name = data_asset_name or f"DA_Map{map_name(level_path)}"
    data_path = data_asset_path(data_asset_name)
    texture_path = map_preview.map_preview_path(level_path)

    read_only = read_only_files([texture_path, data_path])
    if read_only:
        os.makedirs(os.path.dirname(READ_ONLY_REPORT), exist_ok=True)
        with open(READ_ONLY_REPORT, "w", encoding="utf-8") as report:
            report.write("\n".join(read_only) + "\n")
        for file in read_only:
            unreal.log_error(f"bake_map: {file} must be rewritten but is read-only (git lfs lock it)")
        unreal.log_error("bake_map: nothing written")
        return False

    area, spots = read_layout(level_path)
    texture = map_preview.build_preview(level_path, area, texture_path)
    if not assets.save_loaded_asset(texture, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {texture_path}")

    if assets.does_asset_exist(data_path):
        asset = assets.load_asset(data_path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.RTSMapData)
        asset = asset_tools.create_asset(data_asset_name, map_preview.MAP_DATA_DIR, unreal.RTSMapData, factory)
    properties = baked_properties(level_path, area, spots)
    if not str(asset.get_editor_property("display_name")):
        properties["display_name"] = unreal.Text(map_name(level_path))
    if not asset.get_editor_property("default_seats"):
        properties["default_seats"] = default_seats(len(spots))
    for key, value in properties.items():
        asset.set_editor_property(key, value)
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {data_path}")
    unreal.log(f"bake_map: baked {level_path} into {data_path}: {len(spots)} start spots, area {area}")
    return True


if __name__ == "__main__" and os.environ.get("WD_BAKE_MAP"):
    if os.path.exists(READ_ONLY_REPORT):
        os.remove(READ_ONLY_REPORT)
    bake_map(os.environ["WD_BAKE_MAP"], os.environ.get("WD_BAKE_MAP_DATA") or None)
