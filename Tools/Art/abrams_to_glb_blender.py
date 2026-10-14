"""Reworks the local tryout Abrams (Art/Source/Local/Crytek/Abrams/source/Abrams.obj, ripped from CryEngine content, so
Crytek's: gitignored and never committed or shipped) into two static .glb files that import_local_art.py brings in:

  blender -b --python Tools/Art/abrams_to_glb_blender.py

- abrams_hull.glb: everything below the turret ring, its footprint centred on the origin, tracks on z = 0.
- abrams_turret.glb: turret and gun, its origin on the turret ring so it can traverse.
- abrams.json: where the ring sits on the hull and where the muzzle sits on the turret, in centimetres, in Unreal's axes.

Both face +X, in metres. The interior and antennas are dropped and every part is decimated to an RTS budget. The
textures are baked into one 1024 atlas per object, graded a cleaner, warmer desert tan with ambient occlusion so the
panels read from the RTS camera. Two materials per object: Body, and HouseColor (side-skirt band, turret side stripe,
bustle stowage, mantlet cover) baked light grey so the team colour keeps its brightness. Previews of both in a blue and a
red team colour go to Saved/ArtPreviews.
"""

import json
import os
import struct

import bmesh
import bpy
import numpy
from mathutils import Matrix, Vector

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MODEL_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Local", "Crytek", "Abrams")
SOURCE = os.path.join(MODEL_DIR, "source", "Abrams.obj")
PREVIEW_DIR = os.path.join(PROJECT_DIR, "Saved", "ArtPreviews")
ATLAS_SIZE = 1024

# Source groups (centimetres, front +Y, z up once imported).
TURRET_PARTS = {"Mesh_0045", "Mesh_0046", "Mesh_0047", "Mesh_0048", "Mesh_0050", "Mesh_0058", "Mesh_0031", "Mesh_0023"}
INTERIOR_PARTS = {"Mesh_0049", "Mesh_0064"}
GUN_PART = "Mesh_0046"
TURRET_RING_PART = "Mesh_0049"
# Fraction of triangles each part keeps; the rest keep DEFAULT_KEEP. Parts below SMALL_PART triangles stay whole.
KEEP = {"Mesh_0020": 0.5, "Mesh_0045": 0.35, "Mesh_0050": 0.5, "Mesh_0021": 1.0, "Mesh_0022": 1.0}
ROAD_WHEEL_KEEP = 0.2
SPROCKET_KEEP = 0.12
DEFAULT_KEEP = 0.6
SMALL_PART = 120
# Turret scaled up for readability around its ring, and the gun a little more along its length.
TURRET_SCALE = 1.1
GUN_LENGTH_SCALE = 1.08

# Grade: lift the dark, grimy source towards a clean warm tan and add contrast.
TAN = (0.62, 0.48, 0.30)
GRADE_LIFT = 1.9
TAN_MIX = 0.55
# The ripped textures carry baked grime and black gutters; armour never goes darker than this share of the tan.
ARMOUR_FLOOR = 0.45
# Running gear stays dark: the tracks, and the road wheels and sprockets.
RUNNING_GEAR_TEXTURES = ("Track_", "Tex_0025")
AO_STRENGTH = 0.6
HOUSE_GREY = 0.72


def import_source():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.wm.obj_import(filepath=SOURCE, use_split_groups=True)
    for obj in bpy.data.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = bpy.data.objects[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for name in INTERIOR_PARTS:
        bpy.data.objects.remove(bpy.data.objects[name])


def bounds(obj):
    points = [obj.matrix_world @ v.co for v in obj.data.vertices]
    return (Vector([min(p[i] for p in points) for i in range(3)]), Vector([max(p[i] for p in points) for i in range(3)]))


def drop_antennas(obj):
    """Antennas are thin loose parts standing above the turret roof; at RTS distance they only flicker."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    seen = set()
    doomed = []
    for face in bm.faces:
        if face.index in seen:
            continue
        island, stack = [], [face]
        seen.add(face.index)
        while stack:
            current = stack.pop()
            island.append(current)
            for edge in current.edges:
                for other in edge.link_faces:
                    if other.index not in seen:
                        seen.add(other.index)
                        stack.append(other)
        points = [v.co for f in island for v in f.verts]
        size = [max(p[i] for p in points) - min(p[i] for p in points) for i in range(3)]
        if max(p.z for p in points) > 290.0 and size[0] < 15.0 and size[1] < 15.0:
            doomed.extend(island)
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bm.to_mesh(obj.data)
    bm.free()


def decimate(obj):
    triangles = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    if triangles < SMALL_PART:
        return
    if obj.name in KEEP:
        keep = KEEP[obj.name]
    elif triangles == 236:
        keep = ROAD_WHEEL_KEEP
    elif triangles == 1388:
        keep = SPROCKET_KEEP
    else:
        keep = DEFAULT_KEEP
    modifier = obj.modifiers.new("Decimate", "DECIMATE")
    modifier.ratio = keep
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def mark_house_color(obj, part):
    """Flags the faces the team colour goes on, in source coordinates (cm, front +Y)."""
    layer = obj.data.attributes.new("house", "BOOLEAN", "FACE")
    world = obj.matrix_world
    for polygon in obj.data.polygons:
        centre = world @ polygon.center
        normal = (world.to_3x3() @ polygon.normal).normalized()
        flag = False
        if part in ("Mesh_0021", "Mesh_0022"):
            # The upper band of the side skirts.
            flag = abs(normal.x) > 0.7 and abs(centre.x) > 165.0 and 120.0 < centre.z < 165.0
        elif part == "Mesh_0045":
            # A stripe along the turret flanks.
            flag = abs(normal.x) > 0.6 and 200.0 < centre.z < 222.0 and centre.y > -250.0
        elif part in ("Mesh_0050", "Mesh_0048"):
            # Bustle and side stowage, and the mantlet cover.
            flag = True
        layer.data[polygon.index].value = flag


def join(objects, name):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = name
    joined.data.name = name
    return joined


def scale_turret(turret, ring, gun_box):
    bm = bmesh.new()
    bm.from_mesh(turret.data)
    for vertex in bm.verts:
        offset = vertex.co - ring
        if gun_box[0].y - 1.0 < vertex.co.y and vertex.co.z > gun_box[0].z - 1.0 and abs(vertex.co.x) < gun_box[1].x + 1.0:
            offset.y = (vertex.co.y - gun_box[0].y) * GUN_LENGTH_SCALE + gun_box[0].y - ring.y
        vertex.co = ring + offset * TURRET_SCALE
    bm.to_mesh(turret.data)
    bm.free()


def to_game_space(obj, origin):
    """Moves origin to (0, 0, 0), turns front from +Y to +X and centimetres to metres."""
    transform = Matrix.Scale(0.01, 4) @ Matrix.Rotation(-1.5707963, 4, "Z") @ Matrix.Translation(-origin)
    obj.data.transform(transform)
    obj.data.update()


def make_bake_material(source, house):
    """A copy of a source material that emits its graded colour, sampled through the source UVs."""
    material = source.copy()
    material.name = f"{source.name}{'_House' if house else ''}"
    tree = material.node_tree
    # The .mtl points at the ripper's absolute paths; the textures sit next to the .obj under the same names.
    names = [os.path.basename(n.image.filepath.replace("\\", "/")) for n in tree.nodes if n.type == "TEX_IMAGE" and n.image]
    tree.nodes.clear()
    image_node = None
    if names:
        image_node = tree.nodes.new("ShaderNodeTexImage")
        # Blender cannot read every .dds here; the PNG copies that came with the model are preferred.
        path = os.path.join(os.path.dirname(SOURCE), names[0])
        png = os.path.splitext(path)[0] + ".png"
        image_node.image = bpy.data.images.load(png if os.path.exists(png) else path, check_existing=True)
        if image_node.image.size[0] == 0:
            # A compression Blender cannot decode (Tex_0023): plain tan instead of the missing-texture pink.
            tree.nodes.remove(image_node)
            image_node = None
    output = tree.nodes.new("ShaderNodeOutputMaterial")
    emission = tree.nodes.new("ShaderNodeEmission")
    tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])
    colour = None
    if image_node is None:
        emission.inputs["Color"].default_value = (*TAN, 1.0)
    else:
        uv = tree.nodes.new("ShaderNodeUVMap")
        uv.uv_map = "UVMap"
        tree.links.new(uv.outputs["UV"], image_node.inputs["Vector"])
        lift = tree.nodes.new("ShaderNodeMix")
        lift.data_type = "RGBA"
        lift.blend_type = "MULTIPLY"
        lift.inputs["Factor"].default_value = 1.0
        lift.inputs["B"].default_value = (GRADE_LIFT,) * 3 + (1.0,)
        tree.links.new(image_node.outputs["Color"], lift.inputs["A"])
        tan = tree.nodes.new("ShaderNodeMix")
        tan.data_type = "RGBA"
        tan.blend_type = "COLOR"
        tan.inputs["Factor"].default_value = TAN_MIX
        tan.inputs["B"].default_value = (*TAN, 1.0)
        tree.links.new(lift.outputs["Result"], tan.inputs["A"])
        colour = tan.outputs["Result"]
        if not names[0].startswith(RUNNING_GEAR_TEXTURES):
            floor = tree.nodes.new("ShaderNodeMix")
            floor.data_type = "RGBA"
            floor.blend_type = "LIGHTEN"
            floor.inputs["Factor"].default_value = 1.0
            floor.inputs["B"].default_value = tuple(c * ARMOUR_FLOOR for c in TAN) + (1.0,)
            tree.links.new(colour, floor.inputs["A"])
            colour = floor.outputs["Result"]
        tree.links.new(colour, emission.inputs["Color"])
    if house:
        # Light grey that keeps the panel detail.
        brighten = tree.nodes.new("ShaderNodeMath")
        brighten.operation = "MULTIPLY_ADD"
        brighten.inputs[1].default_value = 0.6
        brighten.inputs[2].default_value = HOUSE_GREY * 0.55
        if colour is None:
            brighten.inputs[0].default_value = 0.5
        else:
            grey = tree.nodes.new("ShaderNodeRGBToBW")
            tree.links.new(colour, grey.inputs["Color"])
            tree.links.new(grey.outputs["Val"], brighten.inputs[0])
        tree.links.new(brighten.outputs["Value"], emission.inputs["Color"])
    target = tree.nodes.new("ShaderNodeTexImage")
    target.name = "BakeTarget"
    tree.nodes.active = target
    return material


def prepare_bake_materials(obj, atlas):
    """Splits each slot into a plain and a house-colour bake material, every one writing to atlas."""
    house = obj.data.attributes["house"].data
    sources = list(obj.data.materials)
    obj.data.materials.clear()
    remap = {}
    for index, source in enumerate(sources):
        for flag in (False, True):
            material = make_bake_material(source, flag)
            material.node_tree.nodes["BakeTarget"].image = atlas
            obj.data.materials.append(material)
            remap[(index, flag)] = len(obj.data.materials) - 1
    for polygon in obj.data.polygons:
        polygon.material_index = remap[(polygon.material_index, house[polygon.index].value)]


def atlas_uvs(obj):
    obj.data.uv_layers.new(name="Atlas")
    obj.data.uv_layers.active = obj.data.uv_layers["Atlas"]
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=1.15, island_margin=0.004, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode="OBJECT")


def bake(obj, name):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 64
    scene.render.bake.margin = 6

    colour = bpy.data.images.new(f"{name}_colour", ATLAS_SIZE, ATLAS_SIZE, float_buffer=True)
    occlusion = bpy.data.images.new(f"{name}_ao", ATLAS_SIZE, ATLAS_SIZE, float_buffer=True)
    atlas_uvs(obj)
    prepare_bake_materials(obj, colour)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type="EMIT")
    for material in obj.data.materials:
        material.node_tree.nodes["BakeTarget"].image = occlusion
    scene.cycles.samples = 128
    bpy.ops.object.bake(type="AO")

    pixels = numpy.array(colour.pixels[:]).reshape(-1, 4)
    ao = numpy.array(occlusion.pixels[:]).reshape(-1, 4)[:, :1]
    pixels[:, :3] = numpy.clip(pixels[:, :3] * (1.0 - AO_STRENGTH + AO_STRENGTH * ao), 0.0, 1.0)
    pixels[:, 3] = 1.0
    final = bpy.data.images.new(f"T_{name}", ATLAS_SIZE, ATLAS_SIZE)
    final.colorspace_settings.name = "sRGB"
    # The baked values are linear; the PNG is sRGB.
    linear = pixels[:, :3]
    pixels[:, :3] = numpy.where(linear <= 0.0031308, linear * 12.92, 1.055 * numpy.power(linear, 1 / 2.4) - 0.055)
    final.pixels[:] = pixels.ravel()
    final.filepath_raw = os.path.join(MODEL_DIR, f"T_{name}.png")
    final.file_format = "PNG"
    final.save()
    return final


def finish_materials(obj, texture):
    """Body and HouseColor, both showing the atlas through the Atlas UVs; the source UVs go."""
    # Names can pick up a .001 suffix when the hull's bake materials already took them.
    is_house = ["_House" in m.name for m in obj.data.materials]
    house_by_face = [is_house[p.material_index] for p in obj.data.polygons]
    obj.data.materials.clear()
    for role in ("Body", "HouseColor"):
        # Named after the slot for the export; main() renames them afterwards so the next object can use the names.
        material = bpy.data.materials.new(role)
        material.use_nodes = True
        tree = material.node_tree
        tree.nodes.clear()
        output = tree.nodes.new("ShaderNodeOutputMaterial")
        shader = tree.nodes.new("ShaderNodeBsdfPrincipled")
        shader.inputs["Roughness"].default_value = 0.7
        image = tree.nodes.new("ShaderNodeTexImage")
        image.image = texture
        tree.links.new(image.outputs["Color"], shader.inputs["Base Color"])
        tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
        obj.data.materials.append(material)
    for polygon, house in zip(obj.data.polygons, house_by_face):
        polygon.material_index = 1 if house else 0
    obj.data.uv_layers.remove(obj.data.uv_layers["UVMap"])
    obj.data.attributes.remove(obj.data.attributes["house"])


def externalise_images(glb_path):
    """Rewrites a .glb so its embedded images become PNG files next to it, referenced by uri (as w3d_to_glb_blender.py
    does): placeholder_art.py only imports external textures."""
    with open(glb_path, "rb") as file:
        data = file.read()
    json_length = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + json_length])
    binary_chunk = data[20 + json_length:]
    binary = binary_chunk[8:8 + struct.unpack_from("<I", binary_chunk, 0)[0]]
    for image in gltf.get("images", []):
        if "bufferView" not in image:
            continue
        view = gltf["bufferViews"][image.pop("bufferView")]
        image.pop("mimeType", None)
        file_name = f"{image['name']}.png"
        start = view.get("byteOffset", 0)
        with open(os.path.join(os.path.dirname(glb_path), file_name), "wb") as file:
            file.write(binary[start:start + view["byteLength"]])
        image["uri"] = file_name
    encoded = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    with open(glb_path, "wb") as file:
        file.write(struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(encoded) + len(binary_chunk)))
        file.write(struct.pack("<I4s", len(encoded), b"JSON"))
        file.write(encoded)
        file.write(binary_chunk)


def export(obj, file_name):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    path = os.path.join(MODEL_DIR, file_name)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_image_format="AUTO",
                              export_animations=False, export_skins=False, export_yup=True, export_materials="EXPORT",
                              export_cameras=False, export_lights=False)
    externalise_images(path)
    print(f"wrote {path}")


def render_previews(hull, turret):
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 32
    scene.render.resolution_x, scene.render.resolution_y = 1400, 900
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.35, 0.37, 0.4, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    scene.world = world
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 4.0
    sun.rotation_euler = (0.7, 0.2, 0.9)
    scene.collection.objects.link(sun)
    camera = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    turret.location = Vector(json.loads(open(os.path.join(MODEL_DIR, "abrams.json")).read())["blender_ring"])

    def tint(team):
        # The game's team material: blend towards the texture's brightness times the lifted team colour.
        for obj, strengths in ((hull, (0.15, 0.9)), (turret, (0.15, 0.9))):
            for material, strength in zip(obj.data.materials, strengths):
                tree = material.node_tree
                shader = tree.nodes["Principled BSDF"]
                image = next(n for n in tree.nodes if n.type == "TEX_IMAGE")
                for node in [n for n in tree.nodes if n.name.startswith("Team")]:
                    tree.nodes.remove(node)
                grey = tree.nodes.new("ShaderNodeRGBToBW")
                grey.name = "TeamGrey"
                tree.links.new(image.outputs["Color"], grey.inputs["Color"])
                colour = tree.nodes.new("ShaderNodeMix")
                colour.name = "TeamColour"
                colour.data_type = "RGBA"
                colour.blend_type = "MULTIPLY"
                colour.inputs["Factor"].default_value = 1.0
                colour.inputs["B"].default_value = tuple(c * 1.6 for c in team) + (1.0,)
                tree.links.new(grey.outputs["Val"], colour.inputs["A"])
                blend = tree.nodes.new("ShaderNodeMix")
                blend.name = "TeamBlend"
                blend.data_type = "RGBA"
                blend.inputs["Factor"].default_value = strength
                tree.links.new(image.outputs["Color"], blend.inputs["A"])
                tree.links.new(colour.outputs["Result"], blend.inputs["B"])
                tree.links.new(blend.outputs["Result"], shader.inputs["Base Color"])

    views = {"front34": (Vector((11.0, -8.0, 7.0)), 50), "rts": (Vector((-6.0, -9.0, 14.0)), 45)}
    for team_name, team, turret_yaw in (("blue", (0.05, 0.25, 0.9), 0.0), ("red", (0.85, 0.08, 0.05), 0.6)):
        tint(team)
        turret.rotation_euler = (0.0, 0.0, turret_yaw)
        for view, (position, lens) in views.items():
            camera.location = position
            camera.rotation_euler = (Vector((0.0, 0.0, 1.0)) - position).to_track_quat("-Z", "Y").to_euler()
            camera.data.lens = lens
            scene.render.filepath = os.path.join(PREVIEW_DIR, f"abrams_{team_name}_{view}.png")
            bpy.ops.render.render(write_still=True)
    turret.location = (0.0, 0.0, 0.0)
    turret.rotation_euler = (0.0, 0.0, 0.0)


def main():
    import_source()
    turret_parts, hull_parts = [], []
    ring_box = None
    for obj in list(bpy.data.objects):
        if obj.name == "Mesh_0045":
            drop_antennas(obj)
        mark_house_color(obj, obj.name)
        (turret_parts if obj.name in TURRET_PARTS else hull_parts).append(obj)
    gun_box = bounds(bpy.data.objects[GUN_PART])
    for obj in turret_parts + hull_parts:
        decimate(obj)

    hull = join(hull_parts, "Hull")
    turret = join(turret_parts, "Turret")
    hull_low, hull_high = bounds(hull)
    centre = Vector(((hull_low.x + hull_high.x) / 2.0, (hull_low.y + hull_high.y) / 2.0, hull_low.z))
    # The ring sits on the gun's centre line, on the hull roof, half way along the hull footprint's turret opening.
    ring = Vector((0.0, centre.y, 174.0))
    scale_turret(turret, ring, gun_box)
    muzzle = Vector(((gun_box[0].x + gun_box[1].x) / 2.0, gun_box[0].y + (gun_box[1].y - gun_box[0].y) * GUN_LENGTH_SCALE,
                     (gun_box[0].z + gun_box[1].z) / 2.0))
    muzzle = ring + (muzzle - ring) * TURRET_SCALE

    to_game_space(hull, centre)
    to_game_space(turret, ring)
    ring_m = (Matrix.Scale(0.01, 4) @ Matrix.Rotation(-1.5707963, 4, "Z")) @ (ring - centre)
    muzzle_m = (Matrix.Scale(0.01, 4) @ Matrix.Rotation(-1.5707963, 4, "Z")) @ (muzzle - ring)

    # Unreal: centimetres, and y flips going through glTF.
    with open(os.path.join(MODEL_DIR, "abrams.json"), "w") as file:
        json.dump({
            "turret_offset": [ring_m.x * 100.0, -ring_m.y * 100.0, ring_m.z * 100.0],
            "muzzle_offset": [muzzle_m.x * 100.0, -muzzle_m.y * 100.0, muzzle_m.z * 100.0],
            "blender_ring": list(ring_m),
        }, file, indent=2)
    for obj, name, file_name in ((hull, "Abrams_Hull", "abrams_hull.glb"), (turret, "Abrams_Turret", "abrams_turret.glb")):
        texture = bake(obj, name)
        finish_materials(obj, texture)
        triangles = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        print(f"{obj.name}: {triangles} triangles")
        export(obj, file_name)
        for material in obj.data.materials:
            material.name = f"{obj.name}_{material.name}"
    render_previews(hull, turret)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(MODEL_DIR, "abrams.blend"))


main()
