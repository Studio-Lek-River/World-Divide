"""Flattens a .glb into one static, unskinned mesh in its rest pose, keeping materials and UVs and dropping skins and
animations. Unreal's glTF importer places skinned meshes that sit below other nodes wrongly when it is told to import
them as static meshes, so animated source models are baked with this before they go into Art/Source.

  python Tools/Art/bake_gltf_static.py <in.glb> <out.glb>

Needs numpy. Plain desktop Python, not the editor.
"""

import json
import struct
import sys

import numpy as np

COMPONENT_TYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
COMPONENT_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path):
    data = open(path, "rb").read()
    json_length = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + json_length])
    offset = 20 + json_length
    binary = b""
    if offset < len(data):
        bin_length = struct.unpack_from("<I", data, offset)[0]
        binary = data[offset + 8:offset + 8 + bin_length]
    return gltf, binary


def read_accessor(gltf, binary, index):
    accessor = gltf["accessors"][index]
    view = gltf["bufferViews"][accessor["bufferView"]]
    dtype = np.dtype(COMPONENT_TYPES[accessor["componentType"]])
    count = COMPONENT_COUNTS[accessor["type"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", dtype.itemsize * count)
    rows = np.ndarray((accessor["count"], count), dtype, binary, start, (stride, dtype.itemsize)).copy()
    if accessor.get("normalized"):
        rows = rows.astype(np.float64) / np.iinfo(dtype).max
    return rows


def local_matrix(node):
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    x, y, z, w = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    rotation = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])
    matrix = np.eye(4)
    matrix[:3, :3] = rotation * np.array(node.get("scale", [1.0, 1.0, 1.0]))
    matrix[:3, 3] = node.get("translation", [0.0, 0.0, 0.0])
    return matrix


def world_matrices(gltf):
    worlds = {}

    def visit(index, parent):
        worlds[index] = parent @ local_matrix(gltf["nodes"][index])
        for child in gltf["nodes"][index].get("children", []):
            visit(child, worlds[index])

    scene = gltf["scenes"][gltf.get("scene", 0)]
    for root in scene["nodes"]:
        visit(root, np.eye(4))
    return worlds


def bake(source, target):
    gltf, binary = read_glb(source)
    worlds = world_matrices(gltf)
    chunks = []
    views = []
    accessors = []

    def add(array, target_kind=None, kind="VEC3"):
        raw = np.ascontiguousarray(array).tobytes()
        offset = sum(len(chunk) for chunk in chunks)
        chunks.append(raw + b"\0" * (-len(raw) % 4))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(raw)}
        if target_kind:
            view["target"] = target_kind
        views.append(view)
        accessor = {"bufferView": len(views) - 1, "componentType": 5126 if array.dtype == np.float32 else 5125,
                    "count": len(array), "type": kind}
        if kind == "VEC3" and array.dtype == np.float32:
            accessor["min"] = array.min(axis=0).tolist()
            accessor["max"] = array.max(axis=0).tolist()
        accessors.append(accessor)
        return len(accessors) - 1

    primitives = []
    for node_index, node in enumerate(gltf["nodes"]):
        if "mesh" not in node or node_index not in worlds:
            continue
        skin = gltf["skins"][node["skin"]] if "skin" in node else None
        if skin:
            inverse_binds = read_accessor(gltf, binary, skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
            joint_matrices = np.array([worlds[joint] @ inverse_binds[i] for i, joint in enumerate(skin["joints"])])
        for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
            attributes = primitive["attributes"]
            positions = read_accessor(gltf, binary, attributes["POSITION"]).astype(np.float64)
            homogeneous = np.hstack([positions, np.ones((len(positions), 1))])
            if skin:
                joints = read_accessor(gltf, binary, attributes["JOINTS_0"]).astype(int)
                weights = read_accessor(gltf, binary, attributes["WEIGHTS_0"]).astype(np.float64)
                vertex_matrices = np.einsum("vk,vkij->vij", weights, joint_matrices[joints])
            else:
                vertex_matrices = np.broadcast_to(worlds[node_index], (len(positions), 4, 4))
            baked = {"POSITION": add(np.einsum("vij,vj->vi", vertex_matrices, homogeneous)[:, :3].astype(np.float32), 34962)}
            if "NORMAL" in attributes:
                normals = read_accessor(gltf, binary, attributes["NORMAL"]).astype(np.float64)
                normal_matrices = np.linalg.inv(vertex_matrices[:, :3, :3]).transpose(0, 2, 1)
                normals = np.einsum("vij,vj->vi", normal_matrices, normals)
                normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
                baked["NORMAL"] = add(normals.astype(np.float32), 34962)
            if "TEXCOORD_0" in attributes:
                baked["TEXCOORD_0"] = add(read_accessor(gltf, binary, attributes["TEXCOORD_0"]).astype(np.float32), 34962, "VEC2")
            if "indices" in primitive:
                indices = read_accessor(gltf, binary, primitive["indices"]).astype(np.uint32).reshape(-1)
            else:
                indices = np.arange(len(positions), dtype=np.uint32)
            # A mirroring transform flips the winding; swap two corners so faces keep pointing outwards.
            if np.linalg.det(vertex_matrices[0][:3, :3]) < 0:
                indices = indices.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1)
            baked_primitive = {"attributes": baked, "indices": add(indices, 34963, "SCALAR")}
            if "material" in primitive:
                baked_primitive["material"] = primitive["material"]
            primitives.append(baked_primitive)

    images = []
    for image in gltf.get("images", []):
        image = dict(image)
        if "bufferView" in image:
            view = gltf["bufferViews"][image["bufferView"]]
            raw = binary[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]
            offset = sum(len(chunk) for chunk in chunks)
            chunks.append(raw + b"\0" * (-len(raw) % 4))
            views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(raw)})
            image["bufferView"] = len(views) - 1
        images.append(image)

    name = gltf["nodes"][gltf["scenes"][gltf.get("scene", 0)]["nodes"][0]].get("name", "Mesh")
    output = {
        "asset": {"version": "2.0", "generator": "World Divide bake_gltf_static.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": primitives}],
        "materials": gltf.get("materials", []),
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": sum(len(chunk) for chunk in chunks)}],
    }
    for key in ("textures", "samplers"):
        if key in gltf:
            output[key] = gltf[key]
    if images:
        output["images"] = images

    json_bytes = json.dumps(output, separators=(",", ":")).encode("utf-8")
    json_bytes += b" " * (-len(json_bytes) % 4)
    bin_bytes = b"".join(chunks)
    with open(target, "wb") as out:
        out.write(struct.pack("<III", 0x46546C67, 2, 28 + len(json_bytes) + len(bin_bytes)))
        out.write(struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes)
        out.write(struct.pack("<II", len(bin_bytes), 0x004E4942) + bin_bytes)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    bake(sys.argv[1], sys.argv[2])
