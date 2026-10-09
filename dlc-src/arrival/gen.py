#!/usr/bin/env python3
"""Off to Hackfest's outro: in through the doors of the hall, past the
tables already covered in badges and cables, to the registration desk.
Not a puzzle: the solution is the walk, played as the outro (main/outro.c).

    python3 dlc-src/arrival/gen.py > dlc/hackfest/arrival.txt
"""

W, H, D = 20, 7, 16
grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


# A big white hall: the lights are on, for once.
box(0, W - 1, 0, 0, 0, D - 1, "#")
box(1, W - 2, 0, 0, 1, D - 2, "W")
for y in range(1, H - 1):
    for x in range(W):
        put(x, y, 0, "W")
        put(x, y, D - 1, "W")
    for z in range(D):
        put(0, y, z, "W")
        put(W - 1, y, z, "W")
box(0, W - 1, H - 1, H - 1, 0, D - 1, "#")
# Rows of tables, one high, with aisles between: badges, cables, laptops.
for z0 in (4, 8):
    for x0, x1 in ((2, 7), (12, 17)):
        box(x0, x1, 1, 1, z0, z0 + 1, "#")
# The registration desk at the far end, the way in through its middle.
box(2, 8, 1, 1, 12, 12, "#")
box(11, 17, 1, 1, 12, 12, "#")
# What is on the tables: gear, and somebody's disco ball.
put(3, 2, 4, "C")
put(15, 2, 9, "C")
put(6, 2, 8, "o")
put(13, 2, 5, "C")
# A light bridge over the hall, because of course someone built one.
put(0, 4, 7, "H")
put(9, 1, 1, "S")
put(9, 0, 14, "E")
put(10, 0, 14, "E")

out = ["name: Hackfest",
       "story: You made it. The hall smells of solder and Club-Mate.",
       "size: %d %d %d" % (W, H, D),
       "facing: north"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    if all(c == "#" for r in rows for c in r):
        continue
    out += ["", "layer %d" % y] + rows
out += ["",
        "solution",
        "// In, down the middle aisle, a look round at the tables, and on to",
        "// the registration desk.",
        "wait 1.2",
        "walk_to 9.5 3.5",
        "look 4.5 1.4 8.5",
        "wait 1.2",
        "look 15.5 1.4 5.5",
        "wait 1.2",
        "walk_to 9.8 10.5",
        "look 9.8 1.6 14.5",
        "wait 0.6",
        "walk_to 9.8 14.6",
        ""]
print("\n".join(out))
