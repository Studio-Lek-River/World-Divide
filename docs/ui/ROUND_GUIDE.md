# World Divide — the design round, step by step

This is the producer's guide to one design round: what to type, what to attach, what to paste, and what you should see. The rules behind it are in [`DESIGNER_HANDBOOK.md`](DESIGNER_HANDBOOK.md) section 8 and [`AI_TEAM.md`](AI_TEAM.md). The order of the panels is the work order of the milestone **M2 HUD vertical slice** on GitHub. The target look of the whole HUD is [`mockups/HUD_Target.png`](mockups/HUD_Target.png).

Every panel goes through two issues:

1. **The code issue.** Claude makes the panel work with plain placeholder looks: data, clicks, tests. You type `/fix-issue <n>`, approve the proposal, and test the checklist Claude gives you.
2. **The design issue** (title `[..-design]`). ChatGPT makes it look right; Gemini reviews it. That round is the steps below.

Only start a design issue when its code issue is closed: the brief reads the panel's data from the code.

---

## Step 1: the brief (Claude)

**Type:** `/ui-brief <design issue>`, for example `/ui-brief 91`.

**You see:**
- Claude reports the brief, and Explorer opens with `Saved\UIBriefs\<Panel>.zip` selected.
- The ChatGPT prompt is already on your clipboard.
- `Saved\UIBriefs\<Panel>\STEPS.md` repeats these steps for this issue.

**If it fails:**
- `NO LAYOUT` means the code issue is not done yet; do that first.
- `MISMATCH` means the issue text lists other keys than the code has. Claude fixes the issue text if you ask; the code is right.

## Step 2: the delivery (ChatGPT)

1. Open ChatGPT and start a **new chat**: one chat per design issue, so it remembers the earlier rounds.
2. **Attach** `<Panel>.zip`: drag it from Explorer into the chat.
3. **Paste** with Ctrl+V (the prompt) and send.
4. ChatGPT may ask a question first. Answer it. When you have no preference, reply:
   > Follow the handbook and refs/HUD_Target.png.
5. Wait for the ZIP and **download** it to `D:\Downloads`. Its name is like `<Panel>_<date>.zip`.

**If ChatGPT gives only text or loose files, reply:**
> Deliver everything as the one ZIP of handbook section 7, folders mirroring the repository root.

**Optional, to see it live first:** with the editor open (`ue.ps1 -Task editor`) and PIE running, ChatGPT/Codex can try layout changes through Unreal MCP with `rts.UI.Reload`.

## Step 3: the intake (Claude)

1. **Close the editor.**
2. **Type:** `/ui-intake D:\Downloads\<the zip> <design issue>`.

**You see one of two results:**
- **Passed.** Files copied, tests green, and a proposed commit. Do not commit yet; look first (step 4).
- **Failed.** Nothing was copied, and the fix list is on your clipboard. Paste it into the **same** ChatGPT chat, send, and go back to step 2.5.

## Step 4: look yourself

Open `WorldDivide.uproject`, open `L_TestMap` and press Play. Check what the panel shows:
- with and without a selection
- at low power (build more than your power plants carry)
- when you hover buttons

Note what you don't like, in plain words.

## Step 5: the review (Gemini, automatic)

**Type:** `/ui-review <Panel> <design issue>`, for example `/ui-review HUD 91`.

**You see:**
- Claude takes screenshots at three resolutions, Gemini reviews them, and the review is posted on the design issue.
- The **revision prompt** is on your clipboard: the review's points plus an empty "Producer's notes" list.

You paste nothing into Gemini; Claude runs it.

## Step 6: decide

**Happy?** Tell Claude **"commit"**. The commit closes the design issue. Anything ChatGPT asked for under "Requests for code" becomes a new issue (Claude shows the drafts first).

**Not yet?**
1. In the **same** ChatGPT chat, paste the revision prompt.
2. Write your own remarks under "Producer's notes".
3. Attach the screenshot Claude named.
4. Send, then go back to step 2.5.

Two or three rounds is normal.

## Afterwards

- **Push** when you are ready: Claude gives a short test checklist first.
- **Next panel:** the next design issue in the milestone's work order.

---

## Cheat sheet

| Step | Where | Type / do | Attach | Paste |
|---|---|---|---|---|
| 1 | Claude | `/ui-brief <n>` | | |
| 2 | ChatGPT (new chat) | send | `Saved\UIBriefs\<Panel>.zip` | Ctrl+V (prompt) |
| 3 | Claude, editor closed | `/ui-intake D:\Downloads\<zip> <n>` | | |
| 3 (failed) | ChatGPT (same chat) | send | | Ctrl+V (fix list) |
| 4 | Unreal | play `L_TestMap` | | |
| 5 | Claude | `/ui-review <Panel> <n>` | | |
| 6 (not yet) | ChatGPT (same chat) | add your notes, send | the named screenshot | Ctrl+V (revision prompt) |
| 6 (happy) | Claude | "commit" | | |
