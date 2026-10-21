# Scalability and rendering cost levers

## What it is

Scalability is Unreal's system for trading look against cost. Quality groups (`sg.*`) each map a level 0–3 (plus Cine, 4) to a set of cvars defined in scalability ini files.
Device profiles apply cvars per platform or hardware class.
On top sit per-asset levers (LODs, draw and cull distances, HLOD) and the choice of renderer features (Nanite, Lumen, Virtual Shadow Maps, TSR).
Choose features deliberately in config instead of inheriting engine or template defaults.

## Key concepts

- **Scalability groups.**
  - `sg.ResolutionQuality`, `sg.ViewDistanceQuality`, `sg.AntiAliasingQuality`, `sg.ShadowQuality`, `sg.GlobalIlluminationQuality`, `sg.ReflectionQuality`.
  - `sg.PostProcessQuality`, `sg.TextureQuality`, `sg.EffectsQuality`, `sg.FoliageQuality`, `sg.ShadingQuality`, `sg.LandscapeQuality`.
  - Levels: 0 Low, 1 Medium, 2 High, 3 Epic, 4 Cine.
- **Scalability ini.**
  - `Engine/Config/BaseScalability.ini` has a section `[<Group>@<Level>]` per level, for example `[ShadowQuality@2]`, listing cvars.
  - A project overrides sections in `Config/DefaultScalability.ini`.
  - Setting `sg.ShadowQuality 1` applies every cvar of `[ShadowQuality@1]`.
  - `[ScalabilitySettings]` holds `PerfIndexThresholds_*`, which `scalability auto` uses to pick levels from a CPU and GPU benchmark.
- **User settings.** `UGameUserSettings` stores the player's levels and applies them at start; a settings menu writes through it.
- **Resolution.**
  - `r.ScreenPercentage` renders the 3D scene below output resolution; the UI stays native.
  - With TSR (`r.AntiAliasingMethod 4`) the upscale is temporal and holds up to roughly 50–70 %.
  - `sg.ResolutionQuality` drives the screen percentage per level.
  - Dynamic resolution (`r.DynamicRes.OperationMode`) varies it at runtime to hold a frame budget. Turn it off when benchmarking.
- **Device profiles.**
  - `BaseDeviceProfiles.ini` and `Config/DefaultDeviceProfiles.ini` define `UDeviceProfile` entries (for example `[Windows DeviceProfile]`) with `+CVars=` lines and a parent.
  - Use them for platform or hardware-class overrides. Use scalability for user-facing quality levels.
  - `dp.Override <Profile>` switches profile at runtime for testing.
- **LOD.**
  - Static and skeletal meshes switch LOD by screen size.
  - `r.StaticMeshLODDistanceScale` and `r.SkeletalMeshLODBias` scale it globally; `r.ForceLOD` forces one for checks.
- **Draw and cull distance.**
  - A primitive's `LDMaxDrawDistance` (Desired Max Draw Distance) and `MinDrawDistance` cull it by distance.
  - `ACullDistanceVolume` sets cull distances by object size (`CullDistances` size/distance pairs) inside the volume.
  - ISM components have `InstanceStartCullDistance` / `InstanceEndCullDistance`.
  - `r.ViewDistanceScale` scales all of these; `sg.ViewDistanceQuality` changes it.
- **HLOD.**
  - With World Partition, HLOD layers (`UHLODLayer`) bake distant cells into merged or simplified proxies.
  - Only for large static worlds. It does nothing for moving units.
- **Occlusion.** Hardware occlusion queries (`r.AllowOcclusionQueries`) or HZB (`r.HZBOcclusion`) cull hidden primitives, at a cost per primitive on the render thread and GPU.
- **Nanite.**
  - Virtualised geometry: cheap for dense static meshes, with a fixed base cost.
  - Poor fit for small, simple, moving meshes and for translucent content.
  - `r.Nanite` toggles it at runtime; `r.Nanite.MaxPixelsPerEdge` trades detail for cost.
- **Lumen.**
  - Dynamic GI: `r.DynamicGlobalIlluminationMethod` (0 none, 1 Lumen, 2 SSGI, 3 plugin).
  - Reflections: `r.ReflectionMethod` (0 none, 1 Lumen, 2 SSR).
  - Software Lumen needs mesh distance fields (`r.GenerateMeshDistanceFields`); hardware Lumen needs ray tracing (`r.Lumen.HardwareRayTracing`).
  - `sg.GlobalIlluminationQuality` and `sg.ReflectionQuality` lower or disable it (`r.Lumen.DiffuseIndirect.Allow`, `r.Lumen.Reflections.Allow`).
- **Virtual Shadow Maps.**
  - `r.Shadow.Virtual.Enable` replaces cascaded shadow maps.
  - Cheap for static Nanite geometry thanks to page caching (`r.Shadow.Virtual.Cache`), but every moving caster invalidates its pages each frame.
  - Resolution bias: `r.Shadow.Virtual.ResolutionLodBiasDirectional`, `r.Shadow.Virtual.ResolutionLodBiasLocal`.
  - Cascaded shadows are tuned with `r.Shadow.CSM.MaxCascades`, `r.Shadow.MaxResolution`, `r.Shadow.DistanceScale`, `r.Shadow.RadiusThreshold`.

## Rules and pitfalls

- **Choose renderer features in `DefaultEngine.ini`.**
  - Compiled-in cvar defaults and the values a project template wrote can differ.
  - Type a cvar name with no argument in the console to print its effective value and where it was set.
- **Every level must be playable.** Low is the minimum-spec target, not a broken look. Test each level at the real camera.
- **Tune for the camera you ship.** An RTS camera sees a wide area at a fixed pitch: view distance, shadow distance and LOD screen sizes matter more than close-up detail.
- **Bench at a fixed level with dynamic resolution off.** Otherwise the engine hides the cost you are measuring.
- **Change one lever at a time and measure.**
  - Lumen, VSM and Nanite interact: VSM caching relies on Nanite, software Lumen relies on distance fields.
- **Turn off distance-field generation if nothing uses it.** `r.GenerateMeshDistanceFields` costs build time, memory and GPU updates with no software Lumen or DFAO to use it.
- **Small moving meshes: no Nanite, few shadows.**
  - Under VSM each moving caster re-renders its shadow pages.
  - Turn `CastShadow` off on small parts (`SetCastShadow(false)`).
- **Cull distances need LODs first.** Culling pops; LODs cut cost without popping. Put the cull beyond the last LOD.
- **Device profiles are not a settings menu.** User-adjustable quality belongs in scalability groups.
- **Scalability cvars set from code are overwritten** when a group level changes. Put project-wide values in the scalability ini section.
- **Check TSR below ~50 %.** It smears thin detail; check unit outlines and selection decals at the chosen value.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `scalability <0-4|auto>` | console | Set all groups, or auto-detect from a benchmark. |
| `sg.ViewDistanceQuality` … `sg.LandscapeQuality` | cvar | Per-group level (list above). |
| `UGameUserSettings` | class | Stores and applies the player's levels. |
| `r.ScreenPercentage` | cvar | 3D render resolution in percent. |
| `r.AntiAliasingMethod` | cvar | 0 none, 1 FXAA, 2 TAA, 3 MSAA, 4 TSR, 5 SMAA (`EAntiAliasingMethod`). |
| `r.TSR.History.ScreenPercentage`, `r.TSR.History.UpdateQuality` | cvar | TSR history resolution and quality. |
| `r.DynamicRes.OperationMode` | cvar | Dynamic resolution on or off. |
| `r.ViewDistanceScale` | cvar | Scales draw and cull distances. |
| `r.StaticMeshLODDistanceScale`, `r.SkeletalMeshLODBias`, `r.ForceLOD` | cvar | LOD selection and debug. |
| `r.DynamicGlobalIlluminationMethod`, `r.ReflectionMethod` | cvar | GI and reflection technique. |
| `r.Lumen.HardwareRayTracing`, `r.Lumen.TraceMeshSDFs`, `r.Lumen.ScreenProbeGather.DownsampleFactor` | cvar | Lumen cost levers. |
| `r.GenerateMeshDistanceFields` | cvar | Build mesh distance fields (software Lumen, DFAO). |
| `r.Shadow.Virtual.Enable`, `r.Shadow.Virtual.MaxPhysicalPages` | cvar | VSM on/off and page pool size. |
| `r.Shadow.CSM.MaxCascades`, `r.Shadow.MaxResolution`, `r.Shadow.DistanceScale`, `r.Shadow.RadiusThreshold` | cvar | Cascaded shadow cost. |
| `r.ContactShadows`, `r.SSR.Quality` | cvar | Cheaper shading features. |
| `r.Nanite`, `r.Nanite.MaxPixelsPerEdge` | cvar | Nanite toggle and detail. |
| `r.AllowOcclusionQueries`, `r.HZBOcclusion` | cvar | Occlusion culling method. |
| `r.LightMaxDrawDistanceScale` | cvar | Scales light draw distances. |
| `r.Streaming.PoolSize`, `r.MipMapLODBias`, `r.MaxAnisotropy` | cvar | Texture memory and filtering. |
| `foliage.DensityScale` | cvar | Foliage instance density. |
| `fx.Niagara.QualityLevel` | cvar | Niagara effect quality, driven by `sg.EffectsQuality`. |
| `dp.Override <Profile>` | console | Apply another device profile for testing. |
| `UDeviceProfile`, `UDeviceProfileManager` | class | Device profile data and selection. |
| `ACullDistanceVolume` | class | Size-based cull distances in a volume. |
| `UHLODLayer` | class | World Partition HLOD setup. |
| `USignificanceManager` | plugin class | Game-defined importance; see [perf-many-units.md](perf-many-units.md). |

## In World Divide

- No `Config/DefaultScalability.ini` exists yet, and `DefaultEngine.ini` sets only `r.AllowStaticLighting=False`. Lumen, VSM, TSR and distance fields are therefore not chosen explicitly. Choosing them is audit finding 4 (#233).
- Guidelines 29–30 set the target: features chosen in config, scalability tuned for the RTS camera, every level playable, Low meets the minimum spec.
- Game meshes do not use Nanite, have LODs above 2,000 triangles, and small parts cast no shadow (guidelines 19, 20, 22). The M20's LOD distances come from `Tools/Editor/vehicle_art.py`.
- Bench any lever with `ue.ps1 -Task bench -Cvars ...` (passed as `-dpcvars`) against a baseline.
- See [`../tech/PERFORMANCE_GUIDELINES.md`](../tech/PERFORMANCE_GUIDELINES.md) and [`../tech/PERFORMANCE_AUDIT.md`](../tech/PERFORMANCE_AUDIT.md) §4. Project docs win on conflict.

## Sources

- Scalability: <https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-in-unreal-engine?application_version=5.8>
- Device profiles: <https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-device-profiles-in-unreal-engine?application_version=5.8>
- Significance manager: <https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine>
- UE 5.8 engine source and config:
  - `Config/BaseScalability.ini`: group sections, `[ScalabilitySettings]`.
  - `Runtime/Engine/Public/SceneUtils.h`: `EAntiAliasingMethod`.
  - `Runtime/Renderer/Private/IndirectLightRendering.cpp`: GI and reflection method values.
  - `Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp`: `r.Shadow.Virtual.Enable`.
  - `Runtime/Engine/Classes/Components/PrimitiveComponent.h`: `LDMaxDrawDistance`, `MinDrawDistance`, `CastShadow`.
  - `Runtime/Engine/Private/UnrealEngine.cpp`: `scalability` command.
