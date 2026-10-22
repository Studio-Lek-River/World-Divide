"""Draws the national flags of Art/Shared/flags/ that are made from their official specification rather than painted
(#351), as PNG textures the shared waving cloth (M_Flag_Cloth) maps from hoist (U 0) to fly (U 1), top (V 0) to bottom:

  python Tools/Art/flag_textures.py

- usa.png, the flag of the United States as Executive Order 10834 specifies it: hoist A = 1.0, fly B = 1.9, thirteen
  stripes of A/13 starting and ending red, the union C = 7/13 of the hoist (over seven stripes) by D = 0.76 at the hoist's
  top, and fifty five-pointed stars of diameter K = 0.0616 in nine rows, six and five alternating, spaced E = F = 0.054
  down and G = H = 0.063 across. Colours are Old Glory Red and Old Glory Blue as commonly given in sRGB.

Pure Python 3, no packages: the image is drawn in the flag's own units (so the stars stay round whatever the texture's
size), each pixel the share of its 4 x 4 samples inside each shape, and written as an 8-bit RGB PNG with zlib. The output
is committed; rerun only when a drawing changes. A flag can as well be any PNG of one's own listed in flags.json.
"""

import math
import os
import struct
import zlib

FLAGS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Art", "Shared", "flags")

# Power-of-two sides, so the texture keeps its mips and streams; the flag's 1.9 is the cloth's width, not the image's.
WIDTH, HEIGHT = 2048, 1024
SAMPLES = 4

# Executive Order 10834, in units of the hoist.
HOIST = 1.0
FLY = 1.9
STRIPES = 13
UNION_HEIGHT = 7.0 / 13.0
UNION_WIDTH = 0.76
STAR_DIAMETER = 0.0616
STAR_DOWN = 0.054
STAR_ACROSS = 0.063
STAR_ROWS = 9

RED = (178, 34, 52)
WHITE = (255, 255, 255)
BLUE = (60, 59, 110)


def star_centres():
    """The fifty stars' centres (x along the fly, y down from the top), rows of six and five in turn."""
    centres = []
    for row in range(STAR_ROWS):
        y = STAR_DOWN * (row + 1)
        columns = range(6) if row % 2 == 0 else range(5)
        for column in columns:
            x = STAR_ACROSS * (2 * column + (1 if row % 2 == 0 else 2))
            centres.append((x, y))
    if len(centres) != 50:
        raise SystemExit(f"{len(centres)} stars, not 50")
    return centres


def star_polygon(cx, cy, radius):
    """A five-pointed star, one point up, as ten corners: outer radius radius, inner radius radius * 0.381966."""
    inner = radius * math.sin(math.radians(18.0)) / math.sin(math.radians(126.0))
    corners = []
    for index in range(10):
        angle = math.radians(-90.0 + 36.0 * index)
        r = radius if index % 2 == 0 else inner
        corners.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    return corners


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


def draw_usa():
    """The US flag as rows of RGB bytes."""
    scale_x, scale_y = FLY / WIDTH, HOIST / HEIGHT
    offsets = [(i + 0.5) / SAMPLES for i in range(SAMPLES)]
    stars = [star_polygon(x, y, STAR_DIAMETER / 2.0) for x, y in star_centres()]
    # Which stars reach into each pixel row and column, so each pixel tests only those.
    pixel_stars = {}
    for star in stars:
        xs, ys = [c[0] for c in star], [c[1] for c in star]
        for py in range(int(min(ys) / scale_y), min(HEIGHT, int(max(ys) / scale_y) + 1)):
            for px in range(int(min(xs) / scale_x), min(WIDTH, int(max(xs) / scale_x) + 1)):
                pixel_stars.setdefault((px, py), []).append(star)

    rows = []
    for py in range(HEIGHT):
        # The share of red in the row's samples: the stripes only change along y.
        red = sum(int((py + oy) * scale_y * STRIPES / HOIST) % 2 == 0 for oy in offsets) / SAMPLES
        stripe = tuple(red * r + (1.0 - red) * w for r, w in zip(RED, WHITE))
        union_rows = sum((py + oy) * scale_y < UNION_HEIGHT for oy in offsets) / SAMPLES
        row = bytearray()
        for px in range(WIDTH):
            union_columns = sum((px + ox) * scale_x < UNION_WIDTH for ox in offsets) / SAMPLES
            union = union_rows * union_columns
            color = stripe
            if union > 0.0:
                # Within the union the stars are white on blue; at its edges the stripe shows through.
                white = 0.0
                for star in pixel_stars.get((px, py), ()):
                    white += sum(inside(star, (px + ox) * scale_x, (py + oy) * scale_y)
                                 for oy in offsets for ox in offsets) / (SAMPLES * SAMPLES)
                white = min(white, 1.0)
                field = tuple(white * w + (1.0 - white) * b for w, b in zip(WHITE, BLUE))
                color = tuple(union * f + (1.0 - union) * s for f, s in zip(field, stripe))
            row.extend(int(round(channel)) for channel in color)
        rows.append(bytes(row))
    return rows


def write_png(path, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + row for row in rows)
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    with open(path, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    os.makedirs(FLAGS_DIR, exist_ok=True)
    path = os.path.join(FLAGS_DIR, "usa.png")
    write_png(path, draw_usa())
    print(f"Wrote {os.path.normpath(path)}: {WIDTH} x {HEIGHT}, 13 stripes, 50 stars")


if __name__ == "__main__":
    main()
