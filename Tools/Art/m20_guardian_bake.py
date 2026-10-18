"""Game-mesh finishing, UVs and texture baking for the M20 Guardian (used by m20_guardian_blender.py).

finalize() applies Bevel, Weighted Normal and Triangulate to the game meshes, so the UVs, the bake and the FBX all see the
same triangles and normals. unwrap() lays out one UV atlas per texture set, with less texel density on faces the player
never sees (undersides, the lower hull behind the tracks). bake_set() bakes each game mesh from its high-poly twin (the
same mesh plus bolts, welds and the procedural shader's bump) into BaseColor (team mask in alpha), Normal (DirectX, for
Unreal) and ORM, at source size into textures/source/ and box-filtered to game size into textures/.
"""

import math
import os

import bmesh
import bpy
import numpy as np
from bpy_extras import bmesh_utils

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


def unwrap(objects, margin=0.0012, weight=None):
    """One atlas over all the objects: angle-based islands at equal texel density, then each island scaled by
    weight(world centre, world normal) (texel density relative to the rest), packed without overlaps."""
    edit_mode(objects)
    bpy.ops.uv.smart_project(angle_limit=math.radians(60.0), island_margin=0.0, area_weight=0.0, correct_aspect=True,
                             scale_to_bounds=False)
    bpy.ops.uv.average_islands_scale()
    bpy.ops.object.mode_set(mode="OBJECT")
    if weight:
        for obj in objects:
            scale_islands(obj, weight)
    edit_mode(objects)
    # A fixed margin in UV space: a margin scaled per island wastes most of the atlas on the thousands of small
    # bevel-corner islands.
    bpy.ops.uv.pack_islands(rotate=True, margin_method="FRACTION", margin=margin, shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")


def scale_islands(obj, weight):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    uv = bm.loops.layers.uv.active
    rotation = obj.matrix_world.to_3x3()
    for island in bmesh_utils.bmesh_linked_uv_islands(bm, uv):
        area = sum(face.calc_area() for face in island) or 1.0
        centre = sum((face.calc_center_median() * face.calc_area() for face in island), obj.location * 0.0) / area
        normal = sum((face.normal * face.calc_area() for face in island), obj.location * 0.0)
        factor = weight(obj.matrix_world @ centre, (rotation @ normal).normalized())
        if area < 0.001:
            factor *= 0.6  # bevel corners, bolt heads and other specks: nobody reads texels there
        if abs(factor - 1.0) < 1e-6:
            continue
        loops = [loop for face in island for loop in face.loops]
        middle = sum((loop[uv].uv for loop in loops), loops[0][uv].uv * 0.0) / len(loops)
        for loop in loops:
            loop[uv].uv = middle + (loop[uv].uv - middle) * factor
    bm.to_mesh(obj.data)
    bm.free()


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


# The track's animation channels after UVMap (UV0), as Unreal reads them (#151): per vertex, in Unreal cm in the track's
# X-Z plane, the offset to the same vertex in the previous link (UV1), the next (UV2) and the one after (UV3), and in UV4.x
# the turn in radians from this link to the next, counter-clockwise from +X toward +Z. The material moves every vertex
# part of the way to its place in the next link (a Catmull-Rom curve through those four places) and turns its normal by
# as much of that turn. Unreal flips V on import (v -> 1 - v), so V is written flipped and arrives as it is meant.
TRACK_ANIMATION_UVS = ("TrackPrev", "TrackNext", "TrackNext2", "TrackTurn")


def link_frames(co):
    """Each link's direction in X-Z, from two vertices far apart in the first link (the same two in every link), and
    the turn from each link to the next, wrapped to (-pi, pi]."""
    first = co[0][:, (0, 2)]
    distances = np.linalg.norm(first[:, None, :] - first[None, :, :], axis=2)
    rear, front = np.unravel_index(np.argmax(distances), distances.shape)
    direction = co[:, front, :] - co[:, rear, :]
    angles = np.arctan2(direction[:, 2], direction[:, 0])
    turns = np.angle(np.exp(1j * (np.roll(angles, -1) - angles)))
    return rear, front, angles, turns


def rotate_xz(vectors, angles):
    """Turns vectors (..., 3) by angles (broadcast over the leading axes) in X-Z, from +X toward +Z."""
    c, s = np.cos(angles), np.sin(angles)
    out = vectors.copy()
    out[..., 0] = vectors[..., 0] * c - vectors[..., 2] * s
    out[..., 2] = vectors[..., 0] * s + vectors[..., 2] * c
    return out


def track_vertices(track, links):
    mesh = track.data
    count = len(mesh.vertices)
    per_link = count // links
    assert per_link * links == count, f"{track.name}: {count} vertices are not {links} links"
    co = np.empty(count * 3, np.float32)
    mesh.vertices.foreach_get("co", co)
    return co.reshape(links, per_link, 3).astype(np.float64)


def add_track_animation_uvs(track, links):
    """Writes TRACK_ANIMATION_UVS on a finished track (see there): the track is the same link repeated in build order,
    so vertex k of link i becomes vertex k of link i + 1. Asserts that every link is the one before it moved rigidly,
    which is what lets one link's place stand for the next."""
    co = track_vertices(track, links)
    _, _, _, turns = link_frames(co)
    centres = co.mean(axis=1)
    moved = rotate_xz(co - centres[:, None, :], turns[:, None])
    residual = np.abs(np.roll(co - centres[:, None, :], -1, axis=0) - moved).max()
    assert residual < 1e-4, f"{track.name}: links are not rigid copies of each other ({residual * 100:.3f} cm)"

    def offset(step):
        return np.roll(co, -step, axis=0) - co

    mesh = track.data
    corner_verts = np.empty(len(mesh.loops), np.int32)
    mesh.loops.foreach_get("vertex_index", corner_verts)
    channels = []
    for step in (-1, 1, 2):
        delta = offset(step).reshape(-1, 3)
        assert np.abs(delta[:, 1]).max() < 1e-5, f"{track.name}: links do not stay in their plane"
        channels.append(np.stack((delta[:, 0] * 100.0, 1.0 - delta[:, 2] * 100.0), axis=1))
    per_vertex_turn = np.repeat(turns, co.shape[1])
    channels.append(np.stack((per_vertex_turn, np.ones_like(per_vertex_turn)), axis=1))

    render = mesh.uv_layers.get("UVMap") or mesh.uv_layers[0]
    for name, values in zip(TRACK_ANIMATION_UVS, channels):
        layer = mesh.uv_layers.get(name) or mesh.uv_layers.new(name=name)
        layer.data.foreach_set("uv", values[corner_verts].astype(np.float32).ravel())
    mesh.uv_layers.active = render
    render.active_render = True
    assert [layer.name for layer in mesh.uv_layers] == [render.name, *TRACK_ANIMATION_UVS]
    return residual


def catmull_rom(prev, nxt, nxt2, t):
    """The material's curve from a vertex's place (0) to its place in the next link, through the offsets to the
    previous (prev), next (nxt) and next-but-one (nxt2) links' places, at t in [0, 1]."""
    return 0.5 * ((nxt - prev) * t + (2.0 * prev + 4.0 * nxt - nxt2) * t * t + (-prev - 3.0 * nxt + nxt2) * t ** 3)


def check_track_animation(track, links, path_at, steps=32):
    """The material's link shift (catmull_rom, the turn) against a rigid one, at steps phases through one link pitch:
    every link carried as a rigid body, its centre along the track's centre line and its direction turning evenly toward
    the next link's. Returns, in cm, cm, cm and degrees: the largest distance of a vertex from its rigid place, the
    largest difference from the rigid shift in the gap between one link's front and the next one's rear (the rigid
    track's own gaps already open where it bends round a wheel), the largest jump where the phase wraps
    (a link at the end of the shift against the next link at its start), and the largest error of the turned normal.
    path_at(s) gives the centre line's point (x, z) and direction angle at arc length s, path_at(None) its length."""
    co = track_vertices(track, links)
    rear, front, angles, turns = link_frames(co)
    total = path_at(None)
    pitch = total / links
    centres = np.array([(path_at((i + 0.5) * pitch)[0][0], co[i, 0, 1], path_at((i + 0.5) * pitch)[0][1])
                        for i in range(links)])
    local = co - centres[:, None, :]
    prev, nxt, nxt2 = (np.roll(co, -step, axis=0) - co for step in (-1, 1, 2))

    def gaps(places):
        return np.linalg.norm(np.roll(places[:, rear], -1, axis=0) - places[:, front], axis=1)

    wrap = np.linalg.norm(co + catmull_rom(prev, nxt, nxt2, 1.0) - np.roll(co, -1, axis=0), axis=2).max()
    worst_place = worst_gap = 0.0
    for t in np.linspace(0.0, 1.0, steps + 1):
        shifted = co + catmull_rom(prev, nxt, nxt2, t)
        rigid = rotate_xz(local, (turns * t)[:, None])
        for i in range(links):
            point, _ = path_at(((i + 0.5 + t) * pitch) % total)
            rigid[i] += (point[0], co[i, 0, 1], point[1])
        worst_place = max(worst_place, np.linalg.norm(shifted - rigid, axis=2).max())
        worst_gap = max(worst_gap, np.abs(gaps(shifted) - gaps(rigid)).max())
    # The normal turns by the share of the turn the phase is through, so it meets the next link's at the wrap: its
    # error is how far the link's own direction strays from that even turn along the shifted places.
    worst_turn = 0.0
    for t in np.linspace(0.0, 1.0, steps + 1):
        shifted = co + catmull_rom(prev, nxt, nxt2, t)
        direction = shifted[:, front] - shifted[:, rear]
        actual = np.arctan2(direction[:, 2], direction[:, 0])
        worst_turn = max(worst_turn, np.abs(np.angle(np.exp(1j * (actual - angles - turns * t)))).max())
    return worst_place * 100.0, worst_gap * 100.0, wrap * 100.0, np.degrees(worst_turn)


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


def downsample(data, size, game_size):
    """Box filter from size to game_size (both powers of two), rows of RGBA."""
    factor = size // game_size
    if factor <= 1:
        return data
    return data.reshape(game_size, factor, game_size, factor, 4).mean(axis=(1, 3)).reshape(-1, 4)


def write_set(set_name, size, channels, out_dir):
    """channels: linear float rows for color, mask, normal (0..1 encoded), ao, roughness, metallic. Writes BC (sRGB,
    team mask in alpha), N and ORM. Returns the three paths."""
    os.makedirs(out_dir, exist_ok=True)
    paths = {key: os.path.join(out_dir, f"T_{set_name}_{key}.png") for key in ("BC", "N", "ORM")}
    colour = channels["color"]
    base = np.empty_like(colour)
    base[:, :3] = srgb(colour[:, :3])
    base[:, 3] = channels["mask"][:, 0]
    save_png(f"T_{set_name}_BC", size, base, paths["BC"], "sRGB")
    vector = channels["normal"][:, :3] * 2.0 - 1.0
    vector /= np.maximum(np.linalg.norm(vector, axis=1, keepdims=True), 1e-6)
    normal = np.ones_like(colour)
    normal[:, :3] = vector * 0.5 + 0.5
    save_png(f"T_{set_name}_N", size, normal, paths["N"], "Non-Color")
    orm = np.ones_like(colour)
    orm[:, 0] = channels["ao"][:, 0]
    orm[:, 1] = channels["roughness"][:, 0]
    orm[:, 2] = channels["metallic"][:, 0]
    save_png(f"T_{set_name}_ORM", size, orm, paths["ORM"], "Non-Color")
    return paths


def bake_set(set_name, pairs, size, out_dir, ao_objects, game_size=None, samples=16, ao_samples=128):
    """Bakes the low meshes of one texture set from their high twins. pairs: [(low, high)]. ao_objects: the meshes
    visible for the ambient occlusion pass. Writes the source set to out_dir/source and the game set (game_size) to
    out_dir. Returns the game set's three PNG paths."""
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

    channels = {key: pixels(image).copy() for key, image in images.items()}
    write_set(set_name, size, channels, os.path.join(out_dir, "source"))
    game_size = game_size or size
    paths = write_set(set_name, game_size, {key: downsample(data, size, game_size) for key, data in channels.items()},
                      out_dir)
    for image in images.values():
        bpy.data.images.remove(image)
    bpy.data.materials.remove(target)
    return paths


def assign_game_materials(lows, material, emissive_material, material_names):
    """Each face of the set gets the set's material (glass included), lamps M20_Emissive with their colour in Col."""
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
