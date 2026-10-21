# Unreal Engine 5.8 reference

Short, agent-oriented notes on the engine systems World Divide uses or will use. Read the note for a system before you change code, content or settings that depend on it. Every note ends with an **In World Divide** section that says how the project uses that system and links the project doc that governs it.

The notes describe the engine. Project decisions live in `docs/tech/`, `docs/world/`, `docs/design/` and `docs/ui/`; where they disagree with a note, the project doc wins. Every class, function, macro and console variable named here was checked against the UE 5.8 engine source; if you find one that no longer exists, fix the note.

Epic's documentation root: <https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-documentation>

## Core engine and C++

| Note | Read it when you |
|---|---|
| [core-uobject.md](core-uobject.md) | add a `UCLASS`/`USTRUCT`, hold a `UObject` pointer, worry about GC, cast, or use smart pointers |
| [core-actors-components.md](core-actors-components.md) | spawn or destroy actors, add components, tick, or add a subsystem |
| [core-gameplay-framework.md](core-gameplay-framework.md) | touch GameMode, GameState, PlayerController, PlayerState, Pawn, HUD or Enhanced Input |
| [core-modules-build.md](core-modules-build.md) | change `Build.cs`, `.uproject`, plugins, build configurations or automation tests |
| [core-assets-loading.md](core-assets-loading.md) | reference assets from code, load asynchronously, or reason about cooking |
| [core-cvars-config.md](core-cvars-config.md) | add a console variable or config setting, or override one from the command line |
| [core-blueprint-exposure.md](core-blueprint-exposure.md) | expose C++ to Blueprint, add events, delegates or a `UINTERFACE` |

## Building virtual worlds

| Note | Read it when you |
|---|---|
| [world-landscape.md](world-landscape.md) | create or change terrain, landscape materials or layers |
| [world-partition.md](world-partition.md) | weigh World Partition, One File Per Actor, data layers, HLOD or level instances |
| [world-pcg.md](world-pcg.md) | scatter forests or clutter procedurally |
| [world-foliage-instancing.md](world-foliage-instancing.md) | place many meshes: foliage, ISM/HISM, cull distances |
| [world-lighting.md](world-lighting.md) | change sun, sky, fog, Lumen, shadows or post-process |

## Performance

| Note | Read it when you |
|---|---|
| [perf-profiling.md](perf-profiling.md) | measure: Unreal Insights, `stat` commands, GPU and CSV profiling |
| [perf-scalability.md](perf-scalability.md) | add quality levels, scalability groups or device profiles |
| [perf-many-units.md](perf-many-units.md) | add anything per unit: ticking, queries, instancing, effects, animation budgets |

## Animation and asset pipeline

| Note | Read it when you |
|---|---|
| [anim-skeletal-pipeline.md](anim-skeletal-pipeline.md) | export FBX from Blender or another DCC, import meshes or animations, add sockets, or plan animation |

## Not covered yet

Niagara and UMG/Slate/CommonUI are both used heavily by the project; their notes come in a later pass. Until then see [`../ui/DESIGNER_HANDBOOK.md`](../ui/DESIGNER_HANDBOOK.md) for the HUD and the Niagara rules in [`../tech/PERFORMANCE_GUIDELINES.md`](../tech/PERFORMANCE_GUIDELINES.md).

## Sources

Distilled from Epic's UE 5.8 online documentation and Epic training courses 115.01 *Introduction to Unreal Engine for Programmers* and 107.01 *Introduction to Animation* (UE 5.5, paraphrased and updated to 5.8), checked against the UE 5.8 engine source.
