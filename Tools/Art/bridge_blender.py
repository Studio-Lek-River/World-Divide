"""Builds River Crossing's road bridge (#155) into Art/Shared/bridge/, the parts rivercrossing_greybox.py places twice:

  "D:/Program Files/Blender Foundation/Blender 5.1/blender.exe" -b --python Tools/Art/bridge_blender.py

A plain concrete two-lane girder bridge of 02_MAP_DESIGN.md "Bridges": SPANS spans of SPAN_LENGTH on two piers and two
abutments, DECK_WIDTH overall with PARAPET_WIDTH parapets, so CARRIAGEWAY between them. Each span is its own mesh and
actor, so a later issue can drop one (DESIGN_FOUNDATION section 6).

- SM_Bridge_Span: one span along X, its origin at the middle of its deck's top. An asphalt carriageway on a concrete deck
  slab over GIRDERS precast girders, a parapet with a coping along each edge.
- SM_Bridge_Pier: a pier cap across the bridge on two round columns, its origin at the middle of the cap's top, where the
  girders sit; the columns reach PIER_DEPTH down, through the water into the bed.
- SM_Bridge_Abutment: the end of the bridge, its origin at the deck's top where the span ends and +X away from the river:
  the seat the girders rest on, the backwall and approach slab the road crosses to the ramp, and a wing wall on each side
  that carries the parapet on along the ramp.

Material slots: Concrete and Asphalt. Every mesh carries UCX_ boxes, the collision the import keeps: the span's deck and
parapets, the pier's cap and columns, the abutment's seat and slab and wing walls. Metres in Blender, centimetres in the
FBX, +X forward and Z up, as the other generators export. It writes bridge.json (each mesh's slots, triangles and bounds
in Unreal centimetres), which setup_rivercrossing.py checks the import against. The output is committed; rerun only when
the bridge changes.
"""

import json
import os

import bmesh
import bpy
from mathutils import Matrix

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BRIDGE_DIR = os.path.join(PROJECT_DIR, "Art", "Shared", "bridge")

SPANS = 3
SPAN_LENGTH = 30.0
DECK_WIDTH = 14.0
PARAPET_WIDTH = 0.5
CARRIAGEWAY = DECK_WIDTH - 2.0 * PARAPET_WIDTH
PARAPET_HEIGHT = 1.0
# The parapet's coping overhangs its wall by COPING_OVERHANG on each side.
COPING_HEIGHT = 0.12
COPING_OVERHANG = 0.05
ASPHALT = 0.08
SLAB = 0.3
GIRDERS = 4
GIRDER_WIDTH = 0.6
GIRDER_DEPTH = 1.37
# Under the deck's top: the slab, then the girders down to DECK_DEPTH, where the piers and abutments carry them.
DECK_DEPTH = ASPHALT + SLAB + GIRDER_DEPTH
# The deck's fascia, the slab's outer edge, hangs this far below the slab.
FASCIA_DROP = 0.25

PIER_CAP = (1.6, DECK_WIDTH + 0.6, 1.2)
PIER_COLUMN_RADIUS = 0.7
PIER_COLUMN_SPACING = 8.0
PIER_DEPTH = 9.5
COLUMN_SEGMENTS = 16

# The abutment along X from the span's end: the seat (girders on it) toward the river, the backwall and approach slab
# toward the land, the wing walls on along the ramp; its stem reaches ABUTMENT_DEPTH under the deck's top.
SEAT = 1.2
BACKWALL = 1.5
WING_LENGTH = 6.0
ABUTMENT_DEPTH = 6.0
WING_DEPTH = 4.5


def material(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


SLOTS = ("Concrete", "Asphalt")


class Builder:
    """One mesh built from boxes and cylinders, each face on a slot, with its UCX_ collision boxes."""

    def __init__(self, name):
        self.name = name
        self.mesh = bmesh.new()
        self.uv = self.mesh.loops.layers.uv.new("UVMap")
        self.collision = []

    def _tag(self, verts, slot):
        faces = {face for vert in verts for face in vert.link_faces}
        for face in faces:
            face.material_index = SLOTS.index(slot)

    def box(self, low, high, slot="Concrete", collides=False):
        size = [h - l for l, h in zip(low, high)]
        center = [(l + h) / 2.0 for l, h in zip(low, high)]
        matrix = Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0))
        made = bmesh.ops.create_cube(self.mesh, size=1.0, matrix=matrix, calc_uvs=True)
        self._tag(made["verts"], slot)
        if collides:
            self.collision.append((low, high))

    def column(self, center, radius, top, bottom, slot="Concrete"):
        matrix = Matrix.Translation((center[0], center[1], (top + bottom) / 2.0))
        made = bmesh.ops.create_cone(self.mesh, cap_ends=True, cap_tris=True, segments=COLUMN_SEGMENTS, radius1=radius, radius2=radius,
                                     depth=top - bottom, matrix=matrix, calc_uvs=True)
        self._tag(made["verts"], slot)
        for face in {face for vert in made["verts"] for face in vert.link_faces}:
            face.smooth = len(face.verts) == 4
        self.collision.append(((center[0] - radius, center[1] - radius, bottom), (center[0] + radius, center[1] + radius, top)))

    def finish(self):
        mesh = bpy.data.meshes.new(self.name)
        # create_cube and create_cone face outward already; recalc_face_normals flipped the carriageway's top, which sits
        # flush on the slab, and showed the slab through it.
        self.mesh.to_mesh(mesh)
        self.mesh.free()
        for slot in SLOTS:
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


def parapet(builder, x0, x1, side, bottom=0.0, collides=True):
    """A parapet wall along X from x0 to x1 on the side (+1 or -1) of the deck, with its coping, from bottom up."""
    inner, outer = side * CARRIAGEWAY / 2.0, side * DECK_WIDTH / 2.0
    low_y, high_y = min(inner, outer), max(inner, outer)
    wall_top = PARAPET_HEIGHT - COPING_HEIGHT
    builder.box((x0, low_y, bottom), (x1, high_y, wall_top), collides=False)
    builder.box((x0, low_y - COPING_OVERHANG, wall_top), (x1, high_y + COPING_OVERHANG, PARAPET_HEIGHT), collides=False)
    if collides:
        builder.collision.append(((x0, low_y, bottom), (x1, high_y, PARAPET_HEIGHT)))


def span():
    builder = Builder("SM_Bridge_Span")
    half = SPAN_LENGTH / 2.0
    half_width = DECK_WIDTH / 2.0
    builder.box((-half, -CARRIAGEWAY / 2.0, -ASPHALT), (half, CARRIAGEWAY / 2.0, 0.0), slot="Asphalt")
    builder.box((-half, -half_width, -ASPHALT - SLAB), (half, half_width, -ASPHALT))
    for side in (-1.0, 1.0):
        # The fascia: the slab's edge beam, deeper than the slab, so the deck reads as a beam from the side.
        edge = side * half_width
        builder.box((-half, min(edge, edge - side * 0.35), -ASPHALT - SLAB - FASCIA_DROP),
                    (half, max(edge, edge - side * 0.35), -ASPHALT - SLAB))
        parapet(builder, -half, half, side)
    pitch = (DECK_WIDTH - 2.0) / GIRDERS
    for index in range(GIRDERS):
        y = -DECK_WIDTH / 2.0 + 1.0 + pitch * (index + 0.5)
        builder.box((-half, y - GIRDER_WIDTH / 2.0, -DECK_DEPTH), (half, y + GIRDER_WIDTH / 2.0, -ASPHALT - SLAB))
        # The bottom flange, wider than the web, at each girder's foot.
        builder.box((-half, y - GIRDER_WIDTH, -DECK_DEPTH), (half, y + GIRDER_WIDTH, -DECK_DEPTH + 0.25))
    # The deck's collision: one box from the girders' feet to the road, so nothing falls between them.
    builder.collision.insert(0, ((-half, -half_width, -DECK_DEPTH), (half, half_width, 0.0)))
    return builder.finish()


def pier():
    builder = Builder("SM_Bridge_Pier")
    length, width, height = PIER_CAP
    builder.box((-length / 2.0, -width / 2.0, -height), (length / 2.0, width / 2.0, 0.0), collides=True)
    for side in (-1.0, 1.0):
        builder.column((0.0, side * PIER_COLUMN_SPACING / 2.0), PIER_COLUMN_RADIUS, -height, -PIER_DEPTH)
    return builder.finish()


def abutment():
    builder = Builder("SM_Bridge_Abutment")
    half_width = DECK_WIDTH / 2.0 + 0.3
    # The stem and seat toward the river, the girders' ends resting on it.
    builder.box((-SEAT, -half_width, -ABUTMENT_DEPTH), (0.0, half_width, -DECK_DEPTH), collides=True)
    # The backwall and approach slab: flush with the road from the span's end to the ramp.
    builder.box((0.0, -half_width, -ABUTMENT_DEPTH), (BACKWALL, half_width, -ASPHALT))
    builder.box((0.0, -CARRIAGEWAY / 2.0, -ASPHALT), (BACKWALL, CARRIAGEWAY / 2.0, 0.0), slot="Asphalt")
    builder.collision.append(((0.0, -half_width, -ABUTMENT_DEPTH), (BACKWALL, half_width, 0.0)))
    for side in (-1.0, 1.0):
        # The wing wall under the parapet's line, from the seat's front to the end of the wing.
        inner, outer = side * CARRIAGEWAY / 2.0, side * half_width
        builder.box((-SEAT, min(inner, outer), -WING_DEPTH), (WING_LENGTH, max(inner, outer), 0.0))
        builder.collision.append(((BACKWALL, min(inner, outer), -WING_DEPTH), (WING_LENGTH, max(inner, outer), 0.0)))
        parapet(builder, -SEAT, WING_LENGTH, side)
    return builder.finish()


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
    os.makedirs(BRIDGE_DIR, exist_ok=True)
    records = {}
    for obj, hulls in (span(), pier(), abutment()):
        export([obj] + hulls, os.path.join(BRIDGE_DIR, f"{obj.name}.fbx"))
        records[obj.name] = {
            "fbx": f"{obj.name}.fbx",
            "slots": [slot.material.name for slot in obj.material_slots],
            "triangles": triangles(obj),
            "collision_boxes": len(hulls),
            "bounds": bounds(obj),
        }
        print(f"bridge_blender: {obj.name} {records[obj.name]['triangles']} triangles, {len(hulls)} collision boxes, "
              f"bounds {records[obj.name]['bounds']}")
    with open(os.path.join(BRIDGE_DIR, "bridge.json"), "w", encoding="utf-8", newline="\n") as file:
        json.dump({"about": "Generated by Tools/Art/bridge_blender.py; read by Tools/Editor/rivercrossing_greybox.py.",
                   "span_length_cm": SPAN_LENGTH * 100.0, "spans": SPANS, "deck_width_cm": DECK_WIDTH * 100.0,
                   "carriageway_cm": CARRIAGEWAY * 100.0, "deck_depth_cm": round(DECK_DEPTH * 100.0, 2),
                   "meshes": records}, file, indent=1)
        file.write("\n")


if __name__ == "__main__":
    main()
