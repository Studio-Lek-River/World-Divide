"""Builds the shared building props of #351 into Art/Shared/props/, for any building's layout to place as parts:

  blender -b --python Tools/Art/flag_beacon_blender.py

- SM_Flag_Cloth, the waving flag's cloth: a flat grid of FLAG_COLUMNS x FLAG_ROWS quads, 1 x 1 m, its origin at the hoist's
  top, +X along the fly and -Z down the hoist, facing +-Y. UV0 runs U 0 to 1 from hoist to fly and V 0 to 1 from top to
  bottom, which M_Flag_Cloth pins and waves by and maps the flag's texture with. A building scales it: Z is the hoist's
  height, and X its height times the flag's aspect (ARTSBuilding::ApplyParts, DA_FlagTypes).
- SM_Beacon_Obstruction, a red aviation obstruction beacon for a mast's tip: a dark base and a dome lens, BEACON_HEIGHT tall,
  its origin at the middle of its base. The housing's UVs sit at U < 0.5 and the lens's at U > 0.5, which is how
  M_Beacon_Obstruction tells them apart with one material slot.

It writes the FBX and props.json (each mesh's slots, triangles and bounds in Unreal centimetres), which
Tools/Art/usa_hq_recipe.py reads. Metres in Blender, centimetres in the FBX, +X forward and Z up, as the other generators
export. The output is committed; rerun only when a prop changes.
"""

import json
import math
import os

import bmesh
import bpy
from mathutils import Matrix

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PROPS_DIR = os.path.join(PROJECT_DIR, "Art", "Shared", "props")

# 40 x 24 quads: about ten samples a metre along a 4 m flag for the wave and its flutter, under the 2,000 triangles below
# which a mesh keeps one LOD (PERFORMANCE_GUIDELINES 20).
FLAG_COLUMNS = 40
FLAG_ROWS = 24

# The beacon: a base BASE_RADIUS across and BASE_HEIGHT tall, under a dome lens LENS_RADIUS across and LENS_HEIGHT tall,
# about 0.45 x 0.55 m, so it reads at the default zoom (150 m) as a few bright pixels.
BASE_RADIUS = 0.11
BASE_HEIGHT = 0.12
LENS_RADIUS = 0.22
LENS_HEIGHT = 0.44
BEACON_SEGMENTS = 16
LENS_RINGS = 8
BEACON_HEIGHT = BASE_HEIGHT + LENS_HEIGHT
HOUSING_U = 0.25
LENS_U = 0.75


def material(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


def new_object(name, mesh):
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def flag_cloth():
    mesh = bpy.data.meshes.new("SM_Flag_Cloth")
    vertices = [(column / FLAG_COLUMNS, 0.0, -row / FLAG_ROWS)
                for row in range(FLAG_ROWS + 1) for column in range(FLAG_COLUMNS + 1)]
    faces = []
    for row in range(FLAG_ROWS):
        for column in range(FLAG_COLUMNS):
            a = row * (FLAG_COLUMNS + 1) + column
            faces.append((a, a + FLAG_COLUMNS + 1, a + FLAG_COLUMNS + 2, a + 1))
    mesh.from_pydata(vertices, [], faces)
    uv = mesh.uv_layers.new(name="UVMap")
    for loop in mesh.loops:
        x, _, z = mesh.vertices[loop.vertex_index].co
        # Blender's V runs up and the FBX import flips it: V 0 at the top in Unreal.
        uv.data[loop.index].uv = (x, 1.0 + z)
    mesh.materials.append(material("Flag"))
    mesh.update()
    return new_object("SM_Flag_Cloth", mesh)


def beacon():
    mesh = bpy.data.meshes.new("SM_Beacon_Obstruction")
    built = bmesh.new()
    bmesh.ops.create_cone(built, cap_ends=True, segments=BEACON_SEGMENTS, radius1=BASE_RADIUS, radius2=BASE_RADIUS,
                          depth=BASE_HEIGHT, matrix=Matrix.Translation((0.0, 0.0, BASE_HEIGHT / 2.0)))
    housing = set(built.faces)
    # The lens: the upper half of a sphere stretched to LENS_HEIGHT, its open rim a little inside the base's top.
    rim = []
    for ring in range(LENS_RINGS + 1):
        angle = 0.5 * math.pi * ring / LENS_RINGS
        radius, height = LENS_RADIUS * math.cos(angle), LENS_HEIGHT * math.sin(angle)
        if ring == LENS_RINGS:
            rim.append([built.verts.new((0.0, 0.0, BASE_HEIGHT + height))])
            continue
        rim.append([built.verts.new((radius * math.cos(2 * math.pi * s / BEACON_SEGMENTS),
                                     radius * math.sin(2 * math.pi * s / BEACON_SEGMENTS), BASE_HEIGHT + height))
                    for s in range(BEACON_SEGMENTS)])
    for ring in range(LENS_RINGS):
        low, high = rim[ring], rim[ring + 1]
        for s in range(BEACON_SEGMENTS):
            t = (s + 1) % BEACON_SEGMENTS
            if len(high) == 1:
                built.faces.new((low[s], low[t], high[0]))
            else:
                built.faces.new((low[s], low[t], high[t], high[s]))
    built.faces.new(list(reversed(rim[0])))
    bmesh.ops.recalc_face_normals(built, faces=list(built.faces))
    uv = built.loops.layers.uv.new("UVMap")
    for face in built.faces:
        u = HOUSING_U if face in housing else LENS_U
        face.smooth = face not in housing
        for loop in face.loops:
            loop[uv].uv = (u, 0.5)
    built.to_mesh(mesh)
    built.free()
    mesh.materials.append(material("Beacon"))
    mesh.update()
    return new_object("SM_Beacon_Obstruction", mesh)


def triangles(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def bounds(obj):
    """(min, max) in Unreal centimetres: Blender's metres with Y mirrored back."""
    xs, ys, zs = zip(*(vertex.co for vertex in obj.data.vertices))
    return [[round(min(xs) * 100.0, 2) + 0.0, round(-max(ys) * 100.0, 2) + 0.0, round(min(zs) * 100.0, 2) + 0.0],
            [round(max(xs) * 100.0, 2) + 0.0, round(-min(ys) * 100.0, 2) + 0.0, round(max(zs) * 100.0, 2) + 0.0]]


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, use_mesh_modifiers=False,
                             mesh_smooth_type="FACE", use_tspace=True, colors_type="NONE", apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                             bake_space_transform=False, add_leaf_bones=False, bake_anim=False, use_custom_props=False,
                             path_mode="AUTO")


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(PROPS_DIR, exist_ok=True)
    records = {}
    for obj in (flag_cloth(), beacon()):
        export(obj, os.path.join(PROPS_DIR, f"{obj.name}.fbx"))
        records[obj.name] = {
            "fbx": f"{obj.name}.fbx",
            "slots": [slot.material.name for slot in obj.material_slots],
            "triangles": triangles(obj),
            "bounds": bounds(obj),
        }
        print(f"flag_beacon_blender: {obj.name} {records[obj.name]['triangles']} triangles, bounds {records[obj.name]['bounds']}")
    with open(os.path.join(PROPS_DIR, "props.json"), "w", encoding="utf-8", newline="\n") as file:
        json.dump({"about": "Generated by Tools/Art/flag_beacon_blender.py; read by Tools/Art/usa_hq_recipe.py.",
                   "meshes": records}, file, indent=1)
        file.write("\n")


if __name__ == "__main__":
    main()
