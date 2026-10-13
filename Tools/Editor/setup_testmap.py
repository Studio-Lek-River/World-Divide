"""Builds the placeholder weapon, unit, harvester, builder, upgrade, building (headquarters and superweapon included), AI build order and faction data assets, the placement ghost and shot effect materials, the
placeholder models (imported from Art/Source/Placeholder by placeholder_art.py), the ground and water materials, the L_TestMap level
with a supply source and two builders by each base, the L_Skirmish and L_SkirmishDuel levels, the same ground without the
armies and with four and two start spots, the skirmish maps L_RiverFord, L_QuarryCross and L_SixPeaks with two, four and
six, all where the lobby's seats play, the map data assets that offer them in the lobby with a top-down preview of each
(rendered when the run has a renderer, drawn otherwise), and the
empty L_MainMenu and L_Lobby levels for the menu screens, and the Common UI styles of UI/ and Mods/*/UI/ (see
make_ui_styles). L_TestMap and the skirmish maps are saved with their navmesh built. Safe to re-run: only assets that differ from this script are saved, so a run without script changes
writes nothing. Data assets are compared property by property; materials, models and the map carry a hash of the code and
source files that build them and are rebuilt only when those change.

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
import math
import os
import struct
import subprocess
import sys
import zlib

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import placeholder_art as art  # noqa: E402
import ui_sources  # noqa: E402

UNIT_DIR = "/Game/Data/Units"
WEAPON_DIR = "/Game/Data/Weapons"
BUILDING_DIR = "/Game/Data/Buildings"
UPGRADE_DIR = "/Game/Data/Upgrades"
FACTION_DIR = "/Game/Data/Factions"
AI_DIR = "/Game/Data/AI"
MAP_DATA_DIR = "/Game/Data/Maps"
UI_STYLE_DIR = "/Game/UI/Styles"
MATERIAL_DIR = "/Game/Materials"
MAP_PATH = "/Game/Maps/L_TestMap"
MAIN_MENU_MAP_PATH = "/Game/Maps/L_MainMenu"
LOBBY_MAP_PATH = "/Game/Maps/L_Lobby"
CUBE = "/Engine/BasicShapes/Cube.Cube"
TEAM_MATERIAL = "M_PlaceholderTeam"
# How strongly units and buildings take on their team colour; scenery keeps its own colours.
TEAM_TINT = 0.7

# Package metadata key holding the hash of the code that built the asset.
HASH_TAG = "SetupHash"
# Package metadata key telling whether a map preview was rendered or drawn.
PREVIEW_SOURCE_TAG = "PreviewSource"
# The navmesh a map is saved with is serialized for the engine it was built in, so the maps are rebuilt when the engine
# moves to another major or minor version.
ENGINE_VERSION = ".".join(unreal.SystemLibrary.get_engine_version().split(".")[:2])

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
CONTENT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
READ_ONLY_REPORT = os.path.join(PROJECT_DIR, "Saved", "Setup", "ReadOnlyAssets.txt")
ART_SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Placeholder")

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

# Assets that need saving: (package path, file extension, already on disk, function that writes it).
pending = []

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


def plan_data_asset(name, directory, data_class, properties):
    """Loads or creates a data asset and records the properties it should have; plan_data_assets queues the save."""
    path = f"{directory}/{name}"
    exists = assets.does_asset_exist(path)
    if exists:
        asset = assets.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", data_class)
        asset = asset_tools.create_asset(name, directory, data_class, factory)
    data_assets[path] = (asset, exists, dict(properties))
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
    return plan_now(path, build, spec_hash([art.build_team_material], path))


def make_texture(source):
    """A pack's texture, named after the pack folder: Kenney/CarKit/Textures/colormap.png becomes T_CarKit_colormap."""
    name = f"T_{os.path.basename(os.path.dirname(os.path.dirname(source)))}_{os.path.splitext(os.path.basename(source))[0]}"
    return plan_now(f"{art.TEXTURE_DIR}/{name}", lambda: art.import_texture(source, name),
                    spec_hash([art.import_texture], name, file_hash(source)))


def make_art(name, source, yaw=0.0, tint=TEAM_TINT):
    """Imports Art/Source/Placeholder/<source> as SM_<name>, turned by yaw so it faces +X, with a material instance
    MI_<name>_<slot> per glTF material."""
    source = os.path.join(ART_SOURCE_DIR, source)
    parent = make_team_material()
    slot_materials = {}
    for slot, (color, texture_file) in art.read_gltf_materials(source).items():
        texture = make_texture(texture_file) if texture_file else None
        instance_name = f"MI_{name}_{slot}".replace("-", "_").replace(".", "_")
        slot_materials[slot] = plan_now(
            f"{art.MATERIAL_DIR}/{instance_name}",
            lambda n=instance_name, t=texture, c=color: art.build_material_instance(n, parent, t, c, tint),
            spec_hash([art.build_material_instance], instance_name, parent, texture, color, tint))
    mesh_name = f"SM_{name}"
    return plan_now(f"{art.MESH_DIR}/{mesh_name}", lambda: art.import_mesh(source, mesh_name, yaw, slot_materials),
                    spec_hash([art.import_mesh, art.import_static_mesh], mesh_name, file_hash(source), yaw,
                              sorted((slot, object_path(instance)) for slot, instance in slot_materials.items())))


def fit_scale(mesh, length, width, height=None):
    """MeshScale fitting the mesh inside length x width (along x and y) with its proportions kept, or stretched to height
    when one is given."""
    box = mesh.get_bounding_box()
    scale = min(length / (box.max.x - box.min.x), width / (box.max.y - box.min.y))
    return (scale, scale, height / (box.max.z - box.min.z) if height else scale)


def make_weapon(name, weapon_range, damage, reload_seconds, shell_speed, effect_scale, shell_color, arc_height=0.0,
                ground=True, air=False):
    return plan_data_asset(name, WEAPON_DIR, unreal.RTSWeaponData, {
        "range": weapon_range,
        "damage": damage,
        "reload_seconds": reload_seconds,
        "shell_speed": shell_speed,
        "can_target_ground": ground,
        "can_target_air": air,
        "shell_arc_height": arc_height,
        "effect_scale": effect_scale,
        "shell_color": unreal.LinearColor(*shell_color, 1.0),
    })


def make_unit(name, display_name, mesh, scale, radius, health, weapon, speed, turn_rate, cost, build_time):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSUnitData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": radius,
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


def make_harvester(name, display_name, mesh, scale, radius, health, speed, turn_rate, cost, build_time,
                   carry_amount, gather_seconds, search_radius):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSHarvesterData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": radius,
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


def make_builder(name, display_name, mesh, scale, radius, health, speed, turn_rate, cost, build_time, buildings):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSBuilderData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": radius,
        "max_health": health,
        "weapon": None,
        "move_speed": speed,
        "turn_rate": turn_rate,
        "cost": cost,
        "build_time": build_time,
        "buildings": buildings,
    })


def make_aircraft(name, display_name, mesh, scale, radius, health, weapon, speed, turn_rate, cost, build_time,
                  cruise_altitude, climb_rate, max_bank_angle, ammo_per_sortie, rearm_seconds):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSAircraftData, {
        "display_name": unreal.Text(display_name),
        "mesh": mesh,
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": radius,
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
        "superweapon_radius": superweapon[2] if superweapon else 1500.0,
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


def make_faction(name, display_name, buildings, headquarters=None, uses_power=True, ai_build_order=None):
    return plan_data_asset(name, FACTION_DIR, unreal.RTSFactionData, {
        "display_name": unreal.Text(display_name),
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


def build_shot_material(name, digest):
    """Unlit additive material glowing in its "Color" parameter, for muzzle flashes, shells and impacts. Additive, so a
    colour faded to black is gone."""
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
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    editing.recompile_material(material)
    assets.set_metadata_tag(material, HASH_TAG, digest)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")


def make_shot_material(name):
    plan_rebuild(f"{MATERIAL_DIR}/{name}", ".uasset", lambda digest: build_shot_material(name, digest),
                 spec_hash([build_shot_material], name))


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


def spawn_supply_source(location, amount, footprint, mesh):
    source = spawn(unreal.RTSSupplySource, location, label=f"SupplySource_{int(location[0])}_{int(location[1])}")
    source.set_editor_property("amount", amount)
    source.set_editor_property("footprint", unreal.IntPoint(*footprint))
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


def build_map(tank, infantry, builder, wall, crate, ground, digest):
    new_level(MAP_PATH)

    # 200 x 200 m ground, top face at z = 0.
    spawn_ground(10000.0, ground)

    # Obstacles between the two bases so pathfinding has something to do.
    spawn_wall("Wall_West", (-2500.0, 0.0, 150.0), (2.0, 20.0, 3.0), wall)
    spawn_obstacle("Block_North", (1000.0, 2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_obstacle("Block_South", (1000.0, -2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_wall("Wall_East", (3500.0, 0.0, 150.0), (2.0, 12.0, 3.0), wall)
    # A ridge between Wall_East and the east base, which the east army crosses on its way to the middle. Not labelled Wall_ or
    # Block_: the tests take those to be the obstacles.
    spawn_hill("Hill_East", 4500.0, 300.0, 4000.0, 100.0, 15.0, ground)

    spawn_lighting()

    # The default volume brush is a 200 uu cube, so this covers the whole ground.
    nav = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(100.0, 100.0, 10.0))
    _, nav_extent = nav.get_actor_bounds(False)
    unreal.log(f"NavMeshBoundsVolume extent: {nav_extent}")
    if nav_extent.x < 1000.0:
        unreal.log_warning("NavMeshBoundsVolume has no usable brush: drag one into L_TestMap by hand and save.")

    for team_id, x, yaw in ((0, -6000.0, 0.0), (1, 6000.0, 180.0)):
        start = spawn(unreal.PlayerStart, (x, 0.0, 100.0), yaw, label=f"PlayerStart_Team{team_id}")
        start.set_editor_property("player_start_tag", f"Team{team_id}")
        forward = 1.0 if yaw == 0.0 else -1.0
        spawn_units(tank, 6, team_id, (x + 600.0 * forward, -900.0, 0.0), yaw)
        spawn_units(infantry, 10, team_id, (x + 600.0 * forward, 900.0, 0.0), yaw)
        spawn_units(builder, 2, team_id, (x - 400.0 * forward, 0.0, 0.0), yaw)
        # Beside each base, clear of the units' paths and of the empty strip at y = 7000 the tests build in.
        spawn_supply_source((x - 50.0 * forward, 2550.0 * forward, 0.0), 5000, (3, 3), crate)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    build_navigation(world, MAP_PATH)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {MAP_PATH}")


def make_map(tank, infantry, builder, wall, crate, ground):
    plan_rebuild(MAP_PATH, ".umap", lambda digest: build_map(tank, infantry, builder, wall, crate, ground, digest),
                 spec_hash([build_map, build_navigation, spawn, spawn_block, spawn_hill, spawn_obstacle, spawn_wall, spawn_units, spawn_supply_source,
                            spawn_ground, spawn_lighting], MAP_PATH, CUBE, tank, infantry, builder, wall, crate, ground,
                           material_hashes[ground], ENGINE_VERSION))


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

# L_TestMap's ground and obstacles; west and east as in L_TestMap, south and north on the empty strips.
TEST_OBSTACLES = (
    ("wall", "Wall_West", (-2500.0, 0.0, 150.0), (2.0, 20.0, 3.0)),
    ("block", "Block_North", (1000.0, 2500.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_South", (1000.0, -2500.0, 200.0), (8.0, 8.0, 4.0)),
    ("wall", "Wall_East", (3500.0, 0.0, 150.0), (2.0, 12.0, 3.0)),
)
TEST_SPOTS = ((0, -6000.0, 0.0, 0.0), (1, 6000.0, 0.0, 180.0), (2, 0.0, -7000.0, 90.0), (3, 0.0, 7000.0, -90.0))
SKIRMISH_MAP = SkirmishMap("/Game/Maps/L_Skirmish", 10000.0, TEST_SPOTS, TEST_OBSTACLES, ())
DUEL_MAP = SkirmishMap("/Game/Maps/L_SkirmishDuel", 10000.0, TEST_SPOTS[:2], TEST_OBSTACLES, ())

# 160 x 160 m, west against east across a river running north to south. It is crossed at two fords, each between two
# rocks, with a supply source by each ford, one on either bank.
RIVER_FORD_MAP = SkirmishMap("/Game/Maps/L_RiverFord", 8000.0, (
    (0, -5500.0, 0.0, 0.0), (1, 5500.0, 0.0, 180.0),
), (
    ("water", "Water_South", (0.0, -6300.0, 1.0), (8.0, 34.0, 0.1)),
    ("water", "Water_Middle", (0.0, 0.0, 1.0), (8.0, 68.0, 0.1)),
    ("water", "Water_North", (0.0, 6300.0, 1.0), (8.0, 34.0, 0.1)),
    ("block", "Block_FordNorthWest", (-1500.0, 5300.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordNorthEast", (1500.0, 2700.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordSouthWest", (-1500.0, -2700.0, 150.0), (5.0, 5.0, 3.0)),
    ("block", "Block_FordSouthEast", (1500.0, -5300.0, 150.0), (5.0, 5.0, 3.0)),
    ("wall", "Wall_West", (-3800.0, -4500.0, 150.0), (2.0, 24.0, 3.0)),
    ("wall", "Wall_East", (3800.0, 4500.0, 150.0), (2.0, 24.0, 3.0)),
), (
    (-2500.0, 5500.0, 8000), (2500.0, -5500.0, 8000),
))

# 220 x 220 m, a base in each corner. A quarry in the middle, walled to the west and east and open to the north and
# south, holds two rich supply sources; a rock stands between every two neighbouring bases.
QUARRY_CROSS_MAP = SkirmishMap("/Game/Maps/L_QuarryCross", 11000.0, (
    (0, -7000.0, -7000.0, 45.0), (1, 7000.0, 7000.0, -135.0), (2, 7000.0, -7000.0, 135.0), (3, -7000.0, 7000.0, -45.0),
), (
    ("wall", "Wall_QuarryWest", (-2000.0, 0.0, 150.0), (2.0, 40.0, 3.0)),
    ("wall", "Wall_QuarryEast", (2000.0, 0.0, 150.0), (2.0, 40.0, 3.0)),
    ("block", "Block_East", (7000.0, 0.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_West", (-7000.0, 0.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_North", (0.0, 7000.0, 200.0), (8.0, 8.0, 4.0)),
    ("block", "Block_South", (0.0, -7000.0, 200.0), (8.0, 8.0, 4.0)),
), (
    (0.0, 1000.0, 10000), (0.0, -1000.0, 10000),
))

# 240 x 240 m, six bases on a ring, each opposite another. A peak stands between every two neighbouring bases and six
# rocks ring the middle; a supply source lies in front of each peak.
SIX_PEAKS_MAP = SkirmishMap("/Game/Maps/L_SixPeaks", 12000.0, ring_spots(9000.0, (180.0, 0.0, 240.0, 60.0, 120.0, 300.0)), (
    *(("block", f"Block_Peak{index}", (*ring(8000.0, 30.0 + 60.0 * index), 250.0), (8.0, 8.0, 5.0)) for index in range(6)),
    *(("block", f"Block_Middle{index}", (*ring(3000.0, 60.0 * index), 150.0), (6.0, 6.0, 3.0)) for index in range(6)),
), tuple((*ring(5500.0, 30.0 + 60.0 * index), 6000) for index in range(6)))


def build_skirmish_map(spec, builder, wall, crate, ground, water, digest):
    """A level with spec's ground and obstacles and start spots, each with only its builders and a supply source, and
    the skirmish game mode, which plays the seats the lobby chose (by default a player against an AI opponent from spots
    0 and 1) and lets every side build its army itself."""
    path = spec.path
    new_level(path)

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
        start = spawn(unreal.PlayerStart, (x, y, 100.0), yaw, label=f"PlayerStart_Team{spot}")
        start.set_editor_property("player_start_tag", f"Team{spot}")
        forward = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
        # The units of a spot carry its number; the game mode hands them to the team playing from it.
        spawn_units(builder, 2, spot, (x - 400.0 * forward[0], y - 400.0 * forward[1], 0.0), yaw)
        # To the left of the base, seen from the middle.
        spawn_supply_source((x - 50.0 * forward[0] - 2550.0 * forward[1], y - 50.0 * forward[1] + 2550.0 * forward[0], 0.0),
                            5000, (3, 3), crate)
    for x, y, amount in spec.sources:
        spawn_supply_source((x, y, 0.0), amount, (3, 3), crate)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.RTSSkirmishGameMode.static_class())
    build_navigation(world, path)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {path}")


def make_skirmish_map(spec, builder, wall, crate, ground, water):
    """Queues the level and then its preview."""
    digest = spec_hash([build_skirmish_map, build_navigation, spawn, spawn_block, spawn_obstacle, spawn_wall, spawn_units,
                        spawn_supply_source, spawn_ground, spawn_water, spawn_lighting], spec, CUBE, builder, wall, crate, ground,
                       water, material_hashes[ground], material_hashes[water], ENGINE_VERSION)
    plan_rebuild(spec.path, ".umap", lambda digest: build_skirmish_map(spec, builder, wall, crate, ground, water, digest), digest)
    make_map_preview(spec, digest)


PREVIEW_SIZE = 256
PREVIEW_GROUND = (86, 98, 70)
PREVIEW_OBSTACLE = (58, 58, 64)
PREVIEW_WATER = (40, 70, 110)
PREVIEW_SUPPLY = (214, 176, 62)


def preview_area(spec):
    """The world XY rectangle the map's preview covers, (min x, min y, max x, max y): its whole ground."""
    return (-spec.half_size, -spec.half_size, spec.half_size, spec.half_size)


def preview_position(area, x, y):
    """Where world (x, y) is on a preview of area, from the left and from the top in 0..1, as
    URTSMapData::ToPreviewPosition: world +X is up, world +Y is right."""
    min_x, min_y, max_x, max_y = area
    return (y - min_y) / (max_y - min_y), (max_x - x) / (max_x - min_x)


def write_png(file_name, size, pixels):
    """An 8-bit RGB PNG of size x size pixels, pixels being the rows top to bottom as bytearrays."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(row) for row in pixels)
    with open(file_name, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n")
        file.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)))
        file.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        file.write(chunk(b"IEND", b""))


def draw_map_preview(area, file_name):
    """The loaded level as a PNG at file_name: the ground with its obstacles, water and supply sources, drawn from their
    bounds, for runs without a renderer."""
    pixels = [bytearray(PREVIEW_GROUND * PREVIEW_SIZE) for _ in range(PREVIEW_SIZE)]

    def fill(actor, color):
        origin, extent = actor.get_actor_bounds(False)
        left, top = preview_position(area, origin.x + extent.x, origin.y - extent.y)
        right, bottom = preview_position(area, origin.x - extent.x, origin.y + extent.y)
        columns = range(max(0, int(left * PREVIEW_SIZE)), min(PREVIEW_SIZE, math.ceil(right * PREVIEW_SIZE)))
        for row in range(max(0, int(top * PREVIEW_SIZE)), min(PREVIEW_SIZE, math.ceil(bottom * PREVIEW_SIZE))):
            for column in columns:
                pixels[row][column * 3:column * 3 + 3] = bytes(color)

    # Sorted, so the image does not depend on the order the level lists its actors in.
    for actor in sorted(actors.get_all_level_actors(), key=lambda actor: actor.get_actor_label()):
        label = actor.get_actor_label()
        if isinstance(actor, unreal.StaticMeshActor) and label.startswith(("Wall_", "Block_")):
            fill(actor, PREVIEW_OBSTACLE)
        elif isinstance(actor, unreal.StaticMeshActor) and label.startswith("Water_"):
            fill(actor, PREVIEW_WATER)
        elif isinstance(actor, unreal.RTSSupplySource):
            fill(actor, PREVIEW_SUPPLY)
    write_png(file_name, PREVIEW_SIZE, pixels)


def build_map_preview(spec, texture_path, digest):
    """A top-down image of the saved level of spec, rendered over its preview area when the run has a renderer and
    drawn otherwise."""
    if not levels.load_level(spec.path):
        raise RuntimeError(f"Could not load {spec.path}")
    directory, name = texture_path.rsplit("/", 1)
    source = os.path.join(PROJECT_DIR, "Saved", "Setup", "Previews", f"{name}.png")
    os.makedirs(os.path.dirname(source), exist_ok=True)
    rendered = unreal.RTSEditorScripting.can_render()
    if rendered:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        if not unreal.RTSEditorScripting.capture_preview(world, preview_box(preview_area(spec)), PREVIEW_SIZE, source):
            raise RuntimeError(f"Could not render {spec.path}")
    else:
        draw_map_preview(preview_area(spec), source)

    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = directory
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    texture = unreal.load_asset(texture_path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    # Shown as is in the lobby: no mips, no compression artefacts on the thin walls.
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    assets.set_metadata_tag(texture, HASH_TAG, digest)
    assets.set_metadata_tag(texture, PREVIEW_SOURCE_TAG, "Rendered" if rendered else "Drawn")
    if not assets.save_loaded_asset(texture, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {texture_path}")


def map_preview_path(map_path):
    return f"{MAP_DATA_DIR}/T_Preview_{map_path.rsplit('/', 1)[1]}"


def preview_box(area):
    min_x, min_y, max_x, max_y = area
    return make_struct(unreal.Box2D, {"min": unreal.Vector2D(min_x, min_y), "max": unreal.Vector2D(max_x, max_y),
                                      "is_valid": True})


def make_map_preview(spec, map_digest):
    """Queued after the level, so it shows the level as just built; rebuilt whenever the level is. A drawn preview is
    rendered again by a run that has a renderer; a run without one keeps a rendered preview of an unchanged level."""
    texture_path = map_preview_path(spec.path)
    can_render = unreal.RTSEditorScripting.can_render()
    plan_rebuild(texture_path, ".uasset", lambda digest: build_map_preview(spec, texture_path, digest),
                 spec_hash([build_map_preview, draw_map_preview, preview_area, preview_box, preview_position, write_png], map_digest,
                           PREVIEW_SIZE, PREVIEW_GROUND, PREVIEW_OBSTACLE, PREVIEW_WATER, PREVIEW_SUPPLY),
                 lambda texture: can_render and assets.get_metadata_tag(texture, PREVIEW_SOURCE_TAG) != "Rendered")


def make_map_data(name, display_name, spec, default_seats):
    """default_seats: (seat type, start spot or -1 for random) pairs, at most one per start spot. The start spot positions
    come from spec's spots, which the level's PlayerStarts tagged Team<N> are placed by."""
    preview = map_preview_path(spec.path)
    area = preview_area(spec)
    return plan_data_asset(name, MAP_DATA_DIR, unreal.RTSMapData, {
        "display_name": unreal.Text(display_name),
        "map": unreal.SoftObjectPath(f"{spec.path}.{spec.path.rsplit('/', 1)[1]}"),
        "start_spots": len(spec.spots),
        "default_seats": [make_struct(unreal.RTSSeat, {"type": seat_type, "start_spot": spot})
                          for seat_type, spot in default_seats],
        "preview": unreal.SoftObjectPath(f"{preview}.{preview.rsplit('/', 1)[1]}"),
        "preview_area": preview_box(area),
        "start_spot_positions": [unreal.Vector2D(*preview_position(area, x, y)) for _, x, y, _ in spec.spots],
    })


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
            info.set_editor_property("size", float(font["size"]))
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


def make_ui_styles():
    """The Common UI styles of UI/ and the mods' UI/ folders: a style set per sprite file and theme, plus Common and
    Neutral. A set without its own theme has no colours or text styles, and its buttons use Neutral's text; the
    subsystem falls back to Neutral for those. Problems are logged, and only their entries are skipped."""
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
            unreal.log_error(f"setup_testmap: {file} must be rewritten but is read-only (git lfs lock it)")
        unreal.log_error("setup_testmap: nothing written")
        return

    for _, _, _, write in pending:
        write()
    unreal.log(f"setup_testmap: done ({len(pending)} assets written)")


if os.path.exists(READ_ONLY_REPORT):
    os.remove(READ_ONLY_REPORT)

# Placeholder models (CC0, see Art/Source/Placeholder/CREDITS.md). The Kenney vehicles and characters face +Y and the
# Quaternius tank -X once imported; yaw turns them all to face +X.
tank_mesh = make_art("Tank", "Quaternius/tank.glb", yaw=180.0)
infantry_mesh = make_art("Infantry", "Kenney/MiniCharacters/character-male-a.glb", yaw=-90.0)
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

# A defence outranges a standard tank by a quarter, so tanks take fire while they close in.
TANK_RANGE = 1200.0
TOWER_RANGE_OVER_TANK = 1.25

# Shell speed in cm/s, effect scale, shell colour; the grenade lobs.
cannon = make_weapon("DA_TestCannon", TANK_RANGE, 40.0, 2.0, 6000.0, 1.6, (1.0, 0.45, 0.1))
rifle = make_weapon("DA_TestRifle", 700.0, 8.0, 0.5, 9000.0, 0.4, (1.0, 0.85, 0.3))
tank = make_unit("DA_TestTank", "Test Tank", tank_mesh, fit_scale(tank_mesh, 260.0, 150.0), 90.0, 200.0, cannon, 600.0,
                 180.0, 700, 10.0)
infantry = make_unit("DA_TestInfantry", "Test Infantry", infantry_mesh, fit_scale(infantry_mesh, 100.0, 100.0), 30.0, 100.0,
                     rifle, 350.0, 540.0, 150, 4.0)
grenade = make_weapon("DA_TestGrenade", 600.0, 25.0, 1.5, 1500.0, 1.0, (1.0, 0.6, 0.2), arc_height=250.0)
grenadier = make_unit("DA_TestGrenadier", "Test Grenadier", infantry_mesh, fit_scale(infantry_mesh, 130.0, 130.0), 35.0, 120.0,
                      grenade, 320.0, 540.0, 300, 6.0)
rifle_training = make_upgrade("DA_TestRifleTraining", "Test Rifle Training", 400, 8.0, unreal.RTSUpgradeStat.WEAPON_DAMAGE, 1.5,
                              [infantry])
# The grenadier comes only once the barracks has researched rifle training.
add_properties(grenadier, {"prerequisites": [rifle_training]})
# The tower hits ground and air alike; the flak truck only aircraft in flight, the jet's missiles only the ground.
tower_gun = make_weapon("DA_TestTowerGun", TANK_RANGE * TOWER_RANGE_OVER_TANK, 30.0, 1.0, 7000.0, 1.2, (0.4, 0.8, 1.0),
                        air=True)
flak = make_weapon("DA_TestFlak", 1400.0, 25.0, 0.6, 8000.0, 0.8, (0.9, 0.9, 0.5), ground=False, air=True)
flak_truck = make_unit("DA_TestFlakTruck", "Test Flak Truck", truck_mesh, fit_scale(truck_mesh, 200.0, 120.0), 75.0, 250.0,
                       flak, 450.0, 360.0, 800, 8.0)
missiles = make_weapon("DA_TestJetMissiles", 900.0, 60.0, 1.0, 4000.0, 1.2, (1.0, 0.3, 0.2))
# Four missiles a sortie, eight seconds on its pad to rearm.
jet = make_aircraft("DA_TestJet", "Test Jet", jet_mesh, fit_scale(jet_mesh, 180.0, 150.0), 70.0, 150.0, missiles, 1500.0,
                    100.0, 1200, 12.0, 900.0, 500.0, 45.0, 4, 8.0)
power_plant = make_building("DA_TestPowerPlant", "Test Power Plant", power_plant_mesh, fit_scale(power_plant_mesh, 380.0, 380.0),
                            (4, 4), 800.0, None, 800, 6.0, 5, 0)
tower = make_building("DA_TestTower", "Test Tower", tower_mesh, fit_scale(tower_mesh, 160.0, 160.0, 260.0), (2, 2), 500.0,
                      tower_gun, 600, 3.0, 0, 2, requires_power=True, defense=True)
harvester = make_harvester("DA_TestHarvester", "Test Harvester", truck_mesh, fit_scale(truck_mesh, 180.0, 110.0), 70.0,
                           300.0, 450.0, 360.0, 600, 8.0, 100, 3.0, 6000.0)
# The depot brings one harvester and produces replacements, so a lost harvester needs no vehicle factory.
supply_depot = make_building("DA_TestSupplyDepot", "Test Supply Depot", supply_depot_mesh,
                             fit_scale(supply_depot_mesh, 280.0, 280.0), (3, 3), 1000.0, None, 1500, 5.0, 0, 1,
                             spawn_on_complete=harvester, accepts_supply=True, units=[harvester])
builder = make_builder("DA_TestBuilder", "Test Builder", builder_mesh, fit_scale(builder_mesh, 150.0, 100.0), 60.0, 300.0,
                       500.0, 360.0, 1000, 10.0, [])
barracks = make_building("DA_TestBarracks", "Test Barracks", barracks_mesh, fit_scale(barracks_mesh, 280.0, 280.0), (3, 3),
                         800.0, None, 500, 5.0, 0, 1, units=[infantry, grenadier], upgrades=[rifle_training])
vehicle_factory = make_building("DA_TestVehicleFactory", "Test Vehicle Factory", vehicle_factory_mesh,
                                fit_scale(vehicle_factory_mesh, 380.0, 380.0), (4, 4), 1200.0, None, 1200, 8.0, 0, 2,
                                units=[tank, harvester, flak_truck])
# Four pads side by side on a low slab; each jet it produces parks on one.
airfield = make_building("DA_TestAirfield", "Test Airfield", airfield_mesh, fit_scale(airfield_mesh, 380.0, 780.0, 40.0), (4, 8),
                         1500.0, None, 1000, 10.0, 0, 2, units=[jet], airfield_pads=4)
# Every team starts with one at its PlayerStart and may own only one; a team that lost it builds it again.
headquarters = make_building("DA_TestHeadquarters", "Test Headquarters", headquarters_mesh,
                             fit_scale(headquarters_mesh, 380.0, 380.0), (4, 4), 3000.0, None, 2500, 12.0, 0, 0,
                             units=[builder], max_count=1)
# The tower's spire scaled up. Two minutes to charge; three seconds after launch the strike lands, 15 m across at full
# damage near the centre and a quarter of it at the edge. One per team.
superweapon = make_building("DA_TestSuperweapon", "Test Superweapon", tower_mesh, fit_scale(tower_mesh, 380.0, 380.0, 700.0),
                            (4, 4), 2000.0, None, 3000, 30.0, 0, 3, max_count=1,
                            superweapon=(120.0, 3.0, 1500.0, 1500.0, 0.25))
buildings = [power_plant, tower, supply_depot, barracks, vehicle_factory, headquarters, airfield, superweapon]
# The headquarters produces the builder, which builds the rest.
add_properties(builder, {"buildings": buildings})
# Vehicles lean with the slope; infantry stays upright.
for vehicle in (tank, harvester, builder, flak_truck):
    add_properties(vehicle, {"tilts_to_ground": True})
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
test_faction = make_faction("DA_TestFaction", "Test Faction", buildings, headquarters, ai_build_order=build_order)
# Hotkeys are unique per producer: the builder, the headquarters, the barracks, the factory, the depot and the airfield.
TEST_FACTION_TAG = gameplay_tag("Faction.Test")
add_properties(test_faction, {"faction_tag": TEST_FACTION_TAG})
for entity, icon, hotkey, description in (
    (tank, "Icon.Tank", "T", "Main battle tank. Strong against vehicles and buildings."),
    (infantry, "Icon.Infantry", "I", "Rifle squad. Cheap, and quick to train."),
    (grenadier, "Icon.Grenadier", "G", "Infantry with a grenade launcher that lobs over cover. Needs rifle training."),
    (flak_truck, "Icon.FlakTruck", "F", "Anti-aircraft truck. Hits only aircraft in flight."),
    (jet, "Icon.Jet", "J", "Strike jet. Fires its missiles at ground targets and rearms on its airfield pad."),
    (harvester, "Icon.Harvester", "H", "Gathers supplies and delivers them to a supply depot."),
    (builder, "Icon.Builder", "B", "Builds and places every building."),
    (rifle_training, "Icon.RifleTraining", "R", "Infantry rifles deal half again as much damage."),
    (power_plant, "Icon.PowerPlant", "P", "Generates the power other buildings draw."),
    (tower, "Icon.Tower", "T", "Defensive gun tower against ground and air. Needs power."),
    (supply_depot, "Icon.SupplyDepot", "S", "Receives supplies from harvesters and comes with one."),
    (barracks, "Icon.Barracks", "B", "Trains infantry and researches rifle training."),
    (vehicle_factory, "Icon.VehicleFactory", "V", "Builds tanks, harvesters and flak trucks."),
    (airfield, "Icon.Airfield", "A", "Builds jets and gives each one a pad to rearm on."),
    (headquarters, "Icon.Headquarters", "H", "The command centre every team starts with. Produces builders."),
    (superweapon, "Icon.Superweapon", "W", "Charges a strike that lands anywhere on the map. One per team."),
):
    present(entity, TEST_FACTION_TAG, icon, hotkey, description)
make_ghost_material("M_PlacementGhost")
make_shot_material("M_ShotFx")
ground = make_surface_material("M_Ground", (0.06, 0.075, 0.04), 0.9)
water = make_surface_material("M_Water", (0.015, 0.04, 0.08), 0.1)
make_map(tank, infantry, builder, wall_mesh, crate_mesh, ground)
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
make_ui_styles()
plan_data_assets()
write_pending()
