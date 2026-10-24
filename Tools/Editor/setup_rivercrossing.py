"""Builds and saves the navmesh of WD_RiverCrossing_01 (#154), a hand-authored level that setup_testmap.py never touches
(docs/world/maps/WD_RiverCrossing_01/03_TECHNICAL_PLAN.md sections 2 and 13). When the level does not exist yet it is
first created as the scripted greybox of rivercrossing_greybox.py, with its river material and bridge meshes (#155) and
its roads (#383); an existing level is only loaded, so its terrain and dressing stay as authored. Either way its trees
(rivercrossing_vegetation.py, #158) are then rebuilt and generated in place. -Rebuild has ue.ps1
delete the level and its actor files first, so the greybox is built anew. The river carves the landscape on the GPU,
so building the greybox needs -Render. The map data asset and the preview come from the bake afterwards:

  powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task rivercrossing -Rebuild -Render
  powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bakemap -Map /Game/Maps/WD_RiverCrossing_01 -DataAsset DA_MapRiverCrossing01 -Render

The level stores its actors one file each (One File Per Actor), LFS lockable. If a file the run must rewrite is read-only,
nothing is written: the files are listed in Saved/Setup/ReadOnlyAssets.txt and ue.ps1 prints the git lfs lock lines.
The navmesh build time is logged as "BuildNavigation ... built in".
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rivercrossing_greybox as greybox  # noqa: E402
import rivercrossing_vegetation as vegetation  # noqa: E402

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
READ_ONLY_REPORT = os.path.join(PROJECT_DIR, "Saved", "Setup", "ReadOnlyAssets.txt")
HEIGHTMAP_FILE = os.path.join(PROJECT_DIR, "Saved", "Setup", "WD_RiverCrossing_01_Heightmap.r16")

assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
saving = unreal.EditorLoadingAndSavingUtils


def editor_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def package_file(package):
    """The file on disk of a level or external actor package, or None when it is new."""
    name = package.get_name()
    for extension in (".umap", ".uasset"):
        file = unreal.Paths.convert_relative_path_to_full(
            unreal.Paths.combine([unreal.Paths.project_content_dir(), name[len("/Game/"):] + extension]))
        if os.path.exists(file):
            return file
    return None


def read_only_files(packages):
    files = [package_file(package) for package in packages]
    return [os.path.relpath(file, PROJECT_DIR).replace("\\", "/") for file in files if file and not os.access(file, os.W_OK)]


def main():
    if os.path.exists(READ_ONLY_REPORT):
        os.remove(READ_ONLY_REPORT)
    path = greybox.MAP_PATH
    if assets.does_asset_exist(path):
        if not levels.load_level(path):
            raise RuntimeError(f"Could not load {path}")
        unreal.log(f"setup_rivercrossing: {path} exists, keeping its terrain and actors")
    else:
        if not unreal.RTSEditorScripting.can_render():
            raise RuntimeError(f"Building the greybox of {path} needs a renderer for the river's carving: run with -Render")
        river_material = greybox.build_river_material()
        bridge_meshes = greybox.build_bridge_assets()
        road_materials = greybox.build_road_materials()
        props = greybox.build_prop_assets()
        if not levels.new_level(path):
            raise RuntimeError(f"Could not create {path}")
        greybox.build_greybox(editor_world(), HEIGHTMAP_FILE, river_material, bridge_meshes, road_materials, props)
        unreal.log(f"setup_rivercrossing: built the greybox of {path}")

    world = editor_world()
    changed = greybox.apply_landscape_physics(world)
    unreal.log(f"setup_rivercrossing: set the default physical material of {changed} landscape actors")
    # The trees (#158), on every run: their trunks cut the infantry navmesh, so before it is built.
    vegetation_dirs = vegetation.build(world)
    tiles = unreal.RTSEditorScripting.build_navigation(world)
    if tiles == 0:
        raise RuntimeError(f"Could not build the navmesh of {path}")

    # The level and every actor file the build changed: the navmesh and vegetation actors, and on a new level all of them,
    # with the river material, the bridge's assets, the road materials, the runtime virtual texture, the village's props
    # and the vegetation's graphs.
    owned = (greybox.BRIDGE_DIR + "/", greybox.RIVER_MATERIAL_DIR + "/", greybox.ROAD_MATERIAL_DIR + "/", greybox.RVT_DIR + "/",
             greybox.PROPS_DIR + "/") + vegetation_dirs
    dirty = list(saving.get_dirty_map_packages()) + [package for package in saving.get_dirty_content_packages()
                                                     if package.get_name().startswith(owned)]
    read_only = read_only_files(dirty)
    if read_only:
        os.makedirs(os.path.dirname(READ_ONLY_REPORT), exist_ok=True)
        with open(READ_ONLY_REPORT, "w", encoding="utf-8", newline="\n") as report:
            report.write("\n".join(read_only) + "\n")
        for file in read_only:
            unreal.log_error(f"setup_rivercrossing: {file} must be rewritten but is read-only (git lfs lock it)")
        unreal.log_error("setup_rivercrossing: nothing written")
        return
    if not saving.save_packages(dirty, only_dirty=False):
        raise RuntimeError(f"Could not save {path}")
    unreal.log(f"setup_rivercrossing: saved {path} with {tiles} navmesh tiles ({len(dirty)} packages)")


main()
