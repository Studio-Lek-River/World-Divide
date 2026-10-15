"""Game-mesh finishing, UVs and texture baking for the M20 Guardian (used by m20_guardian_blender.py).

finalize() applies Bevel, Weighted Normal and Triangulate to the game meshes, so the UVs, the bake and the FBX all see the
same triangles and normals. unwrap() lays out one UV atlas per texture set at a uniform texel density. bake_set() bakes
each game mesh from its high-poly twin (the same mesh plus bolts, welds and the procedural shader's bump) into
BaseColor (team mask in alpha), Normal (DirectX, for Unreal) and ORM, saved as PNG.
"""

import math
import os

import bpy
import numpy as np

import m20_guardian_materials as mats


def use_gpu(scene):
    scene.render.engine = "CYCLES"
    preferences = bpy.context.preferences.addons["cycles"].preferences
    for kind in ("OPTIX", "CUDA"):
        try:
            preferences.compute_device_type = kind
        except TypeError:
            continue
        preferences.get_devices()
        devices = [d for d in preferences.devices if d.type == kind]
        if devices:
            for device in preferences.devices:
                device.use = device.type == kind
            scene.cycles.device = "GPU"
            print(f"cycles: {kind} on {', '.join(d.name for d in devices)}")
            return
    scene.cycles.device = "CPU"
    print("cycles: CPU")


def finalize(objects, material_names, triangulate=True):
    """Applies every modifier (adding a Triangulate that keeps the custom normals) to each mesh once, and records each
    face's procedural material as the face attribute src_mat. Triangulating appends faces out of build order, so the
    track is finalized untriangulated first, given its UVs, and triangulated after."""
    depsgraph_meshes = {}
    for obj in objects:
        if obj.data in depsgraph_meshes:
            continue
        if triangulate:
            modifier = obj.modifiers.new("Triangulate", "TRIANGULATE")
            modifier.quad_method = "FIXED"
            modifier.ngon_method = "BEAUTY"
            modifier.keep_custom_normals = True
        depsgraph_meshes[obj.data] = obj
    depsgraph = bpy.context.evaluated_depsgraph_get()
    replaced = {}
    for old, obj in depsgraph_meshes.items():
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), preserve_all_data_layers=True,
                                               depsgraph=depsgraph)
        mesh.name = old.name
        if "src_mat" not in mesh.attributes:
            source = mesh.attributes.new("src_mat", "INT", "FACE")
            indices = [material_names.index(m.name) for m in mesh.materials]
            source.data.foreach_set("value", [indices[p.material_index] for p in mesh.polygons])
        replaced[old] = mesh
    for obj in objects:
        obj.data = replaced.get(obj.data, obj.data)
        obj.modifiers.clear()
    for old in replaced:
        bpy.data.meshes.remove(old)


def edit_mode(objects):
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")


def unwrap(objects, margin=0.003):
    """One atlas over all the objects: angle-based islands, equal texel density, packed without overlaps."""
    edit_mode(objects)
    bpy.ops.uv.smart_project(angle_limit=math.radians(50.0), island_margin=0.0, area_weight=0.0, correct_aspect=True,
                             scale_to_bounds=False)
    bpy.ops.uv.average_islands_scale()
    bpy.ops.uv.pack_islands(rotate=True, margin=margin, shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")


def copy_uvs_tiled(template, target):
    """Gives every link of a track the template link's UVs: the track is the same link repeated in build order."""
    source = template.data.uv_layers.active.data
    count = len(source)
    values = np.empty(count * 2, np.float32)
    source.foreach_get("uv", values)
    links = len(target.data.loops) // count
    assert links * count == len(target.data.loops), "track loops are not a whole number of template links"
    layer = target.data.uv_layers.get("UVMap") or target.data.uv_layers.new(name="UVMap")
    layer.data.foreach_set("uv", np.tile(values, links))


def float_image(name, size):
    image = bpy.data.images.get(name) or bpy.data.images.new(name, size, size, alpha=True, float_buffer=True)
    image.colorspace_settings.name = "Non-Color"
    return image


def pixels(image):
    data = np.empty(len(image.pixels), np.float32)
    image.pixels.foreach_get(data)
    return data.reshape(-1, 4)


def save_png(name, size, data, path, colour_space):
    image = bpy.data.images.new(name, size, size, alpha=True)
    image.colorspace_settings.name = colour_space
    image.pixels.foreach_set(np.clip(data, 0.0, 1.0).ravel())
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)


def srgb(linear):
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(np.maximum(linear, 0.0), 1.0 / 2.4) - 0.055)


def bake_set(set_name, pairs, size, out_dir, ao_objects, samples=16, ao_samples=128):
    """Bakes the low meshes of one texture set from their high twins. pairs: [(low, high)]. ao_objects: the meshes
    visible for the ambient occlusion pass. Returns the three PNG paths."""
    scene = bpy.context.scene
    scene.cycles.use_denoising = False
    margin = max(4, size // 256)
    target = bpy.data.materials.new(f"Bake_{set_name}")
    target.use_nodes = True
    node = target.node_tree.nodes.new("ShaderNodeTexImage")
    target.node_tree.nodes.active = node
    lows = [low for low, _ in pairs]
    for low in lows:
        for i in range(len(low.data.materials)):
            low.data.materials[i] = target
    everything = [o for o in scene.objects if o.type == "MESH"]
    images = {}

    def run(kind, image, selected_to_active, visible):
        node.image = image
        for obj in everything:
            obj.hide_render = obj not in visible
        first = True
        for low, high in pairs:
            bpy.ops.object.select_all(action="DESELECT")
            low.select_set(True)
            if selected_to_active:
                high.select_set(True)
            bpy.context.view_layer.objects.active = low
            bpy.ops.object.bake(type=kind, use_selected_to_active=selected_to_active, cage_extrusion=0.026,
                                max_ray_distance=0.06, margin=margin, margin_type="EXTEND", use_clear=first,
                                normal_space="TANGENT", normal_r="POS_X", normal_g="NEG_Y", normal_b="POS_Z",
                                target="IMAGE_TEXTURES", uv_layer="UVMap")
            first = False

    highs = [high for _, high in pairs]
    scene.cycles.samples = samples
    mats.set_bake_channel(None)
    images["normal"] = float_image(f"{set_name}_normal", size)
    run("NORMAL", images["normal"], True, lows + highs)
    for channel in ("color", "roughness", "metallic", "mask"):
        mats.set_bake_channel(channel)
        images[channel] = float_image(f"{set_name}_{channel}", size)
        run("EMIT", images[channel], True, lows + highs)
    mats.set_bake_channel(None)
    scene.cycles.samples = ao_samples
    scene.world.light_settings.distance = 0.6
    images["ao"] = float_image(f"{set_name}_ao", size)
    run("AO", images["ao"], False, ao_objects)
    for obj in everything:
        obj.hide_render = obj.name.endswith("_High")

    os.makedirs(out_dir, exist_ok=True)
    paths = {key: os.path.join(out_dir, f"T_{set_name}_{key}.png") for key in ("BC", "N", "ORM")}
    colour = pixels(images["color"])
    base = np.empty_like(colour)
    base[:, :3] = srgb(colour[:, :3])
    base[:, 3] = pixels(images["mask"])[:, 0]
    save_png(f"T_{set_name}_BC", size, base, paths["BC"], "sRGB")
    normal = pixels(images["normal"]).copy()
    normal[:, 3] = 1.0
    save_png(f"T_{set_name}_N", size, normal, paths["N"], "Non-Color")
    orm = np.ones_like(colour)
    orm[:, 0] = pixels(images["ao"])[:, 0]
    orm[:, 1] = pixels(images["roughness"])[:, 0]
    orm[:, 2] = pixels(images["metallic"])[:, 0]
    save_png(f"T_{set_name}_ORM", size, orm, paths["ORM"], "Non-Color")
    for image in images.values():
        bpy.data.images.remove(image)
    bpy.data.materials.remove(target)
    return paths


def assign_game_materials(lows, material, emissive_material, material_names):
    """Each face of the set gets the set's material, glass and lamps M20_Emissive with their colour in Col."""
    for mesh in {low.data for low in lows}:
        source = [v.value for v in mesh.attributes["src_mat"].data]
        glowing = [material_names[s] in mats.EMISSIVE for s in source]
        mesh.materials.clear()
        mesh.materials.append(material)
        if any(glowing):
            mesh.materials.append(emissive_material)
        mesh.polygons.foreach_set("material_index", [1 if g else 0 for g in glowing])
        colours = mesh.color_attributes.new("Col", "BYTE_COLOR", "CORNER")
        values = np.zeros((len(mesh.loops), 4), np.float32)
        values[:, 3] = 1.0
        for polygon, glow, src in zip(mesh.polygons, glowing, source):
            if glow:
                values[polygon.loop_start:polygon.loop_start + polygon.loop_total] = \
                    mats.emissive_colour(material_names[src])
        colours.data.foreach_set("color", values.ravel())
