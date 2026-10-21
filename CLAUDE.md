# World Divide — instructions for Claude

World Divide is a UE 5.8 multiplayer RTS: C++ gameplay, content generated from text files by `Tools/Editor/setup_testmap.py`. Start with [`README.md`](README.md); the issue workflow is in `.claude/skills/fix-issue/SKILL.md`.

## Shared building production and intake

For building assets, visual revisions or integration of an accepted building, read [the shared building production workflow](.agents/skills/world-divide-building-production/SKILL.md) and the linked HQ lessons relevant to the task. This repo-owned skill is the single source for both Codex and Claude; do not copy it into a separate Claude workflow.

Read the building's current brief/checklist/handoff and final implementation prompt before changing its assets or integration. Codex owns candidate art and isolated previews; Claude owns authoritative setup/import recipes and production gameplay integration. Preserve recorded producer approvals and distinguish visual acceptance from runtime verification. Follow verified final manifests and dependency mappings rather than choosing the highest candidate folder or rerunning historical generators. Keep integration results and the next action in the same repo handoff.

## Engine reference

[`docs/engine/README.md`](docs/engine/README.md) is the shared Unreal Engine 5.8 reference: reflection and GC, actors and ticking, modules and builds, assets, config, landscape, World Partition, PCG, foliage, lighting, profiling, scalability, large unit counts and the skeletal/FBX pipeline. Read the note for a system before you change code that uses it.

The notes describe the engine. Project decisions live in `docs/tech/`, `docs/world/`, `docs/design/` and `docs/ui/`; where they disagree with a note, the project doc wins.
