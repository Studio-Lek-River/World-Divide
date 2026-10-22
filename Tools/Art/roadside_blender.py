"""Builds River Crossing's road signs and rural fences (#409) into Art/Shared/roadside/, the in-house roadside kit no CC0
source offers (European signs, a wooden pasture fence):

  python Tools/Art/roadside_textures.py
  "D:/Program Files/Blender Foundation/Blender 5.1/blender.exe" -b --python Tools/Art/roadside_blender.py

- SM_Sign_<sign>: one sign of roadside_textures.py on a galvanised post, its origin at the post's foot on the ground and the
  face toward +X. The plate is the sign's outline, PLATE_THICKNESS thick, its face UV-mapped to the sign's frame, so the
  texture T_Sign_<sign>.png lands on it as drawn. The place-name sign stands on two posts.
- SM_Fence_PostRail: one RAIL_BAY bay of a wooden post-and-rail fence along +X, the post at the origin and three rails to
  the next bay's post; SM_Fence_PostRail_End is the post alone, to close a run.
- SM_Fence_FieldWire: one WIRE_BAY bay of a pasture fence, a round wooden post and WIRES strands; SM_Fence_FieldWire_End
  the post alone.
- SM_PowerLine_Cable: one metre of overhead conductor along +X, CABLE_SEGMENTS rings, for a spline mesh to stretch and sag
  between the insulators of Poly Haven's poles (free_assets.json); it has no collision.

Material slots: Galvanised and Face (signs), Wood and Wire (fences), Wire (the cable). UCX_ boxes for the posts (and a fence bay's rails)
are the collision the import keeps. Metres in Blender, centimetres in the FBX, +X forward and Z up, as bridge_blender.py
exports. It writes roadside.json (each mesh's slots, face texture, triangles and bounds in Unreal centimetres), which
Tools/Editor/import_free_assets.py checks the import against. The output is committed; rerun only when the kit changes.
"""

import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roadside_textures as signs  # noqa: E402

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ROADSIDE_DIR = os.path.join(PROJECT_DIR, "Art", "Shared", "roadside")

POST_RADIUS = 0.03
POST_SEGMENTS = 12
# Posts reach this far into the ground, so a sign or fence on a slope shows no gap.
BURY = 0.4
PLATE_THICKNESS = 0.012
# The plate's lower edge above the ground; the place-name sign stands lower, on two posts PLACE_NAME_POSTS apart.
PLATE_BOTTOM = 2.0
PLACE_NAME_BOTTOM = 1.6
PLACE_NAME_POSTS = 0.8

RAIL_BAY = 3.0
RAIL_POST = 0.12
RAIL_POST_HEIGHT = 1.25
RAILS = (0.35, 0.7, 1.05)
RAIL_SECTION = (0.04, 0.12)

WIRE_BAY = 3.0
WIRE_POST_RADIUS = 0.06
WIRE_POST_HEIGHT = 1.15
WIRES = (0.3, 0.55, 0.8, 1.05)
WIRE_THICKNESS = 0.006

CABLE_RADIUS = 0.008
CABLE_SIDES = 6
CABLE_SEGMENTS = 8


def material(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


class Builder:
    """One mesh built from boxes, cylinders and plates, each face on a slot, with its UCX_ collision boxes."""

    def __init__(self, name, slots):
        self.name = name
        self.slots = slots
        self.mesh = bmesh.new()
        self.uv = self.mesh.loops.layers.uv.new("UVMap")
        self.collision = []

    def _tag(self, faces, slot):
        for face in faces:
            face.material_index = self.slots.index(slot)

    def box(self, low, high, slot, collides=False):
        size = [h - l for l, h in zip(low, high)]
        center = [(l + h) / 2.0 for l, h in zip(low, high)]
        made = bmesh.ops.create_cube(self.mesh, size=1.0, matrix=Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0)),
                                     calc_uvs=True)
        self._tag({face for vert in made["verts"] for face in vert.link_faces}, slot)
        if collides:
            self.collision.append((low, high))

    def post(self, center, radius, top, slot, segments=POST_SEGMENTS):
        bottom = -BURY
        made = bmesh.ops.create_cone(self.mesh, cap_ends=True, cap_tris=True, segments=segments, radius1=radius, radius2=radius,
                                     depth=top - bottom, matrix=Matrix.Translation((center[0], center[1], (top + bottom) / 2.0)),
                                     calc_uvs=True)
        faces = {face for vert in made["verts"] for face in vert.link_faces}
        self._tag(faces, slot)
        for face in faces:
            face.smooth = len(face.verts) == 4
        self.collision.append(((center[0] - radius, center[1] - radius, 0.0), (center[0] + radius, center[1] + radius, top)))

    def plate(self, outline, frame, front_x, center_z):
        """The sign's plate: its outline (y, z around the frame's centre) as a face toward +X mapped to the frame, a back
        and the edge, both Galvanised."""
        width, height = frame
        front = [self.mesh.verts.new((front_x, y, center_z + z)) for y, z in outline]
        back = [self.mesh.verts.new((front_x - PLATE_THICKNESS, y, center_z + z)) for y, z in outline]
        # The outline runs counter-clockwise seen from +X, so the face's normal points +X.
        face = self.mesh.faces.new(front)
        self._tag([face], "Face")
        for loop, (y, z) in zip(face.loops, outline):
            loop[self.uv].uv = ((y + width / 2.0) / width, (z + height / 2.0) / height)
        rear = self.mesh.faces.new(list(reversed(back)))
        sides = [self.mesh.faces.new((back[i], back[(i + 1) % len(back)], front[(i + 1) % len(front)], front[i]))
                 for i in range(len(front))]
        self._tag([rear] + sides, "Galvanised")

    def finish(self):
        mesh = bpy.data.meshes.new(self.name)
        self.mesh.to_mesh(mesh)
        self.mesh.free()
        for slot in self.slots:
            mesh.materials.append(material(slot))
        mesh.update()
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.collection.objects.link(obj)
        hulls = []
        for index, (low, high) in enumerate(self.collision):
            built = bmesh.new()
            size = [h - l for l, h in zip(low, high)]
            center = [(l + h) / 2.0 for l, h in zip(low, high)]
            bmesh.ops.create_cube(built, size=1.0, matrix=Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0)))
            hull_mesh = bpy.data.meshes.new(f"UCX_{self.name}_{index:02d}")
            built.to_mesh(hull_mesh)
            built.free()
            hull = bpy.data.objects.new(hull_mesh.name, hull_mesh)
            bpy.context.collection.objects.link(hull)
            hulls.append(hull)
        return obj, hulls


def sign(name):
    builder = Builder(f"SM_Sign_{name}", ["Galvanised", "Face"])
    outline = signs.outline(name)
    frame = signs.FRAMES[name][0]
    low, high = min(z for _, z in outline), max(z for _, z in outline)
    bottom = PLACE_NAME_BOTTOM if name == "place_name" else PLATE_BOTTOM
    center_z = bottom - low
    top = center_z + high - 0.03
    posts = (-PLACE_NAME_POSTS / 2.0, PLACE_NAME_POSTS / 2.0) if name == "place_name" else (0.0,)
    for y in posts:
        builder.post((0.0, y), POST_RADIUS, top, "Galvanised")
    builder.plate(outline, frame, POST_RADIUS + 0.004 + PLATE_THICKNESS, center_z)
    return builder.finish(), f"T_Sign_{name}.png"


def post_rail(end=False):
    builder = Builder("SM_Fence_PostRail_End" if end else "SM_Fence_PostRail", ["Wood"])
    half = RAIL_POST / 2.0
    builder.box((-half, -half, -BURY), (half, half, RAIL_POST_HEIGHT), "Wood")
    builder.collision.append(((-half, -half, 0.0), (half, half, RAIL_POST_HEIGHT)))
    if not end:
        depth, tall = RAIL_SECTION
        for z in RAILS:
            # Nailed to the posts' +Y face, from this post's middle to the next one's.
            builder.box((0.0, half, z - tall / 2.0), (RAIL_BAY, half + depth, z + tall / 2.0), "Wood")
        builder.collision.append(((half, half, RAILS[0] - tall / 2.0), (RAIL_BAY - half, half + depth, RAILS[-1] + tall / 2.0)))
    return builder.finish(), None


def field_wire(end=False):
    builder = Builder("SM_Fence_FieldWire_End" if end else "SM_Fence_FieldWire", ["Wood", "Wire"])
    builder.post((0.0, 0.0), WIRE_POST_RADIUS, WIRE_POST_HEIGHT, "Wood", 10)
    if not end:
        y = WIRE_POST_RADIUS + WIRE_THICKNESS / 2.0
        for z in WIRES:
            builder.box((0.0, y - WIRE_THICKNESS / 2.0, z - WIRE_THICKNESS / 2.0),
                        (WIRE_BAY, y + WIRE_THICKNESS / 2.0, z + WIRE_THICKNESS / 2.0), "Wire")
    return builder.finish(), None


def power_line_cable():
    builder = Builder("SM_PowerLine_Cable", ["Wire"])
    rings = []
    for step in range(CABLE_SEGMENTS + 1):
        x = step / CABLE_SEGMENTS
        rings.append([builder.mesh.verts.new((x, CABLE_RADIUS * math.cos(2.0 * math.pi * side / CABLE_SIDES),
                                              CABLE_RADIUS * math.sin(2.0 * math.pi * side / CABLE_SIDES)))
                      for side in range(CABLE_SIDES)])
    faces = []
    for step in range(CABLE_SEGMENTS):
        for side in range(CABLE_SIDES):
            a, b = rings[step][side], rings[step][(side + 1) % CABLE_SIDES]
            c, d = rings[step + 1][(side + 1) % CABLE_SIDES], rings[step + 1][side]
            face = builder.mesh.faces.new((a, b, c, d))
            face.smooth = True
            for loop, uv in zip(face.loops, ((step / CABLE_SEGMENTS, side / CABLE_SIDES), (step / CABLE_SEGMENTS, (side + 1) / CABLE_SIDES),
                                             ((step + 1) / CABLE_SEGMENTS, (side + 1) / CABLE_SIDES), ((step + 1) / CABLE_SEGMENTS, side / CABLE_SIDES))):
                loop[builder.uv].uv = uv
            faces.append(face)
    builder._tag(faces, "Wire")
    return builder.finish(), None


def triangles(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def bounds(obj):
    """(min, max) in Unreal centimetres: Blender's metres with Y mirrored back."""
    xs, ys, zs = zip(*(vertex.co for vertex in obj.data.vertices))
    return [[round(min(xs) * 100.0, 2) + 0.0, round(-max(ys) * 100.0, 2) + 0.0, round(min(zs) * 100.0, 2) + 0.0],
            [round(max(xs) * 100.0, 2) + 0.0, round(-min(ys) * 100.0, 2) + 0.0, round(max(zs) * 100.0, 2) + 0.0]]


def export(objects, path):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, use_mesh_modifiers=False,
                             mesh_smooth_type="FACE", use_tspace=True, colors_type="NONE", apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                             bake_space_transform=False, add_leaf_bones=False, bake_anim=False, use_custom_props=False,
                             path_mode="AUTO")


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(ROADSIDE_DIR, exist_ok=True)
    built = [sign(name) for name in signs.FRAMES] + [post_rail(), post_rail(True), field_wire(), field_wire(True),
                                                      power_line_cable()]
    records = {}
    for (obj, hulls), face in built:
        export([obj] + hulls, os.path.join(ROADSIDE_DIR, f"{obj.name}.fbx"))
        records[obj.name] = {
            "fbx": f"{obj.name}.fbx",
            "slots": [slot.material.name for slot in obj.material_slots],
            "face": face,
            "triangles": triangles(obj),
            "collision_boxes": len(hulls),
            "bounds": bounds(obj),
        }
        print(f"roadside_blender: {obj.name} {records[obj.name]['triangles']} triangles, {len(hulls)} collision boxes, "
              f"bounds {records[obj.name]['bounds']}")
    with open(os.path.join(ROADSIDE_DIR, "roadside.json"), "w", encoding="utf-8", newline="\n") as file:
        json.dump({"about": "Generated by Tools/Art/roadside_blender.py; read by Tools/Editor/import_free_assets.py.",
                   "rail_bay_cm": RAIL_BAY * 100.0, "wire_bay_cm": WIRE_BAY * 100.0, "meshes": records}, file, indent=1)
        file.write("\n")


if __name__ == "__main__":
    main()
