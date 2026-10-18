"""Makes a unit, building or upgrade icon from a source picture and gives it to the entity
(docs/ui/DESIGNER_HANDBOOK.md, section 3).

    python Tools/Scripts/unit_icon.py <source.png> <Faction> <Entity> [--crop x,y,size]
    python Tools/Scripts/unit_icon.py <cutout.png> <Faction> <Entity> --art [--crop x,y,width]

The picture is cut to a square (the largest one in the middle, or the one --crop gives in source pixels), scaled to
256x256 and saved as UI/Textures/<Faction>/Icons/T_Icon_<Faction>_<Entity>.png. The entry "<Faction>.Icon.<Entity>" is
added to UI/Sprites/<Faction>.sprites.json, or replaced there. <Entity> is the name after "Icon." in the entity's
IconSprite, for example M20Guardian. Run ue.ps1 -Task setup afterwards: it imports the icon and removes the
entity's placeholder. Not with python -I: Pillow comes from the user site-packages.

--art makes the selection panel's hero art instead (#241): a 2:1 box (around what the cut-out shows, or the one --crop
gives with its height half its width) scaled to 512x256, its left and bottom edges faded out so it sits in the panel
without a frame, saved as UI/Textures/<Faction>/Art/T_Art_<Faction>_<Entity>.png with the entry "<Faction>.Art.<Entity>",
for the entity's HeroArtSprite Art.<Entity>. The source should be a transparent render, such as Tools/Art/m20_hero_render.py
makes.
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
ART_SIZE = (512, 256)
# How far in from the left and bottom edges the hero art fades from clear to solid, as a share of its width and height.
ART_FADE_LEFT = 0.10
ART_FADE_BOTTOM = 0.18


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


def art_box(picture, crop):
    """The 2:1 box to cut as (left, top, right, bottom): --crop, or the shown part's bounds with a little room, centred."""
    width, height = picture.size
    if crop is not None:
        left, top, box_width = crop
        box = (left, top, left + box_width, top + box_width // 2)
    else:
        shown = picture.getchannel("A").point(lambda alpha: 255 if alpha > 24 else 0).getbbox()
        if not shown:
            raise ValueError("the picture shows nothing: it is fully transparent")
        box_width = min(max(shown[2] - shown[0], 2 * (shown[3] - shown[1])) * 1.08, width, 2 * height)
        # Centred on what shows, moved back inside the picture where the room runs out.
        centre_x = min(max((shown[0] + shown[2]) / 2, box_width / 2), width - box_width / 2)
        centre_y = min(max((shown[1] + shown[3]) / 2, box_width / 4), height - box_width / 4)
        box = tuple(round(value) for value in (centre_x - box_width / 2, centre_y - box_width / 4,
                                               centre_x + box_width / 2, centre_y + box_width / 4))
    if box[0] < 0 or box[1] < 0 or box[2] > width or box[3] > height or box[2] <= box[0]:
        raise ValueError(f"the box {box} does not fit in the {width}x{height} picture")
    return box


def fade_edges(art):
    """Fades the left and bottom edges out, so the art blends into the panel instead of ending in a hard line."""
    width, height = art.size
    mask = Image.new("L", art.size, 255)
    pixels = mask.load()
    fade_x, fade_y = max(1, round(width * ART_FADE_LEFT)), max(1, round(height * ART_FADE_BOTTOM))
    for y in range(height):
        for x in range(width):
            pixels[x, y] = round(255 * min(1.0, x / fade_x) * min(1.0, (height - 1 - y) / fade_y))
    alpha = Image.composite(art.getchannel("A"), Image.new("L", art.size, 0), mask)
    art.putalpha(alpha)
    return art


def make_art(args, crop, parser):
    with Image.open(args.source) as source:
        picture = source.convert("RGBA")
    try:
        box = art_box(picture, crop)
    except ValueError as error:
        parser.error(str(error))
    art = fade_edges(picture.crop(box).resize(ART_SIZE, Image.Resampling.LANCZOS))

    texture = f"Textures/{args.faction}/Art/T_Art_{args.faction}_{args.entity}.png"
    path = os.path.join(REPO, "UI", *texture.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    art.save(path, optimize=True)

    sprites_file = os.path.join(REPO, "UI", "Sprites", f"{args.faction}.sprites.json")
    entries = load_sprites(sprites_file)
    entries[f"{args.faction}.Art.{args.entity}"] = {"texture": texture, "size": list(ART_SIZE)}
    write_sprites(sprites_file, entries)

    print(f"ART: UI/{texture} from the box {','.join(str(value) for value in box)}")
    print(f"SPRITE: {args.faction}.Art.{args.entity} in UI/Sprites/{args.faction}.sprites.json")
    print("SETUP NEEDED: Tools/Scripts/ue.ps1 -Task setup")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Make an entity icon from a source picture and add its sprite entry.")
    parser.add_argument("source", help="the source picture")
    parser.add_argument("faction", choices=FACTIONS)
    parser.add_argument("entity", help="the name after 'Icon.' in the entity's IconSprite, for example M20Guardian")
    parser.add_argument("--crop", help="the square to cut, in source pixels: x,y,size (with --art: x,y,width of the 2:1 box)")
    parser.add_argument("--art", action="store_true", help="make the selection panel's hero art instead of the icon")
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
    if args.art:
        return make_art(args, crop, parser)

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
