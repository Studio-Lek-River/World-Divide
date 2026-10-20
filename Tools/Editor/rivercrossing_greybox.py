"""The greybox of WD_RiverCrossing_01 (#154): the layout of docs/world/maps/WD_RiverCrossing_01/02_MAP_DESIGN.md as a
landscape and engine cubes. setup_rivercrossing.py builds it only when the level does not exist yet: from then on the
level is authored by hand (03_TECHNICAL_PLAN.md section 2) and this file is never run on it again.

Coordinates are in metres with the origin at the map centre, +X east and +Y north. Every height and every actor is
point-symmetric about the centre (180 degrees): what stands at (x, y) for the west side stands at (-x, -y) for the east,
so routes, cover and supply are equal for both sides by construction.
"""

import math
import os
import struct

import unreal

MAP_PATH = "/Game/Maps/WD_RiverCrossing_01"
CUBE = "/Engine/BasicShapes/Cube.Cube"
GROUND_MATERIAL = "/Game/Materials/M_Ground"
WATER_MATERIAL = "/Game/Materials/M_Water"
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
# (y = +-300) and maps onto itself under the rotation; 45 m wide at the bridges, 70 m at the bends, its bed 3.5 m down.
RIVER_SWING = 60.0
RIVER_HALF_PERIOD = 300.0
RIVER_NARROW = 45.0
RIVER_WIDE = 70.0
RIVER_BED = -3.5
# The bank drops from the field to the bed over this many metres, far steeper than the nav agents' 44 degrees.
RIVER_BANK = 2.0
RIVER_FLAT = 20.0
RIVER_FLAT_FALLOFF = 60.0
# The visible water, a plane with no collision that shows only in the trench.
WATER_LEVEL = -3.0
# Water_ nav blockers: NavArea_Null boxes along the river, this long each, wider than the river by the margin, from the
# bed to below the bridge decks.
WATER_SEGMENT = 40.0
WATER_MARGIN = 4.0
WATER_BOTTOM = -8.0
WATER_TOP = -1.0

# Bridges (02 "Bridges"): the north one at (0, 300), the south one mirrored, along x across the river. A 14 m deck with
# 0.5 m parapets leaves a 13 m carriageway; its top sits a step above the flattened approaches.
BRIDGE_Y = 300.0
BRIDGE_LENGTH = 90.0
BRIDGE_WIDTH = 14.0
BRIDGE_DECK_TOP = 0.2
BRIDGE_DECK_THICKNESS = 1.0
PARAPET = (0.5, 1.0)
BRIDGE_FLAT = (150.0, 30.0)
BRIDGE_FLAT_FALLOFF = 60.0

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


def height(x, y):
    """The greybox ground height at (x, y), in metres. Equal at (x, y) and (-x, -y)."""
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
    if distance <= half_width:
        return RIVER_BED
    if distance <= half_width + RIVER_BANK:
        return RIVER_BED + (ground - RIVER_BED) * (distance - half_width) / RIVER_BANK
    return ground


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


def spawn_river():
    """The Water_ nav blockers along the whole river, edge to edge past the playable area, and the water plane."""
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
    spawn_cube("WaterSurface", (0.0, 0.0, WATER_LEVEL - 0.05), (LANDSCAPE_HALF * 2.0, LANDSCAPE_HALF * 2.0, 0.1),
               material=WATER_MATERIAL, collides=False)


def spawn_bridges():
    for name, bridge_y in (("North", BRIDGE_Y), ("South", -BRIDGE_Y)):
        deck_z = BRIDGE_DECK_TOP - BRIDGE_DECK_THICKNESS / 2.0
        spawn_cube(f"Bridge_{name}_Deck", (0.0, bridge_y, deck_z), (BRIDGE_LENGTH, BRIDGE_WIDTH, BRIDGE_DECK_THICKNESS))
        for side in (-1.0, 1.0):
            parapet_y = bridge_y + side * (BRIDGE_WIDTH - PARAPET[0]) / 2.0
            spawn_cube(f"Bridge_{name}_Parapet{'N' if side > 0 else 'S'}",
                       (0.0, parapet_y, BRIDGE_DECK_TOP + PARAPET[1] / 2.0), (BRIDGE_LENGTH, PARAPET[0], PARAPET[1]))


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


def build_greybox(world, heightmap_file):
    """Fills world, a new empty level, with the greybox: One File Per Actor, the landscape, the river, the bridges, the
    village, both sides' start spots, builders and supply sources, the lighting and the navmesh bounds. Does not save."""
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

    spawn_river()
    spawn_bridges()
    spawn_village()
    yaw = math.degrees(math.atan2(-WEST_BASE[1], -WEST_BASE[0]))
    spawn_side(0, WEST_BASE, yaw)
    spawn_side(1, rotate(WEST_BASE), yaw + 180.0)
    spawn_lighting()

    # The playable area: units path, the camera pans and the preview shows exactly the 2,000 m square.
    nav = spawn(unreal.NavMeshBoundsVolume, cm((0.0, 0.0), 0.0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(PLAYABLE_HALF, PLAYABLE_HALF, 100.0))
    world.get_world_settings().set_editor_property("default_game_mode", unreal.RTSSkirmishGameMode.static_class())
