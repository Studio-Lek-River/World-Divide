# Performance and simplicity audit

Read-only audit of `main` at `0ba0dc1` (2026-10-18), against common Unreal practice for an RTS: little ticking, no per-frame world scans, pooled effects, data in assets, explicit rendering choices. The work already settled in [`scale/`](scale/) (acquisition grid, avoidance cells, idle grounding, formations, path search, team-colour materials, M20 LODs) is left out. The rules that follow from this audit are in [`PERFORMANCE_GUIDELINES.md`](PERFORMANCE_GUIDELINES.md).

Gains are estimates until a `ue.ps1 -Task bench` run on L_Bench shows them. The RTX 3060-class target (1080p / 60 FPS, 200 units) is still unmeasured ([`scale/PERFORMANCE_RESULTS.md`](scale/PERFORMANCE_RESULTS.md)), and the GPU at 240 M20s takes 4.3 ms on the RTX 4090 there; on a 3060 that could come close to the 16.7 ms frame.

## Summary by gain

| # | Finding | Area | Gain | Issue |
|---|---|---|---|---|
| 1 | No cook rules: every marketplace demo map and pack under `Content/` is cooked | Content | High (size, cook time) | #226 |
| 2 | One actor, two point lights, three MIDs and two procedural sound waves per shot, spawned and destroyed | CPU / GPU | High in battles | #227 |
| 3 | Command buttons are rebuilt every frame, and each rebuild walks every unit and building | CPU (client) | Medium–high | #229 |
| 4 | Rendering features (GI, reflections, shadows, AA, distance fields) are never chosen; no scalability file | GPU | Medium–high, to measure | #233 |
| 5 | No registry of units and buildings per team: 52 `TActorIterator` loops outside tests, 26 in the AI | CPU / simplicity | Medium | #228 |
| 6 | Weapon components tick on clients and on unarmed owners; HUD ticks forever; idle units tick at full rate | CPU | Medium | #231 |
| 7 | Layout bindings allocate strings for every key every frame; text is set every frame whether it changed or not | CPU (client) | Medium | #232 |
| 8 | AI thinks scan the world per build-order entry, and every AI thinks in the same frame | CPU (server) | Medium | #230 |
| 9 | Replication at a fixed rate with no dormancy | CPU (server) | Medium | #235 |
| 10 | Hard references from faction data to every mesh and sound; `FindByTag` loads every faction for every icon | Memory / load time | Medium | #236 |
| 11 | No post-process volume: auto exposure and motion blur at engine defaults | Visuals / GPU | Low–medium | #234 |
| 12 | Hand-kept lists and bools where Gameplay Tags fit (upgrade targets, `bIsDefense`) | Simplicity | Medium | #237 |
| 13 | `ARTSUnit` (2.4k lines) and `ARTSPlayerController` (2.1k lines) do many jobs | Simplicity | Medium | #239 |
| 14 | Small items: D3D11 shader format, unused Nanite, building overlap events, shadows on small parts, walls as separate actors, hot-path temporaries | Mixed | Low | #231, #233, #238 |

## 1. Content: cook rules

- There is no `[/Script/UnrealEd.ProjectPackagingSettings]` in `Config/`: no `MapsToCook`, no `DirectoriesToNeverCook`.
- With no maps listed, a cook takes every map under `/Game`, the demo maps of the marketplace packs among them: `Deko_MatrixDemo` (4.0 GB), `Megaplant_Library` (2.9 GB), `StylizedBridge` (1.2 GB), `Real_Stones_Pack_01`, `ProceduralBuildingGenerator`, `HighPoly_Tree_Model`, `UltimateUIMenusSFX`.
- No project asset, source, tool or config file references any of them. They are git-ignored (#153), so a cook also differs from one machine to the next.
- **Fix:** `+MapsToCook` for the maps in `Content/Maps`, `+DirectoriesToNeverCook` for each pack.
- **Guard:** a test that walks the asset registry dependencies of every map to cook and of `URTSSettings`, and fails on a dependency under a pack root or a pack root missing from the never-cook list.

## 2. Shot effects

- `Combat/RTSShotEffect.cpp:99` spawns an `ARTSShotEffect` per shot: three glow meshes and two point lights (`:50`, `:53`). It is destroyed at `:200`.
- `:124` creates a `UMaterialInstanceDynamic` per glow, three per shot, and sets their colour every tick. Each is its own render proxy, so shot draws never merge.
- `:117-118` resolves the mesh and material with `LoadSynchronous` on every shot.
- `:191` calls `GetActorBounds` on the target every tick, which walks all of its parts.
- `Combat/RTSShotSound.cpp:91-99` creates two `USoundWaveProcedural` per shot, each copying its PCM.
- The lights cast no shadows, but they light translucency and feed Lumen, and their radius is 450 cm × `EffectScale`.
- At 240 against 240 with 2 s reloads that is in the order of 120–240 actors, 360–720 MIDs, 240–480 lights and 240–480 sound objects a second.
- **Fix:** a pool in `URTSShotSubsystem` (or a Niagara system per weapon with auto-release pooling); colour through custom primitive data in `M_ShotFx`, as #150 did for team colour; the mesh and material resolved once; a global budget on shot lights (`rts.Fx.MaxShotLights`) with indirect lighting off; one procedural wave per sound key.
- **Guard:** fire N shots in a test world and assert the live effect actors never exceed the pool size, no glow carries a MID, the live lights stay within the budget, and the procedural waves grow by no more than the number of sound keys.

## 3. Command panel rebuilt every frame

- `UI/Layout/RTSUIBinding.cpp:950` rebuilds when the key changes **or** `CachedFrame != GFrameCounter`, so the cache never outlives a frame.
- Every frame `ARTSPlayerController::GetCommandButtons` (`Player/RTSPlayerController.cpp:988`) therefore runs `RTSProduction::GatherOwned` (`Buildings/RTSProductionComponent.cpp:38-62`), a scan of every building and every unit, plus an `IsAtBuildLimit` and `IsUpgradeQueued` scan per button (`:69`, `:84-99`).
- It runs once per panel that shows command buttons, on every client.
- **Fix:** drop the frame check and key the cache on a version that the registry (finding 5), credits, selection, queue and upgrades bump.
- **Guard:** a rebuild counter; with nothing changing over 30 ticks there is exactly one rebuild, and a credit change adds one.

## 4. Rendering choices and scalability

- `Config/DefaultEngine.ini` sets only `r.AllowStaticLighting=False` under `RendererSettings`, so the UE5 defaults apply: Lumen GI and reflections, Virtual Shadow Maps, TSR, mesh distance fields for every mesh. The sky light captures in real time (`Tools/Editor/setup_testmap.py:791`).
- There is no `Config/DefaultScalability.ini`, so the quality levels are the engine's, not tuned for a fixed-pitch camera at 170 m.
- **Fix:** choose explicitly after a bench comparison. Candidates: Lumen against SSGI or none (`r.DynamicGlobalIlluminationMethod`), SSR (`r.ReflectionMethod=2`), VSM against cascaded shadows, `r.GenerateMeshDistanceFields=False` if Lumen goes, TSR with a screen percentage per quality level, shadow distance per `sg.ShadowQuality`.
- **Guard:** a config test that asserts the chosen cvar values and that the scalability file maps each quality level as the guidelines say.

## 5. No registry of units and buildings

- Five world subsystems exist (shots, move requests, avoidance, running gear, UI style), but none owns "every unit and building per team". `ARTSGameState` keeps `BuildingsByTeam` for the weapon search only.
- 52 `TActorIterator` loops remain outside tests and debug code, 26 of them in `AI/RTSAIPlayer.cpp`.
- **Hot ones:**
  - the command panel (finding 3);
  - `Player/RTSHUD.cpp:122` and `:378`, two building scans in every `DrawHUD`;
  - `Buildings/RTSBuildPlacement.cpp:238`, a unit scan every frame while placing;
  - the AI (finding 8).
- **Fix:** a `URTSEntityRegistrySubsystem` with per-team unit and building lists and per-data counts, kept on register and unregister, with a version number for caches.
- **Guard:** a test that the counts match a `TActorIterator` count after spawns, deaths and team changes; a scan counter in the hot paths above that stays at zero.

## 6. Ticking

- **`URTSWeaponComponent`** (`Combat/RTSWeaponComponent.cpp:18`, `:292`) is on every unit and building, harvesters and builders included. It ticks on every machine and returns early on clients and without weapon data.
  - **Fix:** tick only with authority and weapon data, toggled in `SetWeaponData` and `RefreshWeapon`.
- **`ARTSHUD`** (`Player/RTSHUD.cpp:64-72`) ticks forever, only to create its widget once.
  - **Fix:** switch the tick off once the widget exists.
- **Idle units** run the full `ARTSUnit::Tick`. The optional idle tick interval of `scale/RTS_MOVEMENT_PERFORMANCE.md` §3.3 was never built; there is no `SetTickInterval` anywhere in `Source/`.
  - One tick can also move a unit several times: sweep, ground snap (`Units/RTSUnit.cpp:1052`), avoidance push (`Units/RTSAvoidanceSubsystem.cpp:407`), facing (`:1174`).
  - `bComponentShouldUpdatePhysicsVolume` is left at true.
  - **Fix:** an idle tick interval, the snap folded into the move, and the physics-volume update off.
- **`URTSShotSubsystem`** has no `IsTickable`, unlike `URTSMoveRequestSubsystem` (`:208`).
- **Buildings** tick to poll construction, production and charge even when none is pending.
- **Guard:** a tick-policy test over the CDOs and spawned instances: weapon tick off on clients and when unarmed, HUD tick off after set-up, idle unit tick interval above zero and back to zero on an order, physics-volume update off.

## 7. UI bindings

- `UI/Layout/RTSLayoutWidget.cpp:746`: `GetValue` runs `Key.ToString()` and `StartsWith` on every lookup, for every binding, every frame; `IsShown` (`:787`) does it again.
- `:896`: `FormatText` builds a string and compares it with `GetText().ToString()`.
- `:955-967`: tooltips are concatenated every frame for every list item.
- `UI/RTSResourceBarWidget.cpp:96`, `:104-105`: `SetText` twice a frame, changed or not, so Slate re-lays the text every frame.
- `UI/RTSHUDWidget.cpp:238`: `SyncModals` builds a `TArray<FString>` every frame.
- **Fix:** work out each key's kind once at `Build`, keep the last value per element and call `SetText` and `SetToolTipText` only when it changes.
- **Guard:** a test that counts `SetText` and `SetToolTipText` calls; 10 ticks with unchanged sources after the first frame give zero.

## 8. AI

- `AI/RTSAIPlayer.cpp:386-405`: `CountOwned` scans every unit and building once per build-order entry; about 25 scans a think in total, plus a supply-source scan per idle harvester (`:772`).
- Every AI starts its 1 s think timer at `BeginPlay` (`:288`), so in an eight-player match all of them think in the same frame.
- AI tuning values are compile-time constants (`AI/RTSAIPlayer.h:91-133`, `RTSAIPlayer.cpp:33-39`), not data.
- **Fix:** one per-team census per think from the registry; first timer delay `ThinkInterval × index / NumAI`; tuning into `URTSAIBuildOrderData`.
- **Guard:** two AIs' remaining think times differ by at least a quarter interval; one census per think.

## 9. Replication

- Units replicate at 30 Hz, always relevant (`Units/RTSUnit.cpp:214-216`), with no minimum rate and no push model. `bAlwaysRelevant` stays until fog of war.
- Buildings replicate at 10 Hz (`Buildings/RTSBuilding.cpp:79`), never dormant, though they rarely change.
- No Iris or Replication Graph setting.
- **Fix:** buildings dormant once complete, with a flush on health, queue, progress or charge changes; units with a minimum and adaptive rate; push model on turret, gun, ammo and health.
- **Guard:** CDO checks for dormancy and minimum rate; the listen-server test checks a client sees a building's change after a flush.

## 10. Asset references and loads

- `URTSFactionData` (`Data/RTSFactionData.h:28`, `:35`) holds its buildings by hard reference. Through `URTSBuildingData` and `URTSUnitData` (`Mesh`, `RigidMaterial`, `TurretMesh`, `GunMesh`) and `URTSWeaponData` (`FireSound`, `ImpactSound`), loading a faction loads every mesh and sound in it.
- `URTSFactionData::FindByTag` (`Data/RTSFactionData.cpp:12-16`) runs `LoadSynchronous` on every faction until one matches. `RTSUI::GetEntityIcon` (`UI/RTSUI.cpp:33`) calls it for every icon, every frame.
- Other runtime loads:
  - `Player/RTSPlayerController.cpp:1223` when placement begins;
  - `Menu/SRTSLobbyWidget.cpp:81` in a widget getter.
- **Fix:** cache tag → faction in a subsystem; soft references for meshes and sounds with an async preload when the match starts; factions as primary assets.
- **Guard:** after the main menu loads, no unit or building mesh is in memory; `FindByTag` loads at most one faction.

## 11. Post-process

- No map has a `PostProcessVolume`, fog or exposure setting; `spawn_lighting` (`Tools/Editor/setup_testmap.py:786-792`) makes only sun, sky light and atmosphere.
- So auto exposure shifts brightness with zoom and terrain, motion blur blurs camera pans, and bench screenshots are not comparable.
- **Fix:** an unbound volume from `spawn_lighting` with manual exposure and no motion blur, plus `r.DefaultFeature.AutoExposure=False` and `r.DefaultFeature.MotionBlur=False`.
- **Guard:** every map in `Content/Maps` has exactly one unbound volume with manual exposure and motion blur 0.

## 12. Data and tags

- `Data/RTSUpgradeData.h:30` `Targets`: every new unit has to be added to every upgrade by hand. A tag query on entity tags (`Unit.Class.Vehicle`) does it once.
- `Data/RTSBuildingData.h:69` `bIsDefense` has two meanings: a defence, and whether the building keeps its team in the match (`Core/RTSGameState.cpp:14`).
- Balance values in code: `Core/RTSPlayerState.h:57` `LowPowerProductionRate`, `Buildings/RTSProductionComponent.h:57` `MaxQueue`.
- Alert cooldowns in code: `Player/RTSPlayerController.cpp:39-45`.
- **Guard:** an upgrade by tag applies to a unit added after it; the existing upgrade, production and AI tests cover the rest.

## 13. Large classes

| Class | Lines (cpp + h) | Split |
|---|---|---|
| `ARTSUnit` | 1767 + 596 | Visuals and running gear, turret and barrel, order queue into components |
| `ARTSPlayerController` | 1554 + 562 | Camera input to the pawn; selection, placement, command panel model and alerts into components |
| `ARTSAIPlayer` | 1466 + 374 | Economy, build order, defence and waves modules |

This changes no behaviour; the existing selection, command, turret, barrel, vehicle, locomotion, building, net and AI suites cover the refactor.

## 14. Small items

- `Config/DefaultEngine.ini:42-43` compiles every shader for D3D11 SM5 as well as D3D12 SM6. Drop it if DX11 is not a supported fallback.
- Nanite is off on every game mesh, which is correct for these small moving parts and guarded by `WorldDivide.Art.PlaceholderWithoutNanite`. The project still has Nanite enabled; switch it off unless the environment will use it.
- Building collision leaves overlap events on (`Buildings/RTSBuilding.cpp:61`).
- Unit parts all cast shadows (`Units/RTSUnit.cpp:144-158`); with VSM every moving part invalidates its shadow pages. Small parts can drop shadows.
- Walls are one `StaticMeshActor` per piece (`Tools/Editor/setup_testmap.py:733-750`), so each has its own proxy and occlusion query; one ISM per wall is cheaper.
- Placeholder meshes have one LOD and no test stops a heavy replacement without LODs.
- Per-frame temporaries: `Player/RTSPlayerController.cpp:331`, `Units/RTSRunningGearSubsystem.cpp:93`, `Navigation/RTSMoveRequestSubsystem.cpp:166`.
- The camera pawn traces every frame even when it did not move (`Player/RTSCameraPawn.cpp:134`).
- `scale/RTS_MOVEMENT_PERFORMANCE.md` §1 and §3.1-3.2 still describe the scan that step 2 replaced; it is now marked superseded.

## Not applicable yet

There are no skeletal meshes, Niagara systems or decals, so URO, the Animation Budget Allocator and Niagara budgets have nothing to act on. The Significance Manager becomes worth it with fog of war or animated infantry. Mass Entity is not needed at the 200–300 unit target.
