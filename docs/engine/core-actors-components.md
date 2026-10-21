# Actors, components, ticking and subsystems

## What it is

`AActor` is a `UObject` that can be placed or spawned in a `UWorld`, can replicate and can tick. Behaviour and data live in components.
`UActorComponent` is logic only (no transform), `USceneComponent` adds a transform and attachment, `UPrimitiveComponent` adds rendering and collision.
An actor's transform is its `RootComponent`'s transform.
Subsystems are engine-created singletons scoped to the engine, game instance, world or local player; use them instead of manager actors.

## Key concepts

### Components

- Constructor: create with `CreateDefaultSubobject<T>(TEXT("Name"))`, set the root with `SetRootComponent()`, build the hierarchy with `SetupAttachment()`.
- Runtime: `NewObject<T>(this)` + `RegisterComponent()`; attach with `AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName)`; remove with `DestroyComponent()`.
- Lifecycle: `OnComponentCreated()` → `OnRegister()` → `InitializeComponent()` (only if `bWantsInitializeComponent = true`) → `BeginPlay()` → `TickComponent()` → `EndPlay()` → `UninitializeComponent()` → `OnUnregister()` → `OnComponentDestroyed()`.
- Activation: `bAutoActivate`, `SetActive()`. Registration: `bAutoRegister`, `UnregisterComponent()`.

### Actor lifecycle: spawn

`UWorld::SpawnActor<T>(Class, Transform, FActorSpawnParameters)` runs, in order:

1. `PostSpawnInitialize` (registers components: `PreRegisterAllComponents`, `PostRegisterAllComponents`)
2. `PostActorCreated()`
3. `ExecuteConstruction()` (Blueprint construction script, `OnConstruction()`)
4. `PostActorConstruction()`
5. `PreInitializeComponents()` → `InitializeComponents()` → `PostInitializeComponents()`
6. `BeginPlay()` (via `DispatchBeginPlay`, if the world has begun play)

### Actor lifecycle: deferred spawn

- `UWorld::SpawnActorDeferred<T>(Class, Transform, Owner, Instigator, ESpawnActorCollisionHandlingMethod)` returns the actor before step 3.
- Set its properties, then call `FinishSpawning(Transform)` to run construction, initialization and `BeginPlay`.
- Same effect: `FActorSpawnParameters::bDeferConstruction`.

### Actor lifecycle: load and PIE

- Level actors loaded from disk: `PostLoad()` → `UWorld::InitializeActorsForPlay()` → `RouteActorInitialize()` → `PreInitializeComponents()` / `InitializeComponents()` / `PostInitializeComponents()` → `BeginPlay()`.
- No `PostActorCreated` and no construction script at runtime: their result was saved with the level.
- PIE duplicates the editor world; each actor gets `PostDuplicate()` and then follows the load path.

### Actor lifecycle: end

- Triggers: `Destroy()`, `SetLifeSpan()` expiry, level unload, PIE end, quit.
- `EndPlay(EEndPlayReason::Type)` with `Destroyed`, `LevelTransition`, `EndPlayInEditor`, `RemovedFromWorld` or `Quit`.
- A destroyed actor gets `Destroyed()`, is marked garbage and removed from the level.
- A later GC calls `BeginDestroy()` → `IsReadyForFinishDestroy()` → `FinishDestroy()` and frees the memory.
- Status checks: `HasActorBegunPlay()`, `IsActorBeingDestroyed()`, `IsValid(Actor)`.

### Ticking

- Actors have `PrimaryActorTick` (`FActorTickFunction`), components `PrimaryComponentTick`.
- Constructor settings: `bCanEverTick` (cannot be turned on later), `bStartWithTickEnabled`, `TickInterval` (seconds, 0 = every frame), `bTickEvenWhenPaused`, `bAllowTickOnDedicatedServer`, `TickGroup`.
- Tick groups (`ETickingGroup`), in frame order:
  - `TG_PrePhysics`: default; movement and logic that feeds physics.
  - `TG_StartPhysics`, `TG_DuringPhysics`: work that does not need this frame's physics results.
  - `TG_EndPhysics`, `TG_PostPhysics`: read final physics transforms, aim, attach.
  - `TG_PostUpdateWork`: after cameras have updated; `TG_LastDemotable`.
- `EndTickGroup` bounds how late a tick may finish.
- Prerequisites: `AddTickPrerequisiteActor(Other)` / `AddTickPrerequisiteComponent(Other)` make this tick wait for the other; `RemoveTickPrerequisiteActor` undoes it.
- Without a prerequisite, the order of ticks inside a group is not guaranteed, also between an actor and its own components (movement components add theirs themselves).
- Runtime control: `SetActorTickEnabled()`, `SetComponentTickEnabled()`, `SetActorTickInterval()`, `SetComponentTickInterval()`, `PrimaryActorTick.SetTickFunctionEnable()`.
- Slow repeats: `GetWorldTimerManager().SetTimer(...)` instead of tick.
- Custom tick functions: derive a struct from `FTickFunction`, override `ExecuteTick`, set `TickGroup`, call `RegisterTickFunction(Level)` and later `UnRegisterTickFunction()`.

### Subsystems

Created and destroyed with their owner; no registration code.

| Base | Lifetime | Get it |
|---|---|---|
| `UEngineSubsystem` | engine | `GEngine->GetEngineSubsystem<T>()` |
| `UEditorSubsystem` | editor | `GEditor->GetEditorSubsystem<T>()` |
| `UGameInstanceSubsystem` | game instance, survives map changes | `GameInstance->GetSubsystem<T>()` |
| `UWorldSubsystem` | one per world | `World->GetSubsystem<T>()` |
| `UTickableWorldSubsystem` | one per world, ticks | `World->GetSubsystem<T>()` |
| `ULocalPlayerSubsystem` | one per local player | `LocalPlayer->GetSubsystem<T>()` |

- Hooks: `ShouldCreateSubsystem(UObject* Outer)`, `Initialize(FSubsystemCollectionBase& Collection)`, `Deinitialize()`.
- Order: `Collection.InitializeDependency<T>()` inside `Initialize` forces another subsystem to initialize first.
- World subsystem hooks: `PostInitialize()`, `OnWorldBeginPlay(UWorld&)`, `OnWorldEndPlay(UWorld&)`, `DoesSupportWorldType(EWorldType::Type)`.
- `UTickableWorldSubsystem` is also an `FTickableGameObject`: override `Tick(float)` and `GetStatId()` (pure virtual); return `false` from `IsTickable()` when idle.

## Rules and pitfalls

1. Start every actor and component with `bCanEverTick = false`; turn tick on only for real per-frame work. Many ticking actors are a top CPU cost in an RTS.
2. Do not touch other actors, the game mode or the world in a constructor; constructors also build the CDO. Use `BeginPlay` (or `PostInitializeComponents` for setup that must precede other actors' `BeginPlay`).
3. `OnConstruction` runs in the editor every time a property or transform changes. Keep it cheap and side-effect free; no spawning.
4. `SpawnActorDeferred` without `FinishSpawning` leaves a half-built actor. Always pair them.
5. `SpawnActor` can return null (collision handling, invalid class, world teardown). Check it.
6. Call `Super::BeginPlay()`, `Super::EndPlay(Reason)`, `Super::Tick(DeltaTime)`, `Super::Initialize(Collection)` and `Super::Deinitialize()`. A `UTickableWorldSubsystem` that skips `Super::Initialize`/`Super::Deinitialize` never starts or stops ticking.
7. Release timers, delegates and pooled resources in `EndPlay`, not the destructor. Destructors run at GC time, much later.
8. After `Destroy()` the pointer is still non-null until GC. Use `IsValid()` and `TWeakObjectPtr`.
9. World subsystems are also created for editor worlds. Override `DoesSupportWorldType` to limit them to `EWorldType::Game`/`PIE` when they spawn things or hold game state.
10. `GetStatId()` must return a real stat (`RETURN_QUICK_DECLARE_CYCLE_STAT`), or the tick does not show in profiles.
11. A tick interval does not scale behaviour by itself: always use the `DeltaTime` you are given.
12. `BeginPlay` order between actors is not defined. Do not assume another actor already ran its `BeginPlay`; use subsystems or explicit registration.

## APIs, macros and commands

| Name | Use |
|---|---|
| `UWorld::SpawnActor<T>()`, `FActorSpawnParameters` | Spawn and construct an actor now |
| `UWorld::SpawnActorDeferred<T>()`, `AActor::FinishSpawning()` | Spawn, configure, then construct |
| `AActor::Destroy()`, `SetLifeSpan()`, `EndPlay()`, `Destroyed()` | End of life |
| `CreateDefaultSubobject<T>()`, `SetupAttachment()`, `SetRootComponent()` | Constructor-time components |
| `RegisterComponent()`, `AttachToComponent()`, `DestroyComponent()` | Runtime components |
| `PrimaryActorTick`, `PrimaryComponentTick`, `bCanEverTick`, `TickGroup`, `TickInterval` | Tick setup |
| `AddTickPrerequisiteActor()`, `AddTickPrerequisiteComponent()` | Tick ordering |
| `SetActorTickEnabled()`, `SetActorTickInterval()`, `SetComponentTickEnabled()`, `SetComponentTickInterval()` | Runtime tick control |
| `FTimerManager::SetTimer()`, `GetWorldTimerManager()` | Delayed or repeated work without tick |
| `FTickFunction`, `RegisterTickFunction()` | Custom tick in a chosen group |
| `UWorld::GetSubsystem<T>()`, `UGameInstance::GetSubsystem<T>()`, `ULocalPlayer::GetSubsystem<T>()`, `UEngine::GetEngineSubsystem<T>()` | Reach a subsystem |
| `UTickableWorldSubsystem::Tick()`, `GetStatId()`, `IsTickable()` | Ticking world service |
| `stat game`, `dumpticks` | Console: tick cost and the list of registered ticks |

## In World Divide

- Ticking rules are binding: [PERFORMANCE_GUIDELINES.md](../tech/PERFORMANCE_GUIDELINES.md) rules 1–5 (no tick by default, interval when idle, `IsTickable` on subsystems). Project docs win over this note on conflict.
- Units and buildings are spawned deferred in many places (27 files use `SpawnActorDeferred`).
- World services are subsystems: `URTSMoveRequestSubsystem`, `URTSAvoidanceSubsystem`, `URTSShotSubsystem` are `UTickableWorldSubsystem`s. Dust, exhaust, running gear and vehicle lights are `UWorldSubsystem`s with their own `FTickFunction` in `TG_PostUpdateWork`.
- `URTSUIStyleSubsystem` is a `UGameInstanceSubsystem`.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-actor-lifecycle
- https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/spawning-actors-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/components-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine
- Engine source: `Runtime/Engine/Classes/GameFramework/Actor.h`, `Components/ActorComponent.h`, `Engine/EngineBaseTypes.h` (`ETickingGroup`, `FTickFunction`), `Public/Subsystems/WorldSubsystem.h`
- Epic course 115.01 (UE 5.5), paraphrased
