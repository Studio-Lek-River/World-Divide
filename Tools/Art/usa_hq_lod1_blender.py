"""Makes the hand-made LOD1 of the USA HQ's moving parts the engine's reduction breaks (#406): from the delivery's FBX in
Art/USA/buildings/usa_hq/fbx, it writes fbx/<name>_LOD1.fbx with the same slots, which usa_hq_recipe.py's HAND_LOD1 hands
to setup_testmap.py:

  blender -b --python Tools/Art/usa_hq_lod1_blender.py

- SM_HQ19_WingDish_Elevation: the engine's 400 triangles turned the dish into a blob without its back frame at the 260 m
  switch. Each straight strut (the back frame's tubes and the feed's legs) becomes an open six-sided prism along it, and
  the reflector (front and back apart), the feed and the seals are collapsed to a share each (SHARES), within the HQ's 40,000 LOD1 triangles.

Everything is in the delivery's frame, read and written as usa_hq_game_blender.py does.
"""

import math
import os

import bmesh
import bpy
import numpy
from mathutils import Matrix, Vector

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
FBX_DIR = os.path.join(PROJECT_DIR, "Art", "USA", "buildings", "usa_hq", "fbx")

# Per mesh, the triangles each slot's pieces keep: a share for the collapse, or PRISM for straight tubes rebuilt as prisms.
# SIDED slots are thin shells, collapsed front and back apart.
PRISM = "prism"
SHARES = {
    "SM_HQ19_WingDish_Elevation": {
        "HQ17_FramePaint": 210,
        "HQ17_Steel": 120,
        "HQ17_Seals": 20,
        "HQ19_DuctGalvanized": PRISM,
        "HQ17_DarkStructure": PRISM,
    },
}
SIDED = {"HQ17_FramePaint"}
PRISM_SIDES = 6
# Vertices closer than this (m) are welded before the collapse: the delivery splits them at every seam.
WELD_DISTANCE = 0.0001
# Edges whose faces meet at more than this (radians, 35 degrees) shade sharp, the rest smooth, as in the game shell.
SHARP_ANGLE = 0.6109


def select_only(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def import_source(name):
    bpy.ops.import_scene.fbx(filepath=os.path.join(FBX_DIR, f"{name}.fbx"))
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if len(meshes) != 1:
        raise RuntimeError(f"{name}.fbx holds {len(meshes)} meshes, not one")
    obj = meshes[0]
    for other in set(bpy.data.objects) - {obj}:
        bpy.data.objects.remove(other)
    select_only(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def split_by_slot(obj):
    """{slot: object} of obj's faces per material slot, each its own object with all of obj's slots."""
    result = {}
    for index, slot in enumerate(obj.material_slots):
        part = obj.copy()
        part.data = obj.data.copy()
        bpy.context.collection.objects.link(part)
        bm = bmesh.new()
        bm.from_mesh(part.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index != index], context="FACES")
        bm.to_mesh(part.data)
        bm.free()
        part.name = f"{obj.name}_{slot.material.name}"
        result[slot.material.name] = part
    return result


def collapse(obj, target):
    select_only(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=WELD_DISTANCE)
    bpy.ops.object.mode_set(mode="OBJECT")
    modifier = obj.modifiers.new("Decimate", "DECIMATE")
    modifier.decimate_type = "COLLAPSE"
    modifier.use_collapse_triangulate = True
    modifier.ratio = min(1.0, target / max(1, triangles(obj)))
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def collapse_sides(obj, target):
    """Collapses a thin shell (the reflector) front and back apart, half of target each: collapsed as one, its back pokes
    through its front in dark patches."""
    centres = numpy.array([p.center[:] for p in obj.data.polygons])
    axis = numpy.linalg.eigh(numpy.cov((centres - centres.mean(0)).T))[1][:, 0]
    back = obj.copy()
    back.data = obj.data.copy()
    bpy.context.collection.objects.link(back)
    for part, keep_front in ((obj, True), (back, False)):
        bm = bmesh.new()
        bm.from_mesh(part.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if (numpy.dot(f.normal[:], axis) > 0.0) != keep_front],
                         context="FACES")
        bm.to_mesh(part.data)
        bm.free()
        collapse(part, target // 2)
    bpy.ops.object.select_all(action="DESELECT")
    back.select_set(True)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.join()


def prisms(obj):
    """Each piece of obj (a straight tube) as an open PRISM_SIDES-sided prism along its longest axis, as wide as the tube,
    with a wrapped UV in metres."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    out = bmesh.new()
    uv_out = out.loops.layers.uv.new(obj.data.uv_layers.active.name)
    material = next(iter(bm.faces)).material_index
    seen = set()
    for start in bm.verts:
        if start in seen:
            continue
        piece, stack = [], [start]
        seen.add(start)
        while stack:
            vert = stack.pop()
            piece.append(vert.co.copy())
            for edge in vert.link_edges:
                other = edge.other_vert(vert)
                if other not in seen:
                    seen.add(other)
                    stack.append(other)
        centre = sum(piece, Vector()) / len(piece)
        covariance = Matrix(((0.0,) * 3,) * 3)
        for co in piece:
            d = co - centre
            covariance += Matrix(((d.x * d.x, d.x * d.y, d.x * d.z), (d.y * d.x, d.y * d.y, d.y * d.z),
                                  (d.z * d.x, d.z * d.y, d.z * d.z)))
        axis = Vector((1.0, 0.0, 0.0))
        for _ in range(32):
            axis = (covariance @ axis).normalized()
        along = [(co - centre).dot(axis) for co in piece]
        low, high = min(along), max(along)
        radius = sum((co - centre - axis * a).length for co, a in zip(piece, along)) / len(piece)
        side = axis.orthogonal().normalized()
        other = axis.cross(side)
        rings = []
        for end in (low, high):
            rings.append([out.verts.new(centre + axis * end + (side * math.cos(t) + other * math.sin(t)) * radius)
                          for t in (2.0 * math.pi * k / PRISM_SIDES for k in range(PRISM_SIDES))])
        length = high - low
        for k in range(PRISM_SIDES):
            n = (k + 1) % PRISM_SIDES
            face = out.faces.new((rings[0][k], rings[0][n], rings[1][n], rings[1][k]))
            face.material_index = material
            face.smooth = True
            u0, u1 = k / PRISM_SIDES * 2.0 * math.pi * radius, (k + 1) / PRISM_SIDES * 2.0 * math.pi * radius
            for loop, uv in zip(face.loops, ((u0, 0.0), (u1, 0.0), (u1, length), (u0, length))):
                loop[uv_out].uv = uv
    bm.free()
    bmesh.ops.triangulate(out, faces=out.faces[:])
    out.to_mesh(obj.data)
    out.free()


def make_lod1(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    source = import_source(name)
    order = [slot.material.name for slot in source.material_slots]
    if set(order) != set(SHARES[name]):
        raise RuntimeError(f"{name}: slots {order}, SHARES names {sorted(SHARES[name])}")
    parts = split_by_slot(source)
    for slot, share in SHARES[name].items():
        if share == PRISM:
            prisms(parts[slot])
        elif slot in SIDED:
            collapse_sides(parts[slot], share)
        else:
            collapse(parts[slot], share)
    bpy.data.objects.remove(source)
    pieces = [parts[slot] for slot in order]
    bpy.ops.object.select_all(action="DESELECT")
    for piece in pieces:
        piece.select_set(True)
    bpy.context.view_layer.objects.active = pieces[0]
    bpy.ops.object.join()
    lod1 = bpy.context.view_layer.objects.active
    lod1.name = f"{name}_LOD1"
    lod1.data.shade_smooth()
    lod1.data.set_sharp_from_angle(angle=SHARP_ANGLE)
    select_only(lod1)
    path = os.path.join(FBX_DIR, f"{name}_LOD1.fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, use_mesh_modifiers=False,
                             mesh_smooth_type="FACE", use_tspace=True, colors_type="NONE", apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                             bake_space_transform=False, add_leaf_bones=False, bake_anim=False, use_custom_props=False,
                             path_mode="AUTO")
    print(f"usa_hq_lod1: {name}_LOD1 {triangles(lod1)} triangles, slots {[s.material.name for s in lod1.material_slots]}",
          flush=True)


if __name__ == "__main__":
    for mesh_name in SHARES:
        make_lod1(mesh_name)
