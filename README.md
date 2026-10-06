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

## Layout

| Path | Contents |
|---|---|
| `Source/WorldDivide/Data` | `URTSEntityData` / `URTSUnitData`: every unit is a data asset (mesh, health, weapon, speed, cost, build time, prerequisites, faction); `URTSWeaponData` holds range, damage and reload |
| `Source/WorldDivide/Combat` | `URTSHealthComponent` (replicated health, death) and the server-side `URTSWeaponComponent` (targeting, instant-hit fire) |
| `Source/WorldDivide/Commands` | `FRTSCommand`, formation layout and server-side dispatch |
| `Source/WorldDivide/Units` | `ARTSUnit` (light pawn, no Character), its AI controller, and the map-placed `ARTSUnitSpawner` |
| `Source/WorldDivide/Player` | Camera pawn, player controller (input, selection, orders) and HUD |
| `Source/WorldDivide/Core` | Game mode (team assignment, team start spots) and player state |
| `Tools/Editor/setup_testmap.py` | Rebuilds the placeholder unit and weapon data assets and `L_TestMap` |

Every player order is an `FRTSCommand` sent through `ARTSPlayerController::Server_IssueCommand` and executed only on the server, so singleplayer already runs the multiplayer code path. Units are always relevant for now; fog of war will decide relevancy later.

## Tests

`Tools/Scripts/ue.ps1 -Task build|test|setup` wraps the commands below and exits nonzero on failure. It uses `UE_ROOT` as the engine folder, falling back to `G:\Program Files\Epic Games\UE_5.6`.

```
"<UE_5.6>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<repo>\WorldDivide.uproject" -ExecCmds="Automation RunTests WorldDivide;Quit" -unattended -nullrhi
```

`WorldDivide.Smoke.TestMap` plays `L_TestMap` in PIE and checks that a move order brings all 16 own units around the wall to the target while enemy units ignore it. `WorldDivide.Smoke.AttackOrder` orders a tank to attack an enemy out of range and checks it closes in and destroys it. `WorldDivide.Combat.*` cover health and an unordered duel between two idle units.

Idle units fire at the nearest enemy in range on their own; units on a move order hold fire.

To rebuild the test content:

```
"<UE_5.6>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<repo>\WorldDivide.uproject" -run=pythonscript -script="<repo>\Tools\Editor\setup_testmap.py" -unattended -nullrhi
```
