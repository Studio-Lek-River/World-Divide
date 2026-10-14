"""Builds the M20 Guardian main battle tank (original Lek River Studio work, after the blueprint sheet in
Art/USA/vehicles/m20_guardian) from code, as a modular prototype for close-zoom review:

  blender -b --python Tools/Art/m20_guardian_blender.py -- [--stage 1|2] [--verify]

--stage 1 builds the blockout (hull, running gear, turret, gun) and renders it; --stage 2 (the default) adds the
secondary detail, writes the .blend, one FBX per moving part, the renders, m20_guardian.json and report.md, all under
Art/USA/vehicles/m20_guardian. --verify re-imports every FBX into an empty scene and checks scale, origin and triangles.

Every dimension is in metres in the block below: tune proportions there, not in the meshes. The tank faces +X, +Y is its
left, its tracks sit on z = 0 and the origin is under the hull centre. Moving parts are separate objects with their pivot
as origin: Turret on the turret ring, Gun on the trunnions, RWS on its ring, each road wheel, idler and sprocket on its
axle, and each track. Bevels and weighted normals stay live as modifiers; the FBX export applies them.
"""

import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "USA", "vehicles", "m20_guardian")
BLEND = os.path.join(OUT_DIR, "source", "m20_guardian.blend")
FBX_DIR = os.path.join(OUT_DIR, "fbx")
RENDER_DIR = os.path.join(OUT_DIR, "renders")

# ---- dimensions ----
HULL_ROOF = 1.40
# Side profiles (x, z): the lower hull between the tracks, and the upper hull over it and the tracks with the glacis.
LOWER_HULL = [(-3.30, 0.52), (-3.05, 0.36), (2.70, 0.36), (3.38, 0.80), (3.38, 0.90), (3.00, 1.08), (-3.30, 1.08)]
UPPER_HULL = [(-3.30, 1.06), (2.95, 1.06), (3.38, 0.92), (2.30, 1.40), (-3.30, 1.40)]
LOWER_HALF_WIDTH = 1.02
UPPER_HALF_WIDTH = 1.62
GLACIS = ((3.38, 0.92), (2.30, 1.40))

TRACK_Y = 1.32
TRACK_HALF_WIDTH = 0.30
TRACK_THICKNESS = 0.10
TRACK_PITCH = 0.16
ROAD_WHEEL_RADIUS = 0.34
ROAD_WHEEL_X = [-2.0 + 0.80 * i for i in range(6)]
ROAD_WHEEL_Z = ROAD_WHEEL_RADIUS + TRACK_THICKNESS
SPROCKET = (-2.85, 0.62, 0.28)  # x, z, radius at the teeth roots
IDLER = (2.85, 0.58, 0.27)
RETURN_ROLLER_X = (-1.56, 0.0, 1.56)
RETURN_ROLLER_Z = 0.88
RETURN_ROLLER_RADIUS = 0.075

SKIRT_Y = 1.64
SKIRT_THICKNESS = 0.06
SKIRT_Z = (0.62, 1.36)
# Bolt-on skirt panels front to back by their x edges; the one starting at BLUE_SKIRT is the blue ID band.
SKIRT_EDGES = (-3.05, -2.05, -1.05, -0.05, 0.55, 1.00, 2.00, 3.05)
BLUE_SKIRT = 0.55

TURRET_RING = Vector((-0.35, 0.0, HULL_ROOF))
TURRET_RING_RADIUS = 1.0
TURRET_ROOF = 2.08
# Turret plan outlines (x, y) for the left half, front to back; the right half mirrors them.
TURRET_BASE = [(1.50, 0.40), (1.00, 1.36), (-1.25, 1.42), (-1.45, 1.30), (-2.35, 1.22)]
TURRET_TOP = [(1.28, 0.38), (0.82, 1.28), (-1.25, 1.34), (-1.45, 1.24), (-2.30, 1.16)]
TRUNNION = Vector((1.38, 0.0, 1.78))
MANTLET_FRONT = 1.80
MUZZLE_X = 4.50
BARREL_RADIUS = 0.08
BORE_RADIUS = 0.0525
SLEEVE_RADIUS = 0.112
FUME_EXTRACTOR = (2.90, 3.35, 0.15)

CUPOLA = Vector((-0.55, -0.70, TURRET_ROOF))
RWS_RING = CUPOLA + Vector((0.0, 0.0, 0.16))
RWS_BARREL_LENGTH = 1.10

BEVEL_WIDTH = 0.015
SHARP_ANGLE = math.radians(35.0)

# Material slots in the order every object lists them.
MATERIALS = ("Paint", "Blue", "Metal", "Rubber", "Track", "Dark", "Optics", "LightWhite", "LightRed")

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
STAGE = int(ARGS[ARGS.index("--stage") + 1]) if "--stage" in ARGS else 2
DETAIL = STAGE >= 2


# ---- geometry ----

class Part:
    """One object's geometry in a single bmesh, each piece tagged with its material and, if bevelled, its hard edges."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.bevel = self.bm.edges.layers.float.new("bevel_weight_edge")

    def add(self, material, build, bevel=True):
        bm = self.bm
        start = len(bm.faces)
        build(bm)
        bm.faces.ensure_lookup_table()
        faces = [bm.faces[i] for i in range(start, len(bm.faces))]
        bmesh.ops.recalc_face_normals(bm, faces=faces)
        index = MATERIALS.index(material)
        for face in faces:
            face.material_index = index
            face.smooth = True
            if bevel:
                for edge in face.edges:
                    if edge.is_manifold and edge.calc_face_angle(0.0) > math.radians(30.0):
                        edge[self.bevel] = 1.0

    def build(self, pivot, parent=None, parent_pivot=None):
        """The object, its origin at pivot (world space), with only the material slots it uses."""
        bm = self.bm
        bmesh.ops.translate(bm, verts=bm.verts, vec=-Vector(pivot))
        used = sorted({face.material_index for face in bm.faces})
        for face in bm.faces:
            face.material_index = used.index(face.material_index)
        mesh = bpy.data.meshes.new(self.name)
        bm.to_mesh(mesh)
        bm.free()
        for index in used:
            mesh.materials.append(bpy.data.materials[MATERIALS[index]])
        mesh.set_sharp_from_angle(angle=SHARP_ANGLE)
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        add_modifiers(obj)
        obj.parent = parent
        obj.location = Vector(pivot) - Vector(parent_pivot) if parent else Vector(pivot)
        return obj


def add_modifiers(obj):
    bevel = obj.modifiers.new("Bevel", "BEVEL")
    bevel.limit_method = "WEIGHT"
    bevel.width = BEVEL_WIDTH
    bevel.segments = 2
    bevel.harden_normals = True
    bevel.use_clamp_overlap = True
    weighted = obj.modifiers.new("WeightedNormal", "WEIGHTED_NORMAL")
    weighted.keep_sharp = True
    weighted.weight = 50


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


def side_prism(profile_xz, y0, y1):
    return loft([(x, y0, z) for x, z in profile_xz], [(x, y1, z) for x, z in profile_xz])


def plan_prism(profile_xy, z0, z1, top_xy=None):
    return loft([(x, y, z0) for x, y in profile_xy], [(x, y, z1) for x, y in (top_xy or profile_xy)])


def box(x0, x1, y0, y1, z0, z1):
    return plan_prism([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1)


def oriented_box(centre, u, v, w, hu, hv, hw):
    c = Vector(centre)

    def face(s):
        return [c + u * a * hu + v * b * hv + w * s * hw for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    return loft(face(-1), face(1))


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


def cylinder(centre, axis, radius, length, segments=16, radius_end=None):
    return loft(ring(centre, axis, -length / 2.0, radius, segments),
                ring(centre, axis, length / 2.0, radius if radius_end is None else radius_end, segments))


def tube(centre, axis, inner, outer, length, segments=24):
    """A hollow cylinder: rim, tyre, muzzle."""
    def build(bm):
        rings = [[bm.verts.new(p) for p in ring(centre, axis, offset, radius, segments)]
                 for offset in (-length / 2.0, length / 2.0) for radius in (outer, inner)]
        out0, in0, out1, in1 = rings
        for i in range(segments):
            j = (i + 1) % segments
            bm.faces.new((out0[i], out0[j], out1[j], out1[i]))
            bm.faces.new((in0[j], in0[i], in1[i], in1[j]))
            bm.faces.new((out0[j], out0[i], in0[i], in0[j]))
            bm.faces.new((out1[i], out1[j], in1[j], in1[i]))
    return build


def gear(centre, axis, root, tip, teeth, length):
    a, u, v = axis_frame(axis)
    points = []
    for i in range(teeth * 4):
        t = 2.0 * math.pi * i / (teeth * 4)
        radius = tip if i % 4 in (1, 2) else root
        points.append((u * math.cos(t) + v * math.sin(t)) * radius)
    c = Vector(centre)
    return loft([c - a * length / 2.0 + p for p in points], [c + a * length / 2.0 + p for p in points])


class Quad:
    """A flat face given by its corners: b0-b1 along the bottom (u), t0-t1 above them (v). Panels and bolts go on it."""

    def __init__(self, b0, b1, t0, t1, inside):
        self.b0, self.b1, self.t0, self.t1 = (Vector(p) for p in (b0, b1, t0, t1))
        normal = (self.b1 - self.b0).cross(self.t0 - self.b0).normalized()
        centre = (self.b0 + self.b1 + self.t0 + self.t1) / 4.0
        self.normal = normal if normal.dot(centre - Vector(inside)) > 0.0 else -normal

    def at(self, u, v, out=0.0):
        bottom = self.b0.lerp(self.b1, u)
        top = self.t0.lerp(self.t1, u)
        return bottom.lerp(top, v) + self.normal * out

    def panel(self, u0, u1, v0, v1, thickness, gap=0.004):
        corners = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
        return loft([self.at(u, v, gap) for u, v in corners], [self.at(u, v, gap + thickness) for u, v in corners])

    def bolt(self, u, v, out, radius=0.022, height=0.025):
        return cylinder(self.at(u, v, out + height / 2.0), self.normal, radius, height, segments=6)


def mirrored(points_xy):
    """A left-half outline (front to back) completed with its mirror into a closed counter-clockwise outline."""
    return list(points_xy) + [(x, -y) for x, y in reversed(points_xy)]


# ---- the tank ----

def build_hull():
    hull = Part("M20_Hull")
    hull.add("Paint", side_prism(LOWER_HULL, -LOWER_HALF_WIDTH, LOWER_HALF_WIDTH))
    hull.add("Paint", side_prism(UPPER_HULL, -UPPER_HALF_WIDTH, UPPER_HALF_WIDTH))
    # Return rollers on stub axles.
    for side in (1, -1):
        for x in RETURN_ROLLER_X:
            hull.add("Metal", cylinder((x, side * TRACK_Y, RETURN_ROLLER_Z), (0, 1, 0), RETURN_ROLLER_RADIUS,
                                       TRACK_HALF_WIDTH * 1.6, segments=12))
            hull.add("Dark", cylinder((x, side * (LOWER_HALF_WIDTH + 0.08), RETURN_ROLLER_Z), (0, 1, 0), 0.04, 0.16,
                                      segments=8), bevel=False)
    build_skirts(hull)
    if DETAIL:
        build_hull_detail(hull)
    return hull.build((0.0, 0.0, 0.0))


def build_skirts(hull):
    z0, z1 = SKIRT_Z
    for side in (1, -1):
        y0 = side * SKIRT_Y
        y1 = side * (SKIRT_Y + SKIRT_THICKNESS)
        outer = side * (SKIRT_Y + SKIRT_THICKNESS)
        for x0, x1 in zip(SKIRT_EDGES, SKIRT_EDGES[1:]):
            a, b = x0 + 0.012, x1 - 0.012
            if x1 == SKIRT_EDGES[-1]:
                # The front panel is raked up towards the idler.
                profile = [(a, z0), (b - 0.30, z0), (b, z0 + 0.28), (b, z1), (a, z1)]
            else:
                profile = [(a, z0), (b, z0), (b, z1), (a, z1)]
            hull.add("Blue" if x0 == BLUE_SKIRT else "Paint", side_prism(profile, y0, y1))
            if DETAIL:
                count = max(2, round((b - a) / 0.26))
                for i in range(count):
                    x = a + 0.07 + (b - a - 0.14) * i / (count - 1)
                    for z in (z0 + 0.07, z1 - 0.07):
                        if x1 == SKIRT_EDGES[-1] and z < z0 + 0.2 and x > b - 0.35:
                            continue
                        hull.add("Metal", cylinder((x, outer + side * 0.012, z), (0, 1, 0), 0.022, 0.024, segments=6),
                                 bevel=False)
        # A rail along the skirt tops.
        hull.add("Paint", box(SKIRT_EDGES[0], SKIRT_EDGES[-1], min(y0, y1 + side * 0.02), max(y0, y1 + side * 0.02),
                              z1 - 0.05, z1 + 0.02))
        if DETAIL:
            # A small blue ID patch near the rear of each skirt.
            patch_y = side * (SKIRT_Y + SKIRT_THICKNESS)
            hull.add("Blue", side_prism([(-2.95, 1.02), (-2.62, 1.02), (-2.62, 1.26), (-2.95, 1.26)],
                                        patch_y, patch_y + side * 0.012))


def build_hull_detail(hull):
    (gx0, gz0), (gx1, gz1) = GLACIS
    glacis = Quad((gx0, -UPPER_HALF_WIDTH, gz0), (gx0, UPPER_HALF_WIDTH, gz0),
                  (gx1, -UPPER_HALF_WIDTH, gz1), (gx1, UPPER_HALF_WIDTH, gz1), (0.0, 0.0, 0.8))
    # Glacis add-on armour: tan centre plates, blue corner panels.
    for u0, u1, material in ((0.02, 0.17, "Blue"), (0.19, 0.48, "Paint"), (0.52, 0.81, "Paint"), (0.83, 0.98, "Blue")):
        hull.add(material, glacis.panel(u0, u1, 0.10, 0.80, 0.04))
        for u in (u0 + 0.03, u1 - 0.03):
            for v in (0.18, 0.72):
                hull.add("Metal", glacis.bolt(u, v, 0.045), bevel=False)
    # Driver's hatch and periscopes.
    hull.add("Paint", cylinder((1.95, 0.0, HULL_ROOF + 0.03), (0, 0, 1), 0.30, 0.06, segments=20))
    hull.add("Metal", box(1.80, 1.90, -0.06, 0.06, HULL_ROOF + 0.06, HULL_ROOF + 0.10))
    for y in (-0.22, 0.0, 0.22):
        hull.add("Dark", box(2.22, 2.34, y - 0.07, y + 0.07, HULL_ROOF - 0.02, HULL_ROOF + 0.07))
        hull.add("Optics", box(2.335, 2.345, y - 0.055, y + 0.055, HULL_ROOF + 0.01, HULL_ROOF + 0.055), bevel=False)
    for side in (1, -1):
        # Headlight clusters with guards on the glacis corners.
        y = side * 1.38
        hull.add("Metal", box(3.02, 3.20, y - 0.10, y + 0.10, 1.00, 1.14))
        hull.add("LightWhite", box(3.20, 3.215, y - 0.08, y - 0.01, 1.03, 1.11), bevel=False)
        hull.add("LightWhite", box(3.20, 3.215, y + 0.01, y + 0.08, 1.03, 1.11), bevel=False)
        hull.add("Dark", box(3.18, 3.28, y - 0.13, y - 0.10, 0.96, 1.18), bevel=False)
        hull.add("Dark", box(3.18, 3.28, y + 0.10, y + 0.13, 0.96, 1.18), bevel=False)
        # Tow hooks on the lower front plate and the rear plate.
        hull.add("Metal", box(3.30, 3.46, side * 0.62 - 0.05, side * 0.62 + 0.05, 0.62, 0.72))
        hull.add("Metal", box(-3.42, -3.28, side * 0.62 - 0.05, side * 0.62 + 0.05, 0.62, 0.72))
        # Tail lights.
        hull.add("Dark", box(-3.36, -3.30, side * 1.45 - 0.09, side * 1.45 + 0.09, 1.18, 1.32))
        hull.add("LightRed", box(-3.365, -3.355, side * 1.45 - 0.07, side * 1.45 + 0.07, 1.21, 1.29), bevel=False)
        # Side grilles on the rear plate and exhaust boxes.
        build_grille(hull, -3.30, side * 0.84, 0.30, 0.78, 1.28, count=5)
        # Stowage on the rear deck corners: a tool box and two jerry cans.
        hull.add("Paint", box(-3.25, -2.70, side * 1.15 - 0.22, side * 1.15 + 0.22, HULL_ROOF, HULL_ROOF + 0.22))
        for x in (-2.60, -2.42):
            hull.add("Paint", box(x - 0.08, x + 0.08, side * 1.32 - 0.17, side * 1.32 + 0.17, HULL_ROOF,
                                  HULL_ROOF + 0.34))
            hull.add("Dark", box(x - 0.04, x + 0.04, side * 1.32 - 0.03, side * 1.32 + 0.03, HULL_ROOF + 0.34,
                                 HULL_ROOF + 0.38), bevel=False)
    # Main rear grille over the exhaust.
    build_grille(hull, -3.30, 0.0, 0.62, 0.66, 1.28, count=7)
    # Engine deck: raised louvred grilles behind the turret.
    for y0, y1 in ((-0.92, -0.06), (0.06, 0.92)):
        hull.add("Paint", box(-3.22, -1.98, y0, y1, HULL_ROOF, HULL_ROOF + 0.04))
        for i in range(14):
            x = -3.16 + i * 0.085
            hull.add("Dark", box(x, x + 0.045, y0 + 0.04, y1 - 0.04, HULL_ROOF + 0.04, HULL_ROOF + 0.065), bevel=False)
    # Bolt rows along the deck edges.
    for side in (1, -1):
        for i in range(14):
            x = -3.15 + i * 0.40
            hull.add("Metal", cylinder((x, side * 1.54, HULL_ROOF + 0.012), (0, 0, 1), 0.022, 0.024, segments=6),
                     bevel=False)


def build_grille(part, x_face, y_centre, half_width, z0, z1, count):
    """A framed grille with horizontal slats on a rear-facing plate at x_face."""
    part.add("Paint", box(x_face - 0.06, x_face, y_centre - half_width - 0.03, y_centre + half_width + 0.03, z0 - 0.03,
                          z1 + 0.03))
    for i in range(count):
        z = z0 + (z1 - z0) * (i + 0.5) / count
        part.add("Dark", box(x_face - 0.09, x_face - 0.06, y_centre - half_width, y_centre + half_width, z - 0.025,
                             z + 0.025), bevel=False)


def build_turret():
    turret = Part("M20_Turret")
    turret.add("Paint", plan_prism(mirrored(TURRET_BASE), HULL_ROOF + 0.02, TURRET_ROOF, mirrored(TURRET_TOP)))
    turret.add("Dark", cylinder(TURRET_RING + Vector((0.0, 0.0, 0.03)), (0, 0, 1), TURRET_RING_RADIUS, 0.06,
                                segments=32), bevel=False)
    # Commander's cupola, under the RWS.
    turret.add("Paint", cylinder(CUPOLA + Vector((0.0, 0.0, 0.07)), (0, 0, 1), 0.40, 0.14, segments=24))
    if DETAIL:
        build_turret_detail(turret)
    return turret.build(TURRET_RING)


def turret_face(side, start, end):
    """The turret wall between outline points start and end on one side, as a Quad (u from start to end)."""
    def point(outline, i, z):
        x, y = outline[i]
        return (x, side * y, z)
    return Quad(point(TURRET_BASE, start, HULL_ROOF + 0.02), point(TURRET_BASE, end, HULL_ROOF + 0.02),
                point(TURRET_TOP, start, TURRET_ROOF), point(TURRET_TOP, end, TURRET_ROOF),
                (TURRET_RING.x, 0.0, 1.75))


def build_turret_detail(turret):
    roof = TURRET_ROOF
    for side in (1, -1):
        # Bolt-on slab armour on the flanks; the front slab is the blue ID panel.
        flank = turret_face(side, 1, 2)
        for u0, u1, material in ((0.02, 0.17, "Blue"), (0.19, 0.45, "Paint"), (0.47, 0.73, "Paint"),
                                 (0.75, 0.98, "Paint")):
            turret.add(material, flank.panel(u0, u1, 0.10, 0.90, 0.06))
            for u in (u0 + 0.025, u1 - 0.025):
                for v in (0.2, 0.8):
                    turret.add("Metal", flank.bolt(u, v, 0.065), bevel=False)
        # Cheek armour, its outer part blue.
        cheek = turret_face(side, 0, 1)
        turret.add("Paint", cheek.panel(0.05, 0.58, 0.10, 0.88, 0.07))
        turret.add("Blue", cheek.panel(0.62, 0.95, 0.10, 0.88, 0.07))
        for u in (0.10, 0.52, 0.68, 0.90):
            for v in (0.2, 0.78):
                turret.add("Metal", cheek.bolt(u, v, 0.075), bevel=False)
        # Bustle side boxes.
        bustle = turret_face(side, 3, 4)
        turret.add("Paint", bustle.panel(0.05, 0.95, 0.12, 0.85, 0.10))
        # Smoke grenade launchers on the cheeks, four tubes angled up and forward.
        base = cheek.at(0.80, 1.0, 0.0) + Vector((0.0, 0.0, 0.02))
        turret.add("Dark", box(base.x - 0.12, base.x + 0.12, base.y - 0.14, base.y + 0.14, base.z, base.z + 0.06))
        for i in range(4):
            centre = base + Vector((0.0, -0.10 + 0.067 * i, 0.12))
            turret.add("Dark", cylinder(centre, (0.7, side * 0.3, 0.65), 0.033, 0.20, segments=8), bevel=False)
        # Antenna bases and whips at the rear corners.
        antenna = Vector((-1.95, side * 1.00, roof))
        turret.add("Metal", cylinder(antenna + Vector((0.0, 0.0, 0.05)), (0, 0, 1), 0.045, 0.10, segments=10))
        turret.add("Dark", cylinder(antenna + Vector((0.0, 0.0, 0.10 + 0.50)), (0, 0, 1), 0.009, 1.00, segments=6),
                   bevel=False)
        # Warning sensors on the roof corners.
        corner = Vector((0.55, side * 1.18, roof))
        turret.add("Paint", box(corner.x - 0.08, corner.x + 0.08, corner.y - 0.08, corner.y + 0.08, roof, roof + 0.10))
        turret.add("Optics", box(corner.x + 0.08, corner.x + 0.09, corner.y - 0.05, corner.y + 0.05, roof + 0.02,
                                 roof + 0.08), bevel=False)
    # Bustle rack: rails and posts around the rear, holding boxes, jerry cans and a rolled tarp.
    rack_x0, rack_x1, rack_y = -1.55, -2.62, 1.46
    for z in (1.62, 2.00):
        for side in (1, -1):
            turret.add("Metal", box(rack_x1, rack_x0, side * rack_y - 0.02, side * rack_y + 0.02, z - 0.02, z + 0.02),
                       bevel=False)
        turret.add("Metal", box(rack_x1 - 0.02, rack_x1 + 0.02, -rack_y, rack_y, z - 0.02, z + 0.02), bevel=False)
    for x, y in ((rack_x0, rack_y), (rack_x0, -rack_y), (rack_x1, rack_y), (rack_x1, -rack_y), (rack_x1, 0.0),
                 (-2.1, rack_y), (-2.1, -rack_y)):
        turret.add("Metal", box(x - 0.02, x + 0.02, y - 0.02, y + 0.02, 1.55, 2.02), bevel=False)
    turret.add("Metal", box(rack_x1 + 0.02, -2.32, -rack_y, rack_y, 1.58, 1.60), bevel=False)
    for y0, y1 in ((-1.20, -0.62), (-0.55, 0.05)):
        turret.add("Paint", box(-2.58, -2.36, y0, y1, 1.60, 1.95))
    for y in (0.20, 0.40, 0.60):
        turret.add("Paint", box(-2.56, -2.38, y - 0.08, y + 0.08, 1.60, 1.94))
        turret.add("Dark", box(-2.50, -2.44, y - 0.03, y + 0.03, 1.94, 1.98), bevel=False)
    turret.add("Rubber", cylinder((-2.47, 1.00, 1.74), (0, 1, 0), 0.12, 0.62, segments=12))
    turret.add("Paint", box(-2.58, -2.36, 0.72, 0.84, 1.60, 1.86))
    # Gunner's primary sight, left front of the roof: a slab-sided housing with two windows.
    gps = Vector((0.55, 0.78, roof))
    turret.add("Paint", plan_prism([(gps.x - 0.22, gps.y - 0.18), (gps.x + 0.22, gps.y - 0.18),
                                    (gps.x + 0.22, gps.y + 0.18), (gps.x - 0.22, gps.y + 0.18)], roof, roof + 0.34,
                                   [(gps.x - 0.20, gps.y - 0.16), (gps.x + 0.12, gps.y - 0.16),
                                    (gps.x + 0.12, gps.y + 0.16), (gps.x - 0.20, gps.y + 0.16)]))
    turret.add("Dark", box(gps.x + 0.18, gps.x + 0.22, gps.y - 0.15, gps.y + 0.15, roof + 0.05, roof + 0.29))
    turret.add("Optics", cylinder((gps.x + 0.225, gps.y - 0.05, roof + 0.18), (1, 0, 0), 0.075, 0.02, segments=16),
               bevel=False)
    turret.add("Optics", box(gps.x + 0.22, gps.x + 0.23, gps.y + 0.04, gps.y + 0.13, roof + 0.13, roof + 0.23),
               bevel=False)
    # Commander's independent sight on a short mast.
    cits = Vector((-0.05, -0.30, roof))
    turret.add("Metal", cylinder(cits + Vector((0.0, 0.0, 0.08)), (0, 0, 1), 0.07, 0.16, segments=12))
    turret.add("Paint", box(cits.x - 0.16, cits.x + 0.16, cits.y - 0.15, cits.y + 0.15, roof + 0.16, roof + 0.44))
    turret.add("Optics", cylinder((cits.x + 0.165, cits.y, roof + 0.30), (1, 0, 0), 0.085, 0.02, segments=16),
               bevel=False)
    turret.add("Dark", box(cits.x + 0.14, cits.x + 0.19, cits.y - 0.14, cits.y + 0.14, roof + 0.40, roof + 0.44))
    # Vision blocks round the commander's cupola, loader's hatch on the left.
    for i in range(6):
        t = 2.0 * math.pi * i / 6 + 0.3
        p = CUPOLA + Vector((math.cos(t) * 0.40, math.sin(t) * 0.40, 0.09))
        turret.add("Optics", oriented_box(p, Vector((-math.sin(t), math.cos(t), 0.0)), Vector((0.0, 0.0, 1.0)),
                                          Vector((math.cos(t), math.sin(t), 0.0)), 0.06, 0.025, 0.012), bevel=False)
    turret.add("Paint", cylinder((-0.65, 0.62, roof + 0.03), (0, 0, 1), 0.32, 0.06, segments=20))
    turret.add("Metal", box(-0.75, -0.55, 0.60, 0.64, roof + 0.06, roof + 0.10))
    # Roof bolt rows.
    for side in (1, -1):
        for i in range(8):
            x = -2.15 + i * 0.42
            turret.add("Metal", cylinder((x, side * 1.10, roof + 0.012), (0, 0, 1), 0.022, 0.024, segments=6),
                       bevel=False)


def build_gun(turret):
    gun = Part("M20_Gun")
    mantlet = [(TRUNNION.x - 0.10, -0.40), (MANTLET_FRONT, -0.32), (MANTLET_FRONT, 0.32), (TRUNNION.x - 0.10, 0.40)]
    mantlet_top = [(TRUNNION.x - 0.10, -0.38), (MANTLET_FRONT - 0.06, -0.30), (MANTLET_FRONT - 0.06, 0.30),
                   (TRUNNION.x - 0.10, 0.38)]
    gun.add("Paint", plan_prism(mantlet, TRUNNION.z - 0.24, TRUNNION.z + 0.22, mantlet_top))
    axis = (1, 0, 0)
    z = TRUNNION.z
    barrel_length = MUZZLE_X - MANTLET_FRONT
    gun.add("Metal", tube((MANTLET_FRONT + barrel_length / 2.0, 0.0, z), axis, BORE_RADIUS, BARREL_RADIUS,
                          barrel_length, segments=24))
    # Thermal sleeve in sections, with clamp bands, and the fume extractor.
    sleeve_end = MUZZLE_X - 0.20
    fume0, fume1, fume_radius = FUME_EXTRACTOR
    sections = [(MANTLET_FRONT, fume0), (fume1, (fume1 + sleeve_end) / 2.0), ((fume1 + sleeve_end) / 2.0, sleeve_end)]
    for x0, x1 in sections:
        gun.add("Paint", cylinder(((x0 + x1) / 2.0, 0.0, z), axis, SLEEVE_RADIUS, x1 - x0 - 0.01, segments=24))
        if DETAIL:
            for x in (x0 + 0.03, x1 - 0.03):
                gun.add("Metal", cylinder((x, 0.0, z), axis, SLEEVE_RADIUS + 0.008, 0.035, segments=24), bevel=False)
    gun.add("Paint", cylinder(((fume0 + fume1) / 2.0, 0.0, z), axis, fume_radius, fume1 - fume0 - 0.16, segments=24))
    gun.add("Paint", cylinder((fume0 + 0.04, 0.0, z), axis, SLEEVE_RADIUS, 0.08, segments=24, radius_end=fume_radius))
    gun.add("Paint", cylinder((fume1 - 0.04, 0.0, z), axis, fume_radius, 0.08, segments=24, radius_end=SLEEVE_RADIUS))
    gun.add("Metal", tube((MUZZLE_X - 0.08, 0.0, z), axis, BORE_RADIUS, 0.105, 0.16, segments=24))
    if DETAIL:
        # Muzzle reference sensor and the coaxial machine gun port.
        gun.add("Dark", box(MUZZLE_X - 0.30, MUZZLE_X - 0.18, -0.03, 0.03, z + 0.09, z + 0.15))
        gun.add("Dark", cylinder((MANTLET_FRONT + 0.03, -0.24, z - 0.08), axis, 0.035, 0.08, segments=10))
        gun.add("Dark", cylinder((MANTLET_FRONT + 0.03, 0.24, z + 0.06), axis, 0.06, 0.06, segments=12))
        gun.add("Optics", cylinder((MANTLET_FRONT + 0.065, 0.24, z + 0.06), axis, 0.045, 0.01, segments=12),
                bevel=False)
        for y in (-0.22, 0.22):
            for zz in (-0.16, 0.14):
                gun.add("Metal", cylinder((MANTLET_FRONT + 0.012, y, z + zz), axis, 0.022, 0.024, segments=6),
                        bevel=False)
    return gun.build(TRUNNION, turret, TURRET_RING)


def build_rws(turret):
    rws = Part("M20_RWS")
    c = RWS_RING
    rws.add("Metal", cylinder(c + Vector((0.0, 0.0, 0.03)), (0, 0, 1), 0.22, 0.06, segments=20))
    rws.add("Paint", box(c.x - 0.20, c.x + 0.16, c.y - 0.16, c.y + 0.16, c.z + 0.06, c.z + 0.30))
    # Sensor head on the right of the cradle.
    rws.add("Paint", box(c.x - 0.04, c.x + 0.18, c.y - 0.34, c.y - 0.16, c.z + 0.12, c.z + 0.34))
    rws.add("Optics", cylinder((c.x + 0.185, c.y - 0.25, c.z + 0.23), (1, 0, 0), 0.055, 0.015, segments=14),
            bevel=False)
    # 12.7 mm machine gun on top, with its ammunition box on the left.
    gun_z = c.z + 0.38
    rws.add("Dark", box(c.x - 0.30, c.x + 0.18, c.y - 0.05, c.y + 0.05, c.z + 0.30, gun_z + 0.06))
    rws.add("Metal", cylinder((c.x + 0.18 + RWS_BARREL_LENGTH / 2.0, c.y, gun_z), (1, 0, 0), 0.022, RWS_BARREL_LENGTH,
                              segments=10))
    rws.add("Dark", cylinder((c.x + 0.18 + RWS_BARREL_LENGTH - 0.05, c.y, gun_z), (1, 0, 0), 0.032, 0.10, segments=10))
    rws.add("Paint", box(c.x - 0.18, c.x + 0.08, c.y + 0.16, c.y + 0.30, c.z + 0.10, c.z + 0.32))
    return rws.build(c, turret, TURRET_RING)


# ---- running gear ----

def build_road_wheel(name):
    """One road wheel at the origin, its axle along Y, hub on the +Y (outer) face: two rubber-tyred discs."""
    wheel = Part(name)
    r = ROAD_WHEEL_RADIUS
    for y in (-0.14, 0.14):
        wheel.add("Rubber", tube((0.0, y, 0.0), (0, 1, 0), r - 0.06, r, 0.20, segments=20), bevel=False)
        wheel.add("Paint", cylinder((0.0, y, 0.0), (0, 1, 0), r - 0.06, 0.16, segments=20), bevel=False)
        wheel.add("Paint", cylinder((0.0, y + (0.09 if y > 0 else -0.09), 0.0), (0, 1, 0), r - 0.14, 0.02, segments=16), bevel=False)
    wheel.add("Metal", cylinder((0.0, 0.27, 0.0), (0, 1, 0), 0.11, 0.06, segments=16))
    if DETAIL:
        for i in range(6):
            t = 2.0 * math.pi * i / 6
            wheel.add("Metal", cylinder((math.cos(t) * 0.075, 0.305, math.sin(t) * 0.075), (0, 1, 0), 0.014, 0.02,
                                        segments=6), bevel=False)
    return wheel.build((0.0, 0.0, 0.0))


def build_sprocket(name):
    sprocket = Part(name)
    root = SPROCKET[2]
    for y in (-0.13, 0.13):
        sprocket.add("Metal", gear((0.0, y, 0.0), (0, 1, 0), root, root + 0.06, 11, 0.07), bevel=False)
    sprocket.add("Paint", cylinder((0.0, 0.0, 0.0), (0, 1, 0), root - 0.06, 0.30, segments=24))
    sprocket.add("Metal", cylinder((0.0, 0.19, 0.0), (0, 1, 0), 0.13, 0.08, segments=16))
    sprocket.add("Paint", cylinder((0.0, 0.25, 0.0), (0, 1, 0), 0.07, 0.04, segments=12))
    return sprocket.build((0.0, 0.0, 0.0))


def build_idler(name):
    idler = Part(name)
    r = IDLER[2]
    for y in (-0.14, 0.14):
        idler.add("Metal", tube((0.0, y, 0.0), (0, 1, 0), r - 0.05, r, 0.20, segments=20), bevel=False)
        idler.add("Paint", cylinder((0.0, y, 0.0), (0, 1, 0), r - 0.05, 0.14, segments=20), bevel=False)
    idler.add("Metal", cylinder((0.0, 0.26, 0.0), (0, 1, 0), 0.10, 0.06, segments=16))
    return idler.build((0.0, 0.0, 0.0))


def track_path():
    """The track's centre line in (x, z): the convex hull around every wheel, counter-clockwise."""
    half = TRACK_THICKNESS / 2.0
    circles = [(x, ROAD_WHEEL_Z, ROAD_WHEEL_RADIUS + half) for x in ROAD_WHEEL_X]
    circles += [(SPROCKET[0], SPROCKET[1], SPROCKET[2] + 0.03 + half), (IDLER[0], IDLER[1], IDLER[2] + half)]
    circles += [(x, RETURN_ROLLER_Z, RETURN_ROLLER_RADIUS + half) for x in RETURN_ROLLER_X]
    points = sorted({(round(cx + math.cos(t) * r, 6), round(cz + math.sin(t) * r, 6))
                     for cx, cz, r in circles for t in (2.0 * math.pi * i / 48 for i in range(48))})

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in points:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0.0:
            lower.pop()
        lower.append(p)
    for p in reversed(points):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0.0:
            upper.pop()
        upper.append(p)
    return [Vector(p) for p in lower[:-1] + upper[:-1]]


def build_track(name, side):
    track = Part(name)
    path = track_path()
    count = len(path)
    lengths = [(path[(i + 1) % count] - path[i]).length for i in range(count)]
    total = sum(lengths)
    links = round(total / TRACK_PITCH)
    pitch = total / links
    y = side * TRACK_Y
    v = Vector((0.0, 1.0, 0.0))
    segment, start = 0, 0.0
    for i in range(links):
        s = (i + 0.5) * pitch
        while s > start + lengths[segment]:
            start += lengths[segment]
            segment += 1
        a, b = path[segment], path[(segment + 1) % count]
        direction = (b - a).normalized()
        point = a + direction * (s - start)
        u = Vector((direction.x, 0.0, direction.y))
        w = Vector((direction.y, 0.0, -direction.x))  # outward of a counter-clockwise loop
        centre = Vector((point.x, y, point.y))
        # Shoe, grouser bar, end connectors and the guide horn running between the wheel halves.
        track.add("Track", oriented_box(centre, u, v, w, pitch * 0.47, TRACK_HALF_WIDTH * 0.9, TRACK_THICKNESS * 0.32),
                  bevel=False)
        track.add("Track", oriented_box(centre + w * TRACK_THICKNESS * 0.38, u, v, w, 0.035, TRACK_HALF_WIDTH * 0.84,
                                        0.018), bevel=False)
        for end in (1, -1):
            track.add("Metal", oriented_box(centre + v * end * TRACK_HALF_WIDTH * 0.95, u, v, w, pitch * 0.5, 0.03,
                                            TRACK_THICKNESS * 0.42), bevel=False)
        track.add("Metal", oriented_box(centre - w * 0.08, u, v, w, 0.035, 0.03, 0.05), bevel=False)
    return track.build((0.0, y, 0.0))


# ---- materials ----

def worn_paint(name, dark, light, worn, roughness=0.72):
    """Paint mottled by noise, grimed in crevices, worn to bare metal along edges."""
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    tree = material.node_tree
    nodes, links = tree.nodes, tree.links
    shader = nodes["Principled BSDF"]
    coords = nodes.new("ShaderNodeTexCoord")

    def noise(scale, detail):
        node = nodes.new("ShaderNodeTexNoise")
        node.inputs["Scale"].default_value = scale
        node.inputs["Detail"].default_value = detail
        links.new(coords.outputs["Object"], node.inputs["Vector"])
        return node

    def map_range(source, from_min, from_max, to_min, to_max):
        node = nodes.new("ShaderNodeMapRange")
        node.inputs["From Min"].default_value = from_min
        node.inputs["From Max"].default_value = from_max
        node.inputs["To Min"].default_value = to_min
        node.inputs["To Max"].default_value = to_max
        links.new(source, node.inputs["Value"])
        return node.outputs["Result"]

    def mix(a, b, factor):
        node = nodes.new("ShaderNodeMix")
        node.data_type = "RGBA"
        links.new(factor, node.inputs["Factor"])
        for socket, value in (("A", a), ("B", b)):
            if isinstance(value, tuple):
                node.inputs[socket].default_value = value
            else:
                links.new(value, node.inputs[socket])
        return node.outputs["Result"]

    mottle = nodes.new("ShaderNodeValToRGB")
    mottle.color_ramp.elements[0].color = dark
    mottle.color_ramp.elements[1].color = light
    links.new(noise(1.8, 6.0).outputs["Fac"], mottle.inputs["Fac"])
    occlusion = nodes.new("ShaderNodeAmbientOcclusion")
    occlusion.inputs["Distance"].default_value = 0.35
    grime = map_range(occlusion.outputs["AO"], 0.35, 1.0, 0.75, 0.0)
    colour = mix(mottle.outputs["Color"], (0.035, 0.028, 0.018, 1.0), grime)
    # Edges: where the rounded (bevel) normal leaves the true normal.
    bevel = nodes.new("ShaderNodeBevel")
    bevel.inputs["Radius"].default_value = 0.012
    geometry = nodes.new("ShaderNodeNewGeometry")
    dot = nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    links.new(bevel.outputs["Normal"], dot.inputs[0])
    links.new(geometry.outputs["Normal"], dot.inputs[1])
    edge = map_range(dot.outputs["Value"], 0.90, 0.99, 1.0, 0.0)
    chips = map_range(noise(22.0, 4.0).outputs["Fac"], 0.42, 0.58, 0.0, 1.0)
    wear = nodes.new("ShaderNodeMath")
    wear.operation = "MULTIPLY"
    wear.use_clamp = True
    links.new(edge, wear.inputs[0])
    links.new(chips, wear.inputs[1])
    links.new(mix(colour, worn, wear.outputs["Value"]), shader.inputs["Base Color"])
    links.new(map_range(wear.outputs["Value"], 0.0, 1.0, roughness, 0.38), shader.inputs["Roughness"])
    links.new(map_range(wear.outputs["Value"], 0.0, 1.0, 0.0, 0.7), shader.inputs["Metallic"])
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.06
    bump.inputs["Distance"].default_value = 0.002
    links.new(noise(70.0, 3.0).outputs["Fac"], bump.inputs["Height"])
    links.new(bevel.outputs["Normal"], bump.inputs["Normal"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    return material


def plain(name, colour, roughness, metallic=0.0, emission=None, strength=0.0):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    shader = material.node_tree.nodes["Principled BSDF"]
    shader.inputs["Base Color"].default_value = colour
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Metallic"].default_value = metallic
    if emission:
        shader.inputs["Emission Color"].default_value = emission
        shader.inputs["Emission Strength"].default_value = strength
    return material


def make_materials():
    worn_steel = (0.30, 0.29, 0.27, 1.0)
    worn_paint("Paint", (0.20, 0.145, 0.075, 1.0), (0.33, 0.25, 0.14, 1.0), worn_steel)
    worn_paint("Blue", (0.012, 0.035, 0.20, 1.0), (0.02, 0.07, 0.36, 1.0), worn_steel, roughness=0.6)
    plain("Metal", (0.36, 0.34, 0.31, 1.0), 0.45, metallic=0.9)
    plain("Rubber", (0.018, 0.018, 0.017, 1.0), 0.88)
    plain("Track", (0.07, 0.062, 0.052, 1.0), 0.62, metallic=0.75)
    plain("Dark", (0.025, 0.025, 0.022, 1.0), 0.6, metallic=0.3)
    plain("Optics", (0.005, 0.02, 0.05, 1.0), 0.05, metallic=0.2, emission=(0.1, 0.35, 0.8, 1.0), strength=0.6)
    plain("LightWhite", (0.9, 0.85, 0.7, 1.0), 0.2, emission=(1.0, 0.85, 0.55, 1.0), strength=3.0)
    plain("LightRed", (0.5, 0.02, 0.02, 1.0), 0.2, emission=(1.0, 0.05, 0.03, 1.0), strength=3.0)


# ---- assembly, renders, export ----

def socket(name, parent, location):
    empty = bpy.data.objects.new(name, None)
    empty.empty_display_type = "ARROWS"
    empty.empty_display_size = 0.2
    bpy.context.scene.collection.objects.link(empty)
    empty.parent = parent
    empty.location = location
    return empty


def build_tank():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    make_materials()
    parts = {"hull": build_hull()}
    turret = build_turret()
    turret.parent = parts["hull"]
    parts["turret"] = turret
    parts["gun"] = build_gun(turret)
    parts["rws"] = build_rws(turret)
    socket("Muzzle_Socket", parts["gun"], (MUZZLE_X - TRUNNION.x, 0.0, 0.0))
    socket("Coax_Socket", parts["gun"], (MANTLET_FRONT + 0.07 - TRUNNION.x, -0.24, -0.08))
    socket("RWS_Muzzle_Socket", parts["rws"], (0.18 + RWS_BARREL_LENGTH, 0.0, 0.38))
    # One mesh per wheel kind, shared by every instance; the right side's turn half round so the hub faces out.
    meshes = {"RoadWheel": build_road_wheel("M20_RoadWheel").data, "Sprocket": build_sprocket("M20_Sprocket").data,
              "Idler": build_idler("M20_Idler").data}
    for obj in [o for o in bpy.data.objects if o.name in ("M20_RoadWheel", "M20_Sprocket", "M20_Idler")]:
        bpy.data.objects.remove(obj)
    wheels = []
    for side, letter in ((1, "L"), (-1, "R")):
        places = [(f"M20_RoadWheel_{letter}{i + 1}", "RoadWheel", x, ROAD_WHEEL_Z) for i, x in enumerate(ROAD_WHEEL_X)]
        places += [(f"M20_Sprocket_{letter}", "Sprocket", SPROCKET[0], SPROCKET[1]),
                   (f"M20_Idler_{letter}", "Idler", IDLER[0], IDLER[1])]
        for name, kind, x, z in places:
            obj = bpy.data.objects.new(name, meshes[kind])
            scene.collection.objects.link(obj)
            add_modifiers(obj)
            obj.parent = parts["hull"]
            obj.location = (x, side * TRACK_Y, z)
            obj.rotation_euler = (0.0, 0.0, 0.0 if side > 0 else math.pi)
            wheels.append(obj)
        track = build_track(f"M20_Track_{letter}", side)
        track.parent = parts["hull"]
        parts[f"track_{letter}"] = track
    parts["wheels"] = wheels
    unwrap([o for o in scene.objects if o.type == "MESH"])
    return parts


def unwrap(objects):
    done = set()
    for obj in objects:
        if obj.data.name in done:
            continue
        done.add(obj.data.name)
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.003)
        bpy.ops.object.mode_set(mode="OBJECT")


def evaluated_stats():
    """World-space bounds of every mesh with modifiers, and triangles per object."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    low = Vector((1e9, 1e9, 1e9))
    high = -low
    triangles = {}
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH" or obj.name.startswith("Ground"):
            continue
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        mesh.calc_loop_triangles()
        triangles[obj.name] = len(mesh.loop_triangles)
        for vertex in mesh.vertices:
            p = obj.matrix_world @ vertex.co
            low = Vector(min(a, b) for a, b in zip(low, p))
            high = Vector(max(a, b) for a, b in zip(high, p))
        evaluated.to_mesh_clear()
    return low, high, triangles


def setup_render():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.045, 0.06, 0.085, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.2
    scene.world = world
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 4.5
    sun.data.angle = math.radians(3.0)
    sun.rotation_euler = (math.radians(50.0), math.radians(10.0), math.radians(-35.0))
    scene.collection.objects.link(sun)
    fill = bpy.data.objects.new("Fill", bpy.data.lights.new("Fill", "AREA"))
    fill.data.energy = 1500.0
    fill.data.size = 8.0
    fill.location = (-6.0, 8.0, 6.0)
    fill.rotation_euler = (Vector((0.0, 0.0, 1.0)) - fill.location).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(fill)
    ground_mesh = bpy.data.meshes.new("Ground")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=40.0)
    bm.to_mesh(ground_mesh)
    bm.free()
    ground = bpy.data.objects.new("Ground", ground_mesh)
    ground.data.materials.append(plain("Ground", (0.09, 0.09, 0.085, 1.0), 0.9))
    scene.collection.objects.link(ground)
    camera = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    return camera


def render(camera, file_name, location, target, lens=50.0, ortho=None):
    scene = bpy.context.scene
    camera.location = location
    camera.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO" if ortho else "PERSP"
    if ortho:
        camera.data.ortho_scale = ortho
    camera.data.lens = lens
    camera.data.clip_end = 200.0
    scene.render.filepath = os.path.join(RENDER_DIR, file_name)
    bpy.ops.render.render(write_still=True)


def render_views(parts, prefix):
    os.makedirs(RENDER_DIR, exist_ok=True)
    camera = setup_render()
    centre = (0.55, 0.0, 1.1)
    render(camera, f"{prefix}front34.png", (10.5, -8.0, 4.6), (0.4, 0.0, 0.95), lens=55.0)
    render(camera, f"{prefix}side.png", (0.6, -30.0, 1.3), (0.6, 0.0, 1.3), ortho=9.2)
    render(camera, f"{prefix}top.png", (0.6, 0.0, 30.0), (0.6, 0.0001, 0.0), ortho=9.2)
    render(camera, f"{prefix}front.png", (30.0, 0.0, 1.3), (0.0, 0.0, 1.3), ortho=4.6)
    if DETAIL:
        # Pivot check: turret 45 degrees left, gun 10 degrees up, RWS turned to the right.
        parts["turret"].rotation_euler = (0.0, 0.0, math.radians(45.0))
        parts["gun"].rotation_euler = (0.0, math.radians(-10.0), 0.0)
        parts["rws"].rotation_euler = (0.0, 0.0, math.radians(-90.0))
        render(camera, f"{prefix}pivot_test.png", (10.5, -8.0, 4.6), centre, lens=50.0)
        for key in ("turret", "gun", "rws"):
            parts[key].rotation_euler = (0.0, 0.0, 0.0)
    for name in ("Sun", "Fill", "Ground", "Camera"):
        bpy.data.objects.remove(bpy.data.objects[name])


def unreal_cm(vector):
    """Blender metres to Unreal centimetres; the FBX import mirrors Y."""
    return [round(vector.x * 100.0, 2), round(-vector.y * 100.0, 2), round(vector.z * 100.0, 2)]


def write_layout(parts):
    muzzle = Vector((MUZZLE_X, 0.0, TRUNNION.z))
    layout = {
        "turret_offset": unreal_cm(TURRET_RING),
        "muzzle_offset": unreal_cm(muzzle - TURRET_RING),
        "gun_pivot_offset": unreal_cm(TRUNNION - TURRET_RING),
        "gun_muzzle_offset": unreal_cm(muzzle - TRUNNION),
        "rws_offset": unreal_cm(RWS_RING - TURRET_RING),
        "rws_muzzle_offset": unreal_cm(Vector((0.18 + RWS_BARREL_LENGTH, 0.0, 0.38))),
        "wheels": {obj.name: unreal_cm(obj.location) for obj in parts["wheels"]},
        "track_offsets": {f"M20_Track_{s}": unreal_cm(parts[f"track_{s}"].location) for s in ("L", "R")},
        "blender_ring": list(TURRET_RING),
    }
    with open(os.path.join(OUT_DIR, "m20_guardian.json"), "w") as file:
        json.dump(layout, file, indent=2)


EXPORTS = (("M20_Hull", "SM_M20_Hull"), ("M20_Turret", "SM_M20_Turret"), ("M20_Gun", "SM_M20_Gun"),
           ("M20_RWS", "SM_M20_RWS"), ("M20_Track_L", "SM_M20_Track_L"), ("M20_Track_R", "SM_M20_Track_R"),
           ("M20_RoadWheel_L1", "SM_M20_RoadWheel"), ("M20_Sprocket_L", "SM_M20_Sprocket"),
           ("M20_Idler_L", "SM_M20_Idler"))


def export_fbx():
    """Each part on its own, at its pivot: it leaves its parent and goes to the origin for the export, then goes back."""
    os.makedirs(FBX_DIR, exist_ok=True)
    for object_name, file_name in EXPORTS:
        obj = bpy.data.objects[object_name]
        saved = (obj.parent, obj.location.copy(), obj.rotation_euler.copy())
        obj.parent = None
        obj.location = (0.0, 0.0, 0.0)
        obj.rotation_euler = (0.0, 0.0, 0.0)
        bpy.context.view_layer.update()
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.fbx(filepath=os.path.join(FBX_DIR, f"{file_name}.fbx"), use_selection=True,
                                 object_types={"MESH"}, use_mesh_modifiers=True, mesh_smooth_type="FACE",
                                 use_tspace=False, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
                                 axis_forward="X", axis_up="Z", bake_space_transform=False, add_leaf_bones=False,
                                 bake_anim=False, use_custom_props=False, path_mode="AUTO")
        obj.parent, obj.location, obj.rotation_euler = saved
        bpy.context.view_layer.update()


def write_report(low, high, triangles):
    size = high - low
    groups = {
        "Hull": ["M20_Hull"], "Turret": ["M20_Turret"], "Gun": ["M20_Gun"], "RWS": ["M20_RWS"],
        "Tracks (2)": ["M20_Track_L", "M20_Track_R"],
        "Road wheels (12)": [n for n in triangles if n.startswith("M20_RoadWheel")],
        "Sprockets (2)": [n for n in triangles if n.startswith("M20_Sprocket")],
        "Idlers (2)": [n for n in triangles if n.startswith("M20_Idler")],
    }
    lines = ["# M20 Guardian prototype report", "",
             "Generated by `Tools/Art/m20_guardian_blender.py` (Blender 5.1).", "",
             "## Size (evaluated, with modifiers)", "",
             f"- Length {size.x:.2f} m (sheet: 7.8 m incl. gun), width {size.y:.2f} m (sheet: 3.4 m), "
             f"height {size.z:.2f} m (sheet: 2.6 m)",
             f"- Hull length {LOWER_HULL[3][0] - LOWER_HULL[0][0]:.2f} m, gun overhang beyond the hull "
             f"{MUZZLE_X - LOWER_HULL[3][0]:.2f} m", "",
             "## Triangles (evaluated, with bevels)", "", "| Part | Triangles | Materials |", "|---|---:|---|"]
    total = 0
    for label, names in groups.items():
        count = sum(triangles[n] for n in names)
        total += count
        materials = ", ".join(m.name for m in bpy.data.objects[names[0]].data.materials)
        lines.append(f"| {label} | {count:,} | {materials} |")
    lines += [f"| **Total** | **{total:,}** | |", ""]
    with open(os.path.join(OUT_DIR, "report_stats.md"), "w") as file:
        file.write("\n".join(lines))
    print("\n".join(lines))


def verify():
    """Re-imports each FBX into an empty scene: metres, origin at the pivot, the triangle count of the export."""
    expected = json.load(open(os.path.join(OUT_DIR, "export_triangles.json")))
    failures = []
    for _, file_name in EXPORTS:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=os.path.join(FBX_DIR, f"{file_name}.fbx"), axis_forward="X", axis_up="Z")
        obj = next(o for o in bpy.context.scene.objects if o.type == "MESH")
        mesh = obj.data
        mesh.calc_loop_triangles()
        points = [obj.matrix_world @ v.co for v in mesh.vertices]
        low = Vector([min(p[i] for p in points) for i in range(3)])
        high = Vector([max(p[i] for p in points) for i in range(3)])
        line = (f"{file_name}: {len(mesh.loop_triangles)} tris (expected {expected[file_name]}), "
                f"bounds {tuple(round(c, 3) for c in low)} .. {tuple(round(c, 3) for c in high)}, "
                f"location {tuple(round(c, 3) for c in obj.matrix_world.translation)}, "
                f"scale {tuple(round(c, 3) for c in obj.matrix_world.to_scale())}")
        print(line)
        if len(mesh.loop_triangles) != expected[file_name] or obj.matrix_world.translation.length > 1e-4 or \
                abs(obj.matrix_world.to_scale().x - 1.0) > 1e-3:
            failures.append(file_name)
    print("verify:", "FAILED " + ", ".join(failures) if failures else "all FBX files match")


def main():
    if "--verify" in ARGS:
        verify()
        return
    parts = build_tank()
    low, high, triangles = evaluated_stats()
    print(f"bounds {tuple(round(c, 3) for c in low)} .. {tuple(round(c, 3) for c in high)}")
    print(f"triangles {sum(triangles.values())}", sorted(triangles.items(), key=lambda item: -item[1])[:8])
    if STAGE == 1:
        render_views(parts, "stage1_")
        return
    write_layout(parts)
    write_report(low, high, triangles)
    with open(os.path.join(OUT_DIR, "export_triangles.json"), "w") as file:
        json.dump({file_name: triangles[object_name] for object_name, file_name in EXPORTS}, file, indent=2)
    export_fbx()
    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)
    render_views(parts, "m20_")
    # The renders added and removed their lights and camera; save again so the .blend has none of them.
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)


main()
