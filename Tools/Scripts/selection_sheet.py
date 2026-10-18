"""Lays the selection panel's screenshots (#241) out on one labelled contact sheet, the mockup of docs/ui/specs/Selection.md.

    python Tools/Scripts/selection_sheet.py [--faction USA] [--size 1920x1080] [--out docs/ui/mockups/Mockup_Selection_CleanMinimal_USA.png]

Reads the crops ue.ps1 -Task uishots -Panel Selection@<Scene> left in Saved/UIShots; take those first. Scenes without
shots are left out and named. Not with python -I: Pillow comes from the user site-packages.
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SCENES = [
    ("Hero0", "0 upgrades: the row collapses"), ("Hero1", "1 upgrade"), ("Hero2", "2 upgrades"), ("Hero3", "3 upgrades"),
    ("Hero4", "4 upgrades"), ("Hero5", "5 upgrades"), ("Hero6", "6 upgrades: the default slots"),
    ("Hero9", "9 upgrades: 6 icons and +3"), ("LongLabel", "Long localised labels"), ("Infantry", "Infantry: icon, no art"),
    ("Vehicle", "Vehicle: icon, no art"), ("Aircraft", "Aircraft: compact, ammo below"), ("Building", "Building: compact, queue below"),
]
COLUMNS = 3
GAP = 24
LABEL = 28
BACKGROUND = (12, 16, 22)
TEXT = (230, 233, 238)


def main():
    parser = argparse.ArgumentParser(description="Make the selection panel's contact sheet from its screenshots.")
    parser.add_argument("--faction", default="USA")
    parser.add_argument("--size", default="1920x1080")
    parser.add_argument("--out", default=os.path.join("docs", "ui", "mockups", "Mockup_Selection_CleanMinimal_USA.png"))
    args = parser.parse_args()

    shots, missing = [], []
    for scene, label in SCENES:
        path = os.path.join(REPO, "Saved", "UIShots", f"Selection@{scene}", f"{args.faction}_{args.size}_crop.png")
        if os.path.isfile(path):
            shots.append((Image.open(path).convert("RGB"), label))
        else:
            missing.append(scene)
    if not shots:
        print("NO SHOTS: run ue.ps1 -Task uishots -Panel Selection@<Scene> first")
        return 1
    width = max(image.width for image, _ in shots)
    height = max(image.height for image, _ in shots)
    rows = (len(shots) + COLUMNS - 1) // COLUMNS
    sheet = Image.new("RGB", (GAP + COLUMNS * (width + GAP), GAP + rows * (height + LABEL + GAP)), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("arial.ttf", 18)
    except OSError:
        font = ImageFont.load_default()
    for index, (image, label) in enumerate(shots):
        x = GAP + (index % COLUMNS) * (width + GAP)
        y = GAP + (index // COLUMNS) * (height + LABEL + GAP)
        draw.text((x, y), label, fill=TEXT, font=font)
        sheet.paste(image, (x, y + LABEL))
    out = os.path.join(REPO, args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out, optimize=True)
    print(f"SHEET: {os.path.relpath(out, REPO)} with {len(shots)} scenes")
    if missing:
        print(f"MISSING: {', '.join(missing)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
