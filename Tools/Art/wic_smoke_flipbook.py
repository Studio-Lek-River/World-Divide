"""Turns World in Conflict's six-way lit smoke flipbook (rotatesmokefill_6p, the smoke of its tank guns) into the layout of
our muzzle smoke's flipbooks, for import_local_art.py to draw the muzzle smoke with on this machine (#355). Massive's
art: it is written to Art/Source/Local/WiC/Smoke (gitignored) and never committed or shipped.

  python Tools/Art/wic_smoke_flipbook.py "<World in Conflict>/wic1.sdf"

The archive keeps each texture as bare pixels (extract_sdf.py): 1024 x 1024 with one smaller mip after it, 16 x 16
frames of 64 px row by row from the top left. 0pos holds the smoke lit from the right, from above and from the front in
its R, G and B and the opacity in A; 1neg (no alpha) lit from the left, from below and from behind. Both are stored
B, G, R(, A). They become T_WiCSmoke_RLT.png (right, left, top, opacity) and T_WiCSmoke_BBF.png (bottom, back, front),
the channels muzzle_fx_blender.py bakes ours into, so M_MuzzleSmoke shades both the same.

Plain desktop Python with numpy and Pillow.
"""

import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import extract_sdf  # noqa: E402

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Local", "WiC", "Smoke")
SIZE = 1024
POSITIVE = "rotatesmokefill_6p_0pos_64x64_256.dds"
NEGATIVE = "rotatesmokefill_6p_1neg_64x64_256.dds"


def top_mip(data, channels):
    """The largest mip of a bare B, G, R(, A) texture as R, G, B(, A)."""
    pixels = np.frombuffer(data[:SIZE * SIZE * channels], np.uint8).reshape(SIZE, SIZE, channels)
    return pixels[..., [2, 1, 0, 3][:channels]]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    positive = top_mip(extract_sdf.read(sys.argv[1], POSITIVE), 4)
    negative = top_mip(extract_sdf.read(sys.argv[1], NEGATIVE), 3)
    os.makedirs(OUT_DIR, exist_ok=True)
    opacity = positive[..., 3]
    rlt = np.stack([positive[..., 0], negative[..., 0], positive[..., 1], opacity], axis=-1)
    bbf = np.stack([negative[..., 1], negative[..., 2], positive[..., 2], np.full_like(opacity, 255)], axis=-1)
    for name, pixels in (("T_WiCSmoke_RLT", rlt), ("T_WiCSmoke_BBF", bbf)):
        path = os.path.join(OUT_DIR, f"{name}.png")
        Image.fromarray(pixels, "RGBA").save(path)
        print(path)


if __name__ == "__main__":
    main()
