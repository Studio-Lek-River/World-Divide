"""Builds the placeholder weapon, unit, building and faction data assets, the placement ghost material and the L_TestMap
level. Safe to re-run: everything is rebuilt.

Run from the repo root (adjust the engine path):
  "G:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/WorldDivide.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/setup_testmap.py" -unattended -nosplash -nullrhi
"""

import unreal

UNIT_DIR = "/Game/Data/Units"
WEAPON_DIR = "/Game/Data/Weapons"
BUILDING_DIR = "/Game/Data/Buildings"
FACTION_DIR = "/Game/Data/Factions"
MATERIAL_DIR = "/Game/Materials"
MAP_PATH = "/Game/Maps/L_TestMap"
CUBE = "/Engine/BasicShapes/Cube.Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def load_or_create(name, directory, data_class):
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        return assets.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", data_class)
    return asset_tools.create_asset(name, directory, data_class, factory)


def make_weapon(name, weapon_range, damage, reload_seconds):
    weapon = load_or_create(name, WEAPON_DIR, unreal.RTSWeaponData)
    weapon.set_editor_property("range", weapon_range)
    weapon.set_editor_property("damage", damage)
    weapon.set_editor_property("reload_seconds", reload_seconds)
    assets.save_loaded_asset(weapon, only_if_is_dirty=False)
    return weapon


def make_unit(name, display_name, mesh, scale, radius, health, weapon, speed, turn_rate, cost, build_time):
    unit = load_or_create(name, UNIT_DIR, unreal.RTSUnitData)

    unit.set_editor_property("display_name", unreal.Text(display_name))
    unit.set_editor_property("mesh", unreal.load_asset(mesh))
    unit.set_editor_property("mesh_scale", unreal.Vector(*scale))
    unit.set_editor_property("selection_radius", radius)
    unit.set_editor_property("max_health", health)
    unit.set_editor_property("weapon", weapon)
    unit.set_editor_property("move_speed", speed)
    unit.set_editor_property("turn_rate", turn_rate)
    unit.set_editor_property("cost", cost)
    unit.set_editor_property("build_time", build_time)
    assets.save_loaded_asset(unit, only_if_is_dirty=False)
    return unit


def make_building(name, display_name, mesh, scale, footprint, health, weapon, cost, build_time, power_output, power_draw):
    building = load_or_create(name, BUILDING_DIR, unreal.RTSBuildingData)

    building.set_editor_property("display_name", unreal.Text(display_name))
    building.set_editor_property("mesh", unreal.load_asset(mesh))
    building.set_editor_property("mesh_scale", unreal.Vector(*scale))
    building.set_editor_property("footprint", unreal.IntPoint(*footprint))
    building.set_editor_property("selection_radius", max(footprint) * 50.0)
    building.set_editor_property("max_health", health)
    building.set_editor_property("weapon", weapon)
    building.set_editor_property("cost", cost)
    building.set_editor_property("build_time", build_time)
    building.set_editor_property("power_output", power_output)
    building.set_editor_property("power_draw", power_draw)
    assets.save_loaded_asset(building, only_if_is_dirty=False)
    return building


def make_faction(name, display_name, buildings):
    faction = load_or_create(name, FACTION_DIR, unreal.RTSFactionData)
    faction.set_editor_property("display_name", unreal.Text(display_name))
    faction.set_editor_property("buildings", buildings)
    assets.save_loaded_asset(faction, only_if_is_dirty=False)
    return faction


def make_ghost_material(name):
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
    assets.save_loaded_asset(material, only_if_is_dirty=False)
    return material


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


def build_map(tank, infantry):
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

    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {MAP_PATH}")


cannon = make_weapon("DA_TestCannon", 1200.0, 40.0, 2.0)
rifle = make_weapon("DA_TestRifle", 700.0, 8.0, 0.5)
tank = make_unit("DA_TestTank", "Test Tank", CUBE, (2.4, 1.4, 0.9), 90.0, 200.0, cannon, 600.0, 180.0, 700, 10.0)
infantry = make_unit("DA_TestInfantry", "Test Infantry", CYLINDER, (0.5, 0.5, 1.0), 30.0, 100.0, rifle, 350.0, 540.0, 150, 4.0)
tower_gun = make_weapon("DA_TestTowerGun", 1400.0, 30.0, 1.0)
power_plant = make_building("DA_TestPowerPlant", "Test Power Plant", CUBE, (3.8, 3.8, 2.0), (4, 4), 800.0, None, 800, 6.0, 5, 0)
tower = make_building("DA_TestTower", "Test Tower", CYLINDER, (1.6, 1.6, 3.0), (2, 2), 500.0, tower_gun, 600, 3.0, 0, 2)
make_faction("DA_TestFaction", "Test Faction", [power_plant, tower])
make_ghost_material("M_PlacementGhost")
build_map(tank, infantry)
unreal.log("setup_testmap: done")
