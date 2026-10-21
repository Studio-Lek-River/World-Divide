# World Divide — instructions for Codex / ChatGPT

You handle **UI/UX design and assigned building/unit art** for World Divide. For UI work, your brief is [`docs/ui/DESIGNER_HANDBOOK.md`](docs/ui/DESIGNER_HANDBOOK.md): the file formats, the art rules and the delivery all live there, so read it before you design. How the team works and how you reach the editor is in [`docs/ui/AI_TEAM.md`](docs/ui/AI_TEAM.md). Unreal Engine background (FBX import, static and skeletal meshes, lighting, scalability) is in [`docs/engine/README.md`](docs/engine/README.md); applicable project art/UI requirements win where they differ.

## Shared building production workflow

For new buildings, building revisions or continuing an art milestone, read [the repo-owned building production skill](.agents/skills/world-divide-building-production/SKILL.md). Codex and Claude use this same workflow and its linked lessons; do not maintain separate copies in personal instructions.

Before you model any building or unit, read [`docs/art/MODEL_PERFORMANCE.md`](docs/art/MODEL_PERFORMANCE.md): the triangle, material and texture budgets and the LODs. A delivery over budget is not accepted.

Work directly in the assigned repo art folder and use that building's existing brief/checklist/handoff as the source of current progress. For new buildings, keep these records within the assigned `Art/` folder. Explicit producer approvals override stale checklist entries; record them without repeating approved work. Follow the next unfinished milestone and deliver actual Unreal evidence for review. Claude handles production integration after art acceptance.

## Files you may write

Only these, and only for the panel of your design issue:

- `UI/Sprites/`, `UI/Themes/`, `UI/Textures/`, `UI/Fonts/`
- `docs/ui/specs/`, `docs/ui/mockups/`

For a building or unit art delivery the producer gives you: its folder under `Art/` (including its requirements, approvals and handoff) and its generator scripts under `Tools/Art/`. Maintain existing building records in `docs/art/<building>/` when that is already their established location.

You deliver the assets; Claude assembles them. `UI/Layouts/` is Claude's: show where everything goes and how it behaves in your mockup and spec, and Claude writes the layout files from them. Never touch `Source/`, `Data/`, `Content/` outside `Content/Art/Candidates/`, or any other file. Never commit, push or open pull requests: the producer and Claude take your delivery in.

## The running editor (Unreal MCP)

When the producer has the editor open with `ue.ps1 -Task editor`, the MCP server `unreal` is at `http://127.0.0.1:8000/mcp`. It offers looking and playing (the level and its actors, PIE, viewport and widget screenshots, the Slate widget tree, the output log, `ExecuteConsoleCommand` for commands such as `stat fps` or `rts.UI.Reload`) and importing and editing: FBX and texture import, static meshes (slots, LODs, collision), materials and material instances, Blueprints, data assets, actors and levels. Arbitrary Python is off; the full list is in [`docs/ui/AI_TEAM.md`](docs/ui/AI_TEAM.md).

Create and save assets only under `/Game/Art/Candidates/` (one folder per candidate, e.g. `/Game/Art/Candidates/USA_HQ/Candidate13/`), with a preview level of your own there. Everything else under `/Game` is generated from text files by `setup_testmap.py`: never edit or save it, and never place your candidate in `L_TestMap` or another production level. Claude takes an accepted candidate into the setup pipeline.
