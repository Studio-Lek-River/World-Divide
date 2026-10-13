"""Reads the UI look source files, UI/Sprites/*.sprites.json and UI/Themes/*.theme.json with their PNGs and fonts (see
docs/ui/DESIGNER_HANDBOOK.md, sections 3 and 4), for setup_testmap.py, which turns them into Common UI styles.

The base UI/ folder comes first, then Mods/*/UI/ in alphabetical order: a later sprite replaces the sprite of the same
name, a later theme colour or font the one of the same name. Every theme is then filled from Neutral.

A problem (a file that does not parse, an unknown key, a missing file, a PNG whose size is not the declared one) is
added to the report with the file and the key, and only that entry is skipped. No Unreal here, so it runs anywhere.
"""

import json
import os
import re
import struct

COMMON = "Common"
NEUTRAL = "Neutral"
STATES = ("Normal", "Hover", "Pressed", "Disabled", "Selected")
COLORS = ("Primary", "Secondary", "Accent", "Panel", "Text", "MutedText", "Disabled", "Warning", "Danger", "Positive")
FONTS = ("Title", "Body", "Number")
SPRITE_KEYS = {"texture", "states", "size", "margin", "tint", "frames", "frameSize", "fps"}
FONT_KEYS = {"file", "size"}
FONT_EXTENSIONS = (".ttf", ".otf")
# Folder and file names that make valid Unreal asset paths: no spaces, dots or other punctuation; a - becomes a _.
FILE_NAME = re.compile(r"^[A-Za-z0-9_-]+$")
# Colour names, which become part of the text style names.
COLOR_NAME = re.compile(r"^[A-Za-z0-9_]+$")


class Report:
    """The problems found, each as "<file>: <entry>: <key>: <problem>" with the file relative to the project."""

    def __init__(self, project_dir):
        self.project_dir = project_dir
        self.problems = []

    def add(self, file, entry, key, problem):
        parts = [os.path.relpath(file, self.project_dir).replace("\\", "/")]
        parts += [part for part in (entry, key) if part]
        self.problems.append(": ".join(parts + [problem]))


class SourceFile:
    """A PNG or font under a UI/ folder: its full path, the mod it comes from (None for the base) and its path relative
    to that UI/ folder."""

    def __init__(self, path, mod, relative):
        self.path = path
        self.mod = mod
        self.relative = relative

    def asset_directory(self):
        """Where its asset goes: /Game/UI/<relative folder>, or /Game/UI/Mods/<Mod>/<relative folder> for a mod."""
        folder = os.path.dirname(self.relative)
        parts = ("/Game/UI", f"Mods/{self.mod}" if self.mod else "", folder.replace("-", "_"))
        return "/".join(part for part in parts if part)

    def stem(self):
        """The file name without extension, a - turned into a _, to name its assets after."""
        return os.path.splitext(os.path.basename(self.relative))[0].replace("-", "_")


def ui_roots(project_dir):
    """(mod, UI folder) pairs in the order they apply: the base UI/ (mod None), then every Mods/<Mod>/UI/ by name."""
    roots = [(None, os.path.join(project_dir, "UI"))]
    mods_dir = os.path.join(project_dir, "Mods")
    if os.path.isdir(mods_dir):
        for mod in sorted(os.listdir(mods_dir), key=str.lower):
            root = os.path.join(mods_dir, mod, "UI")
            if os.path.isdir(root):
                roots.append((mod, root))
    return roots


def files_in(root, folder, suffix):
    directory = os.path.join(root, folder)
    if not os.path.isdir(directory):
        return []
    return sorted((os.path.join(directory, name) for name in os.listdir(directory) if name.endswith(suffix)),
                  key=str.lower)


def read_json(file, report):
    try:
        with open(file, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as error:
        report.add(file, None, None, f"cannot be read: {error}")
        return None
    if not isinstance(data, dict):
        report.add(file, None, None, "is not a JSON object")
        return None
    return data


def png_size(path):
    """(width, height) from a PNG's header, or None when it is not a PNG."""
    with open(path, "rb") as handle:
        header = handle.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", header[16:24])


def resolve_file(root, mod, relative, file, entry, key, report, extensions):
    """The SourceFile for a path relative to root, or None with a problem reported."""
    if not isinstance(relative, str) or not relative:
        report.add(file, entry, key, "must be a path relative to UI/")
        return None
    relative = relative.replace("\\", "/")
    path = os.path.normpath(os.path.join(root, relative))
    if os.path.isabs(relative) or not path.startswith(os.path.normpath(root) + os.sep):
        report.add(file, entry, key, f"{relative} is outside UI/")
        return None
    if not relative.lower().endswith(extensions):
        report.add(file, entry, key, f"{relative} is not a {' or '.join(extensions)} file")
        return None
    if not os.path.isfile(path):
        report.add(file, entry, key, f"{relative} does not exist")
        return None
    source = SourceFile(path, mod, relative)
    if not all(FILE_NAME.match(part) for part in [os.path.splitext(relative)[0].split("/")[-1]] + relative.split("/")[:-1]):
        report.add(file, entry, key, f"{relative}: use only letters, digits, _ and - in folder and file names")
        return None
    return source


def is_int_list(value, length, minimum=0):
    return (isinstance(value, list) and len(value) == length
            and all(isinstance(item, int) and not isinstance(item, bool) and item >= minimum for item in value))


def read_sprite(root, mod, file, name, entry, report):
    """One sprite entry as a dict, or None with every problem reported."""
    if not isinstance(entry, dict):
        report.add(file, name, None, "must be an object")
        return None
    ok = True
    for key in sorted(set(entry) - SPRITE_KEYS):
        report.add(file, name, key, "unknown key")
        ok = False
    if ("texture" in entry) == ("states" in entry):
        report.add(file, name, None, "needs either texture or states")
        return None
    size = entry.get("size")
    if not is_int_list(size, 2, 1):
        report.add(file, name, "size", "must be [width, height] in pixels")
        return None
    textures = {}
    if "texture" in entry:
        textures["Normal"] = resolve_file(root, mod, entry["texture"], file, name, "texture", report, (".png",))
        ok = ok and textures["Normal"] is not None
    else:
        states = entry["states"]
        if not isinstance(states, dict) or "Normal" not in states:
            report.add(file, name, "states", "must be an object with at least Normal")
            return None
        for state in sorted(set(states) - set(STATES)):
            report.add(file, name, f"states.{state}", "unknown state")
            ok = False
        for state in STATES:
            if state in states:
                textures[state] = resolve_file(root, mod, states[state], file, name, f"states.{state}", report, (".png",))
                ok = ok and textures[state] is not None
    for state, source in textures.items():
        if source is None:
            continue
        actual = png_size(source.path)
        key = "texture" if "texture" in entry else f"states.{state}"
        if actual is None:
            report.add(file, name, key, f"{source.relative} is not a PNG")
            ok = False
        elif list(actual) != size:
            report.add(file, name, key, f"{source.relative} is {actual[0]}x{actual[1]}, size says {size[0]}x{size[1]}")
            ok = False
    margin = entry.get("margin")
    if margin is not None and not (is_int_list(margin, 4) and margin[0] + margin[2] <= size[0]
                                   and margin[1] + margin[3] <= size[1]):
        report.add(file, name, "margin", "must be [left, top, right, bottom] in pixels, inside the size")
        ok = False
    tint = entry.get("tint", False)
    if not isinstance(tint, bool):
        report.add(file, name, "tint", "must be true or false")
        ok = False
    frames, frame_size, fps = entry.get("frames", 0), entry.get("frameSize", [0, 0]), entry.get("fps", 0)
    if "frames" in entry or "frameSize" in entry or "fps" in entry:
        if not (isinstance(frames, int) and frames >= 1 and is_int_list(frame_size, 2, 1)
                and isinstance(fps, (int, float)) and not isinstance(fps, bool) and fps > 0
                and frames * frame_size[0] <= size[0] and frame_size[1] <= size[1]):
            report.add(file, name, "frames", "frames, frameSize and fps go together: a row of frames inside the size")
            ok = False
    if not ok:
        return None
    return {
        "textures": textures,
        "size": tuple(size),
        "margin": tuple(margin) if margin is not None else None,
        "tint": tint,
        "frames": frames,
        "frame_size": tuple(frame_size),
        "fps": float(fps),
        "button": "states" in entry,
        "file": file,
    }


def read_sprites(project_dir, report):
    """{set: {name without the set prefix: sprite}} over the base and the mods, a later entry replacing an earlier one."""
    sets = {}
    for mod, root in ui_roots(project_dir):
        for file in files_in(root, "Sprites", ".sprites.json"):
            set_name = os.path.basename(file)[:-len(".sprites.json")]
            data = read_json(file, report)
            if data is None:
                continue
            for key in sorted(set(data) - {"sprites"}):
                report.add(file, None, key, "unknown key")
            sprites = data.get("sprites")
            if not isinstance(sprites, dict):
                report.add(file, None, "sprites", "must be an object")
                continue
            for full_name, entry in sprites.items():
                prefix, _, name = full_name.partition(".")
                if prefix != set_name or not name:
                    report.add(file, full_name, None, f"must be named {set_name}.<Name> in {os.path.basename(file)}")
                    continue
                sprite = read_sprite(root, mod, file, full_name, entry, report)
                if sprite is not None:
                    sets.setdefault(set_name, {})[name] = sprite
    return sets


def parse_color(value):
    """#RRGGBB or #RRGGBBAA as four bytes, or None."""
    if not isinstance(value, str) or not re.fullmatch(r"#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})", value):
        return None
    digits = value[1:] + ("FF" if len(value) == 7 else "")
    return tuple(int(digits[index:index + 2], 16) for index in range(0, 8, 2))


def srgb_to_linear(rgba):
    """Four bytes, the colour sRGB as a designer picks it, as linear floats the way FLinearColor(FColor) converts them."""
    def channel(byte):
        value = byte / 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    return (channel(rgba[0]), channel(rgba[1]), channel(rgba[2]), rgba[3] / 255.0)


def read_themes(project_dir, report):
    """{faction: {"colors": {name: bytes}, "fonts": {role: {"file": SourceFile, "size": int}}}}, every theme filled from
    Neutral, a later entry replacing an earlier one."""
    themes = {}
    neutral_file = None
    for mod, root in ui_roots(project_dir):
        for file in files_in(root, "Themes", ".theme.json"):
            faction = os.path.basename(file)[:-len(".theme.json")]
            if faction == NEUTRAL:
                neutral_file = file
            data = read_json(file, report)
            if data is None:
                continue
            theme = themes.setdefault(faction, {"colors": {}, "fonts": {}})
            for key in sorted(set(data) - {"colors", "fonts"}):
                report.add(file, None, key, "unknown key")
            colors = data.get("colors", {})
            if not isinstance(colors, dict):
                report.add(file, None, "colors", "must be an object")
                colors = {}
            for name, value in colors.items():
                color = parse_color(value)
                if not COLOR_NAME.match(name):
                    report.add(file, "colors", name, "use only letters, digits and _ in a colour name")
                elif color is None:
                    report.add(file, "colors", name, f"{value!r} is not #RRGGBB or #RRGGBBAA")
                else:
                    theme["colors"][name] = color
            fonts = data.get("fonts", {})
            if not isinstance(fonts, dict):
                report.add(file, None, "fonts", "must be an object")
                fonts = {}
            for role, entry in fonts.items():
                if role not in FONTS:
                    report.add(file, "fonts", role, f"unknown font role, use {', '.join(FONTS)}")
                    continue
                if not isinstance(entry, dict):
                    report.add(file, "fonts", role, "must be an object with file and size")
                    continue
                unknown = sorted(set(entry) - FONT_KEYS)
                for key in unknown:
                    report.add(file, f"fonts.{role}", key, "unknown key")
                size = entry.get("size")
                if not isinstance(size, int) or isinstance(size, bool) or size < 1:
                    report.add(file, f"fonts.{role}", "size", "must be a whole number of points")
                    continue
                source = resolve_file(root, mod, entry.get("file"), file, f"fonts.{role}", "file", report, FONT_EXTENSIONS)
                if source is not None and not unknown:
                    theme["fonts"][role] = {"file": source, "size": size}

    neutral = themes.get(NEUTRAL, {"colors": {}, "fonts": {}})
    if neutral_file is None:
        report.add(os.path.join(project_dir, "UI", "Themes", f"{NEUTRAL}.theme.json"), None, None, "does not exist")
    else:
        for name in COLORS:
            if name not in neutral["colors"]:
                report.add(neutral_file, "colors", name, "required in Neutral, which fills the other themes")
        for role in FONTS:
            if role not in neutral["fonts"]:
                report.add(neutral_file, "fonts", role, "required in Neutral, which fills the other themes")
    for faction, theme in themes.items():
        if faction != NEUTRAL:
            theme["colors"] = {**neutral["colors"], **theme["colors"]}
            theme["fonts"] = {**neutral["fonts"], **theme["fonts"]}
    return themes
