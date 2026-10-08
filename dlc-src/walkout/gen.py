#!/usr/bin/env python3
"""Human in the loop's outro: the walk out of the cubicle, through an office
nobody has worked in for a long time. Not a puzzle: the solution is the
walk, played as the outro (main/outro.c).

    python3 dlc-src/walkout/gen.py > dlc/human-in-the-loop/walkout.txt
"""

W, H, D = 22, 6, 14
grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


# The shell: metal, the white panels mostly gone -- a few left, here
# and there, on the floor and the walls; a ceiling coming down in places.
box(0, W - 1, 0, 0, 0, D - 1, "#")
for z in range(1, D - 1):
    for x in range(1, W - 1):
        if (x * 3 + z * 7) % 5 == 0:
            put(x, 0, z, "W")
for y in range(1, H - 1):
    for x in range(W):
        put(x, y, 0, "W" if (x * 7 + y * 3) % 9 == 0 else "#")
        put(x, y, D - 1, "W" if (x * 5 + y) % 7 == 0 else "#")
    for z in range(D):
        put(0, y, z, "W" if (z + y) % 6 == 0 else "#")
        put(W - 1, y, z, "#")
box(0, W - 1, H - 1, H - 1, 0, D - 1, "#")
for x, z in ((4, 8), (9, 11), (12, 7), (14, 3), (17, 7), (19, 12), (7, 2)):
    put(x, H - 2, z, "#")  # fallen from the ceiling, caught on the grid

# Your cubicle, in the south-west corner: a desk, partitions, a way out.
box(1, 3, 1, 1, 1, 1, "#")     # the desk
box(6, 6, 1, 2, 1, 5, "#")     # its east partition ...
box(6, 6, 1, 2, 4, 4, ".")     # ... with the gap you leave by
box(1, 6, 1, 2, 6, 6, "#")     # its north partition
# The next cubicles: glass partitions, broken.
box(8, 12, 1, 2, 6, 6, "G")
box(10, 11, 1, 2, 6, 6, ".")
box(13, 13, 1, 2, 1, 5, "G")
box(8, 10, 1, 1, 1, 1, "#")    # a desk
box(15, 17, 1, 1, 1, 1, "#")   # another
# The corridor: what is left on its floor.
box(10, 11, 0, 0, 9, 10, "~")  # something leaked
box(4, 5, 0, 0, 10, 11, "~")
put(9, 1, 3, "C")              # boxes nobody packed
put(16, 1, 11, "C")
put(18, 1, 4, "C")
put(15, 1, 8, "o")
put(3, 1, 3, "S")
# The way out.
put(20, 0, 9, "E")

out = ["name: Lights out",
       "story: Assignment complete. You may go.",
       "size: %d %d %d" % (W, H, D),
       "facing: south"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    if all(c == "#" for r in rows for c in r):
        continue
    out.append("")
    out.append("layer %d" % y)
    out += rows
out += ["",
        "solution",
        "// The desk, one last look; then out of the cubicle, and along the",
        "// corridor, past what is left, to the door.",
        "wait 1.5",
        "look 6.5 1.6 4.5",
        "wait 0.8",
        "walk_to 5.2 4.5",
        "walk_to 7.5 4.5",
        "walk_to 7.5 8.0",
        "look 10.5 0.4 9.5",
        "wait 1.5",
        "walk_to 13.5 8.0",
        "look 15.5 1.2 8.5",
        "wait 1.0",
        "walk_to 17.5 9.5",
        "look 20.5 1.4 9.5",
        "wait 0.8",
        "walk_to 20.6 9.5",
        ""]
print("\n".join(out))
