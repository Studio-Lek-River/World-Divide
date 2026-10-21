# UObject, reflection and memory

## What it is

`UObject` is the base of every engine-managed type. The reflection macros (`UCLASS`, `USTRUCT`, `UENUM`, `UPROPERTY`, `UFUNCTION`) are read by the Unreal Header Tool (UHT), which writes `*.generated.h` code.
That generated code gives the engine type info, serialization, editor exposure, Blueprint access, replication and garbage collection (GC).
A `UObject` has no transform, no world presence and no tick of its own; it always has an Outer. Plain C++ types (`F` structs and classes) use Unreal's own smart pointers instead of GC.
UObjects are game-thread objects: create, destroy and mutate them on the game thread.

## Key concepts

### Naming prefixes (UHT enforces them)

- `U`: UObject that is not an actor (`UActorComponent`, `UDataAsset`).
- `A`: actor (`AActor` and children). A reflected `AFoo` that does not derive from `AActor` fails in UHT.
- `F`: struct or plain class (`FVector`, `FMyRow`).
- `E`: enum. `T`: template. `I`: the native half of an interface. `S`: Slate widget.

### Reflection markup

- Include `"Foo.generated.h"` as the **last** include of `Foo.h`.
- Put `GENERATED_BODY()` first in the class or struct body.
- `UCLASS` specifiers: `BlueprintType`, `Blueprintable`, `NotBlueprintable`, `Abstract`, `MinimalAPI`, `Transient`, `Config = Game`, `DefaultConfig`, `Within = OuterClass`.
  Full list: the `UC` namespace in `CoreUObject/Public/UObject/ObjectMacros.h`.
- `UPROPERTY` edit specifiers (pick one):
  - `EditAnywhere`, `EditDefaultsOnly` (class defaults only), `EditInstanceOnly`.
  - `VisibleAnywhere`, `VisibleDefaultsOnly`, `VisibleInstanceOnly`. `Visible*` on a component pointer still lets you edit the component's own properties.
- Other `UPROPERTY` specifiers: `BlueprintReadOnly`, `BlueprintReadWrite`, `Category = "A|B"`, `Transient` (not saved), `Config`, `SaveGame`, `Replicated`, `ReplicatedUsing = OnRep_X`, `Instanced`.
- Common `meta`: `ClampMin`, `ClampMax`, `UIMin`, `UIMax`, `Units = "cm"`, `EditCondition = "bFlag"`, `EditConditionHides`, `DisplayName`, `ToolTip`, `AllowPrivateAccess = "true"`.
  Full list: the `UM` namespace in `ObjectMacros.h`.
- `USTRUCT(BlueprintType)`: value type with `GENERATED_BODY()`. Its `UPROPERTY` object pointers are seen by GC when the struct itself is reachable through a `UPROPERTY`.
- `UENUM(BlueprintType)`: use `enum class EFoo : uint8`; Blueprint only supports `uint8` enums. Per entry: `UMETA(DisplayName = "...")`, `UMETA(Hidden)`.

### Object pointers

- `TObjectPtr<T>`: the UE5 standard for `UPROPERTY` object members. Same size as a raw pointer, same GC behaviour as a raw `UPROPERTY` pointer, plus access tracking and lazy load in editor builds. Use raw `T*` for locals and parameters.
- `TWeakObjectPtr<T>`: does not keep the object alive; `Get()` returns null once it is destroyed or garbage. Use it for caches and references to things you do not own. `IsValid()`, `IsStale()`, `Pin()`.
- `TStrongObjectPtr<T>`: keeps a `UObject` alive from non-reflected code (a static, a lambda, a plain struct) until reset.
- `TSoftObjectPtr<T>` / `TSoftClassPtr<T>`: path-based reference; the asset may not be loaded. See [core-assets-loading.md](core-assets-loading.md).
- `TSubclassOf<T>`: a `UClass*` limited to `T` and its children; the editor filters the picker.

### Garbage collection

- Mark-and-sweep from the root set: objects added with `AddToRoot()` (undo with `RemoveFromRoot()`), CDOs, loaded packages, `FGCObject` referencers.
- References are followed only through reflected properties and `AddReferencedObjects`. An object reachable only through raw, non-reflected pointers is collected and the pointer dangles.
- `FGCObject`: for non-UObject classes that hold UObjects. Derive, override `AddReferencedObjects(FReferenceCollector& Collector)` and `GetReferencerName()` (both pure virtual). Do not store `FGCObject` instances by value in `TArray`.
- Garbage, not PendingKill: `PendingKill` is gone in UE5. `MarkAsGarbage()` flags an object (`RF_MirroredGarbage`, `EInternalObjectFlags::Garbage`); GC clears reflected references to it and frees it in a later collection.
- Test with `IsValid(Obj)` (non-null and not garbage) or `IsValidChecked(Obj)` when it cannot be null. `UObject` has no `IsGarbage()` member, and `IsPendingKill()` no longer exists.
- Destroyed actors are marked garbage; `UPROPERTY` pointers to them are nulled by the next GC.
- GC runs periodically (`gc.TimeBetweenPurgingPendingKillObjects`) and on level loads; force it in tools and tests with `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)`.

### CDO and creation

- One Class Default Object per `UClass`, built by running the constructor once; flag `RF_ClassDefaultObject`. New instances copy their defaults from it.
- Read it with `GetDefault<T>()` or `Class->GetDefaultObject()`. `GetMutableDefault<T>()` changes defaults for every later instance.
- Runtime creation: `NewObject<T>(Outer, Class, Name, Flags)`. Constructor-only: `CreateDefaultSubobject<T>(TEXT("Name"))`.
- Construction order: allocate, zero, run the constructor, copy CDO/archetype values, `PostInitProperties()`. Loaded objects also get `PostLoad()`. Copies: `DuplicateObject`.
- `FObjectInitializer` constructors (`AFoo(const FObjectInitializer& ObjectInitializer)`) are only needed to replace a parent's default subobject class.

### Smart pointers for non-UObjects

- `TSharedPtr<T>` (nullable), `TSharedRef<T>` (never null), `TWeakPtr<T>` (`Pin()` to use), `TUniquePtr<T>` (`MakeUnique`).
- Make with `MakeShared<T>(...)` (one allocation) or `MakeShareable(new T)`.
- Default mode is thread-safe (`ESPMode::ThreadSafe`).
- Derive from `TSharedFromThis<T>` to get `AsShared()` / `SharedThis(this)`.

### Casts

- `Obj->IsA<T>()` or `IsA(UClass*)`: type test including children.
- `Cast<T>(Obj)`: returns null on failure and on null input.
- `ExactCast<T>(Obj)`: matches the exact class only, not children.
- `CastChecked<T>(Obj)`: asserts on failure (checked in non-shipping builds).

## Rules and pitfalls

1. Every `UObject*` member that must stay alive is a `UPROPERTY()` (bare `UPROPERTY()` is enough). A raw member pointer without it is invisible to GC and becomes a dangling pointer.
2. Never use `TSharedPtr`/`TUniquePtr` on a `UObject`. Never `new`/`delete` a `UObject`. Use `NewObject` and let GC free it.
3. Never call `CreateDefaultSubobject` outside a constructor, and never `NewObject` a component in a constructor.
4. Do not put game logic in a UObject constructor: it also runs for the CDO, before a world exists. Use `BeginPlay`, `PostInitProperties`, `PostLoad` or `Initialize`.
5. Check with `IsValid(Obj)`, not `Obj != nullptr`, when the object might be destroyed (actors after `Destroy()`).
6. Do not write `PendingKill`, `IsPendingKill()` or `MarkPendingKill()`; they do not exist in 5.8.
7. `CastExact` does not exist; the name is `ExactCast`.
8. Resolve a `TWeakObjectPtr` once per scope into a local; do not call `Get()` repeatedly in a hot loop.
9. A class without `GENERATED_BODY()` or without its `.generated.h` include fails in UHT with a confusing error; check the include order first.
10. Changes to `UCLASS`/`UPROPERTY`/`UFUNCTION` declarations need a full build, not Live Coding. See [core-modules-build.md](core-modules-build.md).
11. GC is not thread-safe. Worker threads must not hold the only reference to a UObject, and must not create UObjects without care.
12. Do not keep `UObject*` in non-reflected containers (`TArray<UFoo*>` without `UPROPERTY`, `std::` containers). Use a `UPROPERTY()` `TArray<TObjectPtr<UFoo>>` or `TWeakObjectPtr`.

```cpp
UPROPERTY() TObjectPtr<UMyData> Data;      // kept alive
TWeakObjectPtr<AActor> LastTarget;          // not owned, nulls itself
UMyData* Cache;                             // WRONG as a member: GC cannot see it
```

## APIs, macros and commands

| Name | Where | Use |
|---|---|---|
| `UCLASS`, `USTRUCT`, `UENUM`, `UPROPERTY`, `UFUNCTION`, `GENERATED_BODY()` | `UObject/ObjectMacros.h` | Reflection markup |
| `TObjectPtr<T>` | `UObject/ObjectPtr.h` | `UPROPERTY` object members |
| `TWeakObjectPtr<T>` | `UObject/WeakObjectPtrTemplates.h` (Core) | Non-owning UObject reference |
| `TStrongObjectPtr<T>` | `UObject/StrongObjectPtr.h` | Keep a UObject alive from non-reflected code |
| `TSubclassOf<T>` | `Templates/SubclassOf.h` | Class reference limited to `T` |
| `NewObject<T>()`, `CreateDefaultSubobject<T>()`, `DuplicateObject()` | `UObject/UObjectGlobals.h` | Create objects |
| `GetDefault<T>()`, `GetMutableDefault<T>()` | `UObject/UObjectGlobals.h` | Read/change the CDO |
| `IsValid(const UObject*)`, `IsValidChecked()` | `UObject/Object.h` | Non-null-and-not-garbage test |
| `MarkAsGarbage()`, `ClearGarbage()`, `AddToRoot()`, `RemoveFromRoot()` | `UObject/UObjectBaseUtility.h` | GC flags and root set |
| `FGCObject`, `AddReferencedObjects`, `GetReferencerName` | `UObject/GCObject.h` | GC references from plain C++ |
| `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)` | `UObject/UObjectGlobals.h` | Force a GC (tests, tools) |
| `gc.TimeBetweenPurgingPendingKillObjects` | cvar | Seconds between automatic GCs |
| `TSharedPtr`, `TSharedRef`, `TWeakPtr`, `MakeShared`, `MakeShareable`, `TSharedFromThis` | `Templates/SharedPointer.h` | Non-UObject shared ownership |
| `TUniquePtr`, `MakeUnique` | `Templates/UniquePtr.h` | Non-UObject sole ownership |
| `Cast<T>`, `ExactCast<T>`, `CastChecked<T>`, `IsA<T>()` | `Templates/Casts.h`, `UObjectBaseUtility.h` | Type tests and casts |
| `GetOuter()`, `GetTypedOuter<T>()`, `GetClass()`, `StaticClass()` | `UObject/UObjectBaseUtility.h` | Ownership chain and class info |
| `obj list class=<Class>`, `obj gc` | console | Count live objects of a class / force GC |

## In World Divide

- 64 reflected classes under `Source/WorldDivide/`. Members use `TObjectPtr`; cross-actor references use `TWeakObjectPtr` (units, targets, buildings).
- `TStrongObjectPtr` holds the procedurally made shot sounds in statics (`Combat/RTSShotSound.cpp`).
- `IRTSTargetable` (`Combat/RTSTargetable.h`) is a native-only interface (`CannotImplementInterfaceInBlueprint`), so `Cast<IRTSTargetable>` is correct there.
- Per-frame code must not allocate temporaries: [PERFORMANCE_GUIDELINES.md](../tech/PERFORMANCE_GUIDELINES.md) rule 12. Project docs win over this note on conflict.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/objects-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-object-handling-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-objects-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-uproperties
- https://dev.epicgames.com/documentation/en-us/unreal-engine/class-specifiers
- https://dev.epicgames.com/documentation/en-us/unreal-engine/metadata-specifiers-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/structs-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-pointers-in-unreal-engine
- Engine source: `Engine/Source/Runtime/CoreUObject/Public/UObject/` (`ObjectMacros.h`, `Object.h`, `UObjectBaseUtility.h`, `GCObject.h`), `Templates/Casts.h`
- Epic course 115.01 (UE 5.5), paraphrased
