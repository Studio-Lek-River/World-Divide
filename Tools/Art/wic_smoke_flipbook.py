"""Turns World in Conflict's six-way lit smoke flipbook (rotatesmokefill_6p, the smoke of its tank guns) into the layout of
our muzzle smoke's flipbooks, for import_local_art.py to draw the muzzle smoke with on this machine (#355). Massive's
art: it is written to Art/Source/Local/WiC/Smoke (gitignored) and never committed or shipped.

  python Tools/Art/wic_smoke_flipbook.py "<World in Conflict>/wic1.sdf"

WiC's tanks draw the 64 px frames of rotatesmokefill_6p_*_64x64_256; this takes its 128 px set (_128x128_128, the same
smoke in half the frames) and scales it up twice with a Lanczos filter to 256 px frames, the size of ours. Scaling adds
no detail the frames do not have, but it keeps their edges smooth where the sprite is drawn larger than 128 px.

The archive keeps each texture as bare pixels (extract_sdf.py): 2048 x 1024 with one smaller mip after it, 16 x 8
frames of 128 px row by row from the top left. 0pos holds the smoke lit from the right, from above and from the front in
its R, G and B and the opacity in A; 1neg (no alpha) lit from the left, from below and from behind. Both are stored
B, G, R(, A). They become T_WiCSmoke_RLT.png (right, left, top, opacity) and T_WiCSmoke_BBF.png (bottom, back, front),
4096 x 2048, the channels muzzle_fx_blender.py bakes ours into, so M_MuzzleSmoke shades both the same.

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
WIDTH, HEIGHT = 2048, 1024
UPSCALE = 2
POSITIVE = "rotatesmokefill_6p_0pos_128x128_128.dds"
NEGATIVE = "rotatesmokefill_6p_1neg_128x128_128.dds"


def top_mip(data, channels):
    """The largest mip of a bare B, G, R(, A) texture as R, G, B(, A)."""
    pixels = np.frombuffer(data[:WIDTH * HEIGHT * channels], np.uint8).reshape(HEIGHT, WIDTH, channels)
    return pixels[..., [2, 1, 0, 3][:channels]]


def upscale(channel):
    """One channel scaled up UPSCALE times, on its own so the light channels are not weighted by the opacity."""
    size = (WIDTH * UPSCALE, HEIGHT * UPSCALE)
    return np.asarray(Image.fromarray(channel, "L").resize(size, Image.Resampling.LANCZOS))


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
        Image.fromarray(np.stack([upscale(np.ascontiguousarray(pixels[..., i])) for i in range(4)], axis=-1), "RGBA").save(path)
        print(path)


if __name__ == "__main__":
    main()
