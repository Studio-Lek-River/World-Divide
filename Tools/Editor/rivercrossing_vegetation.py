"""River Crossing's trees (#158, phase 6 of docs/world/maps/WD_RiverCrossing_01/04_IMPLEMENTATION_ROADMAP.md): PCG graphs
for the corner forests, the copses, the riverbanks and the border ring, the exclusions and lane bands they read, the
volumes that run them, and the hand-placed hero trees. setup_rivercrossing.py runs it on every run, before the navmesh is
built, on the authored level as on a new greybox.

The trees are Megaplant skeletal Nanite assemblies (01_ASSET_AUDIT.md), drawn with Nanite Foliage (r.Nanite.Foliage) and
the Procedural Vegetation Editor plugin's foliage material. D4 of 05_PRODUCER_ACTIONS.md: a tree has no collision, so
vehicles drive through the woods; every tree's trunk is a hole in the infantry navmesh alone (URTSTrunkObstaclesComponent),
so infantry walks around trunks; shrubs have none. Every tree component is tagged WD_Tree for line of sight (M6) and
crushing.

The graphs (BuildVegetationGraph) take their volume and their component's seed, so another map can reuse them (#322).
Rules of 03_TECHNICAL_PLAN.md section 6, from the greybox's layout:
- nothing grows in the river, on a road or within 6 m of it, in a bridge corridor (60 m wide, with 60 m of clear road
  beyond each ramp), on a base pad, in the village streets and buildings' gardens, or within 8 m of a supply source
  (actors tagged PCG_Exclude);
- within 40 m of a road, a bridge or a village street (PCG_LaneBand) only trees no taller than LANE_HEIGHT grow, which
  check_lane_entries holds against the meshes' bounds;
- the playable woods are point-symmetric: every volume west of the river has its copy turned about the centre.

The ground layer (#463): hedgerows along the field edges (greybox.field_kind), 1.5-2 m and visual only, in pieces with gaps
so they never close a field; clutter (logs, stumps, branches, bark, roots, small rocks) in the woods and along the roads;
ferns, moss and weeds on the wood floor. All Poly Haven static meshes copied out of the never-cooked /Game/PolyHaven into
MESH_DIR, without collision, navigation or trunks, and the clutter and the wood floor without shadows. The field grass and
flowers are the landscape's (greybox.LANDSCAPE_GRASS).

Every actor here is tagged VEG_TAG and found again by its label, so a rerun refills the same actor files (One File Per
Actor); the graphs regenerate with the same seeds, and the log's "GeneratePCG ... hash" lines show the result is the same.
"""

import math
import zlib

import unreal

import rivercrossing_greybox as greybox

VEG_TAG = "WD_Veg"
# The PCG volumes GeneratePCG runs.
GENERATE_TAG = "WD_VegGenerate"
EXCLUDE_TAG = "PCG_Exclude"
LANE_TAG = "PCG_LaneBand"
RIVERBANK_TAG = "PCG_Riverbank"
HEDGE_TAG = "PCG_Hedgerow"
PCG_DIR = "/Game/World/PCG"
# The Poly Haven meshes the ground layer uses, copied out of greybox.POLYHAVEN_DIR with their materials and textures.
MESH_DIR = f"{PCG_DIR}/Meshes"
MEGAPLANT = "/Game/Megaplant_Library"

# 02_MAP_DESIGN.md "Readability": trees within 40 m of a lane no taller than 18 m.
LANE_BAND = 40.0
LANE_HEIGHT = 18.0
ROAD_EXCLUSION = 6.0
BRIDGE_CORRIDOR = 60.0
BRIDGE_CLEAR_ROAD = 60.0
SUPPLY_EXCLUSION = 8.0
GARDEN = 5.0
VILLAGE_STREET_WIDTH = 30.0
RIVER_EXCLUSION = 4.0
# Marker and volume heights, m: from below the river bed to above the border hills.
MARKER_HEIGHT = 200.0
VOLUME_CENTRE_Z = 30.0


def mesh(species, variant):
    return f"{MEGAPLANT}/{species}/{species}_01/{species}_01_{variant}"


ALDER = "Tree_Black_Alder"
SPRUCE = "Tree_Norway_Spruce"
HAZEL = "Tree_Common_Hazel"
SPINDLE = "Shrub_European_Spindle"
CHERRY = "Tree_Yoshino_Cherry"


def polyhaven(model, *suffixes):
    """Poly Haven mesh names of model by suffix (SM_<model>_<suffix>); without suffixes the model's one mesh."""
    return [f"SM_{model}_{suffix}" for suffix in suffixes] or [f"SM_{model}"]


def static_entries(*weighted):
    """(model, mesh names, weight, height) groups as (model, mesh name, weight, height), the weight shared by the meshes;
    each mesh scaled to height metres (None: as it is)."""
    return [(model, name, weight / len(names), tall) for model, names, weight, tall in weighted for name in names]


def entries(*weighted):
    """(species, variants, weight, trunk) groups as (mesh path, weight, trunk), the weight shared by the variants."""
    return [(mesh(species, variant), weight / len(variants), trunk) for species, variants, weight, trunk in weighted
            for variant in variants]


# Within the lane bands: the alders (12-17 m), the young spruce (8 m) and the understory, at LANE_SCALE at most.
LANE_ENTRIES = entries((ALDER, "ABCD", 4.0, True), (SPRUCE, "D", 1.0, True), (HAZEL, "ABC", 2.0, False),
                       (SPINDLE, "ABC", 1.0, False))
LANE_SCALE = (0.85, 1.0)

# Each graph: points per m² (one tree per cell of that size), its entries, its scale range and whether it has trunks for
# the infantry navmesh (the border ring lies outside the playable area and its navmesh).
GRAPHS = {
    "Forest": {"spacing": 9.0, "scale": (0.85, 1.15), "trunks": True,
               "entries": entries((SPRUCE, "ABC", 5.0, True), (ALDER, "ABCD", 3.0, True), (HAZEL, "AB", 1.0, False))},
    "Copse": {"spacing": 7.0, "scale": (0.85, 1.1), "trunks": True,
              "entries": entries((ALDER, "ABCD", 4.0, True), (HAZEL, "ABC", 2.0, False), (SPINDLE, "ABC", 1.0, False))},
    "Riverbank": {"spacing": 8.0, "scale": (0.85, 1.1), "trunks": True, "include": RIVERBANK_TAG,
                  "entries": entries((ALDER, "ABCD", 5.0, True), (HAZEL, "AB", 1.0, False), (SPINDLE, "BC", 1.0, False))},
    "BorderRing": {"spacing": 13.0, "scale": (0.9, 1.2), "trunks": False,
                   "entries": entries((SPRUCE, "ABC", 3.0, True), (ALDER, "AD", 1.0, True))},
}

# The ground layer's graphs (#463), of static meshes: the hedge shrubs of 1.0-1.4 m raised to HEDGE_HEIGHT (1.5-2 m at the
# graph's scale), the clutter and the wood floor. Their lane entries are their entries: none tops the lanes' limit. Only
# shrub_02 is a bush; shrub_03 and shrub_04 are plants of 20-40 cm, which go on the wood floor as they are.
HEDGE_HEIGHT = 1.75
HEDGE_ENTRIES = static_entries(("shrub_02", polyhaven("shrub_02", *"abcd"), 1.0, HEDGE_HEIGHT))
LOGS = (("dead_tree_trunk", polyhaven("dead_tree_trunk"), 1.0, None),
        ("dead_tree_trunk_02", polyhaven("dead_tree_trunk_02"), 1.0, None),
        ("tree_stump_01", polyhaven("tree_stump_01"), 1.5, None),
        ("tree_stump_02", polyhaven("tree_stump_02"), 1.5, None))
# The 5 m rock faces as boulders of about a metre.
ROCKS = (("rock_face_01", polyhaven("rock_face_01"), 1.0, 1.1), ("rock_face_02", polyhaven("rock_face_02"), 1.0, 0.9))
BRANCHES = ("dry_branches_medium_01", polyhaven("dry_branches_medium_01", *"abc"), 3.0, None)
CLUTTER_ENTRIES = static_entries(*LOGS, *ROCKS, BRANCHES,
                                 ("bark_debris_01", polyhaven("bark_debris_01", *"abcd"), 2.0, None),
                                 ("root_cluster_01", polyhaven("root_cluster_01"), 1.0, None))
ROADSIDE_ENTRIES = static_entries(*LOGS[2:], *ROCKS, BRANCHES,
                                  ("nettle_plant", polyhaven("nettle_plant", "medium_a", "medium_b", "tall_a"), 2.0, None),
                                  ("weed_plant_02", polyhaven("weed_plant_02", *"abcde"), 2.0, None))
FLOOR_ENTRIES = static_entries(("fern_02", polyhaven("fern_02", *"abcd"), 4.0, None),
                               ("moss_01", polyhaven("moss_01", *"abcdef"), 2.0, None),
                               ("nettle_plant", polyhaven("nettle_plant", "small_a", "small_b"), 1.0, None),
                               ("periwinkle_plant", polyhaven("periwinkle_plant", "01", "02", "03"), 1.0, None),
                               ("shrub_03", polyhaven("shrub_03", *"abcd"), 1.0, None),
                               ("shrub_04", polyhaven("shrub_04", *"abcd"), 1.0, None),
                               ("shrub_02", polyhaven("shrub_02", "a", "b"), 0.5, 0.8))
# Each: points per m² as for the trees, its entries, scale range, shadows, the distance (m) its instances are culled at
# (0 never) and the markers it fills (none: its whole volume).
GROUND_GRAPHS = {
    "Hedgerow": {"spacing": 1.1, "scale": (0.85, 1.15), "entries": HEDGE_ENTRIES, "shadow": True, "cull": 0.0,
                 "include": HEDGE_TAG},
    "Clutter": {"spacing": 14.0, "scale": (0.8, 1.2), "entries": CLUTTER_ENTRIES, "shadow": False, "cull": 250.0},
    "Roadside": {"spacing": 18.0, "scale": (0.8, 1.2), "entries": ROADSIDE_ENTRIES, "shadow": False, "cull": 250.0,
                 "include": LANE_TAG},
    "WoodFloor": {"spacing": 2.2, "scale": (0.8, 1.25), "entries": FLOOR_ENTRIES, "shadow": False, "cull": 120.0},
}
# The graphs that fill every playable wood (WEST_WOODS and their copies).
GROUND_WOODS = ("Clutter", "WoodFloor")
# The hedgerow and roadside volumes, one a bank: the west half of the playable square (and its copy).
BANK_VOLUME = ((-greybox.PLAYABLE_HALF / 2.0, 0.0), (greybox.PLAYABLE_HALF, 2.0 * greybox.PLAYABLE_HALF))

# The hedgerows on the field edges, west side, metres: a strip HEDGE_WIDTH wide on an edge, in pieces of HEDGE_PIECE[0]
# plus up to HEDGE_PIECE[1] metres with gaps of HEDGE_GAP[0] plus up to HEDGE_GAP[1] between them, of which HEDGE_KEEP's
# share stands, so no field is closed in. None within HEDGE_CLEARANCE of a wood or the base pad, in the village, within
# HEDGE_RIVER of the water's edge or within HEDGE_BORDER of the playable square's edge.
HEDGE_WIDTH = 2.4
HEDGE_PIECE = (40.0, 60.0)
HEDGE_GAP = (8.0, 10.0)
HEDGE_SHORTEST = 15.0
HEDGE_KEEP = 0.65
HEDGE_CLEARANCE = 10.0
HEDGE_RIVER = 35.0
HEDGE_BORDER = 15.0

# The playable woods, west of the river, metres: (name, graph, centre, size, yaw). Each has its copy turned about the
# centre (greybox.rotate). The corner forests stand behind the base (02 "Forests"), the copses (30-80 m) on the hill and
# beside the river, all clear of the roads, the base pad and the forward supply source.
WEST_WOODS = (
    ("Forest_SW", "Forest", (-820.0, -760.0), (360.0, 480.0), 0.0),
    ("Forest_NW", "Forest", (-850.0, 780.0), (300.0, 440.0), 0.0),
    ("Copse_Hill", "Copse", greybox.WEST_HILL, (80.0, 70.0), 20.0),
    ("Copse_RiverSouth", "Copse", (-110.0, -520.0), (60.0, 50.0), 0.0),
    ("Copse_RiverNorth", "Copse", (-100.0, 600.0), (60.0, 60.0), 0.0),
    ("Copse_Field", "Copse", (-560.0, 60.0), (60.0, 60.0), 15.0),
    ("Copse_Farm", "Copse", (-470.0, 250.0), (50.0, 50.0), -10.0),
)
# The riverbank strips along the west bank: this wide, starting this far from the water, in pieces this long (markers
# tagged RIVERBANK_TAG), all inside one volume a bank, RIVERBANK_VOLUME across: a volume per strip would give each its own
# components, and every component costs the game thread.
RIVERBANK_WIDTH = 18.0
RIVERBANK_OFFSET = 14.0
RIVERBANK_PIECE = 80.0
RIVERBANK_VOLUME = ((-60.0, 0.0), (200.0, 2.0 * greybox.PLAYABLE_HALF))
# The border ring outside the playable square, all four sides at once (it is symmetric as a whole).
BORDER = greybox.LANDSCAPE_HALF - greybox.PLAYABLE_HALF

# The hero trees, west side, metres: (label, species, variant, point, scale); each with its copy turned about the
# centre. The village gardens' cherries behind the houses, away from the street; the tall spruces only in the corner
# woods (02 "Readability").
WEST_HEROES = (
    ("Garden_A", CHERRY, "A", (-115.0, -100.0), 0.9),
    ("Garden_B", CHERRY, "B", (-115.0, 40.0), 1.0),
    ("Garden_C", CHERRY, "A", (-115.0, 115.0), 0.85),
    ("Garden_D", CHERRY, "B", (-205.0, 30.0), 1.0),
    ("Corner_A", SPRUCE, "A", (-930.0, -930.0), 1.05),
    ("Corner_B", SPRUCE, "A", (-880.0, -700.0), 1.0),
    ("Corner_C", SPRUCE, "B", (-930.0, 930.0), 1.1),
)
HERO_CLEARANCE = 3.0

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def seed(label):
    """A fixed seed per volume, from its label, so a volume regenerates the same and its copy does not repeat it."""
    return zlib.crc32(label.encode("utf-8")) & 0x7FFFFFFF


class Placer:
    """Finds the vegetation's actors by label, so a rerun moves and refills them instead of spawning new files."""

    def __init__(self):
        self.existing = {}
        for actor in actors.get_all_level_actors():
            if actor.actor_has_tag(VEG_TAG):
                self.existing[actor.get_actor_label()] = actor
        self.used = set()

    def place(self, actor_class, label, centre, yaw, tags):
        self.used.add(label)
        location = greybox.cm(centre[:2], centre[2] if len(centre) > 2 else 0.0)
        rotation = unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw)
        actor = self.existing.get(label)
        if actor and type(actor) is actor_class:
            actor.set_actor_location_and_rotation(location, rotation, False, False)
        else:
            if actor:
                actor.destroy_actor()
            actor = actors.spawn_actor_from_class(actor_class, location, rotation)
            actor.set_actor_label(label)
        actor.tags = [unreal.Name(VEG_TAG)] + [unreal.Name(tag) for tag in tags]
        return actor

    def keep(self, label):
        self.used.add(label)

    def remove_unused(self):
        stale = [label for label in self.existing if label not in self.used]
        for label in stale:
            self.existing[label].destroy_actor()
        return len(stale)


def marker(placer, label, tag, centre, size, yaw):
    """An editor-only box of size (x, y) metres around centre turned yaw, tagged tag, for the graphs to read."""
    box = placer.place(unreal.TriggerBox, label, (centre[0], centre[1], VOLUME_CENTRE_Z), yaw, [tag])
    shape = box.get_component_by_class(unreal.BoxComponent)
    shape.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    shape.set_editor_property("generate_overlap_events", False)
    shape.set_box_extent(unreal.Vector(size[0] * 50.0, size[1] * 50.0, MARKER_HEIGHT * 50.0), False)
    box.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    box.set_editor_property("is_editor_only_actor", True)
    return box


def segment_boxes(points, width):
    """(centre, size, yaw) boxes along the polyline points, width wide, each reaching half the width past its ends."""
    boxes = []
    for start, end in zip(points, points[1:]):
        length = math.dist(start, end)
        if length < 0.01:
            continue
        yaw = math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))
        boxes.append((((start[0] + end[0]) / 2.0, (start[1] + end[1]) / 2.0), (length + width, width), yaw))
    return boxes


def both_sides(boxes):
    return boxes + [(greybox.rotate(centre), size, yaw + 180.0) for centre, size, yaw in boxes]


def road_lines():
    """Every road's centre line, both sides, metres, with its width."""
    lines = []
    for name, kind, waypoints in greybox.WEST_ROADS + tuple(greybox.ramp_roads()):
        path = list(waypoints) if name.startswith("Ramp") else greybox.road_path(waypoints)
        width = greybox.ROAD_KINDS[kind][0]
        lines.append((path, width))
        lines.append(([greybox.rotate(point) for point in path], width))
    return lines


def river_boxes():
    """The river, edge to edge past the playable area, in WATER_SEGMENT pieces, RIVER_EXCLUSION wider each side."""
    boxes = []
    y = -greybox.LANDSCAPE_HALF
    while y < greybox.LANDSCAPE_HALF:
        end = min(y + greybox.WATER_SEGMENT, greybox.LANDSCAPE_HALF)
        width = max(greybox.river_width(y), greybox.river_width(end)) + 2.0 * RIVER_EXCLUSION
        boxes += segment_boxes([(greybox.river_x(y), y), (greybox.river_x(end), end)], width)
        y = end
    return boxes


def bridge_boxes(width):
    """Each bridge with its ramps and BRIDGE_CLEAR_ROAD of road beyond them, width wide."""
    reach = greybox.ROAD_RAMP_FOOT + BRIDGE_CLEAR_ROAD
    return [((0.0, y), (2.0 * reach, width), 0.0) for y in (greybox.BRIDGE_Y, -greybox.BRIDGE_Y)]


def street_boxes(width):
    """Each bank's village street, its whole span, width wide."""
    span = greybox.VILLAGE_STREET_SPAN
    west = [((greybox.VILLAGE_STREET_X, (span[0] + span[1]) / 2.0), (width, span[1] - span[0]), 0.0)]
    return both_sides(west)


def exclusion_boxes():
    """03 section 6's exclusions as (centre, size, yaw) boxes."""
    boxes = river_boxes()
    for path, width in road_lines():
        boxes += segment_boxes(path, width + 2.0 * ROAD_EXCLUSION)
    boxes += bridge_boxes(BRIDGE_CORRIDOR)
    pad = 2.0 * greybox.PAD_HALF
    boxes += both_sides([(greybox.WEST_BASE, (pad, pad), 0.0)])
    boxes += street_boxes(VILLAGE_STREET_WIDTH)
    for _, _, _, centre, yaw, box, _ in greybox.village_buildings(greybox.read_village()):
        boxes.append((((box[0] + box[1]) / 2.0, (box[2] + box[3]) / 2.0),
                      (box[1] - box[0] + 2.0 * GARDEN, box[3] - box[2] + 2.0 * GARDEN), 0.0))
    supply = max(greybox.SUPPLY_FOOTPRINT) + 2.0 * SUPPLY_EXCLUSION
    boxes += both_sides([(point, (supply, supply), 0.0) for _, point, _ in greybox.WEST_SOURCES])
    return boxes


def lane_boxes():
    """Everything within LANE_BAND of a road, a bridge or a village street."""
    boxes = []
    for path, width in road_lines():
        boxes += segment_boxes(path, width + 2.0 * LANE_BAND)
    boxes += bridge_boxes(greybox.BRIDGE_RAMP_HALF_WIDTH * 2.0 + 2.0 * LANE_BAND)
    boxes += street_boxes(greybox.VILLAGE_MAIN_STREET + 2.0 * LANE_BAND)
    return boxes


def riverbank_strips():
    """The west bank's strips beside the water, outside the bridge corridors, as (centre, size, yaw) boxes."""
    strips = []
    count = int(2.0 * greybox.PLAYABLE_HALF / RIVERBANK_PIECE)
    for index in range(count):
        y = -greybox.PLAYABLE_HALF + (index + 0.5) * RIVERBANK_PIECE
        if min(abs(y - greybox.BRIDGE_Y), abs(y + greybox.BRIDGE_Y)) < BRIDGE_CORRIDOR / 2.0 + RIVERBANK_PIECE / 2.0:
            continue
        offset = greybox.river_width(y) / 2.0 + RIVERBANK_OFFSET + RIVERBANK_WIDTH / 2.0
        slope = greybox.river_slope(y)
        norm = math.hypot(1.0, slope)
        # Off the centre line to the west, across the river's direction (slope, 1).
        centre = (greybox.river_x(y) - offset / norm, y + offset * slope / norm)
        yaw = math.degrees(math.atan2(1.0, slope))
        strips.append((centre, (RIVERBANK_PIECE, RIVERBANK_WIDTH), yaw))
    return strips


def border_woods():
    half, inner = greybox.LANDSCAPE_HALF, greybox.PLAYABLE_HALF
    middle = (half + inner) / 2.0
    return [
        ("Border_N", "BorderRing", (0.0, middle), (2.0 * half, BORDER), 0.0),
        ("Border_S", "BorderRing", (0.0, -middle), (2.0 * half, BORDER), 0.0),
        ("Border_W", "BorderRing", (-middle, 0.0), (BORDER, 2.0 * inner), 0.0),
        ("Border_E", "BorderRing", (middle, 0.0), (BORDER, 2.0 * inner), 0.0),
    ]


def in_box(point, centre, size, yaw, margin):
    """Whether point lies in the box of size (x, y) around centre turned yaw, grown by margin each side."""
    turn = math.radians(yaw)
    dx, dy = point[0] - centre[0], point[1] - centre[1]
    along = dx * math.cos(turn) + dy * math.sin(turn)
    across = -dx * math.sin(turn) + dy * math.cos(turn)
    return abs(along) <= size[0] / 2.0 + margin and abs(across) <= size[1] / 2.0 + margin


def hedge_clear(point):
    """Whether a hedge may stand at point (west side): on the west fields, clear of the woods, the base pad, the village
    and the riverbanks. Keeping the roads, streets and supply sources clear is the exclusions' job."""
    x, y = point
    edge = greybox.PLAYABLE_HALF - HEDGE_BORDER
    if x > -HEDGE_BORDER or x < -edge or abs(y) > edge:
        return False
    for _, _, centre, size, yaw in WEST_WOODS:
        if in_box(point, centre, size, yaw, HEDGE_CLEARANCE):
            return False
    pad = 2.0 * greybox.PAD_HALF
    if in_box(point, greybox.WEST_BASE, (pad, pad), 0.0, HEDGE_CLEARANCE):
        return False
    if (x / greybox.VILLAGE_RADII[0]) ** 2 + (y / greybox.VILLAGE_RADII[1]) ** 2 < 1.1:
        return False
    distance, half_width = greybox.river_distance(x, y)
    return distance > half_width + HEDGE_RIVER


def hedge_pieces(start, end, salt):
    """The hedge pieces on the field edge from start to end (metres, west side) as (centre, size, yaw) boxes: pieces and gaps
    of lengths hashed from salt, HEDGE_KEEP of them kept, each only where hedge_clear holds along its whole length."""
    length = math.dist(start, end)
    direction = ((end[0] - start[0]) / length, (end[1] - start[1]) / length)
    yaw = math.degrees(math.atan2(direction[1], direction[0]))
    pieces = []
    index = 0
    at = HEDGE_GAP[0] * greybox.cell_hash(salt[0], salt[1], 11)
    while at < length:
        key = (salt[0] * 64 + index, salt[1])
        stop = min(at + HEDGE_PIECE[0] + HEDGE_PIECE[1] * greybox.cell_hash(*key, 12), length)
        if stop - at >= HEDGE_SHORTEST and greybox.cell_hash(*key, 13) < HEDGE_KEEP:
            points = [(start[0] + direction[0] * (at + (stop - at) * step / 4.0),
                       start[1] + direction[1] * (at + (stop - at) * step / 4.0)) for step in range(5)]
            if all(hedge_clear(point) for point in points):
                pieces.append((points[2], (stop - at, HEDGE_WIDTH), yaw))
        at = stop + HEDGE_GAP[0] + HEDGE_GAP[1] * greybox.cell_hash(*key, 14)
        index += 1
    return pieces


def hedgerows():
    """The west side's hedge pieces on greybox.field_kind's patchwork: along every row's edge and between the fields of each
    row, worked out in the patchwork's turned frame (s along the rows, t across them)."""
    yaw = math.radians(greybox.FIELD_YAW)

    def world(s, t):
        return (s * math.cos(yaw) - t * math.sin(yaw), s * math.sin(yaw) + t * math.cos(yaw))

    depth = greybox.FIELD_SIZE[1]
    reach = greybox.PLAYABLE_HALF * math.sqrt(2.0)
    pieces = []
    for row in range(math.floor(-reach / depth), math.ceil(reach / depth) + 1):
        # The edge between this row and the one before it.
        pieces += hedge_pieces(world(-reach, row * depth), world(reach, row * depth), (row + 512, 0))
        # The edges between this row's fields, at every whole u = s / width + shift (greybox.field_kind).
        width = greybox.FIELD_SIZE[0] * (0.6 + 0.9 * greybox.cell_hash(row, 0, 1))
        shift = greybox.cell_hash(row, 0, 2)
        for column in range(math.floor(-reach / width), math.ceil(reach / width) + 2):
            s = (column - shift) * width
            pieces += hedge_pieces(world(s, row * depth), world(s, (row + 1) * depth), (row + 512, column + 512))
    return pieces


def load_mesh(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"{path} is missing (Megaplant_Library, #153)")
    return asset


def mesh_height(asset):
    """The mesh's height in metres, from its bounds."""
    return asset.get_bounds().box_extent.z * 2.0 / 100.0


def check_lane_entries():
    """Raises when a tree the lane bands may hold could top LANE_HEIGHT at LANE_SCALE."""
    for path, _, _ in LANE_ENTRIES:
        tallest = mesh_height(load_mesh(path)) * LANE_SCALE[1]
        if tallest > LANE_HEIGHT:
            raise RuntimeError(f"{path} reaches {tallest:.1f} m in the lane bands, over {LANE_HEIGHT:.0f} m")


def vegetation_entries(weighted, trunks):
    result = []
    for path, weight, trunk in weighted:
        entry = unreal.RTSVegetationEntry()
        entry.mesh = load_mesh(path)
        entry.weight = weight
        entry.trunk = trunk and trunks
        result.append(entry)
    return result


def load_master(path):
    material = unreal.load_asset(path)
    if not material:
        raise RuntimeError(f"{path} is missing: rebuild River Crossing's greybox (ue.ps1 -Task rivercrossing -Rebuild -Render)")
    return material


def polyhaven_textures(source):
    """The textures of source, a Poly Haven material instance, by parameter, copied into MESH_DIR."""
    textures = {}
    for parameter in ("BaseColor", "Normal", "ARM", "Alpha"):
        found = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(source, parameter)
        if found and not found.get_path_name().startswith(MESH_DIR):
            found = greybox.copy_asset(found.get_path_name().split(".")[0], f"{MESH_DIR}/{found.get_name()}")
        textures[parameter] = found
    return textures


def copy_meshes():
    """The ground layer's Poly Haven meshes copied into MESH_DIR, by name, with each slot's instance remade there: the plants
    (an alpha map under /Game/PolyHaven's foliage master) under the landscape grass's two-sided foliage master, the rest
    under the village props' masters, so nothing of the level points into the never-cooked folder. Does not save."""
    editing = unreal.MaterialEditingLibrary
    masters = {"foliage": load_master(f"{greybox.LANDSCAPE_GRASS_DIR}/{greybox.GRASS_MASTER}"),
               True: load_master(f"{greybox.PROPS_DIR}/{greybox.PROP_MASTERS[True]}"),
               False: load_master(f"{greybox.PROPS_DIR}/{greybox.PROP_MASTERS[False]}")}
    wanted = {name: model for graph in GROUND_GRAPHS.values() for model, name, _, _ in graph["entries"]}
    made, meshes = {}, {}
    for name, model in sorted(wanted.items()):
        source_path = f"{greybox.POLYHAVEN_DIR}/{model}/{name}"
        mesh = greybox.copy_asset(source_path, f"{MESH_DIR}/{name}")
        meshes[name] = mesh
        # A checkout without the pack keeps the copies as they are.
        if not unreal.EditorAssetLibrary.does_asset_exist(source_path):
            continue
        for index, slot in enumerate(unreal.load_asset(source_path).static_materials):
            interface = slot.material_interface
            if not interface:
                continue
            if interface.get_name() not in made:
                textures = polyhaven_textures(interface)
                parent = interface.get_editor_property("parent")
                masked = textures["Alpha"] is not None
                foliage = masked and parent is not None and "Foliage" in parent.get_name()
                material = greybox.instance(MESH_DIR, interface.get_name(), masters["foliage" if foliage else masked])
                for parameter, value in textures.items():
                    if value:
                        editing.set_material_instance_texture_parameter_value(material, parameter, value)
                editing.update_material_instance(material)
                made[interface.get_name()] = material
            mesh.set_material(index, made[interface.get_name()])
    return meshes


def static_mesh_height(mesh):
    """The mesh's height in metres, from its bounding box."""
    box = mesh.get_bounding_box()
    return max(box.max.z - box.min.z, 1.0) / 100.0


def ground_entries(weighted, meshes):
    result = []
    for _, name, weight, tall in weighted:
        entry = unreal.RTSVegetationEntry()
        entry.static_mesh = meshes[name]
        entry.weight = weight
        entry.trunk = False
        entry.scale_factor = tall / static_mesh_height(meshes[name]) if tall else 1.0
        result.append(entry)
    return result


def build_ground_graphs(meshes):
    """The ground layer's PCG graphs in PCG_DIR, rebuilt in place. Returns them by name."""
    graphs = {}
    for name, graph in GROUND_GRAPHS.items():
        spec = unreal.RTSVegetationGraphSpec()
        spec.points_per_squared_meter = 1.0 / graph["spacing"] ** 2
        spec.looseness = 1.0
        spec.scale = unreal.Vector2D(*graph["scale"])
        spec.entries = ground_entries(graph["entries"], meshes)
        spec.lane_entries = ground_entries(graph["entries"], meshes)
        spec.lane_scale = unreal.Vector2D(*graph["scale"])
        spec.exclude_tag = EXCLUDE_TAG
        spec.include_tag = graph.get("include", "None")
        spec.lane_tag = LANE_TAG
        spec.trunks = False
        spec.cast_shadow = graph["shadow"]
        spec.cull_distance = int(graph["cull"] * 100.0)
        path = f"{PCG_DIR}/PCG_WD_{name}"
        built = unreal.RTSEditorScripting.build_vegetation_graph(path, spec)
        if not built:
            raise RuntimeError(f"Could not build {path} (BuildVegetationGraph in the log says why)")
        graphs[name] = built
    return graphs


def build_graphs():
    """The four PCG graphs in PCG_DIR, rebuilt in place. Returns them by name."""
    graphs = {}
    for name, graph in GRAPHS.items():
        spec = unreal.RTSVegetationGraphSpec()
        spec.points_per_squared_meter = 1.0 / graph["spacing"] ** 2
        spec.looseness = 1.0
        spec.scale = unreal.Vector2D(*graph["scale"])
        spec.entries = vegetation_entries(graph["entries"], graph["trunks"])
        spec.lane_entries = vegetation_entries(LANE_ENTRIES, graph["trunks"])
        spec.lane_scale = unreal.Vector2D(*LANE_SCALE)
        spec.exclude_tag = EXCLUDE_TAG
        spec.include_tag = graph.get("include", "None")
        spec.lane_tag = LANE_TAG
        spec.trunks = graph["trunks"]
        spec.cast_shadow = True
        spec.cull_distance = 0
        path = f"{PCG_DIR}/PCG_WD_{name}"
        built = unreal.RTSEditorScripting.build_vegetation_graph(path, spec)
        if not built:
            raise RuntimeError(f"Could not build {path} (BuildVegetationGraph in the log says why)")
        graphs[name] = built
    return graphs


def volume(placer, label, graph, centre, size, yaw):
    """A PCG volume of size (x, y) metres around centre turned yaw running graph with its own seed, on demand."""
    pcg = placer.place(unreal.PCGVolume, label, (centre[0], centre[1], VOLUME_CENTRE_Z), yaw, [GENERATE_TAG])
    # The default volume brush is a 2 m cube.
    pcg.set_actor_scale3d(unreal.Vector(size[0] / 2.0, size[1] / 2.0, MARKER_HEIGHT / 2.0))
    # The graph reads only the bounds; a brush that blocks world-static queries lifts every unit inside it onto its top (#467).
    brush = pcg.get_component_by_class(unreal.BrushComponent)
    brush.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    brush.set_editor_property("generate_overlap_events", False)
    component = pcg.get_component_by_class(unreal.PCGComponent)
    component.set_graph(graph)
    component.set_editor_property("seed", seed(label))
    component.set_editor_property("generation_trigger", unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND)
    return pcg


def ground_z(world, point):
    """The landscape's height at point (metres), traced; the greybox's own height where the trace finds nothing."""
    start = greybox.cm(point, 500.0)
    end = greybox.cm(point, -100.0)
    hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
                                                 unreal.DrawDebugTrace.NONE, True)
    if hit:
        return hit.to_tuple()[4].z / 100.0
    return greybox.height(*point)


def check_heroes(exclusions):
    """Raises when a hero tree stands in an exclusion other than a garden, or within HERO_CLEARANCE of a building."""
    houses = [box for _, _, _, _, _, box, _ in greybox.village_buildings(greybox.read_village())]
    for label, _, _, point, _ in WEST_HEROES:
        for where in (point, greybox.rotate(point)):
            for box in houses:
                if greybox.box_gap((where[0], where[0], where[1], where[1]), box) < HERO_CLEARANCE:
                    raise RuntimeError(f"Hero tree {label} at {where} stands within {HERO_CLEARANCE} m of a building")
            distance, half_width = greybox.river_distance(*where)
            if distance < half_width + RIVER_EXCLUSION:
                raise RuntimeError(f"Hero tree {label} at {where} stands in the river")


def spawn_heroes(world, placer):
    """The hero trees, one actor per mesh with both sides' instances and their trunks."""
    groups = {}
    for _, species, variant, point, scale in WEST_HEROES:
        for where, yaw in ((point, 0.0), (greybox.rotate(point), 180.0)):
            transform = unreal.Transform(greybox.cm(where, ground_z(world, where)),
                                         unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw + 37.0 * len(groups)),
                                         unreal.Vector(scale, scale, scale))
            groups.setdefault(mesh(species, variant), []).append(transform)
    for path, transforms in groups.items():
        label = f"HeroTrees_{path.rsplit('/', 1)[1]}"
        placer.keep(label)
        actor = unreal.RTSEditorScripting.spawn_tree_instances(world, label, load_mesh(path), transforms, 0, True)
        if not actor:
            raise RuntimeError(f"Could not spawn {label} (SpawnTreeInstances in the log says why)")
        actor.tags = [unreal.Name(VEG_TAG)]
    return sum(len(transforms) for transforms in groups.values())


def build(world):
    """Builds and generates River Crossing's trees in world. Returns the content directories it wrote, for saving."""
    check_lane_entries()
    graphs = build_graphs()

    placer = Placer()
    exclusions = exclusion_boxes()
    for index, (centre, size, yaw) in enumerate(exclusions):
        marker(placer, f"PCG_Exclude_{index:03d}", EXCLUDE_TAG, centre, size, yaw)
    lanes = lane_boxes()
    for index, (centre, size, yaw) in enumerate(lanes):
        marker(placer, f"PCG_LaneBand_{index:03d}", LANE_TAG, centre, size, yaw)

    strips = both_sides(riverbank_strips())
    for index, (centre, size, yaw) in enumerate(strips):
        marker(placer, f"PCG_Riverbank_{index:03d}", RIVERBANK_TAG, centre, size, yaw)

    hedges = both_sides(hedgerows())
    for index, (centre, size, yaw) in enumerate(hedges):
        marker(placer, f"PCG_Hedgerow_{index:03d}", HEDGE_TAG, centre, size, yaw)
    graphs.update(build_ground_graphs(copy_meshes()))

    woods = list(WEST_WOODS) + [("Riverbank", "Riverbank", RIVERBANK_VOLUME[0], RIVERBANK_VOLUME[1], 0.0)]
    woods += [(f"{graph}_{name}", graph, centre, size, yaw) for graph in GROUND_WOODS
              for name, _, centre, size, yaw in WEST_WOODS]
    woods += [(graph, graph, BANK_VOLUME[0], BANK_VOLUME[1], 0.0) for graph in ("Hedgerow", "Roadside")]
    volumes = 0
    for name, graph, centre, size, yaw in woods:
        volume(placer, f"Veg_{name}_W", graphs[graph], centre, size, yaw)
        volume(placer, f"Veg_{name}_E", graphs[graph], greybox.rotate(centre), size, yaw + 180.0)
        volumes += 2
    for name, graph, centre, size, yaw in border_woods():
        volume(placer, f"Veg_{name}", graphs[graph], centre, size, yaw)
        volumes += 1

    check_heroes(exclusions)
    heroes = spawn_heroes(world, placer)
    removed = placer.remove_unused()

    instances = unreal.RTSEditorScripting.generate_pcg(world, GENERATE_TAG)
    if instances < 0:
        raise RuntimeError("A vegetation volume did not generate (GeneratePCG in the log says why)")
    unreal.log(f"rivercrossing_vegetation: {volumes} volumes, {len(exclusions)} exclusions, {len(lanes)} lane bands, "
               f"{len(hedges)} hedge pieces ({sum(size[0] for _, size, _ in hedges):.0f} m), "
               f"{instances} generated instances, {heroes} hero trees, {removed} stale actors removed")
    return (PCG_DIR + "/",)
