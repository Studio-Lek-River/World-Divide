"""Builds the USA Headquarters (original Lek River Studio work, after docs/art/refs/USA_HQ_Default_Approved.png and the
spec in docs/art/usa_hq/USA_HQ_SPEC.md) from code, one object per module of the spec's module map (M1 to M16):

  blender -b --python Tools/Art/usa_hq_blender.py -- [--level N] [--verify] [--no-render] [--samples N]

--level picks how far the build goes: 1 is the block-out (#186), the default is the highest level, each one adding real
construction detail on top of the one before without moving a module. --verify builds the model, checks it against the
spec and exits 1 on any failure (footprint, height, garage door and bay, clearance volume, module overlap); it writes
nothing. Without --verify the script builds, verifies, writes the .blend under source/, the FBX under fbx/ and the
renders under renders/ (blockout_*.png for level 1, round<N>_*.png above), all under Art/USA/buildings/usa_hq.

Every dimension is in metres in the block below, in Unreal's frame from the spec (section 2): +X is the garage face, +Y
is to the right when facing +X, Z up, the origin at the footprint centre on the ground. Blender's frame is right-handed,
so the geometry is built in Unreal's frame and mirrored in Y once per object (the FBX import mirrors it back, as for the
M20 Guardian): every number below reads as the spec reads it.

Movable parts (garage door, radar array, wing dish) are separate objects with their origin on the spec's pivot and an
empty marking it. The builder's clearance volume and the exit lane are non-rendering objects in their own collection,
used by --verify only.
"""

import math
import os
import re
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m20_guardian_render as presentation  # noqa: E402

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "USA", "buildings", "usa_hq")
BLEND = os.path.join(OUT_DIR, "source", "USA_HQ_Blockout.blend")
FBX_DIR = os.path.join(OUT_DIR, "fbx")
FBX = os.path.join(FBX_DIR, "SM_USA_HQ_Blockout.fbx")
RENDER_DIR = os.path.join(OUT_DIR, "renders")
CONCEPT = os.path.join(PROJECT_DIR, "docs", "art", "refs", "USA_HQ_Default_Approved.png")
GAME_INI = os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini")

# ---- dimensions (metres, Unreal frame) ----
# Spec section 3: footprint, overall height. The paved slab edge is the footprint edge.
FOOTPRINT = 28.0
HALF = FOOTPRINT / 2.0
APRON_TOP = 0.05
TOTAL_HEIGHT = 24.0  # to the core mast tip, whole mesh
HEIGHT_TOLERANCE = 0.01
FOOTPRINT_TOLERANCE = 0.01

# M1 command core: an octagon, flats on the axes and the diagonals.
CORE_CENTRE = (-1.5, 1.5)
CORE_APOTHEM = 5.0  # 10 m across the flats
CORE_ROOF = 14.0
DRUM_APOTHEM = 3.25
DRUM_ROOF = 16.5
CORE_MAST_TIP = TOTAL_HEIGHT

# M2 administration / communications wing, an L: rear block (dish tower on it), link, front block. Boxes are
# (x0, x1, y0, y1).
WING_REAR = (-11.5, -5.5, 5.0, 12.5)
WING_LINK = (-5.5, 0.5, 8.5, 12.5)
WING_FRONT = (0.5, 7.0, 8.0, 12.5)
WING_ROOF = 9.0
WING_LINK_ROOF = 7.0
DISH_TOWER = (-8.5, -5.5, 7.0, 11.0)
DISH_TOWER_ROOF = 11.0
DISH_PIVOT = (-7.0, 9.0, DISH_TOWER_ROOF)
DISH_RADIUS = 1.25
WING_MAST_TIP = 17.0

# M3 garage (spec section 5): door 7.0 x 4.5 m clear in the plane X = +12.5, bay 8.5 m deep, 5.0 m clear inside.
GARAGE = (3.6, 12.5, -11.0, -1.0)
GARAGE_ROOF = 7.5
DOOR_PLANE = 12.5
DOOR_CENTRE_Y = -6.0  # the lateral exit offset for #192, -600 cm
DOOR_WIDTH = 7.0
DOOR_HEIGHT = 4.5
BAY_DEPTH = 8.5
BAY_HEIGHT = 5.0
# Clear heights count from the paved slab, so the opening's top and the bay ceiling sit APRON_TOP higher.
DOOR_TOP = APRON_TOP + DOOR_HEIGHT
BAY_CEILING = APRON_TOP + BAY_HEIGHT
LINTEL_DEPTH = 0.4
DOOR_PIVOT = (DOOR_PLANE, DOOR_CENTRE_Y, 4.7)
DOOR_LEAF = (DOOR_PLANE - LINTEL_DEPTH + 0.15, DOOR_PLANE - LINTEL_DEPTH + 0.25)  # x of the closed leaf, behind the lintel face
SPAWN = (8.25, DOOR_CENTRE_Y, 0.0)

# The design vehicle (standard section 3) and the margins of its clearance volume along the exit path.
BUILDER = (6.5, 3.0, 3.2)  # length, width, height
CLEAR_SIDE = 1.0  # each side: 5.0 m wide, 1.0 m from each door jamb
CLEAR_TOP = 0.5
CLEAR_REAR = 0.5
CLEAR_FLOOR = 0.05  # above the apron
LANE_WIDTH = 7.0  # spec section 5: nothing stands in it from the bay's back wall to the footprint edge

# M4 courtyard, raised with steps on its front edge; M5 flag on its plinth.
COURTYARD_X = (3.5, 10.0)
COURTYARD_Y = (1.0, 7.5)
COURTYARD_TOP = 0.6
STEPS = (3, 0.4)  # risers, tread
STEPS_Y = (3.5, 6.5)
FLAG_COURTYARD = (6.5, 4.5)
FLAG_COURTYARD_TIP = 9.0
PLINTH = (2.4, 1.6, 0.8)  # x, y, height over the courtyard

# M6 flag on the core's rear annex; M10 connectors.
FLAG_CORE = (-5.0, -3.5)
FLAG_CORE_TIP = 16.0
ANNEX_DEPTH = 2.0  # rear annex off the core's (-X, -Y) face
CONNECTOR_ROOF = 6.0
CONNECTOR_WING = (-2.5, -0.5)  # x span, from the core's +Y flat to the wing link
CONNECTOR_GARAGE_Y = (-3.0, -1.0)  # from the core's (+X, -Y) face to the garage's rear wall

# M7 radar tower and its array (a part), M8 satcom lattice and radome, M9 secondary mast.
RADAR_TOWER = (-3.0, 1.0, -10.0, -6.0)
RADAR_TOWER_ROOF = 11.0
RADAR_PIVOT = (-1.0, -8.0, RADAR_TOWER_ROOF)
RADAR_TOP = 13.5
SATCOM_CENTRE = (-6.5, -10.5)
SATCOM_BASE = 3.0
RADOME_RADIUS = 1.5
RADOME_TOP = 18.0
SATCOM_MAST_TIP = 20.0
MAST_CENTRE = (-2.0, -12.5)
MAST_TIP = 17.0

# M12 checkpoints, M13 fuel tanks, M14 supply stacks, M15 loader, M16 light masts; all outside the exit lane.
CHECKPOINTS = [(12.6, 13.8, -2.3, 0.4), (12.6, 13.8, 7.6, 11.2)]
CHECKPOINT_LAMP_TIP = 4.0
FUEL_TANKS = [(1.35, 0.55), (2.6, 0.55)]  # (y centre, radius); axis along X
FUEL_TANK_X = (11.6, 13.8)
FUEL_TANK_AXIS_Z = 0.95
LOADER = (7.0, 9.8, -0.85, 0.85, 2.5)
SUPPLY = [(7.6, 10.8, 8.6, 12.2, 2.4), (4.5, 11.5, -13.6, -11.4, 2.5), (-9.5, 5.5, 12.8, 13.7, 2.0),
          (-12.6, -9.0, -13.6, -11.6, 2.2)]
LIGHT_MASTS = [(-13.2, 13.2, 8.0), (13.3, 13.3, 8.0), (-13.2, -13.2, 8.0), (13.3, -13.3, 8.0)]

# Overlap check: these pairs may touch (shared faces), checked with each mesh shrunk by this much.
TOUCH_TOLERANCE = 0.005
ADJACENT = {("Core", "Courtyard"), ("Core", "Connectors"), ("Wing", "Connectors"), ("Garage", "Connectors"),
            ("Garage", "GarageDoor"), ("Wing", "WingDish"), ("RadarTower", "Radar"), ("Connectors", "FlagCore"),
            ("Courtyard", "FlagCourtyard")}
ON_APRON = "Apron"  # every module stands on the apron

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
MAX_LEVEL = 1
LEVEL = int(ARGS[ARGS.index("--level") + 1]) if "--level" in ARGS else MAX_LEVEL
SAMPLES = int(ARGS[ARGS.index("--samples") + 1]) if "--samples" in ARGS else 64
VERIFY_ONLY = "--verify" in ARGS
RENDER = "--no-render" not in ARGS

PREFIX = "USA_HQ_"

# Render materials, one per spec section 7 category (#189 replaces them): name, base colour, roughness, metallic,
# emission strength.
MATERIALS = {
    "Concrete": ((0.46, 0.45, 0.42), 0.85, 0.0, 0.0),
    "PaintedMetal": ((0.55, 0.56, 0.57), 0.55, 0.3, 0.0),
    "BareMetal": ((0.45, 0.45, 0.46), 0.4, 1.0, 0.0),
    "ArmouredSteel": ((0.12, 0.13, 0.14), 0.5, 0.8, 0.0),
    "Glass": ((0.05, 0.07, 0.09), 0.08, 0.0, 0.0),
    "Roofing": ((0.18, 0.18, 0.19), 0.9, 0.0, 0.0),
    "Rubber": ((0.04, 0.04, 0.04), 0.9, 0.0, 0.0),
    "BlueAccent": ((0.03, 0.08, 0.25), 0.5, 0.2, 0.0),
    "EaglePlate": ((0.04, 0.10, 0.30), 0.45, 0.2, 0.0),
    "Lamp": ((0.25, 0.5, 1.0), 0.3, 0.0, 6.0),
    "Olive": ((0.17, 0.19, 0.10), 0.7, 0.1, 0.0),
    "Apron": ((0.36, 0.36, 0.35), 0.9, 0.0, 0.0),
    "Hazard": ((0.75, 0.55, 0.05), 0.6, 0.1, 0.0),
}


# ---- geometry (all builders take Unreal-frame coordinates) ----

def loft(bottom, top):
    """A closed solid between two polygons with the same vertex count."""
    def build(bm):
        low = [bm.verts.new(p) for p in bottom]
        high = [bm.verts.new(p) for p in top]
        bm.faces.new(list(reversed(low)))
        bm.faces.new(high)
        count = len(low)
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((low[i], low[j], high[j], high[i]))
    return build


def plan_prism(profile_xy, z0, z1, top_xy=None):
    return loft([(x, y, z0) for x, y in profile_xy], [(x, y, z1) for x, y in (top_xy or profile_xy)])


def rect(x0, x1, y0, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def box(x0, x1, y0, y1, z0, z1):
    return plan_prism(rect(x0, x1, y0, y1), z0, z1)


def centred_box(cx, cy, sx, sy, z0, z1):
    return box(cx - sx / 2.0, cx + sx / 2.0, cy - sy / 2.0, cy + sy / 2.0, z0, z1)


def axis_frame(axis):
    a = Vector(axis).normalized()
    helper = Vector((0.0, 0.0, 1.0)) if abs(a.z) < 0.9 else Vector((1.0, 0.0, 0.0))
    u = a.cross(helper).normalized()
    return a, u, a.cross(u)


def ring(centre, axis, offset, radius, segments, phase=0.0):
    a, u, v = axis_frame(axis)
    c = Vector(centre) + a * offset
    return [c + (u * math.cos(t) + v * math.sin(t)) * radius
            for t in (phase + 2.0 * math.pi * i / segments for i in range(segments))]


def cylinder(centre, axis, radius, length, segments=12, radius_end=None):
    return loft(ring(centre, axis, -length / 2.0, radius, segments),
                ring(centre, axis, length / 2.0, radius if radius_end is None else radius_end, segments))


def post(x, y, z0, z1, radius, segments=8, radius_top=None):
    return cylinder((x, y, (z0 + z1) / 2.0), (0.0, 0.0, 1.0), radius, z1 - z0, segments, radius_top)


def bar(start, end, radius, segments=6):
    a, b = Vector(start), Vector(end)
    return cylinder((a + b) / 2.0, b - a, radius, (b - a).length, segments)


def lathe(centre, axis, profile, segments=16):
    """A capped solid of revolution: profile is a list of (offset along axis, radius)."""
    def build(bm):
        rings = [[bm.verts.new(p) for p in ring(centre, axis, offset, max(radius, 1e-4), segments)]
                 for offset, radius in profile]
        for r0, r1 in zip(rings, rings[1:]):
            for i in range(segments):
                j = (i + 1) % segments
                bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])
    return build


def sphere(centre, radius, segments=16, rings=8):
    return lathe(centre, (0.0, 0.0, 1.0), [(-radius * math.cos(math.pi * k / rings), radius * math.sin(math.pi * k / rings))
                                          for k in range(rings + 1)], segments)


def octagon(cx, cy, apothem):
    """Flats on the axes and diagonals, counter-clockwise from the +X flat's lower corner."""
    r = apothem / math.cos(math.pi / 8.0)
    return [(cx + r * math.cos(math.pi / 8.0 + k * math.pi / 4.0 - math.pi / 4.0),
             cy + r * math.sin(math.pi / 8.0 + k * math.pi / 4.0 - math.pi / 4.0)) for k in range(8)]


def on_core_face(normal_degrees, t, offset=0.0):
    """A point on the core's flat facing normal_degrees (0 = +X, 90 = +Y), t from -1 to 1 along it, offset outwards."""
    a = math.radians(normal_degrees)
    n = Vector((math.cos(a), math.sin(a)))
    along = Vector((-n.y, n.x))
    half = CORE_APOTHEM * math.tan(math.pi / 8.0)
    p = Vector(CORE_CENTRE) + n * (CORE_APOTHEM + offset) + along * half * t
    return (p.x, p.y)


def lattice_tower(x, y, z0, z1, base, top, levels):
    """Four legs tapering from base to top (square half-widths), with a ring of braces at each level."""
    def legs(z):
        h = base + (top - base) * (z - z0) / (z1 - z0)
        return [(x + sx * h, y + sy * h, z) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    pieces = []
    for i in range(4):
        pieces.append(bar(legs(z0)[i], legs(z1)[i], 0.07))
    for k in range(levels + 1):
        z = z0 + (z1 - z0) * k / levels
        ring_points = legs(z)
        for i in range(4):
            pieces.append(bar(ring_points[i], ring_points[(i + 1) % 4], 0.04, 4))
        if k < levels:
            upper = legs(z0 + (z1 - z0) * (k + 1) / levels)
            for i in range(4):
                pieces.append(bar(ring_points[i], upper[(i + 1) % 4], 0.03, 4))
    return pieces


class Module:
    """One object's geometry, built in Unreal's frame; each piece carries its material."""

    def __init__(self, key, collection=None):
        self.key = key
        self.name = PREFIX + key
        self.collection = collection
        self.bm = bmesh.new()

    def add(self, material, *builds):
        index = list(MATERIALS).index(material)
        for build in builds:
            start = len(self.bm.faces)
            build(self.bm)
            self.bm.faces.ensure_lookup_table()
            for i in range(start, len(self.bm.faces)):
                self.bm.faces[i].material_index = index

    def finish(self, pivot=(0.0, 0.0, 0.0)):
        """The object, its origin at pivot (Unreal frame), mirrored into Blender's frame."""
        bm = self.bm
        bmesh.ops.translate(bm, verts=bm.verts, vec=-Vector(pivot))
        bmesh.ops.scale(bm, vec=(1.0, -1.0, 1.0), verts=bm.verts)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        used = sorted({face.material_index for face in bm.faces})
        for face in bm.faces:
            face.material_index = used.index(face.material_index)
        mesh = bpy.data.meshes.new(self.name)
        bm.to_mesh(mesh)
        bm.free()
        for index in used:
            mesh.materials.append(bpy.data.materials[list(MATERIALS)[index]])
        obj = bpy.data.objects.new(self.name, mesh)
        (self.collection or bpy.context.scene.collection).objects.link(obj)
        obj.location = blender(pivot)
        return obj


def blender(p):
    return Vector((p[0], -p[1], p[2]))


def empty(name, location, size=0.6, collection=None):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = "ARROWS"
    obj.empty_display_size = size
    obj.location = blender(location)
    (collection or bpy.context.scene.collection).objects.link(obj)
    return obj


# ---- modules ----

def build_apron():
    m = Module("Apron")
    m.add("Apron", box(-HALF, HALF, -HALF, HALF, 0.0, APRON_TOP))
    return m.finish()


def build_core():
    m = Module("Core")
    cx, cy = CORE_CENTRE
    m.add("Concrete", plan_prism(octagon(cx, cy, CORE_APOTHEM), APRON_TOP, CORE_ROOF))
    m.add("PaintedMetal", plan_prism(octagon(cx, cy, DRUM_APOTHEM), CORE_ROOF, DRUM_ROOF))
    # The antenna cluster: a lattice mast on the drum to the 24 m tip, whips round it.
    m.add("BareMetal", *lattice_tower(cx, cy, DRUM_ROOF, CORE_MAST_TIP - 1.5, 0.45, 0.2, 4))
    m.add("BareMetal", post(cx, cy, CORE_MAST_TIP - 1.5, CORE_MAST_TIP, 0.06))
    for dx, dy, tip in ((1.6, 1.6, 20.5), (-1.6, 1.6, 19.5), (1.6, -1.6, 19.0), (-1.6, -1.6, 20.0)):
        m.add("BareMetal", post(cx + dx, cy + dy, DRUM_ROOF, tip, 0.05))
    return m.finish()


def build_wing():
    m = Module("Wing")
    for block, roof in ((WING_REAR, WING_ROOF), (WING_LINK, WING_LINK_ROOF), (WING_FRONT, WING_ROOF)):
        m.add("Concrete", box(*block, APRON_TOP, roof))
    m.add("Concrete", box(*DISH_TOWER, WING_ROOF, DISH_TOWER_ROOF))
    # Panel antenna array on the rear block and its masts.
    m.add("PaintedMetal", box(-11.0, -9.0, 5.5, 6.0, WING_ROOF, WING_ROOF + 2.0))
    for x, y in ((-11.0, 12.0), (-10.0, 12.0)):
        m.add("BareMetal", post(x, y, WING_ROOF, WING_MAST_TIP, 0.06))
    return m.finish()


def garage_shell(m):
    x0, x1, y0, y1 = GARAGE
    bay_x0 = DOOR_PLANE - BAY_DEPTH
    door_y0, door_y1 = DOOR_CENTRE_Y - DOOR_WIDTH / 2.0, DOOR_CENTRE_Y + DOOR_WIDTH / 2.0
    m.add("Concrete",
          box(x0, bay_x0, y0, y1, APRON_TOP, GARAGE_ROOF),  # back wall
          box(bay_x0, x1, y0, door_y0, APRON_TOP, GARAGE_ROOF),  # -Y side and jamb
          box(bay_x0, x1, door_y1, y1, APRON_TOP, GARAGE_ROOF),  # +Y side and jamb
          box(bay_x0, x1, door_y0, door_y1, BAY_CEILING, GARAGE_ROOF),  # roof over the bay
          box(x1 - LINTEL_DEPTH, x1, door_y0, door_y1, DOOR_TOP, BAY_CEILING))  # lintel


def build_garage():
    m = Module("Garage")
    garage_shell(m)
    # Rooftop solar panels.
    x0, x1, y0, y1 = GARAGE
    m.add("Glass", box(6.0, 10.5, -9.0, -4.0, GARAGE_ROOF, GARAGE_ROOF + 0.4))
    return m.finish()


def build_garage_door():
    m = Module("GarageDoor")
    m.add("ArmouredSteel", box(*DOOR_LEAF, DOOR_CENTRE_Y - DOOR_WIDTH / 2.0 + 0.01,
                               DOOR_CENTRE_Y + DOOR_WIDTH / 2.0 - 0.01, APRON_TOP, DOOR_TOP))
    return m.finish(DOOR_PIVOT)


def build_courtyard():
    m = Module("Courtyard")
    x0, x1 = COURTYARD_X
    y0, y1 = COURTYARD_Y
    # Against the core's +X flat (x0) and its (+X, +Y) face, where the entrance is.
    corner_a, corner_b = on_core_face(45.0, -1.0), on_core_face(45.0, 1.0)
    outline = [(x0, y0), (x1, y0), (x1, y1), (corner_b[0], y1), corner_b, corner_a]
    m.add("Concrete", plan_prism(outline, APRON_TOP, COURTYARD_TOP))
    risers, tread = STEPS
    rise = (COURTYARD_TOP - APRON_TOP) / risers
    for k in range(risers):
        m.add("Concrete", box(x1, x1 + tread * (risers - k), STEPS_Y[0], STEPS_Y[1], APRON_TOP + rise * k,
                              APRON_TOP + rise * (k + 1)))
    fx, fy = FLAG_COURTYARD
    m.add("EaglePlate", centred_box(fx, fy, PLINTH[0], PLINTH[1], COURTYARD_TOP, COURTYARD_TOP + PLINTH[2]))
    for py in (y0 + 1.2, y1 - 1.2):
        m.add("Concrete", centred_box(fx, py, 2.2, 1.4, COURTYARD_TOP, COURTYARD_TOP + 0.7))
    return m.finish()


def build_flag(key, x, y, z0, tip):
    m = Module(key)
    m.add("BareMetal", post(x, y, z0, tip, 0.07, 8, 0.04))
    m.add("BlueAccent", box(x + 0.08, x + 1.98, y - 0.02, y + 0.02, tip - 1.3, tip - 0.1))
    return m.finish()


def build_connectors():
    m = Module("Connectors")
    # Core (+Y flat) to the wing link.
    x0, x1 = CONNECTOR_WING
    m.add("Glass", box(x0, x1, on_core_face(90.0, 0.0)[1], WING_LINK[2], APRON_TOP, CONNECTOR_ROOF))
    # Core ((+X, -Y) face) to the garage's rear wall.
    y0, y1 = CONNECTOR_GARAGE_Y
    normal = Vector((math.cos(math.radians(-45.0)), math.sin(math.radians(-45.0))))
    reach = CORE_APOTHEM + Vector(CORE_CENTRE).dot(normal)

    def face_x(y):  # the core's (+X, -Y) face line at this y
        return (reach - normal.y * y) / normal.x
    m.add("Glass", plan_prism([(face_x(y0), y0), (GARAGE[0], y0), (GARAGE[0], y1), (face_x(y1), y1)],
                              APRON_TOP, CONNECTOR_ROOF))
    # Rear annex off the core's (-X, -Y) face, the second flag on its roof.
    a, b = on_core_face(-135.0, 1.0), on_core_face(-135.0, -1.0)
    a2, b2 = on_core_face(-135.0, 1.0, ANNEX_DEPTH), on_core_face(-135.0, -1.0, ANNEX_DEPTH)
    m.add("Concrete", plan_prism([a, a2, b2, b], APRON_TOP, CONNECTOR_ROOF))
    return m.finish()


def build_radar_tower():
    m = Module("RadarTower")
    m.add("Concrete", box(*RADAR_TOWER, APRON_TOP, RADAR_TOWER_ROOF))
    return m.finish()


def build_radar():
    m = Module("Radar")
    x, y, z = RADAR_PIVOT
    m.add("PaintedMetal", post(x, y, z, z + 0.8, 0.45))
    m.add("PaintedMetal", box(x - 0.25, x + 0.25, y - 1.6, y + 1.6, z + 0.8, RADAR_TOP))
    return m.finish(RADAR_PIVOT)


def build_wing_dish():
    m = Module("WingDish")
    x, y, z = DISH_PIVOT
    m.add("PaintedMetal", post(x, y, z, z + 1.0, 0.3))
    centre = Vector((x, y, z + 1.0 + DISH_RADIUS))
    m.add("PaintedMetal", cylinder(centre + Vector((0.35, 0.0, 0.0)), (1.0, 0.0, 0.3), DISH_RADIUS, 0.35, 16,
                                   DISH_RADIUS * 0.4))
    return m.finish(DISH_PIVOT)


def build_satcom():
    m = Module("Satcom")
    x, y = SATCOM_CENTRE
    centre_z = RADOME_TOP - RADOME_RADIUS
    half = SATCOM_BASE / 2.0
    deck = centre_z - RADOME_RADIUS
    m.add("Concrete", box(x - half, x + half, y - half, y + half, APRON_TOP, 0.3))
    m.add("BareMetal", *lattice_tower(x, y, 0.3, deck - 0.1, half - 0.15, 0.8, 5))
    m.add("PaintedMetal", box(x - half, x + half, y - half, y + half, deck - 0.1, deck + 0.2))
    m.add("PaintedMetal", sphere((x, y, centre_z), RADOME_RADIUS, 20, 10))
    m.add("BareMetal", post(x + half - 0.1, y + half - 0.1, deck + 0.2, SATCOM_MAST_TIP, 0.05))
    m.add("BareMetal", post(x - half + 0.1, y + half - 0.1, deck + 0.2, SATCOM_MAST_TIP - 1.0, 0.05))
    return m.finish()


def build_mast():
    m = Module("Mast")
    x, y = MAST_CENTRE
    m.add("Concrete", centred_box(x, y, 1.0, 1.0, APRON_TOP, 0.4))
    m.add("BareMetal", post(x, y, 0.4, MAST_TIP, 0.12, 8, 0.05))
    return m.finish()


def build_checkpoints():
    m = Module("Checkpoints")
    for x0, x1, y0, y1 in CHECKPOINTS:
        m.add("Concrete", box(x0, x1, y0, y1, APRON_TOP, 0.9))
        m.add("BareMetal", post((x0 + x1) / 2.0, y1 - 0.4, 0.9, CHECKPOINT_LAMP_TIP, 0.08))
    return m.finish()


def build_fuel_tanks():
    m = Module("FuelTanks")
    x0, x1 = FUEL_TANK_X
    for y, r in FUEL_TANKS:
        m.add("BareMetal", cylinder(((x0 + x1) / 2.0, y, FUEL_TANK_AXIS_Z), (1.0, 0.0, 0.0), r, x1 - x0, 16))
        for x in (x0 + 0.4, x1 - 0.4):
            m.add("Concrete", centred_box(x, y, 0.3, 2.0 * r, APRON_TOP, FUEL_TANK_AXIS_Z - r + 0.05))
    return m.finish()


def build_supply():
    m = Module("Supply")
    for x0, x1, y0, y1, h in SUPPLY:
        m.add("Olive", box(x0, x1, y0, y1, APRON_TOP, h))
    return m.finish()


def build_loader():
    m = Module("Loader")
    x0, x1, y0, y1, h = LOADER
    m.add("Olive", box(x0, x1, y0, y1, APRON_TOP + 0.4, 1.5))
    m.add("Olive", box(x0 + 0.6, x0 + 1.8, y0 + 0.2, y1 - 0.2, 1.5, h))
    for x in (x0 + 0.5, x1 - 0.5):
        m.add("Rubber", cylinder((x, 0.0, 0.45), (0.0, 1.0, 0.0), 0.4, y1 - y0 - 0.02, 12))
    return m.finish()


def build_light_masts():
    m = Module("LightMasts")
    for x, y, tip in LIGHT_MASTS:
        m.add("Concrete", centred_box(x, y, 0.8, 0.8, APRON_TOP, 0.4))
        m.add("BareMetal", post(x, y, 0.4, tip - 0.4, 0.1, 8, 0.06))
        m.add("Lamp", centred_box(x, y, 0.6, 0.6, tip - 0.4, tip))
    return m.finish()


def build_clearance(collection):
    """The builder's clearance volume along the exit path, and the full 7 m exit lane: non-rendering, for --verify."""
    length, width, height = BUILDER
    x0 = SPAWN[0] - length / 2.0 - CLEAR_REAR
    clearance = Module("Clearance", collection)
    clearance.add("Hazard", box(x0, HALF, DOOR_CENTRE_Y - width / 2.0 - CLEAR_SIDE,
                                DOOR_CENTRE_Y + width / 2.0 + CLEAR_SIDE, APRON_TOP + CLEAR_FLOOR,
                                APRON_TOP + CLEAR_FLOOR + height + CLEAR_TOP))
    lane = Module("ExitLane", collection)
    lane.add("Hazard", box(DOOR_PLANE - BAY_DEPTH, HALF, DOOR_CENTRE_Y - LANE_WIDTH / 2.0,
                           DOOR_CENTRE_Y + LANE_WIDTH / 2.0, APRON_TOP + CLEAR_FLOOR, DOOR_TOP))
    objects = [clearance.finish(), lane.finish()]
    for obj in objects:
        obj.hide_render = True
        obj.display_type = "WIRE"
    return objects


# ---- assembly ----

def make_materials():
    for name, (colour, roughness, metallic, emission) in MATERIALS.items():
        material = bpy.data.materials.new(name)
        material.use_nodes = True
        shader = material.node_tree.nodes["Principled BSDF"]
        shader.inputs["Base Color"].default_value = (*colour, 1.0)
        shader.inputs["Roughness"].default_value = roughness
        shader.inputs["Metallic"].default_value = metallic
        if emission:
            shader.inputs["Emission Color"].default_value = (*colour, 1.0)
            shader.inputs["Emission Strength"].default_value = emission
        if name == "Glass":
            shader.inputs["Specular IOR Level"].default_value = 0.8


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    bpy.context.preferences.filepaths.save_version = 0  # no .blend1 next to the source
    make_materials()
    modules = [build_apron(), build_core(), build_wing(), build_garage(), build_courtyard(),
               build_flag("FlagCourtyard", *FLAG_COURTYARD, COURTYARD_TOP + PLINTH[2], FLAG_COURTYARD_TIP),
               build_flag("FlagCore", *FLAG_CORE, CONNECTOR_ROOF, FLAG_CORE_TIP), build_connectors(),
               build_radar_tower(), build_satcom(), build_mast(), build_checkpoints(), build_fuel_tanks(),
               build_supply(), build_loader(), build_light_masts()]
    parts = {"GarageDoor": build_garage_door(), "Radar": build_radar(), "WingDish": build_wing_dish()}
    helpers = bpy.data.collections.new("USA_HQ_Helpers")
    scene.collection.children.link(helpers)
    helpers.hide_render = True
    for key, pivot in (("GarageDoor", DOOR_PIVOT), ("Radar", RADAR_PIVOT), ("WingDish", DISH_PIVOT)):
        empty(f"Pivot_{key}", pivot, collection=helpers)
    for name, location in (("Spawn_Garage", SPAWN), ("Exit_Door", (DOOR_PLANE, DOOR_CENTRE_Y, 0.0)),
                           ("Flag_Courtyard", (*FLAG_COURTYARD, FLAG_COURTYARD_TIP)),
                           ("Flag_Core", (*FLAG_CORE, FLAG_CORE_TIP)), ("Mast_Core_Tip", (*CORE_CENTRE, CORE_MAST_TIP))):
        empty(f"Socket_{name}", location, collection=helpers)
    clearance = build_clearance(helpers)
    bpy.context.view_layer.update()
    return modules, parts, clearance


# ---- verification ----

def world_bm(obj, shrink=0.0):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    evaluated.to_mesh_clear()
    bm.transform(obj.matrix_world)
    if shrink:
        bm.normal_update()
        for vert in bm.verts:
            vert.co -= vert.normal * shrink
    return bm


def tree(obj, shrink=0.0):
    bm = world_bm(obj, shrink)
    result = BVHTree.FromBMesh(bm)
    bm.free()
    return result


def bounds(objects):
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    return (Vector([min(p[i] for p in points) for i in range(3)]), Vector([max(p[i] for p in points) for i in range(3)]))


def key_of(obj):
    return obj.name[len(PREFIX):]


def verify(modules, parts, clearance):
    """Checks the model against the spec; returns (lines, failures)."""
    lines, failures = [], []

    def check(ok, text):
        lines.append(("PASS " if ok else "FAIL ") + text)
        if not ok:
            failures.append(text)

    solids = modules + list(parts.values())
    low, high = bounds(solids)
    size = high - low
    check(abs(size.x - FOOTPRINT) <= FOOTPRINT_TOLERANCE and abs(size.y - FOOTPRINT) <= FOOTPRINT_TOLERANCE
          and abs(low.x + HALF) <= FOOTPRINT_TOLERANCE and abs(low.y + HALF) <= FOOTPRINT_TOLERANCE,
          f"footprint {size.x * 100:.1f} x {size.y * 100:.1f} cm, centred (spec 2800 x 2800)")
    check(abs(low.z) <= HEIGHT_TOLERANCE and abs(high.z - TOTAL_HEIGHT) <= HEIGHT_TOLERANCE,
          f"height {low.z * 100:.1f} .. {high.z * 100:.1f} cm (spec 0 .. 2400, to the mast tip)")
    core = bpy.data.objects[PREFIX + "Core"]
    check(abs(bounds([core])[1].z - CORE_MAST_TIP) <= HEIGHT_TOLERANCE,
          "the 24 m core mast is in the static root (Core), not a part")

    # Garage door and bay, measured on the garage's own geometry by rays from the bay.
    garage = tree(bpy.data.objects[PREFIX + "Garage"])

    def hit(origin, direction):
        location, _, _, distance = garage.ray_cast(blender(origin), blender(direction))
        return distance if location is not None else float("inf")
    probe_x = DOOR_PLANE - 0.05
    left = hit((probe_x, DOOR_CENTRE_Y, 2.0), (0.0, -1.0, 0.0))
    right = hit((probe_x, DOOR_CENTRE_Y, 2.0), (0.0, 1.0, 0.0))
    door_y0, door_y1 = DOOR_CENTRE_Y - left, DOOR_CENTRE_Y + right
    door_centre = (door_y0 + door_y1) / 2.0
    check(abs(door_centre - DOOR_CENTRE_Y) <= 0.005 and abs(door_centre + 6.0) <= 0.5,
          f"door centre at Y {door_centre * 100:.1f} cm (spec -600 +/- 50): lateral exit offset confirmed")
    check(door_y1 - door_y0 >= DOOR_WIDTH - 0.001, f"door clear width {(door_y1 - door_y0) * 100:.1f} cm (spec 700)")
    door_top = 2.0 + hit((probe_x, DOOR_CENTRE_Y, 2.0), (0.0, 0.0, 1.0)) - APRON_TOP
    check(door_top >= DOOR_HEIGHT - 0.001, f"door clear height {door_top * 100:.1f} cm over the slab (spec 450)")
    back = SPAWN[0] - hit((SPAWN[0], SPAWN[1], 1.0), (-1.0, 0.0, 0.0))
    check(abs(DOOR_PLANE - back - BAY_DEPTH) <= 0.001, f"bay depth {(DOOR_PLANE - back) * 100:.1f} cm (spec 850)")
    ceiling = 1.0 + hit((SPAWN[0], SPAWN[1], 1.0), (0.0, 0.0, 1.0)) - APRON_TOP
    check(ceiling >= BAY_HEIGHT - 0.001, f"bay clear height {ceiling * 100:.1f} cm (spec 500)")
    for side in (-1.0, 1.0):
        wall = hit((SPAWN[0], SPAWN[1], 1.0), (0.0, side, 0.0))
        check(wall >= DOOR_WIDTH / 2.0 - 0.001, f"bay wall {wall * 100:.1f} cm from the spawn on {'+' if side > 0 else '-'}Y")

    # Clearance volume: no contact at all with the garage or any other module along the exit path. The door leaf is
    # excluded: it opens (rolls into the drum over the opening) before the builder spawns (spec section 5).
    clear_obj, lane_obj = clearance
    clear_tree = tree(clear_obj)
    hits = [key_of(o) for o in solids if key_of(o) != "GarageDoor" and clear_tree.overlap(tree(o))]
    length = HALF - (SPAWN[0] - BUILDER[0] / 2.0 - CLEAR_REAR)
    check(not hits, f"builder clearance volume ({length:.2f} m long, {BUILDER[1] + 2 * CLEAR_SIDE:.1f} m wide, "
                    f"{BUILDER[2] + CLEAR_TOP:.1f} m high, spawn to footprint edge) touches nothing"
                    f"{': ' + ', '.join(hits) if hits else ''}")
    lane_tree = tree(lane_obj, TOUCH_TOLERANCE)
    hits = [key_of(o) for o in solids if key_of(o) not in ("GarageDoor", ON_APRON)
            and lane_tree.overlap(tree(o, TOUCH_TOLERANCE))]
    check(not hits, f"7 m exit lane free from the bay's back wall to the footprint edge"
                    f"{': ' + ', '.join(hits) if hits else ''}")

    # Module overlap: any intersection between two modules fails; listed neighbours may share faces.
    trees = {key_of(o): (tree(o), tree(o, TOUCH_TOLERANCE)) for o in solids}
    keys = list(trees)
    overlaps = []
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            touching = ON_APRON in (a, b) or (a, b) in ADJACENT or (b, a) in ADJACENT
            ta, tb = (trees[a][1], trees[b][1]) if touching else (trees[a][0], trees[b][0])
            if ta.overlap(tb):
                overlaps.append(f"{a}/{b}")
    check(not overlaps, f"no overlapping modules ({len(keys)} objects, {len(keys) * (len(keys) - 1) // 2} pairs)"
                        f"{': ' + ', '.join(overlaps) if overlaps else ''}")
    return lines, failures


def triangle_counts(objects):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    counts = {}
    for obj in objects:
        mesh = obj.evaluated_get(depsgraph).to_mesh()
        mesh.calc_loop_triangles()
        counts[key_of(obj)] = len(mesh.loop_triangles)
        obj.evaluated_get(depsgraph).to_mesh_clear()
    return counts


# ---- export, renders ----

def export_fbx(objects):
    os.makedirs(FBX_DIR, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(filepath=FBX, use_selection=True, object_types={"MESH"}, use_mesh_modifiers=True,
                             mesh_smooth_type="FACE", use_tspace=False, apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                             bake_space_transform=False, add_leaf_bones=False, bake_anim=False,
                             use_custom_props=False, path_mode="AUTO")


def camera_tuning():
    """The RTS camera from Config/DefaultGame.ini (URTSCameraSettings), lengths in metres."""
    values = {"FieldOfView": 60.0, "DefaultZoom": 15000.0, "MinZoom": 600.0, "MaxZoom": 45000.0,
              "TopDownPitch": -55.0, "NearPitch": -8.0, "TiltStartZoom": 6000.0}
    if os.path.exists(GAME_INI):
        text = open(GAME_INI).read()
        section = text.split("[/Script/WorldDivide.RTSCameraSettings]", 1)[-1].split("\n[", 1)[0]
        for key in values:
            match = re.search(rf"^{key}=([-0-9.]+)", section, re.M)
            if match:
                values[key] = float(match.group(1))
    for key in ("DefaultZoom", "MinZoom", "MaxZoom", "TiltStartZoom"):
        values[key] /= 100.0
    return values


def rts_pitch(tuning, arm):
    """ARTSCameraPawn::GetPitchForZoom: along the arm's logarithm, smoothstepped, degrees below the horizon."""
    near, top, low, tilt = -tuning["NearPitch"], -tuning["TopDownPitch"], tuning["MinZoom"], tuning["TiltStartZoom"]
    alpha = max(0.0, min(1.0, math.log(max(arm, low) / low) / math.log(tilt / low)))
    return near + (top - near) * alpha * alpha * (3.0 - 2.0 * alpha)


# The view's yaw (Blender, the direction the camera looks): from past the slab's (+X, +Y) corner towards the rear, square
# on to the core's entrance face as the concept draws it.
VIEW_YAW = 135.0
CONCEPT_VIEW = {"pitch": 45.0, "distance": 100.0, "fov": 30.0, "target": (0.0, 0.0, 1.5)}


def render(prefix):
    os.makedirs(RENDER_DIR, exist_ok=True)
    tuning = camera_tuning()
    camera = presentation.setup(SAMPLES)
    # Building scale: the tank's fill light pushed out ten times, the ground turned to the concept's dry soil.
    fill = bpy.data.objects["Fill"]
    fill.location = Vector(fill.location) * 10.0
    fill.data.energy *= 100.0
    fill.data.size *= 10.0
    ramp = next(n for n in bpy.data.materials["Ground"].node_tree.nodes if n.type == "VALTORGB")
    ramp.color_ramp.elements[0].color = (0.30, 0.20, 0.12, 1.0)
    ramp.color_ramp.elements[1].color = (0.42, 0.30, 0.19, 1.0)
    fov = math.radians(tuning["FieldOfView"])
    target = (0.0, 0.0, 2.0)

    def path(name):
        return os.path.join(RENDER_DIR, f"{prefix}{name}.png")
    for name, arm in (("rts_default", tuning["DefaultZoom"]), ("rts_mid", tuning["TiltStartZoom"]),
                      ("rts_close", 30.0)):
        pitch = rts_pitch(tuning, arm)
        location, aim = presentation.rts_camera(arm, pitch, VIEW_YAW, target)
        print(f"render {name}: arm {arm:.0f} m, pitch {pitch:.1f} deg")
        presentation.shoot(camera, path(name), location, aim, size=(1920, 1080), fov=fov)
    view = CONCEPT_VIEW
    location, aim = presentation.rts_camera(view["distance"], view["pitch"], VIEW_YAW, view["target"])
    presentation.shoot(camera, path("concept_view"), location, aim, size=(1402, 1122), fov=math.radians(view["fov"]))
    presentation.teardown()
    if os.path.exists(CONCEPT):
        presentation.side_by_side(path("concept_view"), CONCEPT, path("concept_compare"))


def main():
    modules, parts, clearance = build()
    lines, failures = verify(modules, parts, clearance)
    counts = triangle_counts(modules + list(parts.values()))
    print("\n".join(lines))
    root = sum(n for k, n in counts.items() if k not in parts)
    print(f"triangles: root {root:,}, parts {sum(counts[k] for k in parts):,}; " +
          ", ".join(f"{k} {n:,}" for k, n in counts.items()))
    print("verify:", f"FAILED ({len(failures)})" if failures else "all checks passed")
    if VERIFY_ONLY:
        sys.stdout.flush()
        if failures:
            sys.exit(1)
        return
    if failures:
        sys.exit(1)
    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)
    export_fbx(modules + list(parts.values()))
    if RENDER:
        render("blockout_" if LEVEL == 1 else f"round{LEVEL}_")
        bpy.ops.wm.save_as_mainfile(filepath=BLEND)


main()
