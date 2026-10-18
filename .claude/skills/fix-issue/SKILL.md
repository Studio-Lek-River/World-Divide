---
name: fix-issue
description: Take a World-Divide GitHub issue number, investigate it against the UE 5.8 codebase, propose a fix and wait for approval, then implement it on main, build, run the automation tests and commit with Closes #n. Wraps the birds-ai issue flow (D:\birds-ai\commands\issue\body.md) with the Unreal and repo rules of this project. Use with /fix-issue <number>; a bare /fix-issue says what to pick up next.
user-invocable: true
argument-hint: "[issue number]"
---

# /fix-issue — GitHub issue to verified commit on main

Argument: `$ARGUMENTS` (the issue number, e.g. `1`). No number → run **What's next** (section 9) and stop there: it names the issue to pick up and the command, and the producer starts it.

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

All through `Tools/Scripts/ue.ps1` (engine from `UE_ROOT`, default `D:\Program Files\Epic Games\UE_5.8`):

```
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task build
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup    # only if setup_testmap.py or data-asset properties changed
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Filter WorldDivide.<Area>,WorldDivide.<Area2>
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Tier fast
```

- `build` exits 2 when `UnrealEditor` is running: ask the user to close the editor, then retry. Never kill the process yourself.
- Run `setup` after `build`, since the script uses the new C++ classes.
- **Tests, cheapest first; never the full suite by default.** Each editor start costs about 15 s, so put every filter in one comma-separated `-Filter`.
  1. While working: `-Filter` with the areas the change touches and the new tests, their `[Slow]` tests included. After a failure, fix it and run `-Rerun` (only the tests that failed last time), not the whole filter again.
  2. Before the commit: `-Tier fast`, the whole suite without the `[Slow]` tests of `Config/Tests/Tags.ini`, about 2 min. It catches regressions elsewhere. Skip it when step 1 already ran the areas that could break, such as a docs-only change or one confined to a single area.
  3. The full suite, a bare `-Task test` (two editors in parallel, about 5 min), only when the change touches shared foundations (navigation, movement, avoidance, networking, commands, the test helpers, `Build.cs` or config), for a scale step, or at a milestone gate. Say why in the report.
- Do not commit red. Exit 5 means the watchdog killed a hung run; the test it names is the one to look at.
- If the run names a test over 8 s without a `[Slow]` tag, add it to `Config/Tests/Tags.ini` in the same commit.
- The report names the filters and tier that ran, with the counts and wall clock.
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

If the setup is blocked by an LFS-locked asset, check `git lfs locks`: nobody holds it → ask the producer once, `git lfs lock` it, rerun setup, and `git lfs unlock` it after the push. Someone else holds it → stop and report who.

## 8. Follow-ups and work order

The work order lives in the descriptions of the open milestones, worked in order: `M1 Design alignment` → `M2 HUD vertical slice` → `M3 Living roster` → `M4 Gameplay foundations` → `M5 Generals & powers` (list them with `gh api "repos/Studio-Lek-River/World-Divide/milestones?state=all"`). The old `Roadmap` milestone is closed; never add to it.

After the push, always, without asking again:

- Create every follow-up from the report as an issue in the milestone it belongs to (usually the one of the issue just closed): body with **Context** (start with `Follow-up to #<n>.`, name the classes involved), **What** and **Acceptance**, no labels, title at most 70 characters. If an open issue already covers it, add a bullet to that issue's **What** instead of creating a duplicate.
- Title prefix: the slot it belongs in, the next free letter or suffix in its series (`[19j]`, `[06b]`, ...), checked against all existing titles. Never renumber existing issues.
- Update that milestone's description (`gh api -X PATCH repos/Studio-Lek-River/World-Divide/milestones/<id> -f description=...`): drop the line of the issue just closed, insert the new ones in order, and renumber the `N. #<n> <short title>` lines under `Work top to bottom:`.
- Report the new issue URLs and the new order.

## 9. What's next

End every run (and answer a bare `/fix-issue`) with a short **What's next** block, so the producer never has to remember the flow. Read it live: the milestone work orders, `gh issue view` for state, and for design issues `docs/ui/ROUND_GUIDE.md`.

1. **Next step.** The first open issue in the lowest open milestone's work order. Say its number and title, what it delivers in one line, and the exact command:
   - code issue → `/fix-issue <n>`;
   - design issue (`[..-design]`) → the step of its round (`docs/ui/ROUND_GUIDE.md`) it is at, worked out from what exists:
     - no `Saved/UIBriefs/<Panel>/` yet → `/ui-brief <n>`, then hand the ZIP to ChatGPT;
     - brief made, no delivery taken in → wait for ChatGPT's ZIP, then `/ui-intake <zip> <n>` (editor closed);
     - delivery taken in, no review comment on the issue → look in PIE yourself, then `/ui-review <Panel> <n>`;
     - reviewed → commit the intake (it closes the issue) or another ChatGPT round with the review.
   - A design issue whose code issue is still open is not ready: say so and take the code issue.
2. **In parallel.** While a design round waits on ChatGPT, name the next code issue in the order that does not depend on that design (code works with placeholder looks), with its `/fix-issue <n>`.
3. **Blocked.** If the next issue builds on an open issue, say which and skip to the next one that is not blocked.

Keep it to a few lines: what to do now, the command, and what can run alongside.
