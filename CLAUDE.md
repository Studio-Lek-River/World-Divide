# World Divide — instructions for Claude

World Divide is a UE 5.8 multiplayer RTS: C++ gameplay, content generated from text files by `Tools/Editor/setup_testmap.py`. Start with [`README.md`](README.md); the issue workflow is in `.claude/skills/fix-issue/SKILL.md`.

## Engine reference

[`docs/engine/README.md`](docs/engine/README.md) is the shared Unreal Engine 5.8 reference: reflection and GC, actors and ticking, modules and builds, assets, config, landscape, World Partition, PCG, foliage, lighting, profiling, scalability, large unit counts and the skeletal/FBX pipeline. Read the note for a system before you change code that uses it.

The notes describe the engine. Project decisions live in `docs/tech/`, `docs/world/`, `docs/design/` and `docs/ui/`; where they disagree with a note, the project doc wins.
