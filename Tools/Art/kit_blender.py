"""Builds every piece of the shared building kit (docs/art/BUILDING_KIT.md) from Art/Shared/kit/kit_catalogue.json:

  blender -b --python Tools/Art/kit_blender.py

Each piece <category>/<NN>_<Piece> is built by its module Tools/Art/kit/<piece>.py, the piece's name in snake case
(FlagCloth: flag_cloth.py), whose build(piece) gets the piece's catalogue entry and returns its LOD0 object and its LOD1
object, or None for a piece that keeps LOD0 alone (class S). Its slots are its materials' names, in the catalogue's order.
The lit glass is the material Lens.

It writes Art/Shared/kit/<category>/<NN>_<Piece>/SM_Kit_<Piece>.fbx and _LOD1.fbx, and the derived manifest
Art/Shared/kit/build/kit_manifest.json: each piece's FBX, slots, triangles per LOD, bounds and the bounds of its Lens faces,
in Unreal centimetres, which setup_testmap.py's make_kit() reads. Metres in Blender, centimetres in the FBX, +X forward and
Z up, as the other generators export. The output is committed; rerun when the catalogue or a module changes.
"""

import importlib.util
import json
import os
import re

import bpy

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
KIT_DIR = os.path.join(PROJECT_DIR, "Art", "Shared", "kit")
CATALOGUE = os.path.join(KIT_DIR, "kit_catalogue.json")
MANIFEST = os.path.join(KIT_DIR, "build", "kit_manifest.json")
MODULE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kit")
LENS = "Lens"


def module(piece):
    """The piece's module, Tools/Art/kit/<piece in snake case>.py."""
    name = re.sub(r"(?<!^)(?=[A-Z])", "_", piece["id"].split("/")[1].split("_", 1)[1]).lower()
    spec = importlib.util.spec_from_file_location(f"kit_{name}", os.path.join(MODULE_DIR, f"{name}.py"))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def triangles(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def to_unreal(points):
    """(min, max) in Unreal centimetres of Blender points in metres, Y mirrored back; None for no points."""
    if not points:
        return None
    xs, ys, zs = zip(*points)
    return [[round(min(xs) * 100.0, 2) + 0.0, round(-max(ys) * 100.0, 2) + 0.0, round(min(zs) * 100.0, 2) + 0.0],
            [round(max(xs) * 100.0, 2) + 0.0, round(-min(ys) * 100.0, 2) + 0.0, round(max(zs) * 100.0, 2) + 0.0]]


def bounds(obj):
    return to_unreal([vertex.co for vertex in obj.data.vertices])


def lens_bounds(obj):
    """The bounds of the faces drawn with the Lens material, or None."""
    slots = [slot.material.name for slot in obj.material_slots]
    if LENS not in slots:
        return None
    lens = slots.index(LENS)
    return to_unreal([obj.data.vertices[v].co for face in obj.data.polygons if face.material_index == lens
                      for v in face.vertices])


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, use_mesh_modifiers=False,
                             mesh_smooth_type="FACE", use_tspace=True, colors_type="NONE", apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="X", axis_up="Z",
                             bake_space_transform=False, add_leaf_bones=False, bake_anim=False, use_custom_props=False,
                             path_mode="AUTO")


def build(piece):
    """Builds, checks and exports one piece; returns its manifest record."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    lod0, lod1 = module(piece).build(piece)
    folder = piece["id"].split("/")
    directory = os.path.join(KIT_DIR, folder[0], folder[1])
    os.makedirs(directory, exist_ok=True)
    record = {"mesh": piece["mesh"], "fbx": "/".join(folder + [f"{piece['mesh']}.fbx"]), "lod1_fbx": None}
    counts = []
    for lod, obj, budget in ((0, lod0, piece["budgets"]["lod0"]), (1, lod1, piece["budgets"]["lod1"])):
        if obj is None:
            continue
        slots = [slot.material.name for slot in obj.material_slots]
        if slots != piece["slots"]:
            raise SystemExit(f"{piece['id']} LOD{lod} has the slots {slots}, not {piece['slots']}")
        counts.append(triangles(obj))
        if counts[-1] > budget:
            raise SystemExit(f"{piece['id']} LOD{lod} is {counts[-1]} triangles, over its {budget}")
        name = piece["mesh"] if lod == 0 else f"{piece['mesh']}_LOD1"
        export(obj, os.path.join(directory, f"{name}.fbx"))
        if lod == 1:
            record["lod1_fbx"] = "/".join(folder + [f"{name}.fbx"])
    record.update({"slots": piece["slots"], "triangles": counts, "bounds": bounds(lod0), "lens_bounds": lens_bounds(lod0)})
    print(f"kit_blender: {piece['id']} {counts} triangles, bounds {record['bounds']}, lens {record['lens_bounds']}")
    return record


def main():
    with open(CATALOGUE, encoding="utf-8") as file:
        pieces = json.load(file)["pieces"]
    ids = [piece["id"] for piece in pieces]
    if len(set(ids)) != len(ids):
        raise SystemExit(f"The catalogue repeats an id: {ids}")
    records = {piece["id"]: build(piece) for piece in pieces}
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as file:
        json.dump({"about": "Derived: written by Tools/Art/kit_blender.py from kit_catalogue.json. Do not edit.",
                   "pieces": records}, file, indent=1)
        file.write("\n")


if __name__ == "__main__":
    main()
