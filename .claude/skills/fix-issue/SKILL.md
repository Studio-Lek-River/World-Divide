---
name: fix-issue
description: Take a World-Divide GitHub issue number, investigate it against the UE 5.6 codebase, propose a fix and wait for approval, then implement it on main, build, run the automation tests and commit with Closes #n. Wraps the birds-ai issue flow (D:\birds-ai\commands\issue\body.md) with the Unreal and repo rules of this project. Use with /fix-issue <number>.
user-invocable: true
argument-hint: "<issue number>"
---

# /fix-issue — GitHub issue to verified commit on main

Argument: `$ARGUMENTS` (the issue number, e.g. `1`). No number → ask for one and stop until you have it.

## 0. Load the base flow

The base logic lives in `D:\birds-ai\commands\issue\body.md`. Read it fresh at the start of every run; never work from memory of it, since the user edits it. Follow its **Fix** section. The rules below override it wherever they conflict.

If that file cannot be read, use this fallback: inspect the issue → investigate code, tests and docs → present the viable fixes with tradeoffs and a recommendation → stop for approval → implement only the approved direction → verify → commit with `Closes #<number>` → report.

## Overrides for this repo

1. **Repo.** Always `gh ... --repo Studio-Lek-River/World-Divide`. GitHub only, no `tea`.
2. **Work on `main`.** No issue branch, no PR. This replaces birds-ai Fix steps 1, 2 and 5.
3. **Two gates.** Gate 1: no edits before the user approves an approach. Gate 2: no push before the user approves the local result.
4. **No attribution.** No `Co-Authored-By` trailer and no reference to Claude, Claude Code or Anthropic in commits or issue comments.

## 1. Investigate (read-only)

- `gh issue view <n> --repo Studio-Lek-River/World-Divide --json number,title,body,state,comments,url`. If the issue is closed, say so and ask whether to continue.
- Follow every `#ref` in the body and comments. If the issue builds on another issue that is still open (e.g. production buildings need building placement), report it as blocked and ask; do not build on missing foundations.
- Read the `README.md` Layout table, then the classes the issue names and their callers. The issue's **What** is the scope and its **Acceptance** is the definition of done.
- Check the issue against the project rules below, so the proposal already respects them.

## 2. Gate 1 — propose and stop

Present the viable approaches: affected files and classes, new classes/data assets, how it replicates, which tests prove the Acceptance, and tradeoffs. Recommend one. Do not invent alternatives just to offer a choice. Then `AskUserQuestion` (approve / adjust / stop) and wait. A recommendation is not approval.

## 3. Prepare git

Once approved:

- `git status --short`, `git branch --show-current`. Stop if the worktree is dirty, a rebase or merge is in progress, HEAD is detached, or the branch is not `main`. Do not stash, reset, clean or overwrite existing work.
- `git fetch origin` then `git merge --ff-only origin/main`. Stop if it cannot fast-forward.
- Record the base: `git rev-parse HEAD`.

## 4. Implement — project rules

- **C++ for gameplay logic**, Blueprint and data assets for content and UI. Every unit, building and weapon is a data asset (`URTSEntityData` and subclasses in `Source/WorldDivide/Data`).
- **Server-authoritative.** Every player order is an `FRTSCommand` sent through `ARTSPlayerController::Server_IssueCommand` and executed only on the server (`RTSCommands::Dispatch`). Gameplay state changes happen on the server and replicate; clients never mutate it directly. Singleplayer runs the same path.
- **Never hand-edit `.uasset` / `.umap`.** They are binary and live in Git LFS. Create or change test content by extending `Tools/Editor/setup_testmap.py` and regenerating.
- **Module dependencies** go in `Source/WorldDivide/WorldDivide.Build.cs`; editor-only code stays behind `WITH_EDITOR` / `Target.bBuildEditor`.
- **Tests.** Cover the issue's Acceptance with automation tests in `Source/WorldDivide/Tests`, named under the `WorldDivide.` prefix, matching the style of `RTSCommandTests.cpp`. PIE-driven tests run headless (`-nullrhi`).
- **Original IP only.** Nothing from C&C Generals: no names, art, audio, UI or code.
- Match the surrounding code: tabs, `RTS` prefix, Unreal naming (`A`/`U`/`F`/`E`), comment density.
- Implement only the approved direction. Findings outside the scope go to the report as suggested new issues, not into this change.

## 5. Verify

All through `Tools/Scripts/ue.ps1` (engine from `UE_ROOT`, default `G:\Program Files\Epic Games\UE_5.6`):

```
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task build
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup    # only if setup_testmap.py or data-asset properties changed
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test     # optional: -Filter WorldDivide.<Area>
```

- `build` exits 2 when `UnrealEditor` is running: ask the user to close the editor, then retry. Never kill the process yourself.
- Run `setup` after `build`, since the script uses the new C++ classes.
- The full `WorldDivide` test run must pass, including the new tests. Fix failures and rerun; do not commit red.
- `git status` afterwards: include regenerated assets under `Content/` in the commit; anything unexpected (e.g. untouched maps rewritten) → investigate before committing.
- Inspect `git diff` for leftovers, debug logging and out-of-scope edits.

## 6. Commit

Imperative subject in the repo's style (e.g. `Add health and weapon components with auto-targeting`), without the `[NN]` roadmap-order prefix from the issue title, a short body if the why is not obvious, and `Closes #<n>` on its own line as the last line. Multi-line messages via a quoted heredoc. Do not push.

## 7. Gate 2 — report and wait

Report:

- Commit SHA and subject, files changed.
- Build and test result (succeeded / failed counts from `ue.ps1`).
- Each **Acceptance** item: covered by an automated test (name it) or needs a manual check.
- A short manual checklist for what only the user can verify: PIE behaviour, feel, listen-server client view (*Play → Net Mode: Play As Listen Server, 2 players*).
- Suggested follow-up issues, if any.

Then `AskUserQuestion`: push to `origin/main` / adjust / leave unpushed. Push only on explicit approval; the push closes the issue through `Closes #<n>`. Never close the issue by hand.

## 8. Follow-ups and roadmap order

After the push, always, without asking again:

- Create every follow-up from the report as an issue in the `Roadmap` milestone: body with **Context** (start with `Follow-up to #<n>.`, name the classes involved), **What** and **Acceptance**, no labels. If an open issue already covers it, add a bullet to that issue's **What** instead of creating a duplicate.
- Title prefix: the roadmap slot it belongs in, `[NNb]` (`[NNc]`, ...) right after the issue it must precede or follow, e.g. `[06b]` for work that a later issue builds on. Never renumber existing issues.
- Update the `Roadmap` milestone description (`gh api -X PATCH repos/Studio-Lek-River/World-Divide/milestones/<id> -f description=...`): drop the line of the issue just closed and insert the new ones in order, keeping the `NN. #<n>` format under `Work top to bottom:`.
- Report the new issue URLs and the new order.
