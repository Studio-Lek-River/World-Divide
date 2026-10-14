"""Writes the brief folder of a UI design issue, ready to attach to ChatGPT. Used by the /ui-brief skill.

    python Tools/Scripts/ui_brief.py <design issue number>

The panels are the UI/Layouts/<Panel>.layout.json files the issue's "Send to ChatGPT" section lists; the folder is
Saved/UIBriefs/<first panel>/. The binding keys, lists and actions come from the code through ue.ps1 -Task uibindings
(URTSUICommandlet), never from the issue text. The folder holds:
- prompt.txt: the issue's "Prompt (ChatGPT)" block;
- bindings.md: per panel its keys, lists and actions, or its slots and modals when it has none, and every engine panel,
  theme colour and font a layout may name;
- sprites.md: every existing sprite per set;
- UI/Layouts/: the panels' current layouts; DESIGNER_HANDBOOK.md; docs/ui/specs/: the panels' specs when they exist;
- screenshots/<Panel>/: the latest Saved/UIShots of each panel, when there are any;
- README.md: the list of files.
Prints "BRIEF: <folder>", and "MISMATCH: ..." where the issue's "Bindings and actions" list differs from the code.
"""

import glob
import json
import os
import re
import shutil
import subprocess
import sys

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
        lines += [f"## {panel}", ""]
        sources = [source for source in bindings["sources"] if panel in source["panels"]]
        if not sources:
            lines.append("No keys and no actions.")
        for source in sources:
            lines.append(f"- Keys: {codes(source['keys'])}")
            lists = "; ".join(f"`{name}` with {codes(items, 'item.')}" for name, items in source["lists"].items())
            lines.append(f"- Lists: {lists or 'none'}")
            lines.append(f"- Actions: {codes(source['actions'])}")
            declared.setdefault(panel, set()).update(source["keys"], source["actions"])
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
    """The issue's hand-written "Bindings and actions" list against the code: every name in backticks under a panel."""
    listed = section(body, "Bindings and actions")
    problems = []
    for panel, names in declared.items():
        block = re.search(r"`%s`:\n(.*?)(?=^- `|\Z)" % re.escape(panel), listed, re.S | re.M)
        if not block:
            continue
        mentioned = set(re.findall(r"`([A-Za-z][\w.]*)`", block.group(1)))
        for name in sorted(names - mentioned):
            problems.append(f"{panel}: the code has `{name}`, the issue does not list it")
        for name in sorted(mentioned - names):
            if "." in name or name[0].isupper():
                problems.append(f"{panel}: the issue lists `{name}`, the code does not have it")
    return problems


def main():
    if len(sys.argv) != 2 or not sys.argv[1].isdigit():
        sys.exit("usage: python Tools/Scripts/ui_brief.py <design issue number>")
    issue = read_issue(int(sys.argv[1]))
    body = issue["body"]
    if "-design]" not in issue["title"]:
        print(f"WARNING: #{issue['number']} is not a design issue: {issue['title']}")
    if issue["state"] != "OPEN":
        print(f"WARNING: #{issue['number']} is {issue['state'].lower()}")

    panels = []
    for panel in re.findall(r"UI/Layouts/(\w+)\.layout\.json", section(body, "Send to ChatGPT")):
        if panel not in panels:
            panels.append(panel)
    if not panels:
        sys.exit(f"FAILED: #{issue['number']} names no UI/Layouts/<Panel>.layout.json under **Send to ChatGPT**")
    prompt = re.search(r"\*\*Prompt \(ChatGPT\)\*\*\s*\n```\w*\n(.*?)\n```", body, re.S)
    if not prompt:
        sys.exit(f"FAILED: #{issue['number']} has no **Prompt (ChatGPT)** block")

    bindings = read_bindings()
    folder = os.path.join(REPO, "Saved", "UIBriefs", panels[0])
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
    declared = write_bindings(os.path.join(folder, "bindings.md"), panels, bindings, layouts)
    write_sprites(os.path.join(folder, "sprites.md"))
    shutil.copy(os.path.join(REPO, "docs", "ui", "DESIGNER_HANDBOOK.md"), folder)

    files = sorted(os.path.relpath(os.path.join(root, name), folder).replace("\\", "/")
                   for root, _, names in os.walk(folder) for name in names)
    with open(os.path.join(folder, "README.md"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# Brief for #{issue['number']}: {issue['title']}\n\nPanels: {', '.join(panels)}\n\n"
                     "Paste `prompt.txt` into ChatGPT and attach every file of this folder.\n\n"
                     + "".join(f"- `{file}`\n" for file in files + ["README.md"]))

    print(f"BRIEF: {os.path.relpath(folder, REPO)}")
    print(f"PANELS: {', '.join(panels)}")
    for panel in panels:
        print(f"  {panel}: {len(declared.get(panel, ()))} key(s) and action(s)"
              + (", screenshots" if os.path.isdir(os.path.join(folder, "screenshots", panel)) else ", no screenshots"))
    for panel in missing:
        print(f"NO LAYOUT: {panel} has no UI/Layouts/{panel}.layout.json yet")
    for problem in check_issue_list(body, declared):
        print(f"MISMATCH: {problem}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
