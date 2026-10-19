"""Bakes the tank gun firing flipbooks (#294, #303) from our own Mantaflow simulation of a gun blast: a burst of burning
propellant gas thrown out of a small sphere in a still box, which flares, billows into a fireball and cools into a
rolling cloud of smoke that thins as it drifts. Nothing is copied from footage; docs/art/refs/muzzle_flash/README.md
says what each layer looks like and where that comes from.

    blender -b --factory-startup --python Tools/Art/muzzle_fx_blender.py -- [--preview] [--samples N] [--no-bake]

Writes to Art/Source/Effects (setup_testmap.py imports them):
- T_MuzzleFire.png: the flame of the first FIRE_FRAMES frames as greyscale heat in RGB and its coverage in alpha; the
  material ramps it from white-yellow through orange to dark red;
- T_MuzzleSmoke_RLT.png and T_MuzzleSmoke_BBF.png: the smoke of the whole sim lit from six sides (six-way lightmaps):
  lit from the right, left and top in RGB of the first and its opacity in alpha; from the bottom, back and front in RGB
  of the second. Right, up and front are the sprite's: +X, +Z and toward the camera (-Y) here.
Each is 8 x 8 frames, row by row from the top left. --preview bakes coarse and small into Saved/FxBake/preview instead,
to check the look quickly. The sim cache and the frames are in Saved/FxBake.
"""

import math
import os
import shutil
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import m20_guardian_bake as bake  # noqa: E402

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Effects")
WORK_DIR = os.path.join(PROJECT_DIR, "Saved", "FxBake")

GRID = 8  # frames per side of a flipbook
DOMAIN_SIZE = 3.0  # m
BURST_Z = -0.3  # m; the burst sits low in the box, so the smoke has room to rise
SIM_FRAMES = 96  # at 24 fps
INFLOW_FRAMES = 3  # the burst: gas flows in for this many frames, then the source shuts
FIRE_FRAMES = (1, 15)  # the flame's frames, spread over the 64 of T_MuzzleFire
SMOKE_FRAMES = (2, 44)  # the smoke's, spread over the 64 of the smoke flipbooks

# The six-way lights, from where each shines, in the camera's frame: right +X, up +Z, front -Y (toward the camera).
SIX_WAY = (("right", (1, 0, 0)), ("left", (-1, 0, 0)), ("top", (0, 0, 1)),
           ("bottom", (0, 0, -1)), ("back", (0, 1, 0)), ("front", (0, -1, 0)))


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = {"preview": "--preview" in argv, "bake": "--no-bake" not in argv, "samples": None}
    if "--samples" in argv:
        args["samples"] = int(argv[argv.index("--samples") + 1])
    return args


def clear_scene():
    """Empties the startup scene (run with --factory-startup). Not read_factory_settings(use_empty=True): Mantaflow
    crashes setting up a domain in that scene."""
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)


def build_sim(preview, cache_dir):
    """The box and the burst. Returns the domain."""
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = 1, SIM_FRAMES
    scene.render.fps = 24

    bpy.ops.mesh.primitive_cube_add(size=DOMAIN_SIZE, location=(0, 0, 0))
    domain = bpy.context.active_object
    domain.name = "Domain"
    domain.modifiers.new("Fluid", "FLUID").fluid_type = "DOMAIN"
    settings = domain.modifiers["Fluid"].domain_settings
    settings.domain_type = "GAS"
    settings.resolution_max = 48 if preview else 128
    settings.cache_type = "ALL"
    settings.cache_frame_start, settings.cache_frame_end = 1, SIM_FRAMES
    settings.use_adaptive_domain = False
    # Hot gas rises a little; smoke is about as heavy as air.
    settings.alpha = 0.0
    settings.beta = 0.4
    settings.vorticity = 0.6
    settings.burning_rate = 0.55
    settings.flame_smoke = 1.6
    settings.flame_vorticity = 0.8
    settings.flame_ignition = 1.2
    settings.flame_max_temp = 3.0
    settings.use_dissolve_smoke = True
    settings.dissolve_speed = 70
    settings.use_dissolve_smoke_log = True
    # Fine detail on top of the coarse sim: the ragged edge of the fireball and the curls of the smoke.
    settings.use_noise = True
    settings.noise_scale = 2
    settings.noise_strength = 2.0
    settings.noise_pos_scale = 1.6
    for side in ("front", "back", "left", "right", "top", "bottom"):
        setattr(settings, f"use_collision_border_{side}", False)

    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.14, location=(0, 0, BURST_Z), segments=24, ring_count=12)
    source = bpy.context.active_object
    source.name = "Burst"
    source.modifiers.new("Fluid", "FLUID").fluid_type = "FLOW"
    flow = source.modifiers["Fluid"].flow_settings
    flow.flow_type = "BOTH"
    flow.flow_behavior = "INFLOW"
    flow.flow_source = "MESH"
    flow.surface_distance = 0.6
    flow.volume_density = 1.0
    flow.fuel_amount = 2.5
    flow.temperature = 3.0
    flow.density = 1.0
    flow.smoke_color = (0.6, 0.58, 0.55)
    flow.subframes = 2
    flow.use_initial_velocity = True
    flow.velocity_normal = 30.0
    flow.velocity_random = 1.0
    flow.use_texture = False
    flow.use_inflow = True
    flow.keyframe_insert("use_inflow", frame=INFLOW_FRAMES)
    flow.use_inflow = False
    flow.keyframe_insert("use_inflow", frame=INFLOW_FRAMES + 1)
    source.hide_render = True

    # Turbulence tears the burst into ragged lobes and curls instead of a smooth ball.
    bpy.ops.object.effector_add(type="TURBULENCE", location=(0, 0, BURST_Z))
    turbulence = bpy.context.active_object
    turbulence.field.strength = 14.0
    turbulence.field.size = 0.35
    turbulence.field.flow = 0.0
    turbulence.field.noise = 0.0
    turbulence.field.seed = 294
    # Last: changing a setting of a domain frees its cache, so with --no-bake the baked one must be pointed at only now.
    settings.cache_directory = cache_dir
    return domain


def bake_sim(domain):
    with bpy.context.temp_override(scene=bpy.context.scene, active_object=domain, object=domain, selected_objects=[domain]):
        bpy.ops.fluid.bake_all()


def volume_material(name, kind):
    """kind "fire": black absorbing flame that glows white by its heat; "smoke": white smoke that only scatters."""
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    volume = nodes.new("ShaderNodeVolumePrincipled")
    links.new(volume.outputs["Volume"], output.inputs["Volume"])
    density = nodes.new("ShaderNodeAttribute")
    density.attribute_type = "GEOMETRY"
    scale = nodes.new("ShaderNodeMath")
    scale.operation = "MULTIPLY"
    if kind == "fire":
        density.attribute_name = "flame"
        scale.inputs[1].default_value = 6.0
        volume.inputs["Color"].default_value = (0, 0, 0, 1)
        volume.inputs["Emission Color"].default_value = (1, 1, 1, 1)
        glow = nodes.new("ShaderNodeMath")
        glow.operation = "MULTIPLY"
        glow.inputs[1].default_value = 3.0
        links.new(density.outputs["Fac"], glow.inputs[0])
        links.new(glow.outputs["Value"], volume.inputs["Emission Strength"])
    else:
        density.attribute_name = "density"
        scale.inputs[1].default_value = 20.0
        volume.inputs["Color"].default_value = (0.92, 0.92, 0.92, 1)
        volume.inputs["Emission Strength"].default_value = 0.0
    volume.inputs["Density Attribute"].default_value = ""
    links.new(density.outputs["Fac"], scale.inputs[0])
    links.new(scale.outputs["Value"], volume.inputs["Density"])
    return material


def build_render(preview, samples):
    scene = bpy.context.scene
    bake.use_gpu(scene)
    scene.cycles.samples = samples or (16 if preview else 96)
    scene.cycles.use_denoising = True
    scene.cycles.volume_bounces = 2
    scene.cycles.volume_step_rate = 1.0 if preview else 0.5
    scene.render.film_transparent = True
    scene.render.use_persistent_data = True
    size = 96 if preview else 256
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_depth = "32"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0

    world = bpy.data.worlds.new("Black")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.0
    scene.world = world

    camera = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = DOMAIN_SIZE
    camera.data.clip_end = 100.0
    camera.location = (0, -20, 0)
    camera.rotation_euler = (math.radians(90), 0, 0)
    scene.collection.objects.link(camera)
    scene.camera = camera

    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 3.0
    sun.data.angle = math.radians(2.0)
    scene.collection.objects.link(sun)
    return sun


def frame_list(first, last):
    """GRID * GRID sim frames from first to last, evenly spread (rounded; the sim cache holds whole frames)."""
    return [round(first + (last - first) * i / (GRID * GRID - 1)) for i in range(GRID * GRID)]


def render_exr(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(path)
    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(image.size[1], image.size[0], 4)[::-1]
    bpy.data.images.remove(image)
    return pixels


def frame_to_fit(frames, alpha_of):
    """Points the camera at the union of what the given frames show, plus a margin, so the flipbook frames are filled:
    renders them small and fast first and measures where their alpha is."""
    scene, camera = bpy.context.scene, bpy.context.scene.camera
    camera.data.ortho_scale, camera.location.x, camera.location.z = DOMAIN_SIZE, 0.0, 0.0
    size, samples = scene.render.resolution_x, scene.cycles.samples
    scene.render.resolution_x = scene.render.resolution_y = 128
    scene.cycles.samples = 8
    union = np.zeros((128, 128), dtype=bool)
    for frame in frames:
        scene.frame_set(frame)
        union |= alpha_of(render_exr(os.path.join(WORK_DIR, "fit.exr"))) > 0.08
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.cycles.samples = samples
    rows, cols = np.nonzero(union)
    if not len(rows):
        return
    # Pixel rows run from the top of the frame; the camera looks along +Y, so +X is right and +Z up.
    cell = DOMAIN_SIZE / 128
    x0, x1 = (cols.min() - 64) * cell, (cols.max() + 1 - 64) * cell
    z1, z0 = (64 - rows.min()) * cell, (64 - rows.max() - 1) * cell
    camera.location.x, camera.location.z = (x0 + x1) / 2, (z0 + z1) / 2
    camera.data.ortho_scale = max(x1 - x0, z1 - z0) * 1.08


def render_frames(domain, sun, frames_dir):
    """Renders each frame of the fire and the six lit smoke views. Returns {"fire": [...], "right": [...], ...}."""
    scene = bpy.context.scene
    fire_material = volume_material("Fire", "fire")
    smoke_material = volume_material("Smoke", "smoke")
    domain.data.materials.clear()
    domain.data.materials.append(fire_material)
    out = {"fire": []}
    sun.hide_render = True
    frame_to_fit(frame_list(*FIRE_FRAMES)[::3], lambda pixels: pixels[..., 0])
    for index, frame in enumerate(frame_list(*FIRE_FRAMES)):
        scene.frame_set(frame)
        out["fire"].append(render_exr(os.path.join(frames_dir, f"fire_{index:02d}.exr")))

    domain.data.materials[0] = smoke_material
    sun.hide_render = False
    sun.rotation_euler = (math.radians(30), 0, 0)
    frame_to_fit(frame_list(*SMOKE_FRAMES)[::3], lambda pixels: pixels[..., 3])
    for name, origin in SIX_WAY:
        sun.rotation_euler = (-Vector(origin)).to_track_quat("-Z", "Y").to_euler()
        out[name] = []
        for index, frame in enumerate(frame_list(*SMOKE_FRAMES)):
            scene.frame_set(frame)
            out[name].append(render_exr(os.path.join(frames_dir, f"smoke_{name}_{index:02d}.exr")))
    return out


def edge_fade(size):
    """1 inside a frame, falling smoothly to 0 over its outer tenth, so no frame shows the edge of its square."""
    ramp = np.clip(np.minimum(np.arange(size) + 0.5, size - np.arange(size) - 0.5) / (size * 0.1), 0.0, 1.0)
    ramp = ramp * ramp * (3.0 - 2.0 * ramp)
    return np.outer(ramp, ramp)


def atlas(frames, channel, fade=True):
    """GRID x GRID frames of one channel, row by row from the top left, faded out at their edges."""
    mask = edge_fade(frames[0].shape[0]) if fade else 1.0
    rows = [np.concatenate([frames[r * GRID + c][..., channel] * mask for c in range(GRID)], axis=1) for r in range(GRID)]
    return np.concatenate(rows, axis=0)


def save_png(path, rgba):
    height, width = rgba.shape[:2]
    image = bpy.data.images.new(os.path.basename(path), width, height, alpha=True)
    image.colorspace_settings.name = "Non-Color"
    image.pixels[:] = np.clip(rgba[::-1], 0.0, 1.0).astype(np.float32).ravel()
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)


def write_flipbooks(frames, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    # Fire: the glow normalised to its brightest frame, its coverage as alpha.
    heat = atlas(frames["fire"], 0)
    heat = heat / max(np.percentile(heat, 99.9), 1e-6)
    cover = atlas(frames["fire"], 3)
    save_png(os.path.join(out_dir, "T_MuzzleFire.png"), np.stack([heat, heat, heat, cover], axis=-1))

    # Smoke: the six lit views normalised together, so their ratios (the lighting) hold.
    lit = {name: atlas(frames[name], 0) for name, _ in SIX_WAY}
    peak = max(np.percentile(view, 99.9) for view in lit.values())
    lit = {name: view / max(peak, 1e-6) for name, view in lit.items()}
    # The thinnest haze would fill each frame as a faint square: cut it, so a puff has a soft but closed edge.
    alpha = atlas(frames["front"], 3)
    alpha = np.clip((alpha - 0.06) / 0.6, 0.0, 1.0)
    alpha = alpha * alpha * (3.0 - 2.0 * alpha)
    save_png(os.path.join(out_dir, "T_MuzzleSmoke_RLT.png"), np.stack([lit["right"], lit["left"], lit["top"], alpha], axis=-1))
    save_png(os.path.join(out_dir, "T_MuzzleSmoke_BBF.png"),
             np.stack([lit["bottom"], lit["back"], lit["front"], np.ones_like(alpha)], axis=-1))


def main():
    args = parse_args()
    work = os.path.join(WORK_DIR, "preview" if args["preview"] else "full")
    cache_dir = os.path.join(work, "cache")
    frames_dir = os.path.join(work, "frames")
    clear_scene()
    domain = build_sim(args["preview"], cache_dir)
    if args["bake"]:
        print("muzzle_fx: baking the sim", flush=True)
        shutil.rmtree(cache_dir, ignore_errors=True)
        bake_sim(domain)
    sun = build_render(args["preview"], args["samples"])
    os.makedirs(frames_dir, exist_ok=True)
    print("muzzle_fx: rendering", flush=True)
    frames = render_frames(domain, sun, frames_dir)
    write_flipbooks(frames, work if args["preview"] else OUT_DIR)
    print("muzzle_fx: done", flush=True)


main()
