#!/usr/bin/env python3
"""Generate the Canyon chamber for Portals (tanmatsu-portal).

    python3 gen_canyon.py OUTDIR

writes OUTDIR/canyon.txt (with the solution) and OUTDIR/canyon-sd.txt
(without it, for the SD card).

The chamber is described below as boxes of cells (x across, y up, z north,
one cell a metre); the file is written as the game reads it: a layer per
height, the top line of each the far (north) side.
"""
import os
import sys

W, H, D = 40, 32, 128

NAME = "Canyon"
HINT = "Everything worth reaching is far away. Make the far things come to you."
STORY = ("This canyon is one hundred and twenty metres long, [Subject-Name-here]. "
         "You are under two. The goo at the bottom does not mind the difference.")

grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]  # [y][z][x]


def box(x0, x1, y0, y1, z0, z1, ch):
    """Fill the cells x0..x1, y0..y1, z0..z1 (inclusive) with ch."""
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                grid[y][z][x] = ch


def cell(x, y, z, ch):
    grid[y][z][x] = ch


# --- The shell: metal all round, goo along the bottom ----------------------
box(0, W - 1, 0, H - 1, 0, D - 1, "#")
box(1, W - 2, 1, H - 2, 1, D - 2, ".")
box(1, W - 2, 0, 0, 1, D - 2, "~")

# --- The walls of the canyon: strata, ledges, buttresses -------------------
# (plain metal: big rectangles, cheap to draw). None of it may stand in
# the way of the funnel's ride (x 29-31, y 25-28, z 1-62), the faith
# plate's arc (z 56-57), the fling (z 80-81, y 21-27), the bridge (x 15-16,
# y 22-24, z 60-121) or the shots between them.
box(1, 2, 0, 9, 14, 40, "#")       # west: a low bench
box(1, 1, 10, 30, 30, 44, "#")     # west: a buttress the full height
box(1, 3, 12, 13, 44, 70, "#")     # west: a band of harder rock
box(1, 2, 14, 30, 92, 100, "#")    # west: a buttress behind the Table
box(1, 3, 0, 5, 96, 120, "#")      # west: a bench under the mesa
box(35, 38, 0, 16, 13, 24, "#")    # east: a ledge below the rim
box(36, 38, 22, 30, 12, 30, "#")   # east: a block hanging high
box(37, 38, 0, 12, 66, 78, "#")    # east: under the fling panel's wall
box(36, 38, 0, 7, 84, 120, "#")    # east: a bench
box(38, 38, 14, 30, 90, 104, "#")  # east: a buttress

# --- South rim: the start ------------------------------------------------
box(1, W - 2, 0, 19, 1, 12, "#")
cell(20, 20, 5, "S")
# The funnel emitter in the rim's floor, and the rock overhang above it,
# jutting from the west wall, its underside white over the emitter.
cell(8, 19, 6, "%")
box(1, 11, 26, 27, 3, 9, "#")
box(7, 9, 26, 26, 5, 7, "W")
# The south wall, high behind the start: a white panel, facing north.
box(29, 30, 26, 27, 0, 0, "W")

# --- The Shelf: a butte on the east side, where the funnel drops you -------
box(22, 38, 0, 21, 44, 64, "#")
cell(22, 21, 56, "J")              # the faith plate, at its west edge ...
# ... aimed at the bottom of the canyon: a white pad, alone in the goo.
box(5, 6, 0, 0, 56, 56, "W")
cell(6, 1, 56, "T")

# --- The fling panel: high on the east wall, facing west. Out of reach of
# a shot from the rim (over 64 m), so the funnel cannot be sent through it.
box(39, 39, 24, 25, 80, 81, "W")

# --- The Table: a butte on the west side ----------------------------------
box(1, 14, 0, 21, 72, 88, "#")
# The light bridge emitter, high on a pillar, aimed at a white slab two
# metres off on another: neither is in reach, and the slab is out of a
# shot's reach from the rim.
box(2, 4, 22, 27, 84, 86, "#")
cell(4, 26, 85, "H")
box(7, 8, 22, 27, 84, 86, "#")
box(7, 7, 26, 27, 85, 85, "W")

# --- The mesa at the far end: the exit ------------------------------------
box(1, W - 2, 0, 21, 121, D - 2, "#")
box(15, 15, 1, 21, 121, 121, ".")  # a notch under the bridge's end
# The post: a white face on a metal block, facing south (white on its
# sides as well, it would take a portal facing east, and a fling from the
# Shelf out of it would skip the bridge).
box(14, 16, 22, 23, 122, 123, "#")
box(15, 15, 22, 23, 122, 122, "W")
box(17, 22, 21, 21, 123, 125, "E")

# --- Rock spires in the goo, and two arches high over the canyon ----------
box(14, 17, 0, 15, 24, 28, "#")
box(4, 7, 0, 9, 30, 34, "#")
box(31, 35, 0, 9, 30, 35, "#")
box(19, 21, 0, 19, 66, 69, "#")
box(26, 29, 0, 6, 70, 73, "#")
box(30, 34, 0, 17, 96, 101, "#")
box(10, 12, 0, 13, 100, 103, "#")
box(22, 25, 0, 11, 106, 110, "#")
box(1, W - 2, 29, 30, 38, 41, "#")
box(1, W - 2, 28, 30, 102, 105, "#")

SOLUTION = """\
solution
// One: the funnel only goes up, into the rock. Turn round: high on the
// wall behind you, a panel faces where you want to go.
shoot blue 29.5 26.8 1.0
shoot orange 8.5 26.0 6.5
walk_to 8.5 6.5
// Up, through the rock and out of the wall: sixty metres over the goo.
wait 21
// Two: the faith plate aims at the bottom of the canyon. Let it; catch
// the fall with a portal on the pad, and leave by the panel on the wall.
walk_to 22.5 57.5
shoot blue 6.0 0.0 56.5
shoot orange 39.0 25.0 80.5
face 180 0
step_off 0.5
wait 3.5
// Three: the bridge hits a slab. The other end goes on the post, far off
// on the mesa: the bridge comes back to you.
walk_to 6.0 81.5
shoot blue 7.0 26.6 85.5
walk_to 12.5 87.5
shoot orange 15.5 22.6 122.0
walk_to 15.5 88.5
walk_to 15.5 101
walk_to 15.5 113
walk_to 15.5 120.6
walk_to 16.7 121.5
walk_to 17.6 121.5
walk_to 18.5 123.5
"""


def write(path, with_solution):
    out = [f"name: {NAME}", f"hint: {HINT}", f"story: {STORY}", f"size: {W} {H} {D}", "facing: north", ""]
    for y in range(H):
        out.append(f"layer {y}")
        for z in range(D - 1, -1, -1):
            out.append("".join(grid[y][z]))
        out.append("")
    if with_solution:
        out.append(SOLUTION)
    with open(path, "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    assert len(NAME) <= 31 and len(HINT) <= 79 and len(STORY) <= 159, (len(NAME), len(HINT), len(STORY))
    os.makedirs(os.path.join(outdir, "with"), exist_ok=True)
    os.makedirs(os.path.join(outdir, "sd"), exist_ok=True)
    write(os.path.join(outdir, "with", "canyon.txt"), True)
    write(os.path.join(outdir, "sd", "canyon.txt"), False)
