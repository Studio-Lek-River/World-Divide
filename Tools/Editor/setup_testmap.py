"""Builds the placeholder weapon, unit, harvester, builder, upgrade, building (headquarters and superweapon included), AI build order and faction data assets, the placement ghost and shot effect materials, the vehicle guns' Niagara muzzle flash, the
placeholder models (imported from Art/Source/Placeholder by placeholder_art.py), the ground and water materials, the L_TestMap level
with a supply source and two builders by each base, the L_Skirmish and L_SkirmishDuel levels, the same ground without the
armies and with four and two start spots, the skirmish maps L_RiverFord, L_QuarryCross and L_SixPeaks with two, four and
six, all where the lobby's seats play, the L_LongPath level of the long-path tests, the L_ThinWalls level of the thin-wall tests, the map data assets that offer them in the lobby with a top-down preview of each
(rendered when the run has a renderer, drawn otherwise), and the
empty L_MainMenu and L_Lobby levels for the menu screens, a placeholder icon for every entity without painted art (see
placeholder_icons.py) and the Common UI styles of UI/ and Mods/*/UI/ (see make_ui_styles). L_TestMap and the skirmish maps are saved with their navmesh built. Safe to re-run: only assets that differ from this script are saved, so a run without script changes
writes nothing. Data assets are compared property by property; materials, models and the map carry a hash of the code and
source files that build them and are rebuilt only when those change. UI assets built in an earlier run whose source entry
is gone are deleted.

The assets are LFS lockable and therefore read-only unless locked. If an asset that must change is read-only, nothing
is written: the files are listed in Saved/Setup/ReadOnlyAssets.txt and the run stops. Lock them (git lfs lock) and rerun.
Assets marked skip-worktree, such as DA_TestTank swapped for local art by import_local_art.py, are left as they are.

Run from the repo root (adjust the engine path), or use Tools/Scripts/ue.ps1 -Task setup:
  "D:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/WorldDivide.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/setup_testmap.py" -unattended -nosplash -nullrhi
To render the map previews instead of drawing them, replace -nullrhi with -AllowCommandletRendering (ue.ps1 -Task setup
-Render).
"""

import collections
import hashlib
import inspect
import json
import math
import os
import subprocess
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bake_map  # noqa: E402
import map_preview  # noqa: E402
import placeholder_art as art  # noqa: E402
import placeholder_icons  # noqa: E402
import ui_sources  # noqa: E402
import vehicle_art  # noqa: E402

UNIT_DIR = "/Game/Data/Units"
WEAPON_DIR = "/Game/Data/Weapons"
BUILDING_DIR = "/Game/Data/Buildings"
UPGRADE_DIR = "/Game/Data/Upgrades"
FACTION_DIR = "/Game/Data/Factions"
AI_DIR = "/Game/Data/AI"
MAP_DATA_DIR = "/Game/Data/Maps"
UI_STYLE_DIR = "/Game/UI/Styles"
# A theme font size is pixels of the em at the 1080p reference; Slate reads points at 96 DPI (RTSLayout::FontPointsPerPixel).
FONT_POINTS_PER_PIXEL = 0.75
MATERIAL_DIR = "/Game/Materials"
EFFECT_DIR = "/Game/Effects"
MAP_PATH = "/Game/Maps/L_TestMap"
# L_TestMap by moonlight (#223), to try the vehicle lights and other night looks by hand: the same ground, bases and units.
NIGHT_MAP_PATH = "/Game/Maps/L_TestMapNight"
# rts.Debug.Bench's level: 300 x 300 m, team 0's headquarters at (-BENCH_CORNER, -BENCH_CORNER), team 1's opposite.
BENCH_MAP_PATH = "/Game/Maps/L_Bench"
BENCH_HALF_SIZE = 15000.0
BENCH_CORNER = 12000.0
MAIN_MENU_MAP_PATH = "/Game/Maps/L_MainMenu"
LOBBY_MAP_PATH = "/Game/Maps/L_Lobby"
CUBE = "/Engine/BasicShapes/Cube.Cube"
TEAM_MATERIAL = "M_PlaceholderTeam"
# How strongly units and buildings take on their team colour; scenery keeps its own colours.
TEAM_TINT = 0.7

# Package metadata key holding the hash of the code that built the asset.
HASH_TAG = "SetupHash"
# The navmesh a map is saved with is serialized for the engine it was built in, so the maps are rebuilt when the engine
# moves to another major or minor version.
ENGINE_VERSION = ".".join(unreal.SystemLibrary.get_engine_version().split(".")[:2])


def read_nav_config():
    """The navigation settings of Config/DefaultEngine.ini, its agent classes among them, without comments: the maps carry
    navmeshes built with them, so they are rebuilt when these change."""
    settings = []
    keep = False
    with open(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_config_dir()), "DefaultEngine.ini"),
              encoding="utf-8") as ini:
        for line in ini:
            line = line.strip()
            if line.startswith("["):
                keep = line.startswith("[/Script/NavigationSystem.")
            elif keep and line and not line.startswith(";"):
                settings.append(line)
    return tuple(settings)


NAV_CONFIG = read_nav_config()

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
CONTENT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
READ_ONLY_REPORT = os.path.join(PROJECT_DIR, "Saved", "Setup", "ReadOnlyAssets.txt")
ART_SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Placeholder")
# The M20 Guardian as Tools/Art/m20_guardian_blender.py exported it, and where its assets go.
M20_SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "USA", "vehicles", "m20_guardian")
M20_DIR = "/Game/Art/USA/Vehicles/M20Guardian"

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

# Assets that need saving: (package path, file extension, already on disk, function that writes it).
pending = []

# Package paths of the assets planned by plan_now and plan_data_asset this run, changed or not.
planned = set()

# Package paths of the generated UI assets queued for deletion by plan_stale_ui_assets.
stale = set()

# Hashes of the surface materials by package path.
material_hashes = {}

# Data assets by package path: (asset, already on disk, desired properties). Compared and queued by plan_data_assets once
# all are planned, so two of them can reference each other.
data_assets = {}


def object_path(obj):
    return obj.get_path_name() if obj else None


def differs(current, desired):
    """True when a property value read from an asset is not the desired value. Float properties are 32 bit, so floats
    are compared with a tolerance."""
    if isinstance(desired, float):
        return not math.isclose(current, desired, rel_tol=1e-6, abs_tol=1e-6)
    if isinstance(desired, unreal.Text):
        return str(current) != str(desired)
    if isinstance(desired, unreal.Vector):
        return any(differs(getattr(current, axis), getattr(desired, axis)) for axis in "xyz")
    if isinstance(desired, unreal.IntPoint):
        return (current.x, current.y) != (desired.x, desired.y)
    if isinstance(desired, list):
        return len(current) != len(desired) or any(differs(c, d) for c, d in zip(current, desired))
    if isinstance(desired, dict):
        return len(current) != len(desired) or any(key not in current or differs(current[key], value)
                                                   for key, value in desired.items())
    if isinstance(desired, unreal.StructBase):
        return current.export_text() != desired.export_text()
    if desired is None or isinstance(desired, unreal.Object):
        return object_path(current) != object_path(desired)
    return current != desired


def spec_hash(functions, *values):
    """Hash of the source of the functions that build an asset plus the values they use."""
    digest = hashlib.sha1()
    for function in functions:
        digest.update(inspect.getsource(function).encode("utf-8"))
    for value in values:
        digest.update(repr(object_path(value) if isinstance(value, unreal.Object) else value).encode("utf-8"))
    return digest.hexdigest()


def load_data_asset(name, directory, data_class):
    """Loads or creates a data asset; returns it and whether it is already on disk."""
    path = f"{directory}/{name}"
    planned.add(path)
    if assets.does_asset_exist(path):
        return assets.load_asset(path), True
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", data_class)
    return asset_tools.create_asset(name, directory, data_class, factory), False


def plan_data_asset(name, directory, data_class, properties):
    """Loads or creates a data asset and records the properties it should have; plan_data_assets queues the save."""
    asset, exists = load_data_asset(name, directory, data_class)
    data_assets[f"{directory}/{name}"] = (asset, exists, dict(properties))
    return asset


def plan_data_assets():
    """Queues a save for every planned data asset that is new or has a property that differs."""
    for path, (asset, exists, desired) in data_assets.items():
        changed = {key: value for key, value in desired.items() if differs(asset.get_editor_property(key), value)}
        if changed or not exists:
            def write(asset=asset, path=path, changed=changed):
                for key, value in changed.items():
                    asset.set_editor_property(key, value)
                if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
                    raise RuntimeError(f"Could not save {path}")
            pending.append((path, ".uasset", exists, write))


def plan_rebuild(path, extension, build, digest, stale=None):
    """Queues build(digest) unless the asset exists and was built from code with the same hash, and stale(asset), when
    given, does not ask for a rebuild anyway."""
    exists = assets.does_asset_exist(path)
    if exists:
        asset = assets.load_asset(path)
        if assets.get_metadata_tag(asset, HASH_TAG) == digest and not (stale and stale(asset)):
            return
    pending.append((path, extension, exists, lambda: build(digest)))


def plan_now(path, build, digest):
    """Builds an asset in memory right away, so assets planned after it can use it, and queues its save. Returns the
    asset on disk instead when it was built from the same hash."""
    planned.add(path)
    exists = assets.does_asset_exist(path)
    if exists:
        asset = assets.load_asset(path)
        if assets.get_metadata_tag(asset, HASH_TAG) == digest:
            return asset
    asset = build()
    assets.set_metadata_tag(asset, HASH_TAG, digest)

    def write():
        if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {path}")
    pending.append((path, ".uasset", exists, write))
    return asset


def new_level(path):
    """Replaces the level at path with a new, empty one. Unreal 5.8 deletes the world from memory but leaves its file and
    its asset registry entry behind when run as a commandlet, which makes new_level refuse the path: remove the file and
    rescan it, so the registry drops it too."""
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
        file = os.path.join(CONTENT_DIR, path[len("/Game/"):] + ".umap")
        if os.path.exists(file):
            os.remove(file)
            unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([file], force_rescan=True)
    if not levels.new_level(path):
        raise RuntimeError(f"Could not create {path}")


def file_hash(path):
    with open(path, "rb") as file:
        return hashlib.sha1(file.read()).hexdigest()


def make_team_material():
    path = f"{MATERIAL_DIR}/{TEAM_MATERIAL}"

    def build():
        material = assets.load_asset(path) if assets.does_asset_exist(path) else asset_tools.create_asset(
            TEAM_MATERIAL, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
        return art.build_team_material(material)
    return plan_now(path, build, spec_hash([art.build_team_material], path, art.TEAM_COLOR_INDEX))


def make_texture(source):
    """A pack's texture, named after the pack folder: Kenney/CarKit/Textures/colormap.png becomes T_CarKit_colormap."""
    name = f"T_{os.path.basename(os.path.dirname(os.path.dirname(source)))}_{os.path.splitext(os.path.basename(source))[0]}"
    return plan_now(f"{art.TEXTURE_DIR}/{name}", lambda: art.import_texture(source, name),
                    spec_hash([art.import_texture], name, file_hash(source)))


def make_art(name, source, yaw=0.0, tint=TEAM_TINT):
    """Imports Art/Source/Placeholder/<source> as SM_<name>, turned by yaw so it faces +X, with a material instance
    MI_<name>_<slot> per glTF material. tint is the team tint strength, or {slot: strength} with TEAM_TINT for the
    slots it leaves out."""
    source = os.path.join(ART_SOURCE_DIR, source)
    parent = make_team_material()
    slot_materials = {}
    for slot, (color, texture_file) in art.read_gltf_materials(source).items():
        texture = make_texture(texture_file) if texture_file else None
        strength = tint.get(slot, TEAM_TINT) if isinstance(tint, dict) else tint
        instance_name = f"MI_{name}_{slot}".replace("-", "_").replace(".", "_")
        slot_materials[slot] = plan_now(
            f"{art.MATERIAL_DIR}/{instance_name}",
            lambda n=instance_name, t=texture, c=color, s=strength: art.build_material_instance(n, parent, t, c, s),
            spec_hash([art.build_material_instance], instance_name, parent, texture, color, strength))
    mesh_name = f"SM_{name}"
    return plan_now(f"{art.MESH_DIR}/{mesh_name}", lambda: art.import_mesh(source, mesh_name, yaw, slot_materials),
                    spec_hash([art.import_mesh, art.import_static_mesh], mesh_name, file_hash(source), yaw,
                              sorted((slot, object_path(instance)) for slot, instance in slot_materials.items())))


def make_m20_guardian(exhaust_system):
    """Imports the M20 Guardian's parts, textures and material instances (see vehicle_art.py) and returns its unit data
    properties: the hull as Mesh at real size, the turret, the gun and every other part where m20_guardian.json puts it,
    the exhaust (exhaust_system) at its grille and the lights at its lamps. The wheels of the right side are the exported
    left wheel turned round."""
    with open(os.path.join(M20_SOURCE_DIR, "m20_guardian.json")) as file:
        layout = json.load(file)
    texture_dir, material_dir, mesh_dir = (f"{M20_DIR}/{folder}" for folder in ("Textures", "Materials", "Meshes"))

    textures = {}
    for texture_set in ("Hull", "Turret", "Running", "Track"):
        for kind in vehicle_art.TEXTURE_KINDS:
            name = f"T_M20_{texture_set}_{kind}"
            source = os.path.join(M20_SOURCE_DIR, "textures", f"{name}.png")
            textures[texture_set, kind] = plan_now(
                f"{texture_dir}/{name}", lambda s=source, n=name, k=kind: vehicle_art.import_texture(s, texture_dir, n, k),
                spec_hash([vehicle_art.import_texture], texture_dir, name, kind, file_hash(source)))

    hull_textures = {kind: textures["Hull", kind] for kind in vehicle_art.TEXTURE_KINDS}
    master = plan_now(f"{vehicle_art.MATERIAL_DIR}/{vehicle_art.VEHICLE_MATERIAL}",
                      lambda: vehicle_art.build_vehicle_material(hull_textures),
                      spec_hash([vehicle_art.build_vehicle_material, vehicle_art.build_track_animation, vehicle_art._mask],
                                sorted(map(object_path, hull_textures.values())), vehicle_art.TEAM_COLOR_INDEX,
                                vehicle_art.TRACK_PHASE_HLSL, vehicle_art.TRACK_SHIFT_HLSL, vehicle_art.TRACK_TURN_HLSL,
                                vehicle_art.TRACK_INPUTS))
    lamp = plan_now(f"{vehicle_art.MATERIAL_DIR}/{vehicle_art.LAMP_MATERIAL}", vehicle_art.build_lamp_material,
                    spec_hash([vehicle_art.build_lamp_material, vehicle_art._scalar], vehicle_art.LAMP_STRENGTH,
                              vehicle_art.LAMP_NIGHT_STRENGTH, vehicle_art.LAMP_ON_INDEX))
    slot_materials = {"M20_Emissive": lamp}
    # The tracks' instance moves their links (#151); MI_M20_Track_Static is the same without, for a track that does not
    # animate (rts.Vehicle.TrackAnimation 0) and for its far LODs (#149).
    instances = {}
    for texture_set, name, animated in (("Hull", "MI_M20_Hull", False), ("Turret", "MI_M20_Turret", False),
                                        ("Running", "MI_M20_Running", False), ("Track", "MI_M20_Track", True),
                                        ("Track", "MI_M20_Track_Static", False)):
        set_textures = {kind: textures[texture_set, kind] for kind in vehicle_art.TEXTURE_KINDS}
        instances[name] = plan_now(
            f"{material_dir}/{name}",
            lambda n=name, t=set_textures, a=animated: vehicle_art.build_material_instance(material_dir, n, master, t, a),
            spec_hash([vehicle_art.build_material_instance], material_dir, name, master,
                      sorted(map(object_path, set_textures.values())), animated))
    for texture_set in ("Hull", "Turret", "Running", "Track"):
        slot_materials[f"M20_{texture_set}"] = instances[f"MI_M20_{texture_set}"]

    # Each part's length along X as exported (m20_guardian_blender.py --verify), to catch a unit mix-up on import, and
    # its LOD1-3 triangles (#149), from about 29k, 8k and 2k per tank. The tracks' LOD2 and LOD3 are the belts of
    # m20_guardian_blender.py --lods, without the link shift; the wheels are instanced, so they switch by their box.
    meshes = {}
    for part, length, triangles in (("Hull", 710.2, (10000, 2600, 560)), ("Turret", 424.2, (7000, 1600, 380)),
                                    ("Gun", 65.5, (800, 180, 40)), ("Barrel", 290.0, (800, 170, 40)), ("RWS", 165.0, (700, 120, 40)),
                                    ("Track_L", 647.3, (2400,)), ("Track_R", 647.3, (2400,)),
                                    ("RoadWheel", 68.0, (300, 120, 20)), ("Sprocket", 68.4, (300, 120, 20)),
                                    ("Idler", 54.0, (300, 120, 20))):
        name = f"SM_M20_{part}"
        source = os.path.join(M20_SOURCE_DIR, "fbx", f"{name}.fbx")
        instanced = part in ("RoadWheel", "Sprocket", "Idler")
        custom = {lod: os.path.join(M20_SOURCE_DIR, "fbx", "lods", f"{name}_LOD{lod}.fbx")
                  for lod in (2, 3)} if part.startswith("Track") else None
        custom_slot = ("M20_Track_Static", instances["MI_M20_Track_Static"]) if custom else None
        # The gun's muzzle, where its shots leave (#217): the tip of the bore the Blender script measured.
        sockets = {"Muzzle": tuple(layout["gun_muzzle_offset"])} if part == "Gun" else {}

        def build(s=source, n=name, l=length, t=triangles, i=instanced, c=custom, cs=custom_slot, so=sockets):
            mesh = vehicle_art.build_lods(vehicle_art.import_part(s, mesh_dir, n, slot_materials, l), t, i, c, cs)
            return vehicle_art.set_sockets(mesh, so)

        # The sockets join the hash only of a part that has them, so adding them rebuilt the gun alone.
        socket_spec = ([vehicle_art.set_sockets], [sorted(sockets.items())]) if sockets else ([], [])
        meshes[part] = plan_now(
            f"{mesh_dir}/{name}", build,
            spec_hash([vehicle_art.import_part, vehicle_art.build_lods, vehicle_art.lod_radius,
                       vehicle_art.lod_screen_sizes] + socket_spec[0], mesh_dir, name, length, file_hash(source),
                      sorted((slot, object_path(material)) for slot, material in slot_materials.items()),
                      triangles, instanced, vehicle_art.LOD_DISTANCES, vehicle_art.SCREEN_MULTIPLE,
                      sorted((lod, file_hash(path)) for lod, path in (custom or {}).items()),
                      custom_slot and (custom_slot[0], object_path(custom_slot[1])),
                      *socket_spec[1]))

    def part(mesh, mount, offset, yaw=0.0, wheel=False, roll_radius=0.0, track_pitch=0.0, rigid_material=None,
             recoils=False):
        value = unreal.RTSVehiclePart()
        value.set_editor_property("mesh", mesh)
        value.set_editor_property("mounted_on", mount)
        value.set_editor_property("offset", unreal.Vector(*offset))
        value.set_editor_property("rotation", unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
        value.set_editor_property("wheel", wheel)
        value.set_editor_property("roll_radius", roll_radius)
        value.set_editor_property("track_pitch", track_pitch)
        value.set_editor_property("rigid_material", rigid_material)
        value.set_editor_property("recoils", recoils)
        return value

    # The barrel shares the gun's pivot and slides back through the mantlet when it fires (#218).
    parts = [part(meshes["Barrel"], unreal.RTSVehiclePartMount.GUN, (0.0, 0.0, 0.0), recoils=True)]
    # The tracks move their links by their pitch, and the wheels roll by where they meet the track (#151).
    parts.append(part(meshes["RWS"], unreal.RTSVehiclePartMount.TURRET, layout["rws_offset"]))
    parts += [part(meshes[name[len("M20_"):]], unreal.RTSVehiclePartMount.HULL, offset, track_pitch=layout["track_pitch"],
                   rigid_material=instances["MI_M20_Track_Static"])
              for name, offset in sorted(layout["track_offsets"].items())]
    for name, offset in sorted(layout["wheels"].items()):
        kind = next(kind for kind in ("RoadWheel", "Sprocket", "Idler") if name.startswith(f"M20_{kind}"))
        right = name.rstrip("0123456789").endswith("R")
        parts.append(part(meshes[kind], unreal.RTSVehiclePartMount.HULL, offset, 180.0 if right else 0.0, True,
                          roll_radius=layout["roll_radii"][kind]))

    return meshes["Hull"], {
        "turret_mesh": meshes["Turret"],
        "turret_offset": unreal.Vector(*layout["turret_offset"]),
        "muzzle_offset": unreal.Vector(*layout["muzzle_offset"]),
        "gun_mesh": meshes["Gun"],
        "gun_offset": unreal.Vector(*layout["gun_pivot_offset"]),
        "gun_muzzle_offset": unreal.Vector(*layout["gun_muzzle_offset"]),
        # A 105 mm gun's stroke and run-out, its jump on the trunnions and the hull's rock on its suspension (#218).
        "gun_recoil": gun_recoil(30.0, 0.015, 0.03, 0.7, jump_degrees=1.0, jump_seconds=0.3, hull_rock_degrees=0.6,
                                 hull_rock_seconds=0.75),
        # Its hull on its torsion bars with the drive (#297): about a degree back pulling away at 10 m/s² and leaning out of
        # its tightest turn at 4 m/s², at most 1.2 degrees diving under its 25 m/s² brakes; the turret trails a 45°/s
        # traverse by 1.5 degrees.
        "body_motion": body_motion(0.1, 0.25, 1.2, turret_lag_degrees=1.5),
        "parts": parts,
        # Out of the rear grille, along its louvres (#222), from a diesel (#325).
        "exhaust": vehicle_exhaust(exhaust_system, layout["exhaust_offsets"], layout["exhaust_direction"], DIESEL_EXHAUST),
        # Its four headlights in the nose clusters and its two red tail lamps on the rear sponsons (#223).
        "lights": vehicle_lights(layout["headlight_offsets"], layout["taillight_offsets"]),
    }


def gun_recoil(distance, recoil_seconds, hold_seconds, return_seconds, jump_degrees=0.0, jump_seconds=0.4,
               jump_damping=0.45, hull_rock_degrees=0.0, hull_rock_seconds=1.0, hull_rock_damping=0.6):
    """A vehicle's reaction to its gun firing (FRTSGunRecoil, #218): the barrel's stroke in cm and its seconds back, held
    and out again; the gun's jump and the hull's rock in degrees, the seconds each takes to settle and the damping ratio
    it settles with (#219)."""
    value = unreal.RTSGunRecoil()
    for name, setting in (("distance", distance), ("recoil_seconds", recoil_seconds), ("hold_seconds", hold_seconds),
                          ("return_seconds", return_seconds), ("jump_degrees", jump_degrees),
                          ("jump_seconds", jump_seconds), ("jump_damping", jump_damping),
                          ("hull_rock_degrees", hull_rock_degrees), ("hull_rock_seconds", hull_rock_seconds),
                          ("hull_rock_damping", hull_rock_damping)):
        value.set_editor_property(name, setting)
    return value


def body_motion(squat_per_acceleration, lean_per_acceleration, max_degrees, settle_seconds=0.8, damping=0.5,
                turret_lag_degrees=0.0, turret_lag_seconds=0.35, turret_lag_damping=0.6):
    """How a vehicle's body moves with its drive (FRTSBodyMotion, #297): degrees of pitch and roll per m/s² of the drive's
    acceleration and of its turn's, the most either may reach, the seconds the hull takes to settle and its damping ratio;
    the most degrees the drawn turret trails its traverse, the seconds it takes to catch up and its damping ratio."""
    value = unreal.RTSBodyMotion()
    for name, setting in (("squat_per_acceleration", squat_per_acceleration),
                          ("lean_per_acceleration", lean_per_acceleration), ("max_degrees", max_degrees),
                          ("settle_seconds", settle_seconds), ("damping", damping),
                          ("turret_lag_degrees", turret_lag_degrees), ("turret_lag_seconds", turret_lag_seconds),
                          ("turret_lag_damping", turret_lag_damping)):
        value.set_editor_property(name, setting)
    return value


def fit_scale(mesh, length, width, height=None):
    """MeshScale fitting the mesh inside length x width (along x and y) with its proportions kept, or stretched to height
    when one is given."""
    box = mesh.get_bounding_box()
    scale = min(length / (box.max.x - box.min.x), width / (box.max.y - box.min.y))
    return (scale, scale, height / (box.max.z - box.min.z) if height else scale)


def fit_height(mesh, height):
    """Uniform MeshScale giving the mesh height (WORLD_SCALE_STANDARD.md rule 2: units are never stretched)."""
    box = mesh.get_bounding_box()
    scale = height / (box.max.z - box.min.z)
    return (scale, scale, scale)


def interaction_radius(mesh, scale):
    """A unit's SelectionRadius, its interaction radius (docking, build reach, holding short): half the longer side of the
    scaled mesh, so a unit that stops that far from a building's edge clears it whichever way it faces."""
    box = mesh.get_bounding_box()
    return round(max((box.max.x - box.min.x) * scale[0], (box.max.y - box.min.y) * scale[1]) / 2.0, 1)


def make_weapon(name, weapon_range, damage, reload_seconds, shell_speed, effect_scale, shell_color, arc_height=0.0,
                ground=True, air=False, muzzle_flash=None, muzzle_smoke=None, splash_radius=0.0, muzzle_brake=False):
    """muzzle_flash: the Niagara system a vehicle gun flashes (make_muzzle_flash); None keeps the placeholder glow.
    muzzle_brake: the gun has a muzzle brake, so its flash throws fire sideways too (#294).
    muzzle_smoke: the Niagara system a vehicle cannon leaves at the muzzle after a shot (make_muzzle_smoke); None leaves none.
    splash_radius: the blast in cm of a shot at a ground location (force fire); 0 for none."""
    return plan_data_asset(name, WEAPON_DIR, unreal.RTSWeaponData, {
        "muzzle_flash": muzzle_flash,
        "muzzle_smoke": muzzle_smoke,
        "muzzle_brake": muzzle_brake,
        "range": weapon_range,
        "damage": damage,
        "reload_seconds": reload_seconds,
        "shell_speed": shell_speed,
        "splash_radius": splash_radius,
        "can_target_ground": ground,
        "can_target_air": air,
        "shell_arc_height": arc_height,
        "effect_scale": effect_scale,
        "shell_color": unreal.LinearColor(*shell_color, 1.0),
    })


def make_unit(name, display_name, mesh, scale, health, weapon, speed, turn_rate, cost, build_time):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSUnitData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": interaction_radius(mesh, scale),
        "max_health": health,
        "weapon": weapon,
        "move_speed": speed,
        "turn_rate": turn_rate,
        "cost": cost,
        "build_time": build_time,
    })


def add_properties(asset, properties):
    """Plans more properties for a data asset planned earlier, for one that references something planned after it."""
    data_assets[asset.get_path_name().split(".")[0]][2].update(properties)


def make_harvester(name, display_name, mesh, scale, health, speed, turn_rate, cost, build_time,
                   carry_amount, gather_seconds, search_radius):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSHarvesterData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": interaction_radius(mesh, scale),
        "max_health": health,
        "weapon": None,
        "move_speed": speed,
        "turn_rate": turn_rate,
        "cost": cost,
        "build_time": build_time,
        "carry_amount": carry_amount,
        "gather_seconds": gather_seconds,
        "search_radius": search_radius,
    })


def make_builder(name, display_name, mesh, scale, health, speed, turn_rate, cost, build_time, buildings):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSBuilderData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": interaction_radius(mesh, scale),
        "max_health": health,
        "weapon": None,
        "move_speed": speed,
        "turn_rate": turn_rate,
        "cost": cost,
        "build_time": build_time,
        "buildings": buildings,
    })


def make_aircraft(name, display_name, mesh, scale, health, weapon, speed, turn_rate, cost, build_time,
                  cruise_altitude, climb_rate, max_bank_angle, ammo_per_sortie, rearm_seconds):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSAircraftData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": interaction_radius(mesh, scale),
        "max_health": health,
        "weapon": weapon,
        "move_speed": speed,
        "turn_rate": turn_rate,
        "cost": cost,
        "build_time": build_time,
        "cruise_altitude": cruise_altitude,
        "climb_rate": climb_rate,
        "max_bank_angle": max_bank_angle,
        "ammo_per_sortie": ammo_per_sortie,
        "rearm_seconds": rearm_seconds,
    })


def make_building(name, display_name, mesh, scale, footprint, health, weapon, cost, build_time, power_output, power_draw,
                  spawn_on_complete=None, accepts_supply=False, requires_power=False, units=(), upgrades=(), defense=False,
                  max_count=0, airfield_pads=0, superweapon=None):
    return plan_data_asset(name, BUILDING_DIR, unreal.RTSBuildingData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "footprint": unreal.IntPoint(*footprint),
        "selection_radius": max(footprint) * 50.0,
        "max_health": health,
        "weapon": weapon,
        "cost": cost,
        "build_time": build_time,
        "power_output": power_output,
        "power_draw": power_draw,
        "requires_power": requires_power,
        "spawn_on_complete": spawn_on_complete,
        "accepts_supply": accepts_supply,
        "units": list(units),
        "upgrades": list(upgrades),
        "is_defense": defense,
        "max_count": max_count,
        "airfield_pads": airfield_pads,
        # (charge time, delay, radius, damage, edge damage), or None for a building that is not a superweapon.
        "superweapon_charge_time": superweapon[0] if superweapon else 0.0,
        "superweapon_delay": superweapon[1] if superweapon else 3.0,
        "superweapon_radius": superweapon[2] if superweapon else 4000.0,
        "superweapon_damage": superweapon[3] if superweapon else 1000.0,
        "superweapon_edge_damage": superweapon[4] if superweapon else 0.25,
    })


def make_upgrade(name, display_name, cost, build_time, stat, multiplier, targets, prerequisites=()):
    return plan_data_asset(name, UPGRADE_DIR, unreal.RTSUpgradeData, {
        "display_name": unreal.Text(display_name),
        "cost": cost,
        "build_time": build_time,
        "prerequisites": list(prerequisites),
        "stat": stat,
        "multiplier": multiplier,
        "targets": list(targets),
    })


def make_faction(name, display_name, buildings, headquarters=None, uses_power=True, ai_build_order=None, ui_style=None):
    """ui_style: the set name of the faction's DA_UIStyle_<Set>, committed and kept up to date by make_ui_styles."""
    style = assets.load_asset(f"{UI_STYLE_DIR}/DA_UIStyle_{ui_style}") if ui_style else None
    return plan_data_asset(name, FACTION_DIR, unreal.RTSFactionData, {
        "display_name": unreal.Text(display_name),
        "ui_style": style,
        "buildings": buildings,
        "headquarters": headquarters,
        "uses_power": uses_power,
        "ai_build_order": ai_build_order,
    })


def struct_from_text(struct_class, text):
    """A struct from its exported text, for structs such as GameplayTag and Key whose fields Python cannot set."""
    struct = struct_class()
    struct.import_text(text)
    return struct


def gameplay_tag(name):
    return struct_from_text(unreal.GameplayTag, f'(TagName="{name}")')


def present(entity, faction_tag, icon, hotkey, description):
    """What the UI shows of an entity: its icon sprite (resolved by its faction's style), hotkey and description."""
    add_properties(entity, {
        "faction_tag": faction_tag,
        "icon_sprite": unreal.Name(icon),
        "hotkey": struct_from_text(unreal.Key, hotkey),
        "description": unreal.Text(description),
    })


def make_struct(struct_class, properties):
    struct = struct_class()
    for key, value in properties.items():
        struct.set_editor_property(key, value)
    return struct


def make_build_order(name, steps, army_units, interval, size, min_size, difficulty_scales):
    """steps: (entry, count) pairs in order; difficulty_scales: {RTSAIDifficulty: (interval scale, size scale)}."""
    return plan_data_asset(name, AI_DIR, unreal.RTSAIBuildOrderData, {
        "steps": [make_struct(unreal.RTSBuildOrderStep, {"entry": entry, "count": count}) for entry, count in steps],
        "army_units": army_units,
        "waves": make_struct(unreal.RTSAIWaveSettings, {"interval": interval, "size": size, "min_size": min_size}),
        "difficulty_scales": {difficulty: make_struct(unreal.RTSAIWaveScale, {"interval_scale": interval_scale,
                                                                              "size_scale": size_scale})
                              for difficulty, (interval_scale, size_scale) in difficulty_scales.items()},
    })


def build_ghost_material(name, digest):
    """Unlit translucent material tinted through a "Color" parameter, for the building placement ghost."""
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
    else:
        material = asset_tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())

    editing = unreal.MaterialEditingLibrary
    editing.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.1, 1.0, 0.2, 1.0))
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 200)
    opacity.set_editor_property("r", 0.4)
    editing.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    editing.recompile_material(material)
    assets.set_metadata_tag(material, HASH_TAG, digest)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")


def make_ghost_material(name):
    plan_rebuild(f"{MATERIAL_DIR}/{name}", ".uasset", lambda digest: build_ghost_material(name, digest),
                 spec_hash([build_ghost_material], name))


# A shot's glow: custom primitive data from this index on, RGBA (RTSPrimitiveData::ShotGlow, #227).
SHOT_GLOW_INDEX = 0


def build_shot_material(name, digest):
    """Unlit additive material glowing in its "Color" parameter, for muzzle flashes, shells and impacts. Additive, so a
    colour faded to black is gone. The colour is custom primitive data (RTSPrimitiveData::ShotGlow, #227), so every glow
    shares this material instead of a dynamic instance of its own."""
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
    else:
        material = asset_tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())

    editing = unreal.MaterialEditingLibrary
    editing.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(8.0, 4.0, 1.0, 1.0))
    color.set_editor_property("use_custom_primitive_data", True)
    color.set_editor_property("primitive_data_index", SHOT_GLOW_INDEX)
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    editing.recompile_material(material)
    assets.set_metadata_tag(material, HASH_TAG, digest)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")


def make_shot_material(name):
    plan_rebuild(f"{MATERIAL_DIR}/{name}", ".uasset", lambda digest: build_shot_material(name, digest),
                 spec_hash([build_shot_material], name, SHOT_GLOW_INDEX))


# The firing flipbooks Tools/Art/muzzle_fx_blender.py bakes from its fire and smoke simulation (#294): 8 x 8 frames each.
EFFECT_SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Effects")
EFFECT_TEXTURE_DIR = f"{EFFECT_DIR}/Textures"
FLIPBOOK_LAST_FRAME = 63.0


def import_effect_texture(source, name):
    """A flipbook as linear data (heat, six-way light) with its alpha, in the effects texture group."""
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = EFFECT_TEXTURE_DIR
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{EFFECT_TEXTURE_DIR}/{name}")
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    return texture


def make_effect_texture(name):
    source = os.path.join(EFFECT_SOURCE_DIR, f"{name}.png")
    return plan_now(f"{EFFECT_TEXTURE_DIR}/{name}", lambda: import_effect_texture(source, name),
                    spec_hash([import_effect_texture], name, file_hash(source)))


def new_material(name):
    """The material name, emptied, to build again."""
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
    else:
        material = asset_tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    return material


def expression(material, kind, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, kind, x, y)


def connect(source, source_pin, target, target_pin):
    unreal.MaterialEditingLibrary.connect_material_expressions(source, source_pin, target, target_pin)


def custom_node(material, x, y, code, output_type, inputs, outputs=()):
    """A Custom expression with the named inputs and, past its return value, the named extra float3 outputs."""
    node = expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    custom_inputs = []
    for input_name in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        custom_inputs.append(custom_input)
    node.set_editor_property("inputs", custom_inputs)
    custom_outputs = []
    for output_name in outputs:
        custom_output = unreal.CustomOutput()
        custom_output.set_editor_property("output_name", output_name)
        custom_output.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        custom_outputs.append(custom_output)
    node.set_editor_property("additional_outputs", custom_outputs)
    return node


def scalar_parameter(material, x, y, name, value):
    node = expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


# The frame of an 8 x 8 flipbook a particle shows at Age (0 to 1 over its life), from the frame First to Last: the two
# neighbouring frames out as UVA and UVB and how far it is between them returned, so the motion blends. A particle whose
# random value is over a half shows it mirrored, so neighbours look different.
FLIPBOOK_CODE = """float Frame = lerp(First, Last, saturate(Age));
float A = floor(Frame);
float B = min(A + 1.0, Last);
float2 Local = UV;
if (Random > 0.5) Local.x = 1.0 - Local.x;
UVA = float3((float2(fmod(A, 8.0), floor(A / 8.0)) + Local) / 8.0, 0.0);
UVB = float3((float2(fmod(B, 8.0), floor(B / 8.0)) + Local) / 8.0, 0.0);
return Frame - A;"""


def flipbook_frame(material, x, y, first, last):
    """The flipbook frame node, its first and last frames scalar parameters FrameFirst and FrameLast."""
    node = custom_node(material, x, y, FLIPBOOK_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                       ["UV", "Age", "Random", "First", "Last"], ["UVA", "UVB"])
    connect(expression(material, unreal.MaterialExpressionTextureCoordinate, x - 300, y), "", node, "UV")
    connect(expression(material, unreal.MaterialExpressionParticleRelativeTime, x - 300, y + 80), "", node, "Age")
    connect(expression(material, unreal.MaterialExpressionParticleRandom, x - 300, y + 160), "", node, "Random")
    connect(scalar_parameter(material, x - 300, y + 240, "FrameFirst", first), "", node, "First")
    connect(scalar_parameter(material, x - 300, y + 320, "FrameLast", last), "", node, "Last")
    return node


def component_mask(material, x, y, channels):
    """A component mask keeping only channels ("r", "rg", ...): every channel is set, as a new mask keeps some by default."""
    mask = expression(material, unreal.MaterialExpressionComponentMask, x, y)
    for channel in "rgba":
        mask.set_editor_property(channel, channel in channels)
    return mask


def flipbook_sample(material, x, y, texture, frame, pin):
    """A sample of texture at the frame node's UVA or UVB."""
    sample = expression(material, unreal.MaterialExpressionTextureSample, x, y)
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    mask = component_mask(material, x - 150, y, "rg")
    connect(frame, pin, mask, "")
    connect(mask, "", sample, "UVs")
    return sample


def blended_sample(material, x, y, texture, frame):
    """texture's two neighbouring frames, blended by the frame node."""
    blend = expression(material, unreal.MaterialExpressionLinearInterpolate, x + 300, y)
    connect(flipbook_sample(material, x, y, texture, frame, "UVA"), "RGBA", blend, "A")
    connect(flipbook_sample(material, x, y + 250, texture, frame, "UVB"), "RGBA", blend, "B")
    connect(frame, "", blend, "Alpha")
    return blend


GLOW_CODE = """float Radius = lerp(0.5, 1.0, saturate(Age * 6.0));
float Falloff = saturate(1.0 - length(UV - 0.5) * 2.0 / Radius);
float Fade = 1.0 - saturate(Age);
return Color.rgb * Color.a * Falloff * Falloff * Fade * Fade;"""


def build_muzzle_flash_material(name):
    """Unlit additive sprite material for the core, the blast cone and the brake jets of the muzzle flash (#220, #294): the
    particle colour in a soft oval, brightest at its centre, that swells from half the sprite to all of it in the first
    sixth of the particle's life and burns out as it ends. The oval follows the sprite, so a sprite stretched along its
    velocity is a tongue of flame."""
    material = new_material(name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_niagara_sprites", True)

    glow = custom_node(material, -500, 100, GLOW_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Color", "UV", "Age"])
    connect(expression(material, unreal.MaterialExpressionParticleColor, -800, 0), "RGBA", glow, "Color")
    connect(expression(material, unreal.MaterialExpressionTextureCoordinate, -800, 200), "", glow, "UV")
    connect(expression(material, unreal.MaterialExpressionParticleRelativeTime, -800, 400), "", glow, "Age")
    unreal.MaterialEditingLibrary.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


# The fireball's colour by its heat as it cools: dark red, orange, white-yellow, white; as bright as the shot's colour.
FIRE_CODE = """float H = Heat * (1.0 - 0.75 * saturate(Age));
float3 C = lerp(float3(0.35, 0.03, 0.0), float3(1.0, 0.36, 0.05), smoothstep(0.05, 0.45, H));
C = lerp(C, float3(1.0, 0.86, 0.5), smoothstep(0.45, 0.85, H));
C = lerp(C, float3(1.0, 0.98, 0.9), smoothstep(0.85, 1.0, H));
return C * Heat * max(max(Color.r, Color.g), Color.b) * Color.a * 0.6;"""


def build_muzzle_fire_material(name, fire):
    """Unlit additive sprite material for the fireball of the muzzle flash (#294): the T_MuzzleFire flipbook played over
    the particle's life, its heat coloured white-yellow through orange to dark red as it cools, as bright as the particle
    colour (ARTSShotEffect's User.Color)."""
    material = new_material(name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_niagara_sprites", True)

    frame = flipbook_frame(material, -1200, 0, 0.0, FLIPBOOK_LAST_FRAME)
    heat = blended_sample(material, -800, 0, fire, frame)
    red = component_mask(material, -400, 0, "r")
    connect(heat, "", red, "")
    shade = custom_node(material, -200, 100, FIRE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Heat", "Age", "Color"])
    connect(red, "", shade, "Heat")
    connect(expression(material, unreal.MaterialExpressionParticleRelativeTime, -400, 200), "", shade, "Age")
    connect(expression(material, unreal.MaterialExpressionParticleColor, -400, 300), "RGBA", shade, "Color")
    unreal.MaterialEditingLibrary.connect_material_property(shade, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


def make_muzzle_flash():
    """The Niagara muzzle flash of vehicle guns (#220, #294), its glow and fire materials and the fire flipbook, built now
    so the weapons can point at it. The system is built in C++ (URTSEditorScripting::BuildMuzzleFlashSystem,
    RTSMuzzleFlashBuilder.cpp), so its hash covers that source too."""
    glow = plan_now(f"{MATERIAL_DIR}/M_MuzzleFlash", lambda: build_muzzle_flash_material("M_MuzzleFlash"),
                    spec_hash([build_muzzle_flash_material, custom_node], GLOW_CODE, "M_MuzzleFlash"))
    fire_texture = make_effect_texture("T_MuzzleFire")
    fire = plan_now(f"{MATERIAL_DIR}/M_MuzzleFire", lambda: build_muzzle_fire_material("M_MuzzleFire", fire_texture),
                    spec_hash([build_muzzle_fire_material, custom_node, flipbook_frame, flipbook_sample, blended_sample, component_mask],
                              FLIPBOOK_CODE, FIRE_CODE, "M_MuzzleFire", fire_texture))
    path = f"{EFFECT_DIR}/NS_MuzzleFlash"

    def build():
        if not unreal.RTSEditorScripting.load_editor_module("NiagaraEditor"):
            raise RuntimeError("Could not load the NiagaraEditor module")
        system = unreal.RTSEditorScripting.build_muzzle_flash_system(path, glow, fire)
        if not system:
            raise RuntimeError(f"Could not build {path}; the log says why")
        return system
    builder_source = file_hash(os.path.join(PROJECT_DIR, "Source", "WorldDivide", "Core", "RTSMuzzleFlashBuilder.cpp"))
    return plan_now(path, build, spec_hash([make_muzzle_flash], path, glow, fire, builder_source))


# A smoke sprite's normal from its six-way light, so the side it is lit from most faces that way, in the sprite's tangent
# space (+X right, +Y down the flipbook, +Z toward the camera); its albedo brighter where it is lit through and scaled by
# Brightness; its opacity thinned from the edges inward by Erosion as it ages.
SMOKE_CODE = """float3 N = float3(RLT.r - RLT.g, BBF.r - RLT.b, (BBF.b - BBF.g) * 0.5 + 0.6);
Normal = normalize(N);
float Light = (RLT.r + RLT.g + RLT.b + BBF.r + BBF.g + BBF.b) / 6.0;
Albedo = Color.rgb * lerp(0.55, 1.15, saturate(Light * 1.6)) * Brightness;
float Thin = saturate(Age) * Erosion;
return saturate((RLT.a - Thin) / max(1.0 - Thin, 0.05)) * Color.a;"""


def build_muzzle_smoke_material(name, rlt, bbf):
    """Lit translucent sprite material for the muzzle smoke and the ground blast (#221, #294, #303): the six-way lit smoke
    flipbooks Tools/Art/muzzle_fx_blender.py bakes (T_MuzzleSmoke_RLT and _BBF), played from FrameFirst to FrameLast over
    the particle's life. The six views give each pixel a normal, so the sun, the sky and the muzzle light shade the cloud
    from wherever they are; the particle colour tints it. It thins from its edges inward by Erosion as it ages and fades
    over FadeDistance cm where it meets the ground or a hull. The instances (SMOKE_LOOKS) set the frames and the erosion of the blast cloud, the drifting
    cloud and the dust; Brightness scales the albedo (1 for the muzzle smoke)."""
    material = new_material(name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_DIRECTIONAL)
    material.set_editor_property("used_with_niagara_sprites", True)

    frame = flipbook_frame(material, -1400, 0, 0.0, FLIPBOOK_LAST_FRAME)
    rlt_sample = blended_sample(material, -1000, 0, rlt, frame)
    bbf_sample = blended_sample(material, -1000, 600, bbf, frame)
    shade = custom_node(material, -300, 200, SMOKE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                        ["RLT", "BBF", "Color", "Age", "Erosion", "Brightness"], ["Normal", "Albedo"])
    connect(rlt_sample, "", shade, "RLT")
    connect(bbf_sample, "", shade, "BBF")
    connect(expression(material, unreal.MaterialExpressionParticleColor, -600, 400), "RGBA", shade, "Color")
    connect(expression(material, unreal.MaterialExpressionParticleRelativeTime, -600, 500), "", shade, "Age")
    connect(scalar_parameter(material, -600, 600, "Erosion", 0.5), "", shade, "Erosion")
    connect(scalar_parameter(material, -600, 700, "Brightness", 1.0), "", shade, "Brightness")

    fade = expression(material, unreal.MaterialExpressionDepthFade, 0, 300)
    connect(shade, "", fade, "Opacity")
    connect(scalar_parameter(material, -300, 400, "FadeDistance", 80.0), "", fade, "FadeDistance")
    roughness = expression(material, unreal.MaterialExpressionConstant, 0, 500)
    roughness.set_editor_property("r", 1.0)
    editing = unreal.MaterialEditingLibrary
    editing.connect_material_property(shade, "Albedo", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(shade, "Normal", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(fade, "", unreal.MaterialProperty.MP_OPACITY)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(material)
    return material


# The smoke instances: the frames of the flipbook each plays and how much it erodes. The blast cloud plays the dense
# billow, the drifting cloud and the dust the later, larger and thinner frames.
SMOKE_LOOKS = {
    "MI_MuzzleSmoke_Blast": {"FrameFirst": 0.0, "FrameLast": 40.0, "Erosion": 0.35},
    "MI_MuzzleSmoke_Drift": {"FrameFirst": 18.0, "FrameLast": 63.0, "Erosion": 0.6},
    "MI_MuzzleSmoke_Dust": {"FrameFirst": 6.0, "FrameLast": 50.0, "Erosion": 0.7},
}


def build_material_instance(name, parent, scalars):
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        instance = asset_tools.create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
    editing = unreal.MaterialEditingLibrary
    editing.set_material_instance_parent(instance, parent)
    for parameter, value in scalars.items():
        editing.set_material_instance_scalar_parameter_value(instance, parameter, value)
    editing.update_material_instance(instance)
    return instance


def make_muzzle_smoke():
    """The Niagara muzzle smoke of vehicle cannons (#221, #303) with its ground blast (#294), the smoke material, its
    instances and the six-way flipbooks, built now so the weapons can point at it. Built in C++ like the muzzle flash
    (URTSEditorScripting::BuildMuzzleSmokeSystem, RTSMuzzleFlashBuilder.cpp)."""
    rlt = make_effect_texture("T_MuzzleSmoke_RLT")
    bbf = make_effect_texture("T_MuzzleSmoke_BBF")
    parent = plan_now(f"{MATERIAL_DIR}/M_MuzzleSmoke", lambda: build_muzzle_smoke_material("M_MuzzleSmoke", rlt, bbf),
                      spec_hash([build_muzzle_smoke_material, custom_node, flipbook_frame, flipbook_sample, blended_sample,
                                 scalar_parameter, component_mask], FLIPBOOK_CODE, SMOKE_CODE, "M_MuzzleSmoke", rlt, bbf))
    looks = {name: plan_now(f"{MATERIAL_DIR}/{name}", lambda n=name, v=values: build_material_instance(n, parent, v),
                            spec_hash([build_material_instance], name, parent, sorted(values.items())))
             for name, values in SMOKE_LOOKS.items()}
    path = f"{EFFECT_DIR}/NS_MuzzleSmoke"

    def build():
        if not unreal.RTSEditorScripting.load_editor_module("NiagaraEditor"):
            raise RuntimeError("Could not load the NiagaraEditor module")
        system = unreal.RTSEditorScripting.build_muzzle_smoke_system(
            path, looks["MI_MuzzleSmoke_Blast"], looks["MI_MuzzleSmoke_Drift"], looks["MI_MuzzleSmoke_Dust"])
        if not system:
            raise RuntimeError(f"Could not build {path}; the log says why")
        return system
    builder_source = file_hash(os.path.join(PROJECT_DIR, "Source", "WorldDivide", "Core", "RTSMuzzleFlashBuilder.cpp"))
    return plan_now(path, build, spec_hash([make_muzzle_smoke], path, *looks.values(), builder_source))


# The exhaust's look (#222): the later, thinner frames of the muzzle smoke's flipbook, eroded, so a puff reads as a light
# haze rather than smoke; it fades over a few cm only, so a puff shows right at the grille it leaves. The smoke material
# takes in far more light than an opaque surface of the same colour, which turned a dark grey exhaust white (#325), so
# Brightness brings it down until FRTSVehicleExhaust's colours read as the colours they are next to the ground and hull.
EXHAUST_LOOK = {"FrameFirst": 24.0, "FrameLast": 63.0, "Erosion": 0.4, "FadeDistance": 10.0, "Brightness": 0.2}


def make_exhaust():
    """The Niagara engine exhaust of vehicles (#222) and its material, an instance of the muzzle smoke's (make_muzzle_smoke
    builds the parent first). Built in C++ like the muzzle flash (URTSEditorScripting::BuildExhaustSystem,
    RTSMuzzleFlashBuilder.cpp)."""
    parent = assets.load_asset(f"{MATERIAL_DIR}/M_MuzzleSmoke")
    look = plan_now(f"{MATERIAL_DIR}/MI_Exhaust", lambda: build_material_instance("MI_Exhaust", parent, EXHAUST_LOOK),
                    spec_hash([build_material_instance], "MI_Exhaust", parent, sorted(EXHAUST_LOOK.items())))
    path = f"{EFFECT_DIR}/NS_Exhaust"

    def build():
        if not unreal.RTSEditorScripting.load_editor_module("NiagaraEditor"):
            raise RuntimeError("Could not load the NiagaraEditor module")
        system = unreal.RTSEditorScripting.build_exhaust_system(path, look)
        if not system:
            raise RuntimeError(f"Could not build {path}; the log says why")
        return system
    builder_source = file_hash(os.path.join(PROJECT_DIR, "Source", "WorldDivide", "Core", "RTSMuzzleFlashBuilder.cpp"))
    return plan_now(path, build, spec_hash([make_exhaust], path, look, builder_source))


# A diesel's exhaust per engine state (#325): a faint blue-grey haze at idle, light grey on the move and a dark puff, near
# black, while it accelerates under load. Colours as (r, g, b) in linear space.
DIESEL_EXHAUST = {
    "idle_color": (0.36, 0.4, 0.46), "idle_opacity": 0.3,
    "moving_color": (0.42, 0.42, 0.41), "moving_opacity": 0.4,
    "accelerating_color": (0.04, 0.04, 0.04), "accelerating_opacity": 0.7,
}


def vehicle_exhaust(system, outlets, direction, engine=None):
    """A vehicle's engine exhaust (FRTSVehicleExhaust, #222): the system, the outlets in the mesh's own space in cm, the
    way the exhaust leaves them and the engine's colours and opacities per state (engine, as DIESEL_EXHAUST); the rest,
    and whatever engine leaves out, keep the struct's defaults."""
    value = unreal.RTSVehicleExhaust()
    value.set_editor_property("system", system)
    value.set_editor_property("outlets", [unreal.Vector(*outlet) for outlet in outlets])
    value.set_editor_property("direction", unreal.Vector(*direction))
    for name, setting in (engine or {}).items():
        value.set_editor_property(name, unreal.LinearColor(*setting, 1.0) if name.endswith("_color") else setting)
    return value


def vehicle_lights(headlights, taillights, **settings):
    """A vehicle's head- and rear lights (FRTSVehicleLights, #223): the headlights' and the tail lamps' faces in the mesh's
    own space in cm; settings (as headlight_intensity=800.0) change the beams, the rest keep the struct's defaults."""
    value = unreal.RTSVehicleLights()
    value.set_editor_property("headlight_offsets", [unreal.Vector(*offset) for offset in headlights])
    value.set_editor_property("taillight_offsets", [unreal.Vector(*offset) for offset in taillights])
    for name, setting in settings.items():
        value.set_editor_property(name, unreal.LinearColor(*setting, 1.0) if name.endswith("_color") else setting)
    return value


def build_surface_material(name, color, roughness, digest):
    """Opaque lit material of one colour, for the ground and the water of the maps."""
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
    else:
        material = asset_tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())

    editing = unreal.MaterialEditingLibrary
    editing.delete_all_material_expressions(material)
    base = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
    base.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
    editing.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", roughness)
    editing.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    editing.recompile_material(material)
    assets.set_metadata_tag(material, HASH_TAG, digest)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")


def make_surface_material(name, color, roughness):
    """Queued before the maps, which load it by path when they are built. Returns its path; its hash goes in
    material_hashes, so the maps and their previews are rebuilt when it changes."""
    path = f"{MATERIAL_DIR}/{name}"
    digest = spec_hash([build_surface_material], name, color, roughness)
    plan_rebuild(path, ".uasset", lambda digest: build_surface_material(name, color, roughness, digest), digest)
    material_hashes[path] = digest
    return path


def spawn(actor_class, location, yaw=0.0, label=None):
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    return actor


def spawn_start_spot(spot, location, yaw=0.0):
    """An RTSStartSpot numbered spot (0-based); it tags itself Team<spot>, which the game mode reads."""
    start = spawn(unreal.RTSStartSpot, location, yaw, label=f"StartSpot_{spot + 1}")
    start.set_editor_property("spot_index", spot)
    if str(start.get_editor_property("player_start_tag")) != f"Team{spot}":
        unreal.log_warning(f"{start.get_actor_label()} is tagged {start.get_editor_property('player_start_tag')}, not Team{spot}")
    return start


def spawn_block(label, location, scale):
    block = spawn(unreal.StaticMeshActor, location, label=label)
    block.static_mesh_component.set_static_mesh(unreal.load_asset(CUBE))
    block.set_actor_scale3d(unreal.Vector(*scale))
    return block


def spawn_obstacle(label, location, scale):
    """A block with a null nav area over it, so Recast leaves no island inside it or on its roof."""
    block = spawn_block(label, location, scale)
    # The default volume brush is a 200 uu cube, twice the size of the engine cube; 50 cm extra above and below.
    nav_null = spawn(unreal.NavModifierVolume, location, label=f"{label}_NavNull")
    nav_null.set_actor_scale3d(unreal.Vector(scale[0] / 2.0, scale[1] / 2.0, (scale[2] + 1.0) / 2.0))
    nav_null.set_editor_property("area_class", unreal.NavArea_Null)
    return block


def spawn_hill(label, x, width, length, height, slope, material):
    """A walkable ridge running along y at x: a ramp of slope degrees up from the west, a flat top width across and height
    high, and a ramp down to the east. Ramps are slabs tilted into the ground, their top face meeting the ground and the top."""
    top = spawn_block(f"{label}_Top", (x, 0.0, height / 2.0), (width / 100.0, length / 100.0, height / 100.0))
    top.static_mesh_component.set_material(0, unreal.load_asset(material))
    angle = math.radians(slope)
    run = height / math.tan(angle)
    thickness = 100.0
    for side, name in ((-1.0, "West"), (1.0, "East")):
        # The middle of the ramp's top face, and the slab's centre half its thickness below it, along the face's normal.
        middle_x = x + side * (width + run) / 2.0
        center = (middle_x - side * thickness / 2.0 * math.sin(angle), 0.0, height / 2.0 - thickness / 2.0 * math.cos(angle))
        ramp = spawn_block(f"{label}_{name}Ramp", center, (run / math.cos(angle) / 100.0, length / 100.0, thickness / 100.0))
        ramp.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-side * slope, yaw=0.0), False)
        ramp.static_mesh_component.set_material(0, unreal.load_asset(material))
    return top


def spawn_units(unit, count, team_id, location, yaw):
    spawner = spawn(unreal.RTSUnitSpawner, location, yaw, label=f"Spawn_Team{team_id}_{unit.get_name()}")
    spawner.set_editor_property("unit_data", unit)
    spawner.set_editor_property("count", count)
    spawner.set_editor_property("team_id", team_id)
    return spawner


def spawn_wall(label, location, scale, piece):
    """An obstacle running along y whose block is hidden behind a row of wall pieces; the block alone collides."""
    block = spawn_obstacle(label, location, scale)
    block.static_mesh_component.set_visibility(False)
    box = piece.get_bounding_box()
    width, length, height = scale[0] * 100.0, scale[1] * 100.0, scale[2] * 100.0
    count = max(1, round(length / width))
    piece_scale = (width / (box.max.x - box.min.x), length / count / (box.max.y - box.min.y), height / (box.max.z - box.min.z))
    for index in range(count):
        y = location[1] - length / 2.0 + length / count * (index + 0.5)
        z = location[2] - height / 2.0 - box.min.z * piece_scale[2]
        # Not labelled Wall_ or Block_: the tests take those to be the obstacles themselves.
        visual = spawn(unreal.StaticMeshActor, (location[0], y, z), label=f"Visual_{label}_{index}")
        visual.static_mesh_component.set_static_mesh(piece)
        visual.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        visual.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)
        visual.set_actor_scale3d(unreal.Vector(*piece_scale))
    return block


def spawn_supply_source(location, amount, mesh):
    """A supply source of the standard's size (WORLD_SCALE_STANDARD.md section 3)."""
    source = spawn(unreal.RTSSupplySource, location, label=f"SupplySource_{int(location[0])}_{int(location[1])}")
    source.set_editor_property("amount", amount)
    source.set_editor_property("footprint", unreal.IntPoint(*SUPPLY_FOOTPRINT))
    source.set_editor_property("height", SUPPLY_HEIGHT)
    source.set_editor_property("mesh", mesh)
    return source


def build_navigation(world, path):
    """Builds the level's navmesh before it is saved, so any map opened later, or loaded in a packaged build, plays with
    a full navmesh straight away."""
    tiles = unreal.RTSEditorScripting.build_navigation(world)
    if tiles == 0:
        raise RuntimeError(f"Could not build the navmesh of {path}")
    unreal.log(f"{path}: navmesh built with {tiles} tiles")


def spawn_ground(half_size, material):
    """The square ground of a map, 2 * half_size across, top face at z = 0."""
    ground = spawn_block("Ground", (0.0, 0.0, -50.0), (half_size / 50.0, half_size / 50.0, 1.0))
    ground.static_mesh_component.set_material(0, unreal.load_asset(material))
    return ground


def spawn_water(label, location, scale, material):
    """A flat strip of water no unit can cross: an obstacle a few centimetres above the ground."""
    water = spawn_obstacle(label, location, scale)
    water.static_mesh_component.set_material(0, unreal.load_asset(material))
    return water


def spawn_lighting():
    sun = spawn(unreal.DirectionalLight, (0.0, 0.0, 2000.0), label="Sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-50.0, yaw=35.0), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("atmosphere_sun_light", True)
    sky = spawn(unreal.SkyLight, (0.0, 0.0, 1000.0), label="SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), label="SkyAtmosphere")


# The moon of L_TestMapNight: dim and cold, low enough in lux that URTSDayNightSubsystem::IsDark holds, with manual exposure
# lifted so hulls still read as shapes against the ground while the lamps and their beams stand out.
NIGHT_MOON_LUX = 0.6
NIGHT_MOON_COLOR = (0.55, 0.66, 1.0)
NIGHT_MOON_PITCH = -40.0
NIGHT_SKY_INTENSITY = 0.25
NIGHT_EXPOSURE_BIAS = 2.0


def spawn_night_lighting():
    moon = spawn(unreal.DirectionalLight, (0.0, 0.0, 2000.0), label="Sun")
    moon.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=NIGHT_MOON_PITCH, yaw=35.0), False)
    light = moon.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_editor_property("atmosphere_sun_light", True)
    light.set_editor_property("intensity", NIGHT_MOON_LUX)
    red, green, blue = (round(c * 255) for c in NIGHT_MOON_COLOR)
    light.set_editor_property("light_color", unreal.Color(r=red, g=green, b=blue, a=255))
    sky = spawn(unreal.SkyLight, (0.0, 0.0, 1000.0), label="SkyLight")
    sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_light.set_editor_property("real_time_capture", True)
    sky_light.set_editor_property("intensity", NIGHT_SKY_INTENSITY)
    spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), label="SkyAtmosphere")
    volume = spawn(unreal.PostProcessVolume, (0.0, 0.0, 0.0), label="NightExposure")
    volume.set_editor_property("unbound", True)
    settings = volume.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", NIGHT_EXPOSURE_BIAS)
    volume.set_editor_property("settings", settings)


def build_map(path, night, tank, infantry, builder, m20, wall, crate, ground, digest):
    new_level(path)
    check_map_lanes(TEST_MAP)

    # 600 x 600 m ground, top face at z = 0.
    spawn_ground(TEST_MAP.half_size, ground)

    # Obstacles between the two bases so pathfinding has something to do.
    for kind, label, location, scale in TEST_MAP.obstacles:
        if kind == "wall":
            spawn_wall(label, location, scale, wall)
        else:
            spawn_obstacle(label, location, scale)
    # A ridge between Wall_East and the east base, which the east army crosses on its way to the middle. Not labelled Wall_ or
    # Block_: the tests take those to be the obstacles.
    spawn_hill("Hill_East", *TEST_HILL, ground)

    if night:
        spawn_night_lighting()
    else:
        spawn_lighting()

    # The default volume brush is a 200 uu cube, so this covers the whole ground.
    nav = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(TEST_MAP.half_size / 100.0, TEST_MAP.half_size / 100.0, 10.0))
    _, nav_extent = nav.get_actor_bounds(False)
    unreal.log(f"NavMeshBoundsVolume extent: {nav_extent}")
    if nav_extent.x < 1000.0:
        unreal.log_warning(f"NavMeshBoundsVolume has no usable brush: drag one into {path} by hand and save.")

    for team_id, x, _, yaw in TEST_MAP.spots:
        spawn_start_spot(team_id, (x, 0.0, 100.0), yaw)
        forward = 1.0 if yaw == 0.0 else -1.0
        # The army in front of the headquarters, tanks to its right and infantry to its left, and the builders behind it.
        spawn_units(tank, 6, team_id, (x + BASE_ARMY_AHEAD * forward, -BASE_ARMY_SIDE, 0.0), yaw)
        spawn_units(infantry, 10, team_id, (x + BASE_ARMY_AHEAD * forward, BASE_ARMY_SIDE, 0.0), yaw)
        spawn_units(builder, 2, team_id, (x - BASE_BUILDERS_BEHIND * forward, 0.0, 0.0), yaw)
        if team_id == 0:
            # The real-size test tank, beside the base and clear of the army's paths.
            spawn_units(m20, 2, team_id, (x + 2400.0, -7800.0, 0.0), yaw)
        # Beside each base, clear of the units' paths and of the empty strip at y = 21000 the tests build in.
        spawn_supply_source((*base_source(x, 0.0, yaw), 0.0), 5000, crate)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    build_navigation(world, path)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {path}")


def make_map(path, night, tank, infantry, builder, m20, wall, crate, ground):
    lighting = spawn_night_lighting if night else spawn_lighting
    night_look = (NIGHT_MOON_LUX, NIGHT_MOON_COLOR, NIGHT_MOON_PITCH, NIGHT_SKY_INTENSITY, NIGHT_EXPOSURE_BIAS) if night else None
    plan_rebuild(path, ".umap", lambda digest: build_map(path, night, tank, infantry, builder, m20, wall, crate, ground, digest),
                 spec_hash([build_map, build_navigation, check_map_lanes, base_source, spawn, spawn_block, spawn_hill, spawn_obstacle, spawn_wall,
                            spawn_units, spawn_supply_source, spawn_ground, lighting], path, night_look, TEST_MAP, TEST_HILL,
                           BASE_LAYOUT, MAP_LANE_RULES, CUBE, tank, infantry, builder, m20, wall, crate, ground,
                           material_hashes[ground], ENGINE_VERSION, NAV_CONFIG))


def build_bench_map(ground, digest):
    """The benchmark level of rts.Debug.Bench (ue.ps1 -Task bench): flat open ground with room in the middle for a grid of
    240 vehicles and their move order, and a headquarters for each of two teams in far corners, which keep both teams in
    the match while the benchmark's units come and go. It does not change with the game's scale, so measurements stay
    comparable."""
    new_level(BENCH_MAP_PATH)
    spawn_ground(BENCH_HALF_SIZE, ground)
    spawn_lighting()
    nav = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(BENCH_HALF_SIZE / 100.0, BENCH_HALF_SIZE / 100.0, 10.0))
    for team_id, corner in ((0, -1.0), (1, 1.0)):
        spawn_start_spot(team_id, (corner * BENCH_CORNER, corner * BENCH_CORNER, 100.0))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    build_navigation(world, BENCH_MAP_PATH)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {BENCH_MAP_PATH}")


def make_bench_map(ground):
    plan_rebuild(BENCH_MAP_PATH, ".umap", lambda digest: build_bench_map(ground, digest),
                 spec_hash([build_bench_map, build_navigation, spawn, spawn_block, spawn_ground, spawn_lighting],
                           BENCH_MAP_PATH, BENCH_HALF_SIZE, BENCH_CORNER, CUBE, ground, material_hashes[ground], ENGINE_VERSION, NAV_CONFIG))


# The long-path level of WorldDivide.Navigation.LongPath (#169), 400 x 400 m. A unit west of a pocket sent to a goal east
# of it heads straight in: the pocket, open to the west and full of pillars that cut the navmesh into many polygons, holds
# more than Recast's 2048 search nodes before A* turns round. The way out runs north round the pocket, through a 12 m gap
# in a wall and south round a baffle. A walled enclosure and an island in a moat are the goals nothing reaches.
LONG_PATH_MAP_PATH = "/Game/Maps/L_LongPath"
LONG_PATH_HALF_SIZE = 20000.0
LONG_PATH_WALL_HEIGHT = 300.0
# The pocket's inside: centre and x and y half sizes, and the pillar grid in it: pitch and side, in cm. The gaps between
# pillars stay wide enough for the vehicle navmesh, so both agents' navmeshes are cut up.
LONG_PATH_POCKET = ((0.0, 0.0), 6000.0, 12000.0)
LONG_PATH_PILLARS = (550.0, 60.0)
# Walls as (label, centre, size along x and y), 8 m thick: blocks, whose corners the vehicles round as they round a
# building's. Wall_NorthLow and Wall_NorthHigh leave the 12 m gap between y = 15600 and 16800.
LONG_PATH_WALLS = (
    ("Wall_PocketNorth", (400.0, 12400.0), (12800.0, 800.0)),
    ("Wall_PocketSouth", (400.0, -12400.0), (12800.0, 800.0)),
    ("Wall_PocketBack", (6400.0, 0.0), (800.0, 25600.0)),
    ("Wall_SouthClosure", (0.0, -16400.0), (800.0, 7200.0)),
    ("Wall_NorthLow", (0.0, 14200.0), (800.0, 2800.0)),
    ("Wall_NorthHigh", (0.0, 18400.0), (800.0, 3200.0)),
    ("Wall_Baffle", (9400.0, 9000.0), (800.0, 22000.0)),
)
# The enclosure (walls round a square) and the island (a moat round a square): centre, inner half size, wall or moat width.
LONG_PATH_ENCLOSURE = ((15000.0, 15000.0), 2000.0, 200.0)
LONG_PATH_ISLAND = ((-14000.0, -15000.0), 2000.0, 600.0)
LONG_PATH_SPOTS = ((0, -17000.0, 17000.0, 0.0), (1, 17000.0, -17000.0, 180.0))


def ring_walls(prefix, center, inner, width):
    """Four strips closing a square of inner half size round center: (label, centre, size along x and y)."""
    x, y = center
    offset = inner + width / 2.0
    span = 2.0 * (inner + width)
    return ((f"{prefix}North", (x, y + offset), (span, width)), (f"{prefix}South", (x, y - offset), (span, width)),
            (f"{prefix}East", (x + offset, y), (width, span)), (f"{prefix}West", (x - offset, y), (width, span)))


def build_long_path_map(ground, water, digest):
    new_level(LONG_PATH_MAP_PATH)
    spawn_ground(LONG_PATH_HALF_SIZE, ground)
    height = LONG_PATH_WALL_HEIGHT
    for label, (x, y), (size_x, size_y) in LONG_PATH_WALLS + ring_walls("Wall_Enclosure", *LONG_PATH_ENCLOSURE):
        spawn_obstacle(label, (x, y, height / 2.0), (size_x / 100.0, size_y / 100.0, height / 100.0))
    for label, (x, y), (size_x, size_y) in ring_walls("Block_Moat", *LONG_PATH_ISLAND):
        spawn_water(label, (x, y, 1.0), (size_x / 100.0, size_y / 100.0, 0.1), water)
    (center_x, center_y), half_x, half_y = LONG_PATH_POCKET
    pitch, side = LONG_PATH_PILLARS
    columns = int((half_x - pitch / 2.0) // pitch)
    rows = int((half_y - pitch / 2.0) // pitch)
    for column in range(-columns, columns + 1):
        for row in range(-rows, rows + 1):
            spawn_obstacle(f"Block_Pillar_{column}_{row}", (center_x + column * pitch, center_y + row * pitch, height / 2.0),
                           (side / 100.0, side / 100.0, height / 100.0))
    spawn_lighting()

    nav = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(LONG_PATH_HALF_SIZE / 100.0, LONG_PATH_HALF_SIZE / 100.0, 10.0))
    for team_id, x, y, yaw in LONG_PATH_SPOTS:
        spawn_start_spot(team_id, (x, y, 100.0), yaw)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    build_navigation(world, LONG_PATH_MAP_PATH)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {LONG_PATH_MAP_PATH}")


def make_long_path_map(ground, water):
    plan_rebuild(LONG_PATH_MAP_PATH, ".umap", lambda digest: build_long_path_map(ground, water, digest),
                 spec_hash([build_long_path_map, ring_walls, build_navigation, spawn, spawn_block, spawn_obstacle, spawn_water,
                            spawn_ground, spawn_lighting], LONG_PATH_MAP_PATH, LONG_PATH_HALF_SIZE, LONG_PATH_WALL_HEIGHT,
                           LONG_PATH_POCKET, LONG_PATH_PILLARS, LONG_PATH_WALLS, LONG_PATH_ENCLOSURE, LONG_PATH_ISLAND,
                           LONG_PATH_SPOTS, CUBE, ground, water, material_hashes[ground], material_hashes[water], ENGINE_VERSION,
                           NAV_CONFIG))


# The thin-wall level of WorldDivide.Navigation.ThinWalls (#173), 200 x 200 m: walls 2 m thick, with navmesh on every side.
# A free-standing wall whose ends the vehicles round, and two walls meeting at an L, the horizontal one running on 3 m
# past the vertical one and the vertical one 3 m past it, as L_LongPath's first layout had them.
THIN_WALLS_MAP_PATH = "/Game/Maps/L_ThinWalls"
THIN_WALLS_HALF_SIZE = 10000.0
THIN_WALLS_HEIGHT = 300.0
# Walls as (label, centre, size along x and y).
THIN_WALLS = (
    ("Wall_Free", (-5000.0, 0.0), (200.0, 6000.0)),
    ("Wall_JunctionNorth", (2750.0, 2400.0), (5500.0, 200.0)),
    ("Wall_JunctionEast", (5100.0, -100.0), (200.0, 5800.0)),
)
THIN_WALLS_SPOTS = ((0, -7000.0, -7000.0, 0.0), (1, 7000.0, -7000.0, 180.0))


def build_thin_walls_map(ground, digest):
    new_level(THIN_WALLS_MAP_PATH)
    spawn_ground(THIN_WALLS_HALF_SIZE, ground)
    height = THIN_WALLS_HEIGHT
    for label, (x, y), (size_x, size_y) in THIN_WALLS:
        spawn_obstacle(label, (x, y, height / 2.0), (size_x / 100.0, size_y / 100.0, height / 100.0))
    spawn_lighting()

    nav = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(THIN_WALLS_HALF_SIZE / 100.0, THIN_WALLS_HALF_SIZE / 100.0, 10.0))
    for team_id, x, y, yaw in THIN_WALLS_SPOTS:
        spawn_start_spot(team_id, (x, y, 100.0), yaw)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    build_navigation(world, THIN_WALLS_MAP_PATH)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {THIN_WALLS_MAP_PATH}")


def make_thin_walls_map(ground):
    plan_rebuild(THIN_WALLS_MAP_PATH, ".umap", lambda digest: build_thin_walls_map(ground, digest),
                 spec_hash([build_thin_walls_map, build_navigation, spawn, spawn_block, spawn_obstacle, spawn_ground,
                            spawn_lighting], THIN_WALLS_MAP_PATH, THIN_WALLS_HALF_SIZE, THIN_WALLS_HEIGHT, THIN_WALLS,
                           THIN_WALLS_SPOTS, CUBE, ground, material_hashes[ground], ENGINE_VERSION, NAV_CONFIG))


def ring(radius, angle):
    """The point at radius from the middle, angle degrees from world +X towards +Y, rounded to the centimetre."""
    return round(radius * math.cos(math.radians(angle)), 1), round(radius * math.sin(math.radians(angle)), 1)


def ring_spots(radius, angles):
    """Start spots on a circle, each facing the middle: (spot, x, y, yaw) in the order of angles."""
    return tuple((spot, *ring(radius, angle), angle % 360.0 - 180.0) for spot, angle in enumerate(angles))


# A skirmish map: its level, half the width of its square ground, its start spots (spot, x, y, yaw) facing the middle,
# its obstacles (kind, label, location, scale) of kind "wall" (runs along y), "block" or "water", and the supply sources
# (x, y, amount) the bases contest, besides the one by each base. The preview covers the whole ground.
SkirmishMap = collections.namedtuple("SkirmishMap", "path half_size spots obstacles sources")

# Real scale (WORLD_SCALE_STANDARD.md section 6, PLACEHOLDER_SCALE_AUDIT.md): the generated maps are laid out three times
# as large as before, for real-size vehicles and 16 to 28 m buildings. Rocks and walls keep their thickness, so the gaps
# between them only grow.
# A supply source's footprint in 1 m cells and its height, in cm.
SUPPLY_FOOTPRINT = (12, 12)
SUPPLY_HEIGHT = 300.0
# What stands by each start spot, in cm along the spot's facing and to its left: the army (L_TestMap only) ahead of the
# 28 m headquarters, the builders behind it and the base's own supply source beside it.
BASE_ARMY_AHEAD = 3000.0
BASE_ARMY_SIDE = 2700.0
BASE_BUILDERS_BEHIND = 2400.0
BASE_SOURCE_BEHIND = 150.0
BASE_SOURCE_SIDE = 7650.0
BASE_LAYOUT = (BASE_ARMY_AHEAD, BASE_ARMY_SIDE, BASE_BUILDERS_BEHIND, BASE_SOURCE_BEHIND, BASE_SOURCE_SIDE, SUPPLY_FOOTPRINT,
               SUPPLY_HEIGHT)
# check_map_lanes: a gap between two obstacles, or between an obstacle and the ground's edge, is closed or at least
# VEHICLE_LANE wide (standard rule 4: 8 m carriageways), and every base keeps BASE_CLEARANCE around its spot free: the
# headquarters' half size (14 m), its exit and the builders behind it.
VEHICLE_LANE = 800.0
BASE_CLEARANCE = 4000.0
MAP_LANE_RULES = (VEHICLE_LANE, BASE_CLEARANCE)


def check_map_lanes(spec):
    """Raises unless spec's obstacles leave vehicle-wide lanes and its bases room (MAP_LANE_RULES)."""
    def footprint(location, scale):
        return (location[0] - scale[0] * 50.0, location[1] - scale[1] * 50.0,
                location[0] + scale[0] * 50.0, location[1] + scale[1] * 50.0)

    def gap(a, b):
        return max(b[0] - a[2], a[0] - b[2], b[1] - a[3], a[1] - b[3])

    boxes = [(label, footprint(location, scale)) for _, label, location, scale in spec.obstacles]
    edge = spec.half_size
    for index, (label, box) in enumerate(boxes):
        for other, other_box in boxes[index + 1:]:
            if 0.0 < gap(box, other_box) < VEHICLE_LANE:
                raise RuntimeError(f"{spec.path}: {label} and {other} leave a {gap(box, other_box):.0f} cm gap, under {VEHICLE_LANE:.0f}")
        to_edge = min(box[0] + edge, box[1] + edge, edge - box[2], edge - box[3])
        if 0.0 < to_edge < VEHICLE_LANE:
            raise RuntimeError(f"{spec.path}: {label} leaves a {to_edge:.0f} cm gap to the edge, under {VEHICLE_LANE:.0f}")
    for spot, x, y, _ in spec.spots:
        base = (x - BASE_CLEARANCE, y - BASE_CLEARANCE, x + BASE_CLEARANCE, y + BASE_CLEARANCE)
        if max(abs(x), abs(y)) + BASE_CLEARANCE > edge:
            raise RuntimeError(f"{spec.path}: start spot {spot} has no room for its base inside the ground")
        for label, box in boxes:
            if gap(base, box) < 0.0:
                raise RuntimeError(f"{spec.path}: {label} stands in the base of start spot {spot}")


def base_source(x, y, yaw):
    """Where the supply source of the base at (x, y) facing yaw goes: to its left, seen from the middle."""
    forward = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    return (x - BASE_SOURCE_BEHIND * forward[0] - BASE_SOURCE_SIDE * forward[1],
            y - BASE_SOURCE_BEHIND * forward[1] + BASE_SOURCE_SIDE * forward[0])


# L_TestMap's ground and obstacles, 600 x 600 m; west and east as in L_TestMap, south and north on the empty strips.
TEST_OBSTACLES = (
    ("wall", "Wall_West", (-7500.0, 0.0, 150.0), (2.0, 60.0, 3.0)),
    ("block", "Block_North", (3000.0, 7500.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_South", (3000.0, -7500.0, 200.0), (8.0, 8.0, 4.0)),
    ("wall", "Wall_East", (10500.0, 0.0, 150.0), (2.0, 36.0, 3.0)),
)
TEST_SPOTS = ((0, -18000.0, 0.0, 0.0), (1, 18000.0, 0.0, 180.0), (2, 0.0, -21000.0, 90.0), (3, 0.0, 21000.0, -90.0))
TEST_MAP = SkirmishMap(MAP_PATH, 30000.0, TEST_SPOTS[:2], TEST_OBSTACLES, ())
# L_TestMap's ridge: x, top width, length along y, height and ramp slope in degrees (spawn_hill).
TEST_HILL = (13500.0, 900.0, 12000.0, 300.0, 15.0)
SKIRMISH_MAP = SkirmishMap("/Game/Maps/L_Skirmish", 30000.0, TEST_SPOTS, TEST_OBSTACLES, ())
DUEL_MAP = SkirmishMap("/Game/Maps/L_SkirmishDuel", 30000.0, TEST_SPOTS[:2], TEST_OBSTACLES, ())

# 500 x 500 m, west against east across a 24 m river running north to south from edge to edge. It is crossed at two
# 36 m fords, each between two rocks, with a supply source by each ford, one on either bank.
RIVER_FORD_MAP = SkirmishMap("/Game/Maps/L_RiverFord", 25000.0, (
    (0, -16500.0, 0.0, 0.0), (1, 16500.0, 0.0, 180.0),
), (
    ("water", "Water_South", (0.0, -19400.0, 1.0), (24.0, 112.0, 0.1)),
    ("water", "Water_Middle", (0.0, 0.0, 1.0), (24.0, 204.0, 0.1)),
    ("water", "Water_North", (0.0, 19400.0, 1.0), (24.0, 112.0, 0.1)),
    ("block", "Block_FordNorthWest", (-4500.0, 15900.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordNorthEast", (4500.0, 8100.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordSouthWest", (-4500.0, -8100.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordSouthEast", (4500.0, -15900.0, 150.0), (5.0, 5.0, 3.0)),
    ("wall", "Wall_West", (-11400.0, -13500.0, 150.0), (2.0, 72.0, 3.0)),
    ("wall", "Wall_East", (11400.0, 13500.0, 150.0), (2.0, 72.0, 3.0)),
), (
    (-7500.0, 16500.0, 8000), (7500.0, -16500.0, 8000),
))

# 660 x 660 m, a base in each corner. A quarry in the middle, walled to the west and east and open to the north and
# south, holds two rich supply sources; a rock stands between every two neighbouring bases.
QUARRY_CROSS_MAP = SkirmishMap("/Game/Maps/L_QuarryCross", 33000.0, (
    (0, -21000.0, -21000.0, 45.0), (1, 21000.0, 21000.0, -135.0), (2, 21000.0, -21000.0, 135.0),
    (3, -21000.0, 21000.0, -45.0),
), (
    ("wall", "Wall_QuarryWest", (-6000.0, 0.0, 150.0), (2.0, 120.0, 3.0)),
    ("wall", "Wall_QuarryEast", (6000.0, 0.0, 150.0), (2.0, 120.0, 3.0)),
    ("block", "Block_East", (21000.0, 0.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_West", (-21000.0, 0.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_North", (0.0, 21000.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_South", (0.0, -21000.0, 200.0), (8.0, 8.0, 4.0)),
), (
    (0.0, 3000.0, 10000), (0.0, -3000.0, 10000),
))

# 720 x 720 m, six bases on a ring, each opposite another. A peak stands between every two neighbouring bases and six
# rocks ring the middle; a supply source lies in front of each peak.
SIX_PEAKS_MAP = SkirmishMap("/Game/Maps/L_SixPeaks", 36000.0, ring_spots(27000.0, (180.0, 0.0, 240.0, 60.0, 120.0, 300.0)), (
    *(("block", f"Block_Peak{index}", (*ring(24000.0, 30.0 + 60.0 * index), 250.0), (8.0, 8.0, 5.0)) for index in range(6)),
    *(("block", f"Block_Middle{index}", (*ring(9000.0, 60.0 * index), 150.0), (6.0, 6.0, 3.0)) for index in range(6)),
), tuple((*ring(16500.0, 30.0 + 60.0 * index), 6000) for index in range(6)))


def build_skirmish_map(spec, builder, wall, crate, ground, water, digest):
    """A level with spec's ground and obstacles and start spots, each with only its builders and a supply source, and
    the skirmish game mode, which plays the seats the lobby chose (by default a player against an AI opponent from spots
    0 and 1) and lets every side build its army itself."""
    path = spec.path
    new_level(path)

    check_map_lanes(spec)
    spawn_ground(spec.half_size, ground)
    for kind, label, location, scale in spec.obstacles:
        if kind == "wall":
            spawn_wall(label, location, scale, wall)
        elif kind == "water":
            spawn_water(label, location, scale, water)
        else:
            spawn_obstacle(label, location, scale)
    spawn_lighting()

    # The default volume brush is a 200 uu cube, so this covers the whole ground.
    nav = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(spec.half_size / 100.0, spec.half_size / 100.0, 10.0))

    for spot, x, y, yaw in spec.spots:
        spawn_start_spot(spot, (x, y, 100.0), yaw)
        forward = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
        # The units of a spot carry its number; the game mode hands them to the team playing from it.
        spawn_units(builder, 2, spot, (x - BASE_BUILDERS_BEHIND * forward[0], y - BASE_BUILDERS_BEHIND * forward[1], 0.0), yaw)
        spawn_supply_source((*base_source(x, y, yaw), 0.0), 5000, crate)
    for x, y, amount in spec.sources:
        spawn_supply_source((x, y, 0.0), amount, crate)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.RTSSkirmishGameMode.static_class())
    build_navigation(world, path)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {path}")


def make_skirmish_map(spec, builder, wall, crate, ground, water):
    """Queues the level and then its preview."""
    digest = spec_hash([build_skirmish_map, build_navigation, check_map_lanes, base_source, spawn, spawn_block, spawn_obstacle,
                        spawn_wall, spawn_units, spawn_supply_source, spawn_ground, spawn_water, spawn_lighting], spec, BASE_LAYOUT,
                       MAP_LANE_RULES, CUBE, builder, wall, crate, ground, water, material_hashes[ground], material_hashes[water],
                       ENGINE_VERSION, NAV_CONFIG)
    plan_rebuild(spec.path, ".umap", lambda digest: build_skirmish_map(spec, builder, wall, crate, ground, water, digest), digest)
    make_map_preview(spec, digest)


def make_map_preview(spec, map_digest):
    """Queued after the level, so it shows the level as just built (bake_map.py); rebuilt whenever the level is. A drawn
    preview is rendered again by a run that has a renderer; a run without one keeps a rendered preview of an unchanged level."""
    texture_path = map_preview.map_preview_path(spec.path)
    can_render = unreal.RTSEditorScripting.can_render()

    def build(digest):
        area, _ = bake_map.read_layout(spec.path)
        texture = map_preview.build_preview(spec.path, area, texture_path)
        assets.set_metadata_tag(texture, HASH_TAG, digest)
        if not assets.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {texture_path}")

    plan_rebuild(texture_path, ".uasset", build,
                 spec_hash([make_map_preview, bake_map.read_layout, map_preview.build_preview, map_preview.draw_map_preview,
                            map_preview.preview_box, map_preview.preview_position, map_preview.write_png], map_digest,
                           map_preview.PREVIEW_SIZE, map_preview.PREVIEW_GROUND, map_preview.PREVIEW_OBSTACLE,
                           map_preview.PREVIEW_WATER, map_preview.PREVIEW_SUPPLY),
                 lambda texture: can_render and assets.get_metadata_tag(texture, map_preview.PREVIEW_SOURCE_TAG) != "Rendered")


def make_map_data(name, display_name, spec, default_seats):
    """default_seats: (seat type, start spot or -1 for random) pairs, at most one per start spot. The preview area and the
    start spot positions are baked from the level (bake_map.py): read now from a level this run leaves as it is, or after
    the rebuild of one it builds again."""
    properties = {
        "display_name": unreal.Text(display_name),
        "default_seats": [make_struct(unreal.RTSSeat, {"type": seat_type, "start_spot": spot})
                          for seat_type, spot in default_seats],
    }
    if not any(entry[0] == spec.path for entry in pending):
        properties.update(bake_map.baked_properties(spec.path, *bake_map.read_layout(spec.path)))
        return plan_data_asset(name, MAP_DATA_DIR, unreal.RTSMapData, properties)

    asset, exists = load_data_asset(name, MAP_DATA_DIR, unreal.RTSMapData)
    path = f"{MAP_DATA_DIR}/{name}"

    def write():
        properties.update(bake_map.baked_properties(spec.path, *bake_map.read_layout(spec.path)))
        for key, value in properties.items():
            asset.set_editor_property(key, value)
        if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {path}")
    pending.append((path, ".uasset", exists, write))
    return asset


def build_menu_map(path, game_mode, digest):
    """An empty level that only shows the menu screen of game_mode."""
    new_level(path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode.static_class())
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {path}")


def make_menu_map(path, game_mode):
    plan_rebuild(path, ".umap", lambda digest: build_menu_map(path, game_mode, digest),
                 spec_hash([build_menu_map], path, game_mode.static_class().get_name()))


def import_ui_file(source, directory, name):
    """Imports a PNG of UI/ as directory/name, replacing the asset there."""
    task = unreal.AssetImportTask()
    task.filename = source.path
    task.destination_path = directory
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    return unreal.load_asset(f"{directory}/{name}")


def import_ui_font_face(source, path):
    face = unreal.RTSEditorScripting.import_font_face(source.path, path)
    if not face:
        raise RuntimeError(f"Could not import {source.path}")
    return face


def import_ui_texture(source, name):
    """A UI PNG as a texture shown pixel for pixel: the UI group, no mips, no compression artefacts."""
    texture = import_ui_file(source, source.asset_directory(), name)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source.path}")
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("srgb", True)
    return texture


def make_ui_texture(source):
    """UI/Textures/<folder>/<name>.png becomes /Game/UI/Textures/<folder>/<name>; a mod's under /Game/UI/Mods/<Mod>."""
    name = source.stem()
    return plan_now(f"{source.asset_directory()}/{name}", lambda: import_ui_texture(source, name),
                    spec_hash([import_ui_file, import_ui_texture], name, file_hash(source.path)))


def build_ui_font(source, face, name):
    path = f"{source.asset_directory()}/{name}"
    font = assets.load_asset(path) if assets.does_asset_exist(path) else asset_tools.create_asset(
        name, source.asset_directory(), unreal.Font, unreal.FontFactory())
    if not unreal.RTSEditorScripting.build_font(font, face):
        raise RuntimeError(f"Could not build {path}")
    return font


def make_ui_font(source):
    """A font file becomes a font face FF_<name> and a runtime font F_<name> with that face as its only typeface."""
    directory = source.asset_directory()
    face_name, font_name = f"FF_{source.stem()}", f"F_{source.stem()}"
    face = plan_now(f"{directory}/{face_name}", lambda: import_ui_font_face(source, f"{directory}/{face_name}"),
                    spec_hash([import_ui_font_face], face_name, file_hash(source.path)))
    return plan_now(f"{directory}/{font_name}", lambda: build_ui_font(source, face, font_name),
                    spec_hash([build_ui_font], font_name, face))


def build_style_class(directory, name, parent, properties):
    """A Blueprint subclass of parent whose class defaults are properties: Common UI reads its styles as classes."""
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        blueprint = assets.load_asset(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        blueprint = asset_tools.create_asset(name, directory, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    defaults = unreal.get_default_object(unreal.BlueprintEditorLibrary.generated_class(blueprint))
    for key, value in properties.items():
        defaults.set_editor_property(key, value)
    return blueprint


def style_value(value):
    """A style property value as text, for the hash of the style class."""
    if isinstance(value, unreal.StructBase):
        return value.export_text()
    if value is None or isinstance(value, unreal.Object):
        return object_path(value)
    return repr(value)


def make_style_class(directory, name, parent, properties):
    """The generated class of the style Blueprint, rebuilt only when its parent or properties change."""
    digest = spec_hash([build_style_class], name, parent.static_class().get_name(),
                       sorted((key, style_value(value)) for key, value in properties.items()))
    blueprint = plan_now(f"{directory}/{name}", lambda: build_style_class(directory, name, parent, properties), digest)
    return unreal.BlueprintEditorLibrary.generated_class(blueprint)


def ui_brush(texture, sprite):
    """The brush of one PNG of a sprite: a 9-slice Box brush when it has a margin, else an image that scales."""
    width, height = sprite["size"]
    brush = unreal.SlateBrush()
    brush.set_editor_property("resource_object", texture)
    size = unreal.DeprecateSlateVector2D()
    size.set_editor_property("x", float(width))
    size.set_editor_property("y", float(height))
    brush.set_editor_property("image_size", size)
    if sprite["margin"]:
        left, top, right, bottom = sprite["margin"]
        brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.BOX)
        brush.set_editor_property("margin", unreal.Margin(left / width, top / height, right / width, bottom / height))
    else:
        brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
    return brush


def make_text_styles(set_name, theme, fonts):
    """TS_<Set>_<Font>_<Color> for every font role and theme colour, by "<Font>.<Color>"."""
    directory = f"{UI_STYLE_DIR}/{set_name}"
    styles = {}
    for role, font in sorted(theme["fonts"].items()):
        for color_name, color in sorted(theme["colors"].items()):
            info = unreal.SlateFontInfo()
            info.set_editor_property("font_object", fonts[font["file"].path])
            info.set_editor_property("typeface_font_name", "Regular")
            info.set_editor_property("size", float(font["size"]) * FONT_POINTS_PER_PIXEL)
            styles[f"{role}.{color_name}"] = make_style_class(directory, f"TS_{set_name}_{role}_{color_name}",
                                                              unreal.CommonTextStyle, {
                "font": info,
                "color": unreal.LinearColor(*ui_sources.srgb_to_linear(color)),
            })
    return styles


def make_ui_style_set(set_name, sprites, theme, text_styles, button_text_styles, textures):
    """DA_UIStyle_<Set> with its sprites' brushes, their BS_ button and BdS_ border style classes, and its theme's
    colours and text styles. button_text_styles are the text styles of the theme its buttons use."""
    directory = f"{UI_STYLE_DIR}/{set_name}"
    entries, button_styles, border_styles = {}, {}, {}
    for name, sprite in sorted(sprites.items()):
        brushes = {state: ui_brush(textures[source.path], sprite) for state, source in sprite["textures"].items()}
        normal = brushes["Normal"]
        entries[name] = make_struct(unreal.RTSUISprite, {
            "brush": normal,
            "tint": sprite["tint"],
            "frames": sprite["frames"],
            "frame_size": unreal.IntPoint(*sprite["frame_size"]),
            "fps": sprite["fps"],
        })
        class_name = name.replace(".", "_")
        if sprite["button"]:
            # A missing state looks like Normal; the text is the theme's Body text, dimmed when disabled.
            text, disabled_text, selected_text = (button_text_styles.get(f"Body.{color}")
                                                  for color in ("Text", "Disabled", "Accent"))
            button_styles[name] = make_style_class(directory, f"BS_{set_name}_{class_name}", unreal.CommonButtonStyle, {
                "normal_base": normal,
                "normal_hovered": brushes.get("Hover", normal),
                "normal_pressed": brushes.get("Pressed", normal),
                "selected_base": brushes.get("Selected", normal),
                "selected_hovered": brushes.get("Selected", normal),
                "selected_pressed": brushes.get("Pressed", normal),
                "disabled": brushes.get("Disabled", normal),
                "normal_text_style": text,
                "normal_hovered_text_style": text,
                "selected_text_style": selected_text,
                "selected_hovered_text_style": selected_text,
                "disabled_text_style": disabled_text,
            })
        elif sprite["margin"]:
            border_styles[name] = make_style_class(directory, f"BdS_{set_name}_{class_name}", unreal.CommonBorderStyle,
                                                   {"background": normal})
    colors = {name: unreal.LinearColor(*ui_sources.srgb_to_linear(color))
              for name, color in sorted(theme["colors"].items())} if theme else {}
    return plan_data_asset(f"DA_UIStyle_{set_name}", UI_STYLE_DIR, unreal.RTSUIStyleSet, {
        "sprites": entries,
        "button_styles": button_styles,
        "border_styles": border_styles,
        "colors": colors,
        "text_styles": text_styles,
    })


def entity_kind(asset, properties):
    """The placeholder_icons kind of an entity, from its data class and the properties planned for it."""
    if isinstance(asset, unreal.RTSUpgradeData):
        return placeholder_icons.UPGRADE
    if isinstance(asset, unreal.RTSBuildingData):
        return placeholder_icons.DEFENSE if properties.get("is_defense") else placeholder_icons.BUILDING
    if isinstance(asset, (unreal.RTSHarvesterData, unreal.RTSBuilderData)):
        return placeholder_icons.WORKER
    if isinstance(asset, unreal.RTSAircraftData):
        return placeholder_icons.AIRCRAFT
    # Vehicles lean with the slope; infantry stays upright.
    return placeholder_icons.VEHICLE if properties.get("tilts_to_ground") else placeholder_icons.INFANTRY


def make_placeholder_icons():
    """A placeholder icon in UI/ for every planned entity whose Icon.<Entity> has no painted art (placeholder_icons.py),
    resolved by the style set of the entity's faction. Runs before make_ui_styles, which imports them."""
    faction_sets = {}
    for asset, _, properties in data_assets.values():
        style = properties.get("ui_style")
        if isinstance(asset, unreal.RTSFactionData) and style and "faction_tag" in properties:
            faction_sets[properties["faction_tag"].export_text()] = style.get_name()[len("DA_UIStyle_"):]
    entities = []
    for asset, _, properties in data_assets.values():
        icon = str(properties.get("icon_sprite", ""))
        if isinstance(asset, unreal.RTSEntityData) and icon.startswith("Icon."):
            tag = properties.get("faction_tag")
            entities.append((icon[len("Icon."):], entity_kind(asset, properties),
                             faction_sets.get(tag.export_text()) if tag else None))
    placeholder_icons.generate(PROJECT_DIR, entities, unreal.log)


def make_ui_styles():
    """The Common UI styles of UI/ and the mods' UI/ folders: a style set per sprite file and theme, plus Common and
    Neutral. A set without its own theme has no colours or text styles, and its buttons use Neutral's text; the
    subsystem falls back to Neutral for those. Problems are logged, and only their entries are skipped. Assets built
    for entries that are gone are deleted by plan_stale_ui_assets."""
    report = ui_sources.Report(PROJECT_DIR)
    sprite_sets = ui_sources.read_sprites(PROJECT_DIR, report)
    themes = ui_sources.read_themes(PROJECT_DIR, report)

    textures = {}
    for sprites in sprite_sets.values():
        for sprite in sprites.values():
            for source in sprite["textures"].values():
                if source.path not in textures:
                    textures[source.path] = make_ui_texture(source)
    fonts = {}
    for theme in themes.values():
        for font in theme["fonts"].values():
            if font["file"].path not in fonts:
                fonts[font["file"].path] = make_ui_font(font["file"])

    text_styles = {name: make_text_styles(name, theme, fonts) for name, theme in sorted(themes.items())}
    for set_name in sorted(set(sprite_sets) | set(themes) | {ui_sources.COMMON, ui_sources.NEUTRAL}):
        own = text_styles.get(set_name, {})
        make_ui_style_set(set_name, sprite_sets.get(set_name, {}), themes.get(set_name), own,
                          own or text_styles.get(ui_sources.NEUTRAL, {}), textures)

    for problem in report.problems:
        unreal.log_warning(f"setup_testmap: {problem}")
    if report.problems:
        unreal.log_warning(f"setup_testmap: {len(report.problems)} UI problems, those entries were skipped")


def is_generated_ui_asset(path):
    """True for an asset under /Game/UI that setup built: it carries the hash tag, or it is a style set, which is
    compared property by property and so has no tag."""
    asset = assets.load_asset(path)
    if assets.get_metadata_tag(asset, HASH_TAG):
        return True
    directory, name = path.rsplit("/", 1)
    return directory == UI_STYLE_DIR and name.startswith("DA_UIStyle_") and isinstance(asset, unreal.RTSUIStyleSet)


def delete_ui_asset(path):
    """Deletes the asset and its file, then the folders under Content/UI that it leaves empty."""
    file = asset_file(path, ".uasset")
    if not assets.delete_asset(path):
        raise RuntimeError(f"Could not delete {path}")
    if os.path.exists(file):
        os.remove(file)
        unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([file], force_rescan=True)
    root = os.path.normpath(os.path.join(CONTENT_DIR, "UI"))
    folder = os.path.normpath(os.path.dirname(file))
    while folder != root and folder.startswith(root) and os.path.isdir(folder) and not os.listdir(folder):
        os.rmdir(folder)
        folder = os.path.dirname(folder)
    unreal.log(f"setup_testmap: deleted {repo_file(path, '.uasset')}, its UI source entry is gone")


def plan_stale_ui_assets():
    """Queues the deletion of the assets under /Game/UI that setup built in an earlier run but did not plan in this
    one, because their sprite, theme, font or mod is gone. Queued after every save, so the style sets no longer refer
    to them when they go. Assets under /Game/UI that setup did not build are left alone."""
    for object_path_name in assets.list_assets("/Game/UI", recursive=True, include_folder=False):
        directory, name = object_path_name.rsplit("/", 1)
        path = f"{directory}/{name.split('.', 1)[0]}"
        if path not in planned and is_generated_ui_asset(path):
            stale.add(path)
            pending.append((path, ".uasset", True, lambda path=path: delete_ui_asset(path)))


def asset_file(path, extension):
    return os.path.join(CONTENT_DIR, path[len("/Game/"):] + extension)


def repo_file(path, extension):
    return os.path.relpath(asset_file(path, extension), PROJECT_DIR).replace("\\", "/")


def skip_worktree_files():
    """Repo files git ignores local changes to (git update-index --skip-worktree), such as a test asset swapped for
    local-only art by import_local_art.py. Empty when git is not available."""
    try:
        listing = subprocess.run(["git", "ls-files", "-v"], cwd=PROJECT_DIR, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return set()
    return {line[2:] for line in listing.stdout.splitlines() if line.startswith("S ")}


def write_pending():
    """Writes every queued asset, or nothing at all when one of the existing files is read-only. Assets marked
    skip-worktree keep their local version."""
    local = skip_worktree_files()
    for path, extension, _, _ in pending:
        if repo_file(path, extension) in local:
            unreal.log_warning(f"setup_testmap: {repo_file(path, extension)} is marked skip-worktree, keeping the local version")
    pending[:] = [entry for entry in pending if repo_file(entry[0], entry[1]) not in local]

    read_only = [asset_file(path, extension) for path, extension, exists, _ in pending
                 if exists and not os.access(asset_file(path, extension), os.W_OK)]
    if read_only:
        files = [os.path.relpath(file, PROJECT_DIR).replace("\\", "/") for file in read_only]
        os.makedirs(os.path.dirname(READ_ONLY_REPORT), exist_ok=True)
        with open(READ_ONLY_REPORT, "w", encoding="utf-8") as report:
            report.write("\n".join(files) + "\n")
        for file in files:
            unreal.log_error(f"setup_testmap: {file} must be rewritten or deleted but is read-only (git lfs lock it)")
        unreal.log_error("setup_testmap: nothing written")
        return

    for _, _, _, write in pending:
        write()
    deleted = sum(1 for path, _, _, _ in pending if path in stale)
    unreal.log(f"setup_testmap: done ({len(pending) - deleted} assets written, {deleted} deleted)")


if os.path.exists(READ_ONLY_REPORT):
    os.remove(READ_ONLY_REPORT)

# Placeholder models (CC0, see Art/Source/Placeholder/CREDITS.md). The Kenney vehicles face +Y once imported; yaw turns
# them to face +X. The tank and the soldier are our own (Tools/Art/build_usa_tank_blender.py, build_soldier_blender.py) and
# already face +X: only their house-colour panels take the full team colour.
OWN_MODEL_TINT = {"Body": 0.15, "Dark": 0.0, "Detail": 0.0, "HouseColor": 0.9}
tank_mesh = make_art("Tank", "Original/usa_mbt.glb", tint=OWN_MODEL_TINT)
infantry_mesh = make_art("Infantry", "Original/infantry_soldier.glb", tint=OWN_MODEL_TINT)
truck_mesh = make_art("Truck", "Kenney/CarKit/delivery.glb", yaw=-90.0)
builder_mesh = make_art("Builder", "Kenney/CarKit/tractor-shovel.glb", yaw=-90.0)
power_plant_mesh = make_art("PowerPlant", "Kenney/CityKitIndustrial/building-m.glb")
supply_depot_mesh = make_art("SupplyDepot", "Kenney/CityKitIndustrial/building-j.glb")
tower_mesh = make_art("Tower", "Kenney/TowerDefenseKit/tower-round-build-c.glb")
barracks_mesh = make_art("Barracks", "Kenney/SurvivalKit/tent-canvas.glb")
vehicle_factory_mesh = make_art("VehicleFactory", "Kenney/CityKitIndustrial/building-l.glb")
headquarters_mesh = make_art("Headquarters", "Kenney/CityKitCommercial/building-skyscraper-a.glb")
wall_mesh = make_art("Wall", "Kenney/CastleKit/wall.glb", tint=0.0)
crate_mesh = make_art("SupplyCrate", "Kenney/FactoryKit/box-wide.glb", tint=0.0)
# The Quaternius jet faces +Y once imported like the Kenney vehicles; the airfield is the crate flattened into a slab.
jet_mesh = make_art("Jet", "Quaternius/Spaceship/spaceship.glb", yaw=-90.0)
airfield_mesh = make_art("Airfield", "Kenney/FactoryKit/box-wide.glb")


def building_fit(mesh, footprint, height):
    """A building's MeshScale: the mesh fills its footprint (1 m cells) less a 5 % border, stretched to the height the
    standard gives that kind of building (WORLD_SCALE_STANDARD.md section 3; rule 2 forbids stretching units only)."""
    return fit_scale(mesh, footprint[0] * 95.0, footprint[1] * 95.0, height)


# Real-scale sizes, speeds and ranges (WORLD_SCALE_STANDARD.md sections 3 and 5, PLACEHOLDER_SCALE_AUDIT.md). Vehicles and
# infantry are real size with a uniform scale: each fit box is the standard's length x width for that class, and the
# placeholder's proportions decide which side binds.
# A defence outranges a standard tank by a quarter, so tanks take fire while they close in.
TANK_RANGE = 6500.0
TOWER_RANGE_OVER_TANK = 1.25

# Shell speed in cm/s, effect scale, shell colour; the grenade lobs. Shell speeds grew with the ranges, so a shot still
# flies 0.2 to 1 s. Vehicle guns flash the Niagara muzzle flash; infantry, defences and jets keep the placeholder glow.
# The tank cannons leave a puff of smoke at the muzzle after each shot. The test tank's gun has a muzzle brake; the M20's
# and the Abrams' have none.
muzzle_flash = make_muzzle_flash()
muzzle_smoke = make_muzzle_smoke()
# Vehicles' engine exhaust (#222): the M20 from its model, the flak truck from a tailpipe placed on its van.
exhaust = make_exhaust()
cannon = make_weapon("DA_TestCannon", TANK_RANGE, 40.0, 2.0, 30000.0, 1.6, (1.0, 0.45, 0.1), muzzle_flash=muzzle_flash,
                     muzzle_smoke=muzzle_smoke, splash_radius=250.0, muzzle_brake=True)
rifle = make_weapon("DA_TestRifle", 3500.0, 8.0, 0.5, 45000.0, 0.4, (1.0, 0.85, 0.3))
# A main battle tank, 9.8 x 3.7 m with its gun forward; the placeholder's long gun lets the width bind.
tank = make_unit("DA_TestTank", "Test Tank", tank_mesh, fit_scale(tank_mesh, 1080.0, 370.0), 200.0, cannon, 1100.0, 75.0,
                 700, 10.0)
# A heavier second tank to try out. It wears the standard tank's model here; on a machine with the local Abrams art,
# import_local_art.py gives it that model and its traversing turret.
heavy_cannon = make_weapon("DA_TestHeavyCannon", TANK_RANGE, 60.0, 2.5, 32500.0, 2.0, (1.0, 0.4, 0.08),
                           muzzle_flash=muzzle_flash, muzzle_smoke=muzzle_smoke, splash_radius=350.0)
abrams = make_unit("DA_TestAbrams", "Test Abrams", tank_mesh, fit_scale(tank_mesh, 1080.0, 380.0), 350.0, heavy_cannon,
                   1000.0, 60.0, 1100, 14.0)
# The M20 Guardian at its real size (7.1 m hull), to try the Blender to Unreal pipeline out: the heavy tank's weapon and
# cost, a light tank's speed and turn rate, and a turret that traverses at 45 degrees a second. No building offers it;
# two stand in L_TestMap.
m20_mesh, m20_parts = make_m20_guardian(exhaust)
m20 = make_unit("DA_TestM20Guardian", "Test M20 Guardian", m20_mesh, (1.0, 1.0, 1.0), 350.0, heavy_cannon, 1250.0, 100.0,
                1100, 14.0)
add_properties(m20, dict(m20_parts, turret_turn_rate=45.0))
# Infantry is 1.8 m tall and narrower than the infantry nav agent (radius 40 cm), rifle and all, so it walks that navmesh.
INFANTRY_HEIGHT = 180.0
infantry = make_unit("DA_TestInfantry", "Test Infantry", infantry_mesh, fit_height(infantry_mesh, INFANTRY_HEIGHT), 100.0,
                     rifle, 450.0, 540.0, 150, 4.0)
grenade = make_weapon("DA_TestGrenade", 3000.0, 25.0, 1.5, 7500.0, 1.0, (1.0, 0.6, 0.2), arc_height=800.0,
                      splash_radius=400.0)
grenadier = make_unit("DA_TestGrenadier", "Test Grenadier", infantry_mesh, fit_height(infantry_mesh, INFANTRY_HEIGHT), 120.0,
                      grenade, 420.0, 540.0, 300, 6.0)
rifle_training = make_upgrade("DA_TestRifleTraining", "Test Rifle Training", 400, 8.0, unreal.RTSUpgradeStat.WEAPON_DAMAGE, 1.5,
                              [infantry])
# The grenadier comes only once the barracks has researched rifle training.
add_properties(grenadier, {"prerequisites": [rifle_training]})
# The tower hits ground and air alike; the flak truck only aircraft in flight, the jet's missiles only the ground.
tower_gun = make_weapon("DA_TestTowerGun", TANK_RANGE * TOWER_RANGE_OVER_TANK, 30.0, 1.0, 35000.0, 1.2, (0.4, 0.8, 1.0),
                        air=True, splash_radius=250.0)
flak = make_weapon("DA_TestFlak", 8000.0, 25.0, 0.6, 40000.0, 0.8, (0.9, 0.9, 0.5), ground=False, air=True,
                   muzzle_flash=muzzle_flash)
# An air-defence truck, 7.0 x 2.5 m; the van's proportions let the width bind.
flak_truck = make_unit("DA_TestFlakTruck", "Test Flak Truck", truck_mesh, fit_scale(truck_mesh, 700.0, 250.0), 250.0,
                       flak, 1400.0, 75.0, 800, 8.0)
# A tailpipe under the van's rear, toward its right side, blowing out backward, in the struct's light diesel grey.
truck_box = truck_mesh.get_bounding_box()
add_properties(flak_truck, {"exhaust": vehicle_exhaust(exhaust, [(
    truck_box.min.x, truck_box.min.y + 0.75 * (truck_box.max.y - truck_box.min.y),
    truck_box.min.z + 0.12 * (truck_box.max.z - truck_box.min.z))], (-1.0, 0.0, 0.0))})
missiles = make_weapon("DA_TestJetMissiles", 5000.0, 60.0, 1.0, 20000.0, 1.2, (1.0, 0.3, 0.2), splash_radius=400.0)

# A fighter compressed to 0.6x, 10 m long with a 7 m span. Four missiles a sortie, eight seconds on its pad to rearm.
jet = make_aircraft("DA_TestJet", "Test Jet", jet_mesh, fit_scale(jet_mesh, 1000.0, 700.0), 150.0, missiles, 4000.0,
                    75.0, 1200, 12.0, 3000.0, 1200.0, 45.0, 4, 8.0)
power_plant = make_building("DA_TestPowerPlant", "Test Power Plant", power_plant_mesh,
                            building_fit(power_plant_mesh, (20, 20), 1400.0), (20, 20), 800.0, None, 800, 6.0, 5, 0)
tower = make_building("DA_TestTower", "Test Tower", tower_mesh, building_fit(tower_mesh, (6, 6), 1100.0), (6, 6), 500.0,
                      tower_gun, 600, 3.0, 0, 2, requires_power=True, defense=True)
# A heavy supply truck, 10 x 2.5 m; the van's proportions let the width bind at about 5.4 m long (accepted in the audit).
harvester = make_harvester("DA_TestHarvester", "Test Harvester", truck_mesh, fit_scale(truck_mesh, 1000.0, 250.0),
                           300.0, 1300.0, 60.0, 600, 8.0, 100, 3.0, 20000.0)
# The depot brings one harvester and produces replacements, so a lost harvester needs no vehicle factory.
supply_depot = make_building("DA_TestSupplyDepot", "Test Supply Depot", supply_depot_mesh,
                             building_fit(supply_depot_mesh, (16, 16), 900.0), (16, 16), 1000.0, None, 1500, 5.0, 0, 1,
                             spawn_on_complete=harvester, accepts_supply=True, units=[harvester])
# An engineering vehicle, 6.5 x 3.0 m.
builder = make_builder("DA_TestBuilder", "Test Builder", builder_mesh, fit_scale(builder_mesh, 650.0, 300.0), 300.0,
                       1100.0, 90.0, 1000, 10.0, [])
barracks = make_building("DA_TestBarracks", "Test Barracks", barracks_mesh, building_fit(barracks_mesh, (16, 16), 700.0),
                         (16, 16), 800.0, None, 500, 5.0, 0, 1, units=[infantry, grenadier], upgrades=[rifle_training])
vehicle_factory = make_building("DA_TestVehicleFactory", "Test Vehicle Factory", vehicle_factory_mesh,
                                building_fit(vehicle_factory_mesh, (24, 24), 1300.0), (24, 24), 1200.0, None, 1200, 8.0, 0, 2,
                                units=[tank, abrams, m20, harvester, flak_truck])
# Four pads of 12 x 24 m side by side on a low slab; each jet it produces parks on one.
airfield = make_building("DA_TestAirfield", "Test Airfield", airfield_mesh, building_fit(airfield_mesh, (24, 48), 100.0),
                         (24, 48), 1500.0, None, 1000, 10.0, 0, 2, units=[jet], airfield_pads=4)
# Every team starts with one at its PlayerStart and may own only one; a team that lost it builds it again.
headquarters = make_building("DA_TestHeadquarters", "Test Headquarters", headquarters_mesh,
                             building_fit(headquarters_mesh, (28, 28), 2200.0), (28, 28), 3000.0, None, 2500, 12.0, 0, 0,
                             units=[builder], max_count=1)
# The tower's spire scaled up. Two minutes to charge; three seconds after launch the strike lands, 80 m across at full
# damage near the centre and a quarter of it at the edge. One per team.
superweapon = make_building("DA_TestSuperweapon", "Test Superweapon", tower_mesh, building_fit(tower_mesh, (24, 24), 3200.0),
                            (24, 24), 2000.0, None, 3000, 30.0, 0, 3, max_count=1,
                            superweapon=(120.0, 3.0, 4000.0, 1500.0, 0.25))
buildings = [power_plant, tower, supply_depot, barracks, vehicle_factory, headquarters, airfield, superweapon]
# The headquarters produces the builder, which builds the rest.
add_properties(builder, {"buildings": buildings})
# Vehicles lean with the slope; infantry stays upright.
for vehicle in (tank, abrams, m20, harvester, builder, flak_truck):
    add_properties(vehicle, {"tilts_to_ground": True})


def locomotion(kind, reverse_speed, acceleration, braking, lateral_acceleration, **geometry):
    """A vehicle's tracks or wheels (FRTSLocomotionProfile, #138): speeds in cm/s, rates in cm/s2, geometry in cm and degrees."""
    profile = unreal.RTSLocomotionProfile()
    profile.set_editor_property("type", kind)
    profile.set_editor_property("reverse_speed", reverse_speed)
    profile.set_editor_property("acceleration", acceleration)
    profile.set_editor_property("braking", braking)
    profile.set_editor_property("max_lateral_acceleration", lateral_acceleration)
    for name, value in geometry.items():
        profile.set_editor_property(name, value)
    return profile


# Tracks drive the tanks: the gauge is the distance between the middles of the tracks, about 0.75 of the hull's width
# (the M20's from its track parts, 264 cm). They reverse at about a third of their top speed and pivot on the spot.
# Tuned to start and stop crisply (#296): the M20 and the tank reach 90 % of top speed in about 1.1 s and stop in 0.5 s,
# the heavier Abrams in about 1.4 s and 0.55 s.
TRACKED = unreal.RTSLocomotionType.TRACKED
add_properties(m20, {"locomotion": locomotion(TRACKED, 450.0, 1000.0, 2500.0, 400.0, track_gauge=264.0)})
add_properties(tank, {"locomotion": locomotion(TRACKED, 400.0, 850.0, 2200.0, 400.0, track_gauge=290.0)})
add_properties(abrams, {"locomotion": locomotion(TRACKED, 350.0, 650.0, 1800.0, 400.0, track_gauge=300.0)})
# Wheels drive the trucks and the builder: 35 degrees of steering on a wheelbase of about 0.6 of the hull's length (the van
# mesh is about 5.4 m long), so the trucks turn no tighter than about 4.7 m and the builder 5.7 m. They reach 90 % of top
# speed in about 1.1 s and stop in about 0.5 s (#296).
WHEELED = unreal.RTSLocomotionType.WHEELED
for truck in (flak_truck, harvester):
    add_properties(truck, {"locomotion": locomotion(WHEELED, 550.0, 1100.0, 2600.0, 350.0, wheelbase=330.0, axle_track=210.0,
                                                    max_steer_angle=35.0, steer_rate=60.0)})
add_properties(builder, {"locomotion": locomotion(WHEELED, 450.0, 1000.0, 2500.0, 350.0, wheelbase=400.0, axle_track=250.0,
                                                  max_steer_angle=35.0, steer_rate=60.0)})
# A harvester needs a depot to deliver to, so the factory only offers one once a depot stands.
add_properties(harvester, {"prerequisites": [supply_depot]})
# The tech tree: vehicles only after infantry, and a tower only once a plant can power it.
add_properties(vehicle_factory, {"prerequisites": [barracks]})
add_properties(airfield, {"prerequisites": [vehicle_factory]})
add_properties(superweapon, {"prerequisites": [vehicle_factory]})
add_properties(tower, {"prerequisites": [power_plant]})
# The AI's base: a second plant before the tower, so the tower's draw never puts it on low power.
build_order = make_build_order("DA_TestBuildOrder", [
    (builder, 2), (power_plant, 1), (supply_depot, 1), (barracks, 1), (infantry, 3), (vehicle_factory, 1), (tank, 2),
    (power_plant, 2), (tower, 1),
], [infantry, tank], 90.0, 8, 4, {
    unreal.RTSAIDifficulty.EASY: (1.5, 0.5),
    unreal.RTSAIDifficulty.HARD: (0.67, 1.5),
})
usa = make_faction("DA_FactionUSA", "USA", buildings, headquarters, ai_build_order=build_order, ui_style="USA")
# Hotkeys are unique per producer: the builder, the headquarters, the barracks, the factory, the depot and the airfield.
# None is a key the controller binds itself (WASD, Q/E, R, X, J, Space, ...): such a key would fall back to 1-9.
USA_TAG = gameplay_tag("Faction.USA")
add_properties(usa, {"faction_tag": USA_TAG})
for entity, icon, hotkey, description in (
    (tank, "Icon.Tank", "T", "Main battle tank. Strong against vehicles and buildings."),
    (abrams, "Icon.Abrams", "M", "Heavy tank. Tougher and harder-hitting than the main battle tank, but slower and dearer."),
    (m20, "Icon.M20Guardian", "N", "M20 Guardian main battle tank at real size, for testing the vehicle art pipeline."),
    (infantry, "Icon.Infantry", "I", "Rifle squad. Cheap, and quick to train."),
    (grenadier, "Icon.Grenadier", "G", "Infantry with a grenade launcher that lobs over cover. Needs rifle training."),
    (flak_truck, "Icon.FlakTruck", "F", "Anti-aircraft truck. Hits only aircraft in flight."),
    (jet, "Icon.Jet", "L", "Strike jet. Fires its missiles at ground targets and rearms on its airfield pad."),
    (harvester, "Icon.Harvester", "H", "Gathers supplies and delivers them to a supply depot."),
    (builder, "Icon.Builder", "B", "Builds and places every building."),
    (rifle_training, "Icon.RifleTraining", "T", "Infantry rifles deal half again as much damage."),
    (power_plant, "Icon.PowerPlant", "P", "Generates the power other buildings draw."),
    (tower, "Icon.Tower", "T", "Defensive gun tower against ground and air. Needs power."),
    (supply_depot, "Icon.SupplyDepot", "U", "Receives supplies from harvesters and comes with one."),
    (barracks, "Icon.Barracks", "B", "Trains infantry and researches rifle training."),
    (vehicle_factory, "Icon.VehicleFactory", "V", "Builds tanks, harvesters and flak trucks."),
    (airfield, "Icon.Airfield", "F", "Builds jets and gives each one a pad to rearm on."),
    (headquarters, "Icon.Headquarters", "H", "The command centre every team starts with. Produces builders."),
    (superweapon, "Icon.Superweapon", "K", "Charges a strike that lands anywhere on the map. One per team."),
):
    present(entity, USA_TAG, icon, hotkey, description)
# The selection panel's hero art (Tools/Art/m20_hero_render.py, then unit_icon.py --art); the others show their icon large.
add_properties(m20, {"hero_art_sprite": unreal.Name("Art.M20Guardian"), "role_text": unreal.Text("Main battle tank")})
make_ghost_material("M_PlacementGhost")
make_shot_material("M_ShotFx")
ground = make_surface_material("M_Ground", (0.06, 0.075, 0.04), 0.9)
water = make_surface_material("M_Water", (0.015, 0.04, 0.08), 0.1)
make_map(MAP_PATH, False, tank, infantry, builder, m20, wall_mesh, crate_mesh, ground)
make_map(NIGHT_MAP_PATH, True, tank, infantry, builder, m20, wall_mesh, crate_mesh, ground)
make_bench_map(ground)
make_long_path_map(ground, water)
make_thin_walls_map(ground)
for spec in (SKIRMISH_MAP, DUEL_MAP, RIVER_FORD_MAP, QUARRY_CROSS_MAP, SIX_PEAKS_MAP):
    make_skirmish_map(spec, builder, wall_mesh, crate_mesh, ground, water)
# The lobby's maps, listed in Config/DefaultGame.ini: by default a player against an AI opponent; the other seats are open.
make_map_data("DA_MapSkirmish", "Test Crossroads", SKIRMISH_MAP,
              [(unreal.RTSSeatType.OPEN, 0), (unreal.RTSSeatType.AI, 1), (unreal.RTSSeatType.CLOSED, -1),
               (unreal.RTSSeatType.CLOSED, -1)])
make_map_data("DA_MapDuel", "Test Duel", DUEL_MAP, [(unreal.RTSSeatType.OPEN, 0), (unreal.RTSSeatType.AI, 1)])
make_map_data("DA_MapRiverFord", "River Ford", RIVER_FORD_MAP, [(unreal.RTSSeatType.OPEN, 0), (unreal.RTSSeatType.AI, 1)])
make_map_data("DA_MapQuarryCross", "Quarry Cross", QUARRY_CROSS_MAP,
              [(unreal.RTSSeatType.OPEN, 0), (unreal.RTSSeatType.AI, 1)] + [(unreal.RTSSeatType.CLOSED, -1)] * 2)
make_map_data("DA_MapSixPeaks", "Six Peaks", SIX_PEAKS_MAP,
              [(unreal.RTSSeatType.OPEN, 0), (unreal.RTSSeatType.AI, 1)] + [(unreal.RTSSeatType.CLOSED, -1)] * 4)
make_menu_map(MAIN_MENU_MAP_PATH, unreal.RTSMainMenuGameMode)
make_menu_map(LOBBY_MAP_PATH, unreal.RTSLobbyGameMode)
make_placeholder_icons()
make_ui_styles()
plan_data_assets()
plan_stale_ui_assets()
write_pending()
