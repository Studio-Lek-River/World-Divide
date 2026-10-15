"""Imports a vehicle built for the game in Blender (Tools/Art, e.g. m20_guardian_blender.py) from its folder under Art/:
one FBX per moving part at its own pivot (metres, +X forward, Z up, one UV channel, tangents), and per texture set the
maps T_<Set>_BC (sRGB, the team mask in alpha), T_<Set>_N (DirectX normals) and T_<Set>_ORM (occlusion, roughness,
metallic). Every set shares the opaque master material M_Vehicle through an instance; a lamp slot gets M_VehicleLamp,
lit by the mesh's vertex colour. Everything here builds in memory only; setup_testmap.py decides what needs rebuilding
and saves it.
"""

import unreal

MATERIAL_DIR = "/Game/Materials"
VEHICLE_MATERIAL = "M_Vehicle"
LAMP_MATERIAL = "M_VehicleLamp"

# The team colour replaces the masked panels at this brightness of their own luminance: team colours are dark and
# saturated, the panels mid-grey.
TEAM_BRIGHTNESS = 2.0
LAMP_STRENGTH = 4.0

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editing = unreal.MaterialEditingLibrary

# Texture kind -> (sRGB, compression, LOD group).
TEXTURE_KINDS = {
    "BC": (True, unreal.TextureCompressionSettings.TC_DEFAULT, unreal.TextureGroup.TEXTUREGROUP_WORLD),
    "N": (False, unreal.TextureCompressionSettings.TC_NORMALMAP, unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP),
    "ORM": (False, unreal.TextureCompressionSettings.TC_MASKS, unreal.TextureGroup.TEXTUREGROUP_WORLD),
}

# The material sampler each texture kind is read with.
SAMPLERS = {
    "BC": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
    "N": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
    "ORM": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
}


def import_texture(source, directory, name, kind):
    """Imports one map of a texture set with the settings its kind needs. BC keeps its alpha (BC3), the normal map is
    already DirectX, so its green channel stays as it is."""
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
    srgb, compression, group = TEXTURE_KINDS[kind]
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("lod_group", group)
    texture.set_editor_property("flip_green_channel", False)
    return texture


def _material(name):
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        return assets.load_asset(path)
    return asset_tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())


def build_vehicle_material(defaults):
    """Opaque, default lit, three samples: BaseColor, Normal and ORM texture parameters (defaults: {kind: texture} of one
    set, which the sampler types are checked against). The BC alpha masks the team panels: there BaseColor becomes the
    panel's own luminance in the team "Color" (ARTSUnit::TeamColorParameter) by TeamStrength."""
    material = _material(VEHICLE_MATERIAL)
    editing.delete_all_material_expressions(material)

    samples = {}
    for row, (kind, parameter) in enumerate((("BC", "BaseColor"), ("N", "Normal"), ("ORM", "ORM"))):
        sample = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D,
                                                    -1400, row * 300)
        sample.set_editor_property("parameter_name", parameter)
        sample.set_editor_property("sampler_type", SAMPLERS[kind])
        sample.set_editor_property("texture", defaults[kind])
        samples[kind] = sample

    weights = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -1100, -200)
    weights.set_editor_property("constant", unreal.LinearColor(0.299, 0.587, 0.114, 1.0))
    luminance = editing.create_material_expression(material, unreal.MaterialExpressionDotProduct, -900, -150)
    editing.connect_material_expressions(samples["BC"], "RGB", luminance, "A")
    editing.connect_material_expressions(weights, "", luminance, "B")
    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -400)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.05, 0.25, 0.9, 1.0))
    team = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -700, -250)
    editing.connect_material_expressions(luminance, "", team, "A")
    editing.connect_material_expressions(color, "", team, "B")
    brightness = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, -450)
    brightness.set_editor_property("parameter_name", "TeamBrightness")
    brightness.set_editor_property("default_value", TEAM_BRIGHTNESS)
    tinted = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -500, -300)
    editing.connect_material_expressions(team, "", tinted, "A")
    editing.connect_material_expressions(brightness, "", tinted, "B")

    strength = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 0)
    strength.set_editor_property("parameter_name", "TeamStrength")
    strength.set_editor_property("default_value", 1.0)
    mask = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -500, 0)
    editing.connect_material_expressions(samples["BC"], "A", mask, "A")
    editing.connect_material_expressions(strength, "", mask, "B")
    blend = editing.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -300, -100)
    editing.connect_material_expressions(samples["BC"], "RGB", blend, "A")
    editing.connect_material_expressions(tinted, "", blend, "B")
    editing.connect_material_expressions(mask, "", blend, "Alpha")
    editing.connect_material_property(blend, "", unreal.MaterialProperty.MP_BASE_COLOR)

    editing.connect_material_property(samples["N"], "RGB", unreal.MaterialProperty.MP_NORMAL)
    editing.connect_material_property(samples["ORM"], "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    editing.connect_material_property(samples["ORM"], "G", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.connect_material_property(samples["ORM"], "B", unreal.MaterialProperty.MP_METALLIC)

    editing.recompile_material(material)
    return material


def build_lamp_material():
    """The lamps: base colour and emissive both the mesh's vertex colour, the emissive LampStrength times brighter."""
    material = _material(LAMP_MATERIAL)
    editing.delete_all_material_expressions(material)
    vertex_color = editing.create_material_expression(material, unreal.MaterialExpressionVertexColor, -600, 0)
    strength = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 200)
    strength.set_editor_property("parameter_name", "LampStrength")
    strength.set_editor_property("default_value", LAMP_STRENGTH)
    emissive = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 100)
    editing.connect_material_expressions(vertex_color, "", emissive, "A")
    editing.connect_material_expressions(strength, "", emissive, "B")
    editing.connect_material_property(vertex_color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 300)
    roughness.set_editor_property("r", 0.1)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(material)
    return material


def build_material_instance(directory, name, parent, textures):
    """An instance of M_Vehicle reading one texture set, textures: {kind: texture}."""
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        instance = asset_tools.create_asset(name, directory, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    for kind, parameter in (("BC", "BaseColor"), ("N", "Normal"), ("ORM", "ORM")):
        editing.set_material_instance_texture_parameter_value(instance, parameter, textures[kind])
    editing.update_material_instance(instance)
    return instance


def import_part(source, directory, name, slot_materials, expected_size):
    """Imports one part's FBX as a static mesh at its own pivot, keeping the normals and tangents the bake was made
    against, without Nanite (the parts move), collision (the unit has its own) or LODs, and gives each material slot the
    material slot_materials names for it. expected_size is the part's length along X in cm, checked against the import
    so a unit mix-up shows at once."""
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    common = pipeline.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    common.set_editor_property("recompute_normals", False)
    common.set_editor_property("recompute_tangents", False)
    meshes = pipeline.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    meshes.set_editor_property("import_skeletal_meshes", False)
    meshes.set_editor_property("collision", False)
    meshes.set_editor_property("build_nanite", False)
    materials = pipeline.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)

    parameters = unreal.ImportAssetParameters()
    parameters.is_automated = True
    parameters.replace_existing = True
    parameters.destination_name = name
    parameters.override_pipelines.append(unreal.SoftObjectPath(pipeline.get_path_name()))
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    manager.import_asset(directory, unreal.InterchangeManager.create_source_data(source), parameters)
    mesh = unreal.load_asset(f"{directory}/{name}")
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Could not import {source} as a static mesh")
    nanite = mesh.get_editor_property("nanite_settings")
    if nanite.enabled:
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)

    box = mesh.get_bounding_box()
    size = box.max.x - box.min.x
    if abs(size - expected_size) > max(2.0, expected_size * 0.02):
        raise RuntimeError(f"{source} imported {size:.1f} cm long instead of {expected_size:.1f}: check the FBX units")
    unreal.log(f"vehicle_art: {name} bounds ({box.min.x:.1f}, {box.min.y:.1f}, {box.min.z:.1f}) .. "
               f"({box.max.x:.1f}, {box.max.y:.1f}, {box.max.z:.1f})")

    for index, slot in enumerate(mesh.static_materials):
        slot_name = str(slot.material_slot_name)
        if slot_name not in slot_materials:
            raise RuntimeError(f"{source}: no material for slot {slot_name}")
        mesh.set_material(index, slot_materials[slot_name])
    return mesh
