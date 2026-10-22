"""Draws the faces of River Crossing's road signs (#409) into Art/Shared/roadside/, the PNG textures the sign plates of
Tools/Art/roadside_blender.py map, and holds the plates' shapes both scripts share:

  python Tools/Art/roadside_textures.py

Five signs in the European (Vienna Convention) style at their normal size, drawn from their geometry, not copied from any
sign set:

- speed_50: a 600 mm disc, a red ring with a thin white rim, black 50 on white.
- warning: a 900 mm triangle point up, red border, black exclamation mark on white (other danger).
- give_way: a 900 mm triangle point down, a wider red border, white inside.
- priority_road: a 600 mm square on its corner, white with a black edge, a yellow square inside.
- place_name: a 1200 x 500 mm yellow plate with a black inner border and the fictional village name MERWEDAL.

The lettering is a stroke font of this script (round caps, DIN-like proportions), so no font file is needed. Each texture
covers its sign's frame in metres (FRAMES); the plate's UVs map the same frame, so U runs from the plate's left as seen from
the front (its -Y) and V from its top. Pure Python 3, no packages, written as 8-bit RGB PNG like flag_textures.py. The
output is committed; rerun only when a drawing changes.
"""

import math
import os
import struct
import zlib

ROADSIDE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Art", "Shared", "roadside")
SAMPLES = 3

RED = (187, 30, 16)
WHITE = (245, 245, 242)
BLACK = (22, 22, 22)
YELLOW = (240, 196, 0)
BACK = (150, 152, 150)

ROUND_DIAMETER = 0.6
TRIANGLE_SIDE = 0.9
DIAMOND_SIDE = 0.6
PLACE_NAME = (1.2, 0.5)
PLACE_NAME_TEXT = "MERWEDAL"

# Each sign's frame (width, height) in metres and its texture's size in pixels; the frame is centred on the plate.
FRAMES = {
    "speed_50": ((ROUND_DIAMETER, ROUND_DIAMETER), (512, 512)),
    "warning": ((TRIANGLE_SIDE, TRIANGLE_SIDE), (512, 512)),
    "give_way": ((TRIANGLE_SIDE, TRIANGLE_SIDE), (512, 512)),
    "priority_road": ((DIAMOND_SIDE * math.sqrt(2.0), DIAMOND_SIDE * math.sqrt(2.0)), (512, 512)),
    "place_name": (PLACE_NAME, (1024, 512)),
}

# The stroke font: polylines on a 4 x 6 grid, y up; one glyph's advance is GLYPH_ADVANCE.
GLYPH_ADVANCE = 5.4


def ellipse(cx, cy, rx, ry, start=0.0, end=360.0, steps=24):
    return [(cx + rx * math.cos(math.radians(start + (end - start) * i / steps)),
             cy + ry * math.sin(math.radians(start + (end - start) * i / steps))) for i in range(steps + 1)]


GLYPHS = {
    "0": [ellipse(2.0, 3.0, 2.0, 3.0)],
    "5": [[(3.9, 6.0), (0.4, 6.0), (0.1, 3.3)] + ellipse(1.9, 2.0, 2.1, 2.0, 140.0, -150.0, 20)],
    "!": [[(2.0, 6.0), (2.0, 2.0)], [(2.0, 0.2), (2.0, 0.1)]],
    "M": [[(0.0, 0.0), (0.0, 6.0), (2.0, 2.4), (4.0, 6.0), (4.0, 0.0)]],
    "E": [[(4.0, 6.0), (0.0, 6.0), (0.0, 0.0), (4.0, 0.0)], [(0.0, 3.0), (3.2, 3.0)]],
    "R": [[(0.0, 0.0), (0.0, 6.0), (2.6, 6.0)] + ellipse(2.6, 4.5, 1.4, 1.5, 90.0, -90.0, 10) + [(0.0, 3.0)],
          [(2.2, 3.0), (4.0, 0.0)]],
    "W": [[(0.0, 6.0), (1.0, 0.0), (2.0, 4.2), (3.0, 0.0), (4.0, 6.0)]],
    "D": [[(0.0, 0.0), (0.0, 6.0), (1.4, 6.0)] + ellipse(1.4, 3.0, 2.6, 3.0, 90.0, -90.0, 16) + [(0.0, 0.0)]],
    "A": [[(0.0, 0.0), (2.0, 6.0), (4.0, 0.0)], [(0.7, 2.0), (3.3, 2.0)]],
    "L": [[(0.0, 6.0), (0.0, 0.0), (4.0, 0.0)]],
}


# --- Shapes, in the frame's metres: y right as seen from the front, z up, origin at the frame's centre -----------------


def circle(radius, steps=48):
    return [(radius * math.cos(2.0 * math.pi * i / steps), radius * math.sin(2.0 * math.pi * i / steps)) for i in range(steps)]


def triangle(side, inset=0.0, down=False):
    """An equilateral triangle centred on its bounding box, shrunk by inset on every side (same incentre)."""
    height = side * math.sqrt(3.0) / 2.0
    inradius = side / (2.0 * math.sqrt(3.0))
    sign = -1.0 if down else 1.0
    centre = sign * (-height / 2.0 + inradius)
    reach = 2.0 * (inradius - inset)
    return [(reach * math.cos(math.radians(sign * 90.0 + 120.0 * i)), centre + reach * math.sin(math.radians(sign * 90.0 + 120.0 * i)))
            for i in range(3)]


def diamond(half_diagonal):
    return [(0.0, half_diagonal), (half_diagonal, 0.0), (0.0, -half_diagonal), (-half_diagonal, 0.0)]


def rectangle(width, height):
    return [(-width / 2.0, -height / 2.0), (width / 2.0, -height / 2.0), (width / 2.0, height / 2.0), (-width / 2.0, height / 2.0)]


def outline(sign):
    """The plate's outline the Blender script extrudes, counter-clockwise as seen from the front."""
    if sign == "speed_50":
        return circle(ROUND_DIAMETER / 2.0)
    if sign == "warning":
        return triangle(TRIANGLE_SIDE)
    if sign == "give_way":
        return triangle(TRIANGLE_SIDE, down=True)
    if sign == "priority_road":
        return diamond(DIAMOND_SIDE * math.sqrt(2.0) / 2.0)
    return rectangle(*PLACE_NAME)


def inside(polygon, x, y):
    """Even-odd test of the point against the polygon's edges."""
    result = False
    previous = polygon[-1]
    for corner in polygon:
        (x1, y1), (x2, y2) = previous, corner
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            result = not result
        previous = corner
    return result


def text_strokes(text, height, centre, stroke=0.8):
    """The text's segments in metres, centred on centre, and its stroke's half width."""
    unit = height / 6.0
    width = (len(text) - 1) * GLYPH_ADVANCE + 4.0
    left = centre[0] - width * unit / 2.0
    bottom = centre[1] - height / 2.0
    segments = []
    for index, char in enumerate(text):
        for line in GLYPHS[char]:
            points = [(left + (index * GLYPH_ADVANCE + x) * unit, bottom + y * unit) for x, y in line]
            segments += list(zip(points, points[1:]))
    return segments, stroke * unit / 2.0


def near(segments, half_width, x, y):
    for (x1, y1), (x2, y2) in segments:
        dx, dy = x2 - x1, y2 - y1
        length = dx * dx + dy * dy
        t = 0.0 if length == 0.0 else max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / length))
        px, py = x1 + t * dx - x, y1 + t * dy - y
        if px * px + py * py <= half_width * half_width:
            return True
    return False


def layers(sign):
    """The sign's layers bottom to top: (colour, test(y, z)); the last layer a point is in gives its colour."""
    def shape(polygon):
        return lambda y, z: inside(polygon, y, z)

    def text(segments, half_width):
        ys = [p[0] for s in segments for p in s]
        zs = [p[1] for s in segments for p in s]
        box = (min(ys) - half_width, max(ys) + half_width, min(zs) - half_width, max(zs) + half_width)
        return lambda y, z: box[0] <= y <= box[1] and box[2] <= z <= box[3] and near(segments, half_width, y, z)

    if sign == "speed_50":
        radius = ROUND_DIAMETER / 2.0
        return [(WHITE, shape(circle(radius))), (RED, shape(circle(radius - 0.008))), (WHITE, shape(circle(radius - 0.065))),
                (BLACK, text(*text_strokes("50", 0.22, (0.0, 0.0), 0.95)))]
    if sign == "warning":
        return [(WHITE, shape(triangle(TRIANGLE_SIDE))), (RED, shape(triangle(TRIANGLE_SIDE, 0.01))),
                (WHITE, shape(triangle(TRIANGLE_SIDE, 0.085))), (BLACK, text(*text_strokes("!", 0.34, (0.0, -0.08), 1.1)))]
    if sign == "give_way":
        return [(WHITE, shape(triangle(TRIANGLE_SIDE, down=True))), (RED, shape(triangle(TRIANGLE_SIDE, 0.01, True))),
                (WHITE, shape(triangle(TRIANGLE_SIDE, 0.12, True)))]
    if sign == "priority_road":
        half = DIAMOND_SIDE * math.sqrt(2.0) / 2.0
        return [(BLACK, shape(diamond(half))), (WHITE, shape(diamond(half - 0.012))), (YELLOW, shape(diamond(half * 0.55)))]
    width, height = PLACE_NAME
    return [(YELLOW, shape(rectangle(width, height))), (BLACK, shape(rectangle(width - 0.04, height - 0.04))),
            (YELLOW, shape(rectangle(width - 0.08, height - 0.08))),
            (BLACK, text(*text_strokes(PLACE_NAME_TEXT, 0.13, (0.0, 0.0), 0.85)))]


def draw(sign):
    (frame_w, frame_h), (width, height) = FRAMES[sign]
    stack = layers(sign)
    offsets = [(i + 0.5) / SAMPLES for i in range(SAMPLES)]
    rows = []
    for py in range(height):
        row = bytearray()
        for px in range(width):
            total = [0.0, 0.0, 0.0]
            for oy in offsets:
                z = frame_h / 2.0 - (py + oy) / height * frame_h
                for ox in offsets:
                    y = -frame_w / 2.0 + (px + ox) / width * frame_w
                    color = BACK
                    for layer_color, test in reversed(stack):
                        if test(y, z):
                            color = layer_color
                            break
                    for channel in range(3):
                        total[channel] += color[channel]
            row.extend(int(round(value / (SAMPLES * SAMPLES))) for value in total)
        rows.append(bytes(row))
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + row for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    os.makedirs(ROADSIDE_DIR, exist_ok=True)
    for sign in FRAMES:
        path = os.path.join(ROADSIDE_DIR, f"T_Sign_{sign}.png")
        write_png(path, *draw(sign))
        print(f"Wrote {os.path.normpath(path)}")


if __name__ == "__main__":
    main()
