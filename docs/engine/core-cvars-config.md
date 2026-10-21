# Config files, console variables and commands

## What it is

Settings come from layered `.ini` files per category (Engine, Game, Input, Editor, Scalability, DeviceProfiles, ...): engine base files, then project defaults, then platform files, then user-saved files, later layers overriding earlier ones. Reflected classes can read their `UPROPERTY`s straight from these files.
Console variables (cvars) are named runtime switches (`r.Shadow.MaxResolution`, `rts.Fx.MaxShotLights`) that can be set from ini, device profiles, the command line or the console; each set carries a priority so a lower-priority source cannot undo a higher one. Console commands run code by name.

## Key concepts

### Ini layers

Load order for one category (`<Type>` = Engine, Game, Input, ...); later layers win:

1. `Engine/Config/Base.ini`
2. `Engine/Config/Base<Type>.ini`
3. `Engine/Config/<Platform>/Base<Platform><Type>.ini`
4. `<Project>/Config/Default<Type>.ini`
5. `<Project>/Config/Generated<Type>.ini` (build-generated, never checked in)
6. `Engine/Config/<Platform>/<Platform><Type>.ini`
7. `<Project>/Config/<Platform>/<Platform><Type>.ini`
8. User files (`User<Type>.ini` in the user settings folders and `<Project>/Config/`)

- Changes made at runtime or in editor settings that are not `DefaultConfig` are saved to `<Project>/Saved/Config/<Platform or WindowsEditor>/<Type>.ini`.
- Engine layer list: `Runtime/Core/Public/Misc/ConfigHierarchy.h`.

### Ini syntax

- `[/Script/Module.ClassName]` section per config class; `Key=Value` sets a value; `;` starts a comment.
- Arrays: `+Key=` adds, `-Key=` removes an exact match, `.Key=` adds even if duplicate, `!Key=` clears.

### Reading config in C++

- Globals: `GConfig` (`FConfigCacheIni`); file names `GEngineIni`, `GGameIni`, `GInputIni`, `GGameUserSettingsIni`, `GEditorPerProjectIni`, `GDeviceProfilesIni`.
- Raw read: `GConfig->GetBool/GetInt/GetFloat/GetString(Section, Key, Out, GGameIni)`.
- Config classes: `UCLASS(Config = Game)` + `UPROPERTY(Config)` loads those members into the CDO from `[/Script/Module.Class]`.
  - `DefaultConfig`: saves go to `Default<Type>.ini` (checked in) instead of `Saved/`.
  - `PerObjectConfig`: sections keyed by object name.
  - `LoadConfig()`, `ReloadConfig()`, `SaveConfig()`, `TryUpdateDefaultConfigFile()`.
- `UDeveloperSettings`: a config class that appears in Project Settings automatically (`GetContainerName()`, `GetCategoryName()`, `GetSectionName()`). Read with `GetDefault<UMySettings>()`.

### Console variables

- Declare: `static TAutoConsoleVariable<int32> CVarX(TEXT("rts.Area.Name"), Default, TEXT("Help"), ECVF_Default);`
- Read: `CVarX.GetValueOnGameThread()`, or `GetValueOnRenderThread()` for `ECVF_RenderThreadSafe` cvars.
- `FAutoConsoleVariableRef` binds a cvar to an existing variable.
- Find any cvar: `IConsoleManager::Get().FindConsoleVariable(TEXT("r.X"))` → `IConsoleVariable` (`GetInt`, `GetFloat`, `Set(Value, ECVF_SetByCode)`, `SetOnChangedCallback(FConsoleVariableDelegate)`).
- Flags: `ECVF_Default`, `ECVF_Cheat` (not in shipping), `ECVF_ReadOnly` (cannot be set from the console), `ECVF_RenderThreadSafe`, `ECVF_Scalability`.

### Set-by priority

Every set carries an `ECVF_SetBy*` source. Low to high:
`Constructor` → `Scalability` → `GameSetting` → `ProjectSetting` → `SystemSettingsIni` → `PluginLowPriority` → `DeviceProfile` → `PluginHighPriority` → `GameOverride` → `ConsoleVariablesIni` → `Hotfix` → `Preview` → `Commandline` → `Code` → `Temp` → `Console`.

- A set from a lower priority than the current value's source is ignored, with a log line ("was ignored as it is lower priority than the previous ...").
- Where ini cvars go:
  - `[ConsoleVariables]` and `[SystemSettings]` in `DefaultEngine.ini`: `SystemSettingsIni` priority.
  - Renderer project settings `[/Script/Engine.RendererSettings]`: `ProjectSetting`.
  - `Engine/Config/ConsoleVariables.ini` (local developer file): `ConsoleVariablesIni`.
  - Scalability groups (`sg.*`) in `DefaultScalability.ini`: `Scalability`.
  - Device profiles in `DefaultDeviceProfiles.ini`, `+CVars=` lines: `DeviceProfile`.

### Console commands

- `FAutoConsoleCommand` (no args, or `FConsoleCommandWithArgsDelegate`), `FAutoConsoleCommandWithWorld`, `FAutoConsoleCommandWithWorldAndArgs` (preferred for gameplay: you get the right world in PIE).
- `UFUNCTION(Exec)` works only on objects the console routes to: local player input, the player controller, its pawn, HUD, the game mode, the cheat manager (`UCheatManager`), game state, camera manager, game instance.

### Command line

- `-ExecCmds="cmd1,cmd2"` runs console commands after start (comma separated).
- `-ini:Engine:[/Script/Engine.Engine]:bSmoothFrameRate=False` overrides one ini key.
- `-dpcvars=r.X=1,rts.Y=2` sets cvars at device-profile time and overrides device profiles.
- Console helpers: `help` (lists all cvars and commands), `DumpConsoleCommands`; typing a cvar name prints its value and who set it; `<cvar> ?` prints its help.

## Rules and pitfalls

1. Change project settings in `Config/Default*.ini`, never in `Saved/Config` (not committed, overwritten) or engine `Base*.ini` (engine install).
2. A `UPROPERTY(Config)` in a class without `UCLASS(Config = ...)` is a UHT error. A `Config` property only loads into the CDO; instances copy it when created.
3. Do not edit `DefaultConfig` settings in code at runtime with `SaveConfig()`; it writes the project file.
4. Ini array lines need the `+`; `Key=Value` on an array replaces the whole array with one entry.
5. Read a cvar with `GetValueOnGameThread()` on the game thread. Cache the `IConsoleVariable*`, not its value, if it can change at runtime.
6. Setting a cvar in code with `ECVF_SetByCode` blocks later console and ini changes of lower priority; use the lowest priority that does the job, or `ECVF_SetByConsole` in debug tools.
7. Name project cvars with the project prefix and an area (`rts.Fx.*`, `rts.Nav.*`, `rts.Debug.*`) and give each a help string.
8. `-ExecCmds` splits at commas; a command that itself needs commas cannot be passed whole.
9. Gameplay tuning does not belong in cvars; cvars are for budgets, debugging and quality switches.
10. A config class reads its ini once, when the CDO is built. Editing the ini while the editor runs does nothing until restart or `ReloadConfig()`.
11. Ini section names for config classes are `[/Script/<Module>.<ClassNameWithoutPrefix>]` (`[/Script/WorldDivide.RTSSettings]`). A typo in the section silently leaves the defaults.
12. `ECVF_Cheat` cvars and `UFUNCTION(Exec)` cheats are compiled out or blocked in Shipping; do not let shipped features depend on them.
13. Register console commands at file scope (`static FAutoConsoleCommand ...`) so they exist in every build that compiles the file; guard debug-only commands with `#if !UE_BUILD_SHIPPING`.
14. Do not read a cvar from a static initializer or a constructor that runs before ini and command-line values are applied; read it when used.
15. A cvar changed with the console in PIE keeps its value for the rest of the editor session; reset it or restart before trusting a measurement.

## APIs, macros and commands

| Name | Use |
|---|---|
| `UCLASS(Config = Game, DefaultConfig)`, `UPROPERTY(Config)` | Config-backed settings |
| `UDeveloperSettings`, `GetDefault<T>()` | Project Settings page + read |
| `GConfig->GetBool/GetInt/GetFloat/GetString`, `GEngineIni`, `GGameIni` | Raw ini access |
| `SaveConfig()`, `TryUpdateDefaultConfigFile()`, `ReloadConfig()` | Write / reload |
| `TAutoConsoleVariable<T>`, `FAutoConsoleVariableRef` | Declare cvars |
| `IConsoleManager::Get().FindConsoleVariable()`, `IConsoleVariable::Set()` | Find / set cvars |
| `ECVF_Cheat`, `ECVF_ReadOnly`, `ECVF_RenderThreadSafe`, `ECVF_SetBy*` | Flags and priorities |
| `FAutoConsoleCommand`, `FAutoConsoleCommandWithWorldAndArgs`, `UFUNCTION(Exec)` | Console commands |
| `-ExecCmds=`, `-ini:<Type>:[Section]:Key=Value`, `-dpcvars=` | Command-line overrides |
| `UDeviceProfileManager`, `DefaultDeviceProfiles.ini` | Per-device cvar sets |
| `help`, `DumpConsoleCommands` | Console discovery |

## In World Divide

- Settings classes: `URTSSettings` and `URTSCameraSettings` (`UDeveloperSettings`, `Config = Game, DefaultConfig`) in `Config/DefaultGame.ini`; `WorldDivide.Config.*` tests check config.
- Cvars and commands use the `rts.` prefix (`rts.Fx.MaxShotLights`, `rts.Nav.MoveRequestsPerFrame`, `rts.UI.Reload`, `rts.Debug.UnitShots`); `ue.ps1` passes them with `-ExecCmds` and `-Cvars` (→ `-dpcvars`).
- Rendering features are set in config, not left at defaults ([PERFORMANCE_GUIDELINES.md](../tech/PERFORMANCE_GUIDELINES.md) rules 29–30; rule 30 asks for a tuned `Config/DefaultScalability.ini`, which does not exist yet). No `[ConsoleVariables]` section in the project ini files yet. Project docs win over this note on conflict.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/configuration-files-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/console-variables-cplusplus-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-device-profiles-in-unreal-engine
- Engine source: `Runtime/Core/Public/HAL/IConsoleManager.h` (`ECVF_SetBy*` order), `Runtime/Core/Public/Misc/ConfigHierarchy.h` (layer list), `Runtime/Core/Private/Misc/ConfigUtilities.cpp`, `Runtime/Engine/Private/Player.cpp` (Exec routing)
- Epic course 115.01 (UE 5.5), paraphrased
