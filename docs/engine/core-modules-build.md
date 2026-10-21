# Modules, plugins, builds and automation tests

## What it is

Unreal C++ is split into modules: a folder with a `<Name>.Build.cs` that UnrealBuildTool (UBT) compiles into one DLL per module in editor builds, or links into one executable in monolithic (packaged) builds. A project is a primary game module plus optional plugins, each plugin holding one or more modules.
Targets (`*.Target.cs`) pick what is built (game, editor, server, client) and configurations pick how (debug to shipping). The Unreal Header Tool (UHT) generates reflection code before C++ compiles.

## Key concepts

### Configurations and targets

- Configurations (`UnrealTargetConfiguration`):
  - `Debug`: engine and game unoptimised.
  - `DebugGame`: game unoptimised, engine optimised; the usual choice for stepping through game code.
  - `Development`: the default; optimised, `check` and the console on.
  - `Test`: like Shipping with some profiling left in.
  - `Shipping`: no console, no `check`, no stats.
- Target types (`TargetType`): `Game`, `Client`, `Server`, `Editor`, `Program`. The editor target is `<Project>Editor`.
- Editor builds load one DLL per module; packaged targets are monolithic (one executable).
- Build guards:
  - `WITH_EDITOR`: editor code. `WITH_EDITORONLY_DATA`: editor-only members; stripped from cooked game builds.
  - `WITH_SERVER_CODE`, `WITH_CLIENT_CODE`, `UE_SERVER`, `UE_EDITOR`, `UE_GAME`.
  - `UE_BUILD_DEBUG`, `UE_BUILD_DEVELOPMENT`, `UE_BUILD_TEST`, `UE_BUILD_SHIPPING`.
  - `WITH_DEV_AUTOMATION_TESTS`: test code.

### Modules

- `ModuleRules` (`.Build.cs`):
  - `PublicDependencyModuleNames`: needed by your public headers; dependents inherit them.
  - `PrivateDependencyModuleNames`: needed only by `.cpp` files and private headers.
  - `DynamicallyLoadedModuleNames`, `PublicIncludePaths`, `PCHUsage`, `RuntimeDependencies`.
  - `Target.bBuildEditor` gates editor-only dependencies.
- `TargetRules` (`.Target.cs`): `Type`, `ExtraModuleNames`, `DefaultBuildSettings = BuildSettingsVersion.V7`, `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8`.
- Module class: implement `IModuleInterface` (`StartupModule()`, `ShutdownModule()`) and register it once with `IMPLEMENT_MODULE(FMyModule, MyModule)`.
  - Primary game module: `IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Name, "Name")`.
  - Other game modules: `IMPLEMENT_GAME_MODULE`. No custom code: `FDefaultModuleImpl`.
- Export macro: `<MODULE>_API` (for example `WORLDDIVIDE_API`) on classes and functions used from other modules. Missing it shows up only as unresolved externals when another module links against you.
- Loading other modules: `FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")`, `FModuleManager::Get().IsModuleLoaded(Name)`, `FModuleManager::GetModulePtr<T>(Name)`.
- A module builds only if it is listed in the `.uproject` (or `.uplugin`) `"Modules"` array and is reachable from a target (a dependency of the primary module or in `ExtraModuleNames`).

### Descriptors (`.uproject`, `.uplugin`)

- `"Modules"` entry: `Name`, `Type` (host type), `LoadingPhase`, optional `TargetAllowList`, `PlatformAllowList`.
- Host types (`EHostType`): `Runtime` (everywhere), `RuntimeNoCommandlet`, `RuntimeAndProgram`, `CookedOnly`, `UncookedOnly`, `Developer`, `DeveloperTool`, `Editor`, `EditorNoCommandlet`, `EditorAndProgram`, `Program`, `ServerOnly`, `ClientOnly`, `ClientOnlyNoCommandlet`.
- Loading phases (`ELoadingPhase`), early to late: `EarliestPossible`, `PostConfigInit`, `PostSplashScreen`, `PreEarlyLoadingScreen`, `PreLoadingScreen`, `PreDefault`, `Default` (normal), `PostDefault`, `PostEngineInit` (editor tooling that needs `GEngine`/`GEditor`), `None`.
- Plugins: a folder under `Plugins/` with a `.uplugin` (`FileVersion`, `FriendlyName`, `Modules`, `Plugins` dependencies, `CanContainContent`, `EnabledByDefault`, `EditorOnly`).
- Enable a plugin in the `.uproject` `"Plugins"` array, optionally limited with `TargetAllowList` (for example `["Editor"]`).

### UBT and UHT

1. UBT reads the `.uproject` and finds the `*.Target.cs` files.
2. It builds the module graph from the `*.Build.cs` files.
3. UHT parses headers with reflection markup and writes `*.generated.h` / `*.gen.cpp` under `Intermediate/`.
4. UBT builds an action graph (makefile) and runs compile and link actions.

### Live Coding

- Patches changed function bodies into a running editor or game (Ctrl+Alt+F11, console `LiveCoding.Compile`).
- It cannot reliably add, remove or change reflected types, `UPROPERTY`/`UFUNCTION` declarations, class layout, constructor defaults of existing objects, or module dependencies. `bEnableReinstancing` (Live Coding settings) is experimental.

### Automation tests

- Put tests in `#if WITH_DEV_AUTOMATION_TESTS`.
- Simple: `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClass, "Dotted.Test.Name", Flags)` + `bool FClass::RunTest(const FString& Parameters)`.
- Parameterised: `IMPLEMENT_COMPLEX_AUTOMATION_TEST` + `GetTests(...)`.
- Multi-frame: `DEFINE_LATENT_AUTOMATION_COMMAND` + `ADD_LATENT_AUTOMATION_COMMAND`. BDD style: `BEGIN_DEFINE_SPEC`.
- Assertions: `TestTrue`, `TestEqual`, `AddError`, `UTEST_TRUE` (also returns from the test on failure).
- Flags (`EAutomationTestFlags`): one or more contexts (`EditorContext`, `ClientContext`, `ServerContext`, `CommandletContext`; `EAutomationTestFlags_ApplicationContextMask` = all) and exactly one filter (`SmokeFilter`, `EngineFilter`, `ProductFilter`, `PerfFilter`, `StressFilter`).

## Rules and pitfalls

1. After changing any header with reflection markup, a `.Build.cs`, a `.Target.cs`, `.uproject` or `.uplugin`: close the editor and do a full UBT build. Live Coding is for function bodies only.
2. UBT refuses to build while a Live Coding session is active ("Unable to build while Live Coding is active"). Close the editor or press Ctrl+Alt+F11 in it.
3. Put a dependency in `Public` only if your public headers include its headers; otherwise `Private`. Editor modules (`UnrealEd`, `*Editor`) go behind `if (Target.bBuildEditor)` and their code behind `#if WITH_EDITOR`, or the packaged game fails to build.
4. Code under `WITH_EDITOR` does not exist in packaged builds. Never put gameplay behind it. Members under `WITH_EDITORONLY_DATA` must not be read at runtime.
5. `check()`/`ensure()` expressions are compiled out in Shipping; never put side effects in them (use `verify()`).
6. One `IMPLEMENT_MODULE` per module, in exactly one `.cpp`. The module name must match the `.Build.cs` name and the descriptor entry.
7. Name tests `Project.Area.Thing`; the dotted name is the filter. Use `ProductFilter` for game tests so engine filters do not pick them up.
8. Run headless tests with the `-Cmd` editor and an exit condition, or the process never quits:

```text
UnrealEditor-Cmd.exe Project.uproject "-ExecCmds=Automation RunTests WorldDivide.Commands+WorldDivide.UI;Quit"
  "-TestExit=Automation Test Queue Empty" -unattended -nullrhi -nosplash -nosound
```

`-ExecCmds` splits at commas, so join several filters with `+`, not commas. Other commands: `Automation List`, `Automation RunFilter Product`, `Automation RunAll`.

## APIs, macros and commands

| Name | Use |
|---|---|
| `ModuleRules`, `PublicDependencyModuleNames`, `PrivateDependencyModuleNames` | Module dependencies |
| `TargetRules`, `TargetType`, `ExtraModuleNames` | Targets |
| `IModuleInterface`, `IMPLEMENT_MODULE`, `IMPLEMENT_PRIMARY_GAME_MODULE`, `FDefaultGameModuleImpl` | Module entry point |
| `FModuleManager::LoadModuleChecked<T>()`, `GetModulePtr<T>()` | Get another module |
| `EHostType`, `ELoadingPhase` | Descriptor `Type` and `LoadingPhase` |
| `WITH_EDITOR`, `WITH_EDITORONLY_DATA`, `UE_BUILD_SHIPPING`, `WITH_DEV_AUTOMATION_TESTS` | Compile guards |
| `Engine/Build/BatchFiles/Build.bat <Target> Win64 Development -Project=<uproject>` | Command-line UBT build |
| `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, `IMPLEMENT_COMPLEX_AUTOMATION_TEST`, `EAutomationTestFlags` | Tests |
| `Automation RunTests <filter>`, `-TestExit=`, `-ExecCmds=` | Run tests |
| `LiveCoding.Compile`, Ctrl+Alt+F11 | Live Coding |

## In World Divide

- One runtime module `WorldDivide` (`Source/WorldDivide/WorldDivide.Build.cs`, `IMPLEMENT_PRIMARY_GAME_MODULE`), targets `WorldDivide` (Game) and `WorldDivideEditor`. Editor-only dependencies sit behind `Target.bBuildEditor`.
- One project plugin, `Plugins/WorldDivideMCP` (Editor module, `PostEngineInit`, editor target only).
- Build and test through `Tools/Scripts/ue.ps1 -Task build` and `-Task test -Filter ...`; never call UBT or the editor by hand. Test flags live in `Source/WorldDivide/Tests/RTSTestFlags.h` (`EditorContext | ProductFilter`). Which tests to add and run: the lean test rule in the project memory and [PERFORMANCE_GUIDELINES.md](../tech/PERFORMANCE_GUIDELINES.md). Project docs win over this note on conflict.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules
- https://dev.epicgames.com/documentation/en-us/unreal-engine/module-properties-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-build-tool-target-reference
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-build-tool-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configurations-reference-for-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/using-live-coding-to-recompile-unreal-engine-applications-at-runtime
- https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine
- Engine source: `Runtime/Core/Public/Modules/ModuleManager.h`, `Runtime/Projects/Public/ModuleDescriptor.h`, `Runtime/Core/Public/Misc/AutomationTest.h`, `Developer/AutomationController/Private/AutomationCommandline.cpp`, `Programs/UnrealBuildTool/System/HotReload.cs`
- Epic course 115.01 (UE 5.5), paraphrased
