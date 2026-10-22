"""Imports local-only reference art (Art/Source/Local, made by Tools/Art/extract_big.py and w3d_to_glb_blender.py) into
/Game/Art/Local. Both folders are gitignored: this art comes from games installed on this machine and is not ours to
commit or ship. Nothing outside /Game/Art/Local is written, so the committed placeholder assets stay untouched.

For each model it builds SM_<name> with a team-tint material instance per glTF material (placeholder_art.py, pointed
at the local folders). Models whose source is missing are skipped.

The model marked as the test tank replaces the mesh of DA_TestTank on this machine only, and the Abrams (hull and
traversing turret, from Tools/Art/abrams_to_glb_blender.py) that of DA_TestAbrams. Those assets are committed, so they
are also marked skip-worktree: git then ignores the local change and it cannot be committed by accident, and
setup_testmap.py leaves it alone. To undo a swap for good:
  git update-index --no-skip-worktree Content/Data/Units/DA_TestTank.uasset
  git checkout -- Content/Data/Units/DA_TestTank.uasset

The World in Conflict Abrams cannon (Art/Source/Local/WiC/Abrams: us_tank_abrams_01a-03c.wav, a blast, a body and an
echo tail per take) becomes SC_TankCannon_Local and the FireSound of DA_TestHeavyCannon (the M20's gun) and
DA_TestCannon on this machine only (#356), marked skip-worktree the same way; undo it as above with
Content/Data/Weapons/DA_TestHeavyCannon.uasset and DA_TestCannon.uasset.

World in Conflict's tank smoke flipbook (Art/Source/Local/WiC/Smoke: T_WiCSmoke_RLT.png and _BBF.png, written from the
installed game by Tools/Art/wic_smoke_flipbook.py) draws the blast clouds and the veil of NS_MuzzleSmoke_Local, which
becomes the MuzzleSmoke of the same two guns on this machine only (#355). It is NS_MuzzleSmoke built again with
instances of M_MuzzleSmoke that point at those flipbooks; the dust keeps ours.

  "<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<repo>/WorldDivide.uproject"
      -run=pythonscript -script="<repo>/Tools/Editor/import_local_art.py" -unattended -nosplash -nullrhi
"""

import json
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
ABRAMS = "/Game/Data/Units/DA_TestAbrams"
ABRAMS_FILE = "Content/Data/Units/DA_TestAbrams.uasset"
ABRAMS_DIR = os.path.join("Crytek", "Abrams")
# The fit boxes setup_testmap.py gives DA_TestAbrams and DA_TestTank (length x width, cm).
ABRAMS_FOOTPRINT = (1080.0, 380.0)
TEST_TANK_FOOTPRINT = (1080.0, 370.0)
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Local")

# The placeholder_art functions read these at call time; pointing them here keeps every asset in the local folder.
art.MESH_DIR = f"{LOCAL_DIR}/Meshes"
art.MATERIAL_DIR = f"{LOCAL_DIR}/Materials"
art.TEXTURE_DIR = f"{LOCAL_DIR}/Textures"

# (name, source under Art/Source/Local, yaw turning it to face +X, team tint strength per glTF material)
MODELS = [
    ("Battlemaster", "EA/Battlemaster/battlemaster.glb", 0.0, {"Body": 0.15, "HouseColor": 0.9}),
    ("Abrams_Hull", "Crytek/Abrams/abrams_hull.glb", 0.0, {"Body": 0.15, "HouseColor": 0.9}),
]
# Parts that turn about their own pivot, so the importer keeps the model's origin instead of centring the footprint.
TURRETS = [
    ("Abrams_Turret", "Crytek/Abrams/abrams_turret.glb", 0.0, {"Body": 0.15, "HouseColor": 0.9}),
]
# The model DA_TestTank uses on this machine, or None to leave it alone.
TEST_TANK_MODEL = None
# The cannon sound's takes under Art/Source/Local, each a blast, a body and an echo tail, and the guns that fire it.
CANNON_SOUND_DIR = os.path.join("WiC", "Abrams")
CANNON_SOUND_TAKES = [f"us_tank_abrams_0{take}{layer}" for take in (1, 2, 3) for layer in "abc"]
CANNON_SOUND = f"{LOCAL_DIR}/Audio/SC_TankCannon_Local"
# World in Conflict's smoke flipbook under Art/Source/Local: 16 x 8 frames, and what the blast clouds and the veil play of
# it. Like SMOKE_LOOKS in setup_testmap.py, which they stand in for, over WiC's longer frame run.
SMOKE_SOURCE_DIR = os.path.join("WiC", "Smoke")
SMOKE_TEXTURES = {"SmokeRLT": "T_WiCSmoke_RLT", "SmokeBBF": "T_WiCSmoke_BBF"}
SMOKE_LOOKS = {
    "MI_WiCSmoke_Blast": {"FrameFirst": 0.0, "FrameLast": 80.0, "Erosion": 0.35, "Brightness": 0.2, "Columns": 16.0, "Rows": 8.0},
    "MI_WiCSmoke_Veil": {"FrameFirst": 36.0, "FrameLast": 127.0, "Erosion": 0.6, "Brightness": 0.2, "Columns": 16.0, "Rows": 8.0},
}
SMOKE_MATERIAL = "/Game/Materials/M_MuzzleSmoke"
DUST_LOOK = "/Game/Materials/MI_MuzzleSmoke_Dust"
MUZZLE_SMOKE = f"{LOCAL_DIR}/Effects/NS_MuzzleSmoke_Local"
CANNON_WEAPONS = [("/Game/Data/Weapons/DA_TestHeavyCannon", "Content/Data/Weapons/DA_TestHeavyCannon.uasset"),
                  ("/Game/Data/Weapons/DA_TestCannon", "Content/Data/Weapons/DA_TestCannon.uasset")]

assets = unreal.EditorAssetLibrary


def save(asset):
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def import_model(name, source, yaw, strengths, turret=False):
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
    if turret:
        mesh = art.import_static_mesh(source, f"SM_{name}", yaw, unreal.Vector(0.0, 0.0, 0.0))
        for index, slot in enumerate(mesh.static_materials):
            mesh.set_material(index, slot_materials[str(slot.material_slot_name)])
    else:
        mesh = art.import_mesh(source, f"SM_{name}", yaw, slot_materials)
    save(mesh)
    return mesh


def keep_local(file):
    """Hides the local change to a committed asset from git. An asset not committed yet is staged first, as setup made
    it, so the committed version is the one without local art."""
    tracked = subprocess.run(["git", "ls-files", "--error-unmatch", file], cwd=PROJECT_DIR, capture_output=True).returncode == 0
    if not tracked:
        subprocess.run(["git", "add", file], cwd=PROJECT_DIR, check=True)
        unreal.log_warning(f"import_local_art: staged {file} as setup made it, so the local swap stays out of git")
    subprocess.run(["git", "update-index", "--skip-worktree", file], cwd=PROJECT_DIR, check=True)
    # LFS checks lockable assets out read-only; this change stays local, so no lock is taken.
    path = os.path.join(PROJECT_DIR, file)
    os.chmod(path, os.stat(path).st_mode | stat.S_IWRITE)


def use_for_abrams(hull, turret):
    """Points DA_TestAbrams at the Abrams hull and turret, with the ring and muzzle the Blender script measured."""
    keep_local(ABRAMS_FILE)
    with open(os.path.join(SOURCE_DIR, ABRAMS_DIR, "abrams.json")) as file:
        layout = json.load(file)
    box = hull.get_bounding_box()
    scale = min(ABRAMS_FOOTPRINT[0] / (box.max.x - box.min.x), ABRAMS_FOOTPRINT[1] / (box.max.y - box.min.y))
    abrams = unreal.load_asset(ABRAMS)
    abrams.set_editor_property("mesh", hull)
    abrams.set_editor_property("mesh_scale", unreal.Vector(scale, scale, scale))
    abrams.set_editor_property("turret_mesh", turret)
    abrams.set_editor_property("turret_offset", unreal.Vector(*layout["turret_offset"]))
    abrams.set_editor_property("muzzle_offset", unreal.Vector(*layout["muzzle_offset"]))
    abrams.set_editor_property("turret_turn_rate", 90.0)
    save(abrams)
    unreal.log(f"import_local_art: DA_TestAbrams uses the local Abrams on this machine (scale {scale:.3f})")


def use_for_test_tank(mesh):
    """Points DA_TestTank at mesh, scaled to the fit box setup_testmap.py gives the committed tank, and
    hides the change from git."""
    keep_local(TEST_TANK_FILE)

    box = mesh.get_bounding_box()
    scale = min(TEST_TANK_FOOTPRINT[0] / (box.max.x - box.min.x), TEST_TANK_FOOTPRINT[1] / (box.max.y - box.min.y))
    tank = unreal.load_asset(TEST_TANK)
    tank.set_editor_property("mesh", mesh)
    tank.set_editor_property("mesh_scale", unreal.Vector(scale, scale, scale))
    save(tank)
    unreal.log(f"import_local_art: DA_TestTank uses {mesh.get_name()} on this machine (scale {scale:.3f})")


def import_wave(source, name):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = f"{LOCAL_DIR}/Audio"
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    wave = unreal.load_asset(f"{LOCAL_DIR}/Audio/{name}")
    if not isinstance(wave, unreal.SoundWave):
        raise RuntimeError(f"Could not import {source}")
    save(wave)
    return wave


def use_cannon_sound():
    """Builds the cannon's firing sound from the local takes and sets it on the tank guns, hidden from git."""
    waves = [import_wave(os.path.join(SOURCE_DIR, CANNON_SOUND_DIR, f"{take}.wav"), take) for take in CANNON_SOUND_TAKES]
    cue = unreal.RTSEditorScripting.build_layered_shot_sound(CANNON_SOUND, waves)
    if not cue:
        raise RuntimeError(f"Could not build {CANNON_SOUND}")
    save(cue)
    for weapon_path, weapon_file in CANNON_WEAPONS:
        keep_local(weapon_file)
        weapon = unreal.load_asset(weapon_path)
        weapon.set_editor_property("fire_sound", cue)
        save(weapon)
    unreal.log(f"import_local_art: the tank guns fire {cue.get_name()} on this machine")


def import_smoke_texture(source, name):
    """A six-way smoke flipbook as linear data in the effects group, as setup_testmap.py imports ours."""
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = f"{LOCAL_DIR}/Textures"
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(f"{LOCAL_DIR}/Textures/{name}")
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    save(texture)
    return texture


def build_smoke_look(name, parent, textures, scalars):
    path = f"{LOCAL_DIR}/Materials/{name}"
    editing = unreal.MaterialEditingLibrary
    if assets.does_asset_exist(path):
        instance = unreal.load_asset(path)
    else:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, f"{LOCAL_DIR}/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    for parameter, texture in textures.items():
        editing.set_material_instance_texture_parameter_value(instance, parameter, texture)
    for parameter, value in scalars.items():
        editing.set_material_instance_scalar_parameter_value(instance, parameter, value)
    editing.update_material_instance(instance)
    save(instance)
    return instance


def use_muzzle_smoke():
    """Builds the muzzle smoke with World in Conflict's flipbook and sets it on the tank guns, hidden from git."""
    textures = {parameter: import_smoke_texture(os.path.join(SOURCE_DIR, SMOKE_SOURCE_DIR, f"{name}.png"), name)
                for parameter, name in SMOKE_TEXTURES.items()}
    parent = unreal.load_asset(SMOKE_MATERIAL)
    looks = {name: build_smoke_look(name, parent, textures, scalars) for name, scalars in SMOKE_LOOKS.items()}
    if not unreal.RTSEditorScripting.load_editor_module("NiagaraEditor"):
        raise RuntimeError("Could not load the NiagaraEditor module")
    system = unreal.RTSEditorScripting.build_muzzle_smoke_system(
        MUZZLE_SMOKE, looks["MI_WiCSmoke_Blast"], looks["MI_WiCSmoke_Veil"], unreal.load_asset(DUST_LOOK))
    if not system:
        raise RuntimeError(f"Could not build {MUZZLE_SMOKE}; the log says why")
    save(system)
    for weapon_path, weapon_file in CANNON_WEAPONS:
        keep_local(weapon_file)
        weapon = unreal.load_asset(weapon_path)
        weapon.set_editor_property("muzzle_smoke", system)
        save(weapon)
    unreal.log(f"import_local_art: the tank guns leave {system.get_name()} on this machine")


imported = {}
for models, is_turret in ((MODELS, False), (TURRETS, True)):
    for model_name, model_source, model_yaw, model_strengths in models:
        model_path = os.path.join(SOURCE_DIR, model_source)
        if not os.path.exists(model_path):
            unreal.log_warning(f"import_local_art: {model_path} not found, skipping {model_name}")
            continue
        mesh_file = os.path.join(PROJECT_DIR, "Content", "Art", "Local", "Meshes", f"SM_{model_name}.uasset")
        if os.path.exists(mesh_file) and not os.access(mesh_file, os.W_OK):
            # Committed through LFS and not locked: it stays as committed.
            unreal.log(f"import_local_art: SM_{model_name} is read-only, keeping it as it is")
            imported[model_name] = unreal.load_asset(f"{LOCAL_DIR}/Meshes/SM_{model_name}")
            continue
        imported[model_name] = import_model(model_name, model_path, model_yaw, model_strengths, is_turret)
if TEST_TANK_MODEL in imported:
    use_for_test_tank(imported[TEST_TANK_MODEL])
if "Abrams_Hull" in imported and "Abrams_Turret" in imported:
    use_for_abrams(imported["Abrams_Hull"], imported["Abrams_Turret"])
if all(os.path.exists(os.path.join(SOURCE_DIR, CANNON_SOUND_DIR, f"{take}.wav")) for take in CANNON_SOUND_TAKES):
    use_cannon_sound()
else:
    unreal.log_warning(f"import_local_art: {os.path.join(SOURCE_DIR, CANNON_SOUND_DIR)} incomplete, skipping the cannon sound")
if all(os.path.exists(os.path.join(SOURCE_DIR, SMOKE_SOURCE_DIR, f"{name}.png")) for name in SMOKE_TEXTURES.values()):
    use_muzzle_smoke()
else:
    unreal.log_warning(f"import_local_art: {os.path.join(SOURCE_DIR, SMOKE_SOURCE_DIR)} incomplete, skipping the muzzle smoke")
unreal.log("import_local_art: done")
