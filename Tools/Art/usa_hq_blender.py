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

# Construction detail (level 2 and up), sized after real reinforced-concrete command buildings: windows 0.25 m deep in
# a precast-clad wall, 0.9 m parapets under a metal coping, 3.5-4.5 m storeys, 2.4 m window bays, ~3 m wide panels
# with 20 mm joints.
REVEAL = 0.25  # outer face of the cladding to the glazing; the building mass stands back this far
PARAPET = 0.9  # roof deck below the coping top
PARAPET_WALL = 0.25
COPING = 0.08
ROOF_MEMBRANE = 0.03
PANEL = 3.0
PANEL_JOINT = 0.02
BAY = 2.4
WINDOW = 1.5
FRAME = 0.06  # window frame and mullion face width
FRAME_DEPTH = 0.08
GLASS = 0.03
SILL = 0.05
CORE_ROWS = [(1.5, 3.9), (5.9, 8.1), (10.3, 12.3)]  # (sill, head) per storey
DRUM_ROWS = [(14.4, 15.6)]
DRUM_PARAPET = 0.6
WING_ROWS = [(1.1, 3.2), (5.3, 7.3)]
LINK_ROWS = [(1.0, 3.2), (4.0, 5.6)]
TOWER_ROWS = [(1.2, 3.0), (5.0, 6.6)]
ANNEX_ROWS = [(1.2, 3.0)]
ANNEX_PARAPET = 0.6
ENTRANCE = (3.2, 3.9)  # core doors: width, head (threshold at the courtyard)
CANOPY = (4.4, 1.3, 4.0, 0.25)  # width, depth, underside, thickness
CORE_PLATE = (3.8, 5.0, 8.4)  # eagle plaque over the entrance: width, bottom, top
WING_PLATE = (3.0, 2.4, 5.0)  # width, bottom, top
TOWER_PLATE = (2.6, 7.4, 10.0)
DISH_TOWER_PLATE = (2.0, 9.3, 10.6)
PLATE_DEPTH = 0.06
LAMP_STRIP = 0.12
GARAGE_SKIN = 0.2  # the hall's side walls stand back behind their pilasters
PILASTER = (0.45, 2.85)  # width, spacing
CLERESTORY = (5.3, 6.3)
DOOR_POST = (0.3, 0.08)  # hazard-striped steel door frame: width, proud of the door plane
HAZARD_BAND = 0.3
CORRIDOR = (0.8, 0.6, 0.15, 1.5)  # glazed links: plinth height, roof fascia, glazing set-back, mullion spacing
MARKING = (0.4, 0.15, 0.001)  # apron edge line: inset, width, thickness
LANE_LINE = 0.15

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
MAX_LEVEL = 2
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
    "EmblemWhite": ((0.80, 0.80, 0.78), 0.5, 0.0, 0.0),
    "Marking": ((0.70, 0.70, 0.67), 0.75, 0.0, 0.0),
    "Foliage": ((0.07, 0.11, 0.04), 0.8, 0.0, 0.0),
    "Soil": ((0.11, 0.08, 0.05), 0.95, 0.0, 0.0),
}
# Weathering for the render materials from level 2: noise scale, colour variation, bump strength, grime height (m).
WEATHERED = {"Concrete": (0.9, 0.18, 0.25, 1.6), "Apron": (0.35, 0.22, 0.2, 0.0), "PaintedMetal": (2.0, 0.08, 0.05, 1.0),
             "Roofing": (1.2, 0.15, 0.2, 0.0), "Olive": (1.5, 0.15, 0.1, 0.6), "BareMetal": (3.0, 0.12, 0.05, 0.0),
             "BlueAccent": (1.5, 0.10, 0.05, 0.0), "EaglePlate": (1.5, 0.08, 0.05, 0.0)}


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


# ---- architecture (level 2 and up) ----

def signed_area(points):
    return sum(points[i][0] * points[(i + 1) % len(points)][1] - points[(i + 1) % len(points)][0] * points[i][1]
               for i in range(len(points))) / 2.0


def ccw(points):
    return list(points) if signed_area(points) > 0.0 else list(reversed(points))


def inset_outline(points, distance):
    """A closed counter-clockwise outline moved inwards by distance (mitred corners)."""
    pts = [Vector(p[:2]) for p in points]
    count = len(pts)
    out = []
    for i in range(count):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % count]
        e0, e1 = (p1 - p0).normalized(), (p2 - p1).normalized()
        n0, n1 = Vector((-e0.y, e0.x)), Vector((-e1.y, e1.x))
        bisector = (n0 + n1).normalized()
        out.append(tuple(p1 + bisector * distance / max(bisector.dot(n0), 0.3)))
    return out


def annulus(outer, inner, z0, z1):
    """A closed ring between two outlines with the same vertex count: parapets, copings, kerbs."""
    def build(bm):
        ob = [bm.verts.new((x, y, z0)) for x, y in outer]
        ot = [bm.verts.new((x, y, z1)) for x, y in outer]
        ib = [bm.verts.new((x, y, z0)) for x, y in inner]
        it = [bm.verts.new((x, y, z1)) for x, y in inner]
        count = len(outer)
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((ob[i], ob[j], ot[j], ot[i]))
            bm.faces.new((ib[j], ib[i], it[i], it[j]))
            bm.faces.new((ot[i], ot[j], it[j], it[i]))
            bm.faces.new((ob[j], ob[i], ib[i], ib[j]))
    return build


class Frame:
    """A facade's plane on one edge of a counter-clockwise outline: s along the edge from p0, d outwards (negative into
    the wall), z up."""

    def __init__(self, p0, p1):
        self.p0 = Vector(p0[:2])
        edge = Vector(p1[:2]) - self.p0
        self.length = edge.length
        self.e = edge / self.length
        self.n = Vector((self.e.y, -self.e.x))

    def point(self, s, d, z):
        p = self.p0 + self.e * s + self.n * d
        return (p.x, p.y, z)

    def box(self, s0, s1, d0, d1, z0, z1):
        corners = ((s0, d0), (s1, d0), (s1, d1), (s0, d1))
        return loft([self.point(s, d, z0) for s, d in corners], [self.point(s, d, z1) for s, d in corners])

    def plate(self, outline_sz, d0, d1):
        """A flat shape drawn on the facade as (s, z) points, d0..d1 thick."""
        return loft([self.point(s, d0, z) for s, z in outline_sz], [self.point(s, d1, z) for s, z in outline_sz])


def panels(m, frame, s0, s1, z0, z1, d0=-REVEAL, d1=0.0):
    """Precast cladding panels between s0 and s1, with open joints showing the wall behind."""
    if z1 - z0 < 0.01 or s1 - s0 < 0.01:
        return
    count = max(1, round((s1 - s0) / PANEL))
    step = (s1 - s0) / count
    for i in range(count):
        a = s0 + step * i + (PANEL_JOINT / 2.0 if i else 0.0)
        b = s0 + step * (i + 1) - (PANEL_JOINT / 2.0 if i < count - 1 else 0.0)
        m.add("Concrete", frame.box(a, b, d0, d1, z0, z1))


def glazing(m, frame, a, b, sill, head, door=False):
    """A window (or glazed doors) set back in its reveal: glass, frame, mullions about every metre, a transom on tall
    openings and a metal sill."""
    back, front = -REVEAL, -REVEAL + FRAME_DEPTH
    m.add("Glass", frame.box(a, b, back, back + GLASS, sill, head))
    m.add("PaintedMetal", frame.box(a, a + FRAME, back, front, sill, head), frame.box(b - FRAME, b, back, front, sill, head),
          frame.box(a + FRAME, b - FRAME, back, front, head - FRAME, head))
    if door:
        m.add("PaintedMetal", frame.box(a + FRAME, b - FRAME, back, front, sill, sill + 0.12))
    else:
        m.add("PaintedMetal", frame.box(a - 0.04, b + 0.04, back, -0.01, sill, sill + SILL))
    count = max(1, round((b - a) / 1.0))
    for i in range(1, count):
        s = a + (b - a) * i / count
        m.add("PaintedMetal", frame.box(s - FRAME / 2.0, s + FRAME / 2.0, back, front, sill, head))
    if head - sill > 1.9:
        z = sill + (head - sill) * (0.78 if door else 0.72)
        m.add("PaintedMetal", frame.box(a + FRAME, b - FRAME, back, front, z - FRAME / 2.0, z + FRAME / 2.0))


def facade(m, frame, z0, z1, rows=(), trims=(0.0, 0.0), skip=(), spans=None, door=False):
    """The cladding of one wall from z0 to z1: panelled bands between the window rows, piers between the windows.
    rows: (sill, head) per storey; skip: (s0, s1) ranges kept blank (plaques); spans: explicit window ranges."""
    s0, s1 = trims[0], frame.length - trims[1]
    if s1 - s0 < 0.05:
        return
    if spans is None:
        spans = []
        if rows:
            count = max(1, int((s1 - s0) / BAY + 0.3))
            step = (s1 - s0) / count
            width = min(WINDOW, step - 0.6)
            for i in range(count):
                c = s0 + step * (i + 0.5)
                a, b = c - width / 2.0, c + width / 2.0
                if width > 0.5 and not any(a < k1 and b > k0 for k0, k1 in skip):
                    spans.append((a, b))
    levels = [z0] + [z for row in rows for z in row] + [z1]
    for k in range(0, len(levels), 2):
        panels(m, frame, s0, s1, levels[k], levels[k + 1])
    edges = [s0] + [x for span in spans for x in span] + [s1]
    for sill, head in rows:
        for k in range(0, len(edges), 2):
            if edges[k + 1] - edges[k] > 0.01:
                m.add("Concrete", frame.box(edges[k], edges[k + 1], -REVEAL, 0.0, sill, head))
        for a, b in spans:
            glazing(m, frame, a, b, sill, head, door)


def block(m, outline, z0, roof, rows=None, skips=None, spans=None, doors=(), parapet=PARAPET, coping_above=False):
    """A flat-roofed building on a counter-clockwise outline: the mass set back by the reveal, the cladding in front of
    it with corner piers, and the roof: a parapet under a metal coping round a membrane deck, or (coping_above) a flat
    deck at the roof height with the coping standing on its edge. rows, skips, spans and doors are per edge index
    (edge k runs from outline[k] to outline[k + 1]). Returns the deck height and the deck's outline."""
    rows, skips, spans = rows or {}, skips or {}, spans or {}
    inner = inset_outline(outline, REVEAL)
    deck = roof if coping_above else roof - parapet
    top = roof if coping_above else roof - COPING
    m.add("Concrete", plan_prism(inner, z0, deck))
    count = len(outline)
    for k in range(count):
        p0, p1 = outline[k], outline[(k + 1) % count]
        frame = Frame(p0, p1)
        t0 = (Vector(inner[k]) - Vector(p0)).dot(frame.e)
        t1 = (Vector(p1) - Vector(inner[(k + 1) % count])).dot(frame.e)
        facade(m, frame, z0, top, rows.get(k, ()), (t0, t1), skips.get(k, ()), spans.get(k), k in doors)
        before = Frame(outline[k - 1], p0).e
        corner, back = Vector(p0), Vector(inner[k])
        piece = [corner + before * (back - corner).dot(before), corner, corner + frame.e * (back - corner).dot(frame.e), back]
        m.add("Concrete", plan_prism([tuple(p) for p in piece], z0, top))
    if coping_above:
        m.add("PaintedMetal", annulus(outline, inset_outline(outline, REVEAL + 0.3), roof, roof + COPING))
        return deck, inset_outline(outline, REVEAL + 0.3)
    wall = inset_outline(outline, REVEAL + PARAPET_WALL)
    m.add("Concrete", annulus(inner, wall, deck, top))
    m.add("Roofing", plan_prism(wall, deck, deck + ROOF_MEMBRANE))
    m.add("PaintedMetal", annulus(outline, inset_outline(outline, REVEAL + PARAPET_WALL + 0.03), top, roof))
    return deck, wall


def emblem(m, frame, cs, cz, size, d0):
    """The generic eagle-and-star emblem (original): a ring round a five-point star, three swept feathers each side."""
    d1 = d0 + 0.025
    r = size * 0.2
    segments = 20
    for i in range(segments):
        a0, a1 = 2.0 * math.pi * i / segments, 2.0 * math.pi * (i + 1) / segments
        m.add("EmblemWhite", frame.plate([(cs + r * math.cos(a0), cz + r * math.sin(a0)),
                                          (cs + r * math.cos(a1), cz + r * math.sin(a1)),
                                          (cs + r * 0.84 * math.cos(a1), cz + r * 0.84 * math.sin(a1)),
                                          (cs + r * 0.84 * math.cos(a0), cz + r * 0.84 * math.sin(a0))], d0, d1))
    star = [(cs + (r * 0.72 if i % 2 == 0 else r * 0.29) * math.cos(math.pi / 2.0 + math.pi * i / 5.0),
             cz + (r * 0.72 if i % 2 == 0 else r * 0.29) * math.sin(math.pi / 2.0 + math.pi * i / 5.0)) for i in range(10)]
    m.add("EmblemWhite", frame.plate(star, d0, d1))
    for side in (-1.0, 1.0):
        for j in range(3):
            inner_s = cs + side * r * 1.1
            outer_s = cs + side * (r * 1.1 + size * (0.30 - 0.06 * j))
            zc = cz + size * (0.07 - 0.085 * j)
            h = size * 0.055
            lift = size * (0.05 - 0.01 * j)
            m.add("EmblemWhite", frame.plate([(inner_s, zc - h), (outer_s, zc + lift - h * 0.35),
                                              (outer_s, zc + lift + h * 0.35), (inner_s, zc + h)], d0, d1))


def plaque(m, frame, s_centre, z0, z1, width, d0=0.0):
    """A team-colour eagle plaque standing proud of a wall: blue plate, metal border, white emblem."""
    m.add("PaintedMetal", frame.box(s_centre - width / 2.0, s_centre + width / 2.0, d0, d0 + PLATE_DEPTH * 0.5, z0, z1))
    m.add("EaglePlate", frame.box(s_centre - width / 2.0 + 0.06, s_centre + width / 2.0 - 0.06, d0, d0 + PLATE_DEPTH,
                                  z0 + 0.06, z1 - 0.06))
    emblem(m, frame, s_centre, (z0 + z1) / 2.0, min(width, (z1 - z0) * 1.4) * 0.9, d0 + PLATE_DEPTH)


def shrub(m, x, y, z, radius):
    m.add("Foliage", sphere((x, y, z + radius * 0.7), radius, 8, 5))


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
    if LEVEL >= 2:
        # Painted lines: the slab's edge line, the exit lane's edges and a keep-clear hatch in front of the door.
        inset, width, thick = MARKING
        a, b = HALF - inset - width, HALF - inset
        z0, z1 = APRON_TOP - 0.003, APRON_TOP + thick  # let into the slab, 1 mm proud
        m.add("Marking", box(-b, b, a, b, z0, z1), box(-b, b, -b, -a, z0, z1), box(a, b, -a, a, z0, z1),
              box(-b, -a, -a, a, z0, z1))
        x0 = DOOR_PLANE + DOOR_POST[1] + 0.05
        for y in (DOOR_CENTRE_Y - LANE_WIDTH / 2.0, DOOR_CENTRE_Y + LANE_WIDTH / 2.0):
            m.add("Hazard", box(x0, a - 0.05, y - LANE_LINE / 2.0, y + LANE_LINE / 2.0, z0, z1))
        lane0 = DOOR_CENTRE_Y - LANE_WIDTH / 2.0 + 0.2
        for i in range(7):
            y = lane0 + i * 0.9
            m.add("Hazard", plan_prism([(x0 + 0.1, y), (x0 + 0.1, y + 0.3), (a - 0.15, y + 0.95), (a - 0.15, y + 0.65)],
                                       z0, z1))
    return m.finish()


def build_core():
    m = Module("Core")
    cx, cy = CORE_CENTRE
    if LEVEL < 2:
        m.add("Concrete", plan_prism(octagon(cx, cy, CORE_APOTHEM), APRON_TOP, CORE_ROOF))
        m.add("PaintedMetal", plan_prism(octagon(cx, cy, DRUM_APOTHEM), CORE_ROOF, DRUM_ROOF))
        mast_base = DRUM_ROOF
    else:
        outline = octagon(cx, cy, CORE_APOTHEM)
        entrance = Frame(outline[1], outline[2])  # the (+X, +Y) face, over the courtyard
        mid = entrance.length / 2.0
        width, head = ENTRANCE
        rows = {k: CORE_ROWS for k in range(8)}
        rows[1] = [(COURTYARD_TOP, head)]
        deck, _ = block(m, outline, APRON_TOP, CORE_ROOF, rows, spans={1: [(mid - width / 2.0, mid + width / 2.0)]},
                        doors={1})
        # Entrance: a canopy with a lamp strip under its edge, blue lamp strips either side, the plaque above.
        w, depth, under, thick = CANOPY
        m.add("Concrete", entrance.box(mid - w / 2.0, mid + w / 2.0, 0.0, depth, under, under + thick))
        m.add("Lamp", entrance.box(mid - w / 2.0 + 0.2, mid + w / 2.0 - 0.2, depth - 0.12, depth - 0.04, under - 0.03,
                                   under))
        for side in (-1.0, 1.0):
            s = mid + side * (width / 2.0 + 0.3)
            m.add("Lamp", entrance.box(s - LAMP_STRIP / 2.0, s + LAMP_STRIP / 2.0, 0.0, 0.05, COURTYARD_TOP + 0.3,
                                       head - 0.2))
        plate_w, plate_z0, plate_z1 = CORE_PLATE
        plaque(m, entrance, mid, plate_z0, plate_z1, plate_w)
        # The drum: a ribbon of windows, a blue lamp band under its coping.
        drum = octagon(cx, cy, DRUM_APOTHEM)
        mast_base, _ = block(m, drum, deck, DRUM_ROOF, {k: DRUM_ROWS for k in range(8)}, parapet=DRUM_PARAPET)
        for k in range(8):
            frame = Frame(drum[k], drum[(k + 1) % 8])
            m.add("Lamp", frame.box(0.35, frame.length - 0.35, 0.0, 0.03, DRUM_ROOF - 0.42, DRUM_ROOF - 0.32))
        mast_base += ROOF_MEMBRANE
    # The antenna cluster: a lattice mast on the drum to the 24 m tip, whips round it.
    m.add("BareMetal", *lattice_tower(cx, cy, mast_base, CORE_MAST_TIP - 1.5, 0.45, 0.2, 4))
    m.add("BareMetal", post(cx, cy, CORE_MAST_TIP - 1.5, CORE_MAST_TIP, 0.06))
    for dx, dy, tip in ((1.6, 1.6, 20.5), (-1.6, 1.6, 19.5), (1.6, -1.6, 19.0), (-1.6, -1.6, 20.0)):
        m.add("BareMetal", post(cx + dx, cy + dy, mast_base, tip, 0.05))
    return m.finish()


def build_wing():
    m = Module("Wing")
    if LEVEL < 2:
        for extent, roof in ((WING_REAR, WING_ROOF), (WING_LINK, WING_LINK_ROOF), (WING_FRONT, WING_ROOF)):
            m.add("Concrete", box(*extent, APRON_TOP, roof))
        m.add("Concrete", box(*DISH_TOWER, WING_ROOF, DISH_TOWER_ROOF))
        deck = WING_ROOF
    else:
        # Edges of a rect outline: 0 faces -Y, 1 faces +X, 2 faces +Y, 3 faces -X. Faces against another block stay
        # blank; the +Y faces carry the eagle plaques.
        plate_w, plate_z0, plate_z1 = WING_PLATE
        for extent, roof, rows, plaques in ((WING_REAR, WING_ROOF, {0: WING_ROWS, 2: WING_ROWS, 3: WING_ROWS}, (2,)),
                                            (WING_LINK, WING_LINK_ROOF, {0: LINK_ROWS, 2: LINK_ROWS}, ()),
                                            (WING_FRONT, WING_ROOF, {0: WING_ROWS, 1: WING_ROWS, 2: WING_ROWS}, (1, 2))):
            outline = rect(*extent)
            skips = {}
            for k in plaques:
                frame = Frame(outline[k], outline[(k + 1) % 4])
                mid = frame.length / 2.0
                skips[k] = [(mid - plate_w / 2.0 - 0.2, mid + plate_w / 2.0 + 0.2)]
            block(m, outline, APRON_TOP, roof, rows, skips)
            for k in plaques:
                frame = Frame(outline[k], outline[(k + 1) % 4])
                plaque(m, frame, frame.length / 2.0, plate_z0, plate_z1, plate_w)
        deck = WING_ROOF - PARAPET
        tower = rect(*DISH_TOWER)
        block(m, tower, deck, DISH_TOWER_ROOF, {0: [(9.4, 10.4)], 3: [(9.4, 10.4)]}, coping_above=True)
        frame = Frame(tower[2], tower[3])
        w, z0, z1 = DISH_TOWER_PLATE
        plaque(m, frame, frame.length / 2.0, z0, z1, w)
        deck += ROOF_MEMBRANE
    # Panel antenna array on the rear block and its masts.
    m.add("PaintedMetal", box(-11.0, -9.0, 5.5, 6.0, deck, deck + 2.0))
    for x, y in ((-11.0, 12.0), (-10.0, 12.0)):
        m.add("BareMetal", post(x, y, deck, WING_MAST_TIP, 0.06))
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
    if LEVEL < 2:
        garage_shell(m)
        m.add("Glass", box(6.0, 10.5, -9.0, -4.0, GARAGE_ROOF, GARAGE_ROOF + 0.4))  # rooftop solar panels
        return m.finish()
    x0, x1, y0, y1 = GARAGE
    bay_x0 = DOOR_PLANE - BAY_DEPTH
    dy0, dy1 = DOOR_CENTRE_Y - DOOR_WIDTH / 2.0, DOOR_CENTRE_Y + DOOR_WIDTH / 2.0
    deck = GARAGE_ROOF - PARAPET
    top = GARAGE_ROOF - COPING
    skin = GARAGE_SKIN
    # The shell round the bay (the bay itself exactly as level 1), its long walls set back behind the pilasters.
    m.add("Concrete",
          box(x0, bay_x0, y0 + skin, y1 - skin, APRON_TOP, deck),
          box(bay_x0, x1, y0 + skin, dy0, APRON_TOP, deck),
          box(bay_x0, x1, dy1, y1 - skin, APRON_TOP, deck),
          box(bay_x0, x1, dy0, dy1, BAY_CEILING, deck),
          box(x1 - LINTEL_DEPTH, x1, dy0, dy1, DOOR_TOP, BAY_CEILING))
    width, spacing = PILASTER
    count = max(1, round((x1 - x0) / spacing))
    columns = [x0 + (x1 - x0) * i / count for i in range(count + 1)]
    for x in columns:
        a, b = max(x0, x - width / 2.0), min(x1, x + width / 2.0)
        m.add("Concrete", box(a, b, y0, y0 + skin, APRON_TOP, top), box(a, b, y1 - skin, y1, APRON_TOP, top))
    for a, b in zip(columns, columns[1:]):
        for wall, out in ((y0 + skin, -1.0), (y1 - skin, 1.0)):
            # Plinth, a clerestory strip with its frame.
            ya, yb = sorted((wall, wall + out * 0.06))
            m.add("Concrete", box(a + width / 2.0, b - width / 2.0, ya, yb, APRON_TOP, 0.6))
            ga, gb = sorted((wall, wall + out * 0.03))
            m.add("Glass", box(a + width / 2.0 + 0.2, b - width / 2.0 - 0.2, ga, gb, CLERESTORY[0], CLERESTORY[1]))
            for z in (CLERESTORY[0] - 0.08, CLERESTORY[1]):
                m.add("PaintedMetal", box(a + width / 2.0 + 0.15, b - width / 2.0 - 0.15, ya, yb, z, z + 0.08))
    # Roof: parapet round a membrane deck, metal coping.
    outline = rect(x0, x1, y0, y1)
    shell = rect(x0, x1, y0 + skin, y1 - skin)
    wall = inset_outline(shell, PARAPET_WALL)
    m.add("Concrete", annulus(shell, wall, deck, top))
    m.add("Roofing", plan_prism(wall, deck, deck + ROOF_MEMBRANE))
    m.add("PaintedMetal", annulus(outline, inset_outline(outline, skin + PARAPET_WALL + 0.03), top, GARAGE_ROOF))
    m.add("Glass", box(6.0, 10.5, -9.0, -4.0, deck + ROOF_MEMBRANE, deck + 0.4))  # solar panels (detailed later)
    # The door frame: hazard-striped steel posts and a steel header, a blue lamp strip over the opening.
    front0, front1 = x1 - 0.1, x1 + DOOR_POST[1]
    post_w = DOOR_POST[0]
    bands = int(math.ceil((DOOR_TOP - APRON_TOP) / HAZARD_BAND))
    for ya, yb in ((dy0 - post_w, dy0), (dy1, dy1 + post_w)):
        for i in range(bands):
            z0 = APRON_TOP + i * HAZARD_BAND
            m.add("Hazard" if i % 2 else "ArmouredSteel", box(front0, front1, ya, yb, z0, min(DOOR_TOP, z0 + HAZARD_BAND)))
    m.add("ArmouredSteel", box(front0, front1, dy0 - post_w, dy1 + post_w, DOOR_TOP, DOOR_TOP + 0.35))
    m.add("Lamp", box(front1, front1 + 0.05, dy0 + 0.2, dy1 - 0.2, DOOR_TOP + 0.45, DOOR_TOP + 0.55))
    # Inside: the roller drum's housing behind the lintel, ceiling light strips, a rubber floor.
    m.add("ArmouredSteel", box(x1 - LINTEL_DEPTH - 0.5, x1 - LINTEL_DEPTH, dy0, dy1, DOOR_TOP, BAY_CEILING))
    for y in (dy0 + 1.0, dy1 - 1.0):
        m.add("Lamp", box(bay_x0 + 1.0, x1 - 1.5, y - 0.1, y + 0.1, BAY_CEILING - 0.02, BAY_CEILING))
    m.add("Rubber", box(bay_x0, x1 - LINTEL_DEPTH, dy0, dy1, APRON_TOP, APRON_TOP + 0.01))
    # The +Y wall: a blue accent panel and a personnel door; the -Y wall: a blue panel.
    m.add("BlueAccent", box(9.6, 11.4, y1 - skin, y1 - skin + 0.05, 1.4, 4.4))
    m.add("ArmouredSteel", box(4.4, 5.4, y1 - skin, y1 - skin + 0.04, APRON_TOP, 2.3))
    m.add("BlueAccent", box(6.6, 9.2, y0 + skin - 0.05, y0 + skin, 1.4, 4.4))
    return m.finish()


def build_garage_door():
    m = Module("GarageDoor")
    y0, y1 = DOOR_CENTRE_Y - DOOR_WIDTH / 2.0 + 0.01, DOOR_CENTRE_Y + DOOR_WIDTH / 2.0 - 0.01
    m.add("ArmouredSteel", box(*DOOR_LEAF, y0, y1, APRON_TOP, DOOR_TOP))
    if LEVEL >= 2:
        # Sectional leaf: six insulated panels with ribs at the joints, a row of vision panels, a rubber bottom seal.
        face = DOOR_LEAF[1]
        sections = 6
        height = (DOOR_TOP - APRON_TOP) / sections
        for i in range(1, sections):
            z = APRON_TOP + height * i
            m.add("PaintedMetal", box(face, face + 0.03, y0, y1, z - 0.03, z + 0.03))
        pane = (y1 - y0 - 0.8) / 6.0
        for k in range(6):
            a = y0 + 0.4 + k * pane
            m.add("Glass", box(face, face + 0.01, a + 0.1, a + pane - 0.1, APRON_TOP + height * 3.3,
                               APRON_TOP + height * 3.7))
        m.add("Rubber", box(DOOR_LEAF[0], face + 0.02, y0, y1, APRON_TOP, APRON_TOP + 0.06))
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
    if LEVEL < 2:
        m.add("EaglePlate", centred_box(fx, fy, PLINTH[0], PLINTH[1], COURTYARD_TOP, COURTYARD_TOP + PLINTH[2]))
        for py in (y0 + 1.2, y1 - 1.2):
            m.add("Concrete", centred_box(fx, py, 2.2, 1.4, COURTYARD_TOP, COURTYARD_TOP + 0.7))
        return m.finish()
    # Kerb and paving, metal step nosings.
    inner = inset_outline(outline, 0.3)
    m.add("Concrete", annulus(outline, inner, COURTYARD_TOP, COURTYARD_TOP + 0.06))
    m.add("Apron", plan_prism(inner, COURTYARD_TOP, COURTYARD_TOP + 0.02))
    for k in range(risers):
        edge = x1 + tread * (risers - k)
        m.add("ArmouredSteel", box(edge - 0.05, edge, STEPS_Y[0], STEPS_Y[1], APRON_TOP + rise * (k + 1) - 0.03,
                                   APRON_TOP + rise * (k + 1) + 0.005))
    # The flag plinth: concrete with the eagle plaque on its front.
    sx, sy, sh = PLINTH
    m.add("Concrete", centred_box(fx, fy, sx, sy, COURTYARD_TOP, COURTYARD_TOP + sh))
    m.add("Concrete", centred_box(fx, fy, sx + 0.16, sy + 0.16, COURTYARD_TOP, COURTYARD_TOP + 0.15))
    front = Frame((fx + sx / 2.0, fy - sy / 2.0), (fx + sx / 2.0, fy + sy / 2.0))
    plaque(m, front, sy / 2.0, COURTYARD_TOP + 0.2, COURTYARD_TOP + sh - 0.08, sy - 0.2)
    # Two planters with soil and shrubs.
    for py in (y0 + 1.2, y1 - 1.2):
        planter = rect(fx - 1.1, fx + 1.1, py - 0.7, py + 0.7)
        bed = inset_outline(planter, 0.15)
        m.add("Concrete", annulus(planter, bed, COURTYARD_TOP, COURTYARD_TOP + 0.7))
        m.add("Soil", plan_prism(bed, COURTYARD_TOP, COURTYARD_TOP + 0.6))
        for dx, dy, r in ((-0.55, -0.15, 0.38), (0.05, 0.2, 0.45), (0.6, -0.1, 0.35)):
            shrub(m, fx + dx, py + dy, COURTYARD_TOP + 0.6, r)
    return m.finish()


def build_flag(key, x, y, z0, tip):
    m = Module(key)
    m.add("BareMetal", post(x, y, z0, tip, 0.07, 8, 0.04))
    m.add("BlueAccent", box(x + 0.08, x + 1.98, y - 0.02, y + 0.02, tip - 1.3, tip - 0.1))
    return m.finish()


def corridor(m, outline, roof, exposed):
    """A glazed link: concrete plinth, glazing set back between mullions, a roof slab with a blue lamp strip along the
    fascia of its exposed edges (by index)."""
    plinth, fascia, setback, spacing = CORRIDOR
    m.add("Concrete", plan_prism(outline, APRON_TOP, plinth))
    m.add("Glass", plan_prism(inset_outline(outline, setback), plinth, roof - fascia))
    m.add("Concrete", plan_prism(outline, roof - fascia, roof))
    for k in range(len(outline)):
        frame = Frame(outline[k], outline[(k + 1) % len(outline)])
        count = max(1, round(frame.length / spacing))
        for i in range(count + 1):
            s = min(max(frame.length * i / count, 0.25), frame.length - 0.25)  # clear of acute corners
            m.add("PaintedMetal", frame.box(s - 0.05, s + 0.05, -setback, 0.0, plinth, roof - fascia))
        if k in exposed:
            m.add("Lamp", frame.box(0.2, frame.length - 0.2, 0.0, 0.03, roof - fascia + 0.1, roof - fascia + 0.18))


def build_connectors():
    m = Module("Connectors")
    # Core (+Y flat) to the wing link.
    x0, x1 = CONNECTOR_WING
    if LEVEL >= 2:
        corridor(m, rect(x0, x1, on_core_face(90.0, 0.0)[1], WING_LINK[2]), CONNECTOR_ROOF, (1, 3))
    else:
        m.add("Glass", box(x0, x1, on_core_face(90.0, 0.0)[1], WING_LINK[2], APRON_TOP, CONNECTOR_ROOF))
    # Core ((+X, -Y) face) to the garage's rear wall.
    y0, y1 = CONNECTOR_GARAGE_Y
    normal = Vector((math.cos(math.radians(-45.0)), math.sin(math.radians(-45.0))))
    reach = CORE_APOTHEM + Vector(CORE_CENTRE).dot(normal)

    def face_x(y):  # the core's (+X, -Y) face line at this y
        return (reach - normal.y * y) / normal.x
    link = [(face_x(y0), y0), (GARAGE[0], y0), (GARAGE[0], y1), (face_x(y1), y1)]
    # Rear annex off the core's (-X, -Y) face, the second flag on its roof.
    a, b = on_core_face(-135.0, 1.0), on_core_face(-135.0, -1.0)
    a2, b2 = on_core_face(-135.0, 1.0, ANNEX_DEPTH), on_core_face(-135.0, -1.0, ANNEX_DEPTH)
    annex = ccw([a, a2, b2, b])
    if LEVEL >= 2:
        corridor(m, link, CONNECTOR_ROOF, (0, 2))
        block(m, annex, APRON_TOP, CONNECTOR_ROOF, {k: ANNEX_ROWS for k in range(4)}, parapet=ANNEX_PARAPET)
    else:
        m.add("Glass", plan_prism(link, APRON_TOP, CONNECTOR_ROOF))
        m.add("Concrete", plan_prism(annex, APRON_TOP, CONNECTOR_ROOF))
    return m.finish()


def build_radar_tower():
    m = Module("RadarTower")
    if LEVEL < 2:
        m.add("Concrete", box(*RADAR_TOWER, APRON_TOP, RADAR_TOWER_ROOF))
        return m.finish()
    tower = rect(*RADAR_TOWER)
    block(m, tower, APRON_TOP, RADAR_TOWER_ROOF, {k: TOWER_ROWS for k in range(4)}, coping_above=True)
    frame = Frame(tower[1], tower[2])  # +X face, towards the garage
    w, z0, z1 = TOWER_PLATE
    plaque(m, frame, frame.length / 2.0, z0, z1, w)
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
            if LEVEL >= 2:  # warm interior behind the glazing (spec section 7)
                shader.inputs["Emission Color"].default_value = (1.0, 0.72, 0.42, 1.0)
                shader.inputs["Emission Strength"].default_value = 0.35
        if LEVEL >= 2 and name in WEATHERED:
            weather(material, colour, *WEATHERED[name])


def weather(material, colour, scale, variation, bump, grime):
    """Mottled colour, a fine bump and, with grime > 0, a darker band up to that height off the ground."""
    nodes, links = material.node_tree.nodes, material.node_tree.links
    shader = nodes["Principled BSDF"]
    coords = nodes.new("ShaderNodeTexCoord")
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = 8.0
    links.new(coords.outputs["Object"], noise.inputs["Vector"])
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (*[c * (1.0 - variation) for c in colour], 1.0)
    ramp.color_ramp.elements[1].color = (*[min(1.0, c * (1.0 + variation)) for c in colour], 1.0)
    links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    result = ramp.outputs["Color"]
    if grime:
        geometry = nodes.new("ShaderNodeNewGeometry")
        split = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geometry.outputs["Position"], split.inputs[0])
        height = nodes.new("ShaderNodeMapRange")
        height.inputs["From Min"].default_value = 0.0
        height.inputs["From Max"].default_value = grime
        height.inputs["To Min"].default_value = 0.62
        height.inputs["To Max"].default_value = 1.0
        links.new(split.outputs["Z"], height.inputs["Value"])
        darken = nodes.new("ShaderNodeVectorMath")
        darken.operation = "MULTIPLY"
        links.new(result, darken.inputs[0])
        links.new(height.outputs["Result"], darken.inputs[1])
        result = darken.outputs["Vector"]
    links.new(result, shader.inputs["Base Color"])
    fine = nodes.new("ShaderNodeTexNoise")
    fine.inputs["Scale"].default_value = scale * 40.0
    links.new(coords.outputs["Object"], fine.inputs["Vector"])
    bumper = nodes.new("ShaderNodeBump")
    bumper.inputs["Strength"].default_value = bump
    bumper.inputs["Distance"].default_value = 0.01
    links.new(fine.outputs["Fac"], bumper.inputs["Height"])
    links.new(bumper.outputs["Normal"], shader.inputs["Normal"])


def flag_core_base():
    """The second flag stands on the annex roof: on its membrane deck from level 2."""
    return CONNECTOR_ROOF if LEVEL < 2 else CONNECTOR_ROOF - ANNEX_PARAPET + ROOF_MEMBRANE


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    bpy.context.preferences.filepaths.save_version = 0  # no .blend1 next to the source
    make_materials()
    modules = [build_apron(), build_core(), build_wing(), build_garage(), build_courtyard(),
               build_flag("FlagCourtyard", *FLAG_COURTYARD, COURTYARD_TOP + PLINTH[2], FLAG_COURTYARD_TIP),
               build_flag("FlagCore", *FLAG_CORE, flag_core_base(), FLAG_CORE_TIP), build_connectors(),
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


def tree(obj, shrink=0.0, centres=None):
    """The object's BVH in world space; centres, if a list, gets each face's centre in Unreal's frame."""
    bm = world_bm(obj, shrink)
    result = BVHTree.FromBMesh(bm)
    if centres is not None:
        bm.faces.ensure_lookup_table()
        centres.extend(tuple(round(c, 2) for c in blender(face.calc_center_median())) for face in bm.faces)
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
    centres = {key_of(o): [] for o in solids}
    trees = {key_of(o): (tree(o, 0.0, centres[key_of(o)]), tree(o, TOUCH_TOLERANCE)) for o in solids}
    keys = list(trees)
    overlaps = []
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            touching = ON_APRON in (a, b) or (a, b) in ADJACENT or (b, a) in ADJACENT
            ta, tb = (trees[a][1], trees[b][1]) if touching else (trees[a][0], trees[b][0])
            pairs = ta.overlap(tb)
            if pairs:
                overlaps.append(f"{a}/{b} near {centres[a][pairs[0][0]]}")
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
