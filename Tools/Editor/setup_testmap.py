"""Builds the placeholder weapon, unit, harvester, builder, upgrade, building (headquarters included), AI build order and faction data assets, the placement ghost and shot effect materials, the
placeholder models (imported from Art/Source/Placeholder by placeholder_art.py), the L_TestMap level with a supply
source and two builders by each base, the L_Skirmish and L_SkirmishDuel levels, the same ground without the armies and
with four and two start spots, where the lobby's seats play, the map data assets that offer them in the lobby, and the
empty L_MainMenu and L_Lobby levels for the menu screens. L_TestMap and the skirmish maps are saved with their navmesh built. Safe to re-run: only assets that differ from this script are saved, so a run without script changes
writes nothing. Data assets are compared property by property; materials, models and the map carry a hash of the code and
source files that build them and are rebuilt only when those change.

The assets are LFS lockable and therefore read-only unless locked. If an asset that must change is read-only, nothing
is written: the files are listed in Saved/Setup/ReadOnlyAssets.txt and the run stops. Lock them (git lfs lock) and rerun.
Assets marked skip-worktree, such as DA_TestTank swapped for local art by import_local_art.py, are left as they are.

Run from the repo root (adjust the engine path), or use Tools/Scripts/ue.ps1 -Task setup:
  "G:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/WorldDivide.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/setup_testmap.py" -unattended -nosplash -nullrhi
"""

import hashlib
import inspect
import math
import os
import subprocess
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import placeholder_art as art  # noqa: E402

UNIT_DIR = "/Game/Data/Units"
WEAPON_DIR = "/Game/Data/Weapons"
BUILDING_DIR = "/Game/Data/Buildings"
UPGRADE_DIR = "/Game/Data/Upgrades"
FACTION_DIR = "/Game/Data/Factions"
AI_DIR = "/Game/Data/AI"
MAP_DATA_DIR = "/Game/Data/Maps"
MATERIAL_DIR = "/Game/Materials"
MAP_PATH = "/Game/Maps/L_TestMap"
SKIRMISH_MAP_PATH = "/Game/Maps/L_Skirmish"
DUEL_MAP_PATH = "/Game/Maps/L_SkirmishDuel"
MAIN_MENU_MAP_PATH = "/Game/Maps/L_MainMenu"
LOBBY_MAP_PATH = "/Game/Maps/L_Lobby"
CUBE = "/Engine/BasicShapes/Cube.Cube"
TEAM_MATERIAL = "M_PlaceholderTeam"
# How strongly units and buildings take on their team colour; scenery keeps its own colours.
TEAM_TINT = 0.7

# Package metadata key holding the hash of the code that built the asset.
HASH_TAG = "SetupHash"

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


def plan_rebuild(path, extension, build, digest):
    """Queues build(digest) unless the asset exists and was built from code with the same hash."""
    exists = assets.does_asset_exist(path)
    if exists and assets.get_metadata_tag(assets.load_asset(path), HASH_TAG) == digest:
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
                  max_count=0, airfield_pads=0):
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


def build_map(tank, infantry, builder, wall, crate, digest):
    if assets.does_asset_exist(MAP_PATH):
        assets.delete_asset(MAP_PATH)
    if not levels.new_level(MAP_PATH):
        raise RuntimeError(f"Could not create {MAP_PATH}")

    # 200 x 200 m ground, top face at z = 0.
    spawn_block("Ground", (0.0, 0.0, -50.0), (200.0, 200.0, 1.0))

    # Obstacles between the two bases so pathfinding has something to do.
    spawn_wall("Wall_West", (-2500.0, 0.0, 150.0), (2.0, 20.0, 3.0), wall)
    spawn_obstacle("Block_North", (1000.0, 2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_obstacle("Block_South", (1000.0, -2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_wall("Wall_East", (3500.0, 0.0, 150.0), (2.0, 12.0, 3.0), wall)

    sun = spawn(unreal.DirectionalLight, (0.0, 0.0, 2000.0), label="Sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-50.0, yaw=35.0), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("atmosphere_sun_light", True)

    sky = spawn(unreal.SkyLight, (0.0, 0.0, 1000.0), label="SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), label="SkyAtmosphere")

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


def make_map(tank, infantry, builder, wall, crate):
    plan_rebuild(MAP_PATH, ".umap", lambda digest: build_map(tank, infantry, builder, wall, crate, digest),
                 spec_hash([build_map, build_navigation, spawn, spawn_block, spawn_obstacle, spawn_wall, spawn_units, spawn_supply_source],
                           MAP_PATH, CUBE, tank, infantry, builder, wall, crate))


# The start spots of the skirmish maps, facing the middle: west and east as in L_TestMap, south and north on the empty
# strips. A map with fewer spots takes the first ones.
SKIRMISH_SPOTS = ((0, -6000.0, 0.0, 0.0), (1, 6000.0, 0.0, 180.0), (2, 0.0, -7000.0, 90.0), (3, 0.0, 7000.0, -90.0))


def build_skirmish_map(path, spot_count, builder, wall, crate, digest):
    """L_TestMap's ground and obstacles with spot_count start spots, each with only its builders and a supply source, and
    the skirmish game mode, which plays the seats the lobby chose (by default a player against an AI opponent from spots
    0 and 1) and lets every side build its army itself."""
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    if not levels.new_level(path):
        raise RuntimeError(f"Could not create {path}")

    spawn_block("Ground", (0.0, 0.0, -50.0), (200.0, 200.0, 1.0))
    spawn_wall("Wall_West", (-2500.0, 0.0, 150.0), (2.0, 20.0, 3.0), wall)
    spawn_obstacle("Block_North", (1000.0, 2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_obstacle("Block_South", (1000.0, -2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_wall("Wall_East", (3500.0, 0.0, 150.0), (2.0, 12.0, 3.0), wall)

    sun = spawn(unreal.DirectionalLight, (0.0, 0.0, 2000.0), label="Sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-50.0, yaw=35.0), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("atmosphere_sun_light", True)
    sky = spawn(unreal.SkyLight, (0.0, 0.0, 1000.0), label="SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), label="SkyAtmosphere")

    nav = spawn(unreal.NavMeshBoundsVolume, (0.0, 0.0, 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(100.0, 100.0, 10.0))

    for spot, x, y, yaw in SKIRMISH_SPOTS[:spot_count]:
        start = spawn(unreal.PlayerStart, (x, y, 100.0), yaw, label=f"PlayerStart_Team{spot}")
        start.set_editor_property("player_start_tag", f"Team{spot}")
        forward = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
        # The units of a spot carry its number; the game mode hands them to the team playing from it.
        spawn_units(builder, 2, spot, (x - 400.0 * forward[0], y - 400.0 * forward[1], 0.0), yaw)
        # To the left of the base, seen from the middle.
        spawn_supply_source((x - 50.0 * forward[0] - 2550.0 * forward[1], y - 50.0 * forward[1] + 2550.0 * forward[0], 0.0),
                            5000, (3, 3), crate)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.RTSSkirmishGameMode.static_class())
    build_navigation(world, path)
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {path}")


def make_skirmish_map(path, spot_count, builder, wall, crate):
    plan_rebuild(path, ".umap", lambda digest: build_skirmish_map(path, spot_count, builder, wall, crate, digest),
                 spec_hash([build_skirmish_map, build_navigation, spawn, spawn_block, spawn_obstacle, spawn_wall, spawn_units,
                            spawn_supply_source], path, spot_count, SKIRMISH_SPOTS, CUBE, builder, wall, crate))


def make_map_data(name, display_name, path, start_spots, default_seats):
    """default_seats: (seat type, start spot or -1 for random) pairs, at most one per start spot."""
    return plan_data_asset(name, MAP_DATA_DIR, unreal.RTSMapData, {
        "display_name": unreal.Text(display_name),
        "map": unreal.SoftObjectPath(f"{path}.{path.rsplit('/', 1)[1]}"),
        "start_spots": start_spots,
        "default_seats": [make_struct(unreal.RTSSeat, {"type": seat_type, "start_spot": spot})
                          for seat_type, spot in default_seats],
    })


def build_menu_map(path, game_mode, digest):
    """An empty level that only shows the menu screen of game_mode."""
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    if not levels.new_level(path):
        raise RuntimeError(f"Could not create {path}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode.static_class())
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {path}")


def make_menu_map(path, game_mode):
    plan_rebuild(path, ".umap", lambda digest: build_menu_map(path, game_mode, digest),
                 spec_hash([build_menu_map], path, game_mode.static_class().get_name()))


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
buildings = [power_plant, tower, supply_depot, barracks, vehicle_factory, headquarters, airfield]
# The headquarters produces the builder, which builds the rest.
add_properties(builder, {"buildings": buildings})
# A harvester needs a depot to deliver to, so the factory only offers one once a depot stands.
add_properties(harvester, {"prerequisites": [supply_depot]})
# The tech tree: vehicles only after infantry, and a tower only once a plant can power it.
add_properties(vehicle_factory, {"prerequisites": [barracks]})
add_properties(airfield, {"prerequisites": [vehicle_factory]})
add_properties(tower, {"prerequisites": [power_plant]})
# The AI's base: a second plant before the tower, so the tower's draw never puts it on low power.
build_order = make_build_order("DA_TestBuildOrder", [
    (builder, 2), (power_plant, 1), (supply_depot, 1), (barracks, 1), (infantry, 3), (vehicle_factory, 1), (tank, 2),
    (power_plant, 2), (tower, 1),
], [infantry, tank], 90.0, 8, 4, {
    unreal.RTSAIDifficulty.EASY: (1.5, 0.5),
    unreal.RTSAIDifficulty.HARD: (0.67, 1.5),
})
make_faction("DA_TestFaction", "Test Faction", buildings, headquarters, ai_build_order=build_order)
make_ghost_material("M_PlacementGhost")
make_shot_material("M_ShotFx")
make_map(tank, infantry, builder, wall_mesh, crate_mesh)
make_skirmish_map(SKIRMISH_MAP_PATH, 4, builder, wall_mesh, crate_mesh)
make_skirmish_map(DUEL_MAP_PATH, 2, builder, wall_mesh, crate_mesh)
# The lobby's maps, listed in Config/DefaultGame.ini: by default a player against an AI opponent; the other seats are open.
make_map_data("DA_MapSkirmish", "Test Crossroads", SKIRMISH_MAP_PATH, 4,
              [(unreal.RTSSeatType.OPEN, 0), (unreal.RTSSeatType.AI, 1), (unreal.RTSSeatType.CLOSED, -1),
               (unreal.RTSSeatType.CLOSED, -1)])
make_map_data("DA_MapDuel", "Test Duel", DUEL_MAP_PATH, 2, [(unreal.RTSSeatType.OPEN, 0), (unreal.RTSSeatType.AI, 1)])
make_menu_map(MAIN_MENU_MAP_PATH, unreal.RTSMainMenuGameMode)
make_menu_map(LOBBY_MAP_PATH, unreal.RTSLobbyGameMode)
plan_data_assets()
write_pending()
