# Many units: CPU and draw patterns

## What it is

Patterns that keep hundreds of actors cheap: tick less and in one place, never scan the world per frame, draw repeated meshes as instances, pool what comes and goes, and spend detail by importance.
Unreal's actor model costs a roughly fixed amount per actor and per ticking component. At RTS scale that framework overhead dominates before the game logic does.
Mass Entity is the engine's data-oriented alternative. It is an architectural change, not a drop-in optimisation.

## Key concepts

- **Tick functions.** Every ticking actor and component registers an `FTickFunction` with the world's tick manager. Each one costs dispatch time even when it does nothing.
  - `PrimaryActorTick.bCanEverTick` / `PrimaryComponentTick.bCanEverTick`: whether a tick function exists at all.
  - `bStartWithTickEnabled`: whether it starts on.
  - `SetActorTickEnabled` / `SetComponentTickEnabled`: switch it at runtime.
  - `SetActorTickInterval` / `SetComponentTickInterval` (`TickInterval`): run it every N seconds instead of every frame.
  - `AddTickPrerequisiteComponent` (and the actor variant): order ticks without polling.
- **Manager tick (aggregation).**
  - One `UTickableWorldSubsystem` (or `FTickableGameObject`) loops over a packed array of units and does the per-frame work in one pass.
  - Gains: one dispatch, better cache use, one profiler scope, easy staggering (`i % N == Frame % N`).
  - `ETickableTickType` and `IsTickable` let the subsystem stop ticking while idle.
- **Timers.** `FTimerManager::SetTimer` for slow, event-like repeats.
- **World queries.**
  - `UGameplayStatics::GetAllActorsOfClass` and `TActorIterator` walk every actor of a class: O(all actors) per call.
  - Keep registries (per team, per type) updated on spawn and destroy, and spatial grids for range queries.
- **Instancing.** `UInstancedStaticMeshComponent` (ISM) draws many copies of one mesh with one component and one draw per material per LOD.
  - Instances: `AddInstance`, `AddInstances`, `RemoveInstance`, `UpdateInstanceTransform`, `BatchUpdateInstancesTransforms`.
  - Per-instance data: `SetNumCustomDataFloats` (`NumCustomDataFloats`), then `SetCustomDataValue`; read it in the material with the `PerInstanceCustomData` node (`UMaterialExpressionPerInstanceCustomData`).
  - `UHierarchicalInstancedStaticMeshComponent` (HISM) adds a cluster tree for culling and LOD of very large static sets (foliage, debris). For instances that move often, plain ISM is cheaper to update.
  - `InstanceStartCullDistance` / `InstanceEndCullDistance` cull instances by distance.
- **Custom primitive data.**
  - Per-component variation such as team colour: `SetCustomPrimitiveDataFloat` / `SetCustomPrimitiveDataVector4`, read with the material's Custom Primitive Data input.
  - Keeps every unit on one material instance so draws can merge, unlike a `UMaterialInstanceDynamic` per unit.
- **Significance** (plugin `SignificanceManager`).
  - `USignificanceManager` scores registered objects with a game-supplied function (`FManagedObjectSignificanceFunction`), with an optional post function (`EPostSignificanceType`: None, Concurrent, Sequential).
  - The game calls `Update(Viewpoints)` once per frame and reads `GetSignificance` / `QuerySignificance`.
  - Use the score to lower tick rates, LODs, effects or animation for unimportant objects. It does nothing by itself.
- **Animation cost** (skeletal meshes only).
  - URO: `bEnableUpdateRateOptimizations` on the skinned mesh component skips or interpolates animation frames by screen size. Debug with `a.URO.Enable`, `a.URO.Draw`.
  - `VisibilityBasedAnimTickOption` (`EVisibilityBasedAnimTickOption`): `AlwaysTickPoseAndRefreshBones`, `AlwaysTickPose`, `OnlyTickMontagesAndRefreshBonesWhenPlayingMontages`, `OnlyTickPoseWhenRendered`, `OnlyTickMontagesWhenNotRendered`. Stops pose work off screen.
  - Animation Budget Allocator (plugin `AnimationBudgetAllocator`): use `USkeletalMeshComponentBudgeted`, register it through `IAnimationBudgetAllocator::Get(World)`, feed significance with `SetComponentSignificance`. It holds total animation time under `a.Budget.BudgetMs` (`a.Budget.Enabled`).
- **Niagara pooling.** `UNiagaraFunctionLibrary::SpawnSystemAtLocation` / `SpawnSystemAttached` take an `ENCPoolMethod`:
  - `None`: a new component every time.
  - `AutoRelease`: pooled and returned automatically. Fire and forget; do not touch it after the spawn frame.
  - `ManualRelease`: pooled; you own it and must call `ReleaseToPool`.
  - `ManualRelease_OnComplete`: you release it; it returns to the pool when the effect finishes.
  - Pool size per system: the system asset's `MaxPoolSize` (and `PoolPrimeSize` to pre-create).
  - `FX.NiagaraComponentPool.Enable` switches pooling globally; `FX.DumpNCPoolInfo` prints the pools.
  - Scalability: a `UNiagaraEffectType` on the system sets distance and instance-count culling per quality level; `fx.Niagara.QualityLevel` follows `sg.EffectsQuality`.
- **Actor pooling.**
  - Spawning and destroying actors is costly: construction, component registration, garbage collection.
  - Keep a free list per class. On release: `SetActorHiddenInGame(true)`, collision and tick off, park it. On reuse: reinitialise.
- **Mass Entity.**
  - `MassEntity` stores entities as archetype chunks of fragments and runs processors over them in batches: `FMassEntityManager`, `UMassEntitySubsystem`, `FMassFragment`, `FMassEntityQuery`, `UMassProcessor`.
  - The `MassGameplay` plugin adds spawning (`UMassEntityConfigAsset`), LOD (`UMassLODCollectorProcessor`), representation (ISM or actor by LOD, `UMassVisualizationTrait`), movement, replication and smart objects.
  - Built for thousands of simple agents (crowds, traffic).

## Rules and pitfalls

- **Default to no tick.** Turn `bCanEverTick` off in the constructor unless the class has per-frame work. Enable on demand; disable when done.
- **Idle actors tick slowly or not at all.** Raise the interval while idle; return to every frame on an order.
- **Tick intervals change timing.** Logic that integrates `DeltaTime` still works; logic that counts frames does not. Tests that step frames must account for it.
- **Do not scan the world in per-frame code.**
  - No `GetAllActorsOfClass` or `TActorIterator` in tick, in UI bindings, or inside loops over units.
  - Scans are fine in setup, tests, debug tools and rare events.
- **Stagger periodic work** (target acquisition, AI thinks) across frames by index, so it does not all land on one frame.
- **No per-frame allocations.** Keep scratch arrays as members and `Reset()` them.
- **Instance repeated static pieces.**
  - One ISM per mesh per owner (or per world), not a component or actor per piece.
  - Batch transform updates and mark render state dirty once, not per instance.
- **Never create a dynamic material instance per unit for colour or state.** Use custom primitive data or per-instance custom data.
- **Pool everything that comes and goes with combat:** shots, impacts, lights, sounds.
  - Niagara: `AutoRelease` for one-shots, `ManualRelease` for persistent effects you update.
  - Using a pooled component after release is a bug.
- **Budget short-lived lights.** Each dynamic light costs shading and possibly shadows. Cap the count and turn their shadows off.
- **Significance is a tool, not a fix.** Use it once a capture shows a per-unit cost that can scale with importance. Call `Update` once per frame, from one place.
- **Do not adopt Mass to fix an unmeasured problem.**
  - Mass changes the whole unit model: data, AI, movement, replication, selection and UI.
  - Adopt it only if a capture shows the actor framework itself dominates after ticks, scans and instancing are fixed, and the target is thousands of units.
- **Overlap events and complex collision cost per unit.** Turn overlap events off (`SetGenerateOverlapEvents(false)` or the Generate Overlap Events flag) unless a rule needs them; use simple collision.
- **Measure each change** with a before/after capture. Tick and query changes are easy to get wrong in both directions.

## APIs, cvars and commands

| Name | Kind | Use |
|---|---|---|
| `PrimaryActorTick.bCanEverTick`, `bStartWithTickEnabled`, `TickInterval` | property | Tick existence, start state and interval. |
| `AActor::SetActorTickEnabled`, `SetActorTickInterval` | function | Switch or slow an actor's tick. |
| `UActorComponent::SetComponentTickEnabled`, `SetComponentTickInterval` | function | Same for components. |
| `AddTickPrerequisiteComponent` | function | Order ticks. |
| `UTickableWorldSubsystem`, `FTickableGameObject`, `ETickableTickType` | class / enum | Manager tick. |
| `UGameplayStatics::GetAllActorsOfClass`, `TActorIterator` | function / class | Full scans: setup and tests only. |
| `UInstancedStaticMeshComponent`, `UHierarchicalInstancedStaticMeshComponent` | class | Instanced drawing. |
| `AddInstances`, `UpdateInstanceTransform`, `BatchUpdateInstancesTransforms`, `RemoveInstance` | function | Instance management. |
| `SetNumCustomDataFloats`, `SetCustomDataValue` | function | Per-instance material data. |
| `SetCustomPrimitiveDataFloat`, `SetCustomPrimitiveDataVector4` | function | Per-component material data. |
| `InstanceStartCullDistance`, `InstanceEndCullDistance` | property | ISM distance culling. |
| `USignificanceManager::RegisterObject`, `UnregisterObject`, `Update`, `GetSignificance` | function | Importance scoring (plugin `SignificanceManager`). |
| `SigMan.ObjectsToShow`, `SigMan.FilterTag` | cvar | Significance debug display. |
| `bEnableUpdateRateOptimizations`, `VisibilityBasedAnimTickOption` | property | Skeletal animation throttling. |
| `a.URO.Enable`, `a.URO.Draw` | cvar | URO on/off and debug. |
| `USkeletalMeshComponentBudgeted`, `IAnimationBudgetAllocator` | class | Animation budget (plugin `AnimationBudgetAllocator`). |
| `a.Budget.Enabled`, `a.Budget.BudgetMs` | cvar | Budget on/off and milliseconds. |
| `ENCPoolMethod`, `ReleaseToPool`, `MaxPoolSize` | enum / function / property | Niagara component pooling. |
| `FX.NiagaraComponentPool.Enable`, `FX.DumpNCPoolInfo` | cvar / command | Pool switch and dump. |
| `UNiagaraEffectType`, `fx.Niagara.QualityLevel` | class / cvar | Niagara scalability and culling. |
| `FMassEntityManager`, `UMassEntitySubsystem`, `UMassProcessor`, `FMassFragment`, `FMassEntityQuery` | class | Mass Entity core. |

## In World Divide

- Binding rules: [`../tech/PERFORMANCE_GUIDELINES.md`](../tech/PERFORMANCE_GUIDELINES.md) (ticking 1–5, world queries 6–8, spawning and memory 9–12, instancing 23, collision 24–26). Project docs win on conflict.
- Units are `APawn` + `UFloatingPawnMovement` + `AAIController`. Mass is not planned unless a capture proves the framework dominates ([`../tech/scale/RTS_MOVEMENT_PERFORMANCE.md`](../tech/scale/RTS_MOVEMENT_PERFORMANCE.md) §3.4).
- Per-frame cosmetics are aggregated in world subsystems (`URTSAvoidanceSubsystem`, `URTSRunningGearSubsystem`, `URTSDustSubsystem`, `URTSExhaustSubsystem`, `URTSVehicleLightSubsystem`). Add new per-unit work there, not to `ARTSUnit`'s tick.
- No skeletal meshes or AnimBPs: tracks animate by material and custom primitive data from `UpdateRunningGear` ([`../tech/scale/VEHICLE_TRACK_ANIMATION.md`](../tech/scale/VEHICLE_TRACK_ANIMATION.md)). Repeated running-gear parts are ISMs; team colour is custom primitive data.
- Shot effects are pooled in `URTSShotSubsystem` with a light budget (`rts.Fx.MaxShotLights`, #227). `URTSDustSubsystem` owns and reuses the dust Niagara components per unit.
- Measure with `ue.ps1 -Task bench` (240-unit gate); open items are in [`../tech/PERFORMANCE_AUDIT.md`](../tech/PERFORMANCE_AUDIT.md).

## Sources

- Mass Entity overview: <https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-entity-in-unreal-engine?application_version=5.8>
- Instanced Static Mesh component: <https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-static-mesh-component-in-unreal-engine?application_version=5.8>
- Animation Budget Allocator: <https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine?application_version=5.8>
- Significance manager: <https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine>
- UE 5.8 engine source:
  - `Runtime/Engine/Classes/GameFramework/Actor.h`, `Components/ActorComponent.h`: tick control.
  - `Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h`: instances and custom data.
  - `Runtime/Engine/Classes/Components/SkinnedMeshComponent.h`, `Private/Components/SkinnedMeshComponent.cpp`: `EVisibilityBasedAnimTickOption`, `a.URO.*`.
  - `Plugins/Runtime/SignificanceManager`, `Plugins/Runtime/AnimationBudgetAllocator`.
  - `Plugins/FX/Niagara/Source/Niagara/Public/NiagaraComponentPoolMethodEnum.h`, `Private/NiagaraComponentPool.cpp`.
  - `Runtime/MassEntity`, `Plugins/Runtime/MassGameplay`.
