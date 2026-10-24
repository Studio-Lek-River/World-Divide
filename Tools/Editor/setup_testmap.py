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
is written: the files are listed in Saved/Setup/ReadOnlyAssets.txt and the run stops. Lock them (git lfs lock, or ue.ps1
-Task setup -Lock) and rerun. The first such asset is found before it is built, and from then on no existing asset is
rebuilt, so a blocked run skips the slow imports and LOD reduction and still lists every asset it would rewrite.
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
import building_art  # noqa: E402
import map_preview  # noqa: E402
import placeholder_art as art  # noqa: E402
import placeholder_icons  # noqa: E402
import rivercrossing_greybox  # noqa: E402
import ui_sources  # noqa: E402
import vehicle_art  # noqa: E402

UNIT_DIR = "/Game/Data/Units"
WEAPON_DIR = "/Game/Data/Weapons"
BUILDING_DIR = "/Game/Data/Buildings"
# Test buildings that only show kit pieces for building shots (#392), outside BUILDING_DIR so no game list picks them up.
SHOWCASE_DIR = "/Game/Data/Showcase"
UPGRADE_DIR = "/Game/Data/Upgrades"
FACTION_DIR = "/Game/Data/Factions"
AI_DIR = "/Game/Data/AI"
MAP_DATA_DIR = "/Game/Data/Maps"
UI_STYLE_DIR = "/Game/UI/Styles"
# A theme font size is pixels of the em at the 1080p reference; Slate reads points at 96 DPI (RTSLayout::FontPointsPerPixel).
FONT_POINTS_PER_PIXEL = 0.75
MATERIAL_DIR = "/Game/Materials"
EFFECT_DIR = "/Game/Effects"
AUDIO_DIR = "/Game/Audio"
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
# The USA HQ as delivered (#187-#191): its layout usa_hq.json (Tools/Art/usa_hq_recipe.py), and where its assets go.
USA_HQ_SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "USA", "buildings", "usa_hq")
USA_HQ_DIR = "/Game/Art/USA/Buildings/USAHQ"
# Its rubble once destroyed (#403): the delivery, where it goes, how each map imports (its material_recipe.json's
# texture_import) and how each slot draws its normal and detail (its "materials").
USA_HQ_RUBBLE_SOURCE_DIR = os.path.join(USA_HQ_SOURCE_DIR, "rubble")
USA_HQ_RUBBLE_DIR = f"{USA_HQ_DIR}/Rubble"
USA_HQ_RUBBLE_TEXTURES = {
    "BaseColor": {"compression": "TC_Default", "srgb": True, "lod_group": "TEXTUREGROUP_World"},
    "DetailColor": {"compression": "TC_Default", "srgb": False, "lod_group": "TEXTUREGROUP_World"},
    "Normal": {"compression": "TC_Normalmap", "srgb": False, "lod_group": "TEXTUREGROUP_WorldNormalMap"},
    "ORM": {"compression": "TC_Masks", "srgb": False, "lod_group": "TEXTUREGROUP_World"},
    "Emissive": {"compression": "TC_Masks", "srgb": False, "lod_group": "TEXTUREGROUP_World"},
}
USA_HQ_RUBBLE_SLOTS = {
    "Debris": {"Normal_UV": 1, "NormalStrength": 0.85, "DetailColorAmount": 0.65},
    "Scorch": {"Normal_UV": 0, "NormalStrength": 0.4},
    "Embers": {"Normal_UV": 0, "NormalStrength": 0.4},
}
# What every building may use: the flags of Art/Shared/flags/flags.json with their shared masters and DA_FlagTypes (#351),
# and the shared building kit (docs/art/BUILDING_KIT.md, #389), built from Art/Shared/kit/kit_catalogue.json and the
# manifest Tools/Art/kit_blender.py derives from it.
SHARED_SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Shared")
SHARED_DIR = "/Game/Art/Shared"
KIT_SOURCE_DIR = os.path.join(SHARED_SOURCE_DIR, "kit")
KIT_DIR = f"{SHARED_DIR}/Kit"
# The generic building masters every building draws with (#388), and the kit's masters (docs/art/BUILDING_KIT.md).
BUILDING_MATERIAL_DIR = f"{SHARED_DIR}/Materials/Building"
KIT_MATERIAL_DIR = f"{KIT_DIR}/Materials"
KIT_TEXTURE_DIR = f"{KIT_DIR}/Textures"
# The kit masters' default maps (#392): one white source, as colour and as linear masks.
KIT_WHITE_TEXTURES = {
    "T_Kit_White": {"compression": "TC_Default", "srgb": True, "lod_group": "TEXTUREGROUP_World", "resize": None},
    "T_Kit_WhiteMasks": {"compression": "TC_Masks", "srgb": False, "lod_group": "TEXTUREGROUP_World", "resize": None},
}
# A piece's finish (#392): the delivered maps in its folder's textures/, T_<Piece>_<Slot>_<Map>.png, by the master's
# parameter, with their import settings. The Lens slot also has its LightProfile.
KIT_FINISH_MAPS = {
    "BaseColor": ("BaseColor", {"compression": "TC_Default", "srgb": True, "lod_group": "TEXTUREGROUP_World", "resize": None}),
    "ORM": ("ORM", {"compression": "TC_Masks", "srgb": False, "lod_group": "TEXTUREGROUP_World", "resize": None}),
    "FinishNormal": ("Finish_Normal", {"compression": "TC_Normalmap", "srgb": False,
                                       "lod_group": "TEXTUREGROUP_WorldNormalMap", "resize": None}),
    "LightProfile": ("LightProfile", {"compression": "TC_Masks", "srgb": False, "lod_group": "TEXTUREGROUP_World",
                                      "resize": None}),
}
KIT_FINISH_MASTERS = {"Housing": "M_Kit_Housing", "Lens": "M_Kit_Lens"}
# The kit's neutral variant set: MI_Kit_Neutral_<slot> on the master its surface draws with. A piece's slot takes its
# faction's MI_Kit_<faction>_<slot> unless the catalogue names a shared instance for it (materials).
KIT_FACTIONS = {"neutral": "Neutral", "usa": "USA"}
KIT_NEUTRAL_VARIANTS = {"MI_Kit_Neutral_Housing": "M_Bld_Surface", "MI_Kit_Neutral_Lens": "M_Kit_Lens"}
# A kit mesh's LOD1 comes in at a building's switch (docs/art/MODEL_PERFORMANCE.md), cm from the camera.
KIT_SWITCH_DISTANCE = 26000.0
# The kit lamps' night look (#440): their light distribution profiles (building_art.LIGHT_PROFILES) as LP_Kit_<profile>
# in KIT_PROFILE_DIR, their beams (SM_Kit_<Piece>_Beam) and the shared glare halo (SM_Kit_Glare) in KIT_GLOW_DIR on
# M_Kit_Glow's instances, generated into KIT_GLOW_SOURCE_DIR first. A beam flares by its light's cone a quarter of the
# way from inner to outer (KIT_BEAM_CONE: the soft edge stays the light's), a Rect's by KIT_RECT_BEAM_SPREAD; a halo is
# KIT_GLARE_SCALE times its lens's half size.
KIT_PROFILE_DIR = f"{KIT_DIR}/Profiles"
KIT_GLOW_DIR = f"{KIT_DIR}/Glow"
KIT_GLOW_SOURCE_DIR = os.path.join(PROJECT_DIR, "Saved", "Setup", "KitGlow")
KIT_RECT_BEAM_SPREAD = 30.0
KIT_BEAM_CONE = 0.25
KIT_GLARE_SCALE = 1.3
# River Crossing's village set and the bridge span's damage states (#408), in the #431 art (#432): village.json, merged
# from Art/Shared/village/final/, gives every mesh its FBX and hand-made LOD1 beside it in VILLAGE_SOURCE_DIR and the
# recipe of each of its slots. The meshes, the spans' too, go to VILLAGE_MESH_DIR, a DA_Civil_<Type>
# (URTSCivilBuildingData) per type to CIVIL_DIR. Every slot but the glass is an instance of M_Village_Surface
# (building_art.build_village_surface_material) with its scan and the mesh's atlas from VILLAGE_TEXTURE_SOURCE_DIR,
# imported non-streamed (docs/art/MODEL_PERFORMANCE.md rule 6); the glass is an instance of M_Bld_Glass.
VILLAGE_SOURCE_DIR = os.path.join(SHARED_SOURCE_DIR, "village")
VILLAGE_TEXTURE_SOURCE_DIR = os.path.join(VILLAGE_SOURCE_DIR, "final", "textures")
VILLAGE_DIR = f"{SHARED_DIR}/Village"
VILLAGE_MESH_DIR = f"{VILLAGE_DIR}/Meshes"
VILLAGE_MATERIAL_DIR = f"{VILLAGE_DIR}/Materials"
VILLAGE_TEXTURE_DIR = f"{VILLAGE_DIR}/Textures"
CIVIL_DIR = "/Game/Data/Civil"
VILLAGE_MASTER = "M_Village_Surface"
# A village texture's import by the end of its name: colour scans sRGB, the scans' normals as normal maps, everything
# else linear masks (the ORMs and the atlases, whose BakeNormal is object-space data, not a tangent normal).
VILLAGE_TEXTURE_SETTINGS = {
    "BaseColor": {"compression": "TC_Default", "srgb": True, "lod_group": "TEXTUREGROUP_World", "resize": None},
    "Normal": {"compression": "TC_Normalmap", "srgb": False, "lod_group": "TEXTUREGROUP_WorldNormalMap", "resize": None},
    "Masks": {"compression": "TC_Masks", "srgb": False, "lod_group": "TEXTUREGROUP_World", "resize": None},
}
# The barn's roof scan repeats in visible blotches (#432): its colour is flattened (FlattenScan) in every state.
VILLAGE_FLATTENED_SLOTS = {("SM_Village_Barn", "Roof"), ("SM_Village_Barn_Damaged", "Roof"),
                           ("SM_Village_Barn_Rubble", "Roof")}
FLAG_DATA_DIR = "/Game/Data/Flags"
FLAG_TEXTURE_SETTINGS = {"srgb": True, "compression": "TC_Default", "lod_group": "TEXTUREGROUP_World", "resize": None}
# The garage door's cycle as delivered (animation/CT_HQ20_DoorMotion.csv), a curve table the garage plays (#192).
USA_HQ_DOOR_MOTION = "CT_HQ20_DoorMotion"
# The bench's dense HQ (#349), built only with ue.ps1 -Task setup -BenchHQ (WD_SETUP_BENCH_HQ=1) and never committed
# (Content/Bench/ is git-ignored): the delivered shell with Nanite, beside the production HQ's LOD0 + LOD1 shell (#358).
BENCH_HQ = os.environ.get("WD_SETUP_BENCH_HQ") == "1"
BENCH_HQ_DIR = "/Game/Bench/USAHQ"

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

# Repo paths of the existing assets this run must rewrite but cannot because they are read-only. Once one is found, the run
# writes nothing, so plan_now no longer rebuilds existing assets.
read_only = []

# Repo files marked skip-worktree, read before the first asset is planned; see skip_worktree_files.
skip_worktree = set()

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
    if isinstance(desired, unreal.Transform):
        # A rotator turned into a quaternion can carry -0.0, which its text keeps.
        return any(differs(getattr(current, part), getattr(desired, part)) for part in ("translation", "scale3d")) or             any(differs(getattr(current.rotation, axis), getattr(desired.rotation, axis)) for axis in "xyzw")
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


def blocked_by(path, extension):
    """True when the existing file of an asset that must be rewritten is read-only; records it in read_only."""
    file = repo_file(path, extension)
    if file in skip_worktree or os.access(asset_file(path, extension), os.W_OK):
        return False
    if file not in read_only:
        read_only.append(file)
    return True


def plan_now(path, build, digest):
    """Builds an asset in memory right away, so assets planned after it can use it, and queues its save. Returns the
    asset on disk instead when it was built from the same hash, or when the run writes nothing anyway because an asset
    it must rewrite is read-only."""
    planned.add(path)
    exists = assets.does_asset_exist(path)
    if exists:
        asset = assets.load_asset(path)
        if assets.get_metadata_tag(asset, HASH_TAG) == digest:
            return asset
        if blocked_by(path, ".uasset") or read_only:
            unreal.log_warning(f"setup_testmap: not rebuilding {path}, the run is blocked by read-only assets")
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


def make_m20_guardian(exhaust_system, dust_system):
    """Imports the M20 Guardian's parts, textures and material instances (see vehicle_art.py) and returns its unit data
    properties: the hull as Mesh at real size, the turret, the gun and every other part where m20_guardian.json puts it,
    the exhaust (exhaust_system) at its grille, the dust (dust_system) behind its tracks and the lights at its lamps. The
    wheels of the right side are the exported left wheel turned round."""
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
        # World in Conflict's big, snappy stroke (#353): 0.3 of the barrel, 87 cm, a third of it outside the mantlet, back in a
        # frame, held to 0.1 s and home at 0.5 s; the fume extractor stops 7 cm short of the boot's clamp ring. The gun's jump
        # stays small (#218); the hull rocks a degree and bounces back past rest by a third of it before it settles, enough to
        # read at close zoom (#376).
        "gun_recoil": gun_recoil(0.3, 0.015, 0.085, 0.4, jump_degrees=0.3, jump_seconds=0.3, hull_rock_degrees=1.0,
                                 hull_rock_seconds=1.0, hull_rock_damping=0.35),
        # Its hull on its torsion bars with the drive (#297): about a degree back pulling away at 10 m/s² and leaning out of
        # its tightest turn at 4 m/s², at most 1.2 degrees diving under its 25 m/s² brakes; the turret trails a 45°/s
        # traverse by 1.5 degrees.
        "body_motion": body_motion(0.1, 0.25, 1.2, turret_lag_degrees=1.5),
        "parts": parts,
        # Out of the rear grille, along its louvres (#222), from a diesel (#325).
        "exhaust": vehicle_exhaust(exhaust_system, layout["exhaust_offsets"], layout["exhaust_direction"], DIESEL_EXHAUST),
        # Its four headlights in the nose clusters and its two red tail lamps on the rear sponsons (#223).
        "lights": vehicle_lights(layout["headlight_offsets"], layout["taillight_offsets"]),
        # Where each track leaves the ground behind the sprocket (#224): the sprocket's middle less its radius, on the ground
        # (z 0 is the ground under the hull's middle in the layout), in the line of the track.
        "dust": vehicle_dust(dust_system, [
            (layout["wheels"][f"M20_Sprocket_{side}"][0] - layout["roll_radii"]["Sprocket"],
             layout["track_offsets"][f"M20_Track_{side}"][1], 0.0) for side in ("L", "R")]),
    }


# The generic building masters (building_art.py, #388) in BUILDING_MATERIAL_DIR: name -> how to build it from the shared
# textures (and the kit's white masks, the baked occlusion's default, #373).
BUILDING_MASTERS = {
    "M_Bld_Surface": lambda d, n, t: building_art.build_surface_material(d, n, "surface", t["T_Kit_WhiteMasks"]),
    "M_Bld_Glass": lambda d, n, t: building_art.build_surface_material(d, n, "glass", t["T_Kit_WhiteMasks"]),
    "M_Bld_Operational": lambda d, n, t: building_art.build_surface_material(d, n, "operational", t["T_Kit_WhiteMasks"]),
    "M_Bld_Ownership": lambda d, n, t: building_art.build_surface_material(d, n, "ownership", t["T_Kit_WhiteMasks"]),
    "M_Bld_Coating": lambda d, n, t: building_art.build_coating_material(d, n, {
        "CoatingBaseColor": t["T_Bld_Coating_2m_BaseColor"], "CoatingRoughness": t["T_Bld_Coating_2m_Roughness"],
        "CoatingNormal": t["T_Bld_Coating_2m_Normal"]}, t["T_Kit_WhiteMasks"]),
}
# The 2 m coating M_Bld_Coating samples: the USA HQ's delivered textures (usa_hq.json's settings), imported as the shared
# T_Bld_Coating_2m_<map>.
BUILDING_TEXTURES = {f"T_Bld_Coating_2m_{map_name}": f"T_HQ17_Coating_2m_{map_name}"
                     for map_name in ("BaseColor", "Normal", "Roughness")}
# usa_hq.json's parents as delivered -> the generic master each one now is.
USA_HQ_SHARED_PARENTS = {
    "M_HQ17_Surface": "M_Bld_Surface",
    "M_HQ17_Glass": "M_Bld_Glass",
    "M_HQ_Final_Operational": "M_Bld_Operational",
    "M_HQ_Final_Ownership": "M_Bld_Ownership",
    "M_HQ17_Coating": "M_Bld_Coating",
}
# The USA HQ's own masters (building_art.py): name -> how to build it from the HQ's textures.
USA_HQ_MASTERS = {
    "M_HQ20_FloorWear": lambda d, n, t: building_art.build_ground_material(
        d, n, t["T_HQ20_Floor_BaseColor"], t["T_HQ20_Floor_Roughness"], False),
    "M_HQ21_PavingWear": lambda d, n, t: building_art.build_ground_material(
        d, n, t["T_HQ21_Paving_BaseColor"], t["T_HQ21_Paving_Roughness"], False),
    "M_HQ21_Soil": lambda d, n, t: building_art.build_ground_material(
        d, n, t["T_HQ21_Soil_BaseColor"], t["T_HQ21_Soil_Normal"], True),
}


def log_building_budget(label, meshes, layout, mounted=()):
    """Logs a building's triangles at LOD0 and LOD1 over every part that draws (each placement counts) and every mounted
    kit piece (mounted, a mesh per mount), its material sections over the components it draws with (one per mesh several
    parts share, which draw instanced) and its switch distance (#358)."""
    totals = [0, 0]
    sections = 0
    placements = [(meshes[name], len([p for p in layout["parts"] if p["mesh"] == name])) for name in sorted(meshes)]
    placements += [(mesh, sum(1 for m in mounted if m == mesh)) for mesh in dict.fromkeys(mounted)]
    for mesh, count in placements:
        for lod in (0, 1):
            totals[lod] += count * mesh.get_num_triangles(min(lod, mesh.get_num_lods() - 1))
        sections += mesh.get_num_sections(0) if count else 0
    unreal.log(f"setup_testmap: {label} LOD0 {totals[0]} and LOD1 {totals[1]} triangles, {sections} material sections, "
               f"LOD1 from {layout['switch_distance'] / 100:.0f} m")


def delete_stale_assets(directory, names):
    """Deletes the assets under directory that are not in names: those a layout no longer has (the delivered shell meshes
    SM_HQ_Shell replaced, #358, the delivered flag and its materials, #351). Nothing setup builds refers to them once it is
    rebuilt."""
    for object_path_name in assets.list_assets(directory, recursive=False, include_folder=False):
        path = object_path_name.split(".", 1)[0]
        if path.rsplit("/", 1)[-1] not in names:
            if blocked_by(path, ".uasset") or read_only:
                continue
            stale.add(path)
            pending.append((path, ".uasset", True, lambda path=path: delete_asset(path)))


def delete_asset(path):
    if not assets.delete_asset(path):
        raise RuntimeError(f"Could not delete {path}")
    # A data asset's file can outlive its package (as delete_ui_asset found).
    file = asset_file(path, ".uasset")
    if os.path.exists(file):
        os.remove(file)
        unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([file], force_rescan=True)
    unreal.log(f"setup_testmap: deleted {path}, which the layout no longer has")


def make_flags():
    """The shared flags (#351): T_Flag_<key> and MI_Flag_<key> in SHARED_DIR/Flags for every flag of flags.json, on
    M_Flag_Cloth in SHARED_DIR/Materials, and DA_FlagTypes, which lists each flag's instance and aspect. Returns the
    instances by name and DA_FlagTypes."""
    flags_dir, material_dir = f"{SHARED_DIR}/Flags", f"{SHARED_DIR}/Materials"
    flags = building_art.read_layout(os.path.join(SHARED_SOURCE_DIR, "flags", "flags.json"))["flags"]
    textures = {}
    for key, record in sorted(flags.items()):
        source = os.path.join(SHARED_SOURCE_DIR, "flags", record["texture"])
        name = f"T_Flag_{key}"
        textures[key] = plan_now(
            f"{flags_dir}/{name}",
            lambda s=source, n=name: building_art.import_texture(s, flags_dir, n, FLAG_TEXTURE_SETTINGS),
            spec_hash([building_art.import_texture], flags_dir, name, sorted(FLAG_TEXTURE_SETTINGS.items()), file_hash(source)))

    # The master's default texture is the first flag's, so it draws a flag without an instance too.
    default = textures[sorted(textures)[0]]
    cloth = plan_now(
        f"{material_dir}/M_Flag_Cloth", lambda: building_art.build_flag_material(material_dir, "M_Flag_Cloth", default),
        spec_hash([building_art.build_flag_material, building_art._seed, building_art._material, building_art._scalar,
                   building_art._multiply, vehicle_art._custom, vehicle_art._transform], material_dir,
                  sorted(building_art.FLAG_PARAMETERS.items()), building_art.FLAG_WPO_HLSL,
                  sorted(building_art.FLAG_WPO_INPUTS.items()), default))
    # The mast beacon's master and instance, unused since it blinks on the kit lens (#393, #438).
    delete_stale_assets(material_dir, {"M_Flag_Cloth"})
    instances = {}

    types = {}
    for key, record in sorted(flags.items()):
        name = f"MI_Flag_{key}"
        instances[name] = plan_now(
            f"{flags_dir}/{name}",
            lambda n=name, t=textures[key]: building_art.build_flag_instance(flags_dir, n, cloth, t),
            spec_hash([building_art.build_flag_instance], flags_dir, name, cloth, textures[key]))
        flag = unreal.RTSFlagType()
        flag.set_editor_property("material", instances[name])
        hoist, fly = record["ratio"]
        flag.set_editor_property("aspect", fly / hoist)
        types[key] = flag
    flag_types = plan_data_asset("DA_FlagTypes", FLAG_DATA_DIR, unreal.RTSFlagTypes, {"flags": types})
    return instances, flag_types


def lens_face(light, lens):
    """The width and height (cm) of the lens a light shines through, across its Y and along its Z once turned."""
    pitch, yaw, roll = light.get("rotation", [0.0, 0.0, 0.0])
    rotation = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    size = [high - low for low, high in zip(*lens)]
    right, up = ([v.x, v.y, v.z] for v in (rotation.get_right_vector(), rotation.get_up_vector()))
    return sum(s * abs(r) for s, r in zip(size, right)), sum(s * abs(u) for s, u in zip(size, up))


def kit_light(light, lens, profile=None):
    """A kit piece's FRTSKitLight from its catalogue light and the manifest's lens bounds (min, max, cm): turned by its
    rotation (+X by default), it stands on the face of the lens it shines through, in the middle of it; a Rect is as wide
    and high as the lens across its Y and Z, a Point or Spot's source half its smaller side (#440). profile: its light
    distribution profile, if any."""
    pitch, yaw, roll = light.get("rotation", [0.0, 0.0, 0.0])
    rotation = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    forward = rotation.get_forward_vector()
    forward = [forward.x, forward.y, forward.z]
    centre = [(low + high) / 2.0 for low, high in zip(*lens)]
    size = [high - low for low, high in zip(*lens)]
    axis = max(range(3), key=lambda i: abs(forward[i]))
    location = list(centre)
    # A flat lens (07's panel, #393): a millimetre in front of it, so it still shines out of it.
    location[axis] += math.copysign(max(size[axis] / 2.0, 0.1), forward[axis])
    value = unreal.RTSKitLight()
    value.set_editor_property("type", getattr(unreal.RTSBuildingLightType, light["type"].upper()))
    value.set_editor_property("location", unreal.Vector(*location))
    value.set_editor_property("rotation", rotation)
    inner, outer = light.get("cone", [0.0, 0.0])
    for key, setting in (("candelas", light["candela"]), ("kelvin", light["kelvin"]), ("radius", light["radius"]),
                         ("inner_cone_angle", inner), ("outer_cone_angle", outer),
                         ("max_draw_distance", light["max_draw_distance"])):
        value.set_editor_property(key, float(setting))
    width, height = lens_face(light, lens)
    if light["type"] == "Rect":
        value.set_editor_property("source_width", width)
        value.set_editor_property("source_height", height)
    else:
        value.set_editor_property("source_radius", min(width, height) / 2.0)
    value.set_editor_property("profile", profile)
    value.set_editor_property("group", light["group"])
    value.set_editor_property("behaviour", getattr(unreal.RTSKitLightBehaviour, light["behaviour"].upper()))
    value.set_editor_property("priority", getattr(unreal.RTSKitLightPriority, light["priority"].upper()))
    return value


def make_kit_glow_parts(masters, kept):
    """What every kit lamp's night look shares (#440): M_Kit_Glow's instances (building_art.GLOW_INSTANCES), the glare
    halo SM_Kit_Glare on MI_Kit_Glare, and each light profile as it is first asked for (glow_profile). Adds what it makes
    to kept."""
    os.makedirs(KIT_GLOW_SOURCE_DIR, exist_ok=True)
    parts = {"profiles": {}}
    for name, scalars in sorted(building_art.GLOW_INSTANCES.items()):
        parts[name] = plan_now(
            f"{KIT_MATERIAL_DIR}/{name}",
            lambda n=name, v=scalars: building_art.build_material_instance(KIT_MATERIAL_DIR, n, masters["M_Kit_Glow"], v, {}),
            spec_hash([building_art.build_material_instance], KIT_MATERIAL_DIR, name, sorted(scalars.items()),
                      masters["M_Kit_Glow"]))
        kept.add(f"{KIT_MATERIAL_DIR}/{name}")
    source = os.path.join(KIT_GLOW_SOURCE_DIR, "SM_Kit_Glare.glb")
    building_art.write_glare_glb(source)
    parts["glare"] = plan_now(
        f"{KIT_GLOW_DIR}/SM_Kit_Glare",
        lambda: vehicle_art.import_part(source, KIT_GLOW_DIR, "SM_Kit_Glare", {"Glow": parts["MI_Kit_Glare"]}, 2.0),
        spec_hash([building_art.write_glare_glb, building_art._write_glb, vehicle_art.import_part], KIT_GLOW_DIR,
                  parts["MI_Kit_Glare"]))
    kept.add(f"{KIT_GLOW_DIR}/SM_Kit_Glare")
    return parts


def glow_profile(parts, name, kept):
    """LP_Kit_<Name>, building_art.LIGHT_PROFILES[name] as a light profile, imported once a run."""
    if name not in parts["profiles"]:
        asset = f"LP_Kit_{name.capitalize()}"
        source = os.path.join(KIT_GLOW_SOURCE_DIR, f"{asset}.ies")
        building_art.write_ies(source, name)
        parts["profiles"][name] = plan_now(
            f"{KIT_PROFILE_DIR}/{asset}",
            lambda: building_art.import_light_profile(source, KIT_PROFILE_DIR, asset),
            spec_hash([building_art.import_light_profile], KIT_PROFILE_DIR, asset, file_hash(source)))
        kept.add(f"{KIT_PROFILE_DIR}/{asset}")
    return parts["profiles"][name]


def make_kit_glow(piece, lens, parts, kept):
    """A piece's night look (#440) from its catalogue entry: its light's "profile", its "beam" ({"strength", "length"
    cm}: SM_Kit_<Piece>_Beam, building_art.write_beam_glb, from its lens's face along its light) and its "glare" (the
    halo's strength). Returns the URTSKitPieceData values; none for what it does not have."""
    light = piece.get("light")
    result = {"profile": None, "beam_mesh": None, "beam_strength": 0.0, "glare_mesh": None, "glare_radius": 0.0,
              "glare_strength": 0.0}
    if light and "profile" in light:
        if light["type"] == "Rect":
            raise RuntimeError(f"kit_catalogue.json: {piece['id']}'s Rect light takes no profile")
        result["profile"] = glow_profile(parts, light["profile"], kept)
    beam = piece.get("beam")
    if beam:
        if not light or light["type"] not in ("Spot", "Rect"):
            raise RuntimeError(f"kit_catalogue.json: {piece['id']} has a beam but no Spot or Rect light")
        width, height = lens_face(light, lens)
        inner, outer = light.get("cone", [0.0, 0.0])
        spread = KIT_RECT_BEAM_SPREAD if light["type"] == "Rect" else inner + KIT_BEAM_CONE * (outer - inner)
        name = f"{piece['mesh']}_Beam"
        source = os.path.join(KIT_GLOW_SOURCE_DIR, f"{name}.glb")
        building_art.write_beam_glb(source, light["type"], width, height, beam["length"], spread)
        result["beam_mesh"] = plan_now(
            f"{KIT_GLOW_DIR}/{name}",
            lambda: vehicle_art.import_part(source, KIT_GLOW_DIR, name, {"Glow": parts["MI_Kit_Beam"]}, beam["length"]),
            spec_hash([building_art.write_beam_glb, building_art._write_glb, vehicle_art.import_part], KIT_GLOW_DIR,
                      name, file_hash(source), parts["MI_Kit_Beam"]))
        result["beam_strength"] = float(beam["strength"])
        kept.add(f"{KIT_GLOW_DIR}/{name}")
    if "glare" in piece:
        if not lens:
            raise RuntimeError(f"kit_catalogue.json: {piece['id']} has a glare but no lens")
        size = [high - low for low, high in zip(*lens)]
        result["glare_mesh"] = parts["glare"]
        result["glare_radius"] = KIT_GLARE_SCALE * max(size) / 2.0
        result["glare_strength"] = float(piece["glare"])
    return result


def make_kit_finish(piece, masters, kept):
    """A piece's own finish (#392, kit_catalogue.json's "finish"): per slot it names, the delivered maps of its folder's
    textures/ (KIT_FINISH_MAPS) as T_Kit_<Piece>_<Slot>_<Map> in KIT_TEXTURE_DIR and MI_Kit_<Piece>_<Slot> in
    KIT_MATERIAL_DIR on the slot's kit master (KIT_FINISH_MASTERS), white, Roughness and Metallic 1 so the maps rule, and
    the slot's values from the catalogue (a number a scalar, a list a colour, true or false a static switch: Glass,
    Sweep, #393). Adds what it makes to kept and returns the
    instances by slot; {} for a piece without a finish."""
    short = piece["mesh"][len("SM_Kit_"):]
    folder = os.path.join(KIT_SOURCE_DIR, *piece["id"].split("/"), "textures")
    slots = {}
    for slot, values in sorted(piece.get("finish", {}).items()):
        textures = {}
        for parameter, (suffix, settings) in sorted(KIT_FINISH_MAPS.items()):
            source = os.path.join(folder, f"T_{short}_{slot}_{suffix}.png")
            if not os.path.exists(source):
                if parameter != "LightProfile" or slot == "Lens":
                    raise RuntimeError(f"kit_catalogue.json: {piece['id']}'s {slot} finish has no {source}")
                continue
            name = f"T_Kit_{short}_{slot}_{suffix.replace('_', '')}"
            textures[parameter] = plan_now(
                f"{KIT_TEXTURE_DIR}/{name}",
                lambda s=source, n=name, t=settings: building_art.import_texture(s, KIT_TEXTURE_DIR, n, t),
                spec_hash([building_art.import_texture], KIT_TEXTURE_DIR, name, sorted(settings.items()), file_hash(source)))
            kept.add(f"{KIT_TEXTURE_DIR}/{name}")
        scalars = {"Roughness": 1.0, "Metallic": 1.0}
        vectors = {"SurfaceColor": (1.0, 1.0, 1.0, 1.0)}
        switches = {}
        for key, value in values.items():
            if isinstance(value, bool):
                switches[key] = value
            elif isinstance(value, list):
                vectors[key] = tuple(value) + (1.0,) * (4 - len(value))
            else:
                scalars[key] = float(value)
        name = f"MI_Kit_{short}_{slot}"
        parent = masters[KIT_FINISH_MASTERS[slot]]
        slots[slot] = plan_now(
            f"{KIT_MATERIAL_DIR}/{name}",
            lambda n=name, p=parent, t=textures, s=scalars, v=vectors, w=switches: building_art.build_kit_finish_instance(
                KIT_MATERIAL_DIR, n, p, t, s, v, w),
            spec_hash([building_art.build_kit_finish_instance], KIT_MATERIAL_DIR, name, object_path(parent),
                      sorted((k, object_path(t)) for k, t in textures.items()), sorted(scalars.items()),
                      sorted(vectors.items()), sorted(switches.items()), building_art.GLASS))
        kept.add(f"{KIT_MATERIAL_DIR}/{name}")
    return slots


def make_kit(masters):
    """The shared building kit (docs/art/BUILDING_KIT.md, #389) and the flags (make_flags): every piece of
    kit_catalogue.json as SM_Kit_<Piece> (building_art.import_kit_mesh) and the generated DA_Kit_<Piece>
    (URTSKitPieceData) in KIT_DIR/<Category>, from the catalogue and the manifest kit_blender.py derived from it, its slots
    on the instances the catalogue names or its faction's variant set; the neutral set is built here on masters (by name,
    make_building_masters). A piece the catalogue no longer has is deleted. Returns the meshes and the data assets by
    catalogue id, and DA_FlagTypes."""
    instances, flag_types = make_flags()
    for name, parent in sorted(KIT_NEUTRAL_VARIANTS.items()):
        instances[name] = plan_now(
            f"{KIT_MATERIAL_DIR}/{name}",
            lambda n=name, p=masters[parent]: building_art.build_material_instance(KIT_MATERIAL_DIR, n, p, {}, {}),
            spec_hash([building_art.build_material_instance], KIT_MATERIAL_DIR, name, masters[parent]))

    catalogue = building_art.read_layout(os.path.join(KIT_SOURCE_DIR, "kit_catalogue.json"))["pieces"]
    manifest = building_art.read_layout(os.path.join(KIT_SOURCE_DIR, "build", "kit_manifest.json"))["pieces"]
    if sorted(p["id"] for p in catalogue) != sorted(manifest):
        raise RuntimeError(f"kit_manifest.json has {sorted(manifest)}, the catalogue {sorted(p['id'] for p in catalogue)}: "
                           "rerun Tools/Art/kit_blender.py")
    meshes, pieces, kept = {}, {}, set()
    glow_parts = make_kit_glow_parts(masters, kept)
    for piece in catalogue:
        built = manifest[piece["id"]]
        if built["mesh"] != piece["mesh"] or built["slots"] != piece["slots"]:
            raise RuntimeError(f"kit_manifest.json's {piece['id']} is not the catalogue's: rerun Tools/Art/kit_blender.py")
        category = piece["id"].split("/")[0]
        directory = f"{KIT_DIR}/{category.capitalize()}"
        variant = KIT_FACTIONS[piece["faction"]]
        slots = {slot: instances[piece.get("materials", {}).get(slot, f"MI_Kit_{variant}_{slot}")]
                 for slot in piece["slots"]}
        slots.update(make_kit_finish(piece, masters, kept))
        source = os.path.join(KIT_SOURCE_DIR, *built["fbx"].split("/"))
        lod1 = os.path.join(KIT_SOURCE_DIR, *built["lod1_fbx"].split("/")) if built["lod1_fbx"] else None
        mesh = plan_now(
            f"{directory}/{piece['mesh']}",
            lambda s=source, d=directory, n=piece["mesh"], m=slots, b=built, l=lod1: building_art.import_kit_mesh(
                s, d, n, m, b["bounds"], b["triangles"], KIT_SWITCH_DISTANCE, l),
            spec_hash([building_art.import_kit_mesh, building_art._reset_lods, vehicle_art.import_part,
                       vehicle_art.lod_radius], directory, piece["mesh"], file_hash(source),
                      file_hash(lod1) if lod1 else None, sorted((slot, object_path(m)) for slot, m in slots.items()),
                      built["bounds"], built["triangles"], KIT_SWITCH_DISTANCE, vehicle_art.SCREEN_MULTIPLE))
        meshes[piece["id"]] = mesh
        # A waving piece (the flag) keeps waving until it has faded out (#351).
        if "Flag" in piece["slots"] and piece.get("wpo_disable_distance", 0.0) < building_art.FLAG_PARAMETERS["FlagFadeEnd"]:
            raise RuntimeError(f"kit_catalogue.json: {piece['id']}'s wpo_disable_distance is inside M_Flag_Cloth's fade")

        lens = built["lens_bounds"]
        glow = make_kit_glow(piece, lens, glow_parts, kept)
        mount_points = {}
        for name, point in piece["mount_points"].items():
            pitch, yaw, roll = point["rotation"]
            mount_points[name] = unreal.Transform(unreal.Vector(*point["location"]),
                                                  unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
        # No lens: an invalid box, which Python's Box() is not.
        lens_bounds = unreal.Box(unreal.Vector(*lens[0]), unreal.Vector(*lens[1])) if lens else unreal.Box()
        lens_bounds.set_editor_property("is_valid", bool(lens))
        data = f"DA_Kit_{piece['mesh'][len('SM_Kit_'):]}"
        pieces[piece["id"]] = plan_data_asset(data, directory, unreal.RTSKitPieceData, {
            "id": piece["id"],
            "mesh": mesh,
            "class_": getattr(unreal.RTSKitClass, piece["class"]),
            "faction": piece["faction"],
            "has_light": "light" in piece,
            "light": kit_light(piece["light"], lens, glow["profile"]) if "light" in piece else unreal.RTSKitLight(),
            "beam_mesh": glow["beam_mesh"],
            "beam_strength": glow["beam_strength"],
            "glare_mesh": glow["glare_mesh"],
            "glare_radius": glow["glare_radius"],
            "glare_strength": glow["glare_strength"],
            "lens_bounds": lens_bounds,
            "emergency_default": piece["emergency_default"],
            "fault_response": getattr(unreal.RTSKitFaultResponse, piece["fault_response"].upper()),
            "mount_points": mount_points,
            "budget_lod0": piece["budgets"]["lod0"],
            "budget_lod1": piece["budgets"]["lod1"],
            "wpo_disable_distance": float(piece.get("wpo_disable_distance", 0.0)),
        })
        kept |= {f"{directory}/{piece['mesh']}", f"{directory}/{data}"}

    # A piece the catalogue dropped: its mesh, data, finish, beam and unused profiles go, wherever its category was.
    kept |= {f"{KIT_TEXTURE_DIR}/{name}" for name in KIT_WHITE_TEXTURES}
    for object_path_name in assets.list_assets(KIT_DIR, recursive=True, include_folder=False):
        path = object_path_name.split(".", 1)[0]
        name = path.rsplit("/", 1)[-1]
        finish = (path.startswith(f"{KIT_TEXTURE_DIR}/T_Kit_")
                  or (path.startswith(f"{KIT_MATERIAL_DIR}/MI_Kit_") and name.rsplit("_", 1)[-1] in KIT_FINISH_MASTERS
                      and name not in instances))
        if (name.startswith(("SM_Kit_", "DA_Kit_", "LP_Kit_")) or finish) and path not in kept:
            if blocked_by(path, ".uasset") or read_only:
                continue
            stale.add(path)
            pending.append((path, ".uasset", True, lambda path=path: delete_asset(path)))
    return meshes, pieces, flag_types


def make_building_masters():
    """The generic building masters every building draws with (#388): BUILDING_MASTERS in BUILDING_MATERIAL_DIR with the
    2 m coating they sample (BUILDING_TEXTURES) in its Textures folder, and the kit's M_Kit_Lens and M_Kit_Housing (#392)
    in KIT_MATERIAL_DIR, their default maps (KIT_WHITE_TEXTURES) in KIT_TEXTURE_DIR. Returns the masters by name."""
    hq_textures = building_art.read_layout(os.path.join(USA_HQ_SOURCE_DIR, "usa_hq.json"))["textures"]
    texture_dir = f"{BUILDING_MATERIAL_DIR}/Textures"
    textures = {}
    for name, delivered in sorted(BUILDING_TEXTURES.items()):
        source = os.path.join(USA_HQ_SOURCE_DIR, "textures", f"{delivered}.png")
        settings = hq_textures[delivered]
        textures[name] = plan_now(
            f"{texture_dir}/{name}",
            lambda s=source, n=name, t=settings: building_art.import_texture(s, texture_dir, n, t),
            spec_hash([building_art.import_texture], texture_dir, name, sorted(settings.items()), file_hash(source)))

    # The kit's masters draw a piece's own maps where its catalogue entry has a finish (#392), white and flat without; the
    # building masters' baked occlusion is white without its map (#373).
    white_source = os.path.join(KIT_SOURCE_DIR, "atlases", "T_Kit_White.png")
    white = {}
    for name, settings in sorted(KIT_WHITE_TEXTURES.items()):
        white[name] = plan_now(
            f"{KIT_TEXTURE_DIR}/{name}",
            lambda n=name, t=settings: building_art.import_texture(white_source, KIT_TEXTURE_DIR, n, t),
            spec_hash([building_art.import_texture], KIT_TEXTURE_DIR, name, sorted(settings.items()),
                      file_hash(white_source)))
    builders = [building_art.build_surface_material, building_art.build_coating_material, building_art._material,
                building_art._vector, building_art._scalar, building_art._multiply, building_art._add,
                building_art._shell_bake]
    constants = (vehicle_art.TEAM_COLOR_INDEX, vehicle_art.LAMP_ON_INDEX, building_art.COATING_ROUGHNESS_MEAN,
                 building_art.COATING_ROUGHNESS_RANGE, sorted(building_art.LIGHT_GROUP_INDICES.items()),
                 building_art.LIGHT_NIGHT_INDEX, building_art.SHELL_BAKE, building_art.FLAT_NORMAL)
    sources = dict(textures, **white)
    masters = {}
    for name, build in BUILDING_MASTERS.items():
        masters[name] = plan_now(
            f"{BUILDING_MATERIAL_DIR}/{name}", lambda b=build, n=name: b(BUILDING_MATERIAL_DIR, n, sources),
            spec_hash(builders + [build], BUILDING_MATERIAL_DIR, name, constants,
                      sorted(map(object_path, sources.values()))))
    kit_builders = [building_art._kit_surface, building_art._sweep, building_art._seed, building_art._material,
                    building_art._vector, building_art._scalar, building_art._multiply, building_art._constant,
                    vehicle_art._custom, vehicle_art._transform, vehicle_art._mask]
    kit_constants = (building_art.SWEEP, building_art.SWEEP_HLSL, building_art.GLASS, building_art.DITHER_FUNCTION)
    defaults = (white["T_Kit_White"], white["T_Kit_WhiteMasks"])
    masters["M_Kit_Lens"] = plan_now(
        f"{KIT_MATERIAL_DIR}/M_Kit_Lens", lambda: building_art.build_lens_material(KIT_MATERIAL_DIR, "M_Kit_Lens", *defaults),
        spec_hash(kit_builders + [building_art.build_lens_material], KIT_MATERIAL_DIR, constants, kit_constants,
                  building_art.LENS_HLSL, building_art.LENS_SHAPE_HLSL, sorted(building_art.LENS_BLINK.items()),
                  building_art.FLAT_NORMAL, sorted(map(object_path, defaults)),
                  (building_art.LENS_LEVEL, building_art.LENS_GROUP, building_art.LENS_FAULT, building_art.LENS_FLASH,
                   building_art.LENS_COLOR)))
    masters["M_Kit_Glow"] = plan_now(
        f"{KIT_MATERIAL_DIR}/M_Kit_Glow", lambda: building_art.build_glow_material(KIT_MATERIAL_DIR, "M_Kit_Glow"),
        spec_hash([building_art.build_glow_material, building_art._material, building_art._scalar,
                   building_art._multiply, vehicle_art._custom, vehicle_art._mask], KIT_MATERIAL_DIR,
                  building_art.GLOW_HLSL, sorted(building_art.GLOW_INSTANCES["MI_Kit_Beam"].items()),
                  (building_art.GLOW_LEVEL, building_art.GLOW_FAULT, building_art.GLOW_FLASH, building_art.GLOW_COLOR)))
    masters["M_Kit_Housing"] = plan_now(
        f"{KIT_MATERIAL_DIR}/M_Kit_Housing",
        lambda: building_art.build_kit_housing_material(KIT_MATERIAL_DIR, "M_Kit_Housing", *defaults),
        spec_hash(kit_builders + [building_art.build_kit_housing_material], KIT_MATERIAL_DIR, kit_constants,
                  building_art.FLAT_NORMAL, sorted(map(object_path, defaults))))
    delete_stale_assets(texture_dir, set(textures))
    delete_stale_assets(BUILDING_MATERIAL_DIR, set(BUILDING_MASTERS))
    return masters


def make_usa_hq():
    """Imports the USA HQ (building_art.py) as usa_hq.json lays it out and returns its root mesh, its footprint and its
    building data properties: the architecture is Mesh at real size, every other mesh a part where the delivery placed
    it with the garage door closed, the collision boxes go around its blocks with the garage bay a box of its own, and
    the sockets give the garage's spawn and exit. The flag cloth and mast beacons (#351) are the shared kit's pieces
    (make_kit, #389), mounted from the layout's "mounts" (#390), and the whole building switches to LOD1 at its switch
    distance as one (LODSwitchDistance); the flag flies the layout's flag from DA_FlagTypes, and the parts that turn have
    their motion."""
    layout = building_art.read_layout(os.path.join(USA_HQ_SOURCE_DIR, "usa_hq.json"))
    shared_masters = make_building_masters()
    kit_meshes, kit_pieces, flag_types = make_kit(shared_masters)
    texture_dir, material_dir, mesh_dir = (f"{USA_HQ_DIR}/{folder}" for folder in ("Textures", "Materials", "Meshes"))

    # The coating is the shared masters' since #388.
    own_textures = {name: settings for name, settings in layout["textures"].items()
                    if name not in BUILDING_TEXTURES.values()}
    textures = {}
    for name, settings in sorted(own_textures.items()):
        source = os.path.join(USA_HQ_SOURCE_DIR, "textures", f"{name}.png")
        textures[name] = plan_now(
            f"{texture_dir}/{name}",
            lambda s=source, n=name, t=settings: building_art.import_texture(s, texture_dir, n, t),
            spec_hash([building_art.import_texture], texture_dir, name, sorted(settings.items()), file_hash(source)))

    builders = [building_art.build_ground_material, building_art._material]
    masters = {}
    for name, build in USA_HQ_MASTERS.items():
        masters[name] = plan_now(
            f"{material_dir}/{name}", lambda b=build, n=name: b(material_dir, n, textures),
            spec_hash(builders + [build], material_dir, name, building_art.SOIL_ROUGHNESS,
                      sorted(map(object_path, textures.values()))))
    parents = dict(masters, **{delivered: shared_masters[shared] for delivered, shared in USA_HQ_SHARED_PARENTS.items()})

    # The baked shell's instances (#373) also set its maps and the masters' SHELL_BAKE. A blocked run writes nothing and
    # has not rebuilt the masters, which may not have the switch yet, so it leaves the switches out.
    instances = {}
    for name, record in sorted(layout["material_instances"].items()):
        parent = parents[record["parent"]]
        maps = {parameter: textures[texture] for parameter, texture in record.get("textures", {}).items()}
        switches = record.get("switches", {})
        instances[name] = plan_now(
            f"{material_dir}/{name}",
            lambda n=name, p=parent, r=record, m=maps, w=switches: building_art.build_material_instance(
                material_dir, n, p, r["scalars"], r["vectors"], m, {} if read_only else w),
            spec_hash([building_art.build_material_instance], material_dir, name, parent,
                      sorted(record["scalars"].items()), sorted(record["vectors"].items()),
                      sorted((parameter, object_path(texture)) for parameter, texture in maps.items()),
                      sorted(switches.items())))

    # Every mesh's placements in the building, the root (the shell) where the building stands: each mesh's bounds cover
    # the building's from each of them, so the whole building switches to LOD1 at once (#358).
    def transform(record):
        pitch, yaw, roll = record["rotation"]
        return unreal.Transform(unreal.Vector(*record["location"]), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw),
                                unreal.Vector(*record["scale"]))

    building_box = layout["meshes"][layout["parts"][0]["mesh"]]["bounds"]
    if layout["parts"][0]["name"] != layout["root_part"]:
        raise RuntimeError(f"usa_hq.json: the root part {layout['root_part']} is not the first part")
    placements = {}
    for record in layout["parts"]:
        placements.setdefault(record["mesh"], []).append(record)

    meshes = {}
    for name, record in sorted(layout["meshes"].items()):
        source = os.path.join(USA_HQ_SOURCE_DIR, *record["fbx"].split("/"))
        lod1 = os.path.join(USA_HQ_SOURCE_DIR, *record["lod1_fbx"].split("/")) if record.get("lod1_fbx") else None
        slots = {slot: instances[instance] for slot, instance in record["slots"].items()}
        where = placements[name]
        meshes[name] = plan_now(
            f"{mesh_dir}/{name}",
            lambda s=source, n=name, m=slots, r=record, w=where, l=lod1: building_art.import_mesh(
                s, mesh_dir, n, m, r["bounds"], r["triangles"], r["lods"], building_box, [transform(p) for p in w],
                layout["switch_distance"], l, instances),
            spec_hash([building_art.import_mesh, building_art._reset_lods, building_art.cover_building,
                       building_art._rejoin_lod_slots, vehicle_art.import_part, vehicle_art.lod_radius], mesh_dir, name,
                      file_hash(source),
                      file_hash(lod1) if lod1 else None,
                      sorted((slot, object_path(material)) for slot, material in slots.items()), record["bounds"],
                      record["triangles"], record["lods"], building_box, [(p["location"], p["rotation"], p["scale"])
                                                                          for p in where],
                      layout["switch_distance"], vehicle_art.SCREEN_MULTIPLE))
    unknown = {m["piece"] for m in layout["mounts"]} - set(kit_pieces)
    if unknown:
        raise RuntimeError(f"usa_hq.json mounts pieces the kit catalogue does not have: {sorted(unknown)}")
    log_building_budget("USA HQ", meshes, layout, [kit_meshes[m["piece"]] for m in layout["mounts"]])
    # Its own meshes only: the flag cloth and beacons it imported before the kit (#389) go too.
    delete_stale_assets(mesh_dir, set(layout["meshes"]))
    # The delivered flag's master and colour instances, which the shared cloth replaced (#351), and the generic masters and
    # their coating, which are shared since #388.
    delete_stale_assets(material_dir, set(masters) | set(instances))
    delete_stale_assets(texture_dir, set(textures))

    def part(record):
        value = unreal.RTSBuildingPart()
        value.set_editor_property("name", record["name"])
        value.set_editor_property("mesh", meshes[record["mesh"]])
        pitch, yaw, roll = record["rotation"]
        value.set_editor_property("transform", unreal.Transform(
            unreal.Vector(*record["location"]), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw),
            unreal.Vector(*record["scale"])))
        value.set_editor_property("movable", record["movable"])
        value.set_editor_property("cast_shadow", record["cast_shadow"])
        value.set_editor_property("bounds_scale", record["bounds_scale"])
        value.set_editor_property("max_draw_distance", record["max_draw_distance"])
        value.set_editor_property("wpo_disable_distance", record["wpo_disable_distance"])
        motion = unreal.RTSPartMotion()
        if record["motion"]:
            m = record["motion"]
            motion.set_editor_property("spin_rate", unreal.Rotator(
                roll=m["spin_rate"][2], pitch=m["spin_rate"][0], yaw=m["spin_rate"][1]))
            motion.set_editor_property("sweep_amplitude", unreal.Rotator(
                roll=m["sweep_amplitude"][2], pitch=m["sweep_amplitude"][0], yaw=m["sweep_amplitude"][1]))
            motion.set_editor_property("sweep_period", m["sweep_period"])
            motion.set_editor_property("pivot_offset", unreal.Vector(*m["pivot_offset"]))
            motion.set_editor_property("follow_part", m["follow_part"])
            motion.set_editor_property("rate_jitter", m["rate_jitter"])
        value.set_editor_property("motion", motion)
        return value

    def mount(record):
        # A kit piece on the building (docs/art/BUILDING_KIT.md, Mounts): at its socket, if any, times its transform.
        value = unreal.RTSKitMount()
        value.set_editor_property("name", record["name"])
        value.set_editor_property("piece", kit_pieces[record["piece"]])
        value.set_editor_property("socket", record.get("socket") or "None")
        location, (pitch, yaw, roll), scale = record.get("transform", [[0.0] * 3, [0.0] * 3, [1.0] * 3])
        value.set_editor_property("transform", unreal.Transform(
            unreal.Vector(*location), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw), unreal.Vector(*scale)))
        # What the state does to it (BUILDING_KIT.md, States, #391); a mount that does not say keeps its piece's.
        if "emergency" in record:
            value.set_editor_property("emergency", unreal.RTSKitOverride.ON if record["emergency"] else unreal.RTSKitOverride.OFF)
        if "fault" in record:
            value.set_editor_property("override_fault_response", True)
            value.set_editor_property("fault_response", getattr(unreal.RTSKitFaultResponse, record["fault"].upper()))
        if "group" in record:
            value.set_editor_property("group", record["group"])
        if "color" in record:
            value.set_editor_property("indicator_color", unreal.LinearColor(*record["color"], 1.0))
        # Brighter or dimmer than its catalogue piece on this building alone (#453).
        if "brightness" in record:
            value.set_editor_property("brightness", record["brightness"])
        return value

    def collision_box(record):
        value = unreal.RTSBuildingCollisionBox()
        value.set_editor_property("name", record["name"])
        value.set_editor_property("min", unreal.Vector(*record["min"]))
        value.set_editor_property("max", unreal.Vector(*record["max"]))
        value.set_editor_property("garage", record["garage"])
        return value

    def socket(name, location):
        value = unreal.RTSBuildingSocket()
        value.set_editor_property("name", name)
        value.set_editor_property("location", unreal.Vector(*location))
        return value

    def light(record):
        # A real light at one of the HQ's lamps (#195), placed from the probed fixtures by usa_hq_recipe.py.
        value = unreal.RTSBuildingLight()
        value.set_editor_property("name", record["name"])
        value.set_editor_property("group", record["group"])
        value.set_editor_property("fixture", record["fixture"])
        value.set_editor_property("type", getattr(unreal.RTSBuildingLightType, record["type"].upper()))
        value.set_editor_property("location", unreal.Vector(*record["location"]))
        pitch, yaw, roll = record["rotation"]
        value.set_editor_property("rotation", unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
        value.set_editor_property("color", unreal.LinearColor(*record["color"], 1.0))
        for key in ("candelas", "radius", "inner_cone_angle", "outer_cone_angle", "source_width", "source_height",
                    "barn_door_angle", "barn_door_length"):
            if key in record:
                value.set_editor_property(key, record[key])
        return value

    door_source = os.path.join(USA_HQ_SOURCE_DIR, "animation", f"{USA_HQ_DOOR_MOTION}.csv")
    door_motion = plan_now(f"{USA_HQ_DIR}/Animation/{USA_HQ_DOOR_MOTION}",
                           lambda: import_curve_table(door_source, f"{USA_HQ_DIR}/Animation", USA_HQ_DOOR_MOTION),
                           spec_hash([import_curve_table], USA_HQ_DOOR_MOTION, file_hash(door_source)))

    root = next(p for p in layout["parts"] if p["name"] == layout["root_part"])
    return meshes[root["mesh"]], tuple(layout["footprint_cells"]), {
        "parts": [part(p) for p in layout["parts"] if p is not root],
        "collision_boxes": [collision_box(b) for b in layout["collision_boxes"]],
        "sockets": [socket(name, location) for name, location in sorted(layout["sockets"].items())],
        "lights": [light(l) for l in layout["lights"]],
        "mounts": [mount(m) for m in layout["mounts"]],
        "lod_switch_distance": float(layout["switch_distance"]),
        # Its class for the light budget (RTSKit::GetLightBudget, #391).
        "size": getattr(unreal.RTSBuildingSize, layout.get("size", "Medium").upper()),
        # The delivered 24 s door cycle opens from 2 to 10 s; the game plays that in GarageDoorSeconds (#192).
        "garage_door_motion": door_motion,
        "garage_door_open_start": 2.0,
        "garage_door_open_end": 10.0,
        "garage_door_seconds": 3.0,
        "flag_types": flag_types,
        "flag_type": layout["flag"],
        "flag_part": layout["flag_part"],
        "ruin_mesh": make_usa_hq_rubble(layout["switch_distance"]),
    }


def make_usa_hq_rubble(switch_distance):
    """The destroyed HQ's rubble (#403, Art/USA/buildings/usa_hq/rubble): SM_HQ_Rubble with its hand-made LOD1 in the
    shell's frame, switching at the HQ's switch distance, its three slots drawn by masters built from material_recipe.json
    (building_art.build_rubble_material), in USA_HQ_RUBBLE_DIR. ARTSBuildingRuin shows it once the HQ has collapsed."""
    recipe = building_art.read_layout(os.path.join(USA_HQ_RUBBLE_SOURCE_DIR, "material_recipe.json"))
    record = building_art.read_layout(os.path.join(USA_HQ_RUBBLE_SOURCE_DIR, "evidence", "verification.json"))["source"]
    materials = {}
    for slot in recipe["slots"]:
        maps = recipe["maps"][slot]["maps"]
        textures = {}
        for key, name in sorted(maps.items()):
            source = os.path.join(USA_HQ_RUBBLE_SOURCE_DIR, "textures", name)
            settings = dict(USA_HQ_RUBBLE_TEXTURES[key], resize=None)
            asset = os.path.splitext(name)[0]
            textures[key] = plan_now(
                f"{USA_HQ_RUBBLE_DIR}/{asset}",
                lambda s=source, a=asset, t=settings: building_art.import_texture(s, USA_HQ_RUBBLE_DIR, a, t),
                spec_hash([building_art.import_texture], USA_HQ_RUBBLE_DIR, asset, sorted(settings.items()),
                          file_hash(source)))
        settings = dict(USA_HQ_RUBBLE_SLOTS[slot], **{k: v for k, v in recipe["materials"][slot].items()
                                                      if k.startswith("emissive_")})
        materials[slot] = plan_now(
            f"{USA_HQ_RUBBLE_DIR}/M_HQ_Rubble_{slot}",
            lambda n=f"M_HQ_Rubble_{slot}", t=textures, r=settings: building_art.build_rubble_material(
                USA_HQ_RUBBLE_DIR, n, t, r),
            spec_hash([building_art.build_rubble_material, building_art._material], USA_HQ_RUBBLE_DIR, slot,
                      sorted(settings.items()), sorted((k, object_path(t)) for k, t in textures.items())))

    source = os.path.join(USA_HQ_RUBBLE_SOURCE_DIR, "SM_HQ_Rubble.fbx")
    lod1 = os.path.join(USA_HQ_RUBBLE_SOURCE_DIR, "SM_HQ_Rubble_LOD1.fbx")
    lod0_record, lod1_record = record["SM_HQ_Rubble"], record["SM_HQ_Rubble_LOD1"]
    bounds = lod0_record["bounds_unreal_cm"]
    return plan_now(
        f"{USA_HQ_RUBBLE_DIR}/SM_HQ_Rubble",
        lambda: building_art.import_mesh(
            source, USA_HQ_RUBBLE_DIR, "SM_HQ_Rubble", materials, bounds, lod0_record["triangles"],
            (lod0_record["triangles"], lod1_record["triangles"]), bounds, [unreal.Transform()], switch_distance, lod1),
        spec_hash([building_art.import_mesh, building_art._reset_lods, building_art.cover_building,
                   vehicle_art.import_part, vehicle_art.lod_radius], USA_HQ_RUBBLE_DIR, file_hash(source),
                  file_hash(lod1), switch_distance, sorted((s, object_path(m)) for s, m in materials.items())))


def import_village_texture(source, name):
    """One of the village's textures, with the settings of its kind, kept in memory (never streamed)."""
    kind = name.rsplit("_", 1)[-1]
    texture = building_art.import_texture(source, VILLAGE_TEXTURE_DIR, name,
                                          VILLAGE_TEXTURE_SETTINGS[kind if kind in ("BaseColor", "Normal") else "Masks"])
    texture.set_editor_property("never_stream", True)
    return texture


def make_village():
    """River Crossing's village set (#408) in the #431 art (#432): every mesh of Art/Shared/village/village.json imported
    as the bridge's are (rivercrossing_greybox.import_bridge_mesh: UCX_ collision, checked against the record; no Nanite)
    with its hand-made LOD1 from KIT_SWITCH_DISTANCE (building_art.attach_lod1), its slots on their instances, and a
    DA_Civil_<Type> per building with its three state meshes, health and garrison data. Returns the data by type."""
    glass_master = unreal.load_asset(f"{BUILDING_MATERIAL_DIR}/M_Bld_Glass")
    if not glass_master:
        raise RuntimeError("M_Bld_Glass is missing from the building masters")
    village = building_art.read_layout(os.path.join(VILLAGE_SOURCE_DIR, "village.json"))

    names = set()
    for record in village["meshes"].values():
        for recipe in record["materials"].values():
            if "atlas" in recipe:
                names.update(f"{recipe['atlas']}_{map}" for map in ("Tint", "BakeNormal", "BakeAO"))
            if "scan" in recipe:
                names.update(f"{recipe['scan']}_{map}" for map in ("BaseColor", "Normal", "ORM"))
    textures = {}
    for name in sorted(names):
        source = os.path.join(VILLAGE_TEXTURE_SOURCE_DIR, f"{name}.png")
        textures[name] = plan_now(
            f"{VILLAGE_TEXTURE_DIR}/{name}", lambda s=source, n=name: import_village_texture(s, n),
            spec_hash([import_village_texture, building_art.import_texture], VILLAGE_TEXTURE_DIR, name,
                      VILLAGE_TEXTURE_SETTINGS, file_hash(source)))

    first = next(iter(village["meshes"]))
    defaults = {"BaseColor": textures["Bricks085_BaseColor"], "Normal": textures["Bricks085_Normal"],
                "ORM": textures["Bricks085_ORM"], "AtlasTint": textures[f"{first}_Tint"],
                "BakeNormal": textures[f"{first}_BakeNormal"], "BakeAO": textures[f"{first}_BakeAO"]}
    master = plan_now(
        f"{VILLAGE_MATERIAL_DIR}/{VILLAGE_MASTER}",
        lambda: building_art.build_village_surface_material(VILLAGE_MATERIAL_DIR, VILLAGE_MASTER, defaults),
        spec_hash([building_art.build_village_surface_material, building_art._material, building_art._vector,
                   building_art._scalar, building_art._multiply, building_art._add],
                  VILLAGE_MATERIAL_DIR, sorted((p, object_path(t)) for p, t in defaults.items())))

    def instance(mesh, slot, recipe):
        name = f"MI_{mesh}_{slot}"
        switches = {}
        if recipe.get("glass"):
            parent, maps = glass_master, {}
            scalars, vectors = {"GlassOpacity": recipe["opacity"]}, {"SurfaceColor": (*recipe["color"], 1.0)}
        else:
            parent = master
            scalars = {"UseScan": 1.0 if "scan" in recipe else 0.0, "BakeStrength": recipe["bake"]}
            vectors = {"SurfaceTint": (*recipe["tint"], 1.0)}
            if "scan_uv_scale" in recipe:
                vectors["ScanUVScale"] = (*recipe["scan_uv_scale"], 1.0, 1.0)
            maps = {map: textures[f"{recipe['atlas']}_{suffix}"]
                    for map, suffix in (("AtlasTint", "Tint"), ("BakeNormal", "BakeNormal"), ("BakeAO", "BakeAO"))}
            if "scan" in recipe:
                maps.update({map: textures[f"{recipe['scan']}_{map}"] for map in ("BaseColor", "Normal", "ORM")})
            if (mesh, slot) in VILLAGE_FLATTENED_SLOTS:
                switches["FlattenScan"] = True
        return plan_now(
            f"{VILLAGE_MATERIAL_DIR}/{name}",
            lambda: building_art.build_material_instance(VILLAGE_MATERIAL_DIR, name, parent, scalars, vectors, maps,
                                                         switches),
            spec_hash([building_art.build_material_instance], VILLAGE_MATERIAL_DIR, name, parent, sorted(scalars.items()),
                      sorted(vectors.items()), sorted((p, object_path(t)) for p, t in maps.items()),
                      sorted(switches.items())))

    def import_village_mesh(source, name, record, bound, directory, lod1):
        mesh = rivercrossing_greybox.import_bridge_mesh(source, name, record, bound, directory, nanite=False)
        slots = [str(slot.material_slot_name) for slot in mesh.static_materials]
        if slots != record["slots"]:
            raise RuntimeError(f"{source} imported the slots {slots}, not {record['slots']}")
        return building_art.attach_lod1(mesh, lod1, (record["triangles"], record["lod1"]["triangles"]),
                                        KIT_SWITCH_DISTANCE)

    meshes = {}
    for name, record in village["meshes"].items():
        directory = VILLAGE_MESH_DIR
        source = os.path.join(VILLAGE_SOURCE_DIR, record["fbx"])
        lod1 = os.path.join(VILLAGE_SOURCE_DIR, record["lod1"]["fbx"])
        bound = {slot: instance(name, slot, recipe) for slot, recipe in record["materials"].items()}
        meshes[name] = plan_now(
            f"{directory}/{name}",
            lambda s=source, n=name, r=record, b=bound, d=directory, l=lod1: import_village_mesh(s, n, r, b, d, l),
            spec_hash([import_village_mesh, rivercrossing_greybox.import_bridge_mesh, building_art.attach_lod1,
                       building_art._reset_lods, building_art._rejoin_lod_slots], directory, name, file_hash(source),
                      file_hash(lod1), KIT_SWITCH_DISTANCE, record,
                      sorted((slot, object_path(m)) for slot, m in bound.items())))
    meshes["SM_Bridge_Span"] = unreal.load_asset(f"{rivercrossing_greybox.BRIDGE_DIR}/SM_Bridge_Span")
    # The greybox (#408): its meshes, in VILLAGE_DIR and beside SM_Bridge_Span, and its flat MI_Village_<slot>. The art
    # imports anew in VILLAGE_MESH_DIR, since a reimport over the greybox keeps its slots and binds the art's sections to
    # them out of order.
    delete_stale_assets(VILLAGE_DIR, set())
    bridge = {path.split(".", 1)[0].rsplit("/", 1)[-1]
              for path in assets.list_assets(rivercrossing_greybox.BRIDGE_DIR, recursive=False, include_folder=False)}
    delete_stale_assets(rivercrossing_greybox.BRIDGE_DIR, bridge - set(village["meshes"]))

    civil = {}
    for key, building in village["buildings"].items():
        pristine, damaged, rubble = (meshes[name] for name in building["states"])
        civil[key] = plan_data_asset(f"DA_Civil_{key}", CIVIL_DIR, unreal.RTSCivilBuildingData, {
            "display_name": unreal.Text(building["name"]),
            "pristine_mesh": pristine,
            "damaged_mesh": damaged,
            "rubble_mesh": rubble,
            "max_health": float(building["health"]),
            "garrison_capacity": building["capacity"],
            "entry_point": unreal.Vector(*building["entry"]),
            "firing_positions": [make_struct(unreal.RTSGarrisonFiringPosition, {
                "name": unreal.Name(position["name"]),
                "location": unreal.Vector(*position["location"]),
                "yaw": float(position["yaw"]),
                "arc_degrees": float(position["arc"]),
                # A belfry goes with the top of the church's tower (#447), an upper window with its floor; a gap in the
                # ruins' wall stubs comes with the ruins (#449).
                "first_state": getattr(unreal.RTSCivilBuildingState, position.get("first_state", "Pristine").upper()),
                "last_state": getattr(unreal.RTSCivilBuildingState, position.get("last_state", "Rubble").upper()),
            }) for position in building["firing_positions"]],
        })
    return civil


def import_curve_table(source, directory, name):
    """A curve table from a CSV of one row per curve and one column per second (the HQ's door cycle), keys linear."""
    factory = unreal.CSVImportFactory()
    settings = factory.get_editor_property("automated_import_settings")
    settings.set_editor_property("import_type", unreal.CSVImportType.ECSV_CURVE_TABLE)
    settings.set_editor_property("import_curve_interp_mode", unreal.RichCurveInterpMode.RCIM_LINEAR)
    factory.set_editor_property("automated_import_settings", settings)
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = directory
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = factory
    asset_tools.import_asset_tasks([task])
    table = unreal.load_asset(f"{directory}/{name}")
    if not isinstance(table, unreal.CurveTable):
        raise RuntimeError(f"Could not import {source} as a curve table")
    return table


def make_bench_hq(footprint, layout):
    """The bench's dense HQ (#349, rts.Debug.Bench @NaniteHQ): DA_BenchHQ_Nanite in BENCH_HQ_DIR, the production HQ's data
    (footprint, collision boxes, sockets, parts in place; layout as make_usa_hq returns it) with the game shell swapped
    for the delivered meshes it stands for (usa_hq.json's delivery_shell), as SM_BenchHQ_Nanite_<part>, Nanite on, as
    delivered. They draw with copies of the HQ's masters that allow Nanite, and draw the glass opaque,
    MI_BenchHQ_CoolGlass and MI_BenchHQ_WarmGlass on the surface master with the delivery's tints, since Nanite draws no
    translucency. The moving parts and the flag stay the production meshes. The LOD shell #349 benched beside it is the
    production HQ since #358.
    The bench HQ produces nothing."""
    hq = building_art.read_layout(os.path.join(USA_HQ_SOURCE_DIR, "usa_hq.json"))
    material_dir, mesh_dir = f"{BENCH_HQ_DIR}/Materials", f"{BENCH_HQ_DIR}/Meshes"
    delivered = hq["delivery_shell"]["meshes"]
    shell = hq["delivery_shell"]["parts"]

    # The shell's masters and instances again in BENCH_HQ_DIR, the masters with the Nanite usage (building_art.allow_nanite),
    # which the production ones lack. The glass is opaque on the surface master.
    # The copies keep usa_hq.json's names; the generic ones build as BUILDING_MASTERS, from the shared coating (#388).
    textures = {name: unreal.load_asset(f"{USA_HQ_DIR}/Textures/{name}") for name in hq["textures"]
                if name not in BUILDING_TEXTURES.values()}
    textures.update({name: unreal.load_asset(f"{BUILDING_MATERIAL_DIR}/Textures/{name}") for name in BUILDING_TEXTURES})
    glass = {"MI_HQ17_CoolGlass": "MI_BenchHQ_CoolGlass", "MI_HQ17_WarmGlass": "MI_BenchHQ_WarmGlass"}
    used = sorted({instance for p in shell for instance in delivered[p["mesh"]]["slots"].values()})
    parents = sorted({"M_HQ17_Surface" if name in glass else hq["material_instances"][name]["parent"] for name in used})
    masters = {}
    for name in parents:
        build = USA_HQ_MASTERS[name] if name in USA_HQ_MASTERS else BUILDING_MASTERS[USA_HQ_SHARED_PARENTS[name]]
        masters[name] = plan_now(
            f"{material_dir}/{name}", lambda b=build, n=name: building_art.allow_nanite(b(material_dir, n, textures)),
            spec_hash([building_art.allow_nanite, build, building_art.build_surface_material,
                       building_art.build_coating_material, building_art.build_ground_material], material_dir, name,
                      sorted(map(object_path, textures.values()))))
    instances = {}
    for production in used:
        record = hq["material_instances"][production]
        name = glass.get(production, production)
        parent = masters["M_HQ17_Surface" if production in glass else record["parent"]]
        scalars = {key: value for key, value in record["scalars"].items() if key != "GlassOpacity"}
        instances[production] = plan_now(
            f"{material_dir}/{name}",
            lambda n=name, p=parent, s=scalars, v=record["vectors"]: building_art.build_material_instance(material_dir, n, p, s, v),
            spec_hash([building_art.build_material_instance], material_dir, name, parent, sorted(scalars.items()),
                      sorted(record["vectors"].items())))

    meshes = {}
    for part in shell:
        record = delivered[part["mesh"]]
        source = os.path.join(USA_HQ_SOURCE_DIR, *record["fbx"].split("/"))
        slots = {slot: instances[instance] for slot, instance in record["slots"].items()}
        name = f"SM_BenchHQ_Nanite_{part['name']}"
        meshes[part["name"]] = plan_now(
            f"{mesh_dir}/{name}",
            lambda s=source, n=name, m=slots, r=record: building_art.import_bench_shell(
                s, mesh_dir, n, m, r["bounds"], r["triangles"]),
            spec_hash([building_art.import_bench_shell, building_art._reset_lods, vehicle_art.import_part], mesh_dir,
                      name, file_hash(source), sorted((slot, object_path(material)) for slot, material in slots.items()),
                      record["bounds"], record["triangles"]))

    def bench_part(name, mesh):
        value = unreal.RTSBuildingPart()
        value.set_editor_property("name", name)
        value.set_editor_property("mesh", mesh)
        return value

    root = "Architecture"
    data = make_building("DA_BenchHQ_Nanite", "Bench HQ (Nanite)", meshes[root], (1.0, 1.0, 1.0), footprint, 3000.0, None,
                         2500, 12.0, 0, 0, directory=BENCH_HQ_DIR)
    parts = [bench_part(name, mesh) for name, mesh in sorted(meshes.items()) if name != root] + list(layout["parts"])
    add_properties(data, dict(layout, parts=parts))


def gun_recoil(stroke_fraction, recoil_seconds, hold_seconds, return_seconds, jump_degrees=0.0, jump_seconds=0.4,
               jump_damping=0.45, hull_rock_degrees=0.0, hull_rock_seconds=1.0, hull_rock_damping=0.6, max_distance=0.0):
    """A vehicle's reaction to its gun firing (FRTSGunRecoil, #218): the barrel's stroke as a share of its length, capped
    at max_distance cm for a gun that would clip (#353), and its seconds back, held and out again; the gun's jump and the
    hull's rock in degrees, the seconds each takes to settle and the damping ratio it settles with (#219)."""
    value = unreal.RTSGunRecoil()
    for name, setting in (("stroke_fraction", stroke_fraction), ("max_distance", max_distance),
                          ("recoil_seconds", recoil_seconds), ("hold_seconds", hold_seconds),
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
                ground=True, air=False, muzzle_flash=None, muzzle_smoke=None, splash_radius=0.0, muzzle_brake=False,
                fire_sound=None):
    """muzzle_flash: the Niagara system a vehicle gun flashes (make_muzzle_flash); None keeps the placeholder glow.
    fire_sound: the sound of a shot (make_cannon_sound); None plays the synthesized one (RTSShotSound).
    muzzle_brake: the gun has a muzzle brake, so its flash throws fire sideways too (#294).
    muzzle_smoke: the Niagara system a vehicle cannon leaves at the muzzle after a shot (make_muzzle_smoke); None leaves none.
    splash_radius: the blast in cm of a shot at a ground location (force fire); 0 for none."""
    return plan_data_asset(name, WEAPON_DIR, unreal.RTSWeaponData, {
        "muzzle_flash": muzzle_flash,
        "muzzle_smoke": muzzle_smoke,
        "muzzle_brake": muzzle_brake,
        "fire_sound": fire_sound,
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
                  max_count=0, airfield_pads=0, superweapon=None, directory=BUILDING_DIR):
    return plan_data_asset(name, directory, unreal.RTSBuildingData, {
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
# The materials take the grid as the scalar parameters Columns and Rows, so World in Conflict's 16 x 8 smoke can stand in
# on the machines that have it (import_local_art.py, #355).
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
UVA = float3((float2(fmod(A, Columns), floor(A / Columns)) + Local) / float2(Columns, Rows), 0.0);
UVB = float3((float2(fmod(B, Columns), floor(B / Columns)) + Local) / float2(Columns, Rows), 0.0);
return Frame - A;"""


def flipbook_frame(material, x, y, first, last):
    """The flipbook frame node, its first and last frames scalar parameters FrameFirst and FrameLast and its grid, frames
    across and down, Columns and Rows."""
    node = custom_node(material, x, y, FLIPBOOK_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                       ["UV", "Age", "Random", "First", "Last", "Columns", "Rows"], ["UVA", "UVB"])
    connect(expression(material, unreal.MaterialExpressionTextureCoordinate, x - 300, y), "", node, "UV")
    connect(expression(material, unreal.MaterialExpressionParticleRelativeTime, x - 300, y + 80), "", node, "Age")
    connect(expression(material, unreal.MaterialExpressionParticleRandom, x - 300, y + 160), "", node, "Random")
    connect(scalar_parameter(material, x - 300, y + 240, "FrameFirst", first), "", node, "First")
    connect(scalar_parameter(material, x - 300, y + 320, "FrameLast", last), "", node, "Last")
    connect(scalar_parameter(material, x - 300, y + 400, "Columns", 8.0), "", node, "Columns")
    connect(scalar_parameter(material, x - 300, y + 480, "Rows", 8.0), "", node, "Rows")
    return node


def component_mask(material, x, y, channels):
    """A component mask keeping only channels ("r", "rg", ...): every channel is set, as a new mask keeps some by default."""
    mask = expression(material, unreal.MaterialExpressionComponentMask, x, y)
    for channel in "rgba":
        mask.set_editor_property(channel, channel in channels)
    return mask


def flipbook_sample(material, x, y, texture, frame, pin, parameter=None):
    """A sample of texture at the frame node's UVA or UVB; with a parameter name, a texture parameter that defaults to it."""
    if parameter:
        sample = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        sample.set_editor_property("parameter_name", parameter)
    else:
        sample = expression(material, unreal.MaterialExpressionTextureSample, x, y)
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    mask = component_mask(material, x - 150, y, "rg")
    connect(frame, pin, mask, "")
    connect(mask, "", sample, "UVs")
    return sample


def blended_sample(material, x, y, texture, frame, parameter=None):
    """texture's two neighbouring frames, blended by the frame node; with a parameter name, through a texture parameter."""
    blend = expression(material, unreal.MaterialExpressionLinearInterpolate, x + 300, y)
    connect(flipbook_sample(material, x, y, texture, frame, "UVA", parameter), "RGBA", blend, "A")
    connect(flipbook_sample(material, x, y + 250, texture, frame, "UVB", parameter), "RGBA", blend, "B")
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


# The fireball's colour by its heat: dark red, orange, white-yellow, white; as bright as the shot's colour, but held below
# the point where it all burns out to white, so it reads as a ball of fire, not a flash. It cools only from white-yellow
# to yellow over its short life and dims a little, then is gone in a frame (#354).
FIRE_CODE = """float H = Heat * (1.0 - 0.35 * saturate(Age));
float3 C = lerp(float3(0.35, 0.03, 0.0), float3(1.0, 0.36, 0.05), smoothstep(0.05, 0.45, H));
C = lerp(C, float3(1.0, 0.86, 0.5), smoothstep(0.45, 0.85, H));
C = lerp(C, float3(1.0, 0.98, 0.9), smoothstep(0.85, 1.0, H));
return C * Heat * (1.0 - 0.4 * saturate(Age)) * max(max(Color.r, Color.g), Color.b) * Color.a * 0.2;"""


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


# The glare's and the sparks' soft round glow: the particle colour, brightest at the centre and falling off wide and
# smooth to nothing at the sprite's edge. Unlike GLOW_CODE it does not fade with age; Niagara sets the brightness over life.
GLARE_CODE = """float Falloff = saturate(1.0 - length(UV - 0.5) * 2.0);
return Color.rgb * Color.a * Falloff * Falloff * Falloff;"""


def build_muzzle_glare_material(name):
    """Unlit additive sprite material for the glare and the sparks of the muzzle flash (#354): the particle colour in a soft
    round glow, faded out over the last few metres in front of whatever lies behind it, so the wide glare washes over the
    hull and the ground without the hard line where a sprite cuts into them."""
    material = new_material(name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_niagara_sprites", True)

    glow = custom_node(material, -500, 100, GLARE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Color", "UV"])
    connect(expression(material, unreal.MaterialExpressionParticleColor, -800, 0), "RGBA", glow, "Color")
    connect(expression(material, unreal.MaterialExpressionTextureCoordinate, -800, 200), "", glow, "UV")
    fade = expression(material, unreal.MaterialExpressionDepthFade, -500, 300)
    fade.set_editor_property("fade_distance_default", 400.0)
    blend = expression(material, unreal.MaterialExpressionMultiply, -200, 100)
    connect(glow, "", blend, "A")
    connect(fade, "", blend, "B")
    unreal.MaterialEditingLibrary.connect_material_property(blend, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


def make_muzzle_flash():
    """The Niagara muzzle flash of vehicle guns (#220, #294, #354), its glow, fire and glare materials and the fire
    flipbook, built now so the weapons can point at it. The system is built in C++
    (URTSEditorScripting::BuildMuzzleFlashSystem, RTSMuzzleFlashBuilder.cpp), so its hash covers that source too."""
    glow = plan_now(f"{MATERIAL_DIR}/M_MuzzleFlash", lambda: build_muzzle_flash_material("M_MuzzleFlash"),
                    spec_hash([build_muzzle_flash_material, custom_node], GLOW_CODE, "M_MuzzleFlash"))
    fire_texture = make_effect_texture("T_MuzzleFire")
    fire = plan_now(f"{MATERIAL_DIR}/M_MuzzleFire", lambda: build_muzzle_fire_material("M_MuzzleFire", fire_texture),
                    spec_hash([build_muzzle_fire_material, custom_node, flipbook_frame, flipbook_sample, blended_sample, component_mask],
                              FLIPBOOK_CODE, FIRE_CODE, "M_MuzzleFire", fire_texture))
    glare = plan_now(f"{MATERIAL_DIR}/M_MuzzleGlare", lambda: build_muzzle_glare_material("M_MuzzleGlare"),
                     spec_hash([build_muzzle_glare_material, custom_node], GLARE_CODE, "M_MuzzleGlare"))
    path = f"{EFFECT_DIR}/NS_MuzzleFlash"

    def build():
        if not unreal.RTSEditorScripting.load_editor_module("NiagaraEditor"):
            raise RuntimeError("Could not load the NiagaraEditor module")
        system = unreal.RTSEditorScripting.build_muzzle_flash_system(path, glow, fire, glare)
        if not system:
            raise RuntimeError(f"Could not build {path}; the log says why")
        return system
    builder_source = file_hash(os.path.join(PROJECT_DIR, "Source", "WorldDivide", "Core", "RTSMuzzleFlashBuilder.cpp"))
    return plan_now(path, build, spec_hash([make_muzzle_flash], path, glow, fire, glare, builder_source))


# A smoke sprite's normal from its six-way light, so the side it is lit from most faces that way, in the sprite's tangent
# space (+X right, +Y down the flipbook, +Z toward the camera): only a little toward the camera, so the sun shades the
# cloud's sides rather than lighting it flat (#361). Its albedo a little brighter where it is lit through, scaled by
# Brightness; its opacity eaten from the edges inward by Erosion as it ages, over a soft ramp rather than a hard edge. A
# particle colour above 1 glows by what it is over, so smoke born in the fireball burns orange before it cools (#355).
SMOKE_CODE = """float3 N = float3(RLT.r - RLT.g, BBF.r - RLT.b, (BBF.b - BBF.g) * 0.5 + 0.25);
Normal = normalize(N);
float Light = (RLT.r + RLT.g + RLT.b + BBF.r + BBF.g + BBF.b) / 6.0;
float3 Base = min(Color.rgb, 1.0);
Albedo = Base * lerp(0.8, 1.05, saturate(Light * 1.6)) * Brightness;
Glow = (Color.rgb - Base) * lerp(0.6, 1.0, saturate(Light * 1.6));
float Thin = saturate(Age) * Erosion;
return smoothstep(Thin, min(Thin + 0.45, 1.0), RLT.a) * Color.a;"""


def build_muzzle_smoke_material(name, rlt, bbf):
    """Lit translucent sprite material for the muzzle smoke and the ground blast (#221, #294, #303): the six-way lit smoke
    flipbooks Tools/Art/muzzle_fx_blender.py bakes (T_MuzzleSmoke_RLT and _BBF), played from FrameFirst to FrameLast over
    the particle's life. The six views give each pixel a normal, so the sun, the sky and the muzzle light shade the cloud
    from wherever they are; the particle colour tints it. It thins from its edges inward by Erosion as it ages and fades
    over FadeDistance cm where it meets the ground or a hull. The instances (SMOKE_LOOKS) set the frames, the erosion and
    the Brightness, which scales the albedo, of the blast clouds, the veil and the dust. The flipbooks are the texture
    parameters SmokeRLT and SmokeBBF, so import_local_art.py can swap in World in Conflict's (#355)."""
    material = new_material(name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # Lit per pixel, not from the coarse translucency volume, so the sun shades each sprite by its six-way normal (#361).
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    material.set_editor_property("used_with_niagara_sprites", True)

    frame = flipbook_frame(material, -1400, 0, 0.0, FLIPBOOK_LAST_FRAME)
    rlt_sample = blended_sample(material, -1000, 0, rlt, frame, "SmokeRLT")
    bbf_sample = blended_sample(material, -1000, 600, bbf, frame, "SmokeBBF")
    shade = custom_node(material, -300, 200, SMOKE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                        ["RLT", "BBF", "Color", "Age", "Erosion", "Brightness"], ["Normal", "Albedo", "Glow"])
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
    editing.connect_material_property(shade, "Glow", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    editing.connect_material_property(fade, "", unreal.MaterialProperty.MP_OPACITY)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(material)
    return material


# The smoke instances: the frames of the flipbook each plays, how much it erodes and how bright it is. The blast clouds
# and bore puffs play the dense billow, the veil over the gun and the dust the later, larger and thinner frames. The
# material takes in far more light than an opaque surface of the same colour (#325), so Brightness brings the clouds'
# colours down until they read as the charcoal and greys of World in Conflict's smoke and the dust as brown (#327, #355).
SMOKE_LOOKS = {
    "MI_MuzzleSmoke_Blast": {"FrameFirst": 0.0, "FrameLast": 40.0, "Erosion": 0.35, "Brightness": 0.2},
    "MI_MuzzleSmoke_Veil": {"FrameFirst": 18.0, "FrameLast": 63.0, "Erosion": 0.6, "Brightness": 0.2},
    "MI_MuzzleSmoke_Dust": {"FrameFirst": 6.0, "FrameLast": 50.0, "Erosion": 0.7, "Brightness": 0.2},
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
    """The Niagara muzzle smoke of vehicle cannons (#221, #303, #355) with its ground blast (#294), the smoke material, its
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
            path, looks["MI_MuzzleSmoke_Blast"], looks["MI_MuzzleSmoke_Veil"], looks["MI_MuzzleSmoke_Dust"])
        if not system:
            raise RuntimeError(f"Could not build {path}; the log says why")
        return system
    builder_source = file_hash(os.path.join(PROJECT_DIR, "Source", "WorldDivide", "Core", "RTSMuzzleFlashBuilder.cpp"))
    return plan_now(path, build, spec_hash([make_muzzle_smoke], path, *looks.values(), builder_source))


# The tank cannon's takes (#374), made by Lek River Studio (Art/Source/Audio/CREDITS.md): per take a blast, a body and an
# echo tail, in the order BuildLayeredShotSound layers them.
CANNON_SOUND_SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Audio", "TankCannon")
CANNON_SOUND_TAKES = [f"TankCannon_0{take}_{layer}" for take in (1, 2, 3) for layer in ("Blast", "Body", "Echo")]


def import_wave(source, directory, name):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = directory
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    wave = unreal.load_asset(f"{directory}/{name}")
    if not isinstance(wave, unreal.SoundWave):
        raise RuntimeError(f"Could not import {source}")
    return wave


def make_cannon_sound():
    """The tank cannons' firing sound (#356, #374): one take at random per shot, each mixing its blast, body and echo by
    distance (URTSEditorScripting::BuildLayeredShotSound, RTSShotSoundBuilder.cpp)."""
    directory = f"{AUDIO_DIR}/Weapons"
    waves = []
    for take in CANNON_SOUND_TAKES:
        source = os.path.join(CANNON_SOUND_SOURCE_DIR, f"{take}.wav")
        name = f"SW_{take}"
        waves.append(plan_now(f"{directory}/{name}", lambda s=source, n=name: import_wave(s, directory, n),
                              spec_hash([import_wave], name, file_hash(source))))
    path = f"{directory}/SC_TankCannon"

    def build():
        cue = unreal.RTSEditorScripting.build_layered_shot_sound(path, waves)
        if not cue:
            raise RuntimeError(f"Could not build {path}; the log says why")
        return cue
    builder_source = file_hash(os.path.join(PROJECT_DIR, "Source", "WorldDivide", "Core", "RTSShotSoundBuilder.cpp"))
    return plan_now(path, build, spec_hash([make_cannon_sound], path, *waves, builder_source))


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


# The dust's haze (#224, #336): the muzzle smoke's flipbook from its thinned, dissipating frames only, eroded hard so a puff
# has no billowing rim and many faint ones run together into a low sheet; it fades over most of a metre where it meets the
# ground, so a low puff's lower half melts into it rather than ending in a line. Lit per pixel as the ground is, so at a
# Brightness of 1 FRTSDustSurface's colours are the dust's own.
DUST_LOOK = {"FrameFirst": 28.0, "FrameLast": 56.0, "Erosion": 0.75, "FadeDistance": 80.0, "Brightness": 1.0}


def build_dust_haze_material(name, rlt, bbf):
    """The muzzle smoke's material (build_muzzle_smoke_material) for the dust's haze (#336), lit per pixel as the ground
    is rather than from the translucency lighting volume: the volume's cells are a metre and more across at RTS distance,
    and those that reach under the ground are in its shadow, which turned a haze lying low on the ground near black."""
    material = build_muzzle_smoke_material(name, rlt, bbf)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material

# A dust chunk (#336): a lit clod with a ragged edge of three lobes, their phases and the outline's turn each from the
# particle's random value so no two match (the sprite is also turned at random and sized 0.6 to 1.4 times), and a domed
# normal (in the sprite's tangent space, +Z toward the camera) so the sun shades it as a lump.
DUST_CHUNK_CODE = """float2 P = UV * 2.0 - 1.0;
float A = atan2(P.y, P.x);
float S = Random * 6.2832;
float T = frac(Random * 7.31) * 6.2832;
float U = frac(Random * 13.17) * 6.2832;
float R = 0.7 + 0.14 * sin(A * 2.0 + S) + 0.09 * sin(A * 3.0 + T) + 0.06 * sin(A * 5.0 + U);
Normal = normalize(float3(P.x, P.y, 1.4));
return 1.0 - length(P) / R;"""


def build_dust_chunk_material(name):
    """Lit masked sprite material for the chunks of the dust (#336): the particle colour, the ground's chunk colour, on a
    ragged clod (DUST_CHUNK_CODE) that the scene's light shades."""
    material = new_material(name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("opacity_mask_clip_value", 0.05)
    material.set_editor_property("used_with_niagara_sprites", True)

    shape = custom_node(material, -400, 200, DUST_CHUNK_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV", "Random"],
                        ["Normal"])
    connect(expression(material, unreal.MaterialExpressionTextureCoordinate, -700, 200), "", shape, "UV")
    connect(expression(material, unreal.MaterialExpressionParticleRandom, -700, 300), "", shape, "Random")
    color = expression(material, unreal.MaterialExpressionParticleColor, -400, 0)
    roughness = expression(material, unreal.MaterialExpressionConstant, -400, 400)
    roughness.set_editor_property("r", 1.0)
    editing = unreal.MaterialEditingLibrary
    editing.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(shape, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    editing.connect_material_property(shape, "Normal", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(material)
    return material


def make_dust():
    """The Niagara dust ground vehicles raise behind their tracks or wheels (#224, #336): its haze material, an instance of
    the muzzle smoke's graph lit per pixel (M_DustHaze, from the flipbooks make_muzzle_smoke imports), and its chunk
    material. Built in C++ like the muzzle flash (URTSEditorScripting::BuildDustSystem, RTSMuzzleFlashBuilder.cpp)."""
    rlt = make_effect_texture("T_MuzzleSmoke_RLT")
    bbf = make_effect_texture("T_MuzzleSmoke_BBF")
    parent = plan_now(f"{MATERIAL_DIR}/M_DustHaze", lambda: build_dust_haze_material("M_DustHaze", rlt, bbf),
                      spec_hash([build_dust_haze_material, build_muzzle_smoke_material, custom_node, flipbook_frame, flipbook_sample,
                                 blended_sample, scalar_parameter, component_mask], FLIPBOOK_CODE, SMOKE_CODE, "M_DustHaze", rlt, bbf))
    look = plan_now(f"{MATERIAL_DIR}/MI_Dust", lambda: build_material_instance("MI_Dust", parent, DUST_LOOK),
                    spec_hash([build_material_instance], "MI_Dust", parent, sorted(DUST_LOOK.items())))
    chunk = plan_now(f"{MATERIAL_DIR}/M_DustChunk", lambda: build_dust_chunk_material("M_DustChunk"),
                     spec_hash([build_dust_chunk_material, custom_node], DUST_CHUNK_CODE, "M_DustChunk"))
    path = f"{EFFECT_DIR}/NS_Dust"

    def build():
        if not unreal.RTSEditorScripting.load_editor_module("NiagaraEditor"):
            raise RuntimeError("Could not load the NiagaraEditor module")
        system = unreal.RTSEditorScripting.build_dust_system(path, look, chunk)
        if not system:
            raise RuntimeError(f"Could not build {path}; the log says why")
        return system
    builder_source = file_hash(os.path.join(PROJECT_DIR, "Source", "WorldDivide", "Core", "RTSMuzzleFlashBuilder.cpp"))
    return plan_now(path, build, spec_hash([make_dust], path, look, chunk, builder_source))


# The kinds of ground (#224): the surface type of each one's physical material, as Config/DefaultEngine.ini names it, and
# the dust a vehicle raises on it (FRTSDustSurface, #336), whatever it leaves out the struct's default, which suits dry dirt:
# rate_scale times FRTSVehicleDust's rate (water raises none); the haze's colour (r, g, b) in linear space, its share of the
# rate (haze_scale) and of the opacity (opacity_scale), its life in s, how fast it spreads along the ground and rises in
# cm/s, how fast it sinks in cm/s² and its size_scale; the chunks' share of the rate, their size in cm and colour. Dirt is
# the ground of every map; the others lie in strips in a corner of L_TestMap (TEST_GROUND_STRIPS).
PHYSICAL_DIR = f"{MATERIAL_DIR}/Physical"
GROUND_KINDS = {
    # An olive-tan wake, a step lighter than the ground, wide and long, a few clods.
    "Dirt": (unreal.PhysicalSurface.SURFACE_TYPE1, {"rate_scale": 1.0, "color": (0.11, 0.1, 0.06)}),
    # Dark bits of torn turf and soil and a faint brown-green haze.
    "Grass": (unreal.PhysicalSurface.SURFACE_TYPE2, {
        "rate_scale": 0.9, "color": (0.12, 0.1, 0.06), "haze_scale": 0.5, "opacity_scale": 1.0, "lifetime": 1.8, "spread": 160.0,
        "lift": 30.0, "sink": 30.0, "size_scale": 1.0, "chunk_scale": 1.2, "chunk_size": 20.0, "chunk_color": (0.03, 0.03, 0.012)}),
    # A thin dry haze, flat and short-lived, at a low rate, no chunks.
    "Road": (unreal.PhysicalSurface.SURFACE_TYPE3, {
        "rate_scale": 0.4, "color": (0.06, 0.058, 0.052), "haze_scale": 0.5, "opacity_scale": 0.8, "lifetime": 1.0,
        "spread": 220.0, "lift": 10.0, "sink": 20.0, "size_scale": 1.3, "chunk_scale": 0.0}),
    # A sand-coloured wake, wide and fast, falling and fading quickly, a little dark grit.
    "Sand": (unreal.PhysicalSurface.SURFACE_TYPE4, {
        "rate_scale": 1.3, "color": (0.36, 0.29, 0.19), "haze_scale": 0.7, "lifetime": 1.3, "spread": 300.0, "lift": 30.0, "sink": 40.0,
        "size_scale": 1.4, "chunk_scale": 0.15, "chunk_size": 5.0, "chunk_color": (0.3, 0.24, 0.15)}),
    # Big dark clods thrown to the sides and back, little haze.
    "Mud": (unreal.PhysicalSurface.SURFACE_TYPE5, {
        "rate_scale": 1.0, "color": (0.09, 0.075, 0.055), "haze_scale": 0.12, "opacity_scale": 0.5, "lifetime": 1.2,
        "spread": 100.0, "lift": 20.0, "sink": 40.0, "size_scale": 0.7, "chunk_scale": 2.0, "chunk_size": 32.0,
        "chunk_color": (0.012, 0.009, 0.006)}),
    # None.
    "Water": (unreal.PhysicalSurface.SURFACE_TYPE6, {"rate_scale": 0.0, "color": (0.3, 0.3, 0.3)}),
}


def build_physical_material(name, surface):
    path = f"{PHYSICAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
    else:
        material = asset_tools.create_asset(name, PHYSICAL_DIR, unreal.PhysicalMaterial, unreal.PhysicalMaterialFactoryNew())
    material.set_editor_property("surface_type", surface)
    return material


def make_physical_materials():
    """PM_<kind> for every kind of ground, built now so the surface materials can point at theirs; by kind."""
    return {kind: plan_now(f"{PHYSICAL_DIR}/PM_{kind}", lambda n=f"PM_{kind}", t=surface: build_physical_material(n, t),
                           spec_hash([build_physical_material], f"PM_{kind}", str(surface)))
            for kind, (surface, _) in GROUND_KINDS.items()}


def vehicle_dust(system, contact_points=None):
    """A ground vehicle's dust (FRTSVehicleDust, #224): the system, every kind of ground's rate, colour and shape from
    GROUND_KINDS (#336), and the contact points in the mesh's own space in cm, on the ground; none leaves them to the
    locomotion and the hull (URTSDustSubsystem::GetContactPoints). The rate's curve keeps the struct's defaults."""
    value = unreal.RTSVehicleDust()
    value.set_editor_property("system", system)
    value.set_editor_property("contact_points", [unreal.Vector(*point) for point in contact_points or []])
    surfaces = []
    for surface, dust in GROUND_KINDS.values():
        entry = unreal.RTSDustSurface()
        entry.set_editor_property("surface", surface)
        for name, setting in dust.items():
            entry.set_editor_property(name, unreal.LinearColor(*setting, 1.0) if name.endswith("color") else setting)
        surfaces.append(entry)
    value.set_editor_property("surfaces", surfaces)
    return value


def build_surface_material(name, color, roughness, physical, digest):
    """Opaque lit material of one colour, for the ground and the water of the maps, on the physical material physical."""
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
    material.set_editor_property("phys_material", physical)

    editing.recompile_material(material)
    assets.set_metadata_tag(material, HASH_TAG, digest)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")


def make_surface_material(name, color, roughness, physical):
    """Queued before the maps, which load it by path when they are built. Returns its path; its hash goes in
    material_hashes, so the maps and their previews are rebuilt when it changes."""
    path = f"{MATERIAL_DIR}/{name}"
    digest = spec_hash([build_surface_material], name, color, roughness, physical)
    plan_rebuild(path, ".uasset", lambda digest: build_surface_material(name, color, roughness, physical, digest), digest)
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
# lifted so hulls still read as shapes against the ground while the lamps and their beams stand out. The building shots'
# night (FRTSShotNight::GetGameMoon, #424) takes the same values.
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


def spawn_ground_strips(strips):
    """L_TestMap's strips of other ground (TEST_GROUND_STRIPS), 1 cm proud of the dirt, each on its kind's material (strips,
    by kind). Labelled Ground_<Kind>, neither Wall_ nor Block_: they are no obstacles, and units drive over them."""
    (west, east), width = TEST_GROUND_STRIP_SPAN
    for kind, y in TEST_GROUND_STRIPS:
        strip = spawn_block(f"Ground_{kind}", ((west + east) / 2.0, y, -49.0), ((east - west) / 100.0, width / 100.0, 1.0))
        strip.static_mesh_component.set_material(0, unreal.load_asset(strips[kind]))


def build_map(path, night, tank, infantry, builder, m20, wall, crate, ground, strips, digest):
    new_level(path)
    check_map_lanes(TEST_MAP)

    # 600 x 600 m ground, top face at z = 0, with the strips of other ground in its south-west.
    spawn_ground(TEST_MAP.half_size, ground)
    spawn_ground_strips(strips)

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
        tanks = (x + BASE_ARMY_AHEAD * forward, -BASE_ARMY_SIDE)
        riflemen = (x + BASE_ARMY_AHEAD * forward, BASE_ARMY_SIDE)
        builders = base_builders(x, 0.0)
        # The real-size test tank, beside the base and clear of the army's paths.
        m20s = (x + 2400.0, -7800.0) if team_id == 0 else None
        check_base_layout(path, x, 0.0, [("tanks", *tanks), ("infantry", *riflemen), ("builders", *builders)]
                          + ([("M20s", *m20s)] if m20s else []))
        spawn_units(tank, 6, team_id, (*tanks, 0.0), yaw)
        spawn_units(infantry, 10, team_id, (*riflemen, 0.0), yaw)
        spawn_units(builder, 2, team_id, (*builders, 0.0), yaw)
        if m20s:
            spawn_units(m20, 2, team_id, (*m20s, 0.0), yaw)
        # Beside each base, clear of the units' paths and of the empty strip at y = 21000 the tests build in.
        spawn_supply_source((*base_source(x, 0.0, yaw), 0.0), 5000, crate)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    build_navigation(world, path)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {path}")


def make_map(path, night, tank, infantry, builder, m20, wall, crate, ground, strips):
    lighting = spawn_night_lighting if night else spawn_lighting
    night_look = (NIGHT_MOON_LUX, NIGHT_MOON_COLOR, NIGHT_MOON_PITCH, NIGHT_SKY_INTENSITY, NIGHT_EXPOSURE_BIAS) if night else None
    plan_rebuild(path, ".umap", lambda digest: build_map(path, night, tank, infantry, builder, m20, wall, crate, ground, strips, digest),
                 spec_hash([build_map, build_navigation, check_map_lanes, check_base_layout, headquarters_local, base_builders, base_source,
                            spawn, spawn_block, spawn_hill, spawn_obstacle, spawn_wall,
                            spawn_units, spawn_supply_source, spawn_ground, spawn_ground_strips, lighting], path, night_look, TEST_MAP,
                           TEST_HILL, TEST_GROUND_STRIPS, TEST_GROUND_STRIP_SPAN, BASE_LAYOUT, MAP_LANE_RULES, CUBE, tank, infantry,
                           builder, m20, wall, crate, ground, material_hashes[ground], sorted(strips.items()),
                           [material_hashes[strip] for _, strip in sorted(strips.items())], ENGINE_VERSION, NAV_CONFIG))


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
# The headquarters every start spot gets (#191): 38 m along its own x, its garage side, and 56 m along its own y. Whichever
# way the spot faces, it stands at the game's HeadquartersYaw (RTSBuildPlacement.h, #426), its garage to the southwest on
# a true diagonal, so it reaches sqrt(19^2 + 28^2) = 33.8 m from its centre and 33.2 m along x and y.
HEADQUARTERS_FOOTPRINT = (38, 56)
HEADQUARTERS_YAW = 225.0
# How far past its garage side the headquarters' exit lane runs, rounded up from RTSBuildPlacement::GetExitLaneLength for
# its widest unit, and how far the units of a spawner spread round its spot.
HEADQUARTERS_EXIT_LANE = 2000.0
BASE_UNIT_REACH = 1200.0
# What stands by each start spot, in cm: the army (L_TestMap only) ahead of the headquarters along the spot's facing and
# to its sides, the builders behind the headquarters, away from its garage, and the base's own supply source to the
# spot's left. check_base_layout keeps them all off the turned headquarters and its exit lane.
BASE_ARMY_AHEAD = 5000.0
BASE_ARMY_SIDE = 2700.0
BASE_BUILDERS_BEHIND = 3600.0
BASE_SOURCE_BEHIND = 150.0
BASE_SOURCE_SIDE = 7650.0
BASE_LAYOUT = (HEADQUARTERS_FOOTPRINT, HEADQUARTERS_YAW, HEADQUARTERS_EXIT_LANE, BASE_UNIT_REACH, BASE_ARMY_AHEAD,
               BASE_ARMY_SIDE, BASE_BUILDERS_BEHIND, BASE_SOURCE_BEHIND, BASE_SOURCE_SIDE, SUPPLY_FOOTPRINT, SUPPLY_HEIGHT)
# check_map_lanes: a gap between two obstacles, or between an obstacle and the ground's edge, is closed or at least
# VEHICLE_LANE wide (standard rule 4: 8 m carriageways), and every base keeps BASE_CLEARANCE around its spot free: the
# turned headquarters' 33.2 m along x and y, its exit and the builders round it.
VEHICLE_LANE = 800.0
BASE_CLEARANCE = 4000.0
MAP_LANE_RULES = (VEHICLE_LANE, BASE_CLEARANCE)


def headquarters_local(x, y, spot_x, spot_y):
    """(x, y) in the frame of the headquarters at (spot_x, spot_y): along its garage side and along its own y."""
    axis = (math.cos(math.radians(HEADQUARTERS_YAW)), math.sin(math.radians(HEADQUARTERS_YAW)))
    dx, dy = x - spot_x, y - spot_y
    return dx * axis[0] + dy * axis[1], -dx * axis[1] + dy * axis[0]


def base_builders(x, y):
    """Where the builders of the base at (x, y) stand: behind the headquarters, away from its garage."""
    axis = (math.cos(math.radians(HEADQUARTERS_YAW)), math.sin(math.radians(HEADQUARTERS_YAW)))
    return x - BASE_BUILDERS_BEHIND * axis[0], y - BASE_BUILDERS_BEHIND * axis[1]


def check_base_layout(path, x, y, units):
    """Raises unless each of units, (label, x, y), stands BASE_UNIT_REACH clear of the headquarters at (x, y) and of its
    exit lane, and the turned headquarters fits in BASE_CLEARANCE."""
    half_x, half_y = HEADQUARTERS_FOOTPRINT[0] * 50.0, HEADQUARTERS_FOOTPRINT[1] * 50.0
    cos, sin = abs(math.cos(math.radians(HEADQUARTERS_YAW))), abs(math.sin(math.radians(HEADQUARTERS_YAW)))
    if max(half_x * cos + half_y * sin, half_x * sin + half_y * cos) > BASE_CLEARANCE:
        raise RuntimeError(f"{path}: the turned headquarters reaches past BASE_CLEARANCE")
    for label, unit_x, unit_y in units:
        local_x, local_y = headquarters_local(unit_x, unit_y, x, y)
        # The footprint, run on along the garage side by the exit lane, and grown by the units' spread.
        if (-half_x - BASE_UNIT_REACH < local_x < half_x + HEADQUARTERS_EXIT_LANE + BASE_UNIT_REACH
                and abs(local_y) < half_y + BASE_UNIT_REACH):
            raise RuntimeError(f"{path}: {label} of the base at ({x:.0f}, {y:.0f}) stand on its headquarters or exit lane")


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
# L_TestMap's strips of other ground than dirt (#336), so rts.Debug.TrackShots shows the dust on each: the kind (GROUND_KINDS)
# and its middle along y, all running along x between the span's west and east ends and as wide as it says, in cm. In the
# south-west, 85 m and more south of team 0's start spot, south of its army and its M20s and west of Block_South: no
# obstacle, base or lane comes near. Long enough for a vehicle at full speed to show its trail on its own ground.
TEST_GROUND_STRIP_SPAN = ((-28000.0, -2000.0), 2500.0)
TEST_GROUND_STRIPS = (("Grass", -13500.0), ("Road", -16500.0), ("Sand", -19500.0), ("Mud", -22500.0), ("Water", -25500.0))
SKIRMISH_MAP = SkirmishMap("/Game/Maps/L_Skirmish", 30000.0, TEST_SPOTS, TEST_OBSTACLES, ())
DUEL_MAP = SkirmishMap("/Game/Maps/L_SkirmishDuel", 30000.0, TEST_SPOTS[:2], TEST_OBSTACLES, ())

# The skirmish maps are at least as large as Zero Hour's official maps for as many players (ZH_PROPORTIONS.md section 4,
# measured with our MBT as the ruler): 1000 m for two players (ZH's median 963 m), 1250 m for four and six (1225 and
# 1246 m). L_Skirmish and L_SkirmishDuel keep L_TestMap's 600 m: they are the lobby's test maps the tests play.

# 1000 x 1000 m, west against east, 760 m apart, across a 24 m river running north to south from edge to edge. It is
# crossed at two 36 m fords, each between two rocks, with a supply source by each ford, one on either bank.
RIVER_FORD_MAP = SkirmishMap("/Game/Maps/L_RiverFord", 50000.0, (
    (0, -38000.0, 0.0, 0.0), (1, 38000.0, 0.0, 180.0),
), (
    ("water", "Water_South", (0.0, -37900.0, 1.0), (24.0, 242.0, 0.1)),
    ("water", "Water_Middle", (0.0, 0.0, 1.0), (24.0, 444.0, 0.1)),
    ("water", "Water_North", (0.0, 37900.0, 1.0), (24.0, 242.0, 0.1)),
    ("block", "Block_FordNorthWest", (-4500.0, 27900.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordNorthEast", (4500.0, 20100.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordSouthWest", (-4500.0, -20100.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordSouthEast", (4500.0, -27900.0, 150.0), (5.0, 5.0, 3.0)),
    ("wall", "Wall_West", (-22800.0, -27000.0, 150.0), (2.0, 144.0, 3.0)),
    ("wall", "Wall_East", (22800.0, 27000.0, 150.0), (2.0, 144.0, 3.0)),
), (
    (-7500.0, 28500.0, 8000), (7500.0, -28500.0, 8000),
))

# 1250 x 1250 m, a base in each corner, 880 m from its neighbours. A quarry in the middle, walled to the west and east
# and open to the north and south, holds two rich supply sources; a rock stands between every two neighbouring bases.
QUARRY_CROSS_MAP = SkirmishMap("/Game/Maps/L_QuarryCross", 62500.0, (
    (0, -44000.0, -44000.0, 45.0), (1, 44000.0, 44000.0, -135.0), (2, 44000.0, -44000.0, 135.0),
    (3, -44000.0, 44000.0, -45.0),
), (
    ("wall", "Wall_QuarryWest", (-11400.0, 0.0, 150.0), (2.0, 228.0, 3.0)),
    ("wall", "Wall_QuarryEast", (11400.0, 0.0, 150.0), (2.0, 228.0, 3.0)),
    ("block", "Block_East", (44000.0, 0.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_West", (-44000.0, 0.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_North", (0.0, 44000.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_South", (0.0, -44000.0, 200.0), (8.0, 8.0, 4.0)),
), (
    (0.0, 5700.0, 10000), (0.0, -5700.0, 10000),
))

# 1250 x 1250 m, six bases on a ring, each opposite another and 470 m from its neighbours. A peak stands between every
# two neighbouring bases and six rocks ring the middle; a supply source lies in front of each peak.
SIX_PEAKS_MAP = SkirmishMap("/Game/Maps/L_SixPeaks", 62500.0, ring_spots(47000.0, (180.0, 0.0, 240.0, 60.0, 120.0, 300.0)), (
    *(("block", f"Block_Peak{index}", (*ring(42000.0, 30.0 + 60.0 * index), 250.0), (8.0, 8.0, 5.0)) for index in range(6)),
    *(("block", f"Block_Middle{index}", (*ring(15600.0, 60.0 * index), 150.0), (6.0, 6.0, 3.0)) for index in range(6)),
), tuple((*ring(28700.0, 30.0 + 60.0 * index), 6000) for index in range(6)))


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
        builders = base_builders(x, y)
        check_base_layout(spec.path, x, y, [("builders", *builders)])
        # The units of a spot carry its number; the game mode hands them to the team playing from it.
        spawn_units(builder, 2, spot, (*builders, 0.0), yaw)
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
    digest = spec_hash([build_skirmish_map, build_navigation, check_map_lanes, check_base_layout, headquarters_local,
                        base_builders, base_source, spawn, spawn_block, spawn_obstacle,
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
    for path, extension, _, _ in pending:
        if repo_file(path, extension) in skip_worktree:
            unreal.log_warning(f"setup_testmap: {repo_file(path, extension)} is marked skip-worktree, keeping the local version")
    pending[:] = [entry for entry in pending if repo_file(entry[0], entry[1]) not in skip_worktree]

    for path, extension, exists, _ in pending:
        if exists:
            blocked_by(path, extension)
    if read_only:
        # LF line endings, so git lfs lock $(cat Saved/Setup/ReadOnlyAssets.txt) from bash gets clean paths.
        os.makedirs(os.path.dirname(READ_ONLY_REPORT), exist_ok=True)
        with open(READ_ONLY_REPORT, "w", encoding="utf-8", newline="\n") as report:
            report.write("\n".join(read_only) + "\n")
        for file in read_only:
            unreal.log_error(f"setup_testmap: {file} must be rewritten or deleted but is read-only (git lfs lock it)")
        unreal.log_error("setup_testmap: nothing written")
        return

    for _, _, _, write in pending:
        write()
    deleted = sum(1 for path, _, _, _ in pending if path in stale)
    unreal.log(f"setup_testmap: done ({len(pending) - deleted} assets written, {deleted} deleted)")


if os.path.exists(READ_ONLY_REPORT):
    os.remove(READ_ONLY_REPORT)
skip_worktree = skip_worktree_files()

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
# Both tank cannons fire the layered cannon sound (#374); every other gun keeps the synthesized one.
cannon_sound = make_cannon_sound()
# Vehicles' engine exhaust (#222): the M20 from its model, the flak truck from a tailpipe placed on its van.
exhaust = make_exhaust()
# The dust behind every ground vehicle (#224): the M20's from its tracks in its model, the others' from their locomotion.
dust = make_dust()
cannon = make_weapon("DA_TestCannon", TANK_RANGE, 40.0, 2.0, 30000.0, 1.6, (1.0, 0.45, 0.1), muzzle_flash=muzzle_flash,
                     muzzle_smoke=muzzle_smoke, splash_radius=250.0, muzzle_brake=True, fire_sound=cannon_sound)
rifle = make_weapon("DA_TestRifle", 3500.0, 8.0, 0.5, 45000.0, 0.4, (1.0, 0.85, 0.3))
# A main battle tank, 9.8 x 3.7 m with its gun forward; the placeholder's long gun lets the width bind.
tank = make_unit("DA_TestTank", "Test Tank", tank_mesh, fit_scale(tank_mesh, 1080.0, 370.0), 200.0, cannon, 1100.0, 75.0,
                 700, 10.0)
# A heavier second tank to try out. It wears the standard tank's model here; on a machine with the local Abrams art,
# import_local_art.py gives it that model and its traversing turret.
heavy_cannon = make_weapon("DA_TestHeavyCannon", TANK_RANGE, 60.0, 2.5, 32500.0, 2.0, (1.0, 0.4, 0.08),
                           muzzle_flash=muzzle_flash, muzzle_smoke=muzzle_smoke, splash_radius=350.0,
                           fire_sound=cannon_sound)
abrams = make_unit("DA_TestAbrams", "Test Abrams", tank_mesh, fit_scale(tank_mesh, 1080.0, 380.0), 350.0, heavy_cannon,
                   1000.0, 60.0, 1100, 14.0)
# The M20 Guardian at its real size (7.1 m hull), to try the Blender to Unreal pipeline out: the heavy tank's weapon and
# cost, a light tank's speed and turn rate, and a turret that traverses at 45 degrees a second. No building offers it;
# two stand in L_TestMap.
m20_mesh, m20_parts = make_m20_guardian(exhaust, dust)
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
# Every team starts with one at its PlayerStart and may own only one; a team that lost it builds it again. The USA HQ
# at real size (#191): 38 x 56 m, its meshes as parts, its blocks as collision boxes.
headquarters_mesh, headquarters_footprint, headquarters_layout = make_usa_hq()
if tuple(headquarters_footprint) != HEADQUARTERS_FOOTPRINT:
    raise RuntimeError(f"The USA HQ's footprint {headquarters_footprint} is not HEADQUARTERS_FOOTPRINT: lay the bases out again")
headquarters = make_building("DA_TestHeadquarters", "Test Headquarters", headquarters_mesh, (1.0, 1.0, 1.0),
                             headquarters_footprint, 3000.0, None, 2500, 12.0, 0, 0, units=[builder], max_count=1)
add_properties(headquarters, headquarters_layout)


def make_kit_lighting_showcase(mesh, footprint, height):
    """DA_TestKitLighting (#392, #393): the kit's wall fixtures (01, 02, 03, 05, 09, the indicator blue) and its ceiling
    fixtures (04, 07, facing out like the wall ones) mounted on the front of a plain block, footprint cells and height cm,
    and the roof flood light (06), the warning beacon (08) and the mast beacons (12, 13) on its roof, for ue.ps1 -Task
    buildingshots -Building DA_TestKitLighting. Test content only: nothing produces it. Its seven steady lights and one
    warning light are a Large building's budget (BUILDING_KIT.md, Light budgets)."""
    scale = building_fit(mesh, footprint, height)
    bounds = mesh.get_bounds()
    front = (bounds.origin.x + bounds.box_extent.x) * scale[0]
    base = (bounds.origin.z - bounds.box_extent.z) * scale[2]
    top = (bounds.origin.z + bounds.box_extent.z) * scale[2]
    placements = [("WallLamp", "lighting/01_WallLamp", (front, -300.0, base + 350.0)),
                  ("CageWallLamp", "lighting/02_CageWallLamp", (front, -150.0, base + 250.0)),
                  ("EntranceLight", "lighting/03_EntranceLight", (front, 0.0, base + 300.0)),
                  ("DirectionalSpot", "lighting/05_DirectionalSpot", (front, 150.0, base + 350.0)),
                  ("IndicatorLED", "lighting/09_IndicatorLED", (front, 300.0, base + 250.0)),
                  ("StripLight", "lighting/04_StripLight", (front, -250.0, base + 480.0)),
                  ("ServiceBayLight", "lighting/07_ServiceBayLight", (front, 50.0, base + 480.0)),
                  ("RoofFloodLight", "lighting/06_RoofFloodLight", (front - 60.0, 300.0, top)),
                  ("WarningBeacon", "lighting/08_WarningBeacon", (front - 60.0, -300.0, top)),
                  ("MastBeaconSingle", "lighting/12_MastBeaconSingle", (front - 60.0, -100.0, top)),
                  ("MastBeaconDual", "lighting/13_MastBeaconDual", (front - 60.0, 100.0, top))]
    # Mounted parts and their lights hang under the building's mesh, so they take its MeshScale: each mount undoes it, to
    # stand at its place at real size.
    mounts = []
    for name, piece, location in placements:
        short = piece.split("_", 1)[1]
        mount = unreal.RTSKitMount()
        mount.set_editor_property("name", name)
        mount.set_editor_property("piece", unreal.load_asset(f"{KIT_DIR}/Lighting/DA_Kit_{short}"))
        mount.set_editor_property("transform", unreal.Transform(
            unreal.Vector(*(value / axis for value, axis in zip(location, scale))), unreal.Rotator(),
            unreal.Vector(*(1.0 / axis for axis in scale))))
        if short == "IndicatorLED":
            mount.set_editor_property("indicator_color", unreal.LinearColor(0.05, 0.2, 1.0, 1.0))
        mounts.append(mount)
    showcase = make_building("DA_TestKitLighting", "Test Kit Lighting", mesh, scale, footprint, 1000.0, None, 0, 1.0, 0, 0,
                             directory=SHOWCASE_DIR)
    add_properties(showcase, {"mounts": mounts, "size": unreal.RTSBuildingSize.LARGE})
    return showcase


kit_lighting = make_kit_lighting_showcase(crate_mesh, (6, 10), 600.0)
if BENCH_HQ:
    make_bench_hq(headquarters_footprint, headquarters_layout)
# The tower's spire scaled up. Two minutes to charge; three seconds after launch the strike lands, 80 m across at full
# damage near the centre and a quarter of it at the edge. One per team.
superweapon = make_building("DA_TestSuperweapon", "Test Superweapon", tower_mesh, building_fit(tower_mesh, (24, 24), 3200.0),
                            (24, 24), 2000.0, None, 3000, 30.0, 0, 3, max_count=1,
                            superweapon=(120.0, 3.0, 4000.0, 1500.0, 0.25))
buildings = [power_plant, tower, supply_depot, barracks, vehicle_factory, headquarters, airfield, superweapon]
# The headquarters produces the builder, which builds the rest.
add_properties(builder, {"buildings": buildings})
# Vehicles lean with the slope; infantry stays upright. Those without a model of their own raise dust where their
# locomotion puts the rear of the tracks or the rear wheels (#224).
for vehicle in (tank, abrams, m20, harvester, builder, flak_truck):
    add_properties(vehicle, {"tilts_to_ground": True})
for vehicle in (tank, abrams, harvester, builder, flak_truck):
    add_properties(vehicle, {"dust": vehicle_dust(dust)})


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
# The selection panel's hero art (Tools/Art/m20_hero_render.py or hero_render.py, then unit_icon.py --art); the others show
# their icon large.
add_properties(m20, {"hero_art_sprite": unreal.Name("Art.M20Guardian"), "role_text": unreal.Text("Main battle tank")})
add_properties(headquarters, {"hero_art_sprite": unreal.Name("Art.Headquarters")})
make_ghost_material("M_PlacementGhost")
make_shot_material("M_ShotFx")
# The maps' ground is dirt and their water water, for the dust behind vehicles (#224); L_TestMap has a strip of every other
# kind of ground too (#336).
physical = make_physical_materials()
ground = make_surface_material("M_Ground", (0.06, 0.075, 0.04), 0.9, physical["Dirt"])
water = make_surface_material("M_Water", (0.015, 0.04, 0.08), 0.1, physical["Water"])
ground_strips = {
    "Grass": make_surface_material("M_Grass", (0.03, 0.09, 0.015), 0.95, physical["Grass"]),
    "Road": make_surface_material("M_Road", (0.03, 0.03, 0.03), 0.8, physical["Road"]),
    "Sand": make_surface_material("M_Sand", (0.26, 0.2, 0.13), 1.0, physical["Sand"]),
    "Mud": make_surface_material("M_Mud", (0.045, 0.032, 0.02), 0.6, physical["Mud"]),
    "Water": water,
}
make_map(MAP_PATH, False, tank, infantry, builder, m20, wall_mesh, crate_mesh, ground, ground_strips)
make_map(NIGHT_MAP_PATH, True, tank, infantry, builder, m20, wall_mesh, crate_mesh, ground, ground_strips)
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
make_village()
make_placeholder_icons()
make_ui_styles()
plan_data_assets()
plan_stale_ui_assets()
write_pending()
