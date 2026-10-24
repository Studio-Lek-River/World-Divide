# Lighting, sky and post process

## What it is

UE 5.8's fully dynamic lighting stack has four parts:
- **Lumen**: global illumination and reflections, by software tracing (mesh SDFs, global SDF and surface cache) or hardware ray tracing.
- **Virtual Shadow Maps (VSM)**: large virtualized shadow maps, best with Nanite.
- **Physically based sky**: `ASkyAtmosphere`, `ASkyLight`, `AVolumetricCloud` and `AExponentialHeightFog`, all driven by one directional "sun".
- **Post process**: tone mapping, exposure and the per-volume Lumen quality knobs.

There is no baked lighting when `r.AllowStaticLighting=False`. The project methods are set in Project Settings → Rendering and stored in `DefaultEngine.ini` `[/Script/Engine.RendererSettings]`.

## Key concepts

- **Project methods** (`URendererSettings`):
  - `r.DynamicGlobalIlluminationMethod`: 0 none, 1 Lumen, 2 SSGI, 3 plugin. The code default is 0.
  - `r.ReflectionMethod`: 0 none, 1 Lumen, 2 SSR. The code default is 2.
  - `r.Shadow.Virtual.Enable` (Shadow Map Method): 1 VSM, 0 conventional shadow maps. The code default is 0.
  - `r.GenerateMeshDistanceFields`: needed for Lumen software tracing and DFAO. The code default is 0.
  - New projects get Lumen, VSM and distance fields because the project wizard (`GameProjectUtils`) writes them into `DefaultEngine.ini`. A project whose ini lacks those lines runs with the code defaults.
- **Lumen.**
  - Scalability can only turn Lumen off, never on: `r.Lumen.DiffuseIndirect.Allow` and `r.Lumen.Reflections.Allow`.
  - The tracing method comes from `r.Lumen.HardwareRayTracing` (needs "Support Hardware Ray Tracing"). Software tracing uses mesh SDFs (`r.Lumen.TraceMeshSDFs`) near the camera and the global SDF further out.
  - The surface cache (cards) only covers meshes within `LumenSceneViewDistance` (post process, default 20000 cm). `LumenMaxTraceDistance` also defaults to 20000 cm.
  - Far field (`r.LumenScene.FarField`, off by default) needs HW-RT.
  - Nanite meshes and Nanite foliage get the best Lumen coverage. Instanced foliage without Nanite is poorly represented.
- **VSM.**
  - The directional light uses a clipmap from `r.Shadow.Virtual.Clipmap.FirstLevel` (6, about 64 cm radius) to `r.Shadow.Virtual.Clipmap.LastLevel` (22, about 42 km).
  - Resolution is chosen per screen pixel and biased with `r.Shadow.Virtual.ResolutionLodBiasDirectional` (scalability; +1 halves resolution).
  - Pages are cached (`r.Shadow.Virtual.Cache`) and invalidated by moving geometry and WPO.
  - The page pool is `r.Shadow.Virtual.MaxPhysicalPages` (scalability: 512 at ShadowQuality 0–1, up to 8192 at the top level). Overflow logs "Virtual Shadow Map Page Pool overflow" and drops shadows.
  - Soft shadows come from `r.Shadow.Virtual.SMRT.RayCountDirectional`. `LightSourceAngle` sets the penumbra.
  - VSM requires SM6 on DX12. The project targets SM6.
- **Conventional shadows** (when VSM is off):
  - Cascaded shadow maps reach only `DynamicShadowDistanceMovableLight`, default 40000 cm (400 m), split into `DynamicShadowCascades` (default 4).
- **Directional light** (`ADirectionalLight`, `UDirectionalLightComponent`):
  - Set it to Movable.
  - `bAtmosphereSunLight` with `AtmosphereSunLightIndex` 0 makes it the sky's sun.
  - `bCastCloudShadows` and `bCastShadowsOnClouds` link it to volumetric clouds.
- **Sky light** (`ASkyLight`, `USkyLightComponent`):
  - `bRealTimeCapture` re-captures the sky (atmosphere, clouds, fog) continuously, time-sliced. Without it, call `RecaptureSky` after changes.
  - Under Lumen the sky light is occluded by Lumen. Without Lumen it is unshadowed unless DFAO is on.
- **Sky atmosphere** (`ASkyAtmosphere`, `USkyAtmosphereComponent`):
  - A physically based sky and aerial perspective, lit by the atmosphere sun lights.
  - Tune it with `RayleighScatteringScale` and `MieScatteringScale` (haze).
- **Volumetric cloud** (`AVolumetricCloud`, `UVolumetricCloudComponent`): ray-marched clouds driven by a material. Expensive; can cast shadows on the ground through the sun.
- **Height fog** (`AExponentialHeightFog`, `UExponentialHeightFogComponent`):
  - `FogDensity` and `FogHeightFalloff` control the fog.
  - `bEnableVolumetricFog` adds froxel volumetric fog, which is costly.
- **Time of day.**
  - Rotate the sun (`SetWorldRotation` on the light). The sky atmosphere, the real-time sky light capture, Lumen and VSM all follow automatically.
  - Change colour and temperature and intensity with the sun's elevation, and add a second, dimmer atmosphere light (index 1) as the moon if needed.
  - Every sun move invalidates all VSM pages of the directional light. Move it in steps or slowly, never every frame at full rate on low-end hardware.
- **Post process volume** (`APostProcessVolume`, `bUnbound` = whole world):
  - Each field in `FPostProcessSettings` has a `bOverride_*` flag.
  - Essentials:
    - Exposure: `AutoExposureMethod` (`AEM_Manual` for a stable RTS look) and `AutoExposureBias`.
    - Lumen and reflections: `DynamicGlobalIlluminationMethod`, `ReflectionMethod`, `LumenSceneLightingQuality`, `LumenSceneDetail`, `LumenSceneViewDistance`, `LumenFinalGatherQuality`, `LumenMaxTraceDistance`.
    - Look: `BloomIntensity`, `VignetteIntensity`, `SceneFringeIntensity` (chromatic aberration), `FilmGrainIntensity`.
- **Nanite.**
  - On by default (`r.Nanite`). It gives virtualized geometry with per-cluster culling, and is the efficient path for VSM (cached and culled per cluster) and Lumen.
  - Non-Nanite meshes in VSM render per page and cost much more.
- **MegaLights** (`r.MegaLights.EnableForProject`): stochastic many-light shadowing for many shadowed local lights, such as vehicle headlights at night.

## Rules and pitfalls

- Verify the project methods before tuning: run `r.DynamicGlobalIlluminationMethod`, `r.ReflectionMethod` and `r.Shadow.Virtual.Enable` in the console with no argument to print the current value. Missing ini lines mean the code defaults (no GI, SSR, conventional shadows).
- Turning on Lumen in Project Settings also enables `r.GenerateMeshDistanceFields`, which rebuilds every static mesh. Plan that build time.
- Top-down RTS camera (pitch about −55°, arm 150–450 m):
  - **Lumen view distance.** The ground is 150–450 m from the camera, beyond the default `LumenSceneViewDistance` and `LumenMaxTraceDistance` of 200 m. Raise both in the unbound post process volume to about max arm + 30 % (for example 60000), or GI and reflections fall back to the coarse global SDF and sky at max zoom.
  - **Shadow distance.** With conventional shadows the default 400 m `DynamicShadowDistanceMovableLight` cuts shadows off near max zoom. With VSM there is no distance cap, but the resolution per pixel drops as the camera rises, which is fine top-down.
  - **Screen traces.** Lumen screen traces are cheap and effective top-down because most of the scene is on screen. Keep them on.
  - **Detail.** Small meshes such as units, props and clutter are culled from the Lumen scene by size (`r.LumenScene.SurfaceCache.CardMinResolution`). Units still receive GI but contribute little bounce. That is acceptable.
- Exposure: use manual exposure (`AEM_Manual`) for gameplay. Auto exposure pumps when the camera pans between bright fields and dark forest.
- No heavy volumetrics over the play area:
  - Keep `bEnableVolumetricFog` off and height fog density low.
  - Volumetric cloud shadows are optional and costly at RTS camera heights. Clouds are mostly off-screen at −55°.
- Skylight: keep `bRealTimeCapture` on when the time of day changes. Turn it off and call `RecaptureSky` for static scenes on low-end hardware.
- VSM:
  - Keep foliage and unit WPO short-ranged. Animated WPO over a large area re-renders shadow pages every frame.
  - Watch `r.Shadow.Virtual.Stats.Visible` (`ShowStats` is deprecated since 5.7) for page-pool use with 200+ units.
- Scalability: set budgets per `sg.GlobalIlluminationQuality`, `sg.ReflectionQuality`, `sg.ShadowQuality`, `sg.FoliageQuality` and `sg.ViewDistanceQuality` (`BaseScalability.ini`). Do not hard-code `r.*` in game code.
- Night scenes: lower the sun's intensity rather than disabling it. Systems that read the sun (the project's day/night check) need a directional light present.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `r.DynamicGlobalIlluminationMethod` / `r.ReflectionMethod` | project setting | GI and reflection method |
| `r.Shadow.Virtual.Enable` | project setting | VSM on or off |
| `r.GenerateMeshDistanceFields` | project setting | Mesh SDFs for Lumen software tracing |
| `r.Lumen.DiffuseIndirect.Allow` / `r.Lumen.Reflections.Allow` | cvar (scalability) | Disable Lumen per quality level |
| `r.Lumen.HardwareRayTracing` | cvar | HW-RT for Lumen when supported |
| `r.Lumen.TraceMeshSDFs` | cvar | Trace mesh SDFs vs global SDF only |
| `r.Lumen.ScreenProbeGather.DownsampleFactor` | cvar | Main Lumen GI cost knob |
| `r.Lumen.Reflections.DownsampleFactor`, `r.Lumen.Reflections.MaxRoughnessToTrace` | cvar | Reflection cost |
| `r.Lumen.TraceDistanceScale` | cvar (scalability) | Scale trace distance |
| `r.LumenScene.FarField` | cvar | Far-field HW-RT traces |
| `r.Lumen.Visualize.CardPlacement` | cvar | Debug surface-cache cards |
| `r.Shadow.Virtual.ResolutionLodBiasDirectional` | cvar (scalability) | VSM sun resolution |
| `r.Shadow.Virtual.MaxPhysicalPages` | cvar (scalability) | VSM page pool size |
| `r.Shadow.Virtual.Clipmap.FirstLevel` / `LastLevel` | cvar | VSM clipmap range |
| `r.Shadow.Virtual.SMRT.RayCountDirectional` | cvar | Soft-shadow rays (0 = hard) |
| `r.Shadow.Virtual.Cache` | cvar | Page caching |
| `r.Shadow.Virtual.Stats.Visible` | cvar | On-screen VSM stats |
| `r.Shadow.Virtual.Visualize` | cvar | VSM visualization mode |
| `r.SkyAtmosphere`, `r.VolumetricCloud`, `r.VolumetricFog`, `r.Fog` | cvar | Toggle sky features |
| `r.SkyLight.RealTimeReflectionCapture` | cvar | Allow real-time sky capture |
| `r.MegaLights.EnableForProject` | project setting | MegaLights |
| `r.Nanite` | cvar | Toggle Nanite rendering |
| `USkyLightComponent::RecaptureSky` / `SetRealTimeCapture` | C++ | Sky light capture |
| `stat GPU`, `ProfileGPU`, `stat Unit` | command | Measure the frame |
| Viewport → Lit → Lumen / Nanite / Virtual Shadow Map | view mode | Visual debug |

## In World Divide

- `Config/DefaultEngine.ini` sets the methods explicitly since #159: `r.DynamicGlobalIlluminationMethod=1` and `r.ReflectionMethod=1` (Lumen, software tracing, `r.Lumen.HardwareRayTracing=False`, producer decision D7), `r.GenerateMeshDistanceFields=True` and `r.Shadow.Virtual.Enable=1` (VSM), next to `r.AllowStaticLighting=False`. Nothing is baked, so the sun can move.
- River Crossing's lighting is `Tools/Editor/rivercrossing_lighting.py`: a movable "Sun", a real-time "SkyLight", "SkyAtmosphere", "Clouds" (volumetric), "HeightFog" and the unbound post-process volume "Environment" (manual exposure, Lumen view and trace distance 600 m). The time of day is the data asset `URTSTimeOfDayData` (`/Game/World/Environment/DA_TimeOfDay_RiverCrossing01`): the sun's path by hour, and sun intensity and colour, sky light, exposure, fog and grade by the sun's elevation; under −6° the one directional light becomes the moon. `Apply(World, Hour)` lights a level; the setup lights River Crossing at `DefaultHour` (15:30), and #318's environment director drives the same asset.
- `spawn_lighting` in `Tools/Editor/setup_testmap.py` places "Sun" (`bAtmosphereSunLight`, pitch −50°), a real-time-capture `SkyLight` and a `SkyAtmosphere`. `spawn_night_lighting` places a 0.6 lux moon for `L_TestMapNight`.
- `URTSDayNightSubsystem` (`Source/WorldDivide/Core/RTSDayNightSubsystem.h`) decides darkness from the sun's intensity × sin(elevation) (`rts.Light.DarkThreshold`, `rts.Light.ForceDark`).
- Previews and captures use `AEM_Manual` (`RTSEditorScripting.cpp`).
- The planned look for RiverCrossing is in plan §10: sun at about 45°, no volumetric fog, manual exposure. The camera arms are in [the world scale standard](../tech/scale/WORLD_SCALE_STANDARD.md) §6. Project docs win on conflict.

## Sources

- Epic, Building Virtual Worlds (UE 5.8): https://dev.epicgames.com/documentation/en-us/unreal-engine/building-virtual-worlds-in-unreal-engine
- Epic, Lumen Technical Details: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- Epic, Lumen Performance Guide: https://dev.epicgames.com/documentation/unreal-engine/lumen-performance-guide-for-unreal-engine
- Epic, Virtual Shadow Maps: https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine
- Epic, Nanite Virtualized Geometry: https://dev.epicgames.com/documentation/unreal-engine/nanite-virtualized-geometry-in-unreal-engine
- Epic, Shadowing: https://dev.epicgames.com/documentation/unreal-engine/shadowing-in-unreal-engine
- Engine source (UE 5.8):
  - `Engine/Source/Runtime/Renderer/Private/IndirectLightRendering.cpp`, `VirtualShadowMaps/VirtualShadowMapClipmap.cpp`, `VirtualShadowMaps/VirtualShadowMapArray.cpp`, `Lumen/`
  - `Engine/Source/Runtime/Engine/Classes/Engine/RendererSettings.h`, `Engine/Source/Runtime/Engine/Private/Scene.cpp` (post-process defaults), `Engine/Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp`
  - `Engine/Source/Editor/GameProjectGeneration/Private/GameProjectUtils.cpp`
  - `Engine/Config/BaseScalability.ini`
