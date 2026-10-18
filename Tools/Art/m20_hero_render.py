"""The M20 Guardian's selection-panel hero art (#241): the saved scene rendered on a transparent film, with only the tank's
contact shadow kept by a shadow-catcher ground, so the HUD shows through around it.

Usage (Blender 5.1):
  blender -b Art/USA/vehicles/m20_guardian/source/M20_Guardian_V5.blend --python Tools/Art/m20_hero_render.py -- [--samples N]

Writes Art/USA/vehicles/m20_guardian/renders/m20_hero_cutout.png (2048 x 1024). Tools/Scripts/unit_icon.py --art turns it
into the UI texture and sprite.
"""

import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m20_guardian_render as presentation  # noqa: E402

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_PATH = os.path.join(PROJECT_DIR, "Art", "USA", "vehicles", "m20_guardian", "renders", "m20_hero_cutout.png")
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SAMPLES = int(ARGS[ARGS.index("--samples") + 1]) if "--samples" in ARGS else 256

# A low three-quarter front view, the gun leading to the right, where the panel's text sits.
CAMERA_LOCATION = (10.4, -10.6, 2.6)
CAMERA_TARGET = (-0.2, 0.0, 1.05)
LENS = 50.0
SIZE = (2048, 1024)


def neutral_team_areas():
    """The baked base colour carries the team mask in its alpha; grey the masked panels so the art bakes in no team colour."""
    for material in bpy.data.materials:
        if not material.use_nodes:
            continue
        nodes, links = material.node_tree.nodes, material.node_tree.links
        shader = nodes.get("Principled BSDF")
        base = shader.inputs["Base Color"] if shader else None
        source = base.links[0].from_node if base and base.is_linked else None
        if not source or source.type != "TEX_IMAGE":
            continue
        grey = nodes.new("ShaderNodeHueSaturation")
        grey.inputs["Saturation"].default_value = 0.0
        grey.inputs["Value"].default_value = 0.8
        links.new(source.outputs["Color"], grey.inputs["Color"])
        mix = nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        links.new(source.outputs["Alpha"], mix.inputs["Factor"])
        links.new(source.outputs["Color"], mix.inputs["A"])
        links.new(grey.outputs["Color"], mix.inputs["B"])
        links.new(mix.outputs["Result"], base)


def main():
    neutral_team_areas()
    camera = presentation.setup(SAMPLES, "studio")
    scene = bpy.context.scene
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    bpy.data.objects["Ground"].is_shadow_catcher = True
    presentation.shoot(camera, OUT_PATH, CAMERA_LOCATION, CAMERA_TARGET, lens=LENS, size=SIZE)
    presentation.teardown()
    print(f"Wrote {OUT_PATH}")


main()
