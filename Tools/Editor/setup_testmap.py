"""Builds the placeholder weapon, unit, harvester, building and faction data assets, the placement ghost material and the
L_TestMap level with a supply source by each base. Safe to re-run: only assets that differ from this script are saved, so
a run without script changes writes nothing. Data assets are compared property by property; the material and the map carry
a hash of the code that builds them and are rebuilt only when that code changes.

The assets are LFS lockable and therefore read-only unless locked. If an asset that must change is read-only, nothing
is written: the files are listed in Saved/Setup/ReadOnlyAssets.txt and the run stops. Lock them (git lfs lock) and rerun.

Run from the repo root (adjust the engine path), or use Tools/Scripts/ue.ps1 -Task setup:
  "G:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/WorldDivide.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/setup_testmap.py" -unattended -nosplash -nullrhi
"""

import hashlib
import inspect
import math
import os

import unreal

UNIT_DIR = "/Game/Data/Units"
WEAPON_DIR = "/Game/Data/Weapons"
BUILDING_DIR = "/Game/Data/Buildings"
FACTION_DIR = "/Game/Data/Factions"
MATERIAL_DIR = "/Game/Materials"
MAP_PATH = "/Game/Maps/L_TestMap"
CUBE = "/Engine/BasicShapes/Cube.Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"

# Package metadata key holding the hash of the code that built the asset.
HASH_TAG = "SetupHash"

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
CONTENT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
READ_ONLY_REPORT = os.path.join(PROJECT_DIR, "Saved", "Setup", "ReadOnlyAssets.txt")

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

# Assets that need saving: (package path, file extension, already on disk, function that writes it).
pending = []


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
    """Loads or creates a data asset and queues a save if it is new or any property differs."""
    path = f"{directory}/{name}"
    exists = assets.does_asset_exist(path)
    if exists:
        asset = assets.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", data_class)
        asset = asset_tools.create_asset(name, directory, data_class, factory)

    changed = {key: value for key, value in properties.items() if differs(asset.get_editor_property(key), value)}
    if changed or not exists:
        def write():
            for key, value in changed.items():
                asset.set_editor_property(key, value)
            if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
                raise RuntimeError(f"Could not save {path}")
        pending.append((path, ".uasset", exists, write))
    return asset


def plan_rebuild(path, extension, build, digest):
    """Queues build(digest) unless the asset exists and was built from code with the same hash."""
    exists = assets.does_asset_exist(path)
    if exists and assets.get_metadata_tag(assets.load_asset(path), HASH_TAG) == digest:
        return
    pending.append((path, extension, exists, lambda: build(digest)))


def make_weapon(name, weapon_range, damage, reload_seconds):
    return plan_data_asset(name, WEAPON_DIR, unreal.RTSWeaponData, {
        "range": weapon_range,
        "damage": damage,
        "reload_seconds": reload_seconds,
    })


def make_unit(name, display_name, mesh, scale, radius, health, weapon, speed, turn_rate, cost, build_time):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSUnitData, {
        "display_name": unreal.Text(display_name),
        "mesh": unreal.load_asset(mesh),
        "mesh_scale": unreal.Vector(*scale),
        "selection_radius": radius,
        "max_health": health,
        "weapon": weapon,
        "move_speed": speed,
        "turn_rate": turn_rate,
        "cost": cost,
        "build_time": build_time,
    })


def make_harvester(name, display_name, mesh, scale, radius, health, speed, turn_rate, cost, build_time,
                   carry_amount, gather_seconds, search_radius):
    return plan_data_asset(name, UNIT_DIR, unreal.RTSHarvesterData, {
        "display_name": unreal.Text(display_name),
        "mesh": unreal.load_asset(mesh),
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


def make_building(name, display_name, mesh, scale, footprint, health, weapon, cost, build_time, power_output, power_draw,
                  spawn_on_complete=None, accepts_supply=False):
    return plan_data_asset(name, BUILDING_DIR, unreal.RTSBuildingData, {
        "display_name": unreal.Text(display_name),
        "mesh": unreal.load_asset(mesh),
        "mesh_scale": unreal.Vector(*scale),
        "footprint": unreal.IntPoint(*footprint),
        "selection_radius": max(footprint) * 50.0,
        "max_health": health,
        "weapon": weapon,
        "cost": cost,
        "build_time": build_time,
        "power_output": power_output,
        "power_draw": power_draw,
        "spawn_on_complete": spawn_on_complete,
        "accepts_supply": accepts_supply,
    })


def make_faction(name, display_name, buildings):
    return plan_data_asset(name, FACTION_DIR, unreal.RTSFactionData, {
        "display_name": unreal.Text(display_name),
        "buildings": buildings,
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


def spawn_units(unit, count, team_id, location, yaw):
    spawner = spawn(unreal.RTSUnitSpawner, location, yaw, label=f"Spawn_Team{team_id}_{unit.get_name()}")
    spawner.set_editor_property("unit_data", unit)
    spawner.set_editor_property("count", count)
    spawner.set_editor_property("team_id", team_id)
    return spawner


def spawn_supply_source(location, amount, footprint):
    source = spawn(unreal.RTSSupplySource, location, label=f"SupplySource_{int(location[0])}_{int(location[1])}")
    source.set_editor_property("amount", amount)
    source.set_editor_property("footprint", unreal.IntPoint(*footprint))
    source.set_editor_property("mesh", unreal.load_asset(CUBE))
    return source


def build_map(tank, infantry, digest):
    if assets.does_asset_exist(MAP_PATH):
        assets.delete_asset(MAP_PATH)
    if not levels.new_level(MAP_PATH):
        raise RuntimeError(f"Could not create {MAP_PATH}")

    # 200 x 200 m ground, top face at z = 0.
    spawn_block("Ground", (0.0, 0.0, -50.0), (200.0, 200.0, 1.0))

    # Obstacles between the two bases so pathfinding has something to do.
    spawn_block("Wall_West", (-2500.0, 0.0, 150.0), (2.0, 20.0, 3.0))
    spawn_block("Block_North", (1000.0, 2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_block("Block_South", (1000.0, -2500.0, 200.0), (8.0, 8.0, 4.0))
    spawn_block("Wall_East", (3500.0, 0.0, 150.0), (2.0, 12.0, 3.0))

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
        # Beside each base, clear of the units' paths and of the empty strip at y = 7000 the tests build in.
        spawn_supply_source((x - 50.0 * forward, 2550.0 * forward, 0.0), 5000, (3, 3))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assets.set_metadata_tag(world, HASH_TAG, digest)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {MAP_PATH}")


def make_map(tank, infantry):
    plan_rebuild(MAP_PATH, ".umap", lambda digest: build_map(tank, infantry, digest),
                 spec_hash([build_map, spawn, spawn_block, spawn_units, spawn_supply_source], MAP_PATH, CUBE, tank, infantry))


def asset_file(path, extension):
    return os.path.join(CONTENT_DIR, path[len("/Game/"):] + extension)


def write_pending():
    """Writes every queued asset, or nothing at all when one of the existing files is read-only."""
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

cannon = make_weapon("DA_TestCannon", 1200.0, 40.0, 2.0)
rifle = make_weapon("DA_TestRifle", 700.0, 8.0, 0.5)
tank = make_unit("DA_TestTank", "Test Tank", CUBE, (2.4, 1.4, 0.9), 90.0, 200.0, cannon, 600.0, 180.0, 700, 10.0)
infantry = make_unit("DA_TestInfantry", "Test Infantry", CYLINDER, (0.5, 0.5, 1.0), 30.0, 100.0, rifle, 350.0, 540.0, 150, 4.0)
tower_gun = make_weapon("DA_TestTowerGun", 1400.0, 30.0, 1.0)
power_plant = make_building("DA_TestPowerPlant", "Test Power Plant", CUBE, (3.8, 3.8, 2.0), (4, 4), 800.0, None, 800, 6.0, 5, 0)
tower = make_building("DA_TestTower", "Test Tower", CYLINDER, (1.6, 1.6, 3.0), (2, 2), 500.0, tower_gun, 600, 3.0, 0, 2)
harvester = make_harvester("DA_TestHarvester", "Test Harvester", CUBE, (1.6, 1.1, 0.8), 70.0, 300.0, 450.0, 360.0, 600, 8.0,
                           100, 3.0, 6000.0)
supply_depot = make_building("DA_TestSupplyDepot", "Test Supply Depot", CUBE, (2.8, 2.8, 1.4), (3, 3), 1000.0, None, 1500,
                             5.0, 0, 1, spawn_on_complete=harvester, accepts_supply=True)
make_faction("DA_TestFaction", "Test Faction", [power_plant, tower, supply_depot])
make_ghost_material("M_PlacementGhost")
make_map(tank, infantry)
write_pending()
