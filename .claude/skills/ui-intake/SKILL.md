---
name: ui-intake
description: Take a World-Divide UI design delivery ZIP from ChatGPT in - check it before copying anything (places, names, JSON, PNG sizes, binding keys, actions, sprites), copy it into the repository and merge the sprite entries, run the setup only when art changed, run WorldDivide.UI.Validate and the full test run, turn "Requests for code" into issues, then stop and propose the commit that closes the design issue. Use with /ui-intake <zip> [design issue number].
user-invocable: true
argument-hint: "<zip> [design issue number]"
---

# /ui-intake — a design delivery into the repository

Arguments: `$ARGUMENTS` — the ZIP path, optionally the design issue number. No ZIP → ask for it and stop.

This is handbook section 8, step 5 (`docs/ui/DESIGNER_HANDBOOK.md`). Rules: `gh ... --repo Studio-Lek-River/World-Divide`; work on `main`; no `Co-Authored-By` trailer and no reference to Claude, Claude Code or Anthropic in commits, issues or comments; never push.

## 1. Preconditions

- `git status --short`, `git branch --show-current`: stop when the branch is not `main` or a file the delivery would overwrite has uncommitted changes. Other local changes stay out of the commit.
- `UnrealEditor` running → setup and tests need it closed; ask the producer to close it.

## 2. Check, then copy

```
python Tools/Scripts/ui_intake.py "<zip>" --apply
```

It unpacks into `Saved/UIIntake/<zip name>/`, checks the places and names of handbook sections 5 and 7 and every JSON file, builds a staging copy of `UI/` with the delivery over it and the sprite entries merged, and checks that with `Tools/Editor/ui_sources.py` (sprite and theme files, PNG sizes) and `ue.ps1 -Task uicheck` (layouts: binding keys, lists, actions, sprites, fonts, colours, layouts). It copies only when there is no problem.

- **`INTAKE FAILED`**: nothing was copied. Report every `PROBLEM:` line grouped by file, each with its key, in words the designer can act on. Write the same list to `Saved/UIIntake/<zip name>/fixes.md`, opening with `Your delivery did not pass the intake check. Fix these points and send the whole ZIP again, same names and format (handbook section 7):`, and put it on the clipboard (`Get-Content -Raw -Encoding UTF8 <file> | Set-Clipboard`). Tell the producer: "The fix list is on your clipboard: paste it into the same ChatGPT chat." Then stop.
- **`INTAKE APPLIED`**: the `COPIED:` lines are what changed. `SETUP NEEDED` means sprites, themes, textures or fonts changed. `README:` is the designer's README; read it.

## 3. Import and test

- Only on `SETUP NEEDED`: `powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup`. Exit 3 → report the LFS lock instructions it prints and stop.
- Always: `powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test` (the full `WorldDivide` run, `WorldDivide.UI.Validate` included). A failure that is technical (a path, a merge) → fix it in the delivered files and rerun; a failure in the design itself → report it and stop.
- `git status --short`: the delivery's files and, after setup, the regenerated assets under `Content/UI/`. Anything else changed → investigate before going on.

## 4. Requests for code

Read section 7 **Requests for code** of each delivered `docs/ui/specs/<Panel>.md` (and the README). For each request not already covered by an open issue, draft an issue (title `[NNb] ...` placed after the panel's code issue, body with **Context** starting `Follow-up to #<design issue>.`, **What**, **Acceptance**, the milestone of the design issue). Show the drafts and create them only after the producer approves.

## 5. Stop and propose the commit

- **Design issue:** the argument, or else the open issue whose title has `-design]` and whose body names the delivered layout files; ask when it is not exactly one.
- Show the changed files, the test result (succeeded / failed counts) and the proposed message:

  ```
  Take in the <Panel> design delivery

  Closes #<design issue>
  ```
  (one line of body when the delivery needs explaining, e.g. which factions or what changed since the last round).
- `AskUserQuestion`: commit / adjust / leave uncommitted. Commit only on approval, with the files of this delivery only (`git add <paths>`), message through a quoted heredoc. Never push; the producer pushes.
