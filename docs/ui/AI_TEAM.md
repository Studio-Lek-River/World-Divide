# World Divide — the AI team

The UI/UX work is done by four members. Everyone works from [`DESIGNER_HANDBOOK.md`](DESIGNER_HANDBOOK.md); section 8 there is the round each panel goes through, and [`ROUND_GUIDE.md`](ROUND_GUIDE.md) is the producer's step-by-step version of it (what to type, attach and paste).

| Who | Role | Writes |
|---|---|---|
| The producer (a person) | Picks the panel, plays, judges, approves every push | Anything |
| Claude Code | Programmer: the code issue of each panel, intake of deliveries, the UI validation test, commits | `Source/`, `Tools/`, `Content/` through `setup_testmap.py`, everything else on approval |
| ChatGPT / Codex | Designer: sprites, icons, themes, fonts, the UX spec and mockups; Claude assembles the layouts from them. Its instructions are [`AGENTS.md`](../../AGENTS.md) | `UI/Sprites`, `UI/Themes`, `UI/Textures`, `UI/Fonts`, `docs/ui/specs`, `docs/ui/mockups` |
| Gemini through the Antigravity CLI `agy` | Reviewer: judges a delivery in the running game | Nothing |

No one but Claude touches `Source/`, `Content/` or `Data/`, no one saves assets through the editor, and only the producer pushes.

## The editor with Unreal MCP

```
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task editor
```

opens the editor with Epic's experimental Unreal MCP server at `http://127.0.0.1:8000/mcp`: loopback only and without authentication, so it is never exposed beyond this machine. The editor loads the plugins `ModelContextProtocol`, `EditorToolset` and `SlateInspectorToolset`, and the project's `WorldDivideMCP`, which adds `ExecuteConsoleCommand`. A packaged game and the headless `ue.ps1 -Task test` and `-Task setup` runs load none of them.

The server lists three tools (`list_toolsets`, `describe_toolset`, `call_tool`) and reaches the rest through them. MCP is for **looking and playing**, and offers nothing else:

| Toolset | What remains |
|---|---|
| `EditorToolset.EditorAppToolset` | PIE start, stop and status; viewport, editor and asset-thumbnail captures; visible and selected actors, selecting and focusing actors, the viewport camera; CVar search |
| `EditorToolset.LogsToolset` | The output log, log categories and their verbosity |
| `SlateInspectorToolset.SlateInspectorToolset` | The Slate widget tree (`Snapshot`, `Observe`), widget screenshots, windows, `WaitFor`, `Hover` |
| `editor_toolset.toolsets.scene.SceneTools` | Finding actors, the current level, outliner folders, world traces |
| `editor_toolset.toolsets.actor.ActorTools` | Actor transforms, bounds, components, labels and tags (read only) |
| `editor_toolset.toolsets.object.ObjectTools` | Object properties and classes (read only) |
| `WorldDivideMCP.RTSConsoleToolset` | `ExecuteConsoleCommand`, e.g. `stat fps`, `rts.UI.Reload` |

Everything else is hidden by the allow-list in `Config/DefaultEditorPerProjectUserSettings.ini` (`ToolsetRegistrySettings`): the asset, Blueprint, material, data, mesh, texture and primitive toolsets, the programmatic and skill toolsets, every setter, and the Slate tools that click or type, since those can reach Save. `ExecuteConsoleCommand` refuses editor Python and commands that save or delete (`[WorldDivideMCP] BlockedConsoleCommands` in the same file). A toolset added by an engine update stays hidden until it is put on the list. Every `.uasset` is generated from text files by `setup_testmap.py`.

## Connecting each client

Each client connects once; the editor must be running for the server to answer.

- **Claude Code**: `.mcp.json` in the repository root defines the server `unreal`. Approve it once when Claude Code asks, then `/mcp` shows it and its tools; `.claude/settings.json` allows its tools without asking per call.
- **Antigravity CLI** (`agy`, signed in with the Google account): MCP servers are per user, so run once
  ```
  agy mcp add unreal http://127.0.0.1:8000/mcp
  agy mcp list
  ```
- **Codex / the ChatGPT desktop app**: MCP servers are per user, in `~/.codex/config.toml` (`%USERPROFILE%\.codex\config.toml` on Windows):
  ```toml
  [mcp_servers.unreal]
  url = "http://127.0.0.1:8000/mcp"
  ```
  Codex reads `AGENTS.md` in the repository root as its instructions.

## The reviewer

The reviewer has no instructions file of its own, so it cannot pick up the designer's `AGENTS.md` role by mistake. `/ui-review` passes the reviewer role and its rules in the prompt (`.claude/skills/ui-review/reviewer-prompt.md`) and runs `agy -p --mode plan` without `--dangerously-skip-permissions`, in a folder outside the repository that holds only the screenshots, the handbook, the spec and the brief, so the reviewer can look but cannot change a file or call a tool that writes.

## The round

Each panel follows handbook section 8: Claude completes the code issue, the producer briefs the designer, the designer delivers, Claude takes the delivery in, the reviewer and the producer judge it in the running game, and the next round starts from that feedback. Three Claude Code skills (`.claude/skills`) run the steps, so the producer only picks the panel, plays and judges:

| Skill | Does | Writes |
|---|---|---|
| `/ui-brief <design issue>` | `Tools/Scripts/ui_brief.py` reads the issue's prompt and panels, and the binding keys, lists and actions of those panels from the code (`ue.ps1 -Task uibindings`) | `Saved/UIBriefs/<Panel>/`: the prompt, `bindings.md`, `sprites.md`, the panels' layouts, the handbook, the style references and the latest screenshots, ready to attach to ChatGPT |
| `/ui-intake <zip>` | `Tools/Scripts/ui_intake.py` unpacks the ZIP into `Saved/UIIntake/`, checks places, names, JSON, PNG sizes, binding keys, actions and sprites (`ue.ps1 -Task uicheck`) and copies nothing when a problem is found; then it copies the files in, merges the sprite entries, runs setup when art changed and the full test run, drafts issues for "Requests for code", and stops with the commit proposed | The delivery's files, `Content/UI` after setup, a commit with `Closes #<design issue>` once the producer approves |
| `/ui-review <Panel>` | Takes the screenshots (`ue.ps1 -Task uishots -Panel`), gives them with the handbook, the spec and the brief to the reviewer in a folder outside the repository, and runs `agy -p --mode plan` with a fixed review format: broken handbook rules, readability, the HUD budget, states without colour, per resolution, at most 10 points | A comment with the review on the design issue |

## Screenshots of the HUD

The designer and the reviewer judge the HUD from pictures the game draws itself, in a fixed match state: 12,500 credits, low power, six units selected with a builder first (its build menu fills the command grid), a barracks with three units queued and one attack warning.

- `ue.ps1 -Task uishots` plays `L_TestMap` without the editor and saves the HUD in each faction's look (each theme in `UI/Themes` but Neutral) at 1920x1080, 2560x1440 and 3440x1440 to `Saved/UIShots/HUD/<Faction>_<Width>x<Height>.png`.
- `ue.ps1 -Task uishots -Panel ResourceBar` saves them to `Saved/UIShots/ResourceBar/`, each with a `_crop.png` of the layout element of that name.
- In a running game or PIE, also over MCP, the console command `rts.UI.Screenshot [Panel] [Faction]` does the same. PIE cannot change the resolution, so there it saves the viewport's size only.

Panels whose code issue is still open show as empty slots, and so do their crops.
