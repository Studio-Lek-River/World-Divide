"""Makes a unit, building or upgrade icon from a source picture and gives it to the entity
(docs/ui/DESIGNER_HANDBOOK.md, section 3).

    python Tools/Scripts/unit_icon.py <source.png> <Faction> <Entity> [--crop x,y,size]

The picture is cut to a square (the largest one in the middle, or the one --crop gives in source pixels), scaled to
256x256 and saved as UI/Textures/<Faction>/Icons/T_Icon_<Faction>_<Entity>.png. The entry "<Faction>.Icon.<Entity>" is
added to UI/Sprites/<Faction>.sprites.json, or replaced there. <Entity> is the name after "Icon." in the entity's
IconSprite, for example M20Guardian. Run ue.ps1 -Task setup afterwards: it imports the icon and removes the
entity's placeholder. Not with python -I: Pillow comes from the user site-packages.
"""

import argparse
import os
import re
import sys

from PIL import Image

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ui_intake import FACTIONS, load_sprites, write_sprites  # noqa: E402

SIZE = 256


def square(width, height, crop):
    """The square to cut as (left, top, right, bottom)."""
    if crop is None:
        side = min(width, height)
        left, top = (width - side) // 2, (height - side) // 2
    else:
        left, top, side = crop
    if left < 0 or top < 0 or side <= 0 or left + side > width or top + side > height:
        raise ValueError(f"the square {left},{top},{side} does not fit in the {width}x{height} picture")
    return left, top, left + side, top + side


def main():
    parser = argparse.ArgumentParser(description="Make an entity icon from a source picture and add its sprite entry.")
    parser.add_argument("source", help="the source picture")
    parser.add_argument("faction", choices=FACTIONS)
    parser.add_argument("entity", help="the name after 'Icon.' in the entity's IconSprite, for example M20Guardian")
    parser.add_argument("--crop", help="the square to cut, in source pixels: x,y,size")
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", args.entity):
        parser.error(f"entity '{args.entity}' must be PascalCase letters and digits, as in M20Guardian")
    crop = None
    if args.crop:
        try:
            crop = tuple(int(part) for part in args.crop.split(","))
        except ValueError:
            crop = ()
        if len(crop) != 3:
            parser.error("--crop takes x,y,size")
    sprites_file = os.path.join(REPO, "UI", "Sprites", f"{args.faction}.sprites.json")
    if not os.path.isfile(sprites_file):
        parser.error(f"no sprite file {os.path.relpath(sprites_file, REPO)}")

    with Image.open(args.source) as source:
        picture = source.convert("RGBA")
    try:
        box = square(picture.width, picture.height, crop)
    except ValueError as error:
        parser.error(str(error))
    icon = picture.crop(box).resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    if icon.getextrema()[3][0] == 255:
        icon = icon.convert("RGB")

    texture = f"Textures/{args.faction}/Icons/T_Icon_{args.faction}_{args.entity}.png"
    path = os.path.join(REPO, "UI", *texture.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    icon.save(path, optimize=True)

    entries = load_sprites(sprites_file)
    entries[f"{args.faction}.Icon.{args.entity}"] = {"texture": texture, "size": [SIZE, SIZE]}
    write_sprites(sprites_file, entries)

    print(f"ICON: UI/{texture} from the square {','.join(str(value) for value in box[:2])},{box[2] - box[0]}")
    print(f"SPRITE: {args.faction}.Icon.{args.entity} in UI/Sprites/{args.faction}.sprites.json")
    print("SETUP NEEDED: Tools/Scripts/ue.ps1 -Task setup")
    return 0


if __name__ == "__main__":
    sys.exit(main())
