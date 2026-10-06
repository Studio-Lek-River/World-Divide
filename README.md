# World Divide

A real-time strategy game in the spirit of C&C Generals Zero Hour, built in Unreal Engine 5.6. Gameplay logic is C++; content and UI go in Blueprint and data assets. Nothing is taken from Generals itself: no names, art, audio, UI or code.

## Requirements

- Unreal Engine 5.6
- Visual Studio 2022 with the "Game development with C++" workload
- Git LFS (`git lfs install` once per machine) — all `.uasset`/`.umap` files live in LFS and are lockable

## Getting started

1. Clone, then right-click `WorldDivide.uproject` → *Generate Visual Studio project files*, or just open the `.uproject` and let it build.
2. The editor opens `L_TestMap`. Press **Play**.

Command-line build:

```
"<UE_5.6>\Engine\Build\BatchFiles\Build.bat" WorldDivideEditor Win64 Development -Project="<repo>\WorldDivide.uproject" -WaitMutex
```

## Controls

| Input | Action |
|---|---|
| WASD / arrow keys / screen edge | Pan camera |
| Mouse wheel | Zoom |
| Q / E, or drag with middle mouse | Rotate camera |
| Left click / left drag | Select own units (Shift adds or toggles) |
| Right click | Move, or attack when on an enemy unit (Shift queues) |
| X | Stop |
| 1–9 | Debug build menu: place the n-th building of your faction |
| Left click / right click or Esc | While placing: build / cancel (Esc also stops PIE in the editor) |

## Layout

| Path | Contents |
|---|---|
| `Source/WorldDivide/Data` | `URTSEntityData` / `URTSUnitData` / `URTSBuildingData`: every unit and building is a data asset (mesh, health, weapon, speed or footprint and power, cost, build time, prerequisites, faction); `URTSWeaponData` holds range, damage and reload; `URTSFactionData` lists what a faction can build |
| `Source/WorldDivide/Combat` | `URTSHealthComponent` (replicated health, death) and the server-side `URTSWeaponComponent` (targeting, instant-hit fire) |
| `Source/WorldDivide/Commands` | `FRTSCommand`, formation layout and server-side dispatch |
| `Source/WorldDivide/Buildings` | `ARTSBuilding` (constructs over its build time, cuts the navmesh, inert until complete), the shared grid and placement rules, and the local placement ghost |
| `Source/WorldDivide/Units` | `ARTSUnit` (light pawn, no Character), its AI controller, and the map-placed `ARTSUnitSpawner` |
| `Source/WorldDivide/Player` | Camera pawn, player controller (input, selection, orders, building placement) and HUD (including the debug build menu) |
| `Source/WorldDivide/Core` | Game mode (team and faction assignment, team start spots), player state, and `URTSSettings` (default faction and ghost material, set in `Config/DefaultGame.ini`) |
| `Tools/Editor/setup_testmap.py` | Builds the placeholder unit, weapon, building and faction data assets, the placement ghost material and `L_TestMap`, saving only what differs from the script |

Every player order is an `FRTSCommand` sent through `ARTSPlayerController::Server_IssueCommand` and executed only on the server, so singleplayer already runs the multiplayer code path. Units and buildings are always relevant for now; fog of war will decide relevancy later.

Buildings snap to a 100 cm grid. The client's ghost and the server run the same placement check (no overlap with units, buildings or obstacles; footprint on the navmesh), which is why clients keep a navmesh too (`bAllowClientSideNavigation`). Until builder units exist, a `Build` order without units is placed directly by the server; the 1–9 menu is a debug stand-in for a builder's command card.

## Tests

`Tools/Scripts/ue.ps1 -Task build|test|setup` wraps the commands below and exits nonzero on failure. It uses `UE_ROOT` as the engine folder, falling back to `G:\Program Files\Epic Games\UE_5.6`.

```
"<UE_5.6>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<repo>\WorldDivide.uproject" -ExecCmds="Automation RunTests WorldDivide;Quit" -unattended -nullrhi
```

`WorldDivide.Smoke.TestMap` plays `L_TestMap` in PIE and checks that a move order brings all 16 own units around the wall to the target while enemy units ignore it. `WorldDivide.Smoke.AttackOrder` orders a tank to attack an enemy out of range and checks it closes in and destroys it. `WorldDivide.Combat.*` cover health and an unordered duel between two idle units. `WorldDivide.Buildings.*` cover grid snapping, construction over the build time and a finished tower killing enemies in range. `WorldDivide.Smoke.BuildingPlacement` checks that client and server accept an open spot and reject spots on buildings, units, walls and off the navmesh, plus buildings outside the faction, and that the building completes after its build time. `WorldDivide.Smoke.RouteAroundBuilding` places a building across a tank's path and checks the tank walks around it. `WorldDivide.Smoke.ListenServer` plays `L_TestMap` as a listen server with one client, checks both players' teams and start spots, and sends a move for every unit from the client's controller: the server moves only the client's units and the client sees them arrive.

Idle units fire at the nearest enemy in range on their own; units on a move order hold fire.

To rebuild the test content after changing `setup_testmap.py` or a data asset class, run `ue.ps1 -Task setup` (after `-Task build`), or directly:

```
"<UE_5.6>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<repo>\WorldDivide.uproject" -run=pythonscript -script="<repo>\Tools\Editor\setup_testmap.py" -unattended -nullrhi
```

Setup only saves assets that differ from the script: data assets are compared property by property, and the ghost material and `L_TestMap` store a hash of the code that builds them and are rebuilt only when that code changes. A run without script changes writes nothing and leaves `git status` clean.

Because the assets are LFS lockable, Git LFS checks them out read-only. If setup has to rewrite one, it writes nothing and `ue.ps1` exits 3 listing the files. Then:

1. `git lfs lock <file>` for each listed file
2. rerun setup, commit and push
3. `git lfs unlock <file>`
