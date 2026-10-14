#!/usr/bin/env python3
"""A broken round's debugger, as a film's pictures: the debugger opening on
the draft, the cursor going to each repair (`repair:` in the round's file),
the change made, then off to play-test it -- each picture drawn by
main/editor.c itself (tests/host_debugger.c, `make host_debugger`) at the
badge's 800 x 480, in its letters.

    from debugger_film import frames
    frames(path, out_dir, "r06")  ->  [[png, seconds], ...]

tools/story_film.py splices them in before the round, after the desk.
"""

import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "build", "debugger", "host_debugger")
MOVE, LAYER, PICK, CHANGE = 0.07, 0.3, 0.5, 0.8  # seconds a step, a layer, a change picked, one made


def header(text, key):
    for line in text.split("\n"):
        k, _, v = line.partition(":")
        if k.strip() == key:
            return v.strip()
    return None


def cells(lo_hi):
    lo, _, hi = lo_hi.partition("-")
    return range(int(lo), int(hi or lo) + 1)


def frames(path, out_dir, prefix):
    if not os.path.exists(TOOL):
        sys.exit("%s: none -- `make host_debugger` first" % TOOL)
    with open(path) as f:
        text = f.read()
    w, h, d = map(int, header(text, "size").split())
    first = header(text, "debug").split()[1][1]  # the brush the debugger opens with (editor_open_debug)
    repairs = []
    for line in text.split("\n"):
        if line.startswith("repair:"):
            x, y, z, ft = line[7:].split()
            repairs += [(xx, yy, zz, ft[1]) for yy in cells(y) for zz in cells(z) for xx in cells(x)]

    # Where editor_open_debug puts the cursor, then each repair in turn.
    at = {"x": w // 2, "z": d // 2, "y": 1 if h > 1 else 0, "brush": first}
    script, shots = [], []

    def shot(secs):
        name = "%s-debug-%04d" % (prefix, len(shots))
        script.append("shot %s" % name)
        script.append("tick %.3f" % secs)
        shots.append([name, secs])

    shot(1.6)
    for (x, y, z, to) in repairs:
        while at["y"] != y:
            at["y"] += 1 if y > at["y"] else -1
            script.append("layer %d" % at["y"])
            shot(LAYER)
        while (at["x"], at["z"]) != (x, z):
            if at["x"] != x:
                at["x"] += 1 if x > at["x"] else -1
            else:
                at["z"] += 1 if z > at["z"] else -1
            script.append("at %d %d" % (at["x"], at["z"]))
            shot(MOVE)
        if at["brush"] != to:
            at["brush"] = to
            script.append("brush %s" % to)
            shot(PICK)
        script.append("paint")
        shot(CHANGE)
    shot(1.6)
    subprocess.run([TOOL, path, out_dir], input="\n".join(script) + "\n", text=True, check=True)
    out = []
    for name, secs in shots:
        ppm = os.path.join(out_dir, name + ".ppm")
        png = os.path.join(out_dir, name + ".png")
        Image.open(ppm).save(png)
        os.remove(ppm)
        out.append([png, round(secs, 3)])
    return out
