---
name: ui-brief
description: Write the design brief folder for a World-Divide UI design issue (#91-#99) to Saved/UIBriefs/<Panel>/, ready to attach to ChatGPT as is - the issue's prompt, the panel's binding keys and actions read from the C++ binding sources, the existing sprite names, the panel's layout files, the handbook and the latest screenshots. Use with /ui-brief <design issue number>.
user-invocable: true
argument-hint: "<design issue number>"
---

# /ui-brief — a design issue to a brief folder for ChatGPT

Argument: `$ARGUMENTS` (a design issue number, e.g. `91`). No number → ask for one and stop.

This is step 1 of a design round (`docs/ui/DESIGNER_HANDBOOK.md` section 8, `docs/ui/AI_TEAM.md`). It writes only under `Saved/UIBriefs/`, never in the repository, and commits nothing.

## 1. Write the folder

```
python Tools/Scripts/ui_brief.py <n>
```

It reads the issue with `gh`, takes as panels the `UI/Layouts/<Panel>.layout.json` files its **Send to ChatGPT** section lists (the folder is named after the first: `HUD` for #91), runs `ue.ps1 -Task uibindings` for the keys, lists and actions the `IRTSUIBindingSource` declarations give each panel, and writes `Saved/UIBriefs/<Panel>/` fresh:

| File | Content |
|---|---|
| `prompt.txt` | the issue's **Prompt (ChatGPT)** block, verbatim |
| `bindings.md` | per panel its keys, lists with `item.*` keys and actions; a panel without a source: "No keys and no actions." plus its slots and modals; then every engine panel, theme colour and font a layout may name |
| `sprites.md` | every existing sprite per set, with size, margin, tint and button states |
| `UI/Layouts/`, `docs/ui/specs/` | the panels' current layouts and specs |
| `DESIGNER_HANDBOOK.md` | the handbook |
| `screenshots/<Panel>/` | the latest `Saved/UIShots/<Panel>/` when there are any; it takes no new ones (`/ui-review` does) |
| `refs/HUD_Target.png` | the target mockup of the whole HUD |
| `README.md` | the list of files |
| `STEPS.md` | the producer's steps for this round (not in the ZIP) |

It packs everything but `prompt.txt` and `STEPS.md` into `Saved/UIBriefs/<Panel>.zip`, the one file to attach, puts `prompt.txt` on the clipboard and opens Explorer with the ZIP selected (`--quiet` skips both).

`FAILED:` → report it and stop. `WARNING:` (not a design issue, or closed) → report it and ask whether to go on.

## 2. Style references

The issue may name other attachments (e.g. the USA upgrade icons). Copy those that are in the repository into the folder and add them to its `README.md`; list the others in the report for the producer to attach by hand.

## 3. Check against the issue

`MISMATCH:` lines are keys or actions the issue's hand-written **Bindings and actions** list and the code disagree on. The code is the truth; report each so the producer can fix the issue text. `NO LAYOUT:` → the panel's code issue is not done; say so.

## 4. Report

The folder path, the panels, the key/action count per panel, whether screenshots were included, any difference between the issue's list and the JSON, and the style references the producer must attach by hand. Copy any added style reference into the ZIP too (re-pack it), so there is still one file to attach.

End with the producer's next step, from `STEPS.md` step 1: "In ChatGPT, open a new chat, attach `Saved\UIBriefs\<Panel>.zip`, press Ctrl+V and send. When ChatGPT gives you a ZIP, save it to D:\Downloads and type `/ui-intake D:\Downloads\<zip> <n>`."
