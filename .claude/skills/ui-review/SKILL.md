---
name: ui-review
description: Get the reviewer's (Gemini through the Antigravity CLI) review of a World-Divide HUD panel - take the screenshots with ue.ps1 -Task uishots -Panel, run agy headless in plan mode with the screenshots, the handbook, the spec and the brief, and post the review as a comment on the panel's design issue. Use with /ui-review <Panel> [design issue number].
user-invocable: true
argument-hint: "<Panel> [design issue number]"
---

# /ui-review — the reviewer's look at a panel

Arguments: `$ARGUMENTS` — the panel (a layout name, e.g. `ResourceBar`), optionally the design issue number. No panel → ask and stop.

Step 4 of the round (`docs/ui/DESIGNER_HANDBOOK.md` section 8, `docs/ui/AI_TEAM.md` "The reviewer"). It changes nothing in the repository. Rules: `gh ... --repo Studio-Lek-River/World-Divide`; no reference to Claude, Claude Code or Anthropic in the comment.

## 1. Design issue

The argument, or else the open issue with `-design]` in its title whose **Workflow** says `/ui-review <Panel>`. None or more than one → ask.

## 2. Screenshots

```
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uishots -Panel <Panel>
```

Exit 2 or "UnrealEditor is running" → ask the producer to close the editor, then retry. It needs a GPU. It writes `Saved/UIShots/<Panel>/<Faction>_<W>x<H>.png` and `_crop.png` for each.

## 3. The reviewer's workspace

The reviewer runs **outside the repository**, so it cannot pick up the designer's `AGENTS.md` and sees only what it is given. Build `$TEMP/WorldDivide-UIReview/<Panel>/` fresh (delete it first):

- `screenshots/`: every PNG of `Saved/UIShots/<Panel>/`;
- `DESIGNER_HANDBOOK.md`: a copy of `docs/ui/DESIGNER_HANDBOOK.md`;
- `spec.md`: a copy of `docs/ui/specs/<Panel>.md`, when it exists;
- `brief/`: `prompt.txt` and `bindings.md` from `Saved/UIBriefs/<folder>/` of the design issue, when `/ui-brief` wrote them;
- `prompt.md`: [reviewer-prompt.md](reviewer-prompt.md) with `{PANEL}` replaced by the panel and `{FILES}` by the list of screenshot paths, one `- screenshots/<file>` per line. The reviewer cannot list folders (shell commands are denied in headless plan mode), so every file it must open is named in the prompt. Do not change its format section: the review must keep that format so the rounds compare.

## 4. Run the reviewer

From that folder:

```
agy -p "$(cat prompt.md)" --mode plan --output-format json --print-timeout 900s
```

Never add `--dangerously-skip-permissions` or `--mode accept-edits`. The JSON's `status` must be `SUCCESS`; its `response` is the review. `denied_actions` in the JSON means the reviewer tried a tool plan mode denies; when the response is then empty, rerun. A failure, an empty review, or a review that names no screenshot file → rerun once, then report the error and stop. If `agy` asks to sign in, ask the producer to run `agy` once interactively.

## 5. Post it

`gh issue comment <issue> --body-file <file>` with:

```
**Review of `<Panel>`** by Gemini (Antigravity CLI, reviewer role), from `ue.ps1 -Task uishots -Panel <Panel>` at `<short HEAD sha>`.
Screenshots: `Saved/UIShots/<Panel>/` (<the file names>).

<the review, verbatim>
```

## 6. The revision prompt

Unless the verdict approves the panel as is, write `Saved/UIBriefs/<folder>/revision.md` (the brief folder of the design issue; `Saved/UIBriefs/<Panel>/` when there is none) and put it on the clipboard (`Get-Content -Raw -Encoding UTF8 <file> | Set-Clipboard`):

```
Review round for <Panel> (#<issue>). Revise your delivery with these points, keep everything else as it is, and send the whole ZIP again in the same format (handbook section 7).

Reviewer's points:
<the review's numbered points, verbatim>

Producer's notes:
- (add your own remarks here before sending)
```

The screenshots go along when they help: say which `Saved/UIShots/<Panel>/<Faction>_1920x1080.png` to attach.

## 7. Report

The comment URL, the verdict line, and the screenshots folder. Then the next step in one line: "The revision prompt is on your clipboard: paste it into the same ChatGPT chat, add your remarks, and attach `<screenshot>`." When the verdict approves, say so instead and offer the commit `/ui-intake` proposed.
