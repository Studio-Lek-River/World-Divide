"""Imports a building modelled in several meshes from its folder under Art/ (the USA HQ, #191): the layout its
usa_hq.json gives (Tools/Art/usa_hq_recipe.py), one FBX per mesh at real size (cm, +X the front, Z up), the textures,
the master materials rebuilt here from the delivery's graphs (recipe/material_graphs.json) and their instances with
the delivery's values. Every mesh keeps the normals and UVs it was exported with, has no Nanite and no collision (the
building's data carries simple boxes instead) and gets the building's two LODs, switching as one (import_mesh, #358).
Everything here builds in memory only; setup_testmap.py decides what needs rebuilding and saves it.

The masters read two custom primitive data channels the building writes to every one of its meshes: the owner's colour
at vehicle_art.TEAM_COLOR_INDEX (M_Bld_Ownership's PlayerColor) and whether it is operational at
vehicle_art.LAMP_ON_INDEX (M_Bld_Operational's Operational, multiplying its glow). M_Bld_Operational also reads
its lighting group and the dark (LIGHT_GROUP_INDICES, LIGHT_NIGHT_INDEX, #195). The generic masters (M_Bld_*) and the kit's
lens (M_Kit_Lens) are every building's, in /Game/Art/Shared (#388).
"""

import json
import re
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

# The shared waving flag (M_Flag_Cloth, #351), its parameters' defaults, which a flag's instance may override. Lengths in
# cm, rates in cycles a second, wave numbers in cycles over the flag's length (U 0 to 1). Waves travel from the hoist to
# the fly, growing as U^1.5 to FlagWaveAmplitude at the fly, plus a second, faster harmonic; the fly flutters, U^3, faster
# and smaller; the fly sags under gravity, U^2, more in a lull; gusts swell the amplitude by FlagGustStrength and the
# waves' speed by FlagGustSpeed over FlagGustPeriod seconds (two incommensurate sines); every flag picks its own phases
# from where it stands. The wind fades out from FlagFadeStart to FlagFadeEnd cm from the camera, inside its part's world
# position offset disable distance (usa_hq_recipe.FLAG_WPO_DISABLE_DISTANCE).
FLAG_PARAMETERS = {
    "FlagWaveAmplitude": 24.0,
    "FlagWaveNumber": 1.35,
    "FlagWaveSpeed": 0.85,
    "FlagHarmonic": 0.3,
    "FlagFlutterAmplitude": 5.0,
    "FlagFlutterNumber": 4.5,
    "FlagFlutterSpeed": 2.6,
    "FlagSag": 22.0,
    "FlagGustStrength": 0.45,
    "FlagGustSpeed": 0.3,
    "FlagGustPeriod": 7.0,
    "FlagFadeStart": 9000.0,
    "FlagFadeEnd": 12000.0,
    "Roughness": 0.85,
}
FLAG_WPO_HLSL = """float3 S = frac(Seed * float3(0.1031, 0.1030, 0.0973));
S += dot(S, S.yzx + 33.33);
S = frac((S.xxy + S.yzz) * S.zyx);
float Tau = 6.2831853;
float Gust = 0.5 + 0.5 * (0.6 * sin(Tau * (Time / GustPeriod + S.x)) + 0.4 * sin(Tau * (Time / (GustPeriod * 1.618) + S.y)));
float Strength = lerp(1.0 - GustStrength, 1.0, Gust);
float Travel = WaveSpeed * (Time - GustSpeed * GustPeriod / Tau * cos(Tau * Time / GustPeriod + Tau * S.x));
float Phase = Tau * (UV.x * WaveNumber - Travel + S.z);
float Wave = sin(Phase) + Harmonic * sin(1.7 * Phase + 2.0 * UV.y + Tau * S.y);
float Flutter = FlutterAmplitude * pow(UV.x, 3.0) * sin(Tau * (UV.x * FlutterNumber - Time * FlutterSpeed + 1.3 * UV.y + S.x));
float Across = Strength * (WaveAmplitude * pow(UV.x, 1.5) * Wave + Flutter);
float Down = Sag * UV.x * UV.x * (1.4 - 0.8 * Strength);
float Fade = 1.0 - saturate((Distance - FadeStart) / max(FadeEnd - FadeStart, 1.0));
return (normalize(Normal) * Across - float3(0.0, 0.0, Down)) * Fade;"""
FLAG_WPO_INPUTS = {"WaveAmplitude": "FlagWaveAmplitude", "WaveNumber": "FlagWaveNumber", "WaveSpeed": "FlagWaveSpeed",
                   "Harmonic": "FlagHarmonic", "FlutterAmplitude": "FlagFlutterAmplitude",
                   "FlutterNumber": "FlagFlutterNumber", "FlutterSpeed": "FlagFlutterSpeed", "Sag": "FlagSag",
                   "GustStrength": "FlagGustStrength", "GustSpeed": "FlagGustSpeed", "GustPeriod": "FlagGustPeriod",
                   "FadeStart": "FlagFadeStart", "FadeEnd": "FlagFadeEnd"}

# The ground masters' constant roughness.
SOIL_ROUGHNESS = 0.94


# M_Bld_Operational's lighting groups (RTSPrimitiveData::LightWindows.., #195): an instance whose Group<name> weight is
# 1 glows only while the building's custom primitive data at that index is 1 (ARTSBuilding::SetLightGroupOn, 1 unless
# switched off); one of no group always glows. In the dark (LIGHT_NIGHT_INDEX 1, URTSBuildingLightSubsystem) the glow rises
# to the instance's NightGain times its day strength.
LIGHT_GROUP_INDICES = {"Windows": 9, "Fixtures": 10, "Accents": 11}
LIGHT_NIGHT_INDEX = 12

# M_Kit_Lens's per-instance custom data (RTSKitLensData, #387, docs/art/BUILDING_KIT.md "Per-instance and LOD"), written
# with SetCustomData: on/level, group mask (1 = gated by the building's Fixtures group), fault phase (0 = no fault), flash
# seed (0 = steady) and the colour, RGB. The level carries the building's state (URTSBuildingStateComponent, #391), power
# included, so a battery lens stays lit on low power: the lens no longer reads Operational. RTSKit::IsLensLit repeats the
# fault and flash sums for the real lights; change both together.
LENS_LEVEL, LENS_GROUP, LENS_FAULT, LENS_FLASH, LENS_COLOR = 0, 1, 2, 3, 4
# A lens with Blink 1 (the mast LEDs, #393) also blinks on its own, LENS_BLINK, its rate jittered and its phase from
# where it stands; with Alternate 1 its lens at U >= 0.5 (13's second dome) blinks half a cycle later. Only lenses
# without a real light blink, so RTSKit::IsLensLit has no blink to repeat.
LENS_HLSL = """
float gate = lerp(1.0, Fixtures, saturate(Group));
float fault = Fault > 0.0 ? step(0.35, frac(Time * 2.3 + Fault + 0.5 * sin(Time * 17.0 + Fault * 40.0))) : 1.0;
float flash = Flash > 0.0 ? step(0.5, frac(Time + Flash)) : 1.0;
float blink = 1.0;
if (Blink > 0.0)
{
    float3 S = frac(Seed * float3(0.1031, 0.1030, 0.0973));
    S += dot(S, S.yzx + 33.33);
    S = frac((S.xxy + S.yzz) * S.zyx);
    float Cycle = frac(Time * BlinkRate * (1.0 + BlinkJitter * (2.0 * S.x - 1.0)) + S.y + 0.5 * Alternate * step(0.5, U));
    blink = saturate((BlinkDuty - Cycle) / max(BlinkFade, 0.001));
}
return Color * Level * gate * fault * flash * blink * lerp(DayGain, 1.0, Night) * Intensity;
"""
# The lens's colour from its LightProfile's red P (#393): EmissionTint, turning to HotCoreTint where the profile is hottest,
# by HotCore (0, the default, keeps EmissionTint throughout).
LENS_SHAPE_HLSL = """
float hot = saturate((P - 0.55) / 0.35) * HotCore;
return P * lerp(Tint, HotTint, hot);
"""
# The lens's blink (the obstruction beacon's, #351): BlinkRate a second, on for BlinkDuty of it and fading over its last
# BlinkFade, every lens up to BlinkJitter faster or slower.
LENS_BLINK = {"BlinkRate": 1.0, "BlinkDuty": 0.18, "BlinkFade": 0.06, "BlinkJitter": 0.08}
# Sweep (#393, 08's rotating beacon): a static switch, off by default, that turns the masked part of the piece about its
# local Z axis at SweepRate turns a second, the turning light's 1.5 (ARTSBuilding::UpdateKitLights), by world position
# offset. P is the local position (cm), Mask 1 where it turns.
SWEEP = "Sweep"
SWEEP_HLSL = """
float a = 6.2831853 * frac(Time * Rate) * Mask;
float2 r = float2(P.x * cos(a) - P.y * sin(a), P.x * sin(a) + P.y * cos(a));
return float3(r - P.xy, 0.0);
"""
# Glass (#393, 08's dome): a static switch, off by default, that lets a lens's thin shell (its LightProfile's green 1) show
# what is behind it: dithered opacity 1 - Transmission x (1 - Fresnel), its base colour times GlassDarken. Its instance
# draws masked and two-sided (build_kit_finish_instance).
GLASS = "Glass"
DITHER_FUNCTION = "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"
# The engine's flat normal: a lens without a baked finish normal draws as if it had none.
FLAT_NORMAL = "/Engine/EngineMaterials/FlatNormal"
# A game mesh baked from its master (#373, docs/art/MODEL_PERFORMANCE.md rule 7): the building masters' static switch
# SHELL_BAKE, off by default, turns on the master's fine detail baked on UV1, the normal map BakedNormal (on the tiling
# normal where the master has one) and the occlusion BakedAO (its R). An instance sets it with its maps.
SHELL_BAKE = "ShellBake"


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


def _shell_bake(material, white_masks, normal=None, normal_pin=""):
    """The normal and occlusion of SHELL_BAKE: on, normal (or flat) with BakedNormal on UV1 laid over it,
    normalize(normal + baked - (0, 0, 1)), and BakedAO's R; off, normal (or flat) and no occlusion. Returns the two
    switches, for MP_NORMAL and MP_AMBIENT_OCCLUSION."""
    uv1 = _node(material, unreal.MaterialExpressionTextureCoordinate, -1900, 1900, coordinate_index=1)
    baked_normal = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -1600, 1900,
                         parameter_name="BakedNormal", texture=unreal.load_asset(FLAT_NORMAL),
                         sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    baked_ao = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -1600, 2200, parameter_name="BakedAO",
                     texture=white_masks, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    _connect(uv1, "", baked_normal, "UVs")
    _connect(uv1, "", baked_ao, "UVs")
    up = _node(material, unreal.MaterialExpressionConstant3Vector, -1600, 1750,
               constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    normal_switch = _node(material, unreal.MaterialExpressionStaticSwitchParameter, -800, 1850, parameter_name=SHELL_BAKE,
                          default_value=False)
    if normal is None:
        _connect(baked_normal, "RGB", normal_switch, "True")
        _connect(up, "", normal_switch, "False")
    else:
        less = _node(material, unreal.MaterialExpressionSubtract, -1150, 1850)
        _connect(_add(material, -1300, 1850, normal, normal_pin, baked_normal, "RGB"), "", less, "A")
        _connect(up, "", less, "B")
        unit = _node(material, unreal.MaterialExpressionNormalize, -1000, 1850)
        _connect(less, "", unit, "VectorInput")
        _connect(unit, "", normal_switch, "True")
        _connect(normal, normal_pin, normal_switch, "False")
    ao_switch = _node(material, unreal.MaterialExpressionStaticSwitchParameter, -800, 2150, parameter_name=SHELL_BAKE,
                      default_value=False)
    _connect(baked_ao, "R", ao_switch, "True")
    _connect(_constant(material, -1000, 2300, 1.0), "", ao_switch, "False")
    return normal_switch, ao_switch


def _finish(material):
    editing.recompile_material(material)
    return material


def build_surface_material(directory, name, kind, white_masks):
    """The flat masters, each a colour, roughness, metallic and glow from its instance:
    "surface" (M_Bld_Surface), "glass" (M_Bld_Glass: translucent, lit per pixel, GlassOpacity, at night
    NightGlow, the rooms lit behind it),
    "operational" (M_Bld_Operational: the glow times Operational, custom primitive data, 1 on and 0 off, its lighting
    group's switch and, in the dark, NightGain: LIGHT_GROUP_INDICES) and
    "ownership" (M_Bld_Ownership: the colour is the owner's PlayerColor, custom primitive data).
    Each has SHELL_BAKE, a baked mesh's normal and occlusion (white_masks: BakedAO's white default)."""
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
    normal, occlusion = _shell_bake(material, white_masks)
    _output(material, normal, "", unreal.MaterialProperty.MP_NORMAL)
    _output(material, occlusion, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    if kind == "operational":
        operational = _scalar(material, -450, 640, "Operational", 1.0, vehicle_art.LAMP_ON_INDEX)
        # The group's switch: 1 less each group's weight times how far it is off, so an instance of no group stays at 1.
        gate = _constant(material, -900, 800, 1.0)
        for row, (group, index) in enumerate(sorted(LIGHT_GROUP_INDICES.items(), key=lambda g: g[1])):
            weight = _scalar(material, -1100, 900 + row * 200, f"Group{group}", 0.0)
            switch = _scalar(material, -1100, 1000 + row * 200, f"{group}On", 1.0, index)
            off = _node(material, unreal.MaterialExpressionOneMinus, -900, 1000 + row * 200)
            _connect(switch, "", off, "")
            less = _node(material, unreal.MaterialExpressionSubtract, -700, 900 + row * 200)
            _connect(gate, "", less, "A")
            _connect(_multiply(material, -800, 950 + row * 200, weight, "", off, ""), "", less, "B")
            gate = less
        night = _scalar(material, -1100, 1500, "Night", 0.0, LIGHT_NIGHT_INDEX)
        gain = _scalar(material, -1100, 1600, "NightGain", 1.0)
        strength = _node(material, unreal.MaterialExpressionLinearInterpolate, -700, 1500, const_a=1.0)
        _connect(gain, "", strength, "B")
        _connect(night, "", strength, "Alpha")
        lit = _multiply(material, -450, 900, operational, "", gate, "")
        lit = _multiply(material, -350, 1100, lit, "", strength, "")
        glow = _multiply(material, -200, 480, emission, "RGB", lit, "")
        _output(material, glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    elif kind == "glass":
        # The rooms lit behind the glass at night (#195): NightGlow while it is dark, the Windows group is on and the
        # building is operational, on top of the glass's own glow.
        night_glow = _vector(material, -1100, 900, "NightGlow", (0.0, 0.0, 0.0, 1.0))
        night = _scalar(material, -1100, 1100, "Night", 0.0, LIGHT_NIGHT_INDEX)
        windows = _scalar(material, -1100, 1200, "WindowsOn", 1.0, LIGHT_GROUP_INDICES["Windows"])
        operational = _scalar(material, -1100, 1300, "Operational", 1.0, vehicle_art.LAMP_ON_INDEX)
        lit = _multiply(material, -800, 1150, night, "", windows, "")
        lit = _multiply(material, -650, 1200, lit, "", operational, "")
        glow = _add(material, -200, 480, emission, "RGB", _multiply(material, -450, 900, night_glow, "RGB", lit, ""), "")
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
    # Parts several placements share and the kit's pieces (#389) draw from one instanced component.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    return _finish(material)


def build_coating_material(directory, name, textures, white_masks):
    """M_Bld_Coating: painted and coated surfaces, the instance's colour times the 2 m coating texture, its roughness
    moved by the coating's (COATING_ROUGHNESS_MEAN), the coating's normal, and SHELL_BAKE over it (white_masks: BakedAO's
    white default). textures: {parameter: texture}."""
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
    normal, occlusion = _shell_bake(material, white_masks, samplers["CoatingNormal"], "RGB")
    _output(material, normal, "", unreal.MaterialProperty.MP_NORMAL)
    _output(material, occlusion, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
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


def _seed(material, x, y):
    """Where the object stands, in metres within a kilometre: what the shared props pick their own phases from."""
    position = _node(material, unreal.MaterialExpressionObjectPositionWS, x, y)
    kilometres = _multiply(material, x + 200, y, position, "", _constant(material, x, y + 120, 0.00001), "")
    within = _node(material, unreal.MaterialExpressionFrac, x + 400, y)
    _connect(kilometres, "", within, "")
    return _multiply(material, x + 600, y, within, "", _constant(material, x + 400, y + 120, 1000.0), "")


def build_flag_material(directory, name, texture):
    """M_Flag_Cloth, the shared waving flag (#351) on the kit's SM_Kit_FlagCloth: the flag's texture (FlagTexture, an instance's per
    flag, texture by default) over UV0, never the owner's colours, waving by world position offset (FLAG_WPO_HLSL with
    FLAG_PARAMETERS): across the cloth (its local Y in the world) and sagging down, nothing at the hoist (U 0), so the cloth
    stays on its pole, and fading out with the distance from the camera. Two-sided, its normal the cross of the moved
    surface's screen derivatives in world space, turned to the side seen."""
    material = _material(directory, name)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("tangent_space_normal", False)
    parameters = {key: _scalar(material, -1700, 120 * index, key, value)
                  for index, (key, value) in enumerate(sorted(FLAG_PARAMETERS.items()))}
    color = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -450, -400, parameter_name="FlagTexture",
                  texture=texture, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _output(material, color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    _output(material, parameters["Roughness"], "", unreal.MaterialProperty.MP_ROUGHNESS)

    wind = vehicle_art._custom(material, -900, 0, FLAG_WPO_HLSL,
                               ["UV", "Time", "Seed", "Normal", "Distance"] + list(FLAG_WPO_INPUTS))
    _connect(_node(material, unreal.MaterialExpressionTextureCoordinate, -1300, -300, coordinate_index=0), "", wind, "UV")
    _connect(_node(material, unreal.MaterialExpressionTime, -1300, -200), "", wind, "Time")
    _connect(_seed(material, -1900, -600), "", wind, "Seed")
    across = _node(material, unreal.MaterialExpressionConstant3Vector, -1500, -900,
                   constant=unreal.LinearColor(0.0, 1.0, 0.0, 1.0))
    normal = vehicle_art._transform(material, -1300, -900, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                                    unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    _connect(across, "", normal, "")
    _connect(normal, "", wind, "Normal")
    distance = _node(material, unreal.MaterialExpressionDistance, -1300, -750)
    _connect(_node(material, unreal.MaterialExpressionCameraPositionWS, -1500, -780), "", distance, "A")
    _connect(_node(material, unreal.MaterialExpressionObjectPositionWS, -1500, -700), "", distance, "B")
    _connect(distance, "", wind, "Distance")
    for pin, parameter in FLAG_WPO_INPUTS.items():
        _connect(parameters[parameter], "", wind, pin)
    _output(material, wind, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

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
    shaded = _multiply(material, 0, 700, normalized, "", side, "")
    _output(material, shaded, "", unreal.MaterialProperty.MP_NORMAL)
    # A kit piece (#389), which may draw from an instanced component.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    return _finish(material)


def build_flag_instance(directory, name, parent, texture):
    """MI_Flag_<key>: M_Flag_Cloth flying texture. The parent is set first and the instance updated after, as
    build_material_instance does."""
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
        editing.clear_all_material_instance_parameters(instance)
    else:
        instance = asset_tools.create_asset(name, directory, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    editing.set_material_instance_texture_parameter_value(instance, "FlagTexture", texture)
    editing.update_material_instance(instance)
    if instance.get_editor_property("parent") != parent:
        raise RuntimeError(f"{name} did not keep its parent {parent.get_name()}")
    return instance


def _kit_surface(material, white, white_masks):
    """The textured surface the kit's masters share (#392): BaseColor times SurfaceColor, the ORM map (occlusion,
    roughness, metallic) times Roughness and Metallic, all on UV0, and the finish normal baked on UV1 (FinishNormal, the
    tangent basis on UV0, BUILDING_KIT.md). The maps default to white and flat, so an instance without them draws from
    its values alone. Returns the Roughness, Metallic and SurfaceColor parameters and the base colour."""
    uv1 = _node(material, unreal.MaterialExpressionTextureCoordinate, -1050, -100, coordinate_index=1)
    base = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -750, -700, parameter_name="BaseColor",
                 texture=white, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    orm = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -750, -400, parameter_name="ORM",
                texture=white_masks, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    finish = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -450, -100, parameter_name="FinishNormal",
                   texture=unreal.load_asset(FLAT_NORMAL), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    _connect(uv1, "", finish, "UVs")
    color = _vector(material, -750, -900, "SurfaceColor", (1.0, 1.0, 1.0, 1.0))
    roughness = _scalar(material, -450, -340, "Roughness", 1.0)
    metallic = _scalar(material, -450, -180, "Metallic", 1.0)
    surface = _multiply(material, -250, -700, base, "RGB", color, "RGB")
    _output(material, surface, "", unreal.MaterialProperty.MP_BASE_COLOR)
    _output(material, orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    _output(material, _multiply(material, -250, -340, orm, "G", roughness, ""), "", unreal.MaterialProperty.MP_ROUGHNESS)
    _output(material, _multiply(material, -250, -180, orm, "B", metallic, ""), "", unreal.MaterialProperty.MP_METALLIC)
    _output(material, finish, "RGB", unreal.MaterialProperty.MP_NORMAL)
    return roughness, metallic, color, surface


def _sweep(material, mask, mask_pin):
    """The SWEEP static switch (#393): on, world position offset turns the vertices where mask is 1 about the piece's
    local Z axis (SWEEP_HLSL, SweepRate turns a second); off, no offset."""
    position = _node(material, unreal.MaterialExpressionLocalPosition, -1900, 1500)
    turn = vehicle_art._custom(material, -1500, 1500, SWEEP_HLSL, ["P", "Mask", "Time", "Rate"])
    _connect(position, "", turn, "P")
    _connect(mask, mask_pin, turn, "Mask")
    _connect(_node(material, unreal.MaterialExpressionTime, -1900, 1700), "", turn, "Time")
    _connect(_scalar(material, -1900, 1800, "SweepRate", 1.5), "", turn, "Rate")
    world = vehicle_art._transform(material, -1200, 1500, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                                   unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    _connect(turn, "", world, "")
    switch = _node(material, unreal.MaterialExpressionStaticSwitchParameter, -900, 1500, parameter_name=SWEEP,
                   default_value=False)
    _connect(world, "", switch, "True")
    _connect(_node(material, unreal.MaterialExpressionConstant3Vector, -1200, 1700), "", switch, "False")
    _output(material, switch, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def build_kit_housing_material(directory, name, white, white_masks):
    """M_Kit_Housing, a kit piece's textured housing (#392): its delivered BaseColor, ORM and finish normal (_kit_surface),
    which the piece's MI_Kit_<Piece>_Housing sets. No glow; it draws on instanced meshes like every kit master. With SWEEP
    (08's turning reflector holder, #393) the part above SweepMinZ within SweepRadius of the Z axis (cm) turns."""
    material = _material(directory, name)
    _kit_surface(material, white, white_masks)
    inside = vehicle_art._custom(material, -1900, 1300, "return (P.z >= MinZ && length(P.xy) < Radius) ? 1.0 : 0.0;",
                                 ["P", "MinZ", "Radius"], output=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    _connect(_node(material, unreal.MaterialExpressionLocalPosition, -2200, 1300), "", inside, "P")
    _connect(_scalar(material, -2200, 1400, "SweepMinZ", 0.0), "", inside, "MinZ")
    _connect(_scalar(material, -2200, 1500, "SweepRadius", 0.0), "", inside, "Radius")
    _sweep(material, inside, "")
    material.set_editor_property("used_with_instanced_static_meshes", True)
    return _finish(material)


def build_kit_finish_instance(directory, name, parent, textures, scalars, vectors, switches):
    """MI_Kit_<Piece>_<Slot>: a piece's own finish on a kit master (#392), its delivered maps as texture parameters
    ({parameter: texture}), its values and its static switches ({parameter: bool}). With GLASS on it draws masked and
    two-sided, so its dithered shell shows what is behind it. Parent first and the instance updated after, as
    build_material_instance does."""
    path = f"{directory}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
        editing.clear_all_material_instance_parameters(instance)
    else:
        instance = asset_tools.create_asset(name, directory, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
    editing.set_material_instance_parent(instance, parent)
    for parameter, texture in sorted(textures.items()):
        editing.set_material_instance_texture_parameter_value(instance, parameter, texture)
    for parameter, value in sorted(scalars.items()):
        editing.set_material_instance_scalar_parameter_value(instance, parameter, value)
    for parameter, value in sorted(vectors.items()):
        editing.set_material_instance_vector_parameter_value(instance, parameter, unreal.LinearColor(*value))
    # The parent's static switches are known to the instance only once it is updated with that parent.
    editing.update_material_instance(instance)
    for parameter, value in sorted(switches.items()):
        editing.set_material_instance_static_switch_parameter_value(instance, parameter, value)
        if editing.get_material_instance_static_switch_parameter_value(instance, parameter) != value:
            raise RuntimeError(f"{name}: its parent {parent.get_name()} has no static switch {parameter}")
    glass = switches.get(GLASS, False)
    overrides = instance.get_editor_property("base_property_overrides")
    overrides.set_editor_property("override_blend_mode", glass)
    overrides.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED if glass else unreal.BlendMode.BLEND_OPAQUE)
    overrides.set_editor_property("override_two_sided", glass)
    overrides.set_editor_property("two_sided", glass)
    instance.set_editor_property("base_property_overrides", overrides)
    editing.update_material_instance(instance)
    if instance.get_editor_property("parent") != parent:
        raise RuntimeError(f"{name} did not keep its parent {parent.get_name()}")
    return instance


def build_lens_material(directory, name, white, white_masks):
    """M_Kit_Lens, every kit piece's lit glass (#388): metal, glass or LED from its instance's SurfaceColor, Roughness and
    Metallic, times a piece's own maps where it has them (_kit_surface, #392), with the finish normal baked on UV1. It
    glows as LENS_HLSL works out from its instance's custom data (LENS_LEVEL..LENS_COLOR), the building's Fixtures group
    and the dark (LIGHT_NIGHT_INDEX), which lifts it from DayGain to full Intensity, and its own blink (LENS_BLINK, with
    Blink 1), times the piece's LightProfile (red, UV0) in EmissionTint and HotCoreTint (LENS_SHAPE_HLSL), white by
    default. No light of its own. GLASS lets the shell (the profile's green) show through, SWEEP turns the rest (#393)."""
    material = _material(directory, name)
    roughness, metallic, color, surface = _kit_surface(material, white, white_masks)
    color.set_editor_property("default_value", unreal.LinearColor(0.8, 0.8, 0.8, 1.0))
    roughness.set_editor_property("default_value", 0.4)
    metallic.set_editor_property("default_value", 0.0)

    pins = {}
    for index, (pin, data) in enumerate((("Level", LENS_LEVEL), ("Group", LENS_GROUP), ("Fault", LENS_FAULT),
                                         ("Flash", LENS_FLASH))):
        pins[pin] = _node(material, unreal.MaterialExpressionPerInstanceCustomData, -900, 120 * index, data_index=data)
    pins["Color"] = _node(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -900, 500, data_index=LENS_COLOR)
    pins["Fixtures"] = _scalar(material, -900, 720, "FixturesOn", 1.0, LIGHT_GROUP_INDICES["Fixtures"])
    pins["Night"] = _scalar(material, -900, 820, "Night", 0.0, LIGHT_NIGHT_INDEX)
    pins["DayGain"] = _scalar(material, -900, 920, "DayGain", 0.1)
    pins["Intensity"] = _scalar(material, -900, 1020, "Intensity", 3.0)
    pins["Time"] = _node(material, unreal.MaterialExpressionTime, -900, 1120)
    pins["Blink"] = _scalar(material, -1200, 0, "Blink", 0.0)
    pins["Alternate"] = _scalar(material, -1200, 100, "Alternate", 0.0)
    pins["Seed"] = _seed(material, -1900, 200)
    for index, (key, value) in enumerate(sorted(LENS_BLINK.items())):
        pins[key] = _scalar(material, -1200, 300 + 100 * index, key, value)
    pins["U"] = vehicle_art._mask(material, -1200, 800, _node(material, unreal.MaterialExpressionTextureCoordinate,
                                                              -1400, 800, coordinate_index=0), "r")
    glow = vehicle_art._custom(material, -400, 400, LENS_HLSL, list(pins))
    for pin, node in pins.items():
        _connect(node, "", glow, pin)
    profile = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 700, parameter_name="LightProfile",
                    texture=white_masks, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    shape = vehicle_art._custom(material, -150, 700, LENS_SHAPE_HLSL, ["P", "Tint", "HotTint", "HotCore"])
    _connect(profile, "R", shape, "P")
    _connect(_vector(material, -400, 950, "EmissionTint", (1.0, 1.0, 1.0, 1.0)), "RGB", shape, "Tint")
    _connect(_vector(material, -400, 1100, "HotCoreTint", (1.0, 1.0, 1.0, 1.0)), "RGB", shape, "HotTint")
    _connect(_scalar(material, -400, 1250, "HotCore", 0.0), "", shape, "HotCore")
    _output(material, _multiply(material, 0, 400, glow, "", shape, ""), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # GLASS: the shell darkened in base colour and dithered through, 1 - Transmission x (1 - Fresnel), where it is shell.
    fresnel = _node(material, unreal.MaterialExpressionFresnel, -1200, -1300, exponent=5.0, base_reflect_fraction=0.04)
    facing = _node(material, unreal.MaterialExpressionOneMinus, -1000, -1300)
    _connect(fresnel, "", facing, "")
    shell = _multiply(material, -800, -1300, profile, "G", _scalar(material, -1000, -1200, "GlassTransmission", 0.91), "")
    through = _node(material, unreal.MaterialExpressionOneMinus, -450, -1300)
    _connect(_multiply(material, -600, -1300, shell, "", facing, ""), "", through, "")
    dither = _node(material, unreal.MaterialExpressionMaterialFunctionCall, -250, -1300,
                   material_function=unreal.load_asset(DITHER_FUNCTION))
    _connect(through, "", dither, "Alpha Threshold")
    opacity = _node(material, unreal.MaterialExpressionStaticSwitchParameter, 0, -1300, parameter_name=GLASS,
                    default_value=False)
    _connect(dither, "", opacity, "True")
    _connect(_constant(material, -250, -1150, 1.0), "", opacity, "False")
    _output(material, opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    darken = _node(material, unreal.MaterialExpressionLinearInterpolate, -250, -1000)
    _connect(_constant(material, -450, -1000, 1.0), "", darken, "A")
    _connect(_scalar(material, -450, -900, "GlassDarken", 0.18), "", darken, "B")
    _connect(profile, "G", darken, "Alpha")
    base = _node(material, unreal.MaterialExpressionStaticSwitchParameter, 0, -800, parameter_name=GLASS,
                 default_value=False)
    _connect(_multiply(material, -100, -900, surface, "", darken, ""), "", base, "True")
    _connect(surface, "", base, "False")
    _output(material, base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # SWEEP turns what is not shell, the profile's green read at full detail in the vertex shader.
    vertex_profile = _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -2200, 1300,
                           parameter_name="LightProfile", texture=white_masks,
                           sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                           mip_value_mode=unreal.TextureMipValueMode.TMVM_MIP_LEVEL, const_mip_value=0)
    inner = _node(material, unreal.MaterialExpressionOneMinus, -1900, 1300)
    _connect(vertex_profile, "G", inner, "")
    _sweep(material, inner, "")
    # The kit's pieces are drawn instanced, one component per mesh per building.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    return _finish(material)


def build_rubble_material(directory, name, textures, recipe):
    """One slot of a building's rubble (#403, as its material_recipe.json describes it): BaseColor and ORM (occlusion,
    roughness, metallic) on UV0, the DirectX Normal on recipe's Normal_UV eased toward flat by its NormalStrength; with a
    DetailColor (Debris) the base colour times lerp(1, DetailColor on UV1, DetailColorAmount), with an Emissive mask
    (Embers) a constant glow, the mask's red times emissive_linear_color and emissive_intensity. Nothing reads custom
    primitive data: the ruin turns every light off and the embers stay lit. textures: {map: texture} by the recipe's map
    names (BaseColor, Normal, ORM, DetailColor, Emissive)."""
    material = _material(directory, name)
    uvs = {index: _node(material, unreal.MaterialExpressionTextureCoordinate, -1300, 300 * index, coordinate_index=index)
           for index in (0, 1)}

    def sample(key, x, y, sampler, uv=0):
        node = _node(material, unreal.MaterialExpressionTextureSample, x, y, texture=textures[key], sampler_type=sampler)
        _connect(uvs[uv], "", node, "UVs")
        return node

    color = sample("BaseColor", -900, 0, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    base = color
    if "DetailColor" in textures:
        detail = sample("DetailColor", -900, 300, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 1)
        mix = _node(material, unreal.MaterialExpressionLinearInterpolate, -600, 300, const_a=1.0,
                    const_alpha=recipe["DetailColorAmount"])
        _connect(detail, "RGB", mix, "B")
        base = _multiply(material, -300, 0, color, "RGB", mix, "")
    orm = sample("ORM", -900, 600, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    normal = sample("Normal", -900, 900, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, recipe["Normal_UV"])
    flat = _node(material, unreal.MaterialExpressionConstant3Vector, -900, 1200, constant=unreal.LinearColor(0, 0, 1, 1))
    eased = _node(material, unreal.MaterialExpressionLinearInterpolate, -600, 900, const_alpha=recipe["NormalStrength"])
    _connect(flat, "", eased, "A")
    _connect(normal, "RGB", eased, "B")
    _output(material, base, "RGB" if base is color else "", unreal.MaterialProperty.MP_BASE_COLOR)
    _output(material, orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    _output(material, orm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    _output(material, orm, "B", unreal.MaterialProperty.MP_METALLIC)
    _output(material, eased, "", unreal.MaterialProperty.MP_NORMAL)
    if "Emissive" in textures:
        mask = sample("Emissive", -900, 1500, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        glow = _vector(material, -900, 1800, "EmberColor",
                       [c * recipe["emissive_intensity"] for c in recipe["emissive_linear_color"]] + [1.0])
        _output(material, _multiply(material, -300, 1500, mask, "R", glow, "RGB"), "",
                unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finish(material)


def build_material_instance(directory, name, parent, scalars, vectors, textures=None, switches=None):
    """An instance of parent with the delivery's values, and textures ({parameter: texture}) and static switches
    ({parameter: bool}) where given. The parent is set first and the instance updated after, so the saved instance keeps
    it (the delivery lost a parent set without an update: audit/ownership_persistence_repair)."""
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
    for parameter, texture in sorted((textures or {}).items()):
        editing.set_material_instance_texture_parameter_value(instance, parameter, texture)
    # The parent's static switches are known to the instance only once it is updated with that parent.
    editing.update_material_instance(instance)
    for parameter, value in sorted((switches or {}).items()):
        editing.set_material_instance_static_switch_parameter_value(instance, parameter, value)
        if editing.get_material_instance_static_switch_parameter_value(instance, parameter) != value:
            raise RuntimeError(f"{name}: its parent {parent.get_name()} has no static switch {parameter}")
    editing.update_material_instance(instance)
    if instance.get_editor_property("parent") != parent:
        raise RuntimeError(f"{name} did not keep its parent {parent.get_name()}")
    return instance


def _reset_lods(mesh, expected_triangles):
    """Back to LOD0 alone at full detail: a reimport keeps the LODs and the LOD0 reduction an earlier run gave the mesh."""
    if mesh.get_num_lods() > 1 or mesh.get_num_triangles(0) < expected_triangles * 0.99:
        options = unreal.StaticMeshReductionOptions()
        whole = unreal.StaticMeshReductionSettings()
        whole.set_editor_property("percent_triangles", 1.0)
        whole.set_editor_property("screen_size", 1.0)
        options.set_editor_property("reduction_settings", [whole])
        vehicle_art._mesh_editing().set_lods(mesh, options)


def cover_building(mesh, building_box, placements):
    """Extends mesh's bounds to cover the building's box (min, max in the building's frame, cm) from each of its
    placements (unreal.Transform in the building's frame), so every mesh of the building measures its LOD from about the
    building's middle and they all switch at once (#358)."""
    box = mesh.get_bounding_box()
    low = [box.min.x, box.min.y, box.min.z]
    high = [box.max.x, box.max.y, box.max.z]
    corners = [unreal.Vector(x, y, z) for x in (building_box[0][0], building_box[1][0])
               for y in (building_box[0][1], building_box[1][1]) for z in (building_box[0][2], building_box[1][2])]
    wanted_low, wanted_high = list(low), list(high)
    for placement in placements:
        for corner in corners:
            local = placement.inverse_transform_location(corner)
            for axis, value in enumerate((local.x, local.y, local.z)):
                wanted_low[axis] = min(wanted_low[axis], value)
                wanted_high[axis] = max(wanted_high[axis], value)
    mesh.set_editor_property("negative_bounds_extension", unreal.Vector(*(l - w for l, w in zip(low, wanted_low))))
    mesh.set_editor_property("positive_bounds_extension", unreal.Vector(*(w - h for w, h in zip(wanted_high, high))))


def _rejoin_lod_slots(mesh, lod, slots, source):
    """The LOD's sections back on LOD0's slots (slots, by name) and the slots its import added dropped. The import matches
    a section to a slot by its material's name, so a slot whose instance is named otherwise than the slot (the baked
    shell's, #373) gets a new slot of the same name, or that name and a number."""
    mesh_editing = vehicle_art._mesh_editing()
    names = [str(slot.material_slot_name) for slot in mesh.static_materials]
    if names == slots:
        return
    for section in range(mesh.get_num_sections(lod)):
        index = mesh_editing.get_lod_material_slot(mesh, lod, section)
        if index < len(slots):
            continue
        wanted = names[index] if names[index] in slots else re.sub(r"_\d+$", "", names[index])
        if wanted not in slots:
            raise RuntimeError(f"{mesh.get_name()}: {source} brought the slot {names[index]}, which LOD0 does not have "
                               f"(LOD0's {slots}, now {names})")
        mesh_editing.set_lod_material_slot(mesh, slots.index(wanted), lod, section)
    mesh.set_editor_property("static_materials", list(mesh.static_materials)[:len(slots)])
    if [str(slot.material_slot_name) for slot in mesh.static_materials] != slots:
        raise RuntimeError(f"{mesh.get_name()}: {source} changed LOD0's slots")


def import_mesh(source, directory, name, slot_materials, bounds, expected_triangles, budgets, building_box, placements,
                switch_distance, lod1_source=None, spare_materials=None):
    """Imports one mesh of the building at its own origin (vehicle_art.import_part: normals and tangents as exported, no
    Nanite, no collision, slots bound by name), checks its size along X and its triangles against the delivery's, and
    gives it the building's two LODs (#358, docs/art/MODEL_PERFORMANCE.md): budgets (LOD0, LOD1) triangles, LOD0 reduced
    to its budget and LOD1 lod1_source (a hand-made LOD1 FBX with the same slots) or the engine's reduction of LOD0; no
    budgets keeps LOD0 alone. Its bounds cover the building's box from every placement (cover_building), and LOD1 comes in
    at switch_distance cm from the camera for those bounds (vehicle_art.lod_screen_sizes' measure; the box's half
    diagonal for a mesh several parts draw, which an instanced component measures by)."""
    size = bounds[1][0] - bounds[0][0]
    # A reimport keeps the bounds an earlier run extended, which import_part's size check would read.
    if assets.does_asset_exist(f"{directory}/{name}"):
        earlier = assets.load_asset(f"{directory}/{name}")
        earlier.set_editor_property("negative_bounds_extension", unreal.Vector(0.0, 0.0, 0.0))
        earlier.set_editor_property("positive_bounds_extension", unreal.Vector(0.0, 0.0, 0.0))
    # It also keeps the slots it had, which the asset cannot drop while it is loaded: a slot the FBX no longer has keeps
    # its instance from spare_materials and draws nothing.
    mesh = vehicle_art.import_part(source, directory, name, dict(spare_materials or {}, **slot_materials), size)
    stale = sorted({str(slot.material_slot_name) for slot in mesh.static_materials} - set(slot_materials))
    if stale:
        unreal.log_warning(f"building_art: {name} keeps the slots {stale} an earlier import gave it, without faces")
    _reset_lods(mesh, expected_triangles)
    # A reimport keeps the slots the asset had: the sections of a slot it did not have (the baked shell's PavingWear,
    # #373) point past them, at the slots the FBX adds after its own, in its (sorted) order.
    names = [str(slot.material_slot_name) for slot in mesh.static_materials]
    for slot in sorted(set(slot_materials) - set(names)):
        materials = list(mesh.static_materials)
        materials.append(unreal.StaticMaterial(material_interface=slot_materials[slot], material_slot_name=slot))
        mesh.set_editor_property("static_materials", materials)
    imported = mesh.get_num_triangles(0)
    if imported < expected_triangles * 0.99:
        raise RuntimeError(f"{source} imported {imported} triangles instead of {expected_triangles}")
    cover_building(mesh, building_box, placements)
    if not budgets:
        unreal.log(f"building_art: {name} LOD0 {imported} triangles, no LOD1")
        return mesh

    mesh_editing = vehicle_art._mesh_editing()
    radius = vehicle_art.lod_radius(mesh, len(placements) > 1)
    screen_sizes = [1.0, 2.0 * vehicle_art.SCREEN_MULTIPLE * radius / switch_distance]
    lod0, lod1 = budgets
    options = unreal.StaticMeshReductionOptions()
    options.set_editor_property("auto_compute_lod_screen_size", False)
    settings = []
    # The engine reduces LOD1 from LOD0 once LOD0 is itself reduced, so its share is of lod0.
    for target, base, screen_size in zip((lod0, lod1), (imported, min(lod0, imported)), screen_sizes):
        setting = unreal.StaticMeshReductionSettings()
        setting.set_editor_property("percent_triangles", min(1.0, target / base))
        setting.set_editor_property("screen_size", screen_size)
        settings.append(setting)
    settings = settings[:1] if lod1_source else settings
    options.set_editor_property("reduction_settings", settings)
    if mesh_editing.set_lods(mesh, options) != len(settings):
        raise RuntimeError(f"{name}: could not make its LODs")
    if lod1_source:
        slots = [str(slot.material_slot_name) for slot in mesh.static_materials]
        if mesh_editing.import_lod(mesh, 1, lod1_source) != 1:
            raise RuntimeError(f"{name}: could not import LOD1 from {lod1_source}")
        _rejoin_lod_slots(mesh, 1, slots, lod1_source)
    if not mesh_editing.set_lod_screen_sizes(mesh, screen_sizes):
        raise RuntimeError(f"{name}: could not set its LOD screen sizes")
    unreal.log(f"building_art: {name} LODs " + ", ".join(f"{mesh.get_num_triangles(i)}" for i in range(mesh.get_num_lods())) +
               f" triangles (of {imported}), {mesh.get_num_sections(0)} sections, radius {radius:.0f} cm, screen sizes "
               f"{[round(s, 4) for s in mesh_editing.get_lod_screen_sizes(mesh)]} (LOD1 from {switch_distance / 100:.0f} m)")
    return mesh


def import_kit_mesh(source, directory, name, slot_materials, bounds, triangles, switch_distance, lod1_source=None):
    """Imports one piece of the shared kit (#389, docs/art/BUILDING_KIT.md) at its own origin, as import_mesh does a
    building's (no Nanite, no collision, slots bound by name), with its hand-made LOD1 lod1_source or LOD0 alone (class S).
    A kit mesh is shared by every building, so its bounds stay its own (no cover_building): LOD1 comes in at
    switch_distance cm for its box's half diagonal, as an instanced component measures it, until the building forces its
    one LOD decision on all its meshes (#387, C2)."""
    mesh = vehicle_art.import_part(source, directory, name, slot_materials, bounds[1][0] - bounds[0][0])
    _reset_lods(mesh, triangles[0])
    if mesh.get_num_triangles(0) != triangles[0]:
        raise RuntimeError(f"{source} imported {mesh.get_num_triangles(0)} triangles instead of {triangles[0]}")
    if not lod1_source:
        unreal.log(f"building_art: kit {name} LOD0 {triangles[0]} triangles, no LOD1")
        return mesh
    mesh_editing = vehicle_art._mesh_editing()
    if mesh_editing.import_lod(mesh, 1, lod1_source) != 1:
        raise RuntimeError(f"{name}: could not import LOD1 from {lod1_source}")
    if mesh.get_num_triangles(1) != triangles[1]:
        raise RuntimeError(f"{lod1_source} imported {mesh.get_num_triangles(1)} triangles instead of {triangles[1]}")
    screen_sizes = [1.0, 2.0 * vehicle_art.SCREEN_MULTIPLE * vehicle_art.lod_radius(mesh, True) / switch_distance]
    if not mesh_editing.set_lod_screen_sizes(mesh, screen_sizes):
        raise RuntimeError(f"{name}: could not set its LOD screen sizes")
    unreal.log(f"building_art: kit {name} LODs {triangles} triangles, screen sizes "
               f"{[round(s, 4) for s in mesh_editing.get_lod_screen_sizes(mesh)]}")
    return mesh


def allow_nanite(material):
    """Gives a master the Nanite usage, which a game outside the editor needs to draw a Nanite mesh with it: without it
    the mesh falls back to its non-Nanite proxy with the default material ("missing usage flag Nanite", #349)."""
    # Its result is not whether the flag is set (in the editor it sets the flag and returns false); ask afterwards.
    editing.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE)
    if not editing.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE):
        raise RuntimeError(f"{material.get_name()}: could not set the Nanite usage")
    return _finish(material)


def import_bench_shell(source, directory, name, slot_materials, bounds, expected_triangles):
    """One delivered mesh of the bench's dense HQ shell (#349), imported as import_mesh does but kept apart from the
    production HQ, as delivered with Nanite on and no LODs."""
    size = bounds[1][0] - bounds[0][0]
    mesh = vehicle_art.import_part(source, directory, name, slot_materials, size)
    _reset_lods(mesh, expected_triangles)
    # A reimport keeps the slots the asset had: the sections of a slot it did not have (the baked shell's PavingWear,
    # #373) point past them, at the slots the FBX adds after its own, in its (sorted) order.
    names = [str(slot.material_slot_name) for slot in mesh.static_materials]
    for slot in sorted(set(slot_materials) - set(names)):
        materials = list(mesh.static_materials)
        materials.append(unreal.StaticMaterial(material_interface=slot_materials[slot], material_slot_name=slot))
        mesh.set_editor_property("static_materials", materials)
    imported = mesh.get_num_triangles(0)
    if imported < expected_triangles * 0.99:
        raise RuntimeError(f"{source} imported {imported} triangles instead of {expected_triangles}")
    settings = mesh.get_editor_property("nanite_settings")
    settings.enabled = True
    mesh.set_editor_property("nanite_settings", settings)
    if not mesh.get_editor_property("nanite_settings").enabled:
        raise RuntimeError(f"{name}: Nanite did not stay on")
    unreal.log(f"building_art: {name} Nanite on, {imported} triangles")
    return mesh
