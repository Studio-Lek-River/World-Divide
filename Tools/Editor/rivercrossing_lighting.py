"""River Crossing's lighting (#159, phase 7 of docs/world/maps/WD_RiverCrossing_01/04_IMPLEMENTATION_ROADMAP.md, 03_TECHNICAL_PLAN.md
section 10): one movable sun, a real-time sky light, the sky atmosphere, volumetric clouds, a light height fog and one
unbound post-process volume with manual exposure, all dynamic so the sun can move. setup_rivercrossing.py runs it on every
run, on the authored level as on a new greybox.

Every value that changes with the time of day lives in TIME_OF_DAY, the data asset URTSTimeOfDayData that lights the level
at its DefaultHour (15:30, the afternoon look and time of day Off of #313); #318's environment director drives the same
asset at runtime. The values that hold at every hour (the fog's falloff, the post process's look and Lumen's distances) are
set here once.

The actors are found again by their label, so a rerun refills the same actor files (One File Per Actor). The greybox's
Sun, SkyLight and SkyAtmosphere (rivercrossing_greybox.spawn_lighting) are kept and set up the same way.

For review only, WD_LIGHT_HOUR lights the level at another hour (for example 19.5 for dusk) without changing the data asset;
do not commit a level lit that way.
"""

import os

import unreal

import rivercrossing_greybox as greybox

ENVIRONMENT_DIR = "/Game/World/Environment"
TIME_OF_DAY = "DA_TimeOfDay_RiverCrossing01"

# The sun's path: up at 6:00, down at 20:00, 53 degrees at 13:00, so at 15:30 it stands at 45 degrees in the south-west and
# its light shines along yaw 35, as on #156's signed-off sheets.
SUN = {"default_hour": 15.5, "sunrise_hour": 6.0, "sunset_hour": 20.0, "noon_elevation": 53.0, "midnight_elevation": -30.0,
       "noon_yaw": -2.5, "yaw_per_hour": 15.0}
# The moon of L_TestMapNight (setup_testmap.py NIGHT_MOON_*): dark enough for URTSDayNightSubsystem::IsDark.
MOON = {"moon_below_elevation": -6.0, "moon_fade_degrees": 6.0, "moon_elevation": 40.0, "moon_lux": 0.6,
        "moon_color": (0.55, 0.66, 1.0)}
# The manual exposure of the afternoon, in stops, calibrated against the #156 sheets (auto exposure with r.ExposureOffset -1.5,
# Saved/CameraShots_ExposurePreview): -1.0 matches their open fields within about 10 %.
DAY_EXPOSURE = -1.0
NIGHT_EXPOSURE = 2.0
DAY_FOG = (0.447, 0.638, 1.0)
NIGHT_FOG = (0.02, 0.03, 0.06)
NIGHT_TINT = (0.8, 0.88, 1.0)
# By sun elevation: (elevation, sun lux, sun colour, sky light, exposure, fog density, fog colour, scene tint). Plan section 10:
# a warm afternoon sun of about 5,800 K, fog no denser than 0.01. Under the horizon the sun only lights the sky; past -6 degrees
# the light is the moon (MOON) and the night's exposure, sky light and grade hold. Only the afternoon is signed off; the
# other hours are a first pass for #318 (at 19:30 the first try, +0.5 stops at the horizon, was far too dark).
KEYS = [
    (-18.0, 0.0, (1.0, 0.85, 0.7), 0.25, NIGHT_EXPOSURE, 0.01, NIGHT_FOG, NIGHT_TINT),
    (-6.0, 2.0, (1.0, 0.8, 0.65), 0.4, DAY_EXPOSURE + 2.5, 0.01, (0.1, 0.12, 0.2), (0.9, 0.93, 1.0)),
    (0.0, 10.0, (1.0, 0.85, 0.7), 1.0, DAY_EXPOSURE + 2.0, 0.008, (0.6, 0.55, 0.6), (1.0, 1.0, 1.0)),
    (15.0, 10.0, (1.0, 0.93, 0.84), 1.0, DAY_EXPOSURE + 0.5, 0.006, DAY_FOG, (1.0, 1.0, 1.0)),
    (45.0, 10.0, (1.0, 0.96, 0.91), 1.0, DAY_EXPOSURE, 0.006, DAY_FOG, (1.0, 1.0, 1.0)),
    (90.0, 10.0, (1.0, 1.0, 1.0), 1.0, DAY_EXPOSURE, 0.006, DAY_FOG, (1.0, 1.0, 1.0)),
]

# The height fog: thin over the play area and deepening only far from the camera, at the border ring (plan section 10). No
# volumetric fog.
FOG_FALLOFF = 0.2
FOG_START_DISTANCE = 40000.0
# Default Earth with a little more haze (Mie).
MIE_SCATTERING_SCALE = 0.005
# Lumen's scene and traces reach past the farthest camera arm, 450 m (docs/engine/world-lighting.md "Top-down camera").
LUMEN_DISTANCE = 60000.0
BLOOM = 0.4

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def color(rgb):
    return unreal.LinearColor(r=rgb[0], g=rgb[1], b=rgb[2], a=1.0)


def build_time_of_day():
    """The time of day data asset, with SUN, MOON and KEYS."""
    path = f"{ENVIRONMENT_DIR}/{TIME_OF_DAY}"
    if assets.does_asset_exist(path):
        data = assets.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.RTSTimeOfDayData)
        data = asset_tools.create_asset(TIME_OF_DAY, ENVIRONMENT_DIR, unreal.RTSTimeOfDayData, factory)
    for name, value in SUN.items():
        data.set_editor_property(name, value)
    for name, value in MOON.items():
        data.set_editor_property(name, color(value) if name == "moon_color" else value)
    keys = []
    for elevation, lux, sun_color, sky, exposure, fog, fog_color, tint in KEYS:
        key = unreal.RTSTimeOfDayKey()
        key.set_editor_property("sun_elevation", elevation)
        key.set_editor_property("sun_lux", lux)
        key.set_editor_property("sun_color", color(sun_color))
        key.set_editor_property("sky_light_intensity", sky)
        key.set_editor_property("exposure_bias", exposure)
        key.set_editor_property("fog_density", fog)
        key.set_editor_property("fog_color", color(fog_color))
        key.set_editor_property("scene_tint", color(tint))
        keys.append(key)
    data.set_editor_property("keys", keys)
    return data


def find_or_spawn(actor_class, label, location):
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() == label and type(actor) is actor_class:
            return actor
    actor = actors.spawn_actor_from_class(actor_class, location, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    actor.set_actor_label(label)
    return actor


def set_up_actors():
    """The level's lighting actors, spawned when missing, with the settings that hold at every hour."""
    sun = find_or_spawn(unreal.DirectionalLight, "Sun", greybox.cm((0.0, 0.0), 20.0))
    light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.set_editor_property("atmosphere_sun_light", True)
    light.set_editor_property("cast_cloud_shadows", False)

    sky = find_or_spawn(unreal.SkyLight, "SkyLight", greybox.cm((0.0, 0.0), 10.0))
    sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_light.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_light.set_editor_property("real_time_capture", True)

    atmosphere = find_or_spawn(unreal.SkyAtmosphere, "SkyAtmosphere", greybox.cm((0.0, 0.0)))
    atmosphere.get_component_by_class(unreal.SkyAtmosphereComponent).set_editor_property(
        "mie_scattering_scale", MIE_SCATTERING_SCALE)

    # The engine's simple cumulus, high above the play area; seen mostly at the screen's top edge at the RTS pitch.
    find_or_spawn(unreal.VolumetricCloud, "Clouds", greybox.cm((0.0, 0.0)))

    fog = find_or_spawn(unreal.ExponentialHeightFog, "HeightFog", greybox.cm((0.0, 0.0)))
    fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog_component.set_editor_property("fog_height_falloff", FOG_FALLOFF)
    fog_component.set_editor_property("start_distance", FOG_START_DISTANCE)
    fog_component.set_editor_property("enable_volumetric_fog", False)

    volume = find_or_spawn(unreal.PostProcessVolume, "Environment", greybox.cm((0.0, 0.0)))
    volume.set_editor_property("unbound", True)
    settings = volume.get_editor_property("settings")
    for name, value in (
            ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
            ("auto_exposure_apply_physical_camera_exposure", False),
            ("bloom_intensity", BLOOM),
            ("vignette_intensity", 0.0),
            ("scene_fringe_intensity", 0.0),
            ("film_grain_intensity", 0.0),
            ("dynamic_global_illumination_method", unreal.DynamicGlobalIlluminationMethod.LUMEN),
            ("reflection_method", unreal.ReflectionMethod.LUMEN),
            ("lumen_scene_view_distance", LUMEN_DISTANCE),
            ("lumen_max_trace_distance", LUMEN_DISTANCE)):
        settings.set_editor_property(f"override_{name}", True)
        settings.set_editor_property(name, value)
    volume.set_editor_property("settings", settings)


def build(world):
    """Lights River Crossing in world at its time of day's DefaultHour. Returns the content directory it wrote, for saving."""
    data = build_time_of_day()
    set_up_actors()
    hour = float(os.environ.get("WD_LIGHT_HOUR") or data.get_editor_property("default_hour"))
    if not data.apply(world, hour):
        raise RuntimeError("rivercrossing_lighting: the level lacks a lighting actor (see the URTSTimeOfDayData::Apply warning)")
    state = data.evaluate(hour)
    unreal.log(f"rivercrossing_lighting: lit at {hour} h, sun at "
               f"{state.get_editor_property('sun_elevation'):.1f} degrees, rotation {state.get_editor_property('light_rotation')}, "
               f"exposure {state.get_editor_property('exposure_bias'):+.2f}")
    return (ENVIRONMENT_DIR + "/",)
