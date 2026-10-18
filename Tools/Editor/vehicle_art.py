"""Imports a vehicle built for the game in Blender (Tools/Art, e.g. m20_guardian_blender.py) from its folder under Art/:
one FBX per moving part at its own pivot (metres, +X forward, Z up, one UV channel, tangents), and per texture set the
maps T_<Set>_BC (sRGB, the team mask in alpha), T_<Set>_N (DirectX normals) and T_<Set>_ORM (occlusion, roughness,
metallic). Every set shares the opaque master material M_Vehicle through an instance; a lamp slot gets M_VehicleLamp,
lit by the mesh's vertex colour. Everything here builds in memory only; setup_testmap.py decides what needs rebuilding
and saves it.
"""

import math

import unreal

MATERIAL_DIR = "/Game/Materials"
VEHICLE_MATERIAL = "M_Vehicle"
LAMP_MATERIAL = "M_VehicleLamp"

# The team colour replaces the masked panels at this brightness of their own luminance: team colours are dark and
# saturated, the panels mid-grey.
TEAM_BRIGHTNESS = 2.0
# Where the team colour comes from: custom primitive data from this index on, RGBA (RTSPrimitiveData::TeamColor, #150), so
# every unit keeps the shared instances below instead of a dynamic instance per slot.
TEAM_COLOR_INDEX = 4
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
    panel's own luminance in the team "Color" by TeamStrength, the colour read from custom primitive data
    (TEAM_COLOR_INDEX)."""
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
    color.set_editor_property("use_custom_primitive_data", True)
    color.set_editor_property("primitive_data_index", TEAM_COLOR_INDEX)
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

    build_track_animation(material, samples["N"])
    editing.connect_material_property(samples["ORM"], "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    editing.connect_material_property(samples["ORM"], "G", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.connect_material_property(samples["ORM"], "B", unreal.MaterialProperty.MP_METALLIC)

    editing.recompile_material(material)
    return material


# The track's link shift (#151), from the channels m20_guardian_bake.add_track_animation_uvs writes: UV1, UV2 and UV3 the
# offsets in local cm (X, Z) to the vertex's place in the previous, next and next-but-one link, UV4.x the turn in radians
# to the next link. The phase through one link comes as custom primitive data from ARTSUnit::UpdateRunningGear: the
# current one, and last frame's unwrapped, for the motion vectors (RTSPrimitiveData::TrackPhase and TrackPreviousPhase).
TRACK_ANIMATION = "TrackAnimation"
TRACK_PHASE = ("TrackPhase", 0)
TRACK_PREVIOUS_PHASE = ("TrackPreviousPhase", 1)
# A Catmull-Rom curve through the four places, from this link's (0) at T = 0 to the next one's at T = 1.
TRACK_SHIFT_HLSL = """float2 O = 0.5 * ((Next - Prev) * T + (2.0 * Prev + 4.0 * Next - Next2) * T * T + (-Prev - 3.0 * Next + Next2) * T * T * T);
return float3(O.x, 0.0, O.y);"""
# The vertex's normal in local space turned by the share T of the turn to the next link, in X-Z from +X toward +Z.
TRACK_TURN_HLSL = """float A = T * Turn;
float C = cos(A);
float S = sin(A);
return float3(N.x * C - N.z * S, N.y, N.x * S + N.z * C);"""


def _connect(source, output, target, pin):
    if not editing.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f"Could not connect {source.get_name()}.{output!r} to {target.get_name()}.{pin!r}")


def _custom(material, x, y, code, inputs, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    custom = editing.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("code", code)
    custom.set_editor_property("output_type", output)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    return custom


def _scalar(material, x, y, name, default=0.0, primitive_index=None):
    scalar = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    scalar.set_editor_property("parameter_name", name)
    scalar.set_editor_property("default_value", default)
    if primitive_index is not None:
        scalar.set_editor_property("use_custom_primitive_data", True)
        scalar.set_editor_property("primitive_data_index", primitive_index)
    return scalar


def _uv(material, x, y, index):
    uv = editing.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, x, y)
    uv.set_editor_property("coordinate_index", index)
    return uv


def _transform(material, x, y, source, destination):
    transform = editing.create_material_expression(material, unreal.MaterialExpressionTransform, x, y)
    transform.set_editor_property("transform_source_type", source)
    transform.set_editor_property("transform_type", destination)
    return transform


def build_track_animation(material, normal_sample):
    """The TrackAnimation static switch (off by default, on in a track's instance): World Position Offset moves every
    vertex along TRACK_SHIFT_HLSL by the phase, last frame's phase for the previous frame's place, and the normal map's
    normal turns with the link (TRACK_TURN_HLSL), so it meets the next link's at the wrap. Off, the material is the
    plain one: no offset, the normal map as it is."""
    phase = _scalar(material, -2200, 900, TRACK_PHASE[0], primitive_index=TRACK_PHASE[1])
    previous = _scalar(material, -2200, 1050, TRACK_PREVIOUS_PHASE[0], primitive_index=TRACK_PREVIOUS_PHASE[1])
    offsets = [_uv(material, -2200, 1200 + 120 * i, i + 1) for i in range(3)]
    shifts = []
    for row, t in enumerate((phase, previous)):
        shift = _custom(material, -1900, 900 + 300 * row, TRACK_SHIFT_HLSL, ("Prev", "Next", "Next2", "T"))
        for offset, pin in zip(offsets, ("Prev", "Next", "Next2")):
            _connect(offset, "", shift, pin)
        _connect(t, "", shift, "T")
        world = _transform(material, -1600, 900 + 300 * row, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                           unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        _connect(shift, "", world, "")
        shifts.append(world)
    frames = editing.create_material_expression(material, unreal.MaterialExpressionPreviousFrameSwitch, -1300, 1000)
    _connect(shifts[0], "", frames, "Current Frame")
    _connect(shifts[1], "", frames, "Previous Frame")
    none = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -1300, 1200)
    offset_switch = editing.create_material_expression(material, unreal.MaterialExpressionStaticSwitchParameter, -1000, 1000)
    offset_switch.set_editor_property("parameter_name", TRACK_ANIMATION)
    offset_switch.set_editor_property("default_value", False)
    _connect(frames, "", offset_switch, "True")
    _connect(none, "", offset_switch, "False")
    editing.connect_material_property(offset_switch, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    to_local = _transform(material, -1100, 500, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT,
                          unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    _connect(normal_sample, "RGB", to_local, "")
    turn_uv = _uv(material, -1100, 650, 4)
    turn = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -900, 650)
    turn.set_editor_property("r", True)
    turn.set_editor_property("g", False)
    _connect(turn_uv, "", turn, "")
    turned = _custom(material, -800, 500, TRACK_TURN_HLSL, ("N", "T", "Turn"))
    _connect(to_local, "", turned, "N")
    _connect(phase, "", turned, "T")
    _connect(turn, "", turned, "Turn")
    to_tangent = _transform(material, -500, 500, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                            unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    _connect(turned, "", to_tangent, "")
    normal_switch = editing.create_material_expression(material, unreal.MaterialExpressionStaticSwitchParameter, -250, 400)
    normal_switch.set_editor_property("parameter_name", TRACK_ANIMATION)
    normal_switch.set_editor_property("default_value", False)
    _connect(to_tangent, "", normal_switch, "True")
    _connect(normal_sample, "RGB", normal_switch, "False")
    editing.connect_material_property(normal_switch, "", unreal.MaterialProperty.MP_NORMAL)


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


def build_material_instance(directory, name, parent, textures, track_animation=False):
    """An instance of M_Vehicle reading one texture set, textures: {kind: texture}; track_animation turns the
    TrackAnimation switch on (build_track_animation)."""
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        instance = asset_tools.create_asset(name, directory, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    for kind, parameter in (("BC", "BaseColor"), ("N", "Normal"), ("ORM", "ORM")):
        editing.set_material_instance_texture_parameter_value(instance, parameter, textures[kind])
    if track_animation:
        editing.set_material_instance_static_switch_parameter_value(instance, TRACK_ANIMATION, True)
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


# The LODs (#149). Every part of a vehicle changes LOD at the same distances from the camera: LOD_DISTANCES in cm, for
# LOD1, LOD2 and LOD3. A part's screen size for a distance is the engine's (ComputeBoundsScreenSize) for its own bounds
# radius there, so a small part has a small threshold and switches with the hull. The projection is the RTS camera's:
# 60 degrees across at 16:9 with the vertical field of view kept, so every aspect ratio and resolution gives the same LOD.
LOD_DISTANCES = (3500.0, 11000.0, 26000.0)
CAMERA_FOV = 60.0
CAMERA_ASPECT = 16.0 / 9.0
SCREEN_MULTIPLE = 0.5 / (math.tan(math.radians(CAMERA_FOV) / 2.0) / CAMERA_ASPECT)


def _mesh_editing():
    """The StaticMeshEditorSubsystem, after loading its module, which a commandlet leaves out."""
    if not unreal.RTSEditorScripting.load_editor_module("StaticMeshEditor"):
        raise RuntimeError("Could not load the StaticMeshEditor module")
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem is None:
        raise RuntimeError("The StaticMeshEditorSubsystem is not available")
    return subsystem


def lod_radius(mesh, instanced):
    """The radius the renderer measures a part's LOD by: its bounds sphere, or for a part drawn by an instanced
    component (wheels), the box's half diagonal, which GPU LOD selection uses per instance."""
    bounds = mesh.get_bounds()
    return bounds.box_extent.length() if instanced else bounds.sphere_radius


def lod_screen_sizes(radius):
    """The screen sizes for LOD0 (1, the engine ignores it) and each of LOD_DISTANCES for a part of this radius."""
    return [1.0] + [2.0 * SCREEN_MULTIPLE * radius / distance for distance in LOD_DISTANCES]


def build_lods(mesh, triangles, instanced=False, custom=None, custom_slot=None):
    """Gives a part LOD1 to LOD3 after LOD0, which stays as imported. triangles: each LOD's triangle target, reduced
    from LOD0 by the engine's quadric reduction with the imported normals kept; custom: {LOD index: FBX} imported in its
    place instead (the tracks' belts, m20_guardian_blender.py --lods), whose sections go to custom_slot, (slot name,
    material), a slot of their own. Then the screen sizes of lod_screen_sizes for the radius of LOD0's bounds, which the
    mesh keeps."""
    mesh_editing = _mesh_editing()
    base = mesh.get_num_triangles(0)
    screen_sizes = lod_screen_sizes(lod_radius(mesh, instanced))
    options = unreal.StaticMeshReductionOptions()
    options.set_editor_property("auto_compute_lod_screen_size", False)
    settings = []
    for target, screen_size in zip((base,) + tuple(triangles), screen_sizes):
        setting = unreal.StaticMeshReductionSettings()
        setting.set_editor_property("percent_triangles", min(1.0, target / base))
        setting.set_editor_property("screen_size", screen_size)
        settings.append(setting)
    options.set_editor_property("reduction_settings", settings)
    if mesh_editing.set_lods(mesh, options) != len(settings):
        raise RuntimeError(f"{mesh.get_name()}: could not make its LODs")
    for index, source in sorted((custom or {}).items()):
        if mesh_editing.import_lod(mesh, index, source) != index:
            raise RuntimeError(f"{mesh.get_name()}: could not import LOD{index} from {source}")
    if custom:
        name, material = custom_slot
        names = [str(slot.material_slot_name) for slot in mesh.static_materials]
        if name not in names:
            mesh.add_material(material)
            materials = mesh.static_materials
            materials[-1].material_slot_name = name
            mesh.static_materials = materials
            names.append(name)
        slot = names.index(name)
        mesh.set_material(slot, material)
        for index in sorted(custom):
            for section in range(mesh.get_num_sections(index)):
                mesh_editing.set_lod_material_slot(mesh, slot, index, section)
    if not mesh_editing.set_lod_screen_sizes(mesh, screen_sizes):
        raise RuntimeError(f"{mesh.get_name()}: could not set its LOD screen sizes")
    unreal.log(f"vehicle_art: {mesh.get_name()} LODs " +
               ", ".join(f"{mesh.get_num_triangles(i)}" for i in range(mesh.get_num_lods())) +
               f" triangles, screen sizes {[round(s, 4) for s in mesh_editing.get_lod_screen_sizes(mesh)]}")
    return mesh
