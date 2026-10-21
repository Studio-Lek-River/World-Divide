"""Imports a building modelled in several meshes from its folder under Art/ (the USA HQ, #191): the layout its
usa_hq.json gives (Tools/Art/usa_hq_recipe.py), one FBX per mesh at real size (cm, +X the front, Z up), the textures,
the master materials rebuilt here from the delivery's graphs (recipe/material_graphs.json) and their instances with
the delivery's values. Every mesh keeps the normals and UVs it was exported with, has no Nanite and no collision (the
building's data carries simple boxes instead) and gets LODs at the vehicles' switch distances (vehicle_art.build_lods).
Everything here builds in memory only; setup_testmap.py decides what needs rebuilding and saves it.

The masters read two custom primitive data channels the building writes to every one of its meshes: the owner's colour
at vehicle_art.TEAM_COLOR_INDEX (M_HQ_Final_Ownership's PlayerColor) and whether it is operational at
vehicle_art.LAMP_ON_INDEX (M_HQ_Final_Operational's Operational, multiplying its glow).
"""

import json
import os

import unreal

import vehicle_art

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editing = unreal.MaterialEditingLibrary

TEXTURE_COMPRESSION = {
    "TC_Default": unreal.TextureCompressionSettings.TC_DEFAULT,
    "TC_Normalmap": unreal.TextureCompressionSettings.TC_NORMALMAP,
    "TC_Masks": unreal.TextureCompressionSettings.TC_MASKS,
}
TEXTURE_GROUPS = {
    "TEXTUREGROUP_World": unreal.TextureGroup.TEXTUREGROUP_WORLD,
    "TEXTUREGROUP_WorldNormalMap": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP,
}

# The coating's roughness: the instance's Roughness moved by the 2 m texture's red around its mean, 0.67, and kept within
# these bounds (the delivery's calibration, recipe/material_finish.json).
COATING_ROUGHNESS_MEAN = 0.67
COATING_ROUGHNESS_RANGE = (0.06, 0.98)

# The flag's wind (M_HQ21_FlagWind): two travelling waves along the world position, pinned at the hoist by UV0.U
# squared, 18 cm at the fly, pushed along this direction. The normal follows the moved surface (screen derivatives).
FLAG_WAVE = (0.0, 0.008, 0.006)
FLAG_SPEED = 1.55
FLAG_RIPPLE = (2.7, 0.23)
FLAG_AMPLITUDE = 18.0
FLAG_DIRECTION = (1.0, 0.12, 0.18)
FLAG_ROUGHNESS = 0.85

# The ground masters' constant roughness.
SOIL_ROUGHNESS = 0.94


def read_layout(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def import_texture(source, directory, name, settings):
    """Imports one texture with the delivery's settings: compression, sRGB, LOD group and, for the 3072² paving, the
    2048² it is built at (mips and streaming as the group gives them)."""
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = directory
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{directory}/{name}")
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    texture.set_editor_property("srgb", settings["srgb"])
    texture.set_editor_property("compression_settings", TEXTURE_COMPRESSION[settings["compression"]])
    texture.set_editor_property("lod_group", TEXTURE_GROUPS[settings["lod_group"]])
    texture.set_editor_property("flip_green_channel", False)
    if settings["resize"]:
        texture.set_editor_property("power_of_two_mode", unreal.TexturePowerOfTwoSetting.RESIZE_TO_SPECIFIC_RESOLUTION)
        texture.set_editor_property("resize_during_build_x", settings["resize"][0])
        texture.set_editor_property("resize_during_build_y", settings["resize"][1])
    return texture


def _material(directory, name):
    """The material name in directory, emptied, to build again."""
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        editing.delete_all_material_expressions(material)
        return material
    return asset_tools.create_asset(name, directory, unreal.Material, unreal.MaterialFactoryNew())


def _node(material, kind, x, y, **properties):
    node = editing.create_material_expression(material, kind, x, y)
    for key, value in properties.items():
        node.set_editor_property(key, value)
    return node


def _connect(source, source_pin, target, target_pin):
    if not editing.connect_material_expressions(source, source_pin, target, target_pin):
        raise RuntimeError(f"Could not connect {source.get_name()}.{source_pin} to {target.get_name()}.{target_pin}")


def _output(material, source, source_pin, prop):
    if not editing.connect_material_property(source, source_pin, prop):
        raise RuntimeError(f"Could not connect {source.get_name()}.{source_pin} to {prop}")


def _vector(material, x, y, name, value, primitive_index=None):
    node = _node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                 default_value=unreal.LinearColor(*value))
    if primitive_index is not None:
        node.set_editor_property("use_custom_primitive_data", True)
        node.set_editor_property("primitive_data_index", primitive_index)
    return node


def _scalar(material, x, y, name, value, primitive_index=None):
    node = _node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)
    if primitive_index is not None:
        node.set_editor_property("use_custom_primitive_data", True)
        node.set_editor_property("primitive_data_index", primitive_index)
    return node


def _constant(material, x, y, value):
    return _node(material, unreal.MaterialExpressionConstant, x, y, r=value)


def _multiply(material, x, y, a, a_pin, b, b_pin):
    node = _node(material, unreal.MaterialExpressionMultiply, x, y)
    _connect(a, a_pin, node, "A")
    _connect(b, b_pin, node, "B")
    return node


def _add(material, x, y, a, a_pin, b, b_pin):
    node = _node(material, unreal.MaterialExpressionAdd, x, y)
    _connect(a, a_pin, node, "A")
    _connect(b, b_pin, node, "B")
    return node


def _finish(material):
    editing.recompile_material(material)
    return material


def build_surface_material(directory, name, kind):
    """The flat masters, each a colour, roughness, metallic and glow from its instance:
    "surface" (M_HQ17_Surface), "glass" (M_HQ17_Glass: translucent, lit per pixel, GlassOpacity),
    "operational" (M_HQ_Final_Operational: the glow times Operational, custom primitive data, 1 on and 0 off) and
    "ownership" (M_HQ_Final_Ownership: the colour is the owner's PlayerColor, custom primitive data)."""
    material = _material(directory, name)
    if kind == "ownership":
        color = _vector(material, -450, 0, "PlayerColor", (0.025, 0.095, 0.2, 1.0), vehicle_art.TEAM_COLOR_INDEX)
    else:
        color = _vector(material, -450, 0, "SurfaceColor", (0.5, 0.5, 0.5, 1.0))
    roughness = _scalar(material, -450, 160, "Roughness", 0.5)
    metallic = _scalar(material, -450, 320, "Metallic", 0.0)
    emission = _vector(material, -450, 480, "EmissionColor", (0.0, 0.0, 0.0, 1.0))
    _output(material, color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    _output(material, roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    _output(material, metallic, "", unreal.MaterialProperty.MP_METALLIC)
    if kind == "operational":
        operational = _scalar(material, -450, 640, "Operational", 1.0, vehicle_art.LAMP_ON_INDEX)
        glow = _multiply(material, -200, 480, emission, "RGB", operational, "")
        _output(material, glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        _output(material, emission, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if kind == "glass":
        opacity = _scalar(material, -450, 640, "GlassOpacity", 0.3)
        _output(material, opacity, "", unreal.MaterialProperty.MP_OPACITY)
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        material.set_editor_property("translucency_lighting_mode",
                                     unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        material.set_editor_property("screen_space_reflections", True)
    return _finish(material)


def build_coating_material(directory, name, textures):
    """M_HQ17_Coating: painted and coated surfaces, the instance's colour times the 2 m coating texture, its roughness
    moved by the coating's (COATING_ROUGHNESS_MEAN), the coating's normal. textures: {parameter: texture}."""
    material = _material(directory, name)
    color = _vector(material, -450, 0, "SurfaceColor", (0.5, 0.5, 0.5, 1.0))
    roughness = _scalar(material, -450, 160, "Roughness", 0.5)
    metallic = _scalar(material, -450, 320, "Metallic", 0.0)
    emission = _vector(material, -450, 480, "EmissionColor", (0.0, 0.0, 0.0, 1.0))
    samplers = {}
    for index, (parameter, sampler) in enumerate((("CoatingBaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                                                  ("CoatingRoughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS),
                                                  ("CoatingNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL))):
        samplers[parameter] = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -750, index * 300,
                                    parameter_name=parameter, texture=textures[parameter], sampler_type=sampler)
    base = _multiply(material, -200, 0, samplers["CoatingBaseColor"], "RGB", color, "RGB")
    offset = _node(material, unreal.MaterialExpressionSubtract, -450, 800, const_b=COATING_ROUGHNESS_MEAN)
    _connect(samplers["CoatingRoughness"], "R", offset, "A")
    moved = _add(material, -250, 160, roughness, "", offset, "")
    clamp = _node(material, unreal.MaterialExpressionClamp, -100, 160, min_default=COATING_ROUGHNESS_RANGE[0],
                  max_default=COATING_ROUGHNESS_RANGE[1])
    _connect(moved, "", clamp, "")
    _output(material, base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    _output(material, clamp, "", unreal.MaterialProperty.MP_ROUGHNESS)
    _output(material, metallic, "", unreal.MaterialProperty.MP_METALLIC)
    _output(material, samplers["CoatingNormal"], "RGB", unreal.MaterialProperty.MP_NORMAL)
    _output(material, emission, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finish(material)


def build_ground_material(directory, name, base_color, second, normal):
    """The textured ground masters without parameters: M_HQ20_FloorWear and M_HQ21_PavingWear (base colour and the
    second texture's red as roughness, normal False) and M_HQ21_Soil (base colour, the second texture as its normal,
    roughness SOIL_ROUGHNESS)."""
    material = _material(directory, name)
    color = _node(material, unreal.MaterialExpressionTextureSample, -450, 0, texture=base_color,
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _output(material, color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    if normal:
        normals = _node(material, unreal.MaterialExpressionTextureSample, -450, 300, texture=second,
                        sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        _output(material, normals, "RGB", unreal.MaterialProperty.MP_NORMAL)
        _output(material, _constant(material, -450, 600, SOIL_ROUGHNESS), "", unreal.MaterialProperty.MP_ROUGHNESS)
    else:
        roughness = _node(material, unreal.MaterialExpressionTextureSample, -450, 300, texture=second,
                          sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        _output(material, roughness, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    return _finish(material)


def build_flag_material(directory, name):
    """M_HQ21_FlagWind: the national flag's colours (SurfaceColor, never the owner's) waving by world position offset
    (FLAG_*): sin(p) + FLAG_RIPPLE[1] sin(FLAG_RIPPLE[0] p), p the wave phase along the world position plus time, times
    FLAG_AMPLITUDE, times UV0.U squared so the hoist stays put, along FLAG_DIRECTION. Two-sided, its normal the cross of
    the moved surface's screen derivatives in world space, turned to the side seen."""
    material = _material(directory, name)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("tangent_space_normal", False)
    color = _vector(material, -450, -300, "SurfaceColor", (0.4, 0.025, 0.035, 1.0))
    _output(material, color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    _output(material, _constant(material, -450, -150, FLAG_ROUGHNESS), "", unreal.MaterialProperty.MP_ROUGHNESS)

    position = _node(material, unreal.MaterialExpressionWorldPosition, -1400, 0)
    wave = _node(material, unreal.MaterialExpressionConstant3Vector, -1400, 120,
                 constant=unreal.LinearColor(*FLAG_WAVE, 1.0))
    along = _node(material, unreal.MaterialExpressionDotProduct, -1200, 0)
    _connect(position, "XYZ", along, "A")
    _connect(wave, "", along, "B")
    time = _node(material, unreal.MaterialExpressionTime, -1400, 240)
    moving = _multiply(material, -1200, 240, time, "", _constant(material, -1400, 340, FLAG_SPEED), "")
    phase = _add(material, -1000, 0, along, "", moving, "")
    first = _node(material, unreal.MaterialExpressionSine, -800, 0, period=6.283185)
    _connect(phase, "", first, "")
    faster = _multiply(material, -800, 160, phase, "", _constant(material, -1000, 200, FLAG_RIPPLE[0]), "")
    second = _node(material, unreal.MaterialExpressionSine, -600, 160, period=6.283185)
    _connect(faster, "", second, "")
    ripple = _multiply(material, -400, 160, second, "", _constant(material, -600, 280, FLAG_RIPPLE[1]), "")
    waves = _add(material, -400, 0, first, "", ripple, "")
    sized = _multiply(material, -200, 0, waves, "", _constant(material, -400, 300, FLAG_AMPLITUDE), "")
    uv = _node(material, unreal.MaterialExpressionTextureCoordinate, -800, 420, coordinate_index=0)
    u = _node(material, unreal.MaterialExpressionComponentMask, -600, 420, r=True, g=False, b=False, a=False)
    _connect(uv, "", u, "")
    pinned = _multiply(material, -400, 420, u, "", u, "")
    weighted = _multiply(material, 0, 0, sized, "", pinned, "")
    direction = _node(material, unreal.MaterialExpressionConstant3Vector, 0, 160,
                      constant=unreal.LinearColor(*FLAG_DIRECTION, 1.0))
    offset = _multiply(material, 200, 0, weighted, "", direction, "")
    _output(material, offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    moved = _node(material, unreal.MaterialExpressionWorldPosition, -800, 700)
    ddx = _node(material, unreal.MaterialExpressionDDX, -600, 650)
    ddy = _node(material, unreal.MaterialExpressionDDY, -600, 750)
    _connect(moved, "XYZ", ddx, "Value")
    _connect(moved, "XYZ", ddy, "Value")
    cross = _node(material, unreal.MaterialExpressionCrossProduct, -400, 700)
    _connect(ddx, "", cross, "A")
    _connect(ddy, "", cross, "B")
    normalized = _node(material, unreal.MaterialExpressionNormalize, -200, 700)
    _connect(cross, "", normalized, "VectorInput")
    side = _node(material, unreal.MaterialExpressionTwoSidedSign, -200, 820)
    normal = _multiply(material, 0, 700, normalized, "", side, "")
    _output(material, normal, "", unreal.MaterialProperty.MP_NORMAL)
    return _finish(material)


def build_material_instance(directory, name, parent, scalars, vectors):
    """An instance of parent with the delivery's values. The parent is set first and the instance updated after, so the
    saved instance keeps it (the delivery lost a parent set without an update: audit/ownership_persistence_repair)."""
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
        editing.clear_all_material_instance_parameters(instance)
    else:
        instance = asset_tools.create_asset(name, directory, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    for parameter, value in sorted(scalars.items()):
        editing.set_material_instance_scalar_parameter_value(instance, parameter, value)
    for parameter, value in sorted(vectors.items()):
        editing.set_material_instance_vector_parameter_value(instance, parameter, unreal.LinearColor(*value))
    editing.update_material_instance(instance)
    if instance.get_editor_property("parent") != parent:
        raise RuntimeError(f"{name} did not keep its parent {parent.get_name()}")
    return instance


def import_mesh(source, directory, name, slot_materials, bounds, expected_triangles, triangles):
    """Imports one mesh at its own origin (vehicle_art.import_part: normals and tangents as exported, no Nanite, no
    collision, slots bound by name), checks its size along X and its triangles against the delivery's, and gives it
    LODs (triangles: LOD1-3 targets, none for a small mesh)."""
    size = bounds[1][0] - bounds[0][0]
    mesh = vehicle_art.import_part(source, directory, name, slot_materials, size)
    imported = mesh.get_num_triangles(0)
    if imported < expected_triangles * 0.99:
        raise RuntimeError(f"{source} imported {imported} triangles instead of {expected_triangles}")
    if triangles:
        vehicle_art.build_lods(mesh, triangles)
    return mesh
