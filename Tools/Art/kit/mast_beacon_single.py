"""Kit lighting/12_MastBeaconSingle, SM_Kit_MastBeaconSingle: the red aviation obstruction beacon on a mast's tip (#351),
built by Tools/Art/kit_blender.py.

A dark base and a dome lens, BEACON_HEIGHT tall, its origin at the middle of its base. The housing's UVs sit at U < 0.5
and the lens's at U > 0.5, which is how M_Beacon_Obstruction tells them apart with one material slot. Class S: under 250
triangles with SEGMENTS around, and no LOD1.
"""

import math

import bmesh
import bpy
from mathutils import Matrix

# A base BASE_RADIUS across and BASE_HEIGHT tall, under a dome lens LENS_RADIUS across and LENS_HEIGHT tall, about
# 0.45 x 0.55 m, so it reads at the default zoom (150 m) as a few bright pixels.
BASE_RADIUS = 0.11
BASE_HEIGHT = 0.12
LENS_RADIUS = 0.22
LENS_HEIGHT = 0.44
SEGMENTS = 12
LENS_RINGS = 8
HOUSING_U = 0.25
LENS_U = 0.75


def build(piece):
    mesh = bpy.data.meshes.new(piece["mesh"])
    built = bmesh.new()
    bmesh.ops.create_cone(built, cap_ends=True, segments=SEGMENTS, radius1=BASE_RADIUS, radius2=BASE_RADIUS,
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
        rim.append([built.verts.new((radius * math.cos(2 * math.pi * s / SEGMENTS),
                                     radius * math.sin(2 * math.pi * s / SEGMENTS), BASE_HEIGHT + height))
                    for s in range(SEGMENTS)])
    for ring in range(LENS_RINGS):
        low, high = rim[ring], rim[ring + 1]
        for s in range(SEGMENTS):
            t = (s + 1) % SEGMENTS
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
    mesh.materials.append(bpy.data.materials.get("Beacon") or bpy.data.materials.new("Beacon"))
    mesh.update()
    obj = bpy.data.objects.new(piece["mesh"], mesh)
    bpy.context.collection.objects.link(obj)
    return obj, None
