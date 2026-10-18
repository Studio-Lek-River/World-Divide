"""Imports the placeholder models in Art/Source/Placeholder (CC0, credited in CREDITS.md there) as static meshes, each
material slot getting an instance of a team-tint material that keeps the model's own texture or colour. Everything here
builds in memory only; setup_testmap.py decides what needs rebuilding and saves it.
"""

import json
import os
import struct

import unreal

MESH_DIR = "/Game/Art/Placeholder/Meshes"
MATERIAL_DIR = "/Game/Art/Placeholder/Materials"
TEXTURE_DIR = "/Game/Art/Placeholder/Textures"
WHITE_TEXTURE = "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"
# The team colour: custom primitive data from this index on, RGBA (RTSPrimitiveData::TeamColor, #150).
TEAM_COLOR_INDEX = 4

# Imported models whose footprint centre is further than this from the origin are re-imported shifted onto it.
CENTRE_TOLERANCE = 0.5

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editing = unreal.MaterialEditingLibrary


def read_gltf_materials(source):
    """The materials of a .glb as {name: (linear base colour factor, absolute path of its base colour texture or None)}.
    Only external textures are supported, which is how the Kenney kits ship them."""
    with open(source, "rb") as file:
        data = file.read()
    gltf = json.loads(data[20:20 + struct.unpack_from("<I", data, 12)[0]])
    materials = {}
    for material in gltf.get("materials", []):
        pbr = material.get("pbrMetallicRoughness", {})
        texture = None
        if "baseColorTexture" in pbr:
            image = gltf["images"][gltf["textures"][pbr["baseColorTexture"]["index"]]["source"]]
            if "uri" not in image:
                raise RuntimeError(f"{source}: embedded textures are not supported, export the texture next to the model")
            texture = os.path.normpath(os.path.join(os.path.dirname(source), image["uri"]))
        materials[material.get("name", "")] = (tuple(pbr.get("baseColorFactor", (1.0, 1.0, 1.0, 1.0))), texture)
    return materials


def build_team_material(material):
    """Lit material: BaseTexture x BaseTint, blended by TintStrength towards its own brightness in the team "Color", read
    from custom primitive data (TEAM_COLOR_INDEX), so every unit and building of a kind keeps its shared instances."""
    editing.delete_all_material_expressions(material)

    texture = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 0)
    texture.set_editor_property("parameter_name", "BaseTexture")
    texture.set_editor_property("texture", unreal.load_asset(WHITE_TEXTURE))
    tint = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1200, 300)
    tint.set_editor_property("parameter_name", "BaseTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    base = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -900, 100)
    editing.connect_material_expressions(texture, "RGB", base, "A")
    editing.connect_material_expressions(tint, "", base, "B")

    weights = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -900, 300)
    weights.set_editor_property("constant", unreal.LinearColor(0.299, 0.587, 0.114, 1.0))
    luminance = editing.create_material_expression(material, unreal.MaterialExpressionDotProduct, -700, 250)
    editing.connect_material_expressions(base, "", luminance, "A")
    editing.connect_material_expressions(weights, "", luminance, "B")

    color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 450)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    color.set_editor_property("use_custom_primitive_data", True)
    color.set_editor_property("primitive_data_index", TEAM_COLOR_INDEX)
    team = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -500, 300)
    editing.connect_material_expressions(luminance, "", team, "A")
    editing.connect_material_expressions(color, "", team, "B")
    # Team colours are dark and saturated; lift them so tinted parts stay about as bright as the original.
    brightness = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, 500)
    brightness.set_editor_property("parameter_name", "TintBrightness")
    brightness.set_editor_property("default_value", 1.6)
    tinted = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 350)
    editing.connect_material_expressions(team, "", tinted, "A")
    editing.connect_material_expressions(brightness, "", tinted, "B")

    strength = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -300, 550)
    strength.set_editor_property("parameter_name", "TintStrength")
    strength.set_editor_property("default_value", 0.5)
    blend = editing.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -100, 200)
    editing.connect_material_expressions(base, "", blend, "A")
    editing.connect_material_expressions(tinted, "", blend, "B")
    editing.connect_material_expressions(strength, "", blend, "Alpha")
    editing.connect_material_property(blend, "", unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -100, 450)
    roughness.set_editor_property("r", 0.8)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    editing.recompile_material(material)
    return material


def import_texture(source, name):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = TEXTURE_DIR
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{TEXTURE_DIR}/{name}")
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Could not import {source}")
    # The Kenney textures are small colour palettes: sample them without blending neighbouring swatches.
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    return texture


def build_material_instance(name, parent, texture, tint, strength):
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        instance = asset_tools.create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    editing.set_material_instance_texture_parameter_value(instance, "BaseTexture", texture or unreal.load_asset(WHITE_TEXTURE))
    editing.set_material_instance_vector_parameter_value(instance, "BaseTint", unreal.LinearColor(*tint))
    editing.set_material_instance_scalar_parameter_value(instance, "TintStrength", strength)
    editing.update_material_instance(instance)
    return instance


def import_static_mesh(source, name, yaw, offset):
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("import_offset_rotation", unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    pipeline.set_editor_property("import_offset_translation", offset)
    common = pipeline.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    meshes = pipeline.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    meshes.set_editor_property("import_skeletal_meshes", False)
    # The game gives units and buildings their own collision; the mesh components never collide.
    meshes.set_editor_property("collision", False)
    # A few hundred triangles each: Nanite gains nothing, and the translucent placement ghost can't render on it.
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
    manager.import_asset(MESH_DIR, unreal.InterchangeManager.create_source_data(source), parameters)
    mesh = unreal.load_asset(f"{MESH_DIR}/{name}")
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Could not import {source} as a static mesh")
    # A re-import keeps the existing mesh's Nanite setting instead of the pipeline's, so turn it off here too.
    nanite = mesh.get_editor_property("nanite_settings")
    if nanite.enabled:
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)
    return mesh


def import_mesh(source, name, yaw, slot_materials):
    """Imports source as one static mesh turned by yaw, with its footprint centred on the origin and its base where the
    model has it, and gives each material slot the instance slot_materials names for it."""
    mesh = import_static_mesh(source, name, yaw, unreal.Vector(0.0, 0.0, 0.0))
    box = mesh.get_bounding_box()
    centre = unreal.Vector((box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0, 0.0)
    if abs(centre.x) > CENTRE_TOLERANCE or abs(centre.y) > CENTRE_TOLERANCE:
        mesh = import_static_mesh(source, name, yaw, unreal.Vector(-centre.x, -centre.y, 0.0))

    for index, slot in enumerate(mesh.static_materials):
        slot_name = str(slot.material_slot_name)
        if slot_name not in slot_materials:
            raise RuntimeError(f"{source}: no material instance for slot {slot_name}")
        mesh.set_material(index, slot_materials[slot_name])
    return mesh
