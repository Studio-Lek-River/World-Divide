"""Builds the USA main battle tank (working name M5 "Bulwark", original Lek River Studio work) from code and exports it
as Art/Source/Placeholder/Original/usa_mbt.glb, with the .blend next to it for hand edits and preview renders in
Saved/ArtPreviews.

  blender -b --python Tools/Art/build_usa_tank_blender.py

Low-poly and flat-shaded with colour-only materials, which is what placeholder_art.py imports: Body, Dark (tracks,
wheels, grilles), Detail (optics, machine gun) and HouseColor (the panels the game tints in the player's colour). The tank
faces +X with its tracks on the ground at z = 0, in metres. It comes in three objects, Hull, Turret (pivot on the turret
ring) and Gun (pivot on the trunnions, child of Turret), so turret traverse can use them later; the importer merges them.
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUTPUT = os.path.join(PROJECT_DIR, "Art", "Source", "Placeholder", "Original", "usa_mbt.glb")
PREVIEW_DIR = os.path.join(PROJECT_DIR, "Saved", "ArtPreviews")

# Linear base colours. HouseColor is light so the team tint keeps its brightness.
MATERIALS = {
    "Body": (0.13, 0.135, 0.07, 1.0),
    "Dark": (0.03, 0.03, 0.028, 1.0),
    "Detail": (0.012, 0.02, 0.035, 1.0),
    "HouseColor": (0.75, 0.75, 0.75, 1.0),
}
BEVEL = 0.035

TURRET_RING = Vector((0.3, 0.0, 1.62))
TRUNNION = Vector((2.1, 0.0, 2.05))


class Part:
    """Collects bmesh geometry per material for one object."""

    def __init__(self, name):
        self.name = name
        self.meshes = {material: bmesh.new() for material in MATERIALS}

    def add(self, material, build, bevel=False):
        bm = bmesh.new()
        build(bm)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        if bevel:
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=BEVEL, segments=1, affect="EDGES", profile=0.5,
                            clamp_overlap=True)
        mesh = bpy.data.meshes.new("tmp")
        bm.to_mesh(mesh)
        bm.free()
        self.meshes[material].from_mesh(mesh)
        bpy.data.meshes.remove(mesh)

    def build(self, pivot, parent=None):
        """One object with a slot per material that has geometry, its origin at pivot (world space)."""
        mesh = bpy.data.meshes.new(self.name)
        combined = bmesh.new()
        slots = []
        for material, bm in self.meshes.items():
            if not bm.faces:
                continue
            index = len(slots)
            slots.append(material)
            for face in bm.faces:
                face.material_index = index
            scratch = bpy.data.meshes.new("tmp")
            bm.to_mesh(scratch)
            combined.from_mesh(scratch)
            bpy.data.meshes.remove(scratch)
            bm.free()
        bmesh.ops.translate(combined, verts=combined.verts, vec=-pivot)
        bmesh.ops.remove_doubles(combined, verts=combined.verts, dist=1e-5)
        combined.to_mesh(mesh)
        combined.free()
        for material in slots:
            mesh.materials.append(bpy.data.materials[material])
        for polygon in mesh.polygons:
            polygon.use_smooth = False
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        obj.location = pivot - parent.location if parent else pivot
        obj.parent = parent
        return obj


# ---- primitive builders (each returns a function that fills a bmesh) ----

def loft(bottom, top):
    """Closed solid between two polygons with the same vertex count, given as 3D points."""
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
    """A side profile (x, z) extruded across y0..y1."""
    return loft([(x, y0, z) for x, z in profile_xz], [(x, y1, z) for x, z in profile_xz])


def plan_prism(profile_xy, z0, z1, inset=0.0):
    """A top-down outline (x, y) extruded from z0 to z1, the top shrunk by inset towards its centre."""
    cx = sum(p[0] for p in profile_xy) / len(profile_xy)
    cy = sum(p[1] for p in profile_xy) / len(profile_xy)
    top = [(x - (x - cx) * inset, y - (y - cy) * inset, z1) for x, y in profile_xy]
    return loft([(x, y, z0) for x, y in profile_xy], top)


def box(x0, x1, y0, y1, z0, z1):
    return plan_prism([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1)


def cylinder(centre, radius, length, axis="X", segments=12, radius_end=None):
    rotations = {"X": Matrix.Rotation(math.pi / 2, 4, "Y"), "Y": Matrix.Rotation(math.pi / 2, 4, "X"),
                 "Z": Matrix.Identity(4)}

    def build(bm):
        bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments, radius1=radius,
                              radius2=radius if radius_end is None else radius_end, depth=length,
                              matrix=Matrix.Translation(centre) @ rotations[axis])
    return build


def offset_convex(points, thickness):
    """Insets a convex counter-clockwise polygon by thickness, each vertex moving to where its edges' insets meet."""
    count = len(points)
    lines = []
    for i in range(count):
        a, b = Vector(points[i]), Vector(points[(i + 1) % count])
        direction = (b - a).normalized()
        normal = Vector((-direction.y, direction.x))
        lines.append((a + normal * thickness, direction))
    inner = []
    for i in range(count):
        (p1, d1), (p2, d2) = lines[i - 1], lines[i]
        cross = d1.x * d2.y - d1.y * d2.x
        t = ((p2 - p1).x * d2.y - (p2 - p1).y * d2.x) / cross
        inner.append(tuple(p1 + d1 * t))
    return inner


def track_loop(profile_xz, thickness, y0, y1):
    """A closed track band: the side profile as a ring of the given thickness, extruded across y0..y1."""
    inner = offset_convex(profile_xz, thickness)

    def build(bm):
        rings = []
        for y in (y0, y1):
            rings.append(([bm.verts.new((x, y, z)) for x, z in profile_xz],
                          [bm.verts.new((x, y, z)) for x, z in inner]))
        count = len(profile_xz)
        (outer0, inner0), (outer1, inner1) = rings
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((outer0[i], outer0[j], outer1[j], outer1[i]))
            bm.faces.new((inner0[j], inner0[i], inner1[i], inner1[j]))
            bm.faces.new((outer0[j], outer0[i], inner0[i], inner0[j]))
            bm.faces.new((outer1[i], outer1[j], inner1[j], inner1[i]))
    return build


# ---- the tank ----

def build_hull(hull):
    # Lower hull between the tracks, with a raked lower front plate.
    hull.add("Body", side_prism([(-3.72, 0.42), (3.2, 0.42), (3.86, 1.06), (-3.78, 1.06)], -1.04, 1.04), bevel=True)
    # Upper hull over the tracks: long flat deck, steep sloped glacis, slightly raked rear plate.
    upper = [(-3.88, 1.04), (3.38, 1.04), (3.98, 1.24), (3.98, 1.34), (2.35, 1.62), (-3.72, 1.62), (-3.88, 1.48)]
    hull.add("Body", side_prism(upper, -1.86, 1.86), bevel=True)
    # Add-on armour wedge on the glacis.
    hull.add("Body", side_prism([(2.42, 1.63), (3.84, 1.38), (3.84, 1.46), (2.5, 1.71)], -1.25, 1.25), bevel=True)
    # Driver's hatch and periscopes.
    hull.add("Body", cylinder((2.05, -0.55, 1.66), 0.32, 0.08, "Z", 10))
    hull.add("Detail", box(2.32, 2.42, -0.85, -0.25, 1.62, 1.72))
    # Engine deck grilles and the rear exhaust.
    for i in range(6):
        x = -3.45 + i * 0.22
        hull.add("Dark", box(x, x + 0.12, -0.95, 0.95, 1.62, 1.665))
    hull.add("Dark", box(-3.92, -3.86, -1.2, 1.2, 1.12, 1.4))
    # Stowage bins on the rear fenders, tow points, headlight clusters.
    for sign in (1.0, -1.0):
        y0, y1 = sorted((sign * 1.25, sign * 1.82))
        hull.add("Body", box(-3.7, -2.75, y0, y1, 1.62, 1.92), bevel=True)
        hull.add("Detail", box(3.72, 3.92, sign * 1.5 - 0.12, sign * 1.5 + 0.12, 1.32, 1.42))
        hull.add("Dark", box(3.78, 4.02, sign * 0.75 - 0.1, sign * 0.75 + 0.1, 0.78, 0.98))

    for sign in (1.0, -1.0):
        inner_y, outer_y = sorted((sign * 1.02, sign * 1.82))
        centre_y = sign * 1.42
        # Wide track band around the running gear.
        profile = [(-3.74, 0.72), (-3.48, 0.24), (-3.08, 0.0), (3.08, 0.0), (3.56, 0.28), (3.88, 0.82),
                   (3.52, 1.04), (-3.44, 1.04)]
        hull.add("Dark", track_loop(profile, 0.11, inner_y, outer_y))
        # Seven road wheels with hubs, the drive sprocket at the rear and the idler at the front.
        for i in range(7):
            x = -2.58 + i * 0.86
            hull.add("Dark", cylinder((x, centre_y, 0.42), 0.36, 0.62, "Y", 12))
            hull.add("Body", cylinder((x, centre_y + sign * 0.33, 0.42), 0.15, 0.06, "Y", 8))
        hull.add("Dark", cylinder((-3.38, centre_y, 0.68), 0.34, 0.62, "Y", 10))
        hull.add("Body", cylinder((-3.38, centre_y + sign * 0.33, 0.68), 0.2, 0.06, "Y", 8))
        hull.add("Dark", cylinder((3.46, centre_y, 0.74), 0.3, 0.62, "Y", 10))
        # Side skirts in four bolted panels, the front one cut back over the idler, with a team stripe.
        skirt_y0, skirt_y1 = sorted((sign * 1.82, sign * 1.9))
        panels = [(-3.62, -1.86), (-1.82, -0.06), (-0.02, 1.74), (1.78, 3.58)]
        for index, (x0, x1) in enumerate(panels):
            if index == len(panels) - 1:
                shape = [(x0, 0.6), (x1 - 0.32, 0.6), (x1, 0.98), (x1, 1.2), (x0, 1.2)]
            else:
                shape = [(x0, 0.6), (x1, 0.6), (x1, 1.2), (x0, 1.2)]
            hull.add("Body", side_prism(shape, skirt_y0, skirt_y1), bevel=True)
        stripe_y0, stripe_y1 = sorted((sign * 1.9, sign * 1.925))
        hull.add("HouseColor", side_prism([(-3.5, 0.98), (3.3, 0.98), (3.3, 1.1), (-3.5, 1.1)], stripe_y0, stripe_y1))


def build_turret(turret):
    z0, z1 = 1.62, 2.56
    # Arrow-head wedge front with the mantlet set into its notch, long flat sides and a rear bustle.
    outline = [(1.95, 0.42), (2.45, 0.52), (1.15, 1.74), (-1.55, 1.74), (-2.05, 1.32), (-2.05, -1.32), (-1.55, -1.74),
               (1.15, -1.74), (2.45, -0.52), (1.95, -0.42)]
    turret.add("Body", plan_prism(outline, z0, z1, inset=0.04), bevel=True)
    # Turret ring skirt below the overhang.
    turret.add("Dark", cylinder((TURRET_RING.x, 0.0, 1.6), 1.25, 0.08, "Z", 16))
    # Mantlet.
    turret.add("Body", box(1.82, 2.38, -0.42, 0.42, 1.74, 2.38), bevel=True)
    # Team panels on the turret flanks.
    for sign in (1.0, -1.0):
        y0, y1 = sorted((sign * 1.70, sign * 1.775))
        turret.add("HouseColor", box(-1.45, 0.85, y0, y1, 1.92, 2.3))
    # Team band across the roof, so the player's colour reads from the RTS camera.
    turret.add("HouseColor", box(-1.62, -1.22, -1.5, 1.5, z1, z1 + 0.025))
    # Bustle basket: frame rails around the rear.
    turret.add("Dark", box(-2.5, -2.05, -1.3, -1.22, 1.9, 2.36))
    turret.add("Dark", box(-2.5, -2.05, 1.22, 1.3, 1.9, 2.36))
    turret.add("Dark", box(-2.5, -2.42, -1.3, 1.3, 1.9, 2.36))
    turret.add("Dark", box(-2.5, -2.05, -1.3, 1.3, 1.9, 1.95))
    turret.add("Body", box(-2.42, -2.08, -1.15, 1.15, 1.95, 2.18))
    # Gunner's primary sight, commander's independent sight, hatches.
    turret.add("Body", box(1.0, 1.55, 0.78, 1.18, z1 - 0.02, z1 + 0.2), bevel=True)
    turret.add("Detail", box(1.55, 1.58, 0.84, 1.12, z1 + 0.04, z1 + 0.16))
    turret.add("Body", cylinder((-0.1, -1.0, z1 + 0.18), 0.2, 0.36, "Z", 10))
    turret.add("Body", box(-0.38, 0.18, -1.24, -0.76, z1 + 0.36, z1 + 0.62), bevel=True)
    turret.add("Detail", box(0.18, 0.21, -1.18, -0.82, z1 + 0.42, z1 + 0.56))
    turret.add("Body", cylinder((-0.85, -0.85, z1 + 0.03), 0.36, 0.07, "Z", 10))
    turret.add("Body", cylinder((-0.6, 0.55, z1 + 0.03), 0.34, 0.07, "Z", 10))
    # Remote weapon station with its machine gun.
    turret.add("Body", cylinder((-0.6, 0.55, z1 + 0.14), 0.22, 0.16, "Z", 10))
    turret.add("Body", box(-0.85, -0.3, 0.38, 0.72, z1 + 0.22, z1 + 0.5), bevel=True)
    turret.add("Detail", cylinder((0.2, 0.55, z1 + 0.38), 0.035, 1.0, "X", 6))
    turret.add("Detail", box(-0.3, -0.2, 0.6, 0.7, z1 + 0.3, z1 + 0.44))
    # Smoke grenade launchers, four tubes a side, angled up and out.
    for sign in (1.0, -1.0):
        for i in range(4):
            centre = Vector((0.65 + i * 0.16, sign * 1.82, 2.34))
            tube = cylinder((0.0, 0.0, 0.0), 0.055, 0.3, "Y", 6)

            def place(bm, tube=tube, centre=centre, sign=sign):
                tube(bm)
                bmesh.ops.rotate(bm, verts=bm.verts, cent=(0.0, 0.0, 0.0),
                                 matrix=Matrix.Rotation(sign * math.radians(35), 3, "X"))
                bmesh.ops.translate(bm, verts=bm.verts, vec=centre)
            turret.add("Dark", place)
    # Antennas at the rear corners.
    for sign in (1.0, -1.0):
        turret.add("Dark", cylinder((-1.7, sign * 1.45, z1 + 0.75), 0.018, 1.5, "Z", 5))
        turret.add("Dark", cylinder((-1.7, sign * 1.45, z1 + 0.04), 0.06, 0.08, "Z", 6))


def build_gun(gun):
    z = TRUNNION.z
    # Thick 120 mm tube with a thermal sleeve, the bore evacuator and the muzzle collar.
    gun.add("Body", cylinder((2.4 + 0.5, 0.0, z), 0.2, 1.0, "X", 12))
    gun.add("Body", cylinder((4.65, 0.0, z), 0.15, 2.5, "X", 12, radius_end=0.14))
    gun.add("Body", cylinder((4.35, 0.0, z), 0.215, 0.8, "X", 12))
    gun.add("Body", cylinder((6.35, 0.0, z), 0.135, 1.3, "X", 12))
    gun.add("Dark", cylinder((7.05, 0.0, z), 0.165, 0.14, "X", 12))
    gun.add("Dark", cylinder((7.13, 0.0, z), 0.09, 0.03, "X", 10))


def make_materials():
    for name, colour in MATERIALS.items():
        material = bpy.data.materials.new(name)
        material.diffuse_color = colour
        material.roughness = 0.8
        if material.node_tree is None and hasattr(material, "use_nodes"):
            material.use_nodes = True
        principled = material.node_tree.nodes.get("Principled BSDF") if material.node_tree else None
        if principled:
            principled.inputs["Base Color"].default_value = colour
            principled.inputs["Roughness"].default_value = 0.8
            principled.inputs["Metallic"].default_value = 0.0


def render_previews(objects):
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.show_object_outline = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("Preview")
    scene.world = world
    world.color = (0.32, 0.3, 0.26)

    ground = bpy.data.meshes.new("Ground")
    bm = bmesh.new()
    box(-12, 12, -12, 12, -0.02, 0.0)(bm)
    bm.to_mesh(ground)
    bm.free()
    ground_material = bpy.data.materials.new("Ground")
    ground_material.diffuse_color = (0.25, 0.22, 0.16, 1.0)
    ground.materials.append(ground_material)
    ground_object = bpy.data.objects.new("Ground", ground)
    scene.collection.objects.link(ground_object)

    camera_data = bpy.data.cameras.new("Camera")
    camera = bpy.data.objects.new("Camera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    target = Vector((0.6, 0.0, 1.2))
    views = {
        "three_quarter": (Vector((11.0, -9.0, 6.0)), 50),
        "side": (Vector((0.6, -17.0, 1.6)), 50),
        "rts": (Vector((-9.0, -6.0, 17.0)), 45),
    }
    for name, (position, lens) in views.items():
        camera.location = position
        camera.rotation_euler = (target - position).to_track_quat("-Z", "Y").to_euler()
        camera_data.lens = lens
        scene.render.filepath = os.path.join(PREVIEW_DIR, f"usa_mbt_{name}.png")
        bpy.ops.render.render(write_still=True)

    bpy.data.objects.remove(ground_object)
    bpy.data.objects.remove(camera)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    make_materials()

    hull = Part("Hull")
    build_hull(hull)
    turret = Part("Turret")
    build_turret(turret)
    gun = Part("Gun")
    build_gun(gun)
    hull_object = hull.build(Vector((0.0, 0.0, 0.0)))
    turret_object = turret.build(TURRET_RING.copy())
    gun_object = gun.build(TRUNNION.copy(), parent=turret_object)
    bpy.context.view_layer.update()

    triangles = 0
    for obj in (hull_object, turret_object, gun_object):
        triangles += sum(len(polygon.vertices) - 2 for polygon in obj.data.polygons)
    print(f"build_usa_tank: {triangles} triangles")

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.splitext(OUTPUT)[0] + ".blend")
    render_previews((hull_object, turret_object, gun_object))
    bpy.ops.export_scene.gltf(filepath=OUTPUT, export_format="GLB", export_yup=True, export_apply=True,
                              export_materials="EXPORT", export_cameras=False, export_lights=False,
                              use_selection=False)
    print(f"build_usa_tank: wrote {OUTPUT}")


main()
