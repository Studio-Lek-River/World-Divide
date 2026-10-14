"""Writes the brief folder of a UI design issue, ready to attach to ChatGPT. Used by the /ui-brief skill.

    python Tools/Scripts/ui_brief.py <design issue number> [--quiet]

The panels are the UI/Layouts/<Panel>.layout.json files the issue's "Send to ChatGPT" section lists; the folder is
Saved/UIBriefs/<first panel>/. An issue without panels (themes and sprites only) gets the folder of the spec its prompt
names (Saved/UIBriefs/Theme/ for docs/ui/specs/Theme.md), else Saved/UIBriefs/Issue<n>/. The binding keys, lists and actions come from the code through ue.ps1 -Task uibindings
(URTSUICommandlet), never from the issue text. The folder holds:
- prompt.txt: the issue's "Prompt (ChatGPT)" block;
- bindings.md: per panel its keys, lists and actions, or its slots and modals when it has none, and every engine panel,
  theme colour and font a layout may name;
- sprites.md: every existing sprite per set;
- UI/Layouts/: the panels' current layouts; DESIGNER_HANDBOOK.md; docs/ui/specs/: the panels' specs when they exist;
- screenshots/<Panel>/: the latest Saved/UIShots of each panel, when there are any;
- the other repository files that section names (themes, mockups; wildcards allowed), at their repository paths;
- refs/HUD_Target.png: the target mockup of the whole HUD;
- README.md: the list of files; STEPS.md: the producer's steps for this round.
It also packs everything but prompt.txt and STEPS.md into Saved/UIBriefs/<Panel>.zip, the one file to attach. Unless
--quiet, it puts prompt.txt on the clipboard and opens Explorer on the ZIP.
Prints "BRIEF: <folder>", "ZIP: <file>", and "MISMATCH: ..." where the issue's "Bindings and actions" list differs from the code.
"""

import glob
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GH_REPO = "Studio-Lek-River/World-Divide"
BINDINGS = "Saved/UIBriefs/bindings.json"


def read_issue(number):
    result = subprocess.run(["gh", "issue", "view", str(number), "--repo", GH_REPO, "--json", "number,title,body,state"],
                            capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        sys.exit(f"FAILED: gh issue view {number}: {result.stderr.strip()}")
    issue = json.loads(result.stdout)
    issue["body"] = issue["body"].replace("\r\n", "\n")
    return issue


def section(body, title):
    """The text after the line that starts with **title** up to the next line that starts with a bold heading."""
    match = re.search(r"^\*\*%s\*\*[^\n]*\n(.*?)(?=^\*\*[^*\n]+\*\*|\Z)" % re.escape(title), body, re.S | re.M)
    return match.group(1) if match else ""


def read_bindings():
    result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                             os.path.join(REPO, "Tools", "Scripts", "ue.ps1"), "-Task", "uibindings", "-Out", BINDINGS],
                            cwd=REPO, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("FAILED: ue.ps1 -Task uibindings\n" + result.stdout[-2000:])
    with open(os.path.join(REPO, BINDINGS), encoding="utf-8") as handle:
        return json.load(handle)


def slots(element, found):
    """(name, layout or widget) of every Slot in a layout element tree."""
    if element.get("type") == "Slot":
        found.append((element.get("name", "?"), element.get("layout") or element.get("widget") or "?"))
    for child in element.get("children", []):
        slots(child, found)
    if isinstance(element.get("template"), dict):
        slots(element["template"], found)
    return found


def codes(names, prefix=""):
    return ", ".join(f"`{prefix}{name}`" for name in names) or "none"


def write_bindings(path, panels, bindings, layouts):
    lines = ["# Binding keys and actions", "",
             "Read from the game code (`IRTSUIBindingSource`). Use only these in `{key}`, `bind`, `visibleWhen`, `repeat` "
             "and `action`; ask for anything else under \"Requests for code\" in your spec.", ""]
    declared = {}
    for panel in panels:
        declared[panel] = {"keys": set(), "names": set(), "optional": set()}
        lines += [f"## {panel}", ""]
        sources = [source for source in bindings["sources"] if panel in source["panels"]]
        if not sources:
            lines.append("No keys and no actions.")
        for source in sources:
            lines.append(f"- Keys: {codes(source['keys'])}")
            lists = "; ".join(f"`{name}` with {codes(items, 'item.')}" for name, items in source["lists"].items())
            lines.append(f"- Lists: {lists or 'none'}")
            lines.append(f"- Actions: {codes(source['actions'])}")
            declared[panel]["keys"].update(source["keys"])
            declared[panel]["names"].update(source["keys"], source["actions"], source["lists"])
            for items in source["lists"].values():
                declared[panel]["names"].update(f"item.{item}" for item in items)
            if source["lists"]:
                # URTSLayoutWidget supplies item.empty to every padded list cell (RTSLayout::ItemEmptyKey).
                declared[panel]["optional"].add("item.empty")
        layout = layouts.get(panel)
        if layout is not None:
            found = slots(layout, [])
            if found:
                lines.append(f"- Slots: " + ", ".join(f"`{name}` ({target})" for name, target in found))
            if layout.get("modals"):
                lines.append(f"- Modals: {codes(layout['modals'])}")
        lines.append("")
    lines += ["## Everything a layout may name", "",
              f"- Engine panels for a `Slot`'s `widget`: {codes(bindings['widgets'])}",
              f"- Theme colours: {codes(bindings['colors'])}",
              f"- Fonts: {codes(bindings['fonts'])}", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    return declared


def write_sprites(path):
    lines = ["# Existing sprites", "",
             "A layout names a sprite without its set: the engine takes `<Faction>.<Name>`, then `Common.<Name>`, then a grey "
             "placeholder (handbook section 3).", ""]
    for file in sorted(glob.glob(os.path.join(REPO, "UI", "Sprites", "*.sprites.json"))):
        lines += [f"## {os.path.basename(file)}", ""]
        with open(file, encoding="utf-8") as handle:
            sprites = json.load(handle).get("sprites", {})
        for name, entry in sprites.items():
            size = entry.get("size", ["?", "?"])
            facts = [f"{size[0]}x{size[1]}"]
            facts += [f"margin {entry['margin']}"] if "margin" in entry else []
            facts += ["tint"] if entry.get("tint") else []
            facts += ["button states"] if "states" in entry else []
            facts += [f"{entry['frames']} frames"] if "frames" in entry else []
            lines.append(f"- `{name}`: {', '.join(facts)}")
        lines.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def check_issue_list(body, declared):
    """The issue's hand-written "Bindings and actions" list against the code: every name in backticks under a panel,
    except the values written after a key (`selection.mode`: `None`, `Single` or `Multi`)."""
    listed = section(body, "Bindings and actions")
    problems = []
    for panel, found in declared.items():
        block = re.search(r"`%s`:\n(.*?)(?=^- `|\Z)" % re.escape(panel), listed, re.S | re.M)
        if not block:
            continue
        mentioned = set()
        for line in block.group(1).splitlines():
            value = re.search(r"`([\w.]+)`:", line)
            if value and value.group(1) in found["keys"]:
                line = line[:value.end()]
            mentioned.update(re.findall(r"`([A-Za-z][\w.]*)`", line))
        names = found["names"]
        for name in sorted(names - mentioned):
            problems.append(f"{panel}: the code has `{name}`, the issue does not list it")
        for name in sorted(mentioned - names - found["optional"]):
            if "." in name or name[0].isupper():
                problems.append(f"{panel}: the issue lists `{name}`, the code does not have it")
    return problems


STEPS = r"""# Your steps for #{number}: {title}

The full guide is `docs/ui/ROUND_GUIDE.md`.

1. **ChatGPT, new chat.** Attach `Saved\UIBriefs\{zip}` (Explorer has it selected), press **Ctrl+V** (the prompt is
   on your clipboard; it is `prompt.txt` in this folder if you lost it) and send.
   ChatGPT asks a question first? Answer it; when unsure, say "Follow the handbook and refs/HUD_Target.png."
2. **Download** the ZIP ChatGPT gives back to `D:\Downloads`.
   Only text, no ZIP? Reply: "Deliver it as the one ZIP of handbook section 7."
3. **Claude:** close the editor, then type `/ui-intake D:\Downloads\<the zip> {number}`.
   Problems? Claude puts the fix list on your clipboard: paste it into the same ChatGPT chat and go back to 2.
4. **Look yourself:** open the project and play `L_TestMap`.
5. **Claude:** type `/ui-review {panel} {number}`. Gemini reviews the screenshots; you paste nothing.
   Claude puts `revision.md` (the review plus your notes) on your clipboard.
6. **Happy?** Tell Claude "commit". **Not yet?** Paste into the same ChatGPT chat, add your own remarks, go back to 2.
"""


def pack(folder, archive):
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
        for root, _, names in os.walk(folder):
            for name in names:
                full = os.path.join(root, name)
                relative = os.path.relpath(full, folder).replace("\\", "/")
                if relative in ("prompt.txt", "STEPS.md"):
                    continue
                # The 1440p and ultrawide shots stay in the folder; the ZIP keeps uploads small with 1080p only.
                if relative.startswith("screenshots/") and "1920x1080" not in name:
                    continue
                package.write(full, relative)


def hand_over(prompt_file, archive):
    """The prompt on the clipboard (PowerShell keeps the Unicode that clip.exe mangles) and Explorer on the ZIP."""
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    f"Get-Content -Raw -Encoding UTF8 -LiteralPath '{prompt_file}' | Set-Clipboard"], check=False)
    subprocess.run(["explorer", f"/select,{archive}"], check=False)


def main():
    arguments = [argument for argument in sys.argv[1:] if argument != "--quiet"]
    if len(arguments) != 1 or not arguments[0].isdigit():
        sys.exit("usage: python Tools/Scripts/ui_brief.py <design issue number> [--quiet]")
    issue = read_issue(int(arguments[0]))
    body = issue["body"]
    if "-design]" not in issue["title"]:
        print(f"WARNING: #{issue['number']} is not a design issue: {issue['title']}")
    if issue["state"] != "OPEN":
        print(f"WARNING: #{issue['number']} is {issue['state'].lower()}")

    panels = []
    for panel in re.findall(r"UI/Layouts/(\w+)\.layout\.json", section(body, "Send to ChatGPT")):
        if panel not in panels:
            panels.append(panel)
    prompt = re.search(r"\*\*Prompt \(ChatGPT\)\*\*\s*\n```\w*\n(.*?)\n```", body, re.S)
    if not prompt:
        sys.exit(f"FAILED: #{issue['number']} has no **Prompt (ChatGPT)** block")
    if panels:
        name = panels[0]
    else:
        spec = re.search(r"docs/ui/specs/(\w+)\.md", prompt.group(1))
        name = spec.group(1) if spec else f"Issue{issue['number']}"

    bindings = read_bindings()
    folder = os.path.join(REPO, "Saved", "UIBriefs", name)
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    os.makedirs(os.path.join(folder, "UI", "Layouts"))

    with open(os.path.join(folder, "prompt.txt"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(prompt.group(1) + "\n")
    layouts = {}
    missing = []
    for panel in panels:
        source = os.path.join(REPO, "UI", "Layouts", f"{panel}.layout.json")
        if os.path.isfile(source):
            with open(source, encoding="utf-8") as handle:
                layouts[panel] = json.load(handle)
            shutil.copy(source, os.path.join(folder, "UI", "Layouts"))
        else:
            missing.append(panel)
        spec = os.path.join(REPO, "docs", "ui", "specs", f"{panel}.md")
        if os.path.isfile(spec):
            os.makedirs(os.path.join(folder, "docs", "ui", "specs"), exist_ok=True)
            shutil.copy(spec, os.path.join(folder, "docs", "ui", "specs"))
        shots = os.path.join(REPO, "Saved", "UIShots", panel)
        if os.path.isdir(shots):
            shutil.copytree(shots, os.path.join(folder, "screenshots", panel))
    attached = []
    for pattern in re.findall(r"`((?:UI|docs)/[^`\s]+)`", section(body, "Send to ChatGPT")):
        for source in sorted(glob.glob(os.path.join(REPO, pattern))):
            relative = os.path.relpath(source, REPO).replace("\\", "/")
            if relative == "docs/ui/DESIGNER_HANDBOOK.md" or relative.startswith("UI/Layouts/") or relative in attached:
                continue
            os.makedirs(os.path.join(folder, os.path.dirname(relative)), exist_ok=True)
            shutil.copy(source, os.path.join(folder, relative))
            attached.append(relative)
    declared = write_bindings(os.path.join(folder, "bindings.md"), panels, bindings, layouts)
    write_sprites(os.path.join(folder, "sprites.md"))
    shutil.copy(os.path.join(REPO, "docs", "ui", "DESIGNER_HANDBOOK.md"), folder)
    mockup = os.path.join(REPO, "docs", "ui", "mockups", "HUD_Target.png")
    if os.path.isfile(mockup):
        os.makedirs(os.path.join(folder, "refs"))
        shutil.copy(mockup, os.path.join(folder, "refs"))

    files = sorted(os.path.relpath(os.path.join(root, name), folder).replace("\\", "/")
                   for root, _, names in os.walk(folder) for name in names)
    with open(os.path.join(folder, "README.md"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# Brief for #{issue['number']}: {issue['title']}\n\nPanels: {', '.join(panels) or 'none'}\n\n"
                     "`refs/HUD_Target.png` is the target look of the whole HUD: follow its arrangement and mood "
                     "within the handbook's rules.\n\n"
                     + "".join(f"- `{file}`\n" for file in files + ["README.md"]))
    archive = folder + ".zip"
    if os.path.isfile(archive):
        os.remove(archive)
    pack(folder, archive)
    with open(os.path.join(folder, "STEPS.md"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(STEPS.format(number=issue["number"], title=issue["title"], panel=panels[0] if panels else "HUD",
                                  zip=os.path.basename(archive)))
    if "--quiet" not in sys.argv:
        hand_over(os.path.join(folder, "prompt.txt"), archive)

    print(f"BRIEF: {os.path.relpath(folder, REPO)}")
    print(f"ZIP: {os.path.relpath(archive, REPO)}")
    print(f"PANELS: {', '.join(panels) or 'none'}")
    for relative in attached:
        print(f"ATTACHED: {relative}")
    for panel in panels:
        print(f"  {panel}: {len(declared[panel]['names'])} key(s), list(s) and action(s)"
              + (", screenshots" if os.path.isdir(os.path.join(folder, "screenshots", panel)) else ", no screenshots"))
    for panel in missing:
        print(f"NO LAYOUT: {panel} has no UI/Layouts/{panel}.layout.json yet")
    for problem in check_issue_list(body, declared):
        print(f"MISMATCH: {problem}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
