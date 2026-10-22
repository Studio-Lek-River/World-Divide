"""The greybox of WD_RiverCrossing_01 (#154): the layout of docs/world/maps/WD_RiverCrossing_01/02_MAP_DESIGN.md as a
landscape, engine cubes, a Water plugin river and the concrete bridges of Tools/Art/bridge_blender.py (#155).
setup_rivercrossing.py builds it only when the level does not exist yet, or anew with ue.ps1 -Task rivercrossing
-Rebuild: from then on the level is authored by hand (03_TECHNICAL_PLAN.md section 2) and this file is never run on it
again. The river carves its bed through the landscape's Water edit layer on the GPU, so the build needs a renderer
(-Render).

Coordinates are in metres with the origin at the map centre, +X east and +Y north. Every height and every actor is
point-symmetric about the centre (180 degrees): what stands at (x, y) for the west side stands at (-x, -y) for the east,
so routes, cover and supply are equal for both sides by construction.
"""

import json
import math
import os
import struct

import unreal

MAP_PATH = "/Game/Maps/WD_RiverCrossing_01"
CUBE = "/Engine/BasicShapes/Cube.Cube"
GROUND_MATERIAL = "/Game/Materials/M_Ground"
# Landscape collision ignores M_Ground's physical material: it reads the layers' ones, else the landscape's default
# (#337, docs/engine/world-landscape.md). Per-layer grass, road, mud and water come with the landscape material (#156).
GROUND_PHYSICAL_MATERIAL = "/Game/Materials/Physical/PM_Dirt"
CRATE_MESH = "/Game/Art/Placeholder/Meshes/SM_SupplyCrate"
BUILDER = "/Game/Data/Units/DA_TestBuilder"

# Landscape (03 section 4): 16 x 16 components of 2 x 2 sections of 63 quads, 2017 vertices a side, 1.25 m a quad, so
# 2,520 m across: the 2,000 m playable area and a 260 m border ring on every side.
COMPONENTS = 16
QUADS_PER_SECTION = 63
VERTICES = COMPONENTS * 2 * QUADS_PER_SECTION + 1
QUAD = 1.25
LANDSCAPE_HALF = (VERTICES - 1) * QUAD / 2.0
# Z scale 100: one height step is 100 / 128 cm, 32768 is the actor's Z.
HEIGHT_STEPS_PER_METRE = 128.0
PLAYABLE_HALF = 1000.0
BORDER_HEIGHT = 60.0

# Bases (02 "Bases"): the west start spot and its 350 m flat pad, the east one mirrored.
WEST_BASE = (-720.0, -220.0)
PAD_HALF = 175.0
PAD_FALLOFF = 80.0
# The west hill (15 to 25 m high, slopes under 20 degrees) overlooks the west side's home-bridge approach.
WEST_HILL = (-350.0, -650.0)
HILL_HEIGHT = 20.0
HILL_RADIUS = 140.0
# The village rise: 4 m over an ellipse straddling the river between the bridges.
VILLAGE_RISE = 4.0
VILLAGE_RADII = (220.0, 320.0)
# 2 to 8 m of rolling ground over the fields, flattened on the pads, by the bridges and along the river; raised by
# ROLLING_LIFT so no dip reaches the water level and shows as a pond.
ROLLING = ((2.0, 170.0, 210.0, "cos"), (1.5, 95.0, 130.0, "sin"))
ROLLING_LIFT = 1.5

# River (02 "River"): an S-curve x = RIVER_SWING * sin(pi * y / RIVER_HALF_PERIOD), which crosses x = 0 at the bridges
# (y = +-300) and maps onto itself under the rotation; 45 m wide at the bridges, 70 m at the bends.
RIVER_SWING = 60.0
RIVER_HALF_PERIOD = 300.0
RIVER_NARROW = 45.0
RIVER_WIDE = 70.0
RIVER_FLAT = 20.0
RIVER_FLAT_FALLOFF = 60.0
# The Water plugin river (03 section 7): its surface at WATER_LEVEL, a point every RIVER_POINT_SPACING metres edge to
# edge, RIVER_SHALLOW deep at the bridges and RIVER_DEEP at the bends. Its brush carves the bed into the landscape and
# raises the banks from RIVER_SHORE over the water at RIVER_BANK_ANGLE degrees, far steeper than the nav agents' 44.
WATER_LEVEL = -3.0
RIVER_POINT_SPACING = 40.0
RIVER_SHALLOW = 2.0
RIVER_DEEP = 3.0
RIVER_BANK_ANGLE = 60.0
RIVER_SHORE = 0.16
# The engine's river material and the instance that gives it River Crossing's murky green-brown lowland water.
RIVER_MATERIAL_PARENT = "/Water/Materials/WaterSurface/Water_Material_River"
RIVER_MATERIAL_DIR = "/Game/Materials/Water"
RIVER_MATERIAL = "MI_RiverCrossing_River"
RIVER_MATERIAL_VECTORS = {
    "Absorption": (2.2, 2.0, 2.6, 8.0),
    "Scattering": (1.8, 2.2, 0.8, 0.5),
    "Water Albedo": (0.3, 0.28, 0.14, 0.5),
}
# Water_ nav blockers: NavArea_Null boxes along the river, this long each, wider than the river by the margin, from the
# bed to below the bridge decks.
WATER_SEGMENT = 40.0
WATER_MARGIN = 4.0
WATER_BOTTOM = -8.0
WATER_TOP = -1.0

# Bridges (02 "Bridges"): the north one at (0, 300), the south one mirrored, along x across the river: the spans, piers
# and abutments of Tools/Art/bridge_blender.py (Art/Shared/bridge/bridge.json), BRIDGE_SPANS spans of BRIDGE_SPAN, 14 m
# wide with a 13 m carriageway, its deck's top BRIDGE_DECK_TOP over the flattened approaches, 6 m over the water. At each
# end an earthen ramp: flat at the deck's top for BRIDGE_RAMP_TOP past the abutment's approach slab (BRIDGE_ABUTMENT),
# then down to the approach over BRIDGE_RAMP (5.7 degrees, under the 8 of 02), BRIDGE_RAMP_HALF_WIDTH to each side of the
# road and falling to the ground over BRIDGE_RAMP_SIDE beyond, steeper than any agent climbs. Both keep the navmesh
# linked across its tile seam at 64 m (TileSizeUU, Config/DefaultEngine.ini), which the ramp would otherwise cross: the
# flat top runs on past it, and the steep sides give the road its own polygons there. With walkable sides the far tile
# drew the ramp and the fields round it as one polygon at field height, which Detour does not link to the road's at the
# deck's height, so the hull clearance (RTSHullClearance) read a wall across the road and every tank stopped there.
BRIDGE_Y = 300.0
BRIDGE_SPANS = 3
BRIDGE_SPAN = 30.0
BRIDGE_LENGTH = BRIDGE_SPANS * BRIDGE_SPAN
BRIDGE_DECK_TOP = 3.0
BRIDGE_ABUTMENT = 1.5
BRIDGE_RAMP_TOP = 19.5
BRIDGE_RAMP = 30.0
BRIDGE_RAMP_HALF_WIDTH = 9.0
BRIDGE_RAMP_SIDE = 2.0
BRIDGE_FLAT = (150.0, 30.0)
BRIDGE_FLAT_FALLOFF = 60.0
BRIDGE_SOURCE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Art", "Shared", "bridge")
BRIDGE_DIR = "/Game/Art/Shared/Bridge"
BRIDGE_MESHES = ("SM_Bridge_Span", "SM_Bridge_Pier", "SM_Bridge_Abutment")
# The bridge's master material: a colour and a roughness, varied over a few metres of world space so a 30 m span does not
# read as one flat grey; road dust on both (PM_Road). The two slots' instances: concrete and the asphalt carriageway.
BRIDGE_PHYSICAL_MATERIAL = "/Game/Materials/Physical/PM_Road"
BRIDGE_MASTER = "M_Bridge"
BRIDGE_VARIATION_SCALE = 450.0
BRIDGE_VARIATION = (0.82, 1.08)
BRIDGE_SLOTS = {
    "Concrete": ("MI_Bridge_Concrete", (0.24, 0.235, 0.22), 0.85),
    "Asphalt": ("MI_Bridge_Asphalt", (0.045, 0.045, 0.048), 0.75),
}

# Supply sources (02 "Bases" and "Central village"), west side: safe behind the base, forward on the far flank, and the
# village stockpile; each with its amount. The east side has the same at (-x, -y). The safe source lies 90 m behind the
# start spot, within the AI's depot reach (DepotSourceDistance in RTSAIPlayer.cpp), and holds enough to carry a side
# through a match on its own: the skirmish AI does not yet expand to the forward and village sources.
WEST_SOURCES = (
    ("Safe", (-806.0, -246.0), 15000),
    ("Forward", (-400.0, 500.0), 8000),
    ("Village", (-90.0, 40.0), 10000),
)
SUPPLY_FOOTPRINT = (12, 12)
SUPPLY_HEIGHT = 300.0
# Each spot starts with two builders this far behind it, as on the generated skirmish maps.
BUILDERS = 2
BUILDERS_BEHIND = 24.0

# Village blockout (02 "Central village"), the west bank: (x, y, length along x, width along y, height). The east bank
# is the same turned about the centre. Streets of at least 10 m between faces; every house stays clear of the river.
WEST_VILLAGE = (
    (-135.0, -190.0, 16.0, 12.0, 8.0),
    (-135.0, -130.0, 14.0, 14.0, 7.0),
    (-180.0, -160.0, 16.0, 16.0, 9.0),
    (-135.0, -70.0, 16.0, 12.0, 8.0),
    (-180.0, -95.0, 14.0, 18.0, 7.0),
    (-125.0, 0.0, 18.0, 14.0, 12.0),
    (-180.0, 10.0, 16.0, 14.0, 8.0),
    (-130.0, 80.0, 14.0, 14.0, 7.0),
    (-180.0, 90.0, 16.0, 12.0, 8.0),
    (-120.0, 150.0, 16.0, 14.0, 9.0),
)
VILLAGE_RIVER_CLEARANCE = 15.0

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editing = unreal.MaterialEditingLibrary


def smoothstep(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def rotate(point):
    """The point turned 180 degrees about the map centre: the east side's copy of a west-side position."""
    return (-point[0], -point[1])


def river_x(y):
    return RIVER_SWING * math.sin(math.pi * y / RIVER_HALF_PERIOD)


def river_slope(y):
    """dx/dy of the river's centre line."""
    return RIVER_SWING * math.pi / RIVER_HALF_PERIOD * math.cos(math.pi * y / RIVER_HALF_PERIOD)


def river_width(y):
    return RIVER_NARROW + (RIVER_WIDE - RIVER_NARROW) * abs(math.sin(math.pi * y / RIVER_HALF_PERIOD))


def river_distance(x, y):
    """About how far (x, y) is from the river's centre line, across the river, and the river's half width there."""
    return abs(x - river_x(y)) / math.sqrt(1.0 + river_slope(y) ** 2), river_width(y) / 2.0


def box_mask(dx, dy, half_x, half_y, falloff):
    """1 inside the box of half sizes half_x, half_y around a point dx, dy away, falling to 0 over falloff metres."""
    return 1.0 - smoothstep(max(abs(dx) - half_x, abs(dy) - half_y, 0.0) / falloff)


def bump(dx, dy, radius_x, radius_y, height):
    """A cosine hill of height, radius_x by radius_y across its foot."""
    r = math.sqrt((dx / radius_x) ** 2 + (dy / radius_y) ** 2)
    return height * 0.5 * (1.0 + math.cos(math.pi * r)) if r < 1.0 else 0.0


def river_depth(y):
    return RIVER_SHALLOW + (RIVER_DEEP - RIVER_SHALLOW) * abs(math.sin(math.pi * y / RIVER_HALF_PERIOD))


def ramp(x, y, bridge_y):
    """How high the ramps of the bridge at bridge_y lift (x, y): the deck's top behind each abutment's approach slab, down
    to nothing BRIDGE_RAMP further on, and nothing under the bridge, where the abutment's stem stands in front of it."""
    along = abs(x) - BRIDGE_LENGTH / 2.0
    if along < BRIDGE_ABUTMENT / 2.0:
        return 0.0
    rise = BRIDGE_DECK_TOP * min(1.0, max(0.0, 1.0 - (along - BRIDGE_ABUTMENT - BRIDGE_RAMP_TOP) / BRIDGE_RAMP))
    side = 1.0 - smoothstep((abs(y - bridge_y) - BRIDGE_RAMP_HALF_WIDTH) / BRIDGE_RAMP_SIDE)
    return rise * side


def height(x, y):
    """The greybox ground height at (x, y), in metres. Equal at (x, y) and (-x, -y). The river's bed is not in it: the
    river's water brush carves it (create_river)."""
    rolling = ROLLING_LIFT
    for amplitude, wave_x, wave_y, kind in ROLLING:
        # cos * cos and sin * sin both keep their value under (x, y) -> (-x, -y).
        wave = math.cos if kind == "cos" else math.sin
        rolling += amplitude * wave(x / wave_x) * wave(y / wave_y)

    # The base pads and the bridge approaches are flat at 0.
    flat = 0.0
    for base in (WEST_BASE, rotate(WEST_BASE)):
        flat = max(flat, box_mask(x - base[0], y - base[1], PAD_HALF + 20.0, PAD_HALF + 20.0, PAD_FALLOFF))
    for bridge_y in (BRIDGE_Y, -BRIDGE_Y):
        flat = max(flat, box_mask(x, y - bridge_y, BRIDGE_FLAT[0], BRIDGE_FLAT[1], BRIDGE_FLAT_FALLOFF))

    distance, half_width = river_distance(x, y)
    # No rolling dip by the river, so every bank stands well above the bed.
    river_flat = 1.0 - smoothstep((distance - half_width - RIVER_FLAT) / RIVER_FLAT_FALLOFF)

    features = 0.0
    for hill in (WEST_HILL, rotate(WEST_HILL)):
        features += bump(x - hill[0], y - hill[1], HILL_RADIUS, HILL_RADIUS, HILL_HEIGHT)
    features += bump(x, y, VILLAGE_RADII[0], VILLAGE_RADII[1], VILLAGE_RISE)
    features += BORDER_HEIGHT * smoothstep((max(abs(x), abs(y)) - PLAYABLE_HALF) / (LANDSCAPE_HALF - PLAYABLE_HALF))

    ground = rolling * (1.0 - max(flat, river_flat)) + features * (1.0 - flat)
    lift = max(ramp(x, y, BRIDGE_Y), ramp(x, y, -BRIDGE_Y))
    return ground + max(0.0, lift - ground)


def write_heightmap(file_name):
    """Writes the greybox heights, VERTICES x VERTICES 16-bit steps, row by row from the south-west corner along +X."""
    rows = []
    for row in range(VERTICES):
        y = -LANDSCAPE_HALF + row * QUAD
        steps = [min(65535, max(0, round(32768 + height(-LANDSCAPE_HALF + column * QUAD, y) * HEIGHT_STEPS_PER_METRE)))
                 for column in range(VERTICES)]
        rows.append(struct.pack(f"<{VERTICES}H", *steps))
    os.makedirs(os.path.dirname(file_name), exist_ok=True)
    with open(file_name, "wb") as file:
        file.write(b"".join(rows))


def check_layout():
    """Raises when the layout breaks a rule the design depends on: a house or a supply source in the river or too close
    to it, or two houses with less than a 10 m street between them."""
    houses = [(x, y, length, width) for x, y, length, width, _ in WEST_VILLAGE]
    for x, y, length, width in houses:
        for corner_x in (x - length / 2.0, x + length / 2.0):
            for corner_y in (y - width / 2.0, y + width / 2.0):
                distance, half_width = river_distance(corner_x, corner_y)
                if distance < half_width + VILLAGE_RIVER_CLEARANCE:
                    raise RuntimeError(f"Village house at ({x}, {y}) stands within {VILLAGE_RIVER_CLEARANCE} m of the river")
    everything = houses + [(*rotate((x, y)), length, width) for x, y, length, width in houses]
    for index, (x, y, length, width) in enumerate(everything):
        for other_x, other_y, other_length, other_width in everything[index + 1:]:
            gap = max(abs(x - other_x) - (length + other_length) / 2.0, abs(y - other_y) - (width + other_width) / 2.0)
            if gap < 10.0:
                raise RuntimeError(f"Village houses at ({x}, {y}) and ({other_x}, {other_y}) leave a {gap:.1f} m street")
    for name, (x, y), _ in WEST_SOURCES:
        distance, half_width = river_distance(x, y)
        if distance < half_width + 20.0:
            raise RuntimeError(f"Supply source {name} lies within 20 m of the river")


def cm(point, z=0.0):
    return unreal.Vector(point[0] * 100.0, point[1] * 100.0, z * 100.0)


def apply_landscape_physics(world):
    """Gives every landscape in world GROUND_PHYSICAL_MATERIAL as its default physical material and rebuilds its
    collision, so the vehicles' dust (RTSGround::TraceSurface) finds dirt on it. Returns how many changed."""
    material = unreal.load_asset(GROUND_PHYSICAL_MATERIAL)
    if not material:
        raise RuntimeError(f"Could not load {GROUND_PHYSICAL_MATERIAL}")
    return unreal.RTSEditorScripting.set_landscape_physical_material(world, material)


def spawn(actor_class, location, yaw=0.0, label=None):
    actor = actors.spawn_actor_from_class(actor_class, location, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    return actor


def spawn_cube(label, center, size, yaw=0.0, material=None, collides=True):
    """An engine cube of size (x, y, z) metres around center (x, y, z)."""
    cube = spawn(unreal.StaticMeshActor, cm(center[:2], center[2]), yaw, label)
    mesh = cube.static_mesh_component
    mesh.set_static_mesh(unreal.load_asset(CUBE))
    cube.set_actor_scale3d(unreal.Vector(*size))
    if material:
        mesh.set_material(0, unreal.load_asset(material))
    if not collides:
        mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        mesh.set_editor_property("can_ever_affect_navigation", False)
    return cube


def spawn_nav_null(label, center, size, yaw=0.0):
    """A NavArea_Null box of size (x, y, z) metres around center; the default volume brush is a 2 m cube."""
    volume = spawn(unreal.NavModifierVolume, cm(center[:2], center[2]), yaw, label)
    volume.set_actor_scale3d(unreal.Vector(size[0] / 2.0, size[1] / 2.0, size[2] / 2.0))
    volume.set_editor_property("area_class", unreal.NavArea_Null)
    return volume


def spawn_water_blockers():
    """The Water_ nav blockers along the whole river, edge to edge past the playable area."""
    y = -LANDSCAPE_HALF
    index = 0
    while y < LANDSCAPE_HALF:
        end = min(y + WATER_SEGMENT, LANDSCAPE_HALF)
        start_x, end_x = river_x(y), river_x(end)
        length = math.hypot(end_x - start_x, end - y) + 2.0
        width = max(river_width(y), river_width(end)) + WATER_MARGIN
        yaw = math.degrees(math.atan2(end - y, end_x - start_x))
        center = ((start_x + end_x) / 2.0, (y + end) / 2.0, (WATER_BOTTOM + WATER_TOP) / 2.0)
        spawn_nav_null(f"Water_{index:03d}", center, (length, width, WATER_TOP - WATER_BOTTOM), yaw)
        y = end
        index += 1


def instance(directory, name, parent):
    """The material instance directory/name of parent, its parameters cleared, new or the one there. Does not save."""
    path = f"{directory}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        made = unreal.load_asset(path)
        editing.clear_all_material_instance_parameters(made)
    else:
        made = asset_tools.create_asset(name, directory, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(made, parent)
    return made


def build_river_material():
    """MI_RiverCrossing_River: the engine's river material with RIVER_MATERIAL_VECTORS. Does not save."""
    parent = unreal.load_asset(RIVER_MATERIAL_PARENT)
    if not parent:
        raise RuntimeError(f"Could not load {RIVER_MATERIAL_PARENT}: is the Water plugin enabled?")
    river = instance(RIVER_MATERIAL_DIR, RIVER_MATERIAL, parent)
    for name, value in sorted(RIVER_MATERIAL_VECTORS.items()):
        editing.set_material_instance_vector_parameter_value(river, name, unreal.LinearColor(*value))
    editing.update_material_instance(river)
    # Read back: a name the parent does not have is dropped without a word.
    for name, value in sorted(RIVER_MATERIAL_VECTORS.items()):
        got = editing.get_material_instance_vector_parameter_value(river, name)
        if abs(got.r - value[0]) > 1e-3 or abs(got.b - value[2]) > 1e-3:
            raise RuntimeError(f"{RIVER_MATERIAL}: {name} is {got}, not {value}; has {RIVER_MATERIAL_PARENT} renamed it?")
    return river


def create_river(world, material):
    """The Water plugin river along the centre line, edge to edge, carving its bed into the landscape (#155)."""
    points, widths, depths = [], [], []
    steps = int(math.ceil(2.0 * LANDSCAPE_HALF / RIVER_POINT_SPACING))
    for step in range(steps + 1):
        y = -LANDSCAPE_HALF + 2.0 * LANDSCAPE_HALF * step / steps
        points.append(cm((river_x(y), y), WATER_LEVEL))
        widths.append(river_width(y) * 100.0)
        depths.append(river_depth(y) * 100.0)
    river = unreal.RTSEditorScripting.create_river(world, points, widths, depths, RIVER_BANK_ANGLE, RIVER_SHORE * 100.0,
                                                   material)
    if not river:
        raise RuntimeError("The Water plugin river did not carve the landscape (CreateRiver in the log says why)")
    return river


def read_bridge_records():
    with open(os.path.join(BRIDGE_SOURCE_DIR, "bridge.json"), encoding="utf-8") as file:
        return json.load(file)


def build_bridge_assets():
    """M_Bridge, its two instances and the bridge's meshes from Art/Shared/bridge (Nanite, their UCX_ collision kept),
    checked against bridge.json. Does not save; returns the meshes by name."""
    records = read_bridge_records()["meshes"]
    master = build_bridge_master()
    slots = {}
    for slot, (name, color, roughness) in BRIDGE_SLOTS.items():
        made = instance(BRIDGE_DIR, name, master)
        editing.set_material_instance_vector_parameter_value(made, "Color", unreal.LinearColor(*color, 1.0))
        editing.set_material_instance_scalar_parameter_value(made, "Roughness", roughness)
        editing.update_material_instance(made)
        slots[slot] = made
    return {name: import_bridge_mesh(os.path.join(BRIDGE_SOURCE_DIR, records[name]["fbx"]), name, records[name], slots)
            for name in BRIDGE_MESHES}


def build_bridge_master():
    path = f"{BRIDGE_DIR}/{BRIDGE_MASTER}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
    else:
        material = asset_tools.create_asset(BRIDGE_MASTER, BRIDGE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    editing.delete_all_material_expressions(material)
    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(*BRIDGE_SLOTS["Concrete"][1], 1.0))
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 300)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", BRIDGE_SLOTS["Concrete"][2])
    # Gradient noise over world space, 0 to 1 and clamped (its own output range is not a bound), shading the colour from
    # BRIDGE_VARIATION's low to its high: weathering a few metres across.
    noise = editing.create_material_expression(material, unreal.MaterialExpressionNoise, -1000, 150)
    noise.set_editor_property("scale", 1.0 / BRIDGE_VARIATION_SCALE)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)
    noise.set_editor_property("turbulence", False)
    noise.set_editor_property("levels", 3)
    clamped = editing.create_material_expression(material, unreal.MaterialExpressionSaturate, -850, 150)
    editing.connect_material_expressions(noise, "", clamped, "")
    low = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -850, 250)
    low.set_editor_property("r", BRIDGE_VARIATION[0])
    high = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -850, 330)
    high.set_editor_property("r", BRIDGE_VARIATION[1])
    variation = editing.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -600, 150)
    editing.connect_material_expressions(low, "", variation, "A")
    editing.connect_material_expressions(high, "", variation, "B")
    editing.connect_material_expressions(clamped, "", variation, "Alpha")
    shade = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -350, 0)
    editing.connect_material_expressions(color, "", shade, "A")
    editing.connect_material_expressions(variation, "", shade, "B")
    editing.connect_material_property(shade, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    material.set_editor_property("phys_material", unreal.load_asset(BRIDGE_PHYSICAL_MATERIAL))
    # Its result is not whether the flag is set (building_art.allow_nanite); ask afterwards.
    editing.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE)
    if not editing.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE):
        raise RuntimeError(f"{BRIDGE_MASTER}: could not set the Nanite usage")
    editing.recompile_material(material)
    return material


def import_bridge_mesh(source, name, record, slots):
    """One bridge part's FBX as a Nanite static mesh with its UCX_ boxes as its collision, the slots bound by name."""
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    common = pipeline.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    meshes = pipeline.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    meshes.set_editor_property("import_skeletal_meshes", False)
    meshes.set_editor_property("collision", True)
    meshes.set_editor_property("import_collision_according_to_mesh_name", True)
    meshes.set_editor_property("build_nanite", True)
    materials = pipeline.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    parameters = unreal.ImportAssetParameters()
    parameters.is_automated = True
    parameters.replace_existing = True
    parameters.destination_name = name
    parameters.override_pipelines.append(unreal.SoftObjectPath(pipeline.get_path_name()))
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    manager.import_asset(BRIDGE_DIR, unreal.InterchangeManager.create_source_data(source), parameters)
    mesh = unreal.load_asset(f"{BRIDGE_DIR}/{name}")
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Could not import {source} as a static mesh")

    box = mesh.get_bounding_box()
    size, wanted = box.max.x - box.min.x, record["bounds"][1][0] - record["bounds"][0][0]
    if abs(size - wanted) > 2.0:
        raise RuntimeError(f"{source} imported {size:.1f} cm long instead of {wanted:.1f}: check the FBX units")
    if mesh.get_num_triangles(0) < record["triangles"]:
        raise RuntimeError(f"{source} imported {mesh.get_num_triangles(0)} triangles instead of {record['triangles']}")
    # Each UCX_ box comes in as a convex hull.
    shapes = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    collision = len(shapes.get_editor_property("convex_elems")) + len(shapes.get_editor_property("box_elems"))
    if collision != record["collision_boxes"]:
        raise RuntimeError(f"{source} imported {collision} collision shapes, not its {record['collision_boxes']} UCX_ boxes")
    for index, slot in enumerate(mesh.static_materials):
        slot_name = str(slot.material_slot_name)
        if slot_name not in slots:
            raise RuntimeError(f"{source}: no material for slot {slot_name}")
        mesh.set_material(index, slots[slot_name])
    nanite = mesh.get_editor_property("nanite_settings")
    if not nanite.enabled:
        nanite.enabled = True
        mesh.set_editor_property("nanite_settings", nanite)
    # Traces and the navmesh use the boxes, never the render triangles.
    mesh.get_editor_property("body_setup").set_editor_property("collision_trace_flag",
                                                               unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    unreal.log(f"rivercrossing_greybox: {name} {mesh.get_num_triangles(0)} triangles, {collision} collision boxes, Nanite on")
    return mesh


def spawn_bridges(meshes):
    """Each bridge's spans, piers and abutments along x across the river, labelled Bridge_<North|South>_<part>, every part
    world-static and blocking all, so ground traces (RTSGround) and the navmesh find the deck. The piers' tops and the
    abutments' seats carry the girders' feet, deck_depth_cm under the road."""
    half = BRIDGE_LENGTH / 2.0
    deck_depth = read_bridge_records()["deck_depth_cm"] / 100.0
    for name, bridge_y in (("North", BRIDGE_Y), ("South", -BRIDGE_Y)):
        parts = [(f"Span{span + 1}", "SM_Bridge_Span", -half + BRIDGE_SPAN * (span + 0.5), BRIDGE_DECK_TOP, 0.0)
                 for span in range(BRIDGE_SPANS)]
        parts += [(f"Pier{pier}", "SM_Bridge_Pier", -half + BRIDGE_SPAN * pier, BRIDGE_DECK_TOP - deck_depth, 0.0)
                  for pier in range(1, BRIDGE_SPANS)]
        parts += [("AbutmentW", "SM_Bridge_Abutment", -half, BRIDGE_DECK_TOP, 180.0),
                  ("AbutmentE", "SM_Bridge_Abutment", half, BRIDGE_DECK_TOP, 0.0)]
        for part, mesh, x, z, yaw in parts:
            actor = spawn(unreal.StaticMeshActor, cm((x, bridge_y), z), yaw, f"Bridge_{name}_{part}")
            component = actor.static_mesh_component
            component.set_static_mesh(meshes[mesh])
            component.set_collision_profile_name("BlockAll")


def spawn_village():
    for bank, flip in (("W", False), ("E", True)):
        for index, (x, y, length, width, house_height) in enumerate(WEST_VILLAGE):
            point = rotate((x, y)) if flip else (x, y)
            base = min(height(point[0] + dx, point[1] + dy) for dx in (-length / 2.0, length / 2.0)
                       for dy in (-width / 2.0, width / 2.0))
            # Sunk a metre into the ground; a null nav area over it so Recast leaves no island on its roof.
            center = (point[0], point[1], base - 1.0 + (house_height + 1.0) / 2.0)
            size = (length, width, house_height + 1.0)
            label = f"Block_Village{bank}{index + 1:02d}"
            spawn_cube(label, center, size)
            spawn_nav_null(f"{label}_NavNull", center, (size[0], size[1], size[2] + 1.0))


def spawn_side(spot, base, yaw):
    start = spawn(unreal.RTSStartSpot, cm(base, height(*base) + 1.0), yaw, f"StartSpot_{spot + 1}")
    start.set_editor_property("spot_index", spot)
    forward = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    behind = (base[0] - BUILDERS_BEHIND * forward[0], base[1] - BUILDERS_BEHIND * forward[1])
    spawner = spawn(unreal.RTSUnitSpawner, cm(behind, height(*behind)), yaw, f"Spawn_Team{spot}_Builders")
    spawner.set_editor_property("unit_data", unreal.load_asset(BUILDER))
    spawner.set_editor_property("count", BUILDERS)
    spawner.set_editor_property("team_id", spot)
    crate = unreal.load_asset(CRATE_MESH)
    for name, west, amount in WEST_SOURCES:
        point = west if spot == 0 else rotate(west)
        source = spawn(unreal.RTSSupplySource, cm(point, height(*point)), label=f"SupplySource_{name}_{spot + 1}")
        source.set_editor_property("amount", amount)
        source.set_editor_property("footprint", unreal.IntPoint(*SUPPLY_FOOTPRINT))
        source.set_editor_property("height", SUPPLY_HEIGHT)
        source.set_editor_property("mesh", crate)


def spawn_lighting():
    """The project's base lighting (setup_testmap.py spawn_lighting) with the afternoon sun of 03 section 10."""
    sun = spawn(unreal.DirectionalLight, cm((0.0, 0.0), 20.0), label="Sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-45.0, yaw=35.0), False)
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("atmosphere_sun_light", True)
    sky = spawn(unreal.SkyLight, cm((0.0, 0.0), 10.0), label="SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, cm((0.0, 0.0)), label="SkyAtmosphere")


def build_greybox(world, heightmap_file, river_material, bridge_meshes):
    """Fills world, a new empty level, with the greybox: One File Per Actor, the landscape, the river in river_material
    and its Water_ blockers, the bridges of bridge_meshes (build_bridge_assets), the village, both sides' start spots,
    builders and supply sources, the lighting and the navmesh bounds. Does not save."""
    check_layout()
    if not unreal.RTSEditorScripting.use_external_actors(world):
        raise RuntimeError(f"Could not turn on One File Per Actor in {MAP_PATH}")
    write_heightmap(heightmap_file)
    corner = unreal.Vector(-LANDSCAPE_HALF * 100.0, -LANDSCAPE_HALF * 100.0, 0.0)
    landscape = unreal.RTSEditorScripting.create_landscape(world, heightmap_file, COMPONENTS, QUADS_PER_SECTION, corner,
                                                           unreal.Vector(QUAD * 100.0, QUAD * 100.0, 100.0),
                                                           unreal.load_asset(GROUND_MATERIAL))
    if not landscape:
        raise RuntimeError(f"Could not create the landscape of {MAP_PATH}")
    apply_landscape_physics(world)

    create_river(world, river_material)
    spawn_water_blockers()
    spawn_bridges(bridge_meshes)
    spawn_village()
    yaw = math.degrees(math.atan2(-WEST_BASE[1], -WEST_BASE[0]))
    spawn_side(0, WEST_BASE, yaw)
    spawn_side(1, rotate(WEST_BASE), yaw + 180.0)
    spawn_lighting()

    # The playable area: units path, the camera pans and the preview shows exactly the 2,000 m square.
    nav = spawn(unreal.NavMeshBoundsVolume, cm((0.0, 0.0), 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(PLAYABLE_HALF, PLAYABLE_HALF, 100.0))
    world.get_world_settings().set_editor_property("default_game_mode", unreal.RTSSkirmishGameMode.static_class())
