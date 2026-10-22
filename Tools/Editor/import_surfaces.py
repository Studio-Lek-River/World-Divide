"""Imports the scanned surfaces that ue.ps1 -Task surfaces prepared in Saved/Surfaces/ (#330).

The sources are the producer's Megascans (and other scanned) surface ZIPs: third-party textures the public repository may
not hold, so /Game/Surfaces is git-ignored and never cooked like the packs (Config/DefaultGame.ini). ue.ps1 has already
scaled each set to 2K and packed its ORM (AO, roughness, metalness); Saved/Surfaces/manifest.json lists the sets.

Per set this writes /Game/Surfaces/<Name>/T_<Name>_BC (sRGB), T_<Name>_N (normal map) and T_<Name>_ORM (masks), and the
instance MI_<Name> of /Game/Surfaces/M_ScannedSurface with TileSize from the set's scan area. The master projects the
textures from above in world space, so a tile, a mesh or a landscape shows the surface at its real size without UVs.
Every run replaces what an earlier run wrote. The report goes to Saved/Surfaces/imported.md.

Run with Tools/Scripts/ue.ps1 -Task surfaces -Path <folder of ZIPs>.
"""

import json
import os

import unreal

STAGING_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Surfaces")
ROOT = "/Game/Surfaces"
MASTER = f"{ROOT}/M_ScannedSurface"

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def save(asset):
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def import_texture(source, folder, name, kind, flip_green):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{folder}/{name}")
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    if kind == "BaseColor":
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    elif kind == "Normal":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
        texture.set_editor_property("flip_green_channel", flip_green)
    else:
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR)
    save(texture)
    return texture


def node(material, kind, x, y):
    return editing.create_material_expression(material, kind, x, y)


def scalar(material, name, value, x, y):
    parameter = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    parameter.set_editor_property("parameter_name", name)
    parameter.set_editor_property("default_value", value)
    return parameter


def build_master(defaults):
    """World-aligned from above: UV = world XY / (TileSize m * 100). Base colour times Tint, the normal map, and the ORM's
    AO, roughness (times RoughnessScale) and metalness. defaults are the first set's textures, for the samplers' types."""
    if assets.does_asset_exist(MASTER):
        # Rebuilt in place: the instances of an earlier run keep their parent.
        material = unreal.load_asset(MASTER)
        editing.delete_all_material_expressions(material)
    else:
        material = tools.create_asset("M_ScannedSurface", ROOT, unreal.Material, unreal.MaterialFactoryNew())

    world = node(material, unreal.MaterialExpressionWorldPosition, -1600, 0)
    mask = node(material, unreal.MaterialExpressionComponentMask, -1400, 0)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    editing.connect_material_expressions(world, "", mask, "")
    tile = scalar(material, "TileSize", 2.0, -1600, 200)
    centimetres = node(material, unreal.MaterialExpressionConstant, -1600, 350)
    centimetres.set_editor_property("r", 100.0)
    size = node(material, unreal.MaterialExpressionMultiply, -1400, 250)
    editing.connect_material_expressions(tile, "", size, "A")
    editing.connect_material_expressions(centimetres, "", size, "B")
    uv = node(material, unreal.MaterialExpressionDivide, -1200, 100)
    editing.connect_material_expressions(mask, "", uv, "A")
    editing.connect_material_expressions(size, "", uv, "B")

    samplers = {}
    for index, (parameter, sampler_type) in enumerate((("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                                                        ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                                                        ("ORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS))):
        sampler = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, -300 + 350 * index)
        sampler.set_editor_property("parameter_name", parameter)
        sampler.set_editor_property("texture", defaults[parameter])
        sampler.set_editor_property("sampler_type", sampler_type)
        editing.connect_material_expressions(uv, "", sampler, "UVs")
        samplers[parameter] = sampler

    tint = node(material, unreal.MaterialExpressionVectorParameter, -600, -450)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    color = node(material, unreal.MaterialExpressionMultiply, -400, -300)
    editing.connect_material_expressions(samplers["BaseColor"], "RGB", color, "A")
    editing.connect_material_expressions(tint, "", color, "B")
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(samplers["Normal"], "RGB", unreal.MaterialProperty.MP_NORMAL)

    roughness_scale = scalar(material, "RoughnessScale", 1.0, -600, 550)
    roughness = node(material, unreal.MaterialExpressionMultiply, -400, 450)
    editing.connect_material_expressions(samplers["ORM"], "G", roughness, "A")
    editing.connect_material_expressions(roughness_scale, "", roughness, "B")
    editing.connect_material_property(samplers["ORM"], "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.connect_material_property(samplers["ORM"], "B", unreal.MaterialProperty.MP_METALLIC)

    editing.recompile_material(material)
    save(material)
    return material


def build_instance(name, folder, master, textures, tile_m):
    path = f"{folder}/MI_{name}"
    if assets.does_asset_exist(path):
        instance = unreal.load_asset(path)
    else:
        instance = tools.create_asset(f"MI_{name}", folder, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, master)
    for parameter, texture in textures.items():
        editing.set_material_instance_texture_parameter_value(instance, parameter, texture)
    editing.set_material_instance_scalar_parameter_value(instance, "TileSize", tile_m)
    editing.update_material_instance(instance)
    save(instance)
    return instance


def main():
    with open(os.path.join(STAGING_DIR, "manifest.json"), encoding="utf-8-sig") as file:
        manifest = json.load(file)
    sets = manifest.get("sets") or []
    if isinstance(sets, dict):
        sets = [sets]
    if not sets:
        raise RuntimeError("Saved/Surfaces/manifest.json lists no surface")

    imported = []
    for entry in sets:
        name = entry["name"]
        folder = f"{ROOT}/{name}"
        unreal.log(f"ImportSurfaces: {name}")
        textures = {kind: import_texture(entry["textures"][kind], folder, f"T_{name}_{suffix}", kind, entry["flip_green"])
                    for kind, suffix in (("BaseColor", "BC"), ("Normal", "N"), ("ORM", "ORM"))}
        imported.append((entry, folder, textures))

    master = build_master(imported[0][2])
    lines = ["# Imported surfaces", "", f"Generated by Tools/Editor/import_surfaces.py. Master `{MASTER}`.", "",
             "| Instance | Tile (m) | Source | Normal green flipped |", "|---|---|---|---|"]
    for entry, folder, textures in imported:
        build_instance(entry["name"], folder, master, textures, float(entry["tile_m"]))
        lines.append(f"| `{folder}/MI_{entry['name']}` | {float(entry['tile_m']):g} | {entry['source']} | "
                     f"{'yes' if entry['flip_green'] else 'no'} |")
    skipped = manifest.get("skipped") or []
    if isinstance(skipped, str):
        skipped = [skipped]
    lines += ["", f"Skipped: {len(skipped)}"] + [f"- {reason}" for reason in skipped]
    with open(os.path.join(STAGING_DIR, "imported.md"), "w", encoding="utf-8") as file:
        file.write("\n".join(lines) + "\n")
    unreal.log(f"ImportSurfaces: {len(imported)} surfaces in {ROOT}")


main()
