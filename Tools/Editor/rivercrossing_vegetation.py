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
PCG_DIR = "/Game/World/PCG"
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

    woods = list(WEST_WOODS) + [("Riverbank", "Riverbank", RIVERBANK_VOLUME[0], RIVERBANK_VOLUME[1], 0.0)]
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
               f"{instances} generated instances, {heroes} hero trees, {removed} stale actors removed")
    return (PCG_DIR + "/",)
