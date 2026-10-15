"""Presentation scene for the M20 Guardian (used by m20_guardian_blender.py): Cycles on the GPU, a physical sky with a
soft sun, one large soft key light for controlled highlights, a neutral concrete ground, AgX colour management.

Cameras: close-ups, the reference three-quarter view, orthographic views, and the game's RTS camera (spring arm pitch
and length from ARTSCameraPawn, Unreal's default 90 degree horizontal field of view).
"""

import math
import os

import bmesh
import bpy
import numpy as np
from mathutils import Vector

import m20_guardian_bake as bake

SUN_ELEVATION = math.radians(38.0)
SUN_AZIMUTH = math.radians(-78.0)  # from +X towards +Y: the sun is off the tank's right flank, a little ahead
RTS_FOV = math.radians(90.0)
RTS_TOP_DOWN_PITCH = 55.0
RTS_DEFAULT_ARM = 30.0  # m (3000 cm)


def ground_material():
    material = bpy.data.materials.new("Ground")
    material.use_nodes = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    shader = nodes["Principled BSDF"]
    coords = nodes.new("ShaderNodeTexCoord")
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 0.6
    noise.inputs["Detail"].default_value = 8.0
    links.new(coords.outputs["Object"], noise.inputs["Vector"])
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.16, 0.15, 0.135, 1.0)
    ramp.color_ramp.elements[1].color = (0.25, 0.235, 0.21, 1.0)
    links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], shader.inputs["Base Color"])
    shader.inputs["Roughness"].default_value = 0.88
    fine = nodes.new("ShaderNodeTexNoise")
    fine.inputs["Scale"].default_value = 60.0
    links.new(coords.outputs["Object"], fine.inputs["Vector"])
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.15
    links.new(fine.outputs["Fac"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    return material


def setup(samples):
    scene = bpy.context.scene
    bake.use_gpu(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.cycles.denoiser = "OPENIMAGEDENOISE"
    scene.cycles.max_bounces = 8
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.view_settings.exposure = -0.4
    scene.render.film_transparent = False

    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    nodes, links = world.node_tree.nodes, world.node_tree.links
    sky = nodes.new("ShaderNodeTexSky")
    sky.sky_type = "MULTIPLE_SCATTERING"
    sky.sun_disc = False
    sky.sun_elevation = SUN_ELEVATION
    sky.sun_rotation = SUN_AZIMUTH + math.pi / 2.0
    sky.air_density = 1.0
    sky.aerosol_density = 2.0
    links.new(sky.outputs["Color"], nodes["Background"].inputs["Color"])
    nodes["Background"].inputs["Strength"].default_value = 0.05
    scene.world = world

    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 5.0
    sun.data.angle = math.radians(3.5)
    sun.data.color = (1.0, 0.95, 0.88)
    direction = Vector((math.cos(SUN_ELEVATION) * math.cos(SUN_AZIMUTH),
                        math.cos(SUN_ELEVATION) * math.sin(SUN_AZIMUTH), math.sin(SUN_ELEVATION)))
    sun.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(sun)
    # A large soft key from front left for edge highlights on the armour facets.
    key = bpy.data.objects.new("Key", bpy.data.lights.new("Key", "AREA"))
    key.data.energy = 600.0
    key.data.size = 7.0
    key.data.color = (0.92, 0.95, 1.0)
    key.location = (7.0, 8.0, 7.0)
    key.rotation_euler = (Vector((0.0, 0.0, 1.2)) - key.location).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(key)

    mesh = bpy.data.meshes.new("Ground")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=400.0)
    bm.to_mesh(mesh)
    bm.free()
    ground = bpy.data.objects.new("Ground", mesh)
    ground.data.materials.append(ground_material())
    scene.collection.objects.link(ground)
    camera = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    return camera


def teardown():
    for name in ("Sun", "Key", "Ground", "Camera"):
        obj = bpy.data.objects.get(name)
        if obj:
            bpy.data.objects.remove(obj)


def shoot(camera, path, location, target, lens=50.0, ortho=None, size=(2400, 1500), fov=None):
    scene = bpy.context.scene
    scene.render.resolution_x, scene.render.resolution_y = size
    camera.location = location
    camera.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO" if ortho else "PERSP"
    if ortho:
        camera.data.ortho_scale = ortho
    camera.data.sensor_fit = "HORIZONTAL"
    if fov:
        camera.data.angle = fov
    else:
        camera.data.lens = lens
    camera.data.clip_end = 1000.0
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def rts_camera(arm, pitch_degrees, yaw_degrees, target=(0.3, 0.0, 1.0)):
    """Camera location for the game's spring arm: arm length in metres, pitch below the horizon, yaw of the view."""
    pitch, yaw = math.radians(pitch_degrees), math.radians(yaw_degrees)
    forward = Vector((math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), -math.sin(pitch)))
    return tuple(Vector(target) - forward * arm), target


def rts_pitch(arm, min_zoom=1.75, tilt_start=30.0, near=8.0, top_down=RTS_TOP_DOWN_PITCH):
    """ARTSCameraPawn::GetPitchForZoom, in metres and degrees below the horizon."""
    alpha = max(0.0, min(1.0, (arm - min_zoom) / (tilt_start - min_zoom)))
    smooth = alpha * alpha * (3.0 - 2.0 * alpha)
    return near + (top_down - near) * smooth


def side_by_side(left, right, path):
    images = [bpy.data.images.load(p) for p in (left, right)]
    width, height = images[0].size
    if tuple(images[1].size) != (width, height):
        images[1].scale(width, height)
    data = [np.array(image.pixels[:], np.float32).reshape(height, width, 4) for image in images]
    joined = np.concatenate(data, axis=1)
    out = bpy.data.images.new("compare", width * 2, height, alpha=True)
    out.pixels.foreach_set(joined.ravel())
    out.filepath_raw = path
    out.file_format = "PNG"
    out.save()
    for image in images + [out]:
        bpy.data.images.remove(image)


def render_all(render_dir, parts, previous_front34=None, preview=False):
    os.makedirs(render_dir, exist_ok=True)
    camera = setup(32 if preview else 256)
    prefix = "preview_" if preview else "m20_"

    def path(name):
        return os.path.join(render_dir, f"{prefix}{name}.png")

    # The v1 reference view (same camera and size as the first prototype).
    shoot(camera, path("front34"), (10.5, -8.0, 4.6), (0.4, 0.0, 0.95), lens=55.0, size=(1600, 1000))
    shoot(camera, path("side"), (0.6, -30.0, 1.3), (0.6, 0.0, 1.3), ortho=9.2, size=(1600, 1000))
    shoot(camera, path("top"), (0.6, 0.0, 30.0), (0.6, 0.0001, 0.0), ortho=9.2, size=(1600, 1000))
    shoot(camera, path("front"), (30.0, 0.0, 1.3), (0.0, 0.0, 1.3), ortho=4.6, size=(1600, 1000))
    if preview:
        shoot(camera, path("rear34"), (-9.0, 7.0, 4.0), (-0.6, 0.0, 1.1), lens=50.0, size=(1600, 1000))
        shoot(camera, path("close_turret"), (4.8, -4.4, 3.6), (0.3, 0.0, 1.85), lens=60.0, size=(1600, 1000))
        shoot(camera, path("close_smoke"), (2.5, -3.2, 3.0), (0.35, -1.20, 2.15), lens=75.0, size=(1600, 1000))
        shoot(camera, path("close_mantlet"), (4.3, -1.9, 2.4), (1.65, 0.0, 1.85), lens=65.0, size=(1600, 1000))
        shoot(camera, path("close_commander"), (1.5, -3.2, 3.3), (-0.30, -0.50, 2.35), lens=70.0, size=(1600, 1000))
        shoot(camera, path("close_running_gear"), (2.6, -5.8, 0.95), (-0.2, -1.5, 0.5), lens=50.0,
              size=(1600, 1000))
        teardown()
        return
    shoot(camera, path("hero34"), (8.6, -6.9, 3.0), (0.35, 0.0, 1.05), lens=58.0)
    shoot(camera, path("rear34"), (-8.6, 6.4, 3.6), (-0.6, 0.0, 1.1), lens=55.0)
    shoot(camera, path("close_turret"), (4.6, -4.1, 3.5), (0.35, 0.0, 1.85), lens=62.0)
    shoot(camera, path("close_commander"), (1.5, -3.2, 3.3), (-0.30, -0.50, 2.35), lens=70.0)
    shoot(camera, path("close_smoke"), (2.5, -3.2, 3.0), (0.35, -1.20, 2.15), lens=75.0)
    shoot(camera, path("close_mantlet"), (4.3, -1.9, 2.4), (1.65, 0.0, 1.85), lens=65.0)
    shoot(camera, path("close_running_gear"), (2.4, -5.6, 0.9), (-0.3, -1.5, 0.5), lens=50.0)
    for name, arm in (("rts_default", RTS_DEFAULT_ARM), ("rts_mid", 15.0)):
        location, target = rts_camera(arm, rts_pitch(arm), 215.0)
        shoot(camera, path(name), location, target, size=(1920, 1080), fov=RTS_FOV)
    # Pivot check: turret 45 degrees left, gun 10 degrees up, RWS turned to the right.
    parts["turret"].rotation_euler = (0.0, 0.0, math.radians(45.0))
    parts["gun"].rotation_euler = (0.0, math.radians(-10.0), 0.0)
    parts["rws"].rotation_euler = (0.0, 0.0, math.radians(-90.0))
    shoot(camera, path("pivot_test"), (10.5, -8.0, 4.6), (0.55, 0.0, 1.1), lens=50.0, size=(1600, 1000))
    for key in ("turret", "gun", "rws"):
        parts[key].rotation_euler = (0.0, 0.0, 0.0)
    teardown()
    if previous_front34 and os.path.exists(previous_front34):
        side_by_side(previous_front34, path("front34"), path("v2_v3_compare"))
