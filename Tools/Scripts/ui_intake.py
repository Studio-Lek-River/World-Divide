"""Checks a designer's delivery ZIP (docs/ui/DESIGNER_HANDBOOK.md, section 7) before anything lands in the repository, and
with --apply copies it in. Used by the /ui-intake skill.

    python Tools/Scripts/ui_intake.py <zip> [--apply]

The ZIP is unpacked into Saved/UIIntake/<zip name>/delivery. Every file must sit where section 7 puts it and be named as
section 5 says, and every JSON file must parse. Then Saved/UIIntake/<zip name>/staging/UI is built from the repository's
UI/ with the delivery laid over it, its sprite entries merged into the sprite files of the same set, and checked:
- the sprite and theme files with Tools/Editor/ui_sources.py, as the setup script reads them (PNG sizes, missing files);
- the layouts with ue.ps1 -Task uicheck (URTSUICommandlet): binding keys, actions, sprites, fonts, colours, layouts.

Every problem is printed as "PROBLEM: <file>: <entry>: <key>: <problem>" and the script exits 1 with nothing copied.
With --apply and no problem, the files are copied into the repository (README.txt stays out), the sprite files merged,
each one printed as "COPIED: <path>", and "SETUP NEEDED" printed when sprites, themes, textures or fonts changed.
"""

import argparse
import filecmp
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "Tools", "Editor"))
import ui_sources  # noqa: E402

FACTIONS = ("USA", "China", "Insurgents")
NAME = r"[A-Za-z0-9_]+"
FACTION = "|".join(FACTIONS)
# Where each delivered file may go (handbook sections 5 and 7), as a pattern over its path from the repository root.
PLACES = [
    (re.compile(r"UI/Sprites/(Common|%s)\.sprites\.json" % FACTION), "a sprite file"),
    (re.compile(r"UI/Themes/(Neutral|%s)\.theme\.json" % FACTION), "a theme"),
    (re.compile(r"UI/Textures/(?P<f>Common|%s)/T_UI_(?P=f)_%s\.png" % (FACTION, NAME)), "a HUD piece"),
    (re.compile(r"UI/Textures/(?P<f>Common|%s)/Icons/T_Icon_(?P=f)_%s\.png" % (FACTION, NAME)), "an entity icon"),
    (re.compile(r"UI/Textures/(?P<f>%s)/Commands/T_Cmd_(?P=f)_%s\.png" % (FACTION, NAME)), "a command icon"),
    (re.compile(r"UI/Textures/(?P<f>%s)/Art/T_Art_(?P=f)_%s\.png" % (FACTION, NAME)), "an illustration"),
    (re.compile(r"UI/Fonts/[A-Za-z0-9_-]+(\.LICENSE)?\.(ttf|otf|txt)"), "a font or its licence"),
    (re.compile(r"docs/ui/specs/[A-Za-z0-9]+\.md"), "a UX spec"),
    (re.compile(r"docs/ui/mockups/Mockup_[A-Za-z0-9]+_(%s)\.png" % FACTION), "a mockup"),
    (re.compile(r"README\.txt"), "the README"),
]
# Folders whose change needs ue.ps1 -Task setup before the game sees it.
SETUP_FOLDERS = ("UI/Sprites/", "UI/Themes/", "UI/Textures/", "UI/Fonts/")
IGNORED = re.compile(r"(^|/)(__MACOSX/|\.DS_Store$|Thumbs\.db$)")


def unpack(zip_path, delivery, problems):
    """Unpacks zip_path into delivery; the files' paths from the repository root. A ZIP that wraps everything in one
    folder of its own is unwrapped."""
    with zipfile.ZipFile(zip_path) as archive:
        names = [info.filename for info in archive.infolist() if not info.is_dir() and not IGNORED.search(info.filename)]
        tops = {name.split("/", 1)[0] for name in names}
        prefix = ""
        if len(tops) == 1 and all("/" in name for name in names) and not tops & {"UI", "docs"}:
            prefix = tops.pop() + "/"
        files = []
        for name in names:
            relative = name[len(prefix):]
            if relative.startswith("/") or ".." in relative.split("/") or ":" in relative:
                problems.append(f"{name}: is outside the ZIP's root")
                continue
            target = os.path.join(delivery, *relative.split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with archive.open(name) as source, open(target, "wb") as out:
                shutil.copyfileobj(source, out)
            files.append(relative)
    return sorted(set(files))


def check_places(files, delivery, problems):
    for relative in files:
        if not any(pattern.fullmatch(relative) for pattern, _ in PLACES):
            problems.append(f"{relative}: not a place or name the handbook allows (sections 5 and 7)")
        elif relative.endswith(".json"):
            try:
                with open(os.path.join(delivery, relative), encoding="utf-8") as handle:
                    json.load(handle)
            except ValueError as error:
                problems.append(f"{relative}: does not parse: {error}")
        elif relative.endswith(".png") and ui_sources.png_size(os.path.join(delivery, relative)) is None:
            problems.append(f"{relative}: is not a PNG")


def load_sprites(path):
    with open(path, encoding="utf-8") as handle:
        sprites = json.load(handle).get("sprites", {})
    return sprites if isinstance(sprites, dict) else {}


def format_value(value):
    """A sprite entry the way the sprite files write it: one line, spaces inside braces."""
    if isinstance(value, dict):
        return "{ " + ", ".join(f"{json.dumps(key)}: {format_value(item)}" for key, item in value.items()) + " }"
    if isinstance(value, list):
        return "[" + ", ".join(format_value(item) for item in value) + "]"
    return json.dumps(value)


def merged_sprites(repo_file, delivered_file):
    """The repository's entries with the delivered ones replacing those of the same name and the new ones after them,
    or None when that changes nothing."""
    entries = load_sprites(repo_file) if os.path.isfile(repo_file) else {}
    merged = {**entries, **load_sprites(delivered_file)}
    return None if merged == entries else merged


def format_entry(name, entry):
    """One sprite entry; a button's states one per line, as the sprite files write them."""
    head = f"    {json.dumps(name)}: "
    states = entry.get("states") if isinstance(entry, dict) else None
    if not isinstance(states, dict):
        return head + format_value(entry)
    indent = " " * len(head)
    lines = [f'{indent}    {json.dumps(state)}: {format_value(file)}' for state, file in states.items()]
    rest = ", ".join(f"{json.dumps(key)}: {format_value(item)}" for key, item in entry.items() if key != "states")
    if not rest:
        return head + '{ "states": {\n' + ",\n".join(lines) + " } }"
    return head + '{ "states": {\n' + ",\n".join(lines) + " },\n" + f"{indent}  {rest} }}"


def write_sprites(path, entries):
    lines = [format_entry(name, entry) for name, entry in entries.items()]
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write('{\n  "sprites": {\n' + ",\n".join(lines) + "\n  }\n}\n")


def stage(files, delivery, staging):
    """staging/UI: the repository's UI/ with the delivery over it and its sprite files merged."""
    if os.path.isdir(staging):
        shutil.rmtree(staging)
    shutil.copytree(os.path.join(REPO, "UI"), os.path.join(staging, "UI"))
    for relative in files:
        if not relative.startswith("UI/"):
            continue
        source = os.path.join(delivery, relative)
        target = os.path.join(staging, relative)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if relative.startswith("UI/Sprites/"):
            merged = merged_sprites(os.path.join(REPO, relative), source)
            if merged is not None:
                write_sprites(target, merged)
        else:
            shutil.copyfile(source, target)


def check_staging(staging, problems):
    report = ui_sources.Report(staging)
    ui_sources.read_sprites(staging, report)
    ui_sources.read_themes(staging, report)
    problems.extend(report.problems)

    relative = os.path.relpath(os.path.join(staging, "UI"), REPO).replace("\\", "/")
    result = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(REPO, "Tools", "Scripts", "ue.ps1"),
         "-Task", "uicheck", "-Path", relative],
        cwd=REPO, capture_output=True, text=True)
    lines = result.stdout.splitlines()
    problems.extend(line for line in lines if line.startswith("UI/Layouts/"))
    if result.returncode != 0 and not any(line.startswith("UI/Layouts/") for line in lines):
        problems.append("ue.ps1 -Task uicheck failed: " + " | ".join(lines[-10:] + result.stderr.splitlines()[-5:]))


def apply(files, delivery, staging):
    """Copies the delivery into the repository; the paths that changed."""
    changed = []
    for relative in files:
        if relative == "README.txt":
            continue
        source = os.path.join(staging if relative.startswith("UI/Sprites/") else delivery, relative)
        target = os.path.join(REPO, relative)
        if not os.path.isfile(source) or (os.path.isfile(target) and filecmp.cmp(source, target, shallow=False)):
            continue  # A sprite file that changes nothing is not staged.
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(source, target)
        changed.append(relative)
    return changed


def main():
    parser = argparse.ArgumentParser(description="Check a UI delivery ZIP and with --apply copy it into the repository.")
    parser.add_argument("zip")
    parser.add_argument("--apply", action="store_true", help="copy the delivery in when it has no problem")
    args = parser.parse_args()

    if not zipfile.is_zipfile(args.zip):
        print(f"PROBLEM: {args.zip}: is not a ZIP file")
        return 1
    stem = re.sub(r"[^A-Za-z0-9_-]", "_", os.path.splitext(os.path.basename(args.zip))[0])
    work = os.path.join(REPO, "Saved", "UIIntake", stem)
    if os.path.isdir(work):
        shutil.rmtree(work)
    delivery = os.path.join(work, "delivery")
    staging = os.path.join(work, "staging")

    problems = []
    files = unpack(args.zip, delivery, problems)
    print(f"DELIVERY: {len(files)} file(s) in {os.path.relpath(delivery, REPO)}")
    for relative in files:
        print(f"  {relative}")
    check_places(files, delivery, problems)
    if not problems:
        stage(files, delivery, staging)
        check_staging(staging, problems)

    if problems:
        for problem in problems:
            print(f"PROBLEM: {problem}")
        print(f"INTAKE FAILED: {len(problems)} problem(s), nothing copied")
        return 1
    if not args.apply:
        print("INTAKE CHECKED: no problems, nothing copied (run with --apply to copy)")
        return 0

    changed = apply(files, delivery, staging)
    for relative in changed:
        print(f"COPIED: {relative}")
    if any(relative.startswith(SETUP_FOLDERS) for relative in changed):
        print("SETUP NEEDED")
    if "README.txt" in files:
        print(f"README: {os.path.relpath(os.path.join(delivery, 'README.txt'), REPO)}")
    print(f"INTAKE APPLIED: {len(changed)} file(s) changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
