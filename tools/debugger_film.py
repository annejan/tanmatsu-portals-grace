#!/usr/bin/env python3
"""A broken round's debugger, as a film's pictures: the debugger opening on
the draft, the cursor going to each repair (`repair:` in the round's file),
the change made, then off to play-test it -- drawn as main/editor.c draws
the debugger (editor_draw), its colours from main/chamber.c's legend, at the
badge's 800 x 480 with each pixel doubled.

    from debugger_film import frames
    frames(text, out_dir, "r06")  ->  [[png, seconds], ...]

tools/story_film.py splices them in before the round, after the desk.
"""

import glob
import os
import re

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLYPH = re.compile(r"\{'(.|\\'|\\\\)', \"([^\"]+)\", (GLYPH_\w+), (MAT_\w+), \d+, ('(?:.|\\')'|0), (0x[0-9A-Fa-f]+u|0)\}")
MOVE, LAYER, PICK, CHANGE = 0.07, 0.3, 0.5, 0.8  # seconds a step, a layer, a brush picked, a change made


def legend():
    """main/chamber.c's chamber_legend: {char: (name, kind, mat, key, colour)}."""
    with open(os.path.join(ROOT, "main", "chamber.c")) as f:
        src = f.read()
    out = {}
    for m in GLYPH.finditer(src):
        ch = m.group(1).replace("\\'", "'").replace("\\\\", "\\")
        key = None if m.group(5) == "0" else m.group(5)[1:-1].replace("\\'", "'")
        col = int(m.group(6)[:-1], 16) if m.group(6) != "0" else 0
        out.setdefault(ch, (m.group(2), m.group(3), m.group(4), key, col))
    return out


def rgb(c):
    return (c >> 16) & 255, (c >> 8) & 255, c & 255


def mono(size):
    paths = glob.glob("/usr/share/fonts/**/DejaVuSansMono.ttf", recursive=True)
    return ImageFont.truetype(paths[0], size) if paths else ImageFont.load_default()


def cells(lo_hi):
    lo, _, hi = lo_hi.partition("-")
    return range(int(lo), int(hi or lo) + 1)


def frames(text, out_dir, prefix):
    LEG = legend()
    head = dict(l.split(":", 1) for l in text.split("\n") if ":" in l and not l.startswith("layer"))
    W, H, D = map(int, head["size"].split())
    debug = head["debug"].split()
    budget, kinds = int(debug[0]), debug[1:]
    repairs = []
    for line in text.split("\n"):
        if line.startswith("repair:"):
            x, y, z, ft = line[7:].split()
            repairs += [(xx, yy, zz, ft[1]) for yy in cells(y) for zz in cells(z) for xx in cells(x)]
    lines, grid = text.split("\n"), {}
    for i, l in enumerate(lines):
        if re.fullmatch(r"layer \d+", l.strip()):
            grid[int(l.split()[1])] = [list(r) for r in lines[i + 1:i + 1 + D]]
        if l.strip() == "solution":
            break

    def get(x, y, z):
        row = grid.get(y, [[]] * D)[D - 1 - z]
        return row[x] if x < len(row) else "#"

    def colour(c, floor):
        g = LEG.get(c)
        if g is None:
            return rgb(0xFFFF00FF)
        return rgb(g[4]) if g[4] else rgb(0xFF2C2F36 if floor else 0xFF15161A)

    f10, f12, f18 = mono(12), mono(12), mono(18)
    gw, gh = 540, 440
    cs = min(gw // W, gh // D, 32)
    x0, y0 = 12 + (gw - cs * W) // 2, 24 + (gh - cs * D) // 2
    state = {"x": W // 2, "z": D // 2, "y": 1 if H > 1 else 0, "brush": kinds[0][1], "changes": [], "t": 0.0}
    out, n = [], [0]

    def draw(secs, msg=None):
        s = state
        img = Image.new("RGB", (800, 480), rgb(0xFF0C0D10))
        d = ImageDraw.Draw(img)
        for z in range(D):
            for x in range(W):
                c = get(x, s["y"], z)
                gb = LEG.get(get(x, s["y"] - 1, z))
                floor = s["y"] > 0 and gb is not None and gb[2] not in ("MAT_AIR", "MAT_FIZZ")
                px, py = x0 + x * cs, y0 + (D - 1 - z) * cs
                d.rectangle([px, py, px + cs - 2, py + cs - 2], fill=colour(c, floor))
                kind = LEG.get(c, (0, ""))[1]
                mx, my = px + cs / 2, py + cs / 2
                if kind in ("GLYPH_DOOR", "GLYPH_BUTTON") and cs >= 12:
                    d.text((px + cs * 0.25, py + cs * 0.05), c, fill=(0, 0, 0), font=mono(int(cs * 0.7)))
                if kind == "GLYPH_CUBE":
                    r = cs * 0.18
                    d.ellipse([mx - r, my - r, mx + r, my + r], fill=rgb(0xFF6EB4E6))
                if kind == "GLYPH_SPHERE":
                    r = cs * 0.3
                    d.ellipse([mx - r, my - r, mx + r, my + r], fill=(255, 255, 255))
                if kind == "GLYPH_TURRET":
                    r, r2 = cs * 0.3, cs * 0.1
                    d.ellipse([mx - r, my - r, mx + r, my + r], fill=(240, 240, 240))
                    d.ellipse([mx - r2, my - r2, mx + r2, my + r2], fill=rgb(0xFFFF2A1C))
        for (x, y, z) in s["changes"]:
            if y == s["y"]:
                px, py = x0 + x * cs, y0 + (D - 1 - z) * cs
                d.rectangle([px, py, px + cs - 2, py + cs - 2], outline=rgb(0xFF7CFF8A))
        px, py = x0 + s["x"] * cs, y0 + (D - 1 - s["z"]) * cs
        d.rectangle([px - 1, py - 1, px + cs - 1, py + cs - 1], outline=(110, 110, 110))
        d.rectangle([px - 2, py - 2, px + cs, py + cs], outline=(110, 110, 110))
        tx = 570
        d.text((tx, 8), "DEBUGGER", fill=rgb(0xFF7CFF8A), font=f18)
        d.text((tx, 32), "layer %d of %d  (Q/E)" % (s["y"], H - 1), fill=(255, 255, 255), font=f12)
        used = len(s["changes"])
        d.text((tx, 50), "changes: %d of %d" % (used, budget),
               fill=(255, 255, 255) if used < budget else rgb(0xFFFF7A6A), font=f12)
        d.text((tx, 74), "it may change:", fill=rgb(0xFFA0A0A8), font=f12)
        for j, k in enumerate(kinds):
            y = 92 + j * 16
            d.rectangle([tx, y + 1, tx + 9, y + 10], fill=colour(k[0], True))
            d.rectangle([tx + 14, y + 1, tx + 23, y + 10], fill=colour(k[1], True))
            key = (LEG.get(k[1]) or ("", "", "", "?"))[3] or "?"
            line = "%s  %s to %s" % (key, LEG[k[0]][0], LEG[k[1]][0])
            if len(line) > 30:
                line = "%s  to %.24s" % (key, LEG[k[1]][0])
            d.text((tx + 30, y), line, fill=(255, 255, 107) if s["brush"] == k[1] else (255, 255, 255), font=f12)
        for i, h in enumerate(["arrows  move", "Space   change a cell", "Bksp    change it back", "P       play-test",
                               "Esc     menu"]):
            d.text((tx, 300 + i * 15), h, fill=rgb(0xFFA0A0A8), font=f12)
        if msg is None and s["t"] < 5.0:
            msg = "DEBUGGER: %d change%s allowed" % (budget, "" if budget == 1 else "s")
        if msg:
            d.text((12, 4), msg, fill=(255, 255, 255), font=f12)
        img = img.resize((1600, 960), Image.NEAREST)
        p = os.path.join(out_dir, "%s-debug-%04d.png" % (prefix, n[0]))
        n[0] += 1
        img.save(p)
        out.append([p, round(secs, 3)])
        s["t"] += secs

    draw(1.6)
    for (x, y, z, to) in repairs:
        while state["y"] != y:
            state["y"] += 1 if y > state["y"] else -1
            draw(LAYER)
        while (state["x"], state["z"]) != (x, z):
            if state["x"] != x:
                state["x"] += 1 if x > state["x"] else -1
            else:
                state["z"] += 1 if z > state["z"] else -1
            draw(MOVE)
        if state["brush"] != to:
            state["brush"] = to
            draw(PICK)
        grid[y][D - 1 - z][x] = to
        state["changes"].append((x, y, z))
        draw(CHANGE)
    draw(1.2)
    draw(0.8, "P: play-test")
    return out
