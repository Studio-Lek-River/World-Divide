"""The shared building kit's spike (#387, docs/art/BUILDING_KIT.md, "Per-instance and LOD"): throwaway content in the
git-ignored /Game/Developers/KitSpike that proves, before production code relies on it, that

- a kit lens drawn by an instanced static mesh reads its own state per instance (PerInstanceCustomData: on/level, group
  mask, fault phase, flash seed, colour) next to the building's night state (custom primitive data 12, the component's),
  at LOD0 and LOD1 (rts.Debug.KitSpikeLens), and
- one kit mesh shared by a large and a small building switches to LOD1 with each building's shell at 260 m without a copy
  per building (ue.ps1 -Task buildingshots -Building DA_KitSpike_<Large|Small>_<A|B>, option C with
  -Cvars rts.Debug.KitSpikeLODSync=1).

Built here:

- SM_KitSpike_Lamp: a 1.5 m cube (the engine's, at build scale) drawn yellow at LOD0 and magenta at LOD1, so every
  instance's switch shows in a shot; its LOD1 comes in at 260 m for its own bounds (option A's screen size).
- SM_KitSpike_Lens: the same cube with M_KitSpike_Lens on both LODs, for the lens row.
- SM_KitSpike_SmallShell: a 16 x 16 x 7 m box, grey at LOD0 and cyan at LOD1 at 260 m, the small building's shell.
- DA_KitSpike_Large_A/_B: the USA HQ (DA_TestHeadquarters) with lamps around its box; DA_KitSpike_Small_A/_B: the test
  barracks on the small shell with lamps at its corners. _B gives the lamps a BoundsScale from the building's radius.

Nothing outside /Game/Developers/KitSpike changes. Run with Tools/Scripts/ue.ps1 -Task kitspike.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vehicle_art  # noqa: E402
from building_art import _connect, _constant, _material, _multiply, _node, _output, _scalar, _vector, _finish  # noqa: E402

SPIKE_DIR = "/Game/Developers/KitSpike"
CUBE = "/Engine/BasicShapes/Cube.Cube"
LARGE_SOURCE = "/Game/Data/Buildings/DA_TestHeadquarters"
SMALL_SOURCE = "/Game/Data/Buildings/DA_TestBarracks"
# usa_hq_recipe.SWITCH_DISTANCE: every building switches to LOD1 at 260 m from the RTS camera.
SWITCH_DISTANCE = 26000.0
# The engine cube is 100 cm on a side.
CUBE_SIZE = 100.0
LAMP_SIZE = 150.0
SMALL_SHELL = (1600.0, 1600.0, 700.0)

# The lens's per-instance custom data, as M_Kit_Lens will read it: on/level, group mask (1 = gated by the building's
# Fixtures group, CPD 10), fault phase (0 = no fault), flash seed (0 = steady), colour RGB.
LENS_LEVEL, LENS_GROUP, LENS_FAULT, LENS_FLASH, LENS_COLOR = 0, 1, 2, 3, 4
LENS_FLOATS = 7
FIXTURES_INDEX = 10
NIGHT_INDEX = 12
LENS_HLSL = """
float gate = lerp(1.0, Fixtures, saturate(Group));
float fault = Fault > 0.0 ? step(0.35, frac(Time * 2.3 + Fault + 0.5 * sin(Time * 17.0 + Fault * 40.0))) : 1.0;
float flash = Flash > 0.0 ? step(0.5, frac(Time + Flash)) : 1.0;
return Color * Level * gate * fault * flash * lerp(DayGain, 1.0, Night) * Intensity;
"""

assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary


def _save(asset):
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def flat_material(name, color, glow):
    """A default-lit colour that also glows glow times itself, readable at 260 m by day."""
    material = _material(SPIKE_DIR, name)
    base = _vector(material, -400, 0, "Color", color)
    _output(material, base, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    _output(material, _multiply(material, -200, 200, base, "RGB", _constant(material, -400, 300, glow), ""), "",
            unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    _save(_finish(material))
    return material


def lens_material():
    """M_KitSpike_Lens: the kit lens's glow from its instance's custom data and the component's night and Fixtures."""
    material = _material(SPIKE_DIR, "M_KitSpike_Lens")
    pins = {}
    for index, (pin, data) in enumerate([("Level", LENS_LEVEL), ("Group", LENS_GROUP), ("Fault", LENS_FAULT),
                                         ("Flash", LENS_FLASH)]):
        pins[pin] = _node(material, unreal.MaterialExpressionPerInstanceCustomData, -900, 120 * index, data_index=data)
    pins["Color"] = _node(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -900, 500, data_index=LENS_COLOR)
    pins["Night"] = _scalar(material, -900, 650, "Night", 0.0, NIGHT_INDEX)
    pins["Fixtures"] = _scalar(material, -900, 750, "Fixtures", 1.0, FIXTURES_INDEX)
    pins["DayGain"] = _scalar(material, -900, 850, "DayGain", 0.1)
    pins["Intensity"] = _scalar(material, -900, 950, "Intensity", 3.0)
    pins["Time"] = _node(material, unreal.MaterialExpressionTime, -900, 1050)
    glow = vehicle_art._custom(material, -400, 400, LENS_HLSL, list(pins))
    for pin, node in pins.items():
        _connect(node, "", glow, pin)
    _output(material, glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    housing = _multiply(material, -400, 0, pins["Color"], "", _constant(material, -600, 100, 0.2), "")
    _output(material, housing, "", unreal.MaterialProperty.MP_BASE_COLOR)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    _save(_finish(material))
    return material


def box_mesh(name, size, lod0_material, lod1_material, instanced):
    """The engine cube at size (cm, x y z) with two LODs of the same geometry, LOD0 drawn with lod0_material and LOD1 with
    lod1_material, LOD1 from SWITCH_DISTANCE for its own bounds: the box's half diagonal for an instanced mesh (GPU LOD
    selection), its sphere for one a component draws alone (vehicle_art.lod_radius)."""
    path = f"{SPIKE_DIR}/{name}"
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    mesh = assets.duplicate_asset(CUBE, path)
    mesh_editing = vehicle_art._mesh_editing()
    # For a box the bounds sphere is its half diagonal too.
    radius = 0.5 * math.sqrt(sum(axis * axis for axis in size))
    options = unreal.StaticMeshReductionOptions()
    options.set_editor_property("auto_compute_lod_screen_size", False)
    settings = []
    for screen_size in (1.0, 2.0 * vehicle_art.SCREEN_MULTIPLE * radius / SWITCH_DISTANCE):
        setting = unreal.StaticMeshReductionSettings()
        setting.set_editor_property("percent_triangles", 1.0)
        setting.set_editor_property("screen_size", screen_size)
        settings.append(setting)
    options.set_editor_property("reduction_settings", settings)
    if mesh_editing.set_lods(mesh, options) != 2:
        raise RuntimeError(f"{name}: could not make its LODs")
    for lod in range(2):
        build = mesh_editing.get_lod_build_settings(mesh, lod)
        build.set_editor_property("build_scale3d", unreal.Vector(*(axis / CUBE_SIZE for axis in size)))
        mesh_editing.set_lod_build_settings(mesh, lod, build)
    mesh.set_editor_property("static_materials", [
        unreal.StaticMaterial(material_interface=lod0_material, material_slot_name="LOD0"),
        unreal.StaticMaterial(material_interface=lod1_material, material_slot_name="LOD1")])
    mesh_editing.set_lod_material_slot(mesh, 0, 0, 0)
    mesh_editing.set_lod_material_slot(mesh, 1, 1, 0)
    mesh.set_editor_property("nanite_settings", unreal.MeshNaniteSettings())
    extent = mesh.get_bounds().box_extent
    if abs(extent.x * 2.0 - size[0]) > 1.0:
        raise RuntimeError(f"{name} is {extent.x * 2.0} cm wide instead of {size[0]}")
    _save(mesh)
    unreal.log(f"KitSpike: {name} {size} cm, LOD1 below screen size {settings[1].get_editor_property('screen_size'):.5f}"
               f" (radius {radius:.0f} cm, {'instanced' if instanced else 'alone'})")
    return mesh


def lamp(name, mesh, location, bounds_scale):
    part = unreal.RTSBuildingPart()
    part.set_editor_property("name", name)
    part.set_editor_property("mesh", mesh)
    part.set_editor_property("transform", unreal.Transform(unreal.Vector(*location)))
    part.set_editor_property("cast_shadow", False)
    part.set_editor_property("bounds_scale", bounds_scale)
    return part


def perimeter(low, high, z, count_x, count_y):
    """Points around a box's outline at height z, count_x along each X side and count_y along each Y side, corners once."""
    points = set()
    for i in range(count_x):
        x = low[0] + (high[0] - low[0]) * i / (count_x - 1)
        points.add((round(x), round(low[1]), z))
        points.add((round(x), round(high[1]), z))
    for j in range(count_y):
        y = low[1] + (high[1] - low[1]) * j / (count_y - 1)
        points.add((round(low[0]), round(y), z))
        points.add((round(high[0]), round(y), z))
    return sorted(points)


def spike_building(source, name, shell, points, lamp_mesh, bounds_scale):
    """A copy of source as name, on shell when given, with a lamp at each of points."""
    path = f"{SPIKE_DIR}/{name}"
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    data = assets.duplicate_asset(source.get_path_name(), path)
    if shell:
        data.set_editor_property("mesh", shell)
        data.set_editor_property("mesh_scale", unreal.Vector(1.0, 1.0, 1.0))
    parts = list(data.get_editor_property("parts"))
    parts += [lamp(f"KitSpikeLamp{index:02d}", lamp_mesh, point, bounds_scale) for index, point in enumerate(points)]
    data.set_editor_property("parts", parts)
    _save(data)
    unreal.log(f"KitSpike: {name}, {len(points)} lamps, BoundsScale {bounds_scale:.2f}")


def main():
    yellow = flat_material("M_KitSpike_Yellow", (1.0, 0.8, 0.0), 2.0)
    magenta = flat_material("M_KitSpike_Magenta", (1.0, 0.0, 1.0), 2.0)
    grey = flat_material("M_KitSpike_Grey", (0.35, 0.35, 0.35), 0.0)
    cyan = flat_material("M_KitSpike_Cyan", (0.0, 0.8, 1.0), 0.5)
    lens = lens_material()
    lamp_size = (LAMP_SIZE,) * 3
    lamp_mesh = box_mesh("SM_KitSpike_Lamp", lamp_size, yellow, magenta, True)
    box_mesh("SM_KitSpike_Lens", lamp_size, lens, lens, True)
    small_shell = box_mesh("SM_KitSpike_SmallShell", SMALL_SHELL, grey, cyan, False)
    lamp_radius = 0.5 * math.sqrt(3.0) * LAMP_SIZE

    # The HQ's meshes cover its whole box (building_art.cover_building), so its shell's bounds are the building's.
    large = unreal.load_asset(LARGE_SOURCE)
    box = large.get_editor_property("mesh").get_bounding_box()
    low, high = [box.min.x, box.min.y], [box.max.x, box.max.y]
    # Lamps sit in the shell's frame, which the parts share (ARTSBuilding::ApplyParts attaches them to it).
    large_points = perimeter(low, high, round((box.min.z + box.max.z) / 2.0), 4, 5) + [
        (round((low[0] + high[0]) / 2.0), round((low[1] + high[1]) / 2.0), round(box.max.z + LAMP_SIZE / 2.0))]
    large_radius = 0.5 * (box.max - box.min).length()
    half = [axis / 2.0 for axis in SMALL_SHELL]
    small_points = perimeter([-half[0], -half[1]], [half[0], half[1]], round(half[2] + LAMP_SIZE / 2.0), 2, 2) + [
        (round(half[0]), 0, 0), (round(-half[0]), 0, 0)]
    small_radius = 0.5 * math.sqrt(sum(axis * axis for axis in SMALL_SHELL))
    unreal.log(f"KitSpike: large building box {box.min} to {box.max}, radius {large_radius:.0f} cm; small radius"
               f" {small_radius:.0f} cm; lamp radius {lamp_radius:.0f} cm")

    small_source = unreal.load_asset(SMALL_SOURCE)
    for option, scale in (("A", None), ("B", True)):
        spike_building(large, f"DA_KitSpike_Large_{option}", None, large_points, lamp_mesh,
                       large_radius / lamp_radius if scale else 1.0)
        spike_building(small_source, f"DA_KitSpike_Small_{option}", small_shell, small_points, lamp_mesh,
                       small_radius / lamp_radius if scale else 1.0)
    unreal.log("KitSpike: done")


main()
