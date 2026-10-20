"""The top-down preview of a map the lobby and the minimap show (#289): rendered through URTSEditorScripting::CapturePreview
when the run has a renderer, drawn from the level's actors otherwise, and imported as a UI texture. Used by bake_map.py and
setup_testmap.py.
"""

import math
import os
import struct
import zlib

import unreal

MAP_DATA_DIR = "/Game/Data/Maps"
# The minimap shows it at 280 px; the lobby at 256.
PREVIEW_SIZE = 512
PREVIEW_GROUND = (86, 98, 70)
PREVIEW_OBSTACLE = (58, 58, 64)
PREVIEW_WATER = (40, 70, 110)
PREVIEW_SUPPLY = (214, 176, 62)
# Package metadata key telling whether a map preview was rendered or drawn.
PREVIEW_SOURCE_TAG = "PreviewSource"

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def map_preview_path(map_path):
    return f"{MAP_DATA_DIR}/T_Preview_{map_path.rsplit('/', 1)[1]}"


def preview_position(area, x, y):
    """Where world (x, y) is on a preview of area, (min x, min y, max x, max y), from the left and from the top in 0..1,
    as URTSMapData::ToPreviewPosition: world +X is up, world +Y is right."""
    min_x, min_y, max_x, max_y = area
    return (y - min_y) / (max_y - min_y), (max_x - x) / (max_x - min_x)


def preview_box(area):
    min_x, min_y, max_x, max_y = area
    box = unreal.Box2D()
    box.set_editor_property("min", unreal.Vector2D(min_x, min_y))
    box.set_editor_property("max", unreal.Vector2D(max_x, max_y))
    box.set_editor_property("is_valid", True)
    return box


def write_png(file_name, size, pixels):
    """An 8-bit RGB PNG of size x size pixels, pixels being the rows top to bottom as bytearrays."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(row) for row in pixels)
    with open(file_name, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n")
        file.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)))
        file.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        file.write(chunk(b"IEND", b""))


def draw_map_preview(area, file_name):
    """The loaded level as a PNG at file_name: the ground with its obstacles, water and supply sources, drawn from their
    bounds, for runs without a renderer."""
    pixels = [bytearray(PREVIEW_GROUND * PREVIEW_SIZE) for _ in range(PREVIEW_SIZE)]

    def fill(actor, color):
        origin, extent = actor.get_actor_bounds(False)
        left, top = preview_position(area, origin.x + extent.x, origin.y - extent.y)
        right, bottom = preview_position(area, origin.x - extent.x, origin.y + extent.y)
        columns = range(max(0, int(left * PREVIEW_SIZE)), min(PREVIEW_SIZE, math.ceil(right * PREVIEW_SIZE)))
        for row in range(max(0, int(top * PREVIEW_SIZE)), min(PREVIEW_SIZE, math.ceil(bottom * PREVIEW_SIZE))):
            for column in columns:
                pixels[row][column * 3:column * 3 + 3] = bytes(color)

    # Sorted, so the image does not depend on the order the level lists its actors in.
    for actor in sorted(actors.get_all_level_actors(), key=lambda actor: actor.get_actor_label()):
        label = actor.get_actor_label()
        if isinstance(actor, unreal.StaticMeshActor) and label.startswith(("Wall_", "Block_")):
            fill(actor, PREVIEW_OBSTACLE)
        elif isinstance(actor, unreal.StaticMeshActor) and label.startswith("Water_"):
            fill(actor, PREVIEW_WATER)
        elif isinstance(actor, unreal.RTSSupplySource):
            fill(actor, PREVIEW_SUPPLY)
    write_png(file_name, PREVIEW_SIZE, pixels)


def build_preview(map_path, area, texture_path):
    """A top-down image of the loaded level map_path over area, rendered when the run has a renderer and drawn otherwise,
    imported as texture_path in memory, not saved. Returns the texture."""
    directory, name = texture_path.rsplit("/", 1)
    source = os.path.join(PROJECT_DIR, "Saved", "Setup", "Previews", f"{name}.png")
    os.makedirs(os.path.dirname(source), exist_ok=True)
    rendered = unreal.RTSEditorScripting.can_render()
    if rendered:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        if not unreal.RTSEditorScripting.capture_preview(world, preview_box(area), PREVIEW_SIZE, source):
            raise RuntimeError(f"Could not render {map_path}")
    else:
        draw_map_preview(area, source)

    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = directory
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    texture = unreal.load_asset(texture_path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    # Shown as is in the lobby: no mips, no compression artefacts on the thin walls.
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    assets.set_metadata_tag(texture, PREVIEW_SOURCE_TAG, "Rendered" if rendered else "Drawn")
    return texture
