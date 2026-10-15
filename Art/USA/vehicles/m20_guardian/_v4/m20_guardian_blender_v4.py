"""Builds the M20 Guardian main battle tank (original Lek River Studio work, after the blueprint sheet in
Art/USA/vehicles/m20_guardian) from code, as a modular hero model for close-zoom review and the game:

  blender -b --python Tools/Art/m20_guardian_blender.py -- [--stage 1|2] [--no-bake] [--preview] [--verify] [--samples N]

--stage 1 builds the blockout (hull, running gear, turret, gun) and renders it. --stage 2 (the default) adds the detail,
applies the bevels to the game meshes, lays out the UVs, bakes the texture sets from the high-poly twins, and writes the
.blend, one FBX per moving part, the textures, the renders, m20_guardian.json and report_stats.md, all under
Art/USA/vehicles/m20_guardian. --no-bake skips the UVs and bakes (the FBX then carry the procedural slots). --preview only
builds and renders quick views to renders/preview_*.png. --verify re-imports every FBX into an empty scene and checks
scale, origin and triangles.

Every dimension is in metres in the block below: tune proportions there, not in the meshes. The tank faces +X, +Y is its
left, its tracks sit on z = 0 and the origin is under the hull centre. Moving parts are separate objects with their pivot
as origin: Turret on the turret ring, Gun on the trunnions, RWS on its ring, each road wheel, idler and sprocket on its
axle, and each track. Each part also has a high-poly twin (<name>_High, hidden, in the M20_High collection): the same
mesh plus bolts, weld beads and other detail that only goes into the normal map.

Materials, baking and the render scene live in the sibling modules m20_guardian_materials.py, m20_guardian_bake.py and
m20_guardian_render.py.
"""

import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m20_guardian_bake as bake  # noqa: E402
import m20_guardian_materials as mats  # noqa: E402
import m20_guardian_render as presentation  # noqa: E402
import m20_guardian_validate as validation  # noqa: E402

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "USA", "vehicles", "m20_guardian")
BLEND = os.path.join(OUT_DIR, "source", "M20_Guardian_V4.blend")
FBX_DIR = os.path.join(OUT_DIR, "fbx")
RENDER_DIR = os.path.join(OUT_DIR, "renders")
TEXTURE_DIR = os.path.join(OUT_DIR, "textures")
PREVIOUS_FRONT34 = os.path.join(OUT_DIR, "_v3", "renders", "m20_front34.png")
CONCEPT = os.path.join(OUT_DIR, "M20 Guardian_ Technische Tankblauwdruk.png")

# ---- dimensions ----
HULL_ROOF = 1.40
# Side profiles (x, z): the lower hull between the tracks, and the upper hull over it and the tracks: a short vertical
# nose plate, the glacis, and a chamfered rear deck edge.
LOWER_HULL = [(-3.30, 0.52), (-3.05, 0.36), (2.70, 0.36), (3.38, 0.80), (3.38, 0.96), (3.00, 1.08), (-3.30, 1.08)]
UPPER_HULL = [(-3.30, 1.06), (3.00, 1.06), (3.40, 0.96), (3.40, 1.10), (2.30, 1.40), (-3.18, 1.40), (-3.30, 1.30)]
LOWER_HALF_WIDTH = 1.02
UPPER_HALF_WIDTH = 1.62
GLACIS = ((3.40, 1.10), (2.30, 1.40))

TRACK_Y = 1.32
TRACK_HALF_WIDTH = 0.30
TRACK_THICKNESS = 0.10
TRACK_PITCH = 0.16
ROAD_WHEEL_RADIUS = 0.34
ROAD_WHEEL_X = [-2.0 + 0.80 * i for i in range(6)]
ROAD_WHEEL_Z = ROAD_WHEEL_RADIUS + TRACK_THICKNESS
ROAD_ARM = (0.32, 0.62)  # the road arm's hull pivot: x ahead of the axle, z
SPROCKET = (-2.85, 0.62, 0.28)  # x, z, radius at the teeth roots
IDLER = (2.85, 0.58, 0.27)
RETURN_ROLLER_X = (-1.56, 0.0, 1.56)
RETURN_ROLLER_Z = 0.88
RETURN_ROLLER_RADIUS = 0.075

SKIRT_Y = 1.64
SKIRT_THICKNESS = 0.10
SKIRT_Z = (0.62, 1.36)
# Bolt-on skirt modules front to back by their x edges; the one starting at BLUE_SKIRT is the blue ID band.
SKIRT_EDGES = (-3.05, -2.05, -1.05, -0.05, 0.55, 1.00, 2.00, 3.05)
BLUE_SKIRT = 0.55

TURRET_RING = Vector((-0.35, 0.0, HULL_ROOF))
TURRET_RING_RADIUS = 1.0
TURRET_SKIRT_TOP = 1.50
SHELL_TOP = 2.04
TURRET_ROOF = 2.08
# Turret plan outlines (x, y) for the left half, front to back; the right half mirrors them.
TURRET_BASE = [(1.50, 0.40), (1.00, 1.36), (-1.25, 1.42), (-1.45, 1.30), (-2.35, 1.22)]
TURRET_TOP = [(1.28, 0.38), (0.82, 1.28), (-1.25, 1.34), (-1.45, 1.24), (-2.30, 1.16)]
TRUNNION = Vector((1.38, 0.0, 1.78))
MANTLET_FRONT = 1.80
MUZZLE_X = 4.50
BORE_RADIUS = 0.0525
SLEEVE_RADIUS = (0.118, 0.106)  # thermal sleeve at the mantlet and at the muzzle
SLEEVE_END = 4.28
SLEEVE_BANDS = (2.22, 2.78, 3.46, 3.88, 4.22)
FUME_EXTRACTOR = (2.90, 3.35, 0.15)

CUPOLA = Vector((-0.55, -0.70, TURRET_ROOF))
RWS_RING = CUPOLA + Vector((0.0, 0.0, 0.16))
RWS_BARREL_LENGTH = 1.10

BEVEL_WIDTH = 0.025  # main armour; small parts pass a bevel weight below 1
SMALL = 0.35
SHARP_ANGLE = math.radians(35.0)

# Material slots in the order every object lists them.
MATERIALS = ("Paint", "Olive", "Blue", "Metal", "Rubber", "Track", "Dark", "Optics", "LightWhite", "LightRed")
# Face components for the triangle report, in first-use order.
COMPONENTS = []

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
STAGE = int(ARGS[ARGS.index("--stage") + 1]) if "--stage" in ARGS else 2
DETAIL = STAGE >= 2
PREVIEW = "--preview" in ARGS
BAKE = DETAIL and not PREVIEW and "--no-bake" not in ARGS
SAMPLES = int(ARGS[ARGS.index("--samples") + 1]) if "--samples" in ARGS else None


# ---- geometry ----

class Part:
    """One object's geometry: the game mesh and, for its high-poly twin, extra detail. Each piece is tagged with its
    material, its component (for the report) and, if bevelled, a bevel weight on its hard edges."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.high = bmesh.new()
        for bm in (self.bm, self.high):
            bm.edges.layers.float.new("bevel_weight_edge")
            bm.faces.layers.int.new("component")
        # High-only detail (bolts, welds) is flagged so the materials keep it painted and wear it less.
        self.high.faces.layers.int.new("detail")
        self.zone(name.replace("M20_", ""))

    def zone(self, component):
        if component not in COMPONENTS:
            COMPONENTS.append(component)
        self.component = COMPONENTS.index(component)

    def add(self, material, build, bevel=True, high=False):
        """bevel: True for the full width, a weight (0..1) for a narrower bevel, False for none. high: detail that only
        goes into the high-poly twin."""
        bm = self.high if high else self.bm
        weight = 1.0 if bevel is True else float(bevel or 0.0)
        if getattr(build, "round", False):
            weight = 0.0  # round parts carry their chamfers in their profile
        layer = bm.edges.layers.float["bevel_weight_edge"]
        component = bm.faces.layers.int["component"]
        detail = bm.faces.layers.int.get("detail")
        start = len(bm.faces)
        build(bm)
        bm.faces.ensure_lookup_table()
        faces = [bm.faces[i] for i in range(start, len(bm.faces))]
        bmesh.ops.recalc_face_normals(bm, faces=faces)
        index = MATERIALS.index(material)
        for face in faces:
            face.material_index = index
            face.smooth = True
            face[component] = self.component
            if detail is not None:
                face[detail] = 1
            if weight:
                for edge in face.edges:
                    if edge.is_manifold and edge.calc_face_angle(0.0) > math.radians(30.0):
                        edge[layer] = weight

    def build(self, pivot, parent=None, parent_pivot=None):
        """The game object, its origin at pivot (world space), with only the material slots it uses, and its high-poly
        twin parented to it."""
        offset = -Vector(pivot)
        for bm in (self.bm, self.high):
            bmesh.ops.translate(bm, verts=bm.verts, vec=offset)
        twin = self.bm.copy()
        extra = bpy.data.meshes.new("extra")
        self.high.to_mesh(extra)
        twin.from_mesh(extra)
        bpy.data.meshes.remove(extra)
        obj = make_object(self.name, self.bm)
        high = make_object(f"{self.name}_High", twin, collection=high_collection())
        for bm in (self.bm, self.high, twin):
            bm.free()
        high.parent = obj
        high.hide_render = True
        obj.parent = parent
        obj.location = Vector(pivot) - Vector(parent_pivot) if parent else Vector(pivot)
        return obj


def high_collection():
    collection = bpy.data.collections.get("M20_High")
    if not collection:
        collection = bpy.data.collections.new("M20_High")
        bpy.context.scene.collection.children.link(collection)
    return collection


def make_object(name, bm, collection=None):
    used = sorted({face.material_index for face in bm.faces})
    for face in bm.faces:
        face.material_index = used.index(face.material_index)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    for index in used:
        mesh.materials.append(bpy.data.materials[MATERIALS[index]])
    mesh.set_sharp_from_angle(angle=SHARP_ANGLE)
    obj = bpy.data.objects.new(name, mesh)
    (collection or bpy.context.scene.collection).objects.link(obj)
    add_modifiers(obj)
    return obj


def add_modifiers(obj):
    bevel = obj.modifiers.new("Bevel", "BEVEL")
    bevel.limit_method = "WEIGHT"
    bevel.width = BEVEL_WIDTH
    bevel.segments = 2
    bevel.profile = 0.5
    bevel.harden_normals = True
    bevel.use_clamp_overlap = True
    weighted = obj.modifiers.new("WeightedNormal", "WEIGHTED_NORMAL")
    weighted.keep_sharp = True
    weighted.use_face_influence = True
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


def rect(x0, x1, y0, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def oriented_box(centre, u, v, w, hu, hv, hw):
    c = Vector(centre)

    def face(s):
        return [c + u * a * hu + v * b * hv + w * s * hw for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    return loft(face(-1), face(1))


def tapered_box(centre, u, v, w, base, top, hw):
    """A box whose -w face is base (hu, hv) and +w face top (hu, hv): wedges, horns, hoods."""
    c = Vector(centre)

    def face(s, size):
        return [c + u * a * size[0] + v * b * size[1] + w * s * hw for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    return loft(face(-1, base), face(1, top))


def beam(start, end, up, half_width, half_height, overhang=0.0):
    """A rectangular bar from start to end, its height along up."""
    a, b = Vector(start), Vector(end)
    u = (b - a).normalized()
    w = (Vector(up) - u * u.dot(Vector(up))).normalized()
    return oriented_box((a + b) / 2.0, u, u.cross(w).normalized(), w, (b - a).length / 2.0 + overhang, half_width,
                        half_height)


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


def round_part(build):
    build.round = True
    return build


def cylinder(centre, axis, radius, length, segments=16, radius_end=None):
    return round_part(loft(ring(centre, axis, -length / 2.0, radius, segments),
                           ring(centre, axis, length / 2.0, radius if radius_end is None else radius_end, segments)))


def tube(centre, axis, inner, outer, length, segments=24):
    """A hollow cylinder: rim, bezel, muzzle."""
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
    return round_part(build)


def lathe(centre, axis, profile, segments=24, closed=True, phase=0.0):
    """A solid of revolution: profile is a list of (offset along axis, radius). closed: the profile is a closed outline
    (a ring with a hole, like a tyre or a bored barrel); otherwise the first and last rings are capped."""
    def build(bm):
        rings = [[bm.verts.new(p) for p in ring(centre, axis, offset, radius, segments, phase)]
                 for offset, radius in profile]
        count = len(rings)
        for k in range(count if closed else count - 1):
            r0, r1 = rings[k], rings[(k + 1) % count]
            for i in range(segments):
                j = (i + 1) % segments
                bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
        if not closed:
            bm.faces.new(list(reversed(rings[0])))
            bm.faces.new(rings[-1])
    return round_part(build)


def torus(centre, axis, radius, thickness, segments=12, sides=6):
    return lathe(centre, axis, [(math.sin(t) * thickness, radius + math.cos(t) * thickness)
                                for t in (2.0 * math.pi * i / sides for i in range(sides))], segments)


def polyline_tube(points, radius, segments=6):
    """A round bar along a polyline: rails, handles, cables, guards. The frame is carried along without twisting."""
    pts = [Vector(p) for p in points]

    def build(bm):
        tangents = [(pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized() for i in range(len(pts))]
        _, u, v = axis_frame(tangents[0])
        previous = tangents[0]
        rings = []
        for p, t in zip(pts, tangents):
            turn = previous.rotation_difference(t)
            u, v, previous = turn @ u, turn @ v, t
            rings.append([bm.verts.new(p + (u * math.cos(a) + v * math.sin(a)) * radius)
                          for a in (2.0 * math.pi * k / segments for k in range(segments))])
        for r0, r1 in zip(rings, rings[1:]):
            for i in range(segments):
                j = (i + 1) % segments
                bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])
    return round_part(build)


def rounded(points, radius, steps=3):
    """A polyline with its inner corners replaced by arcs of the given radius."""
    pts = [Vector(p) for p in points]
    out = [pts[0]]
    for a, b, c in zip(pts, pts[1:], pts[2:]):
        d0, d1 = (a - b).normalized(), (c - b).normalized()
        cut = min(radius, (a - b).length / 2.0, (c - b).length / 2.0)
        p0, p1 = b + d0 * cut, b + d1 * cut
        for i in range(steps + 1):
            t = i / steps
            out.append((1 - t) ** 2 * p0 + 2 * (1 - t) * t * b + t ** 2 * p1)
    out.append(pts[-1])
    return out


def gear(centre, axis, root, tip, teeth, length):
    """A sprocket ring: each tooth a rounded root, two flanks and a flat tip."""
    a, u, v = axis_frame(axis)
    shape = (root, root * 0.995, (root + tip) / 2.0, tip, tip, (root + tip) / 2.0)
    points = []
    for i in range(teeth * len(shape)):
        t = 2.0 * math.pi * i / (teeth * len(shape))
        points.append((u * math.cos(t) + v * math.sin(t)) * shape[i % len(shape)])
    c = Vector(centre)
    return round_part(loft([c - a * length / 2.0 + p for p in points], [c + a * length / 2.0 + p for p in points]))


def inset_outline(points, distance):
    """A closed 2D outline moved inwards by distance (mitred corners)."""
    pts = [Vector(p) for p in points]
    count = len(pts)
    area = sum(pts[i].x * pts[(i + 1) % count].y - pts[(i + 1) % count].x * pts[i].y for i in range(count))
    sign = 1.0 if area > 0.0 else -1.0
    out = []
    for i in range(count):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % count]
        e0, e1 = (p1 - p0).normalized(), (p2 - p1).normalized()
        n0 = Vector((-e0.y, e0.x)) * sign
        n1 = Vector((-e1.y, e1.x)) * sign
        bisector = (n0 + n1).normalized()
        out.append(tuple(p1 + bisector * distance / max(bisector.dot(n0), 0.3)))
    return out


class Quad:
    """A flat face given by its corners: b0-b1 along the bottom (u), t0-t1 above them (v). Panels and bolts go on it."""

    def __init__(self, b0, b1, t0, t1, inside):
        self.b0, self.b1, self.t0, self.t1 = (Vector(p) for p in (b0, b1, t0, t1))
        normal = (self.b1 - self.b0).cross(self.t0 - self.b0).normalized()
        centre = (self.b0 + self.b1 + self.t0 + self.t1) / 4.0
        self.normal = normal if normal.dot(centre - Vector(inside)) > 0.0 else -normal
        self.u_dir = (self.b1 - self.b0).normalized()
        self.v_dir = self.normal.cross(self.u_dir).normalized()
        if self.v_dir.dot(self.t0 - self.b0) < 0.0:
            self.v_dir = -self.v_dir
        self.u_len = (self.b1 - self.b0).length
        self.v_len = (self.t0 - self.b0).length

    def at(self, u, v, out=0.0):
        bottom = self.b0.lerp(self.b1, u)
        top = self.t0.lerp(self.t1, u)
        return bottom.lerp(top, v) + self.normal * out

    def panel(self, u0, u1, v0, v1, thickness, gap=0.004, taper=0.0):
        """A plate on the face; taper (m) pulls its outer face's edges in, for chamfered armour modules."""
        du, dv = taper / self.u_len, taper / self.v_len
        inner = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
        outer = [(u0 + du, v0 + dv), (u1 - du, v0 + dv), (u1 - du, v1 - dv), (u0 + du, v1 - dv)]
        return loft([self.at(u, v, gap) for u, v in inner], [self.at(u, v, gap + thickness) for u, v in outer])

    def block(self, u, v, hu, hv, out0, out1):
        """A box on the face at (u, v), hu and hv in metres, from out0 to out1 off the face."""
        return oriented_box(self.at(u, v, (out0 + out1) / 2.0), self.u_dir, self.v_dir, self.normal, hu, hv,
                            (out1 - out0) / 2.0)

    def bolt(self, u, v, out, radius=0.02, height=0.022):
        return cylinder(self.at(u, v, out + height / 2.0), self.normal, radius, height, segments=6)


def mirrored(points_xy):
    """A left-half outline (front to back) completed with its mirror into a closed counter-clockwise outline."""
    return list(points_xy) + [(x, -y) for x, y in reversed(points_xy)]


def bolt_row(part, quad, u0, u1, v, out, count, material="Paint"):
    """Bolts are painted over with the plate they hold (high-poly detail only)."""
    for i in range(count):
        part.add(material, quad.bolt(u0 + (u1 - u0) * i / max(1, count - 1), v, out), bevel=False, high=True)


def bolt_frame(part, quad, u0, u1, v0, v1, out, spacing=0.22, material="Paint"):
    count_u = max(2, round((u1 - u0) * quad.u_len / spacing) + 1)
    count_v = max(2, round((v1 - v0) * quad.v_len / spacing) + 1)
    for v in (v0, v1):
        bolt_row(part, quad, u0, u1, v, out, count_u, material)
    for u in (u0, u1):
        for i in range(1, count_v - 1):
            part.add(material, quad.bolt(u, v0 + (v1 - v0) * i / (count_v - 1), out), bevel=False, high=True)


def weld(part, points, radius=0.009):
    part.add("Paint", polyline_tube(points, radius, segments=6), bevel=False, high=True)


# ---- hull ----

def build_hull():
    hull = Part("M20_Hull")
    hull.zone("Hull body")
    hull.add("Paint", side_prism(LOWER_HULL, -LOWER_HALF_WIDTH, LOWER_HALF_WIDTH))
    hull.add("Paint", side_prism(UPPER_HULL, -UPPER_HALF_WIDTH, UPPER_HALF_WIDTH))
    hull.zone("Suspension")
    build_suspension(hull)
    hull.zone("Skirts")
    build_skirts(hull)
    if DETAIL:
        hull.zone("Glacis and nose")
        build_front(hull)
        hull.zone("Engine deck")
        build_deck(hull)
        hull.zone("Rear plate")
        build_rear(hull)
        hull.zone("Hull welds (high only)")
        build_hull_welds(hull)
    return hull.build((0.0, 0.0, 0.0))


def build_suspension(hull):
    """Road arms, hubs, bump stops and dampers, final drives, idler mounts and return rollers on their brackets."""
    for side in (1, -1):
        arm_y = side * 1.07
        for i, x in enumerate(ROAD_WHEEL_X):
            pivot = Vector((x + ROAD_ARM[0], arm_y, ROAD_ARM[1]))
            axle = Vector((x, arm_y, ROAD_WHEEL_Z))
            hull.add("Olive", cylinder((pivot.x, side * 1.04, pivot.z), (0, 1, 0), 0.095, 0.12, segments=10), SMALL)
            hull.add("Olive", beam(pivot, axle, (0, 0, 1), 0.035, 0.055, overhang=0.04), SMALL)
            hull.add("Metal", cylinder((x, side * 1.10, ROAD_WHEEL_Z), (0, 1, 0), 0.07, 0.10, segments=8), SMALL)
            if not DETAIL:
                continue
            hull.add("Dark", box(x + 0.08, x + 0.20, side * 1.02, side * 1.12, 0.74, 0.82), SMALL)
            hull.add("Olive", box(x + 0.06, x + 0.22, side * 1.02, side * 1.13, 0.82, 0.86), SMALL)
            if i in (0, 4, 5):
                low = pivot.lerp(axle, 0.45) + Vector((0.0, side * 0.02, 0.03))
                high = Vector((x + ROAD_ARM[0] + 0.32, side * 1.09, 0.98))
                hull.add("Metal", polyline_tube([low, low.lerp(high, 0.55)], 0.022, segments=8), SMALL)
                hull.add("Olive", polyline_tube([low.lerp(high, 0.4), high], 0.042, segments=8), SMALL)
        # Final drive housing behind the sprocket.
        hull.add("Olive", lathe((SPROCKET[0], side * 1.0, SPROCKET[1]), (0, side, 0),
                                [(-0.05, 0.08), (-0.05, 0.23), (0.06, 0.23), (0.10, 0.19), (0.13, 0.12),
                                 (0.13, 0.07)], segments=20, closed=False), SMALL)
        # Idler crank and its tensioner housing.
        crank = Vector((IDLER[0] - 0.24, side * 1.07, IDLER[1] + 0.14))
        hull.add("Olive", beam(crank, (IDLER[0], side * 1.07, IDLER[1]), (0, 0, 1), 0.04, 0.06, overhang=0.05), SMALL)
        hull.add("Metal", cylinder((IDLER[0], side * 1.10, IDLER[1]), (0, 1, 0), 0.065, 0.10, segments=12), SMALL)
        if DETAIL:
            hull.add("Olive", cylinder((IDLER[0] - 0.42, side * 1.07, 0.78), (1, 0, 0.35), 0.07, 0.34, segments=10),
                     SMALL)
        # Return rollers: rubber-tyred twin rollers on stub axles from brackets on the hull side.
        for x in RETURN_ROLLER_X:
            hull.add("Olive", box(x - 0.07, x + 0.07, side * 1.0, side * 1.08, RETURN_ROLLER_Z - 0.06,
                                  RETURN_ROLLER_Z + 0.16), SMALL)
            hull.add("Metal", cylinder((x, side * 1.20, RETURN_ROLLER_Z), (0, 1, 0), 0.035, 0.40, segments=8), SMALL)
            for offset in (-0.13, 0.13):
                hull.add("Rubber", lathe((x, side * (TRACK_Y + offset), RETURN_ROLLER_Z), (0, 1, 0),
                                         [(-0.08, 0.045), (-0.08, 0.068), (-0.07, RETURN_ROLLER_RADIUS),
                                          (0.07, RETURN_ROLLER_RADIUS), (0.08, 0.068), (0.08, 0.045)], segments=12),
                         SMALL)


def build_skirts(hull):
    """Bolt-on composite modules (10 cm) with raised cover plates, hung from a rail by brackets."""
    z0, z1 = SKIRT_Z
    for side in (1, -1):
        y0 = side * SKIRT_Y
        y1 = side * (SKIRT_Y + SKIRT_THICKNESS)
        for x0, x1 in zip(SKIRT_EDGES, SKIRT_EDGES[1:]):
            a, b = x0 + 0.012, x1 - 0.012
            front = x1 == SKIRT_EDGES[-1]
            profile = [(a, z0), (b - 0.30, z0), (b, z0 + 0.28), (b, z1), (a, z1)] if front else \
                [(a, z0), (b, z0), (b, z1), (a, z1)]
            material = "Blue" if x0 == BLUE_SKIRT else "Paint"
            hull.add(material, side_prism(profile, y0, y1))
            if not DETAIL:
                continue
            cover = inset_outline(profile, 0.075 if b - a > 0.6 else 0.06)
            hull.add(material, side_prism(cover, y1, y1 + side * 0.02), SMALL)
            face = Quad((a, y1, z0), (b, y1, z0), (a, y1, z1), (b, y1, z1), (0.0, 0.0, 1.0))
            count = max(2, round((b - a) / 0.22))
            for i in range(count):
                u = (0.05 + 0.9 * i / (count - 1))
                for v in (0.05, 0.95):
                    if front and v < 0.5 and u > 0.6:
                        continue
                    hull.add(material, face.bolt(u, v, 0.0), bevel=False, high=True)
            # Hanger bracket at the module's rear top corner, over the rail.
            hull.add("Olive", box(a + 0.05, a + 0.17, side * 1.56, side * (SKIRT_Y + 0.04), z1 - 0.06, z1 + 0.05),
                     SMALL)
        hull.add("Paint", polyline_tube([(SKIRT_EDGES[0] + 0.02, side * (SKIRT_Y + 0.035), z1 + 0.02),
                                         (SKIRT_EDGES[-1] - 0.02, side * (SKIRT_Y + 0.035), z1 + 0.02)], 0.03,
                                        segments=8), SMALL)
        if DETAIL:
            # A small blue ID patch near the rear of each skirt.
            patch_y = side * (SKIRT_Y + SKIRT_THICKNESS + 0.02)
            hull.add("Blue", side_prism([(-2.95, 1.02), (-2.62, 1.02), (-2.62, 1.26), (-2.95, 1.26)],
                                        patch_y, patch_y + side * 0.012), SMALL)
            # Front fender plate over the idler, and a rubber mud flap.
            hull.add("Paint", side_prism([(3.03, 1.00), (3.40, 1.00), (3.40, 1.10), (3.03, 1.36)],
                                         side * SKIRT_Y, side * (SKIRT_Y + 0.05)), SMALL)
            hull.add("Rubber", box(3.36, 3.39, side * 1.06, side * 1.60, 0.80, 0.97), SMALL)


def build_front(hull):
    (gx0, gz0), (gx1, gz1) = GLACIS
    glacis = Quad((gx0, -UPPER_HALF_WIDTH, gz0), (gx0, UPPER_HALF_WIDTH, gz0),
                  (gx1, -UPPER_HALF_WIDTH, gz1), (gx1, UPPER_HALF_WIDTH, gz1), (0.0, 0.0, 0.8))
    # Glacis add-on modules: chamfered blocks with a raised cover plate; the outer two blue.
    for u0, u1, material in ((0.015, 0.17, "Blue"), (0.19, 0.465, "Paint"), (0.535, 0.81, "Paint"),
                             (0.83, 0.985, "Blue")):
        hull.add(material, glacis.panel(u0, u1, 0.06, 0.88, 0.06, gap=0.008, taper=0.025))
        hull.add(material, glacis.panel(u0 + 0.025, u1 - 0.025, 0.2, 0.74, 0.016, gap=0.068), SMALL)
        bolt_frame(hull, glacis, u0 + 0.012, u1 - 0.012, 0.12, 0.82, 0.068, material=material)
    # Spine plate between the centre modules, with lifting eyes either side.
    hull.add("Paint", glacis.panel(0.475, 0.525, 0.08, 0.90, 0.03, gap=0.006), SMALL)
    for u in (0.44, 0.56):
        p = glacis.at(u, 0.93, 0.0)
        hull.add("Metal", torus(p + Vector((0.0, 0.0, 0.05)), (1, 0, 0), 0.04, 0.013), SMALL)
    # Nose: headlight clusters in guards, tow eyes on the lower plate, a lower add-on plate.
    for side in (1, -1):
        y = side * 1.36
        # Lamp cluster: a bolted base plate on the nose, the armoured housing, and a brow over the lamps.
        hull.add("Paint", box(3.40, 3.42, y - 0.20, y + 0.20, 0.94, 1.115), SMALL)
        plate = Quad((3.42, y - 0.20, 0.94), (3.42, y + 0.20, 0.94), (3.42, y - 0.20, 1.115), (3.42, y + 0.20, 1.115),
                     (0.0, y, 1.0))
        bolt_frame(hull, plate, 0.04, 0.96, 0.08, 0.92, 0.0, spacing=0.10)
        hull.add("Olive", plan_prism(rect(3.42, 3.49, y - 0.14, y + 0.14), 0.955, 1.095,
                                     rect(3.42, 3.48, y - 0.13, y + 0.13)), SMALL)
        hull.add("Olive", box(3.42, 3.52, y - 0.145, y + 0.145, 1.085, 1.10), SMALL)
        for dy in (-0.07, 0.07):
            hull.add("Dark", tube((3.495, y + dy, 1.025), (1, 0, 0), 0.04, 0.052, 0.02, segments=12), SMALL)
            hull.add("LightWhite", cylinder((3.49, y + dy, 1.025), (1, 0, 0), 0.042, 0.01, segments=12), bevel=False)
        guard = rounded([(3.40, y - 0.18, 1.11), (3.56, y - 0.18, 1.11), (3.56, y + 0.18, 1.11), (3.40, y + 0.18, 1.11)],
                        0.05)
        hull.add("Dark", polyline_tube(guard, 0.014), False)
        for dy in (-0.18, 0.18):
            hull.add("Dark", polyline_tube([(3.56, y + dy, 1.11), (3.56, y + dy, 0.95), (3.42, y + dy, 0.95)], 0.012),
                     False)
            for z in (0.95, 1.11):
                hull.add("Dark", box(3.42, 3.445, y + dy - 0.022, y + dy + 0.022, z - 0.022, z + 0.022), False)
        clevis(hull, Vector((3.40, side * 0.62, 0.70)), 1.0)
    belly = Quad((2.70, -LOWER_HALF_WIDTH, 0.36), (2.70, LOWER_HALF_WIDTH, 0.36),
                 (3.38, -LOWER_HALF_WIDTH, 0.80), (3.38, LOWER_HALF_WIDTH, 0.80), (0.0, 0.0, 0.8))
    hull.add("Paint", belly.panel(0.05, 0.95, 0.12, 0.88, 0.035, taper=0.015), SMALL)
    bolt_frame(hull, belly, 0.07, 0.93, 0.18, 0.82, 0.039, spacing=0.3)
    # Driver's hatch and periscopes in armoured hoods.
    hatch = Vector((1.95, 0.0, HULL_ROOF))
    hull.add("Paint", lathe(hatch, (0, 0, 1), [(0.0, 0.26), (0.0, 0.34), (0.04, 0.34), (0.05, 0.32), (0.05, 0.26)],
                            segments=24), SMALL)
    hull.add("Paint", lathe(hatch, (0, 0, 1), [(0.0, 0.25), (0.065, 0.25), (0.075, 0.23)], segments=24,
                            closed=False), SMALL)
    hull.add("Metal", box(hatch.x - 0.24, hatch.x - 0.12, -0.08, 0.08, HULL_ROOF + 0.02, HULL_ROOF + 0.10), SMALL)
    hull.add("Metal", polyline_tube(rounded([(hatch.x + 0.05, -0.08, HULL_ROOF + 0.075), (hatch.x + 0.05, -0.08,
                                             HULL_ROOF + 0.12), (hatch.x + 0.05, 0.08, HULL_ROOF + 0.12),
                                             (hatch.x + 0.05, 0.08, HULL_ROOF + 0.075)], 0.03), 0.012), False)
    # One welded hood strip over the three periscopes, with dividers between the windows.
    hull.add("Paint", plan_prism(rect(2.16, 2.38, -0.36, 0.36), HULL_ROOF - 0.01, HULL_ROOF + 0.025,
                                 rect(2.165, 2.375, -0.355, 0.355)), SMALL)
    hull.add("Paint", plan_prism([(2.18, -0.34), (2.36, -0.34), (2.36, 0.34), (2.18, 0.34)], HULL_ROOF + 0.02,
                                 HULL_ROOF + 0.10, [(2.18, -0.33), (2.30, -0.33), (2.30, 0.33), (2.18, 0.33)]), SMALL)
    hull.add("Paint", box(2.27, 2.40, -0.35, 0.35, HULL_ROOF + 0.095, HULL_ROOF + 0.11), SMALL)
    for y in (-0.24, 0.0, 0.24):
        hull.add("Optics", box(2.345, 2.355, y - 0.075, y + 0.075, HULL_ROOF + 0.035, HULL_ROOF + 0.08), bevel=False)
    weld(hull, [(2.16, -0.36, HULL_ROOF), (2.16, 0.36, HULL_ROOF)])


def build_deck(hull):
    """Engine deck behind the turret: framed grilles with louvres, an access plate, stowage and lifting eyes."""
    roof = HULL_ROOF
    for y0, y1 in ((-0.94, -0.06), (0.06, 0.94)):
        x0, x1 = -3.12, -1.98
        hull.add("Dark", box(x0 + 0.04, x1 - 0.04, y0 + 0.04, y1 - 0.04, roof, roof + 0.012), bevel=False)
        for bx0, bx1, by0, by1 in ((x0, x1, y0, y0 + 0.05), (x0, x1, y1 - 0.05, y1), (x0, x0 + 0.05, y0, y1),
                                   (x1 - 0.05, x1, y0, y1)):
            hull.add("Paint", box(bx0, bx1, by0, by1, roof, roof + 0.055), SMALL)
        for i in range(13):
            x = x0 + 0.11 + i * (x1 - x0 - 0.22) / 12
            hull.add("Dark", oriented_box((x, (y0 + y1) / 2.0, roof + 0.035), Vector((0.8, 0.0, 0.6)),
                                          Vector((0.0, 1.0, 0.0)), Vector((-0.6, 0.0, 0.8)), 0.035,
                                          (y1 - y0) / 2.0 - 0.05, 0.005), bevel=False)
        hull.add("Paint", polyline_tube(rounded([((x0 + x1) / 2 - 0.12, (y0 + y1) / 2, roof + 0.055),
                                                 ((x0 + x1) / 2 - 0.12, (y0 + y1) / 2, roof + 0.12),
                                                 ((x0 + x1) / 2 + 0.12, (y0 + y1) / 2, roof + 0.12),
                                                 ((x0 + x1) / 2 + 0.12, (y0 + y1) / 2, roof + 0.055)], 0.04), 0.013),
                 False)
    # Access plate ahead of the grilles.
    hull.add("Paint", box(-1.92, -1.40, -0.55, 0.55, roof, roof + 0.025), SMALL)
    deck = Quad((-1.92, -0.55, roof + 0.025), (-1.92, 0.55, roof + 0.025), (-1.40, -0.55, roof + 0.025),
                (-1.40, 0.55, roof + 0.025), (0.0, 0.0, 0.0))
    bolt_frame(hull, deck, 0.04, 0.96, 0.08, 0.92, 0.0, spacing=0.18)
    for side in (1, -1):
        # Tool box and two jerry cans on each rear deck corner.
        hull.add("Olive", plan_prism(rect(-3.25, -2.70, side * 1.15 - 0.22, side * 1.15 + 0.22), roof, roof + 0.24,
                                     rect(-3.24, -2.71, side * 1.15 - 0.21, side * 1.15 + 0.21)))
        hull.add("Olive", box(-3.255, -2.695, side * 1.15 - 0.225, side * 1.15 + 0.225, roof + 0.18, roof + 0.25),
                 SMALL)
        for x in (-3.10, -2.85):
            hull.add("Dark", box(x - 0.03, x + 0.03, side * 1.37 - 0.004, side * 1.37 + 0.02, roof + 0.14,
                                 roof + 0.21), bevel=False)
        for x in (-3.25, -2.70):
            for y in (side * 0.93, side * 1.37):
                hull.add("Dark", box(x - 0.02, x + 0.02, y - 0.03, y + 0.03, roof, roof + 0.07), False)
        for x in (-2.58, -2.40):
            jerry(hull, Vector((x, side * 1.33, roof)))
        # Jerry can rack: a base tray and an end stop.
        hull.add("Dark", box(-2.69, -2.29, side * 1.33 - 0.19, side * 1.33 + 0.19, roof, roof + 0.012), bevel=False)
        hull.add("Dark", box(-2.30, -2.28, side * 1.33 - 0.19, side * 1.33 + 0.19, roof, roof + 0.22), False)
        hull.add("Dark", polyline_tube([(-2.70, side * 1.48, roof + 0.18), (-2.30, side * 1.48, roof + 0.18)], 0.012),
                 False)
        # Lifting eyes at the deck corners and ahead of the turret.
        for x in (-3.05, 1.45):
            hull.add("Paint", plan_prism(rect(x - 0.09, x + 0.09, side * 1.50 - 0.06, side * 1.50 + 0.06),
                                         roof - 0.01, roof + 0.02, rect(x - 0.08, x + 0.08, side * 1.50 - 0.05,
                                                                        side * 1.50 + 0.05)), False)
            hull.add("Metal", box(x - 0.06, x + 0.06, side * 1.50 - 0.03, side * 1.50 + 0.03, roof, roof + 0.045),
                     SMALL)
            hull.add("Metal", torus((x, side * 1.50, roof + 0.085), (0, 1, 0), 0.045, 0.014), SMALL)
        # Bolts along the deck edges (normal map).
        for i in range(15):
            x = -3.10 + i * 0.38
            hull.add("Paint", cylinder((x, side * 1.55, roof + 0.011), (0, 0, 1), 0.02, 0.022, segments=6),
                     bevel=False, high=True)


def clevis(part, eye, forward):
    """A tow eye: two lug plates welded to the plate behind, a pin through them, and the shackle ring on the pin. eye is
    the pin; forward is +1 at the nose (the lugs reach back down the lower glacis) and -1 at the rear plate."""
    if forward > 0:
        profile = [(3.08, 0.60), (3.30, 0.57), (3.47, 0.65), (3.47, 0.75), (3.41, 0.81), (3.36, 0.80)]
    else:
        profile = [(-3.29, 0.58), (-3.40, 0.62), (-3.43, 0.67), (-3.40, 0.73), (-3.29, 0.77)]
    for dy in (-0.05, 0.05):
        part.add("Paint", side_prism(profile, eye.y + dy - 0.014, eye.y + dy + 0.014), SMALL)
    part.add("Metal", cylinder(eye, (0, 1, 0), 0.022, 0.14, segments=10), SMALL)
    for dy in (-0.072, 0.072):
        part.add("Metal", cylinder(eye + Vector((0.0, dy, 0.0)), (0, 1, 0), 0.032, 0.012, segments=10), SMALL)
    part.add("Metal", torus(eye + Vector((forward * 0.07, 0.0, -0.02)), (0, 1, 0), 0.06, 0.018, segments=12), SMALL)
    weld(part, [(profile[0][0], eye.y - 0.07, profile[0][1]), (profile[0][0], eye.y + 0.07, profile[0][1])])


def jerry(part, base):
    """A 20 l jerry can standing on base: chamfered body, recessed X, three-bar handle."""
    part.add("Paint", plan_prism(rect(base.x - 0.08, base.x + 0.08, base.y - 0.17, base.y + 0.17), base.z,
                                 base.z + 0.40, rect(base.x - 0.075, base.x + 0.075, base.y - 0.16, base.y + 0.16)),
             SMALL)
    for dy in (-0.05, 0.0, 0.05):
        part.add("Paint", polyline_tube(rounded([(base.x - 0.03, base.y + dy, base.z + 0.40),
                                                 (base.x - 0.03, base.y + dy, base.z + 0.45),
                                                 (base.x + 0.03, base.y + dy, base.z + 0.45),
                                                 (base.x + 0.03, base.y + dy, base.z + 0.40)], 0.02), 0.009), False)
    part.add("Dark", cylinder((base.x, base.y + 0.11, base.z + 0.42), (0, 0, 1), 0.025, 0.05, segments=8), False)
    for sx in (-1, 1):
        for dy, dz in ((0.10, 0.25), (-0.10, 0.25)):
            part.add("Paint", polyline_tube([(base.x + sx * 0.081, base.y - dy, base.z + 0.20 - 0.08),
                                             (base.x + sx * 0.081, base.y + dy, base.z + 0.20 + 0.08)], 0.008, 4),
                     False, high=True)


def build_rear(hull):
    """Rear plate: framed exhaust grille with angled louvres, side grilles, lamps in guards, towing pintle, cable."""
    x = -3.30
    louvre = (Vector((-0.6, 0.0, -0.8)).normalized(), Vector((0.0, 1.0, 0.0)), Vector((-0.8, 0.0, 0.6)).normalized())

    def grille(y_centre, half_width, z0, z1, count, depth):
        hull.add("Paint", box(x - depth, x, y_centre - half_width - 0.05, y_centre + half_width + 0.05, z0 - 0.05,
                              z0), SMALL)
        hull.add("Paint", box(x - depth, x, y_centre - half_width - 0.05, y_centre + half_width + 0.05, z1,
                              z1 + 0.05), SMALL)
        for y in (y_centre - half_width - 0.025, y_centre + half_width + 0.025):
            hull.add("Paint", box(x - depth, x, y - 0.025, y + 0.025, z0, z1), SMALL)
        hull.add("Dark", box(x - 0.01, x, y_centre - half_width, y_centre + half_width, z0, z1), bevel=False)
        for i in range(count):
            z = z0 + (z1 - z0) * (i + 0.5) / count
            hull.add("Dark", oriented_box((x - depth * 0.55, y_centre, z), *louvre, 0.004, half_width, 0.035),
                     bevel=False)

    grille(0.0, 0.50, 0.70, 1.24, 8, 0.10)
    for side in (1, -1):
        grille(side * 0.82, 0.16, 0.74, 1.20, 6, 0.07)
        # Tail lamps in a guard on the sponson corners.
        y = side * 1.45
        hull.add("Paint", box(x - 0.015, x, y - 0.15, y + 0.15, 1.13, 1.36), SMALL)
        hull.add("Olive", box(x - 0.06, x - 0.015, y - 0.10, y + 0.10, 1.16, 1.32), SMALL)
        hull.add("LightRed", box(x - 0.065, x - 0.06, y - 0.08, y - 0.01, 1.20, 1.29), bevel=False)
        hull.add("LightWhite", box(x - 0.065, x - 0.06, y + 0.01, y + 0.08, 1.20, 1.29), bevel=False)
        hull.add("Dark", polyline_tube(rounded([(x - 0.015, y - 0.13, 1.34), (x - 0.12, y - 0.13, 1.34),
                                                (x - 0.12, y + 0.13, 1.34), (x - 0.015, y + 0.13, 1.34)], 0.04),
                                       0.012), False)
        # Rear tow eyes and the cable clamps on the plate.
        clevis(hull, Vector((-3.38, side * 0.62, 0.67)), -1.0)
        for cy in (0.45, 0.95):
            hull.add("Dark", box(x - 0.06, x, side * cy - 0.03, side * cy + 0.03, 0.54, 0.64), False)
    # Towing pintle and the tow cable looped across the plate.
    hull.add("Paint", box(x - 0.02, x, -0.14, 0.14, 0.50, 0.70), SMALL)
    hull.add("Metal", box(x - 0.14, x - 0.02, -0.08, 0.08, 0.56, 0.64), SMALL)
    hull.add("Metal", cylinder((x - 0.10, 0.0, 0.66), (0, 0, 1), 0.018, 0.08, segments=8), SMALL)
    hull.add("Dark", polyline_tube(rounded([(x - 0.02, -0.98, 0.62), (x - 0.04, -0.98, 0.58), (x - 0.05, -0.4, 0.56),
                                            (x - 0.05, 0.4, 0.56), (x - 0.04, 0.98, 0.58), (x - 0.02, 0.98, 0.62)],
                                           0.12), 0.022, segments=8), False)


def build_hull_welds(hull):
    w = UPPER_HALF_WIDTH
    weld(hull, [(GLACIS[1][0], -w + 0.02, HULL_ROOF), (GLACIS[1][0], w - 0.02, HULL_ROOF)])
    weld(hull, [(GLACIS[0][0], -w + 0.02, GLACIS[0][1]), (GLACIS[0][0], w - 0.02, GLACIS[0][1])])
    for side in (1, -1):
        weld(hull, [(-3.16, side * w, HULL_ROOF), (2.28, side * w, HULL_ROOF)])
        weld(hull, [(3.38, side * w, 1.10), (2.32, side * w, 1.39)])
    weld(hull, [(-3.30, -w + 0.02, 1.30), (-3.30, w - 0.02, 1.30)])


# ---- turret ----

def build_turret():
    turret = Part("M20_Turret")
    turret.zone("Turret shell")
    base, top = mirrored(TURRET_BASE), mirrored(TURRET_TOP)
    turret.add("Paint", plan_prism(inset_outline(base, 0.07), HULL_ROOF + 0.02, TURRET_SKIRT_TOP))
    turret.add("Paint", plan_prism(base, TURRET_SKIRT_TOP, SHELL_TOP, top))
    turret.add("Paint", plan_prism(inset_outline(top, 0.04), SHELL_TOP, TURRET_ROOF, inset_outline(top, 0.07)), SMALL)
    turret.add("Dark", cylinder(TURRET_RING + Vector((0.0, 0.0, 0.03)), (0, 0, 1), TURRET_RING_RADIUS, 0.06,
                                segments=32), bevel=False)
    turret.zone("Cupola")
    turret.add("Paint", lathe(CUPOLA, (0, 0, 1), [(0.0, 0.30), (0.0, 0.47), (0.03, 0.47), (0.05, 0.45),
                                                  (0.12, 0.43), (0.16, 0.38), (0.16, 0.30)], segments=28), SMALL)
    if DETAIL:
        build_turret_armour(turret)
        turret.zone("Bustle and stowage")
        build_bustle(turret)
        turret.zone("Sights and roof")
        build_turret_roof(turret)
        turret.zone("Turret welds (high only)")
        weld(turret, [(x, y, SHELL_TOP) for x, y in top + top[:1]])
    return turret.build(TURRET_RING)


def turret_face(side, start, end):
    """The turret wall between outline points start and end on one side, as a Quad (u from start to end)."""
    def point(outline, i, z):
        x, y = outline[i]
        return (x, side * y, z)
    return Quad(point(TURRET_BASE, start, TURRET_SKIRT_TOP), point(TURRET_BASE, end, TURRET_SKIRT_TOP),
                point(TURRET_TOP, start, SHELL_TOP), point(TURRET_TOP, end, SHELL_TOP),
                (TURRET_RING.x, 0.0, 1.75))


def build_turret_armour(turret):
    turret.zone("Composite armour")
    for side in (1, -1):
        # Cheek: a composite wedge module standing off the shell on brackets, with bolt-on covers, the outer blue.
        cheek = turret_face(side, 0, 1)
        turret.add("Paint", cheek.panel(0.04, 0.97, 0.04, 0.96, 0.17, gap=0.035, taper=0.05))
        out = 0.035 + 0.17
        turret.add("Paint", cheek.panel(0.10, 0.52, 0.15, 0.85, 0.022, gap=out), SMALL)
        turret.add("Blue", cheek.panel(0.56, 0.91, 0.15, 0.85, 0.022, gap=out), SMALL)
        for u0, u1, material in ((0.10, 0.52, "Paint"), (0.56, 0.91, "Blue")):
            bolt_frame(turret, cheek, u0 + 0.02, u1 - 0.02, 0.2, 0.8, out + 0.022, spacing=0.18, material=material)
        for u in (0.2, 0.5, 0.8):
            turret.add("Olive", cheek.block(u, 0.5, 0.035, 0.22, 0.0, 0.036), SMALL)
        # Flank: four bolt-on slabs with a raised centre plate, hung from hooks on the shell top; the front one blue.
        flank = turret_face(side, 1, 2)
        for u0, u1, material in ((0.02, 0.17, "Blue"), (0.19, 0.45, "Paint"), (0.47, 0.73, "Paint"),
                                 (0.75, 0.98, "Paint")):
            turret.add(material, flank.panel(u0, u1, 0.07, 0.90, 0.08, gap=0.028, taper=0.022))
            turret.add(material, flank.panel(u0 + 0.022, u1 - 0.022, 0.24, 0.76, 0.015, gap=0.108), SMALL)
            bolt_frame(turret, flank, u0 + 0.01, u1 - 0.01, 0.12, 0.85, 0.108, spacing=0.2, material=material)
            turret.add("Olive", flank.block((u0 + u1) / 2.0, 0.94, 0.05, 0.025, -0.01, 0.09), SMALL)
        build_smoke_dischargers(turret, side)


def build_smoke_dischargers(turret, side):
    """Smoke grenade dischargers: an armoured housing on the roof edge just behind the cheek module, its outboard face
    flush with the flank slabs below so it caps them, its front face turned out and back. The two rows of four tubes
    are buried in it and only their muzzles stand out under a brow; a junction box and an armoured conduit at the back
    take the firing cable into the roof."""
    roof = TURRET_ROOF
    z0, z1 = 1.985, roof + 0.22
    # Plan outline (x, y) of the left housing: rear inboard, rear outboard, front outboard, front inboard.
    plan = [(0.06, 1.04), (0.06, 1.43), (0.52, 1.43), (0.68, 1.04)]
    top = [(0.10, 1.07), (0.10, 1.39), (0.44, 1.39), (0.58, 1.07)]
    flip = [(x, side * y) for x, y in plan]
    flip_top = [(x, side * y) for x, y in top]
    turret.add("Paint", plan_prism(flip, z0, z1, flip_top))
    # Welded mounting pad under its inboard half on the roof, and gussets to the shell top behind it.
    turret.add("Paint", plan_prism([(x, side * y) for x, y in [(0.0, 1.0), (0.0, 1.24), (0.62, 1.24), (0.66, 1.0)]],
                                   roof - 0.01, roof + 0.025), SMALL)
    turret.add("Paint", side_prism([(-0.14, SHELL_TOP), (0.07, SHELL_TOP), (0.07, roof + 0.16)], side * 1.25,
                                   side * 1.28), SMALL)
    weld(turret, [(0.0, side * 1.0, roof + 0.025), (0.66, side * 1.0, roof + 0.025)])
    weld(turret, [(0.06, side * 1.04, roof + 0.025), (0.06, side * 1.43, roof + 0.025)])
    # Top access cover, bolted.
    cover = Quad((0.16, side * 1.10, z1), (0.16, side * 1.36, z1), (0.46, side * 1.10, z1), (0.46, side * 1.36, z1),
                 (0.3, side * 1.2, z0))
    turret.add("Paint", cover.panel(0.0, 1.0, 0.0, 1.0, 0.014, gap=0.0, taper=0.008), SMALL)
    bolt_frame(turret, cover, 0.08, 0.92, 0.1, 0.9, 0.014, spacing=0.11)
    # Front face: the sloped front of the housing from just above the roof pad to its top edge, so it looks out and up.
    t = (roof + 0.035 - z0) / (z1 - z0)
    (fx0, fy0), (fx1, fy1) = [Vector(b).lerp(Vector(a), t) for b, a in ((plan[3], top[3]), (plan[2], top[2]))]
    (tx0, ty0), (tx1, ty1) = top[3], top[2]
    face = Quad((fx0, side * fy0, roof + 0.035), (fx1, side * fy1, roof + 0.035), (tx0, side * ty0, z1),
                (tx1, side * ty1, z1), (0.2, side * 1.2, 2.1))
    # A raised frame round the tube field, a brow over it and side cheeks: the tubes sit in a shallow bay.
    turret.add("Paint", face.block(0.5, 0.95, face.u_len * 0.5, 0.025, 0.0, 0.08), SMALL)
    for u in (0.03, 0.97):
        turret.add("Paint", face.block(u, 0.55, 0.018, face.v_len * 0.40, 0.0, 0.06), SMALL)
    turret.add("Dark", face.panel(0.07, 0.93, 0.12, 0.84, 0.01, gap=0.0), SMALL)
    direction = (face.normal + Vector((0.0, 0.0, 0.18))).normalized()
    for v in (0.29, 0.71):
        for k in range(4):
            u = 0.17 + 0.22 * k
            turret.add("Dark", lathe(face.at(u, v, 0.0), direction,
                                     [(-0.10, 0.034), (0.025, 0.034), (0.025, 0.042), (0.05, 0.042), (0.05, 0.029),
                                      (0.012, 0.029)], segments=8, closed=False), False)
            turret.add("Metal", torus(face.at(u, v, 0.012), direction, 0.04, 0.006, segments=12, sides=4), False,
                       high=True)
    # Junction box at the back with the firing cable in an armoured conduit down into the roof.
    turret.add("Olive", plan_prism(rect(-0.08, 0.07, side * 1.12 - 0.08, side * 1.12 + 0.08), roof + 0.02, roof + 0.14,
                                   rect(-0.07, 0.07, side * 1.12 - 0.07, side * 1.12 + 0.07)), SMALL)
    turret.add("Dark", polyline_tube(rounded([(-0.04, side * 1.07, roof + 0.08), (-0.16, side * 1.07, roof + 0.08),
                                              (-0.22, side * 1.04, roof - 0.01)], 0.05), 0.018, segments=8), False)
    turret.add("Metal", cylinder((-0.22, side * 1.04, roof + 0.012), (0, 0, 1), 0.035, 0.025, segments=10), SMALL)


def build_bustle(turret):
    for side in (1, -1):
        # Storage bins on the bustle sides, with latches and a hinge bar.
        bustle = turret_face(side, 3, 4)
        for u0, u1 in ((0.04, 0.48), (0.52, 0.96)):
            turret.add("Olive", bustle.panel(u0, u1, 0.10, 0.88, 0.22, gap=0.008, taper=0.02))
            for u in (u0 + 0.12, u1 - 0.12):
                turret.add("Dark", bustle.block(u, 0.80, 0.025, 0.035, 0.22, 0.25), False)
            turret.add("Dark", polyline_tube([bustle.at(u0 + 0.03, 0.9, 0.12), bustle.at(u1 - 0.03, 0.9, 0.12)],
                                             0.012), False)
            for u in (u0 + 0.05, (u0 + u1) / 2.0, u1 - 0.05):
                turret.add("Metal", cylinder(bustle.at(u, 0.9, 0.12), bustle.u_dir, 0.02, 0.05, segments=8), SMALL)
    # Bustle rack: round bar rails and posts holding containers, jerry cans and a rolled tarp on a mesh floor.
    # The side rails run clear of the bins; their posts hang from arms welded to the bustle wall between the bins.
    rack_x0, rack_x1, rack_y = -1.46, -2.62, 1.58
    for z in (1.62, 2.00):
        turret.add("Metal", polyline_tube(rounded([(rack_x0, rack_y, z), (rack_x1, rack_y, z), (rack_x1, -rack_y, z),
                                                   (rack_x0, -rack_y, z)], 0.10), 0.02, segments=8), SMALL)
    for x, y in ((rack_x0, rack_y), (rack_x0, -rack_y), (rack_x1 + 0.03, rack_y - 0.03), (rack_x1 + 0.03, -rack_y + 0.03),
                 (rack_x1, 0.0), (rack_x1, 0.75), (rack_x1, -0.75), (-1.90, rack_y), (-1.90, -rack_y)):
        turret.add("Metal", polyline_tube([(x, y, 1.58), (x, y, 2.02)], 0.02, segments=8), SMALL)
    for side in (1, -1):
        for x, base_y in ((rack_x0, 1.30), (-1.90, 1.26)):
            for z in (1.66, 1.96):
                wall_y = base_y - 0.06 * (z - TURRET_SKIRT_TOP) / (SHELL_TOP - TURRET_SKIRT_TOP)
                turret.add("Metal", beam((x, side * wall_y, z), (x, side * rack_y, z), (0, 0, 1), 0.012, 0.02), False)
                turret.add("Paint", box(x - 0.018, x + 0.018, side * (wall_y - 0.02), side * (wall_y + 0.012),
                                        z - 0.05, z + 0.05), False)
    turret.add("Dark", box(rack_x1 + 0.02, -2.32, -rack_y + 0.02, rack_y - 0.02, 1.575, 1.595), bevel=False)
    # Gussets carry the rack floor off the bustle's rear wall.
    for y in (-1.0, 0.0, 1.0):
        turret.add("Paint", side_prism([(-2.36, 1.52), (-2.36, 1.575), (-2.58, 1.575)], y - 0.012, y + 0.012), False)
    for y0, y1 in ((-1.25, -0.66), (-0.60, 0.02)):
        turret.add("Olive", plan_prism(rect(-2.58, -2.36, y0, y1), 1.60, 1.94, rect(-2.575, -2.365, y0 + 0.005,
                                                                                     y1 - 0.005)))
        turret.add("Olive", box(-2.585, -2.355, y0 - 0.005, y1 + 0.005, 1.90, 1.96), SMALL)
        for y in (y0 + 0.12, y1 - 0.12):
            turret.add("Dark", box(-2.60, -2.585, y - 0.025, y + 0.025, 1.84, 1.90), bevel=False)
        turret.add("Metal", box(-2.598, -2.588, (y0 + y1) / 2.0 - 0.03, (y0 + y1) / 2.0 + 0.03, 1.70, 1.76), False)
        # Ratchet straps over the lid and down both faces to the rack floor.
        for y in ((y0 + y1) / 2.0 - 0.07, (y0 + y1) / 2.0 + 0.07):
            turret.add("Dark", polyline_tube([(-2.595, y, 1.60), (-2.595, y, 1.965), (-2.345, y, 1.965),
                                              (-2.345, y, 1.60)], 0.007, segments=4), False)
    for y in (0.16, 0.36):
        jerry(turret, Vector((-2.47, y, 1.60)))
    turret.add("Olive", lathe((-2.47, 0.98, 1.74), (0, 1, 0), [(-0.30, 0.10), (-0.32, 0.115), (-0.30, 0.13),
                                                              (0.30, 0.13), (0.32, 0.115), (0.30, 0.10)],
                              segments=14), False)
    for y in (0.80, 1.16):
        turret.add("Dark", torus((-2.47, y, 1.74), (0, 1, 0), 0.133, 0.008, segments=14, sides=4), False)
    turret.add("Paint", box(-2.58, -2.36, 0.60, 0.74, 1.60, 1.84), SMALL)


def build_turret_roof(turret):
    roof = TURRET_ROOF
    # Gunner's primary sight, left front: armoured housing with a sloped face, two windows, open shutters and a hood.
    g = Vector((0.55, 0.78, roof))
    # Base flange bolted to the roof, then the housing: chamfered rear corners and sides that lean in.
    turret.add("Paint", plan_prism(rect(g.x - 0.31, g.x + 0.24, g.y - 0.25, g.y + 0.22), roof - 0.01, roof + 0.03,
                                   rect(g.x - 0.30, g.x + 0.23, g.y - 0.24, g.y + 0.21)), SMALL)
    flange = Quad((g.x - 0.31, g.y - 0.25, roof + 0.03), (g.x + 0.24, g.y - 0.25, roof + 0.03),
                  (g.x - 0.31, g.y + 0.22, roof + 0.03), (g.x + 0.24, g.y + 0.22, roof + 0.03), (g.x, g.y, roof))
    bolt_frame(turret, flange, 0.04, 0.96, 0.05, 0.95, 0.0, spacing=0.12)
    turret.add("Paint", plan_prism([(g.x - 0.26, g.y - 0.15), (g.x - 0.20, g.y - 0.21), (g.x + 0.20, g.y - 0.21),
                                    (g.x + 0.20, g.y + 0.21), (g.x - 0.20, g.y + 0.21), (g.x - 0.26, g.y + 0.15)],
                                   roof + 0.03, roof + 0.30,
                                   [(g.x - 0.23, g.y - 0.12), (g.x - 0.18, g.y - 0.17), (g.x + 0.10, g.y - 0.17),
                                    (g.x + 0.10, g.y + 0.17), (g.x - 0.18, g.y + 0.17), (g.x - 0.23, g.y + 0.12)]))
    # Side access plate and the cable gland at the back.
    side = Quad((g.x - 0.20, g.y + 0.21, roof + 0.03), (g.x + 0.20, g.y + 0.21, roof + 0.03),
                (g.x - 0.18, g.y + 0.17, roof + 0.30), (g.x + 0.10, g.y + 0.17, roof + 0.30), g)
    turret.add("Paint", side.panel(0.12, 0.62, 0.18, 0.78, 0.012, gap=0.0, taper=0.006), SMALL)
    turret.add("Dark", polyline_tube(rounded([(g.x - 0.24, g.y, roof + 0.14), (g.x - 0.33, g.y, roof + 0.14),
                                              (g.x - 0.38, g.y - 0.05, roof)], 0.05), 0.016, segments=8), False)
    face = Quad((g.x + 0.20, g.y - 0.21, roof), (g.x + 0.20, g.y + 0.21, roof), (g.x + 0.10, g.y - 0.19, roof + 0.30),
                (g.x + 0.10, g.y + 0.19, roof + 0.30), g + Vector((-0.1, 0.0, 0.15)))
    turret.add("Dark", face.panel(0.08, 0.92, 0.18, 0.86, 0.015), SMALL)
    turret.add("Dark", tube(face.at(0.33, 0.52, 0.03), face.normal, 0.075, 0.095, 0.04, segments=20), SMALL)
    turret.add("Optics", cylinder(face.at(0.33, 0.52, 0.02), face.normal, 0.075, 0.02, segments=20), bevel=False)
    turret.add("Optics", face.panel(0.60, 0.88, 0.32, 0.72, 0.008, gap=0.02), bevel=False)
    turret.add("Paint", face.block(0.5, 1.05, 0.23, 0.02, 0.0, 0.10), SMALL)
    # Armoured door frame round the windows, and the opened shutters on their hinges.
    for v in (0.12, 0.92):
        turret.add("Paint", face.block(0.5, v, 0.20, 0.018, 0.0, 0.035), SMALL)
    for u in (-0.04, 1.04):
        turret.add("Paint", oriented_box(face.at(u, 0.5, 0.07), face.normal, face.v_dir, face.u_dir, 0.07, 0.11,
                                         0.012), SMALL)
        turret.add("Metal", cylinder(face.at(min(max(u, 0.0), 1.0), 0.5, 0.015), face.v_dir, 0.014, 0.20,
                                     segments=8), SMALL)
    # Commander's independent sight on a pedestal: armoured head with a large window, a rangefinder and a sun hood.
    c = Vector((0.03, -0.24, roof))
    # Pedestal: bolted base flange, armoured column, the traverse ring and a cable entry, then the head.
    turret.add("Paint", lathe(c, (0, 0, 1), [(-0.01, 0.04), (-0.01, 0.20), (0.025, 0.20), (0.035, 0.18),
                                              (0.035, 0.04)], segments=20, closed=False), SMALL)
    for i in range(8):
        t = 2.0 * math.pi * (i + 0.5) / 8
        turret.add("Paint", cylinder(c + Vector((math.cos(t) * 0.165, math.sin(t) * 0.165, 0.045)), (0, 0, 1),
                                     0.016, 0.02, segments=6), bevel=False, high=True)
    turret.add("Olive", lathe(c, (0, 0, 1), [(0.03, 0.04), (0.03, 0.12), (0.06, 0.11), (0.15, 0.10), (0.165, 0.125),
                                             (0.19, 0.125), (0.20, 0.09), (0.20, 0.04)], segments=16, closed=False),
               SMALL)
    turret.add("Dark", tube(c + Vector((0.0, 0.0, 0.178)), (0, 0, 1), 0.10, 0.13, 0.025, segments=16), SMALL)
    turret.add("Dark", polyline_tube(rounded([(c.x - 0.10, c.y + 0.03, roof + 0.13), (c.x - 0.22, c.y + 0.03,
                                              roof + 0.13), (c.x - 0.26, c.y + 0.03, roof)], 0.05), 0.014),
               False)
    turret.add("Olive", plan_prism([(c.x - 0.18, c.y - 0.11), (c.x - 0.13, c.y - 0.16), (c.x + 0.17, c.y - 0.16),
                                    (c.x + 0.17, c.y + 0.16), (c.x - 0.13, c.y + 0.16), (c.x - 0.18, c.y + 0.11)],
                                   roof + 0.20, roof + 0.46,
                                   [(c.x - 0.16, c.y - 0.09), (c.x - 0.11, c.y - 0.14), (c.x + 0.15, c.y - 0.14),
                                    (c.x + 0.15, c.y + 0.14), (c.x - 0.11, c.y + 0.14), (c.x - 0.16, c.y + 0.09)]))
    for sy in (1, -1):
        turret.add("Olive", box(c.x - 0.10, c.x + 0.08, c.y + sy * 0.150, c.y + sy * 0.163, roof + 0.25, roof + 0.40),
                   SMALL)
    turret.add("Dark", box(c.x + 0.165, c.x + 0.18, c.y - 0.13, c.y + 0.13, roof + 0.23, roof + 0.43), SMALL)
    turret.add("Dark", tube((c.x + 0.19, c.y - 0.03, roof + 0.33), (1, 0, 0), 0.075, 0.095, 0.03, segments=20), SMALL)
    turret.add("Optics", cylinder((c.x + 0.183, c.y - 0.03, roof + 0.33), (1, 0, 0), 0.075, 0.01, segments=20),
               bevel=False)
    turret.add("Optics", cylinder((c.x + 0.186, c.y + 0.09, roof + 0.36), (1, 0, 0), 0.028, 0.012, segments=12),
               bevel=False)
    turret.add("Olive", box(c.x + 0.05, c.x + 0.26, c.y - 0.17, c.y + 0.17, roof + 0.455, roof + 0.475), SMALL)
    for sy in (1, -1):
        turret.add("Olive", side_prism([(c.x + 0.14, roof + 0.455), (c.x + 0.26, roof + 0.455),
                                        (c.x + 0.15, roof + 0.40)], c.y + sy * 0.150, c.y + sy * 0.165), False)
    # Window wiper arm parked along the bottom of the window.
    turret.add("Metal", polyline_tube([(c.x + 0.20, c.y - 0.12, roof + 0.25), (c.x + 0.20, c.y + 0.05, roof + 0.25)],
                                      0.007), False)
    turret.add("Dark", cylinder((c.x + 0.195, c.y - 0.12, roof + 0.25), (1, 0, 0), 0.016, 0.03, segments=8), False)
    # Armoured brow on the roof front edge, over the mantlet's top.
    turret.add("Paint", plan_prism(rect(1.00, 1.33, -0.40, 0.40), roof - 0.01, roof + 0.045,
                                   rect(1.00, 1.29, -0.38, 0.38)))
    brow = Quad((1.02, -0.37, roof + 0.045), (1.02, 0.37, roof + 0.045), (1.27, -0.37, roof + 0.045),
                (1.27, 0.37, roof + 0.045), (1.1, 0.0, roof))
    bolt_frame(turret, brow, 0.04, 0.96, 0.15, 0.85, 0.0, spacing=0.16)
    # Commander's vision blocks under armoured hoods round the cupola.
    for i in range(6):
        t = 2.0 * math.pi * i / 6 + 0.3
        radial = Vector((math.cos(t), math.sin(t), 0.0))
        tangent = Vector((-math.sin(t), math.cos(t), 0.0))
        p = CUPOLA + radial * 0.44 + Vector((0.0, 0.0, 0.10))
        # The hood sits on the glass and its cheeks run down to the cupola wall, so nothing floats.
        turret.add("Paint", tapered_box(p + radial * 0.005 + Vector((0, 0, 0.040)), tangent, radial,
                                        Vector((0, 0, 1)), (0.085, 0.055), (0.075, 0.04), 0.015), SMALL)
        for s in (-1, 1):
            turret.add("Paint", oriented_box(p + tangent * s * 0.073 - radial * 0.005, tangent, radial,
                                             Vector((0, 0, 1)), 0.012, 0.035, 0.028), False)
        turret.add("Optics", oriented_box(p, tangent, Vector((0, 0, 1)), radial, 0.06, 0.025, 0.012), bevel=False)
    weld(turret, [CUPOLA + Vector((math.cos(t) * 0.47, math.sin(t) * 0.47, 0.005))
                  for t in (2.0 * math.pi * i / 32 for i in range(33))])
    # Loader's hatch with hinge and handle, and a periscope.
    hatch = Vector((-0.65, 0.62, roof))
    turret.add("Paint", lathe(hatch, (0, 0, 1), [(0.0, 0.25), (0.0, 0.34), (0.035, 0.34), (0.045, 0.32), (0.045, 0.25)],
                              segments=24), SMALL)
    turret.add("Paint", lathe(hatch, (0, 0, 1), [(0.0, 0.24), (0.06, 0.24), (0.07, 0.22)], segments=24,
                              closed=False), SMALL)
    turret.add("Metal", box(hatch.x - 0.36, hatch.x - 0.22, hatch.y - 0.10, hatch.y + 0.10, roof, roof + 0.09), SMALL)
    turret.add("Metal", polyline_tube(rounded([(hatch.x + 0.05, hatch.y - 0.08, roof + 0.07),
                                               (hatch.x + 0.05, hatch.y - 0.08, roof + 0.11),
                                               (hatch.x + 0.05, hatch.y + 0.08, roof + 0.11),
                                               (hatch.x + 0.05, hatch.y + 0.08, roof + 0.07)], 0.03), 0.012), False)
    turret.add("Paint", plan_prism(rect(-0.25, -0.09, 0.62, 0.80), roof, roof + 0.09, rect(-0.25, -0.12, 0.64, 0.78)),
               SMALL)
    turret.add("Optics", box(-0.10, -0.088, 0.65, 0.77, roof + 0.02, roof + 0.07), bevel=False)
    for side in (1, -1):
        # Antenna bases and whips at the rear corners.
        antenna = Vector((-1.95, side * 1.00, roof))
        turret.add("Paint", plan_prism(rect(antenna.x - 0.10, antenna.x + 0.10, antenna.y - 0.10, antenna.y + 0.10),
                                       roof - 0.01, roof + 0.025, rect(antenna.x - 0.09, antenna.x + 0.09,
                                                                       antenna.y - 0.09, antenna.y + 0.09)), False)
        for dx in (-0.07, 0.07):
            for dy in (-0.07, 0.07):
                turret.add("Paint", cylinder(antenna + Vector((dx, dy, 0.035)), (0, 0, 1), 0.012, 0.02, segments=6),
                           bevel=False, high=True)
        antenna = antenna + Vector((0.0, 0.0, 0.025))
        turret.add("Metal", lathe(antenna, (0, 0, 1), [(0.0, 0.06), (0.05, 0.06), (0.07, 0.035), (0.14, 0.03)],
                                  segments=10, closed=False), SMALL)
        turret.add("Dark", cylinder(antenna + Vector((0.0, 0.0, 0.20)), (0, 0, 1), 0.02, 0.12, segments=6), False)
        turret.add("Dark", cylinder(antenna + Vector((0.0, 0.0, 0.76)), (0, 0, 1), 0.009, 1.0, segments=6,
                                    radius_end=0.005), False)
        # Laser warning receiver on the smoke discharger housing, looking forward and out.
        top = roof + 0.22 + 0.014
        corner = Vector((0.30, side * 1.30, top))
        turret.add("Olive", plan_prism(rect(corner.x - 0.07, corner.x + 0.07, corner.y - 0.06, corner.y + 0.06), top,
                                       top + 0.09, rect(corner.x - 0.06, corner.x + 0.04, corner.y - 0.05,
                                                        corner.y + 0.05)), SMALL)
        turret.add("Dark", box(corner.x + 0.047, corner.x + 0.066, corner.y - 0.04, corner.y + 0.04, top + 0.02,
                               top + 0.07), bevel=False)
        # Lifting eyes and grab handles.
        for x in (-0.08, -1.80):
            turret.add("Paint", plan_prism(rect(x - 0.08, x + 0.08, side * 1.10 - 0.05, side * 1.10 + 0.05),
                                           roof - 0.01, roof + 0.02, rect(x - 0.07, x + 0.07, side * 1.10 - 0.04,
                                                                          side * 1.10 + 0.04)), False)
            turret.add("Metal", box(x - 0.05, x + 0.05, side * 1.10 - 0.025, side * 1.10 + 0.025, roof, roof + 0.045),
                       SMALL)
            turret.add("Metal", torus((x, side * 1.10, roof + 0.085), (0, 1, 0), 0.042, 0.013), SMALL)
        turret.add("Metal", polyline_tube(rounded([(-0.70, side * 1.20, roof), (-0.70, side * 1.20, roof + 0.07),
                                                   (-1.20, side * 1.20, roof + 0.07), (-1.20, side * 1.20, roof)],
                                                  0.04), 0.013), False)
        # Roof bolt rows (normal map).
        for i in range(8):
            x = -2.15 + i * 0.42
            turret.add("Paint", cylinder((x, side * 1.10, roof + 0.011), (0, 0, 1), 0.02, 0.022, segments=6),
                       bevel=False, high=True)
    # Rear roof plate: a bolted access panel behind the hatches, and the weld seam where the front roof plate meets it.
    rear = Quad((-2.05, -0.80, roof), (-2.05, 0.80, roof), (-1.15, -0.80, roof), (-1.15, 0.80, roof),
                (-1.6, 0.0, 1.5))
    turret.add("Paint", rear.panel(0.0, 1.0, 0.0, 1.0, 0.015, gap=0.0, taper=0.01), SMALL)
    bolt_frame(turret, rear, 0.03, 0.97, 0.04, 0.96, 0.015, spacing=0.20)
    weld(turret, [(-1.08, -1.20, roof + 0.002), (-1.08, 1.20, roof + 0.002)])
    # Wind sensor mast at the rear centre.
    turret.add("Metal", polyline_tube([(-2.05, 0.0, roof), (-2.05, 0.0, roof + 0.42)], 0.018), False)
    turret.add("Dark", box(-2.10, -2.00, -0.10, 0.10, roof + 0.42, roof + 0.45), SMALL)


# ---- gun ----

def octagon(x, w, h, c, z=None):
    z = TRUNNION.z if z is None else z
    return [(x, y, z + dz) for y, dz in ((w - c, h), (-w + c, h), (-w, h - c), (-w, -h + c), (-w + c, -h),
                                          (w - c, -h), (w, -h + c), (w, h - c))]


def sleeve_radius(x):
    t = max(0.0, min(1.0, (x - 1.93) / (SLEEVE_END - 1.93)))
    return SLEEVE_RADIUS[0] + (SLEEVE_RADIUS[1] - SLEEVE_RADIUS[0]) * t


def barrel_profile():
    """The 105 mm gun from the mantlet collar to the muzzle as one lathe profile (x, radius), bored through."""
    fume0, fume1, fume_r = FUME_EXTRACTOR
    outer = [(1.60, 0.125), (1.80, 0.125), (1.80, 0.152), (1.90, 0.152), (1.93, sleeve_radius(1.93))]
    events = sorted([(b, "band") for b in SLEEVE_BANDS] + [(fume0, "fume")])
    for x, kind in events:
        if kind == "band":
            r = sleeve_radius(x)
            outer += [(x - 0.025, r), (x - 0.018, r + 0.009), (x + 0.018, r + 0.009), (x + 0.025, r)]
        else:
            outer += [(fume0 - 0.04, sleeve_radius(fume0 - 0.04)), (fume0 + 0.04, fume_r), (fume0 + 0.06, fume_r + 0.004),
                      (fume1 - 0.06, fume_r + 0.004), (fume1 - 0.04, fume_r), (fume1 + 0.04, sleeve_radius(fume1 + 0.04))]
    outer = sorted(outer, key=lambda p: p[0])
    outer += [(SLEEVE_END, sleeve_radius(SLEEVE_END)), (SLEEVE_END + 0.02, 0.092), (4.32, 0.092), (4.335, 0.106),
              (MUZZLE_X - 0.03, 0.106), (MUZZLE_X, 0.094), (MUZZLE_X, BORE_RADIUS + 0.006), (MUZZLE_X - 0.01, BORE_RADIUS),
              (4.10, BORE_RADIUS), (1.60, BORE_RADIUS + 0.008)]
    return outer


def build_gun(turret):
    gun = Part("M20_Gun")
    z = TRUNNION.z
    gun.zone("Mantlet")
    gun.add("Paint", loft(octagon(1.28, 0.42, 0.26, 0.10), octagon(MANTLET_FRONT, 0.35, 0.22, 0.09)))
    gun.add("Paint", loft(octagon(MANTLET_FRONT, 0.32, 0.20, 0.08), octagon(MANTLET_FRONT + 0.03, 0.30, 0.18, 0.07)),
            SMALL)
    gun.zone("Barrel")
    gun.add("Paint", lathe((0.0, 0.0, z), (1, 0, 0), barrel_profile(), segments=24), bevel=False)
    if DETAIL:
        gun.zone("Mantlet")
        front = Quad((MANTLET_FRONT + 0.03, 0.30, z - 0.18), (MANTLET_FRONT + 0.03, -0.30, z - 0.18),
                     (MANTLET_FRONT + 0.03, 0.30, z + 0.18), (MANTLET_FRONT + 0.03, -0.30, z + 0.18),
                     (TRUNNION.x, 0.0, z))
        bolt_frame(gun, front, 0.06, 0.94, 0.08, 0.92, 0.0, spacing=0.12)
        # Add-on armour over the mantlet: a top plate and a cheek plate on each side, bolted through.
        top = Quad((1.28, -0.32, z + 0.26), (1.28, 0.32, z + 0.26), (MANTLET_FRONT, -0.26, z + 0.22),
                   (MANTLET_FRONT, 0.26, z + 0.22), (TRUNNION.x, 0.0, z))
        gun.add("Paint", top.panel(0.06, 0.94, 0.10, 0.94, 0.025, gap=0.0, taper=0.015), SMALL)
        bolt_frame(gun, top, 0.10, 0.90, 0.16, 0.88, 0.025, spacing=0.14)
        for side in (1, -1):
            cheek = Quad((1.28, side * 0.42, z - 0.16), (MANTLET_FRONT, side * 0.35, z - 0.13),
                         (1.28, side * 0.42, z + 0.16), (MANTLET_FRONT, side * 0.35, z + 0.13), (TRUNNION.x, 0.0, z))
            gun.add("Paint", cheek.panel(0.32, 0.97, 0.06, 0.94, 0.03, gap=0.0, taper=0.012), SMALL)
            bolt_frame(gun, cheek, 0.38, 0.92, 0.14, 0.86, 0.03, spacing=0.12)
        # Gun shield boot between the cover plate and the barrel collar, held by a clamp ring.
        gun.add("Rubber", lathe((0.0, 0.0, z), (1, 0, 0), [(MANTLET_FRONT + 0.02, 0.13), (MANTLET_FRONT + 0.02, 0.195),
                                                          (MANTLET_FRONT + 0.045, 0.205), (MANTLET_FRONT + 0.07, 0.19),
                                                          (MANTLET_FRONT + 0.09, 0.178), (MANTLET_FRONT + 0.115, 0.16),
                                                          (MANTLET_FRONT + 0.115, 0.13)], segments=20), False)
        gun.add("Metal", lathe((0.0, 0.0, z), (1, 0, 0), [(MANTLET_FRONT + 0.105, 0.150), (MANTLET_FRONT + 0.105, 0.166),
                                                         (MANTLET_FRONT + 0.135, 0.166), (MANTLET_FRONT + 0.135, 0.150)],
                               segments=24), False)
        gun.add("Metal", box(MANTLET_FRONT + 0.105, MANTLET_FRONT + 0.135, -0.02, 0.02, z + 0.160, z + 0.19), False)
        # Coaxial machine gun port in an armoured surround, and the auxiliary sight window under a hood.
        port = [(x, y - 0.24, zz) for x, y, zz in octagon(MANTLET_FRONT + 0.03, 0.07, 0.06, 0.02, z - 0.08)]
        port_face = [(x, y - 0.24, zz) for x, y, zz in octagon(MANTLET_FRONT + 0.065, 0.06, 0.05, 0.018, z - 0.08)]
        gun.add("Paint", loft(port, port_face), SMALL)
        gun.add("Dark", tube((MANTLET_FRONT + 0.07, -0.24, z - 0.08), (1, 0, 0), 0.022, 0.04, 0.03, segments=12),
                SMALL)
        gun.add("Dark", tube((MANTLET_FRONT + 0.05, 0.24, z + 0.06), (1, 0, 0), 0.045, 0.062, 0.04, segments=14),
                SMALL)
        gun.add("Optics", cylinder((MANTLET_FRONT + 0.04, 0.24, z + 0.06), (1, 0, 0), 0.045, 0.01, segments=14),
                bevel=False)
        gun.add("Paint", box(MANTLET_FRONT + 0.03, MANTLET_FRONT + 0.13, 0.165, 0.315, z + 0.125, z + 0.15), SMALL)
        for y in (0.165, 0.30):
            gun.add("Paint", box(MANTLET_FRONT + 0.03, MANTLET_FRONT + 0.11, y, y + 0.015, z - 0.01, z + 0.125),
                    SMALL)
        # Lifting eye on a lug on the mantlet top plate.
        gun.add("Metal", box(MANTLET_FRONT - 0.26, MANTLET_FRONT - 0.14, -0.025, 0.025, z + 0.24, z + 0.275), SMALL)
        gun.add("Metal", torus((MANTLET_FRONT - 0.20, 0.0, z + 0.31), (0, 1, 0), 0.04, 0.013), SMALL)
        gun.zone("Barrel")
        # Muzzle reference sensor on its bracket.
        gun.add("Olive", box(MUZZLE_X - 0.36, MUZZLE_X - 0.30, -0.03, 0.03, z + 0.10, z + 0.14), SMALL)
        gun.add("Dark", box(MUZZLE_X - 0.40, MUZZLE_X - 0.26, -0.045, 0.045, z + 0.14, z + 0.21), SMALL)
        gun.add("Optics", box(MUZZLE_X - 0.40, MUZZLE_X - 0.395, -0.03, 0.03, z + 0.155, z + 0.195), bevel=False)
        for x in SLEEVE_BANDS:
            r = sleeve_radius(x) + 0.009
            gun.add("Metal", box(x - 0.02, x + 0.02, -0.025, 0.025, z - r - 0.025, z - r + 0.005), SMALL, high=True)
    return gun.build(TRUNNION, turret, TURRET_RING)


# ---- remote weapon station ----

def build_rws(turret):
    rws = Part("M20_RWS")
    c = RWS_RING
    gun_z = c.z + 0.38
    rws.zone("RWS")
    rws.add("Olive", lathe(c, (0, 0, 1), [(0.0, 0.25), (0.035, 0.25), (0.06, 0.22), (0.06, 0.10)], segments=24,
                           closed=False), SMALL)
    rws.add("Dark", tube(c + Vector((0.0, 0.0, 0.008)), (0, 0, 1), 0.20, 0.258, 0.016, segments=24), SMALL)
    for i in range(12):
        t = 2.0 * math.pi * (i + 0.5) / 12
        rws.add("Dark", cylinder(c + Vector((math.cos(t) * 0.225, math.sin(t) * 0.225, 0.045)), (0, 0, 1), 0.012,
                                  0.016, segments=6), bevel=False, high=True)
    rws.add("Olive", plan_prism([(c.x - 0.24, c.y - 0.12), (c.x - 0.19, c.y - 0.17), (c.x + 0.16, c.y - 0.17),
                                 (c.x + 0.16, c.y + 0.17), (c.x - 0.19, c.y + 0.17), (c.x - 0.24, c.y + 0.12)],
                                c.z + 0.06, c.z + 0.24,
                                [(c.x - 0.22, c.y - 0.10), (c.x - 0.17, c.y - 0.15), (c.x + 0.12, c.y - 0.15),
                                 (c.x + 0.12, c.y + 0.15), (c.x - 0.17, c.y + 0.15), (c.x - 0.22, c.y + 0.10)]))
    side_cover = Quad((c.x - 0.24, c.y - 0.11, c.z + 0.08), (c.x - 0.24, c.y + 0.11, c.z + 0.08),
                      (c.x - 0.2225, c.y - 0.095, c.z + 0.22), (c.x - 0.2225, c.y + 0.095, c.z + 0.22),
                      c + Vector((0, 0, 0.15)))
    rws.add("Olive", side_cover.panel(0.05, 0.95, 0.12, 0.88, 0.012, gap=0.0), SMALL)
    bolt_frame(rws, side_cover, 0.08, 0.92, 0.18, 0.82, 0.012, spacing=0.08, material="Olive")
    # Cradle: two yoke plates holding the gun on its elevation axis.
    for dy in (-0.085, 0.085):
        rws.add("Olive", side_prism([(c.x - 0.12, c.z + 0.22), (c.x + 0.12, c.z + 0.22), (c.x + 0.10, gun_z + 0.07),
                                     (c.x - 0.06, gun_z + 0.07)], c.y + dy - 0.012, c.y + dy + 0.012), SMALL)
        rws.add("Metal", cylinder((c.x + 0.02, c.y + dy * 1.25, gun_z), (0, 1, 0), 0.035, 0.03, segments=10), SMALL)
    # 12.7 mm machine gun: receiver, feed cover, spade grips, barrel with its support sleeve and flash hider.
    rws.add("Dark", box(c.x - 0.30, c.x + 0.14, c.y - 0.055, c.y + 0.055, gun_z - 0.07, gun_z + 0.05))
    rws.add("Dark", box(c.x - 0.22, c.x + 0.10, c.y - 0.05, c.y + 0.05, gun_z + 0.05, gun_z + 0.08), SMALL)
    for dy in (-0.035, 0.035):
        rws.add("Dark", polyline_tube([(c.x - 0.30, c.y + dy, gun_z - 0.02), (c.x - 0.36, c.y + dy, gun_z - 0.04),
                                       (c.x - 0.36, c.y + dy, gun_z - 0.10)], 0.012), False)
    rws.add("Metal", lathe((c.x, c.y, gun_z), (1, 0, 0), [(0.14, 0.04), (0.32, 0.04), (0.34, 0.022),
                                                         (0.18 + RWS_BARREL_LENGTH - 0.10, 0.022),
                                                         (0.18 + RWS_BARREL_LENGTH - 0.10, 0.03),
                                                         (0.18 + RWS_BARREL_LENGTH, 0.03),
                                                         (0.18 + RWS_BARREL_LENGTH, 0.012)], segments=10,
                            closed=False), SMALL)
    rws.add("Dark", polyline_tube(rounded([(c.x + 0.04, c.y, gun_z + 0.08), (c.x + 0.04, c.y, gun_z + 0.15),
                                           (c.x + 0.22, c.y, gun_z + 0.15), (c.x + 0.22, c.y, gun_z + 0.05)], 0.04),
                                  0.012), False)
    # Sensor head on the right: armoured box with day and thermal windows and a rangefinder, under a hood.
    s0, s1 = c.y - 0.37, c.y - 0.13
    rws.add("Olive", plan_prism(rect(c.x - 0.08, c.x + 0.20, s0, s1), c.z + 0.14, c.z + 0.42,
                                rect(c.x - 0.07, c.x + 0.18, s0 + 0.01, s1 - 0.01)))
    rws.add("Dark", box(c.x + 0.195, c.x + 0.21, s0 + 0.02, s1 - 0.02, c.z + 0.17, c.z + 0.39), SMALL)
    rws.add("Dark", tube((c.x + 0.22, c.y - 0.20, c.z + 0.30), (1, 0, 0), 0.05, 0.065, 0.03, segments=16), SMALL)
    rws.add("Optics", cylinder((c.x + 0.215, c.y - 0.20, c.z + 0.30), (1, 0, 0), 0.05, 0.01, segments=16), bevel=False)
    rws.add("Optics", box(c.x + 0.21, c.x + 0.216, c.y - 0.335, c.y - 0.265, c.z + 0.26, c.z + 0.34), bevel=False)
    rws.add("Optics", cylinder((c.x + 0.213, c.y - 0.30, c.z + 0.20), (1, 0, 0), 0.02, 0.01, segments=10),
            bevel=False)
    rws.add("Olive", box(c.x + 0.10, c.x + 0.27, s0 - 0.01, s1 + 0.01, c.z + 0.415, c.z + 0.435), SMALL)
    # Armoured shutter doors swung open either side of the windows, on hinge pins.
    for y in (s0 - 0.004, s1 + 0.004):
        sign = -1.0 if y < c.y - 0.25 else 1.0
        rws.add("Olive", box(c.x + 0.205, c.x + 0.30, y + sign * 0.0, y + sign * 0.013, c.z + 0.17, c.z + 0.40), False)
        rws.add("Metal", cylinder((c.x + 0.205, y + sign * 0.006, c.z + 0.285), (0, 0, 1), 0.011, 0.22, segments=8),
                SMALL)
    # Cable loom from the sensor head into the cradle.
    rws.add("Dark", polyline_tube(rounded([(c.x - 0.07, c.y - 0.25, c.z + 0.24), (c.x - 0.14, c.y - 0.25, c.z + 0.24),
                                           (c.x - 0.18, c.y - 0.15, c.z + 0.20)], 0.04), 0.013), False)
    # Ammunition box on the left with its feed chute.
    rws.add("Olive", box(c.x - 0.20, c.x + 0.08, c.y + 0.15, c.y + 0.31, c.z + 0.10, c.z + 0.34))
    rws.add("Olive", box(c.x - 0.21, c.x + 0.09, c.y + 0.14, c.y + 0.32, c.z + 0.33, c.z + 0.355), SMALL)
    rws.add("Metal", cylinder((c.x - 0.06, c.y + 0.322, c.z + 0.34), (1, 0, 0), 0.01, 0.24, segments=6), False)
    rws.add("Dark", box(c.x + 0.08, c.x + 0.10, c.y + 0.20, c.y + 0.25, c.z + 0.28, c.z + 0.345), False)
    rws.add("Dark", polyline_tube(rounded([(c.x - 0.18, c.y + 0.225, c.z + 0.355), (c.x - 0.18, c.y + 0.225, c.z + 0.40),
                                           (c.x - 0.10, c.y + 0.225, c.z + 0.40)], 0.03), 0.01), False)
    rws.add("Dark", polyline_tube(rounded([(c.x - 0.02, c.y + 0.15, c.z + 0.30), (c.x - 0.04, c.y + 0.10, gun_z),
                                           (c.x - 0.06, c.y + 0.055, gun_z)], 0.04), 0.03, segments=4), False)
    return rws.build(c, turret, TURRET_RING)


# ---- running gear ----

def build_road_wheel(name):
    """One road wheel at the origin, axle along Y, hub on the +Y (outer) face: twin rubber-tyred dished discs."""
    wheel = Part(name)
    for sign in (1, -1):
        def y(offset):
            return sign * offset
        if sign > 0:
            tyre = [(0.045, 0.285), (0.045, 0.325), (0.058, 0.34), (0.182, 0.34), (0.195, 0.325), (0.195, 0.285)]
            disc = [(0.05, 0.285), (0.19, 0.285), (0.19, 0.27), (0.155, 0.245), (0.135, 0.18), (0.165, 0.115),
                    (0.205, 0.105), (0.205, 0.075), (0.05, 0.075)]
        else:
            tyre = [(0.045, 0.285), (0.045, 0.34), (0.195, 0.34), (0.195, 0.285)]
            disc = [(0.05, 0.285), (0.19, 0.285), (0.15, 0.20), (0.15, 0.075), (0.05, 0.075)]
        segments = 24 if sign > 0 else 16
        wheel.add("Rubber", lathe((0, 0, 0), (0, sign, 0), tyre, segments=segments), bevel=False)
        wheel.add("Olive", lathe((0, 0, 0), (0, sign, 0), disc, segments=segments), bevel=False)
    # Steel rim flange between the outer disc and its tyre.
    wheel.add("Metal", lathe((0, 0, 0), (0, 1, 0), [(0.188, 0.268), (0.200, 0.268), (0.200, 0.292)],
                             segments=16), bevel=False)
    wheel.add("Olive", lathe((0, 0, 0), (0, 1, 0), [(0.205, 0.10), (0.235, 0.10), (0.245, 0.09), (0.25, 0.06),
                                                   (0.25, 0.035)], segments=16, closed=False), bevel=False)
    wheel.add("Metal", lathe((0, 0, 0), (0, 1, 0), [(0.25, 0.03), (0.258, 0.022)], segments=8, closed=False),
              bevel=False)
    if DETAIL:
        for i in range(8):
            t = 2.0 * math.pi * (i + 0.5) / 8
            wheel.add("Metal", cylinder((math.cos(t) * 0.155, 0.16, math.sin(t) * 0.155), (0, 1, 0), 0.016, 0.03,
                                        segments=6), bevel=False, high=True)
        for i in range(6):
            t = 2.0 * math.pi * i / 6
            wheel.add("Dark", cylinder((math.cos(t) * 0.215, 0.152, math.sin(t) * 0.215), (0, 1, 0), 0.032, 0.012,
                                       segments=8), bevel=False)
    return wheel.build((0.0, 0.0, 0.0))


def build_sprocket(name):
    sprocket = Part(name)
    root = SPROCKET[2]
    for y in (-0.13, 0.13):
        sprocket.add("Metal", gear((0.0, y, 0.0), (0, 1, 0), root, root + 0.065, 11, 0.06), SMALL)
    sprocket.add("Olive", lathe((0, 0, 0), (0, 1, 0), [(-0.17, 0.10), (-0.17, 0.24), (-0.10, 0.26), (0.10, 0.26),
                                                       (0.17, 0.24), (0.17, 0.16), (0.22, 0.14), (0.22, 0.10)],
                                segments=24), bevel=False)
    sprocket.add("Metal", lathe((0, 0, 0), (0, 1, 0), [(0.22, 0.11), (0.26, 0.11), (0.275, 0.08), (0.28, 0.05)],
                                segments=16, closed=False), bevel=False)
    if DETAIL:
        for i in range(8):
            t = 2.0 * math.pi * (i + 0.5) / 8
            sprocket.add("Metal", cylinder((math.cos(t) * 0.125, 0.235, math.sin(t) * 0.125), (0, 1, 0), 0.016,
                                           0.03, segments=6), bevel=False, high=True)
        for i in range(5):
            t = 2.0 * math.pi * i / 5
            sprocket.add("Dark", cylinder((math.cos(t) * 0.19, 0.172, math.sin(t) * 0.19), (0, 1, 0), 0.04, 0.012,
                                          segments=10), bevel=False)
    return sprocket.build((0.0, 0.0, 0.0))


def build_idler(name):
    idler = Part(name)
    r = IDLER[2]
    for sign in (1, -1):
        idler.add("Metal", lathe((0, 0, 0), (0, sign, 0), [(0.045, r - 0.04), (0.045, r - 0.008), (0.055, r),
                                                           (0.185, r), (0.195, r - 0.008), (0.195, r - 0.04)],
                                 segments=24), bevel=False)
        idler.add("Olive", lathe((0, 0, 0), (0, sign, 0), [(0.05, r - 0.04), (0.19, r - 0.04), (0.14, 0.14),
                                                           (0.17, 0.08), (0.05, 0.08)], segments=24), bevel=False)
    idler.add("Metal", lathe((0, 0, 0), (0, 1, 0), [(0.17, 0.09), (0.24, 0.09), (0.255, 0.06), (0.26, 0.03)],
                             segments=16, closed=False), bevel=False)
    if DETAIL:
        for i in range(6):
            t = 2.0 * math.pi * i / 6
            idler.add("Dark", cylinder((math.cos(t) * 0.18, 0.165, math.sin(t) * 0.18), (0, 1, 0), 0.035, 0.012,
                                       segments=10), bevel=False)
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


def track_link(part, centre, u, v, w, pitch):
    """One link, w pointing out of the loop (to the ground on the bottom run): steel shoe, twin rubber pads, end
    connectors over the pins, and the centre guide horn running between the wheel discs. The same pieces in the same
    order for every link, so the track can share one link's UVs."""
    shoe = centre - w * 0.015
    part.add("Track", oriented_box(shoe, u, v, w, pitch * 0.40, TRACK_HALF_WIDTH * 0.90, 0.03), bevel=False)
    for side in (1, -1):
        part.add("Rubber", oriented_box(shoe + w * 0.045 + v * side * 0.135, u, v, w, pitch * 0.36, 0.115, 0.015),
                 bevel=False)
    for side in (1, -1):
        part.add("Metal", oriented_box(shoe + v * side * TRACK_HALF_WIDTH * 0.95, u, v, w, pitch * 0.5, 0.028, 0.038),
                 bevel=False)
    part.add("Metal", tapered_box(shoe - w * 0.08, u, v, -w, (0.048, 0.034), (0.024, 0.017), 0.05), bevel=False)
    # Normal-map detail: wedge bolts on the end connectors, pad bolts.
    for side in (1, -1):
        part.add("Metal", cylinder(shoe + v * side * (TRACK_HALF_WIDTH * 0.95 + 0.03), v, 0.016, 0.012, segments=6),
                 bevel=False, high=True)
        part.add("Metal", cylinder(shoe + w * 0.062 + v * side * 0.135, w, 0.012, 0.006, segments=6), bevel=False,
                 high=True)


def build_track(name, side):
    track = Part(name)
    track.zone("Tracks")
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
        track_link(track, Vector((point.x, y, point.y)), u, v, w, pitch)
    obj = track.build((0.0, y, 0.0))
    obj["pitch"] = pitch
    return obj


def build_track_template(pitch):
    """One link at the origin (a bottom-run link), the source of the track's UVs and texture."""
    link = Part("M20_TrackLink")
    link.zone("Tracks")
    track_link(link, Vector((0.0, 0.0, 0.05)), Vector((1.0, 0.0, 0.0)), Vector((0.0, 1.0, 0.0)),
               Vector((0.0, 0.0, -1.0)), pitch)
    return link.build((60.0, 0.0, 0.0))


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
    mats.make_procedural()
    parts = {"hull": build_hull()}
    turret = build_turret()
    turret.parent = parts["hull"]
    parts["turret"] = turret
    parts["gun"] = build_gun(turret)
    parts["rws"] = build_rws(turret)
    socket("Muzzle_Socket", parts["gun"], (MUZZLE_X - TRUNNION.x, 0.0, 0.0))
    socket("Coax_Socket", parts["gun"], (MANTLET_FRONT + 0.07 - TRUNNION.x, -0.24, -0.08))
    socket("RWS_Muzzle_Socket", parts["rws"], (0.18 + RWS_BARREL_LENGTH, 0.0, 0.38))
    # One mesh per wheel kind, shared by every instance; the right side's turn half round so the hub faces out. The
    # high-poly twins stay as single objects on the left instance, for the bake.
    kinds = {}
    for kind, builder in (("RoadWheel", build_road_wheel), ("Sprocket", build_sprocket), ("Idler", build_idler)):
        obj = builder(f"M20_{kind}")
        high = bpy.data.objects[f"M20_{kind}_High"]
        kinds[kind] = (obj.data, high)
        bpy.data.objects.remove(obj)
    wheels = []
    for side, letter in ((1, "L"), (-1, "R")):
        places = [(f"M20_RoadWheel_{letter}{i + 1}", "RoadWheel", x, ROAD_WHEEL_Z) for i, x in enumerate(ROAD_WHEEL_X)]
        places += [(f"M20_Sprocket_{letter}", "Sprocket", SPROCKET[0], SPROCKET[1]),
                   (f"M20_Idler_{letter}", "Idler", IDLER[0], IDLER[1])]
        for name, kind, x, z in places:
            obj = bpy.data.objects.new(name, kinds[kind][0])
            scene.collection.objects.link(obj)
            add_modifiers(obj)
            obj.parent = parts["hull"]
            obj.location = (x, side * TRACK_Y, z)
            obj.rotation_euler = (0.0, 0.0, 0.0 if side > 0 else math.pi)
            wheels.append(obj)
        track = build_track(f"M20_Track_{letter}", side)
        track.parent = parts["hull"]
        parts[f"track_{letter}"] = track
    for kind, (_, high) in kinds.items():
        left = bpy.data.objects[f"M20_{kind}_L" if kind != "RoadWheel" else "M20_RoadWheel_L1"]
        high.parent = left
        high.location = (0.0, 0.0, 0.0)
    parts["wheels"] = wheels
    parts["template"] = build_track_template(parts["track_L"]["pitch"])
    return parts


def game_objects():
    return [o for o in bpy.context.scene.objects if o.type == "MESH" and not o.name.endswith("_High")
            and o.name != "Ground"]


def evaluated_stats():
    """World-space bounds of every game mesh, triangles per object, and triangles per (object, component)."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    low = Vector((1e9, 1e9, 1e9))
    high = -low
    triangles, components = {}, {}
    for obj in game_objects():
        if obj.name == "M20_TrackLink":
            continue
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        mesh.calc_loop_triangles()
        triangles[obj.name] = len(mesh.loop_triangles)
        attribute = mesh.attributes.get("component")
        for polygon in mesh.polygons:
            key = COMPONENTS[attribute.data[polygon.index].value] if attribute else obj.name
            components[key] = components.get(key, 0) + polygon.loop_total - 2
        for vertex in mesh.vertices:
            p = obj.matrix_world @ vertex.co
            low = Vector(min(a, b) for a, b in zip(low, p))
            high = Vector(max(a, b) for a, b in zip(high, p))
        evaluated.to_mesh_clear()
    return low, high, triangles, components


# (set, bake/source size, game size): baked at source size, box-filtered to the game size Unreal imports.
TEXTURE_SETS = (("M20_Hull", 4096, 2048), ("M20_Turret", 4096, 2048), ("M20_Running", 2048, 2048),
                ("M20_Track", 2048, 1024))


def hidden_weight(centre, normal):
    """Relative texel density by how much the player sees a face: undersides, the lower hull behind the tracks and
    the suspension get less, so the visible armour gets more."""
    if normal.z < -0.5:
        return 0.4
    if abs(centre.y) < LOWER_HALF_WIDTH + 0.08 and centre.z < 1.0 and -3.1 < centre.x < 2.65:
        return 0.45
    if LOWER_HALF_WIDTH < abs(centre.y) < SKIRT_Y and centre.z < 0.95 and -3.1 < centre.x < 3.1:
        return 0.6
    return 1.0


def running_weight(centre, normal):
    """The road wheel, sprocket and idler are built on the left side: their inboard faces (towards -Y) get less."""
    return 0.5 if normal.y < -0.6 else 1.0


def finish_game_meshes(parts):
    """Applies the modifiers, lays out the UVs per texture set, bakes, and puts the game materials on."""
    template = parts["template"]
    tracks = [parts["track_L"], parts["track_R"]]
    linked = tracks + [template]
    bake.finalize([o for o in game_objects() if o not in linked], list(MATERIALS))
    bake.finalize(linked, list(MATERIALS), triangulate=False)
    hull, turret, gun, rws = parts["hull"], parts["turret"], parts["gun"], parts["rws"]
    running = [bpy.data.objects[n] for n in ("M20_RoadWheel_L1", "M20_Sprocket_L", "M20_Idler_L")]
    sets = {"M20_Hull": [hull], "M20_Turret": [turret, gun, rws], "M20_Running": running, "M20_Track": [template]}
    weights = {"M20_Hull": hidden_weight, "M20_Turret": hidden_weight, "M20_Running": running_weight}
    for name, lows in sets.items():
        bake.unwrap(lows, weight=weights.get(name))
    for track in tracks:
        bake.copy_uvs_tiled(template, track)
    bake.finalize(linked, list(MATERIALS))
    if not BAKE:
        return {}
    bake.use_gpu(bpy.context.scene)
    world = bpy.data.worlds.new("BakeWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    bpy.context.scene.world = world
    body = [hull, turret, gun, rws] + tracks + [w for w in parts["wheels"]]
    ao_scope = {"M20_Hull": body, "M20_Turret": body, "M20_Running": running, "M20_Track": [template]}
    paths = {}
    for name, size, game_size in TEXTURE_SETS:
        lows = sets[name]
        pairs = [(low, bpy.data.objects[f"{low.name}_High"] if f"{low.name}_High" in bpy.data.objects else
                  next(c for c in low.children if c.name.endswith("_High"))) for low in lows]
        print(f"baking {name} ({size} -> {game_size})")
        paths[name] = bake.bake_set(name, pairs, size, TEXTURE_DIR, ao_scope[name], game_size)
    emissive = mats.emissive_game()
    for name, _, _ in TEXTURE_SETS:
        material = mats.textured(name, paths[name]["BC"], paths[name]["N"], paths[name]["ORM"])
        lows = sets[name] + (tracks if name == "M20_Track" else [])
        bake.assign_game_materials(lows, material, emissive, list(MATERIALS))
    bpy.data.worlds.remove(world)
    return paths


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
                                 use_tspace=True, colors_type="SRGB", apply_unit_scale=True,
                                 apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                                 bake_space_transform=False, add_leaf_bones=False, bake_anim=False,
                                 use_custom_props=False, path_mode="AUTO")
        obj.parent, obj.location, obj.rotation_euler = saved
        bpy.context.view_layer.update()


def texture_memory(size, alpha=True):
    """Unreal GPU memory (MB, with mips) for one set: BaseColor BC3 (BC1 without alpha), Normal BC5, ORM BC1."""
    pixels = size * size * 4.0 / 3.0
    return pixels * ((1.0 if alpha else 0.5) + 1.0 + 0.5) / (1024 * 1024)


def write_report(low, high, triangles, components):
    size = high - low
    groups = {
        "Hull": ["M20_Hull"], "Turret": ["M20_Turret"], "Gun": ["M20_Gun"], "RWS": ["M20_RWS"],
        "Tracks (2)": ["M20_Track_L", "M20_Track_R"],
        "Road wheels (12)": [n for n in triangles if n.startswith("M20_RoadWheel")],
        "Sprockets (2)": [n for n in triangles if n.startswith("M20_Sprocket")],
        "Idlers (2)": [n for n in triangles if n.startswith("M20_Idler")],
    }
    lines = ["# M20 Guardian report", "",
             "Generated by `Tools/Art/m20_guardian_blender.py` (Blender 5.1).", "",
             "## Size (evaluated, with modifiers)", "",
             f"- Length {size.x:.2f} m (sheet: 7.8 m incl. gun), width {size.y:.2f} m (sheet: 3.4 m), "
             f"height {size.z:.2f} m (sheet: 2.6 m to the roof optics)",
             f"- Hull length {LOWER_HULL[3][0] - LOWER_HULL[0][0]:.2f} m, gun overhang beyond the hull "
             f"{MUZZLE_X - LOWER_HULL[3][0]:.2f} m", "",
             "## Triangles per part (game mesh, bevels applied)", "", "| Part | Triangles | Materials |", "|---|---:|---|"]
    total = 0
    for label, names in groups.items():
        count = sum(triangles[n] for n in names)
        total += count
        materials = ", ".join(m.name for m in bpy.data.objects[names[0]].data.materials)
        lines.append(f"| {label} | {count:,} | {materials} |")
    lines += [f"| **Total** | **{total:,}** | |", "",
              "## Triangles per component", "", "| Component | Triangles |", "|---|---:|"]
    for name in COMPONENTS:
        if components.get(name):
            lines.append(f"| {name} | {components[name]:,} |")
    used = sorted({m.name for o in game_objects() if o.name != "M20_TrackLink" for m in o.data.materials})
    lines += ["", "## Materials and textures", "", f"- {len(used)} materials: {', '.join(used)}"]
    if BAKE:
        lines += ["", "| Texture set | Game size | Unreal memory (game) | Source size | Unreal memory (source) |",
                  "|---|---:|---:|---:|---:|"]
        game_total = source_total = 0.0
        for name, size_px, game_px in TEXTURE_SETS:
            game_mb, source_mb = texture_memory(game_px), texture_memory(size_px)
            game_total += game_mb
            source_total += source_mb
            lines.append(f"| {name} | {game_px} | {game_mb:.1f} MB | {size_px} | {source_mb:.1f} MB |")
        lines.append(f"| **Total** | | **{game_total:.1f} MB** | | **{source_total:.1f} MB** |")
        lines += ["", "Unreal memory: BaseColor BC3 (team mask in alpha), Normal BC5, ORM BC1, full mip chain, "
                      "before streaming."]
    lines.append("")
    with open(os.path.join(OUT_DIR, "report_stats.md"), "w") as file:
        file.write("\n".join(lines))
    print("\n".join(lines))
    return total


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
                f"scale {tuple(round(c, 3) for c in obj.matrix_world.to_scale())}, "
                f"uv {len(mesh.uv_layers)}, materials {[m.name for m in mesh.materials]}")
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
    if PREVIEW or STAGE == 1:
        low, high, triangles, components = evaluated_stats()
        print(f"bounds {tuple(round(c, 3) for c in low)} .. {tuple(round(c, 3) for c in high)}")
        print(f"triangles {sum(triangles.values())}", sorted(components.items(), key=lambda item: -item[1]))
        parts["template"].hide_render = True
        presentation.render_all(RENDER_DIR, parts, preview=True)
        return
    finish_game_meshes(parts)
    bpy.data.objects.remove(parts.pop("template"))
    low, high, triangles, components = evaluated_stats()
    print(f"bounds {tuple(round(c, 3) for c in low)} .. {tuple(round(c, 3) for c in high)}")
    write_layout(parts)
    write_report(low, high, triangles, components)
    with open(os.path.join(OUT_DIR, "export_triangles.json"), "w") as file:
        json.dump({file_name: triangles[object_name] for object_name, file_name in EXPORTS}, file, indent=2)
    export_fbx()
    if BAKE:
        lines, _ = validation.validate()
        with open(os.path.join(OUT_DIR, "report_stats.md"), "a") as file:
            file.write("\n".join(lines))
    os.makedirs(os.path.dirname(BLEND), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)
    presentation.render_all(RENDER_DIR, parts, previous_front34=PREVIOUS_FRONT34, concept=CONCEPT, samples=SAMPLES)
    # The renders added and removed their lights and camera; save again so the .blend has none of them.
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)


main()
