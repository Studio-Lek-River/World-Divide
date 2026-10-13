# World Divide — the AI team

The UI/UX work is done by four members. Everyone works from [`DESIGNER_HANDBOOK.md`](DESIGNER_HANDBOOK.md); section 8 there is the round each panel goes through.

| Who | Role | Writes |
|---|---|---|
| The producer (a person) | Picks the panel, plays, judges, approves every push | Anything |
| Claude Code | Programmer: the code issue of each panel, intake of deliveries, the UI validation test, commits | `Source/`, `Tools/`, `Content/` through `setup_testmap.py`, everything else on approval |
| ChatGPT / Codex | Designer: layouts, sprites, themes, the UX spec and mockups; its instructions are [`AGENTS.md`](../../AGENTS.md) | `UI/Layouts`, `UI/Sprites`, `UI/Themes`, `UI/Textures`, `UI/Fonts`, `docs/ui/specs`, `docs/ui/mockups` |
| Gemini through the Antigravity CLI `agy` | Reviewer: judges a delivery in the running game | Nothing |

No one but Claude touches `Source/`, `Content/` or `Data/`, no one saves assets through the editor, and only the producer pushes.

## The editor with Unreal MCP

```
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task editor
```

opens the editor with Epic's experimental Unreal MCP server at `http://127.0.0.1:8000/mcp`: loopback only and without authentication, so it is never exposed beyond this machine. The editor loads the plugins `ModelContextProtocol`, `EditorToolset` and `SlateInspectorToolset`, and the project's `WorldDivideMCP`, which adds `ExecuteConsoleCommand`. A packaged game and the headless `ue.ps1 -Task test` and `-Task setup` runs load none of them.

The server lists three tools (`list_toolsets`, `describe_toolset`, `call_tool`) and reaches the rest through them. MCP is for **looking and playing**: the actors in the level, PIE start and stop, viewport and widget screenshots, the Slate widget tree, the output log, console commands such as `stat fps` and `rts.UI.Reload`. It is never for saving assets or editing Blueprints: every `.uasset` is generated from text files by `setup_testmap.py`. The `EditorToolset` also offers tools that write; the instructions forbid them, and the reviewer runs where it cannot call them.

## Connecting each client

Each client connects once; the editor must be running for the server to answer.

- **Claude Code**: `.mcp.json` in the repository root defines the server `unreal`. Approve it once when Claude Code asks, then `/mcp` shows it and its tools.
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

The reviewer has no instructions file of its own, so it cannot pick up the designer's `AGENTS.md` role by mistake. `/ui-review` (#108) passes the reviewer role and its rules in the prompt and runs `agy -p --mode plan` without `--dangerously-skip-permissions`, so the reviewer can look at the editor but cannot change a file or call a tool that writes.

## The round

Each panel follows handbook section 8: Claude completes the code issue, the producer briefs the designer, the designer delivers, Claude takes the delivery in, the reviewer and the producer judge it in the running game, and the next round starts from that feedback. The skills `/ui-brief`, `/ui-intake` and `/ui-review` (#108) run these steps.
