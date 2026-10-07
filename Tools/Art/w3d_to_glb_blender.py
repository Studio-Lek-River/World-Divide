"""Converts a C&C Generals / Zero Hour .w3d model into one static .glb in its rest pose, for use as local reference art
(see extract_big.py). Needs Blender with the OpenSAGE W3D add-on (io_mesh_w3d) installed as an extension; the model's
textures (.dds or .tga) must sit next to the .w3d, which is where extract_big.py puts them.

  blender -b --python Tools/Art/w3d_to_glb_blender.py -- <in.w3d> <out.glb>

All parts are merged into one mesh with two materials: "Body" for the model's own texture and "HouseColor" for the parts
the game tints in the player's colour (meshes named HOUSECOLOR*). Effect meshes (muzzle flashes) are dropped. The
textures are written as PNG next to the .glb and referenced from it, which is the form placeholder_art.py imports.
"""

import json
import os
import struct
import sys

import addon_utils
import bpy

ADDON = "bl_ext.user_default.io_mesh_w3d"
# Meshes the game only shows for effects.
EFFECT_PREFIXES = ("MUZZLEFX",)
HOUSE_COLOR_PREFIX = "HOUSECOLOR"


def merge_into_static_mesh():
    """Unparents every mesh from the W3D skeleton keeping its rest pose, deletes the skeleton and effect meshes, and joins
    the rest into one object whose materials are reduced to Body and HouseColor."""
    meshes = []
    for obj in list(bpy.data.objects):
        if obj.type != "MESH":
            continue
        if obj.name.upper().startswith(EFFECT_PREFIXES):
            bpy.data.objects.remove(obj)
            continue
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = world
        meshes.append(obj)
    for obj in list(bpy.data.objects):
        if obj.type != "MESH":
            bpy.data.objects.remove(obj)

    shared = {}
    for obj in meshes:
        role = "HouseColor" if obj.name.upper().startswith(HOUSE_COLOR_PREFIX) else "Body"
        material = shared.setdefault(role, obj.data.materials[0])
        obj.data.materials.clear()
        obj.data.materials.append(material)
    for role, material in shared.items():
        material.name = role

    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def externalise_images(glb_path):
    """Rewrites a .glb so its embedded images become PNG files next to it, referenced by uri. The image bytes stay
    behind in the binary chunk unused; the file is small enough that this does not matter."""
    with open(glb_path, "rb") as file:
        data = file.read()
    json_length = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + json_length])
    binary_chunk = data[20 + json_length:]
    binary = binary_chunk[8:8 + struct.unpack_from("<I", binary_chunk, 0)[0]]

    directory = os.path.dirname(glb_path)
    for image in gltf.get("images", []):
        if "bufferView" not in image:
            continue
        view = gltf["bufferViews"][image.pop("bufferView")]
        image.pop("mimeType", None)
        file_name = f"{image['name']}.png"
        start = view.get("byteOffset", 0)
        with open(os.path.join(directory, file_name), "wb") as file:
            file.write(binary[start:start + view["byteLength"]])
        image["uri"] = file_name

    encoded = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    total = 12 + 8 + len(encoded) + len(binary_chunk)
    with open(glb_path, "wb") as file:
        file.write(struct.pack("<4sII", b"glTF", 2, total))
        file.write(struct.pack("<I4s", len(encoded), b"JSON"))
        file.write(encoded)
        file.write(binary_chunk)


def main():
    source, target = sys.argv[sys.argv.index("--") + 1:][:2]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if not addon_utils.enable(ADDON, default_set=True):
        sys.exit(f"{ADDON} is not installed: install the OpenSAGE W3D add-on as a Blender extension first")
    bpy.ops.import_mesh.westwood_w3d(filepath=os.path.abspath(source))
    merge_into_static_mesh()
    bpy.ops.export_scene.gltf(filepath=os.path.abspath(target), export_format="GLB", export_image_format="AUTO",
                              export_animations=False, export_skins=False, export_yup=True)
    externalise_images(os.path.abspath(target))
    print(f"wrote {target}")


main()
