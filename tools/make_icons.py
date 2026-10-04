#!/usr/bin/env python3
"""Generate the launcher icons in metadata/ (16, 32 and 64 px PNGs).

A white panel wall with a blue portal on the left and an orange one on the
right. Drawn at 256 px and scaled down, so the small sizes stay clean.
Deterministic; the PNGs are committed. `make icons` reruns it.
"""

import os

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "metadata")
S = 256


def master():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Rounded dark tile with a 2x2 grid of white panels.
    d.rounded_rectangle((0, 0, S - 1, S - 1), radius=40, fill=(40, 42, 48, 255))
    m, g = 18, 10
    w = (S - 2 * m - g) // 2
    for i in range(2):
        for j in range(2):
            x, y = m + i * (w + g), m + j * (w + g)
            d.rounded_rectangle((x, y, x + w, y + w), radius=10, fill=(214, 214, 206, 255))
    # The two portals: tall ovals, a glowing rim round a dark inside.
    for cx, rim, core in ((80, (44, 140, 255), (12, 40, 80)), (176, (255, 138, 28), (80, 40, 8))):
        rw, rh = 42, 92
        d.ellipse((cx - rw, S // 2 - rh, cx + rw, S // 2 + rh), fill=rim + (255,))
        d.ellipse((cx - rw + 14, S // 2 - rh + 14, cx + rw - 14, S // 2 + rh - 14), fill=core + (255,))
    return img


def main():
    img = master()
    for size in (16, 32, 64):
        path = os.path.join(OUT, f"icon{size}.png")
        img.resize((size, size), Image.LANCZOS).save(path, optimize=True)
        print("wrote metadata/icon%d.png" % size)


if __name__ == "__main__":
    main()
