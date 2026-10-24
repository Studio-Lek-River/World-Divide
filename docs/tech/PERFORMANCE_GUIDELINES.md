# Performance guidelines

Rules for every change to World Divide's code, config and content. Each rule says what is allowed and names the automation test that holds it. A rule whose test is marked *planned* is not enforced yet; the issue that adds the test is listed in [`PERFORMANCE_AUDIT.md`](PERFORMANCE_AUDIT.md). Where a rule must be broken, say why in the code and add the exception to the test's list, so it stays visible.

Measure before and after any change that claims a gain: `ue.ps1 -Task bench` on L_Bench, recorded in [`scale/PERFORMANCE_RESULTS.md`](scale/PERFORMANCE_RESULTS.md). Tests check counts, flags and settings, never milliseconds; times belong in the bench.

## Ticking

1. **No tick unless the work is per frame.** Actors and components start with `bCanEverTick = false`. Work that happens on an event uses the event; work that repeats slowly uses a timer or `SetTickInterval`.
2. **Tick only where the work runs.** Server-only logic switches its tick off on clients; a component without its data (a weapon component without `WeaponData`) switches it off until it gets it.
3. **Idle is cheap.** An idle unit with no order and no target ticks at an interval, not every frame, and goes back to every frame on an order.
4. **Tickable subsystems say when they are idle** through `IsTickable`.
5. **One-off set-up ticks switch themselves off** once done (the HUD creating its widget).

Test: `WorldDivide.Perf.TickPolicy` (*planned*).

## World queries

6. **No `TActorIterator` or `GetAllActorsOfClass` in per-frame code.** Use the entity registry (units and buildings per team, counts per data), the avoidance grid or `QueryRadius`. Scans are allowed in tests, debug tools, set-up and rare events.
7. **Caches are keyed on a version, not on the frame.** A cache rebuilds when what it depends on changed.
8. **The AI gathers one census per think**, and AI players think in different frames.

Tests: `WorldDivide.Perf.EntityRegistry`, `WorldDivide.Perf.CommandPanelRebuilds`, `WorldDivide.AI.ThinkStagger` (*planned*).

## Spawning and memory

9. **Nothing is spawned and destroyed per shot.** Effects, lights and sounds that come and go with combat are pooled, or made once per key.
10. **No `UMaterialInstanceDynamic` per instance for colour or state.** Use custom primitive data on the master material, as team colour does.
11. **Short-lived lights have a budget** (`rts.Fx.MaxShotLights`), cast no shadows and add no indirect light.
12. **No temporaries in per-frame code.** Arrays and sets used every frame are members, `Reset()` each frame.

Tests: `WorldDivide.Perf.ShotPool`, `WorldDivide.Art.TeamColorMaterials`.

## UI

13. **Set a widget's text, tooltip or brush only when its value changed.**
14. **No string building per binding per frame.** A binding key's kind is worked out once, when the layout is built.

Tests: `WorldDivide.Perf.LayoutUpdates`, `WorldDivide.Perf.ResourceBar` (*planned*).

## Assets and loading

15. **Data assets hold meshes, materials and sounds by soft reference**, and the match loads them in advance. A menu or the lobby loads no unit or building art.
16. **No `LoadSynchronous` in per-frame or per-shot code.** Resolve once and keep the pointer.
17. **Gameplay values live in data assets or `URTSSettings`**, not in C++ constants. Mechanics constants (reach margins, retry delays) may stay in code.
18. **Groups of entities are named by Gameplay Tags**, not by hand-kept lists or extra bools.

Tests: `WorldDivide.Perf.MenuMemory`, `WorldDivide.Upgrades.ByTag` (*planned*).

## Meshes and materials

19. **Small moving game meshes and building shells do not use Nanite**; buildings get LOD0 + LOD1 with one switch distance per building (#349). Environment meshes may, once the environment is real.
20. **A game mesh above 2,000 triangles has LODs** with decreasing screen sizes.
21. **Every game material is an instance of a master material.** Translucency only on the list in the test (the placement ghost, shot glows, the kit lamps' beams and glare halos, `M_Kit_Glow`: additive, night only, #440).
22. **Small parts cast no shadow.** Only the hull and the large parts of a unit do.
23. **Repeated static pieces are instances** (one ISM per wall), not separate actors.

Tests: `WorldDivide.Art.PlaceholderWithoutNanite`, `WorldDivide.Art.M20GuardianLODs`, `WorldDivide.Art.TeamColorMaterials`; `WorldDivide.Art.MeshBudget`, `WorldDivide.Maps.Instancing` (*planned*).

## Collision and movement

24. **Overlap events are off** on units, buildings and effects unless a gameplay rule needs them.
25. **No complex collision** on units, buildings or effects.
26. **A unit moves once per tick**; corrections fold into that move. The physics-volume update is off.

Tests: `WorldDivide.Units.Footprint` (unit overlap events); `WorldDivide.Perf.TickPolicy` (*planned*, the rest).

## Replication

27. **Buildings go dormant once complete** and flush on a change.
28. **Units replicate at an adaptive rate** with a minimum rate set; values that change rarely use the push model.

Test: `WorldDivide.Perf.Replication` (*planned*).

## Rendering and config

29. **Rendering features are chosen in config, not left at engine defaults**: GI, reflections, shadows, anti-aliasing, distance fields, and the shader formats compiled.
30. **`Config/DefaultScalability.ini` is tuned for the RTS camera.** Every quality level is playable; Low meets the minimum-spec target, High is the look.
31. **Every map has one unbound post-process volume** with manual exposure and no motion blur.
32. **Only the project's maps are cooked**; marketplace packs under `Content/` are never cooked unless the game references them.

Tests: `WorldDivide.Config.Cook`; `WorldDivide.Config.Renderer`, `WorldDivide.Maps.PostProcess` (*planned*).
