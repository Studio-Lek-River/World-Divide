# Exposing C++ to Blueprint

## What it is

Blueprint sees only what reflection exposes: classes marked `Blueprintable`/`BlueprintType`, properties with `BlueprintReadOnly`/`BlueprintReadWrite`, functions with `BlueprintCallable`/`BlueprintPure`, events that Blueprint implements, interfaces declared with `UINTERFACE`, and dynamic delegates.
Blueprint is a production tool, not a prototype tool: the usual split is systems and hot loops in C++, content-specific glue and tuning in Blueprint and data assets.

## Key concepts

### Functions

- `UFUNCTION(BlueprintCallable, Category = "A|B")`: a node with exec pins.
- `BlueprintPure`: a node without exec pins that reruns for every connected input read.
- A `const` `BlueprintCallable` function with an output also becomes pure, unless `meta = (BlueprintPure = false)`.
- Other specifiers:
  - `BlueprintAuthorityOnly`: runs only with authority.
  - `BlueprintCosmetic`: skipped on dedicated servers.
  - `CallInEditor`: a button in the Details panel.
  - `Exec`; `Server`/`Client`/`NetMulticast` with `Reliable`/`Unreliable` for RPCs.
- Function `meta`:
  - `WorldContext = "WorldContextObject"` for static library functions that need a world; `DefaultToSelf`, `HidePin`.
  - `ExpandEnumAsExecs` (one exec output per enum value), `DeterminesOutputType` (output pin typed by an input class).
  - `DisplayName`, `CompactNodeTitle`, `Keywords`, `ToolTip`, `AdvancedDisplay`, `ReturnDisplayName`.
  - `BlueprintInternalUseOnly`, `BlueprintProtected`, `BlueprintThreadSafe`.
- Function libraries: static `BlueprintCallable`/`BlueprintPure` functions on a `UBlueprintFunctionLibrary` subclass are available in every graph.

### Events Blueprint implements

- `BlueprintImplementableEvent`: no C++ body. C++ calls it; only a Blueprint override does anything.
- `BlueprintNativeEvent`: C++ default body in `Foo_Implementation()`; Blueprint may override and call the parent.
  - C++ callers call `Foo()`, never `Foo_Implementation()` directly.
  - A C++ subclass overrides `Foo_Implementation` (with `override`), not `Foo`.

### Properties

- `BlueprintReadOnly`, `BlueprintReadWrite`; combine with an edit specifier (`EditAnywhere`, `EditDefaultsOnly`, `VisibleAnywhere`).
- Private members need `meta = (AllowPrivateAccess = "true")`.
- `meta = (ExposeOnSpawn = "true")` adds a pin on Spawn Actor / Create Widget nodes.
- Accessors: `UPROPERTY(BlueprintGetter = GetHealth, BlueprintSetter = SetHealth)` routes Blueprint reads and writes through `UFUNCTION(BlueprintGetter)` / `UFUNCTION(BlueprintSetter)` functions, so validation and change events run.

### Classes

- `Blueprintable`: can be a Blueprint parent. `BlueprintType`: usable as a variable type. `Abstract`: cannot be placed or instanced.
- Components: `meta = (BlueprintSpawnableComponent)` makes "Add Component" list them; `ClassGroup = (Custom)` groups them.

### Interfaces

Declare a `UINTERFACE` class and the native `I` class:

```cpp
UINTERFACE(MinimalAPI, Blueprintable)
class UDamageable : public UInterface { GENERATED_BODY() };
class IDamageable { GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent) void ApplyHit(float Amount);
};
```

- Test with `Obj->Implements<UDamageable>()` (or `GetClass()->ImplementsInterface(UDamageable::StaticClass())`).
- Call with `IDamageable::Execute_ApplyHit(Obj, 10.f)`; hold references as `TScriptInterface<IDamageable>`.
- Native-only interfaces: `UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))` with plain `virtual` functions. Then `Cast<IDamageable>(Obj)` and direct calls are correct, and Blueprints cannot implement it.

### Delegates

- Native (fast, not visible to Blueprint): `DECLARE_DELEGATE`, `DECLARE_MULTICAST_DELEGATE`, `DECLARE_EVENT` (+ `_OneParam`, `_RetVal`, ...).
  - Bind with `BindUObject`, `BindLambda`, `AddUObject`, `AddWeakLambda`.
  - Keep the `FDelegateHandle` to `Remove()` it, or `RemoveAll(this)`.
  - Call with `ExecuteIfBound()` / `Broadcast()`.
- Dynamic (serializable, Blueprint-visible, slower): `DECLARE_DYNAMIC_DELEGATE`, `DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDied, AActor*, Killer)` (type and name for each parameter).
  - Expose a multicast one as `UPROPERTY(BlueprintAssignable)` for Blueprint "Bind Event"; `BlueprintCallable` on the property lets Blueprint broadcast it.
  - Bind in C++ with `AddDynamic(this, &UMy::Handler)` / `AddUniqueDynamic`; unbind with `RemoveDynamic`. The handler must be a `UFUNCTION()`.

## Rules and pitfalls

1. Calling a Blueprint-implementable interface function through `Cast<IMyInterface>(Obj)->Func()` skips Blueprint implementations (the cast fails for Blueprint-only implementers). Use `Implements<UMyInterface>()` + `IMyInterface::Execute_Func(Obj, ...)` for any interface function that is a `BlueprintNativeEvent` or `BlueprintImplementableEvent`.
2. Never call `Foo_Implementation()` from outside the class; call `Foo()` so Blueprint overrides run.
3. `BlueprintPure` functions run once per output read. Do not mark expensive or state-changing functions pure.
4. `AddDynamic` handlers must be `UFUNCTION()`s, or the bind fails at runtime (ensure/log), not at compile time. Bind in `BeginPlay`, not the constructor, for delegates on other objects; unbind in `EndPlay`.
5. Dynamic delegate parameter names in the macro are what Blueprint shows; they cannot be changed later without fixing Blueprints.
6. Blueprint-exposed enums must be `uint8` (`enum class E : uint8`). Use `int32`, `float`/`double`, `FName`, `FString`, `FText`, `TArray`, `TMap`, `TSet` and reflected structs for Blueprint-facing signatures; UHT rejects other types with "is not supported by blueprint".
7. Do not declare `BlueprintNativeEvent` or `BlueprintImplementableEvent` functions `virtual` (UHT error for native events, warning for implementable ones). A `BlueprintImplementableEvent` with a return value returns the default when no Blueprint overrides it.
8. Use `BlueprintAuthorityOnly` / `HasAuthority()` for gameplay changes; Blueprint graphs run on clients too.
9. Expose deliberately: every `BlueprintReadWrite` member is a public write API that Blueprints and data can depend on.
10. Renaming or removing an exposed function, property or event breaks the Blueprints that use it; add redirects or keep the old name until they are fixed.
11. Native delegates bound with `BindRaw`/`AddRaw` to an object that dies first crash on broadcast; prefer `BindUObject`/`AddUObject` or `AddWeakLambda`, which skip dead objects.
12. Do not broadcast a delegate from inside a handler of the same delegate that adds or removes bindings; collect changes and apply them after the broadcast.

## APIs, macros and commands

| Name | Use |
|---|---|
| `UFUNCTION(BlueprintCallable)`, `BlueprintPure` | Expose functions |
| `UFUNCTION(BlueprintImplementableEvent)`, `BlueprintNativeEvent` + `_Implementation` | Blueprint-overridable events |
| `BlueprintAuthorityOnly`, `BlueprintCosmetic`, `CallInEditor` | Where/how a function runs |
| `meta = (WorldContext, DefaultToSelf, ExpandEnumAsExecs, DeterminesOutputType, HidePin)` | Node shape |
| `UPROPERTY(BlueprintReadOnly/BlueprintReadWrite)`, `meta = (AllowPrivateAccess, ExposeOnSpawn)` | Expose properties |
| `BlueprintGetter`, `BlueprintSetter` | Accessor-backed properties |
| `UBlueprintFunctionLibrary` | Static Blueprint utilities |
| `UINTERFACE`, `UInterface`, `Implements<U>()`, `ImplementsInterface()`, `Execute_<Func>()`, `TScriptInterface<I>` | Interfaces |
| `CannotImplementInterfaceInBlueprint` | Native-only interface |
| `DECLARE_DYNAMIC_MULTICAST_DELEGATE_*`, `UPROPERTY(BlueprintAssignable)`, `AddDynamic`, `RemoveDynamic`, `Broadcast` | Blueprint-visible events |
| `DECLARE_DELEGATE`, `DECLARE_MULTICAST_DELEGATE`, `BindUObject`, `AddUObject`, `FDelegateHandle` | Native-only events |

## In World Divide

- The project is C++-first; almost nothing is Blueprint-exposed. `URTSHealthComponent` exposes `FRTSDamagedSignature`/`FRTSDeathSignature` as `BlueprintAssignable` dynamic multicast delegates; `URTSEditorScripting` has `BlueprintCallable` editor utilities (with `WorldContext`) for the Python setup scripts.
- `IRTSTargetable` is a native-only interface (`CannotImplementInterfaceInBlueprint`), so `Cast<IRTSTargetable>` is the right call there; any future Blueprint-implementable interface must use `Execute_`.
- No `BlueprintNativeEvent`/`BlueprintImplementableEvent` yet. Gameplay values go in data assets or `URTSSettings`, not Blueprint graphs ([PERFORMANCE_GUIDELINES.md](../tech/PERFORMANCE_GUIDELINES.md) rule 17). Project docs win over this note on conflict.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/exposing-gameplay-elements-to-blueprints-visual-scripting-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/ufunctions-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-uproperties
- https://dev.epicgames.com/documentation/en-us/unreal-engine/metadata-specifiers-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/interfaces-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/delegates-and-lambda-functions-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/dynamic-delegates-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine
- Engine source: `Runtime/CoreUObject/Public/UObject/ObjectMacros.h`, `Runtime/Core/Public/Delegates/DelegateCombinations.h`, `Runtime/CoreUObject/Public/UObject/Interface.h`, `Runtime/CoreUObject/Public/UObject/ScriptInterface.h`
- Epic course 115.01 (UE 5.5), paraphrased
