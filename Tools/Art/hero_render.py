"""Selection-panel hero art for any unit or building (docs/ui/specs/Selection.md, section 3): the opened Blender scene
rendered under the studio preset on a transparent film, with only its contact shadow kept by a shadow-catcher ground,
so the HUD shows through around it. Tools/Scripts/unit_icon.py --art turns the render into the UI texture and sprite.

Usage (Blender 5.1):
  blender -b <scene.blend> --python Tools/Art/hero_render.py -- --out <render.png> --camera x,y,z --target x,y,z
      [--lens 50] [--size 2048,1024] [--samples 256] [--light-scale 1] [--neutral Material,Material]

--light-scale moves and sizes the studio lights (set up around a 10 m tank) for a bigger or smaller subject, keeping the
light on it the same. --neutral greys the named team-coloured materials so the art bakes in no team colour. The blend
is never saved.

The USA HQ (#435):
  blender -b Art/USA/buildings/usa_hq/source/USA_HQ_Final.blend --python Tools/Art/hero_render.py --
      --out Art/USA/buildings/usa_hq/renders/hq_hero_cutout.png --camera 84,-101,96 --target 0,-2,6 --lens 42
      --light-scale 5 --neutral HQ17_TeamPanel,HQ17_TeamLight --textures Art/USA/buildings/usa_hq/textures
"""

import argparse
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m20_guardian_render as presentation  # noqa: E402

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def triple(text):
    values = tuple(float(value) for value in text.split(","))
    if len(values) != 3:
        raise argparse.ArgumentTypeError(f"{text} is not x,y,z")
    return values


def pair(text):
    values = tuple(int(value) for value in text.split(","))
    if len(values) != 2:
        raise argparse.ArgumentTypeError(f"{text} is not width,height")
    return values


def parse_args():
    parser = argparse.ArgumentParser(prog="hero_render.py")
    parser.add_argument("--out", required=True, help="the PNG to write, relative to the repository or absolute")
    parser.add_argument("--camera", required=True, type=triple, help="camera location in metres")
    parser.add_argument("--target", required=True, type=triple, help="the point the camera looks at, in metres")
    parser.add_argument("--lens", type=float, default=50.0)
    parser.add_argument("--size", type=pair, default=(2048, 1024))
    parser.add_argument("--samples", type=int, default=256)
    parser.add_argument("--light-scale", type=float, default=1.0)
    parser.add_argument("--neutral", default="", help="comma-separated team-coloured materials to grey")
    parser.add_argument("--textures", help="folder to find images the blend points at a path that no longer exists")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def relink_images(folder):
    """Point each image whose file is gone at the file of the same name in the folder; any still missing is an error,
    so a render never shows the missing-texture pink."""
    missing = []
    for image in bpy.data.images:
        if image.source != "FILE" or image.packed_file or os.path.exists(bpy.path.abspath(image.filepath)):
            continue
        candidate = os.path.join(folder, os.path.basename(bpy.path.abspath(image.filepath))) if folder else None
        if candidate and os.path.exists(candidate):
            image.filepath = candidate
            image.reload()
        else:
            missing.append(image.filepath)
    if missing:
        raise RuntimeError("missing images (pass --textures): " + ", ".join(missing))


def grey(colour):
    """The colour's luminance as a grey, keeping its alpha."""
    value = 0.2126 * colour[0] + 0.7152 * colour[1] + 0.0722 * colour[2]
    return (value, value, value, colour[3])


def neutral_materials(names):
    """Grey the base and emission colours of each named material; a name the scene lacks is an error."""
    for name in names:
        material = bpy.data.materials.get(name)
        if material is None or not material.use_nodes:
            raise RuntimeError(f"the scene has no node material {name}")
        for node in material.node_tree.nodes:
            if node.type != "BSDF_PRINCIPLED":
                continue
            for socket in ("Base Color", "Emission Color"):
                if node.inputs[socket].is_linked:
                    raise RuntimeError(f"{name}'s {socket} comes from a texture: grey it by its mask instead")
                node.inputs[socket].default_value = grey(node.inputs[socket].default_value)


def scale_lights(scale):
    """Move the studio lights out and size them up by the scale, with energy by its square so the subject is lit the same."""
    for obj in bpy.context.scene.objects:
        if obj.type == "LIGHT":
            obj.location = Vector(obj.location) * scale
            obj.data.size *= scale
            obj.data.energy *= scale * scale


def main():
    args = parse_args()
    out = args.out if os.path.isabs(args.out) else os.path.join(PROJECT_DIR, args.out)
    relink_images(args.textures and (args.textures if os.path.isabs(args.textures) else os.path.join(PROJECT_DIR, args.textures)))
    neutral_materials([name for name in args.neutral.split(",") if name])
    camera = presentation.setup(args.samples, "studio")
    scale_lights(args.light_scale)
    scene = bpy.context.scene
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    bpy.data.objects["Ground"].is_shadow_catcher = True
    presentation.shoot(camera, out, args.camera, args.target, lens=args.lens, size=args.size)
    presentation.teardown()
    print(f"Wrote {out}")


main()
