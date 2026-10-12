"""Imports local-only reference art (Art/Source/Local, made by Tools/Art/extract_big.py and w3d_to_glb_blender.py) into
/Game/Art/Local. Both folders are gitignored: this art comes from games installed on this machine and is not ours to
commit or ship. Nothing outside /Game/Art/Local is written, so the committed placeholder assets stay untouched.

For each model it builds SM_<name> with a team-tint material instance per glTF material (placeholder_art.py, pointed
at the local folders). Models whose source is missing are skipped.

The model marked as the test tank replaces the mesh of DA_TestTank on this machine only. That asset is committed, so it
is also marked skip-worktree: git then ignores the local change and it cannot be committed by accident, and
setup_testmap.py leaves it alone. To undo the swap for good:
  git update-index --no-skip-worktree Content/Data/Units/DA_TestTank.uasset
  git checkout -- Content/Data/Units/DA_TestTank.uasset

  "<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<repo>/WorldDivide.uproject"
      -run=pythonscript -script="<repo>/Tools/Editor/import_local_art.py" -unattended -nosplash -nullrhi
"""

import os
import stat
import subprocess
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import placeholder_art as art  # noqa: E402

LOCAL_DIR = "/Game/Art/Local"
TEAM_MATERIAL = "/Game/Materials/M_PlaceholderTeam"
TEST_TANK = "/Game/Data/Units/DA_TestTank"
TEST_TANK_FILE = "Content/Data/Units/DA_TestTank.uasset"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Local")

# The placeholder_art functions read these at call time; pointing them here keeps every asset in the local folder.
art.MESH_DIR = f"{LOCAL_DIR}/Meshes"
art.MATERIAL_DIR = f"{LOCAL_DIR}/Materials"
art.TEXTURE_DIR = f"{LOCAL_DIR}/Textures"

# (name, source under Art/Source/Local, yaw turning it to face +X, team tint strength per glTF material)
MODELS = [
    ("Battlemaster", "EA/Battlemaster/battlemaster.glb", 0.0, {"Body": 0.15, "HouseColor": 0.9}),
]
# The model DA_TestTank uses on this machine, or None to leave it alone.
TEST_TANK_MODEL = "Battlemaster"

assets = unreal.EditorAssetLibrary


def save(asset):
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def import_model(name, source, yaw, strengths):
    parent = unreal.load_asset(TEAM_MATERIAL)
    slot_materials = {}
    for slot, (color, texture_file) in art.read_gltf_materials(source).items():
        texture = None
        if texture_file:
            texture = art.import_texture(texture_file, f"T_{name}_{slot}")
            # import_texture samples nearest for the Kenney palettes; these are painted textures.
            texture.set_editor_property("filter", unreal.TextureFilter.TF_DEFAULT)
            save(texture)
        instance = art.build_material_instance(f"MI_{name}_{slot}", parent, texture, color, strengths.get(slot, 0.5))
        save(instance)
        slot_materials[slot] = instance
    mesh = art.import_mesh(source, f"SM_{name}", yaw, slot_materials)
    save(mesh)
    return mesh


def use_for_test_tank(mesh):
    """Points DA_TestTank at mesh, scaled to the 260 x 150 footprint setup_testmap.py gives the committed tank, and
    hides the change from git."""
    subprocess.run(["git", "update-index", "--skip-worktree", TEST_TANK_FILE], cwd=PROJECT_DIR, check=True)
    # LFS checks lockable assets out read-only; this change stays local, so no lock is taken.
    file = os.path.join(PROJECT_DIR, TEST_TANK_FILE)
    os.chmod(file, os.stat(file).st_mode | stat.S_IWRITE)

    box = mesh.get_bounding_box()
    scale = min(260.0 / (box.max.x - box.min.x), 150.0 / (box.max.y - box.min.y))
    tank = unreal.load_asset(TEST_TANK)
    tank.set_editor_property("mesh", mesh)
    tank.set_editor_property("mesh_scale", unreal.Vector(scale, scale, scale))
    save(tank)
    unreal.log(f"import_local_art: DA_TestTank uses {mesh.get_name()} on this machine (scale {scale:.3f})")


for model_name, model_source, model_yaw, model_strengths in MODELS:
    model_path = os.path.join(SOURCE_DIR, model_source)
    if not os.path.exists(model_path):
        unreal.log_warning(f"import_local_art: {model_path} not found, skipping {model_name}")
        continue
    model_mesh = import_model(model_name, model_path, model_yaw, model_strengths)
    if model_name == TEST_TANK_MODEL:
        use_for_test_tank(model_mesh)
unreal.log("import_local_art: done")
