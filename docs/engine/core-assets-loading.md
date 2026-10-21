# Assets, references and loading

## What it is

Every asset lives in a package (`UPackage`, one `.uasset`/`.umap` file) and is named by a path such as `/Game/Maps/L_TestMap.L_TestMap` (package path + `.` + object name). A hard reference loads the target whenever the referencer loads; a soft reference stores only the path and loads on request.
`FStreamableManager` and `UAssetManager` load soft references asynchronously; the asset registry answers "what assets exist" without loading them. Cooking converts the assets a build needs into platform format.

## Key concepts

### Paths

- `/Game/...` is the project's `Content/`; `/PluginName/...` is a plugin's content; `/Script/Module.Class` is a native class.
- Object path = package path + `.` + object name: `/Game/Data/DA_Tank.DA_Tank`. A Blueprint's generated class is `/Game/BP/BP_Tank.BP_Tank_C`.
- Path types: `FSoftObjectPath` (any object path), `FTopLevelAssetPath` (package + asset name; used for class paths in the registry), `FSoftClassPath`.

### Hard references

- `UPROPERTY() TObjectPtr<UStaticMesh> Mesh;`, `TSubclassOf<T>`, `ConstructorHelpers::FObjectFinder` / `FClassFinder` (constructor only).
- Loading the referencer loads the whole hard-reference chain, recursively. One careless hard reference in a widely used asset can pull most of the game into memory.

### Soft references

- `TSoftObjectPtr<T>` and `TSoftClassPtr<T>` wrap an `FSoftObjectPath` and do not load anything by themselves.
- `IsNull()`: no path set. `IsPending()`: path set, not loaded. `Get()`: the object if already in memory, else null.
- `LoadSynchronous()`: blocking load. `ToSoftObjectPath()`: the path, for async requests.

### Loading

- Synchronous: `LoadObject<T>(nullptr, TEXT("/Game/..."))`, `StaticLoadObject`, `FStreamableManager::RequestSyncLoad`. `FindObject<T>` only finds what is already loaded.
- Asynchronous: `FStreamableManager::RequestAsyncLoad(Paths, FStreamableDelegate::CreateUObject(this, &UMy::OnLoaded))` returns a `TSharedPtr<FStreamableHandle>`.
  - The handle keeps the assets loaded while it lives.
  - `HasLoadCompleted()`, `WaitUntilComplete()`, `GetLoadedAsset()`, `ReleaseHandle()`.
  - Shared manager: `UAssetManager::GetStreamableManager()` (static).
  - Low level: `LoadPackageAsync`.

### Primary assets and the asset manager

- A primary asset is one the asset manager tracks by `FPrimaryAssetId` (`FPrimaryAssetType` + name), typically a `UPrimaryDataAsset` subclass, which implements `GetPrimaryAssetId()`.
- Register types under `[/Script/Engine.AssetManagerSettings]` `PrimaryAssetTypesToScan` (Project Settings > Asset Manager).
- Load with `UAssetManager::Get().LoadPrimaryAsset(Id, Bundles, Delegate)` / `LoadPrimaryAssets`; release with `UnloadPrimaryAsset`.

### Asset registry

- `IAssetRegistry` from `FAssetRegistryModule` (module `AssetRegistry`). Queries return `FAssetData` (path, class, tags) without loading.
- Queries: `GetAssetsByPath`, `GetAssetsByClass(FTopLevelAssetPath)`, `GetAssets(FARFilter)`, `GetAssetByObjectPath(FSoftObjectPath)`.
- `FAssetData::GetAsset()` loads the asset; `IsAssetLoaded()` tells without loading.
- In the editor the registry scans asynchronously: wait with `IsLoadingAssets()` / `OnFilesLoaded`, or call `SearchAllAssets(true)` in tools.

### Redirectors and renames

- Renaming or moving an asset in the editor leaves a `UObjectRedirector` at the old path so old references still load.
- Fix them up (Content Browser > Fix Up Redirectors) and commit the removal together with the re-saved referencers.
- Renamed C++ classes, properties and enums need `[CoreRedirects]` entries (`+ClassRedirects=`, `+PropertyRedirects=`) in `DefaultEngine.ini`.

### Cooking

- Cooking builds a platform copy of the content a build needs.
- What gets cooked: `MapsToCook`, `DirectoriesToAlwaysCook`, primary assets marked for cook, and everything they reference (hard or soft). `DirectoriesToNeverCook` excludes; `bCookAll` cooks all of `Content/`.
- Settings: `[/Script/UnrealEd.ProjectPackagingSettings]` in `DefaultGame.ini`.
- Run: `UnrealEditor-Cmd.exe <uproject> -run=cook -targetplatform=Windows`, or `RunUAT BuildCookRun` to build, cook and stage.
- An asset loaded only by a string path built at runtime is invisible to the cooker; reference it from data or list its directory.

## Rules and pitfalls

1. Data assets reference meshes, materials, sounds and Blueprint classes softly; load them before they are needed (match start, loading screen), never on the first shot or frame they are used.
2. Never call `LoadSynchronous()` or `LoadObject` in per-frame or per-event gameplay code. Each call can hitch for a disk read; it also flushes async loading. Resolve once, keep a hard pointer.
3. Keep the `FStreamableHandle` (or a hard `UPROPERTY` to the loaded object). When the last handle and reference go, GC unloads the asset.
4. `TSoftObjectPtr::Get()` returns null until loaded; it does not load. Do not treat null as "no asset set" — that is `IsNull()`.
5. Never hard-code `/Game/...` paths in C++ for content that may move; use a config or data-asset soft reference. If you must, use the object path with the `.Name` suffix.
6. `ConstructorHelpers` only works in constructors and makes a hard reference from the CDO; avoid it for anything large.
7. Renaming a C++ `UCLASS`, `UPROPERTY` or `UENUM` breaks saved assets that use it unless you add a `[CoreRedirects]` entry.
8. Assets under directories in `DirectoriesToNeverCook` silently go missing in packaged builds even if referenced.
9. Async load callbacks can run after the requester was destroyed; bind with `CreateUObject` / weak lambdas and check validity.
10. Do not move or rename assets with file-system tools or git; the editor must do it, or every referencer breaks. Generated content is changed in its generator script instead.
11. `FindObject` is not a load. Code that works in the editor (where the asset happens to be loaded already) can fail in a packaged build; use a soft reference and load it.
12. Asset registry queries in a packaged game see only cooked assets. Do not use the registry to discover gameplay content at runtime unless the primary asset types are set up for it.
13. Check hard-reference chains with the Reference Viewer and Size Map before adding a hard pointer to a widely used class or data asset.
14. A `TSoftObjectPtr<UWorld>` names a map: open it with `UGameplayStatics::OpenLevelBySoftObjectPtr` (or server travel), not by loading the `UWorld` object yourself.
15. Soft references inside a `UPROPERTY` are followed by the cooker; soft paths kept in plain `FString`s are not.
16. Loading in a commandlet or Python tool is synchronous and fine; the async rules apply to the running game.
17. A Blueprint class referenced softly needs `TSoftClassPtr<T>` and the `_C` class path; a `TSoftObjectPtr` to the Blueprint asset gives the `UBlueprint`, which does not exist in cooked builds.

```cpp
// Load once, ahead of time; keep the handle.
Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
    Data->Mesh.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &UMy::OnMeshLoaded));
```

## APIs, macros and commands

| Name | Use |
|---|---|
| `TSoftObjectPtr<T>`, `TSoftClassPtr<T>`, `FSoftObjectPath`, `FTopLevelAssetPath` | Soft references and paths |
| `LoadSynchronous()`, `LoadObject<T>()`, `StaticLoadObject()`, `FindObject<T>()` | Blocking load / find loaded |
| `FStreamableManager::RequestAsyncLoad()`, `RequestSyncLoad()`, `FStreamableHandle` | Streaming loads |
| `UAssetManager::Get()`, `GetStreamableManager()`, `LoadPrimaryAsset()`, `UnloadPrimaryAsset()` | Asset manager |
| `UPrimaryDataAsset`, `FPrimaryAssetId`, `FPrimaryAssetType`, `PrimaryAssetTypesToScan` | Primary assets |
| `FAssetRegistryModule`, `IAssetRegistry`, `FAssetData`, `FARFilter` | Find assets without loading |
| `UObjectRedirector`, `[CoreRedirects]` | Moved assets / renamed code types |
| `MapsToCook`, `DirectoriesToAlwaysCook`, `DirectoriesToNeverCook`, `bCookAll` | Cook settings |
| `-run=cook -targetplatform=Windows`, `RunUAT BuildCookRun` | Cook / package |
| `s.AsyncLoadingThreadEnabled` | Cvar: async loading thread |
| `obj list class=StaticMesh`, `memreport -full` | Console: what is loaded and memory |

## In World Divide

- Game data are `UPrimaryDataAsset` subclasses (`URTSEntityData`, `URTSFactionData`, `URTSMapData`, `URTSWeaponData`, ...) in `Source/WorldDivide/Data`; `URTSSettings` holds factions, maps and shared art as `TSoftObjectPtr`.
- Asset rules are binding: soft references in data, load ahead, no `LoadSynchronous` in per-frame code ([PERFORMANCE_GUIDELINES.md](../tech/PERFORMANCE_GUIDELINES.md) rules 15–16), only the listed maps are cooked (rule 32, `MapsToCook` and `DirectoriesToNeverCook` in `Config/DefaultGame.ini`, test `WorldDivide.Config.Cook`).
- Most `/Game` content is generated by `Tools/Editor/setup_testmap.py`; do not hand-edit it. Project docs win over this note on conflict.

## Sources

- https://dev.epicgames.com/documentation/en-us/unreal-engine/referencing-assets-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/core-redirects-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/cooking-content-in-unreal-engine
- Engine source: `Runtime/Engine/Classes/Engine/StreamableManager.h`, `Engine/AssetManager.h`, `Runtime/AssetRegistry/Public/AssetRegistry/IAssetRegistry.h`, `Runtime/CoreUObject/Public/UObject/SoftObjectPtr.h`
- Epic course 115.01 (UE 5.5), paraphrased
