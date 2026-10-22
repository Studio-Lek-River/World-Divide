"""Kit identity/01_FlagCloth, SM_Kit_FlagCloth: the waving flag's cloth (#351), built by Tools/Art/kit_blender.py.

A flat grid of COLUMNS x ROWS quads, 1 x 1 m, its origin at the hoist's top, +X along the fly and -Z down the hoist,
facing +-Y. UV0 runs U 0 to 1 from hoist to fly and V 0 to 1 from top to bottom, which M_Flag_Cloth pins and waves by and
maps the flag's texture with. A building scales it: Z is the hoist's height, and X its height times the flag's aspect
(ARTSBuilding::ApplyParts, DA_FlagTypes). LOD1, past the 260 m switch where the wind is long off, is a coarse flat sheet.
"""

import bpy

# 40 x 24 quads: about ten samples a metre along a 4 m flag for the wave and its flutter.
COLUMNS = 40
ROWS = 24
LOD1_COLUMNS = 10
LOD1_ROWS = 6


def grid(name, columns, rows):
    mesh = bpy.data.meshes.new(name)
    vertices = [(column / columns, 0.0, -row / rows) for row in range(rows + 1) for column in range(columns + 1)]
    faces = []
    for row in range(rows):
        for column in range(columns):
            a = row * (columns + 1) + column
            faces.append((a, a + columns + 1, a + columns + 2, a + 1))
    mesh.from_pydata(vertices, [], faces)
    uv = mesh.uv_layers.new(name="UVMap")
    for loop in mesh.loops:
        x, _, z = mesh.vertices[loop.vertex_index].co
        # Blender's V runs up and the FBX import flips it: V 0 at the top in Unreal.
        uv.data[loop.index].uv = (x, 1.0 + z)
    mesh.materials.append(bpy.data.materials.get("Flag") or bpy.data.materials.new("Flag"))
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def build(piece):
    return grid(piece["mesh"], COLUMNS, ROWS), grid(f"{piece['mesh']}_LOD1", LOD1_COLUMNS, LOD1_ROWS)
