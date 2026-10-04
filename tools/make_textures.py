#!/usr/bin/env python3
"""Generate the chamber textures into textures/ (32x32 RGB PNGs).

Deterministic: the same script writes the same bytes, so the PNGs are
committed and a clone builds without Pillow. `make textures` reruns it.
"""

import os
import random

from PIL import Image

N = 32
OUT = os.path.join(os.path.dirname(__file__), "..", "textures")


def clamp(v):
    return max(0, min(255, int(v)))


def panel(base, seam, noise, rng):
    """A square panel: flat colour with a little grain, a dark seam round the
    edge, and a lighter bevel just inside it."""
    img = Image.new("RGB", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            g = rng.uniform(-noise, noise)
            c = [b + g for b in base]
            edge = min(x, y, N - 1 - x, N - 1 - y)
            if edge == 0:
                c = seam
            elif edge == 1:
                c = [v + 14 for v in c]
            px[x, y] = tuple(clamp(v) for v in c)
    return img


def metal(rng):
    """Dark plate with diagonal brushing and a rivet in each corner."""
    img = panel((64, 68, 74), (30, 32, 36), 3, rng)
    px = img.load()
    for y in range(2, N - 2):
        for x in range(2, N - 2):
            if (x + y) % 6 == 0:
                r, g, b = px[x, y]
                px[x, y] = (clamp(r + 8), clamp(g + 8), clamp(b + 8))
    for cx, cy in ((4, 4), (N - 5, 4), (4, N - 5), (N - 5, N - 5)):
        px[cx, cy] = (120, 124, 130)
        px[cx + 1, cy + 1] = (40, 42, 46)
    return img


def goo(rng):
    """Toxic sludge: blotchy brown-green."""
    img = Image.new("RGB", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            v = rng.uniform(0, 1)
            px[x, y] = (clamp(80 + 30 * v), clamp(70 + 30 * v), clamp(20 + 10 * v))
    for _ in range(10):
        cx, cy, r = rng.randrange(N), rng.randrange(N), rng.randrange(2, 5)
        for y in range(-r, r + 1):
            for x in range(-r, r + 1):
                if x * x + y * y <= r * r:
                    px[(cx + x) % N, (cy + y) % N] = (140, 120, 30)
    return img


def exit_pad(rng):
    """Green glow with chevrons."""
    img = Image.new("RGB", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            stripe = (y + abs(x - N // 2)) % 12 < 4
            g = 220 if stripe else 150
            px[x, y] = (40, g, clamp(70 + rng.uniform(-8, 8)))
    return img


def cube(rng):
    """Weighted storage cube face: grey plate, dark rim, a ring in the middle."""
    img = panel((150, 154, 160), (60, 62, 68), 3, rng)
    px = img.load()
    c = (N - 1) / 2
    for y in range(N):
        for x in range(N):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            if 7 <= d <= 10:
                px[x, y] = (205, 210, 215)
            elif d < 5:
                px[x, y] = (110, 180, 230)
            # dark corner plates
            if (x < 7 or x > N - 8) and (y < 7 or y > N - 8) and min(x, y, N - 1 - x, N - 1 - y) > 0:
                px[x, y] = (80, 82, 88)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    rng = random.Random(0x9047A1)
    textures = {
        "white.png": panel((214, 214, 206), (110, 110, 108), 4, rng),
        "metal.png": metal(rng),
        "goo.png": goo(rng),
        "exit.png": exit_pad(rng),
        "cube.png": cube(rng),
    }
    for name, img in textures.items():
        img.save(os.path.join(OUT, name), optimize=True)
        print("wrote textures/" + name)


if __name__ == "__main__":
    main()
