"""Builds the placeholder rifleman (original Lek River Studio work) from code and exports it as
Art/Source/Placeholder/Original/infantry_soldier.glb, with the .blend next to it for hand edits and preview renders in
Saved/ArtPreviews.

  blender -b --python Tools/Art/build_soldier_blender.py

A standing soldier at real proportions (WORLD_SCALE_STANDARD.md: 1.8 m tall, about 0.5 m across the shoulders), holding
his rifle across his chest, so a uniform fit to 180 cm keeps him inside the infantry nav agent (radius 40 cm). Low-poly
and flat-shaded with the colour-only materials placeholder_art.py imports, as the tank: Body (uniform), Dark (boots,
gloves, rifle), Detail (face, goggles) and HouseColor (helmet band and shoulder patches, tinted in the player's colour).
He faces +X with his boots on the ground at z = 0, in metres. One object.
"""

import os

import bmesh
import bpy
from mathutils import Matrix, Vector

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUTPUT = os.path.join(PROJECT_DIR, "Art", "Source", "Placeholder", "Original", "infantry_soldier.glb")
PREVIEW_DIR = os.path.join(PROJECT_DIR, "Saved", "ArtPreviews")

# Linear base colours. HouseColor is light so the team tint keeps its brightness.
MATERIALS = {
    "Body": (0.11, 0.115, 0.07, 1.0),
    "Dark": (0.025, 0.025, 0.022, 1.0),
    "Detail": (0.33, 0.22, 0.15, 1.0),
    "HouseColor": (0.75, 0.75, 0.75, 1.0),
}


def box(x0, x1, y0, y1, z0, z1, inset=0.0):
    """A box from (x0, y0, z0) to (x1, y1, z1), its top shrunk by inset towards its centre."""
    def build(bm):
        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        bottom = [bm.verts.new((x, y, z0)) for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
        top = [bm.verts.new((x - (x - cx) * inset, y - (y - cy) * inset, z1))
               for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
        bm.faces.new(list(reversed(bottom)))
        bm.faces.new(top)
        for i in range(4):
            j = (i + 1) % 4
            bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    return build


def cylinder(centre, radius, length, axis="Z", segments=10, radius_end=None):
    rotations = {"X": Matrix.Rotation(1.5707963, 4, "Y"), "Y": Matrix.Rotation(1.5707963, 4, "X"), "Z": Matrix.Identity(4)}

    def build(bm):
        bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments, radius1=radius,
                              radius2=radius if radius_end is None else radius_end, depth=length,
                              matrix=Matrix.Translation(centre) @ rotations[axis])
    return build


def sphere(centre, radius, scale=(1.0, 1.0, 1.0)):
    def build(bm):
        bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=radius,
                                  matrix=Matrix.Translation(centre) @ Matrix.Diagonal((*scale, 1.0)))
    return build


def build_soldier(add):
    # Boots and legs, slightly apart.
    for sign in (1.0, -1.0):
        y = sign * 0.1
        add("Dark", box(-0.07, 0.15, y - 0.06, y + 0.06, 0.0, 0.11))
        add("Body", box(-0.07, 0.08, y - 0.07, y + 0.07, 0.11, 0.52, inset=-0.05))
        add("Dark", box(-0.06, 0.09, y - 0.075, y + 0.075, 0.5, 0.56))
        add("Body", box(-0.08, 0.09, y - 0.085, y + 0.085, 0.56, 0.94, inset=0.04))
    # Hips, torso with the plate carrier, and the backpack.
    add("Body", box(-0.1, 0.1, -0.19, 0.19, 0.88, 1.04))
    add("Body", box(-0.11, 0.12, -0.21, 0.21, 1.02, 1.48, inset=0.08))
    add("Dark", box(0.0, 0.14, -0.17, 0.17, 1.06, 1.4, inset=0.05))
    add("Body", box(-0.24, -0.1, -0.16, 0.16, 1.08, 1.42, inset=0.1))
    # Arms hanging close in, the forearms forward to hold the rifle.
    for sign in (1.0, -1.0):
        y = sign * 0.215
        add("HouseColor", box(-0.065, 0.065, y - 0.05, y + 0.05, 1.32, 1.42))
        add("Body", box(-0.06, 0.06, y - 0.045, y + 0.045, 1.08, 1.4))
        add("Body", box(-0.02, 0.2, y - 0.04 - sign * 0.06, y + 0.04 - sign * 0.06, 1.04, 1.13))
        add("Dark", box(0.18, 0.24, y - 0.04 - sign * 0.09, y + 0.04 - sign * 0.09, 1.05, 1.12))
    # Neck, head, face and goggles, and the helmet with its team band.
    add("Body", cylinder((0.0, 0.0, 1.52), 0.055, 0.1))
    add("Detail", sphere((0.01, 0.0, 1.62), 0.1, (1.0, 0.9, 1.15)))
    add("Detail", box(0.08, 0.11, -0.07, 0.07, 1.62, 1.66))
    add("Body", sphere((0.0, 0.0, 1.69), 0.135, (1.08, 1.0, 0.82)))
    add("HouseColor", cylinder((0.0, 0.0, 1.68), 0.138, 0.035, segments=12))
    # The rifle across the chest, muzzle forward and to the left.
    add("Dark", box(0.12, 0.26, -0.03, 0.03, 1.08, 1.18))
    add("Dark", box(0.26, 0.42, -0.015, 0.015, 1.13, 1.16))
    add("Dark", box(0.02, 0.12, -0.025, 0.025, 1.05, 1.15))


def make_materials():
    for name, colour in MATERIALS.items():
        material = bpy.data.materials.new(name)
        material.diffuse_color = colour
        material.roughness = 0.85
        if material.node_tree is None and hasattr(material, "use_nodes"):
            material.use_nodes = True
        principled = material.node_tree.nodes.get("Principled BSDF") if material.node_tree else None
        if principled:
            principled.inputs["Base Color"].default_value = colour
            principled.inputs["Roughness"].default_value = 0.85
            principled.inputs["Metallic"].default_value = 0.0


def build_object():
    meshes = {material: bmesh.new() for material in MATERIALS}

    def add(material, build):
        bm = bmesh.new()
        build(bm)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        scratch = bpy.data.meshes.new("tmp")
        bm.to_mesh(scratch)
        bm.free()
        meshes[material].from_mesh(scratch)
        bpy.data.meshes.remove(scratch)

    build_soldier(add)
    mesh = bpy.data.meshes.new("Soldier")
    combined = bmesh.new()
    slots = []
    for material, bm in meshes.items():
        if not bm.faces:
            continue
        for face in bm.faces:
            face.material_index = len(slots)
        slots.append(material)
        scratch = bpy.data.meshes.new("tmp")
        bm.to_mesh(scratch)
        combined.from_mesh(scratch)
        bpy.data.meshes.remove(scratch)
        bm.free()
    combined.to_mesh(mesh)
    combined.free()
    for material in slots:
        mesh.materials.append(bpy.data.materials[material])
    for polygon in mesh.polygons:
        polygon.use_smooth = False
    obj = bpy.data.objects.new("Soldier", mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def render_previews():
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.render.resolution_x, scene.render.resolution_y = 800, 1000
    world = bpy.data.worlds.new("Preview")
    scene.world = world
    world.color = (0.32, 0.3, 0.26)
    camera_data = bpy.data.cameras.new("Camera")
    camera = bpy.data.objects.new("Camera", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    target = Vector((0.0, 0.0, 0.9))
    for name, position in {"three_quarter": Vector((3.0, -2.5, 1.8)), "front": Vector((4.0, 0.0, 1.0))}.items():
        camera.location = position
        camera.rotation_euler = (target - position).to_track_quat("-Z", "Y").to_euler()
        camera_data.lens = 50
        scene.render.filepath = os.path.join(PREVIEW_DIR, f"infantry_soldier_{name}.png")
        bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(camera)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    make_materials()
    obj = build_object()
    bpy.context.view_layer.update()
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    size = [max(c[i] for c in corners) - min(c[i] for c in corners) for i in range(3)]
    triangles = sum(len(polygon.vertices) - 2 for polygon in obj.data.polygons)
    print(f"build_soldier: {triangles} triangles, {size[0]:.3f} x {size[1]:.3f} x {size[2]:.3f} m")

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.splitext(OUTPUT)[0] + ".blend")
    render_previews()
    bpy.ops.export_scene.gltf(filepath=OUTPUT, export_format="GLB", export_yup=True, export_apply=True,
                              export_materials="EXPORT", export_cameras=False, export_lights=False,
                              use_selection=False)
    print(f"build_soldier: wrote {OUTPUT}")


main()
