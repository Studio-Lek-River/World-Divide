"""Finds the USA HQ's lamps (#195): every connected island of the lamp and glass slots in the game shell's FBX, with
its centre, facing and size in building centimetres, so each real light the HQ casts sits on a lamp you can see:

  blender -b --python Tools/Art/usa_hq_light_probe.py

Writes Art/USA/buildings/usa_hq/issue195/fixtures.json, which Tools/Art/usa_hq_recipe.py reads to place the lights.
The FBXs are only read. Blender's frame is the delivery's (usa_hq_game_blender.py): metres, Y mirrored from Unreal.
"""

import json
import os

import bmesh
import bpy
from mathutils import Vector

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ROOT = os.path.join(PROJECT_DIR, "Art", "USA", "buildings", "usa_hq")
OUT = os.path.join(ROOT, "issue195", "fixtures.json")

# The slots that emit light, per mesh: lamps, team light strips, the lit interior behind the windows and the glass.
SLOTS = {
    "SM_HQ_ShellDetail": ("MI_HQ20_FixtureWarm", "MI_HQ20_FixtureBlue", "MI_HQ17_TeamLight"),
    "SM_HQ_Shell": ("MI_HQ17_InnerLight", "MI_HQ17_WarmGlass", "MI_HQ17_CoolGlass"),
}
# Islands smaller than this (cm², area) are rivets and trims, not lamps.
MIN_AREA = 150.0


def unreal(v):
    return [round(v.x * 100.0, 1), round(-v.y * 100.0, 1), round(v.z * 100.0, 1)]


def unreal_dir(v):
    n = Vector((v.x, -v.y, v.z))
    if n.length > 0:
        n.normalize()
    return [round(c, 3) for c in n]


def islands(bm, faces):
    todo, out = set(faces), []
    while todo:
        seed = todo.pop()
        group, stack = [seed], [seed]
        while stack:
            f = stack.pop()
            for e in f.edges:
                for g in e.link_faces:
                    if g in todo:
                        todo.discard(g)
                        group.append(g)
                        stack.append(g)
        out.append(group)
    return out


def probe(mesh_name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.join(ROOT, "game", mesh_name + ".fbx"), axis_forward="X", axis_up="Z")
    found = []
    for obj in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bm.transform(obj.matrix_world)
        names = [m.name.split(".")[0] if m else "" for m in obj.data.materials]
        for slot in SLOTS[mesh_name]:
            idx = [i for i, n in enumerate(names) if n == slot]
            faces = [f for f in bm.faces if f.material_index in idx]
            for group in islands(bm, faces):
                area = sum(f.calc_area() for f in group) * 1e4
                if area < MIN_AREA:
                    continue
                centre = sum((f.calc_center_median() * f.calc_area() for f in group), Vector()) / (area / 1e4)
                normal = sum((f.normal * f.calc_area() for f in group), Vector())
                lo = Vector([min(v.co[i] for f in group for v in f.verts) for i in range(3)])
                hi = Vector([max(v.co[i] for f in group for v in f.verts) for i in range(3)])
                found.append({"mesh": mesh_name, "slot": slot, "centre": unreal(centre), "normal": unreal_dir(normal),
                              "size": [round((hi - lo)[i] * 100.0, 1) for i in range(3)], "area": round(area, 1)})
        bm.free()
    return found


fixtures = []
for name in SLOTS:
    fixtures += probe(name)
for i, f in enumerate(sorted(fixtures, key=lambda f: (f["slot"], f["centre"]))):
    f["id"] = f"{f['slot'].split('_')[-1]}_{i:03d}"
fixtures.sort(key=lambda f: f["id"])
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as fh:
    json.dump({"about": "Lamp, light-strip and window islands of the HQ game shell, Unreal building cm (#195).",
               "fixtures": fixtures}, fh, indent=1)
print(f"usa_hq_light_probe: {len(fixtures)} islands -> {OUT}")
