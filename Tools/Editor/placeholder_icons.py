"""Draws a placeholder icon for every entity whose Icon.<Entity> resolves to no painted art, for setup_testmap.py, which
runs it before it turns UI/ into Common UI styles (see docs/ui/DESIGNER_HANDBOOK.md, section 5).

Each placeholder is UI/Textures/Common/Icons/T_Icon_Common_<Entity>.png, 256x256: a background colour and a simple
silhouette per kind, the entity's initials and a hatched corner mark, so nobody takes it for final art. A text chunk in
the PNG marks it as generated; the Common.Icon.<Entity> entries of UI/Sprites/Common.sprites.json pointing at these
files are kept in one sorted block, the hand-written entries are left as they are.

Painted art always wins: an Icon.<Entity> in the entity's faction set, or a Common.Icon.<Entity> whose PNG does not
carry the marker, is never touched, and with painted faction art the placeholder and its entry are removed. They come
back once the art is gone. Placeholders of entities that no longer exist are removed too. A file is only written when
its content changes, so a second run writes nothing.

No Unreal here, so it runs anywhere: python -I Tools/Editor/placeholder_icons.py --check runs the self-check.
"""

import hashlib
import inspect
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_sources  # noqa: E402

SIZE = 256
ICON_DIR = "Textures/Common/Icons"
SPRITE_FILE = os.path.join("Sprites", "Common.sprites.json")
# tEXt keyword and text that mark a generated placeholder; the text ends in the hash of what drew it.
MARKER_KEY = b"Comment"
MARKER_TEXT = "World Divide placeholder icon "

INFANTRY, VEHICLE, AIRCRAFT, BUILDING, DEFENSE, UPGRADE, WORKER = (
    "infantry", "vehicle", "aircraft", "building", "defense", "upgrade", "worker")
# Background per kind; the silhouette, initials and edge are drawn in light colours on top.
KIND_COLORS = {
    INFANTRY: (74, 93, 35),
    VEHICLE: (46, 74, 104),
    AIRCRAFT: (30, 104, 116),
    BUILDING: (110, 84, 52),
    DEFENSE: (120, 38, 38),
    UPGRADE: (88, 54, 120),
    WORKER: (150, 98, 20),
}
GLYPH_COLOR = (236, 234, 222)
EDGE_COLOR = (20, 22, 24)
MARK_COLORS = ((242, 196, 32), (24, 24, 24))

# 5x7 capitals and digits, a row per string, for the initials.
FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01110", "10001", "10000", "10000", "10000", "10001", "01110"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01110", "10001", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("01110", "00100", "00100", "00100", "00100", "00100", "01110"),
    "J": ("00111", "00010", "00010", "00010", "00010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "10001", "11001", "10101", "10011", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "11110", "00001", "00001", "10001", "01110"),
    "6": ("00110", "01000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00010", "01100"),
}


class Canvas:
    """RGB pixels with shapes filled at 2x2 samples a pixel, each only over its bounding box."""

    SAMPLES = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75))

    def __init__(self, color):
        self.pixels = [[list(color) for _ in range(SIZE)] for _ in range(SIZE)]

    def fill(self, inside, box, color):
        x0, y0, x1, y1 = (max(0, min(SIZE, int(v))) for v in (box[0], box[1], box[2] + 1, box[3] + 1))
        for y in range(y0, y1):
            row = self.pixels[y]
            for x in range(x0, x1):
                hits = sum(1 for dx, dy in self.SAMPLES if inside(x + dx, y + dy))
                if hits:
                    pixel = row[x]
                    for channel in range(3):
                        pixel[channel] += (color[channel] - pixel[channel]) * hits // 4

    def rect(self, x0, y0, x1, y1, color):
        self.fill(lambda x, y: x0 <= x < x1 and y0 <= y < y1, (x0, y0, x1, y1), color)

    def circle(self, cx, cy, r, color):
        self.fill(lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r, (cx - r, cy - r, cx + r, cy + r), color)

    def polygon(self, points, color):
        def inside(x, y):
            result = False
            for (ax, ay), (bx, by) in zip(points, points[1:] + points[:1]):
                if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
                    result = not result
            return result
        xs, ys = [p[0] for p in points], [p[1] for p in points]
        self.fill(inside, (min(xs), min(ys), max(xs), max(ys)), color)

    def line(self, ax, ay, bx, by, width, color):
        """A stroke with round ends."""
        dx, dy = bx - ax, by - ay
        length = dx * dx + dy * dy
        r = width / 2

        def inside(x, y):
            t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / length))
            return (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2 <= r * r
        self.fill(inside, (min(ax, bx) - r, min(ay, by) - r, max(ax, bx) + r, max(ay, by) + r), color)


# Silhouettes, drawn in the upper part of the icon (about 40..176 high), centred on x 128.

def draw_infantry(canvas, color):
    canvas.circle(128, 66, 22, color)
    canvas.polygon([(92, 176), (98, 110), (110, 96), (146, 96), (158, 110), (164, 176)], color)
    canvas.line(160, 120, 196, 84, 9, color)


def draw_vehicle(canvas, color):
    canvas.polygon([(52, 142), (60, 118), (196, 118), (204, 142)], color)
    canvas.polygon([(92, 118), (100, 92), (150, 92), (160, 118)], color)
    canvas.line(150, 104, 214, 98, 10, color)
    canvas.rect(48, 146, 208, 170, color)
    for x in range(64, 200, 24):
        canvas.circle(x, 158, 7, KIND_COLORS[VEHICLE])


def draw_aircraft(canvas, color):
    canvas.polygon([(128, 36), (140, 70), (142, 112), (214, 146), (214, 158), (142, 140), (138, 164), (162, 180),
                    (162, 188), (128, 180), (94, 188), (94, 180), (118, 164), (114, 140), (42, 158), (42, 146),
                    (114, 112), (116, 70)], color)


def draw_building(canvas, color):
    canvas.polygon([(48, 176), (48, 104), (84, 82), (84, 104), (120, 82), (120, 104), (156, 82), (156, 104),
                    (208, 104), (208, 176)], color)
    canvas.rect(176, 52, 196, 104, color)
    for x in (64, 104, 144):
        canvas.rect(x, 124, x + 22, 144, KIND_COLORS[BUILDING])


def draw_defense(canvas, color):
    canvas.polygon([(128, 38), (196, 62), (190, 128), (128, 184), (66, 128), (60, 62)], color)
    canvas.polygon([(128, 60), (176, 76), (172, 124), (128, 164)], KIND_COLORS[DEFENSE])


def draw_upgrade(canvas, color):
    for top in (44, 100):
        canvas.polygon([(128, top), (194, top + 56), (168, top + 74), (128, top + 40), (88, top + 74),
                        (62, top + 56)], color)


def draw_worker(canvas, color):
    canvas.rect(44, 92, 152, 156, color)
    canvas.polygon([(158, 156), (158, 104), (192, 104), (212, 130), (212, 156)], color)
    canvas.rect(172, 112, 192, 128, KIND_COLORS[WORKER])
    for x in (74, 122, 186):
        canvas.circle(x, 164, 16, color)
        canvas.circle(x, 164, 7, KIND_COLORS[WORKER])


DRAW = {
    INFANTRY: draw_infantry,
    VEHICLE: draw_vehicle,
    AIRCRAFT: draw_aircraft,
    BUILDING: draw_building,
    DEFENSE: draw_defense,
    UPGRADE: draw_upgrade,
    WORKER: draw_worker,
}


def initials(entity):
    """Up to three letters: the capitals of a CamelCase name ("FlakTruck" -> "FT"), else its first two letters."""
    capitals = re.findall(r"[A-Z0-9]", entity)
    letters = "".join(capitals) if len(capitals) >= 2 else entity[:2]
    return "".join(c for c in letters.upper() if c in FONT)[:3]


def draw_text(canvas, text, color):
    """The text in the font, 6 px a dot, centred in the band under the silhouette."""
    dot, gap = 6, 8
    width = len(text) * 5 * dot + (len(text) - 1) * gap
    x, y = (SIZE - width) // 2, 192
    for char in text:
        for row, bits in enumerate(FONT[char]):
            for column, bit in enumerate(bits):
                if bit == "1":
                    canvas.rect(x + column * dot, y + row * dot, x + (column + 1) * dot, y + (row + 1) * dot, color)
        x += 5 * dot + gap


def draw_icon(entity, kind):
    """The rows of the placeholder as bytes, top to bottom."""
    background = KIND_COLORS[kind]
    canvas = Canvas(background)
    DRAW[kind](canvas, GLYPH_COLOR)
    draw_text(canvas, initials(entity), GLYPH_COLOR)
    # A dark edge, so neighbouring icons of the same kind stay apart.
    for inset in range(4):
        canvas.rect(inset, inset, SIZE - inset, inset + 1, EDGE_COLOR)
        canvas.rect(inset, SIZE - inset - 1, SIZE - inset, SIZE - inset, EDGE_COLOR)
        canvas.rect(inset, inset, inset + 1, SIZE - inset, EDGE_COLOR)
        canvas.rect(SIZE - inset - 1, inset, SIZE - inset, SIZE - inset, EDGE_COLOR)
    # The placeholder mark: a hatched triangle in the top-right corner.
    corner = 40
    for index, offset in enumerate(range(0, corner, 8)):
        band = [(SIZE - corner + offset, 0), (SIZE - corner + offset + 8, 0), (SIZE, corner - offset - 8),
                (SIZE, corner - offset)]
        canvas.polygon(band, MARK_COLORS[index % 2])
    return [bytes(channel for pixel in row for channel in pixel) for row in canvas.pixels]


def drawing_hash(entity, kind):
    """Changes with the entity, its kind and the code that draws it."""
    digest = hashlib.sha1(f"{entity}|{kind}|{SIZE}".encode("utf-8"))
    for function in (Canvas, draw_icon, draw_text, initials, DRAW[kind]):
        digest.update(inspect.getsource(function).encode("utf-8"))
    digest.update(repr((KIND_COLORS, GLYPH_COLOR, EDGE_COLOR, MARK_COLORS, sorted(FONT.items()))).encode("utf-8"))
    return digest.hexdigest()


def png_bytes(rows, marker):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + row for row in rows)
    return b"".join((
        b"\x89PNG\r\n\x1a\n",
        chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)),
        chunk(b"tEXt", MARKER_KEY + b"\x00" + marker.encode("latin-1")),
        chunk(b"IDAT", zlib.compress(raw, 9)),
        chunk(b"IEND", b""),
    ))


def read_marker(path):
    """The marker text of a generated placeholder, or None for any other file (painted art)."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return None
    position = 8
    while position + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if kind == b"tEXt":
            key, _, text = body.partition(b"\x00")
            if key == MARKER_KEY and text.startswith(MARKER_TEXT.encode("latin-1")):
                return text.decode("latin-1")
        if kind in (b"IDAT", b"IEND"):
            return None
        position += 12 + length
    return None


def icon_relative(entity):
    return f"{ICON_DIR}/T_Icon_Common_{entity}.png"


def sprite_line(entity):
    return f'    "Common.Icon.{entity}": {{ "texture": "{icon_relative(entity)}", "size": [{SIZE}, {SIZE}] }}'


def entry_spans(lines):
    """(start, end, name) of every entry of the "sprites" object, one entry starting on each line indented by four."""
    spans = []
    for index, line in enumerate(lines):
        match = re.match(r'^    "([^"]+)"\s*:', line)
        if match:
            if spans:
                spans[-1][1] = index
            spans.append([index, None, match.group(1)])
        elif re.match(r"^  \}", line) and spans and spans[-1][1] is None:
            spans[-1][1] = index
    return [tuple(span) for span in spans]


def rewrite_sprites(text, remove, add):
    """The sprite file text with the Common.Icon.<Entity> entries in remove dropped and those of add (sorted) written
    as one block where the first dropped entry was, or after the last entry."""
    lines = text.split("\n")
    spans = entry_spans(lines)
    if not spans:
        raise RuntimeError(f"{SPRITE_FILE}: no sprite entries found")
    names = {f"Common.Icon.{entity}" for entity in remove | add}
    kept, insert_at = [], None
    for start, end, name in spans:
        if name in names:
            if insert_at is None:
                insert_at = len(kept)
            continue
        kept.append([line for line in lines[start:end]])
    block = [[sprite_line(entity)] for entity in sorted(add)]
    insert_at = len(kept) if insert_at is None else insert_at
    entries = kept[:insert_at] + block + kept[insert_at:]
    for entry in entries:
        entry[-1] = entry[-1].rstrip().rstrip(",")
    for entry in entries[:-1]:
        entry[-1] += ","
    head, tail = lines[:spans[0][0]], lines[spans[-1][1]:]
    return "\n".join(head + [line for entry in entries for line in entry] + tail)


def write_if_changed(path, data):
    try:
        with open(path, "rb") as handle:
            if handle.read() == data:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)
    return True


def generate(project_dir, entities, log=print):
    """entities: (entity, kind, faction set or None), entity being the <Entity> of its Icon.<Entity>. Writes, refreshes
    and removes the placeholders under UI/ and their sprite entries; returns the files changed, relative to UI/."""
    ui_dir = os.path.join(project_dir, "UI")
    sprite_sets = ui_sources.read_sprites(project_dir, ui_sources.Report(project_dir))
    common = sprite_sets.get(ui_sources.COMMON, {})
    changed = []

    wanted = {}
    for entity, kind, faction_set in entities:
        if faction_set and f"Icon.{entity}" in sprite_sets.get(faction_set, {}):
            continue
        painted = common.get(f"Icon.{entity}")
        if painted and read_marker(painted["textures"]["Normal"].path) is None:
            continue
        wanted[entity] = kind

    icon_dir = os.path.join(ui_dir, ICON_DIR)
    generated = set()
    if os.path.isdir(icon_dir):
        for name in os.listdir(icon_dir):
            match = re.match(r"^T_Icon_Common_(\w+)\.png$", name)
            if match and read_marker(os.path.join(icon_dir, name)) is not None:
                generated.add(match.group(1))
    for entity in sorted(generated - set(wanted)):
        os.remove(os.path.join(ui_dir, icon_relative(entity)))
        changed.append(icon_relative(entity))
        log(f"placeholder_icons: removed the placeholder of {entity}, it has painted art or is gone")

    for entity, kind in sorted(wanted.items()):
        path = os.path.join(ui_dir, icon_relative(entity))
        marker = MARKER_TEXT + drawing_hash(entity, kind)
        if read_marker(path) == marker:
            continue
        write_if_changed(path, png_bytes(draw_icon(entity, kind), marker))
        changed.append(icon_relative(entity))
        log(f"placeholder_icons: drew the {kind} placeholder of {entity}")

    sprite_path = os.path.join(ui_dir, SPRITE_FILE)
    with open(sprite_path, "rb") as handle:
        text = handle.read().decode("utf-8")
    # Keep the line endings the checkout has.
    newline = "\r\n" if "\r\n" in text else "\n"
    rewritten = rewrite_sprites(text.replace("\r\n", "\n"), generated - set(wanted), set(wanted))
    json.loads(rewritten)
    if write_if_changed(sprite_path, rewritten.replace("\n", newline).encode("utf-8")):
        changed.append(SPRITE_FILE.replace("\\", "/"))
    return changed


def check():
    """Self-check in a scratch project: painted art is kept, removing it brings the placeholder back, a second run
    writes nothing."""
    project = tempfile.mkdtemp(prefix="placeholder_icons_")
    try:
        sprites = os.path.join(project, "UI", "Sprites")
        os.makedirs(sprites)
        with open(os.path.join(sprites, "Common.sprites.json"), "w", encoding="utf-8") as handle:
            handle.write('{\n  "sprites": {\n'
                         '    "Common.Panel": { "texture": "Textures/Common/T_UI_Common_Panel.png", "size": [4, 4] }\n'
                         '  }\n}\n')
        panel = os.path.join(project, "UI", "Textures", "Common", "T_UI_Common_Panel.png")
        os.makedirs(os.path.dirname(panel))
        with open(panel, "wb") as handle:
            handle.write(png_bytes([b"\x00" * SIZE * 3] * SIZE, "")[:16] + struct.pack(">II", 4, 4))
        entities = [("Tank", VEHICLE, "USA"), ("Jet", AIRCRAFT, "USA"), ("PowerPlant", BUILDING, "USA")]
        quiet = lambda message: None  # noqa: E731

        def sprite_names():
            with open(os.path.join(sprites, "Common.sprites.json"), encoding="utf-8") as handle:
                return set(json.load(handle)["sprites"])

        first = generate(project, entities, quiet)
        assert len(first) == 4, first
        assert {"Common.Icon.Tank", "Common.Icon.Jet", "Common.Icon.PowerPlant", "Common.Panel"} == sprite_names()
        assert ui_sources.png_size(os.path.join(project, "UI", icon_relative("Tank"))) == (SIZE, SIZE)
        tank_rows = draw_icon("Tank", VEHICLE)
        assert tank_rows != draw_icon("PowerPlant", BUILDING) and tank_rows != draw_icon("Tower", VEHICLE)
        assert generate(project, entities, quiet) == [], "the second run writes nothing"

        # Painted faction art for the tank: its placeholder goes, the others stay.
        painted = os.path.join(project, "UI", "Textures", "USA", "Icons", "T_Icon_USA_Tank.png")
        os.makedirs(os.path.dirname(painted))
        shutil.copy(panel, painted)
        with open(os.path.join(sprites, "USA.sprites.json"), "w", encoding="utf-8") as handle:
            json.dump({"sprites": {"USA.Icon.Tank": {"texture": "Textures/USA/Icons/T_Icon_USA_Tank.png",
                                                     "size": [4, 4]}}}, handle)
        generate(project, entities, quiet)
        assert not os.path.exists(os.path.join(project, "UI", icon_relative("Tank")))
        assert "Common.Icon.Tank" not in sprite_names() and "Common.Icon.Jet" in sprite_names()
        assert os.path.exists(painted), "painted art is never touched"

        # Painted Common art for the jet replaces the placeholder file: it is kept as it is.
        jet = os.path.join(project, "UI", icon_relative("Jet"))
        shutil.copy(panel, jet)
        with open(os.path.join(sprites, "Common.sprites.json"), encoding="utf-8") as handle:
            text = handle.read().replace(f'"Common.Icon.Jet": {{ "texture": "{icon_relative("Jet")}", "size": [256, 256] }}',
                                         f'"Common.Icon.Jet": {{ "texture": "{icon_relative("Jet")}", "size": [4, 4] }}')
        with open(os.path.join(sprites, "Common.sprites.json"), "w", encoding="utf-8") as handle:
            handle.write(text)
        assert generate(project, entities, quiet) == []
        assert read_marker(jet) is None

        # The painted tank is removed: its placeholder comes back.
        os.remove(os.path.join(sprites, "USA.sprites.json"))
        generate(project, entities, quiet)
        assert read_marker(os.path.join(project, "UI", icon_relative("Tank"))) is not None
        assert "Common.Icon.Tank" in sprite_names()

        # An entity that is gone loses its placeholder.
        generate(project, entities[:1] + [("Jet", AIRCRAFT, "USA")], quiet)
        assert "Common.Icon.PowerPlant" not in sprite_names()
        assert generate(project, entities[:2], quiet) == []
    finally:
        shutil.rmtree(project, ignore_errors=True)
    print("placeholder_icons: check passed")


if __name__ == "__main__":
    if sys.argv[1:] == ["--check"]:
        check()
    else:
        print("usage: python -I Tools/Editor/placeholder_icons.py --check")
