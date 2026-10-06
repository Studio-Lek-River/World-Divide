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
| Left click / left drag | Select own units (Shift adds or toggles); a click on an own building selects that building |
| Delete, or the HUD's cancel button | Cancel the own construction site under the cursor, or else the selected one, for a refund |
| Right click | Move, attack when on an enemy unit or building, or harvest when on a supply source (Shift queues) |
| X | Stop |
| 1–9 | Debug build menu: place the n-th building of your faction, if you can afford it |
| Left click / right click or Esc | While placing: build / cancel (Esc also stops PIE in the editor) |

## Layout

| Path | Contents |
|---|---|
| `Source/WorldDivide/Data` | `URTSEntityData` / `URTSUnitData` / `URTSBuildingData`: every unit and building is a data asset (mesh, health, weapon, speed or footprint, power output and draw and whether its weapon needs power, cost, build time, prerequisites, faction); `URTSHarvesterData` adds load size, gather time and search radius, and a building can take supply deliveries and spawn a unit when completed; `URTSWeaponData` holds range, damage and reload; `URTSFactionData` lists what a faction can build and whether it uses power |
| `Source/WorldDivide/Combat` | `URTSHealthComponent` (replicated health, death), `IRTSTargetable` (team, health and footprint shared by units and buildings) and the server-side `URTSWeaponComponent` (targeting, instant-hit fire) |
| `Source/WorldDivide/Commands` | `FRTSCommand`, formation layout and server-side dispatch |
| `Source/WorldDivide/Buildings` | `ARTSBuilding` (constructs over its build time, cuts the navmesh, inert until complete), the shared grid and placement rules, and the local placement ghost |
| `Source/WorldDivide/Economy` | The map-placed `ARTSSupplySource` (finite supplies, cuts the navmesh like a building) and the server-side `URTSHarvesterComponent` that runs a harvester's gather and delivery loop |
| `Source/WorldDivide/Units` | `ARTSUnit` (light pawn, no Character), its AI controller, the map-placed `ARTSUnitSpawner`, and `RTSApproach` (picks a reachable spot around a building or supply source footprint) |
| `Source/WorldDivide/Player` | Camera pawn, player controller (input, selection, orders, building placement) and HUD (health of selected units and of the hovered or selected building, a cancel button for a selected construction site, credits and power, the debug build menu with costs) |
| `Source/WorldDivide/Core` | Game mode (team and faction assignment, starting credits, team start spots), player state (team, faction, credits, the power of its completed buildings), and `URTSSettings` (default faction and ghost material, set in `Config/DefaultGame.ini`) |
| `Tools/Editor/setup_testmap.py` | Builds the placeholder unit, harvester, weapon, building and faction data assets, the placement ghost material and `L_TestMap` with a supply source by each base, saving only what differs from the script |

Every player order is an `FRTSCommand` sent through `ARTSPlayerController::Server_IssueCommand` and executed only on the server, so singleplayer already runs the multiplayer code path. Units and buildings are always relevant for now; fog of war will decide relevancy later.

Buildings snap to a 100 cm grid. The client's ghost and the server run the same placement check (no overlap with units, buildings or obstacles; footprint on the navmesh), which is why clients keep a navmesh too (`bAllowClientSideNavigation`). Until builder units exist, a `Build` order without units is placed directly by the server; the 1–9 menu is a debug stand-in for a builder's command card.

Every player starts with the game mode's `StartingCredits`; credits replicate to their owner only. Placing a building charges its `Cost`, and the server refuses what the player cannot afford, just as the client's ghost turns red. Cancelling an own construction site refunds the share still to be built, rounded down: half built returns half the cost. A construction site the enemy destroys refunds nothing. A completed supply depot brings one harvester, which loads at the nearest supply source, delivers to the nearest own depot and earns its load in credits, round after round. When its source runs dry it delivers what it carries and moves on to the nearest other source within its search radius, or goes idle.

Completed buildings add their `PowerOutput` to and take their `PowerDraw` from their owner's grid; construction sites count for nothing. While the draw exceeds the output the player is low on power: all production runs a third slower (construction now, units and upgrades once they exist) and buildings flagged `bRequiresPower`, such as the test tower, hold fire. A faction with `bUsesPower` off is never low on power. Power, like credits, replicates to its owner only, and a team without a player is always fully powered.

## Tests

`Tools/Scripts/ue.ps1 -Task build|test|setup` wraps the commands below and exits nonzero on failure. It uses `UE_ROOT` as the engine folder, falling back to `G:\Program Files\Epic Games\UE_5.6`.

```
"<UE_5.6>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<repo>\WorldDivide.uproject" -ExecCmds="Automation RunTests WorldDivide;Quit" -unattended -nullrhi
```

`WorldDivide.Smoke.TestMap` plays `L_TestMap` in PIE and checks that a move order brings all 16 own units around the wall to the target while enemy units ignore it. `WorldDivide.Smoke.AttackOrder` orders a tank to attack an enemy out of range and checks it closes in and destroys it. `WorldDivide.Combat.*` cover health and an unordered duel between two idle units. `WorldDivide.Buildings.*` cover grid snapping, construction over the build time, a finished tower killing enemies in range, an idle unit destroying an enemy construction site whose edge is in range, and weapons preferring enemy units over buildings. `WorldDivide.Economy.*` cover credits, supply sources running dry, a completed depot spawning its harvester, and a full harvest run docked between a depot and a source: steady deliveries, the last partial load, moving on to the next source and stopping when none is in reach. `WorldDivide.Power.*` cover the efficiency calculation and the grid's totals, a powered tower going dark and construction slowing when the only plant falls and both recovering once a new plant completes, and a faction without power being unaffected. `WorldDivide.Economy.ConstructionRefund` covers the refund for cancelling a site, refused cancels of another team's site or a completed building, and no refund for a destroyed site. `WorldDivide.Smoke.BuildingPlacement` gives the player a power plant and checks that client and server accept an open spot and reject spots on buildings, units, walls and off the navmesh, plus buildings outside the faction and buildings the player cannot afford, that placing charges the cost, and that the building completes after its build time and draws from the grid; it then selects and cancels a half-built site for half its cost and checks that enemy buildings cannot be selected or cancelled. `WorldDivide.Smoke.Harvest` buys a supply depot beside a source holding three loads and checks its harvester delivers each load in steady cycles, then goes idle. `WorldDivide.Smoke.RouteAroundBuilding` places a building across a tank's path and checks the tank walks around it. `WorldDivide.Smoke.AttackBuilding` orders three tanks to attack an enemy power plant and checks they destroy it and its hole in the navmesh closes. `WorldDivide.Smoke.SolidObstacles` checks that no navmesh lies inside or on top of the map's walls and blocks, while the ground beside them stays walkable. `WorldDivide.Smoke.MoveIntoObstacle` sends three tanks onto the roof of a block and checks they stop at its edge on their own side instead of dropping the order. `WorldDivide.Smoke.ListenServer` plays `L_TestMap` as a listen server with one client, checks both players' teams and start spots and that each sees only its own credits, and sends a move for every unit from the client's controller: the server moves only the client's units and the client sees them arrive. The client buys a construction site and cancels it: the site disappears on both sides and the client sees its refund while the host's credits stay put. The client then orders its units to attack a host building and must see its health drop before it disappears.

Idle units fire at the nearest enemy in range on their own, enemy units before enemy buildings; units on a move order hold fire. A move into a wall or a building ends at its edge on the unit's side. Weapon range is measured to the edge of a target's footprint.

To rebuild the test content after changing `setup_testmap.py` or a data asset class, run `ue.ps1 -Task setup` (after `-Task build`), or directly:

```
"<UE_5.6>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<repo>\WorldDivide.uproject" -run=pythonscript -script="<repo>\Tools\Editor\setup_testmap.py" -unattended -nullrhi
```

Setup only saves assets that differ from the script: data assets are compared property by property, and the ghost material and `L_TestMap` store a hash of the code that builds them and are rebuilt only when that code changes. A run without script changes writes nothing and leaves `git status` clean.

Because the assets are LFS lockable, Git LFS checks them out read-only. If setup has to rewrite one, it writes nothing and `ue.ps1` exits 3 listing the files. Then:

1. `git lfs lock <file>` for each listed file
2. rerun setup, commit and push
3. `git lfs unlock <file>`
