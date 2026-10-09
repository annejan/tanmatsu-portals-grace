#!/usr/bin/env python3
"""Geeky chores, Saturday 07:00: merge the pull request before you leave.

The "merge button" is a floor button by the door; it wants a cube. The only
cube sits on a high shelf (3 m up, too high to jump). One white panel by the
start, one on the wall behind the shelf: portal up, take the cube, drop down,
put it on the button, out through the door to the exit.

    python3 gen.py > pull-request.txt
"""

W, H, D = 12, 8, 14
grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


box(0, W - 1, 0, 0, 0, D - 1, "#")                 # the floor
for y in range(1, H - 1):                          # the walls
    for x in range(W):
        put(x, y, 0, "#")
        put(x, y, D - 1, "#")
    for z in range(D):
        put(0, y, z, "#")
        put(W - 1, y, z, "#")
box(0, W - 1, H - 1, H - 1, 0, D - 1, "#")         # the ceiling

box(0, W - 1, 1, H - 2, 9, 9, "#")                 # the wall to the exit room
box(5, 6, 1, 2, 9, 9, "a")                         # its door

box(1, 4, 1, 2, 5, 8, "#")                         # the shelf, 3 m up
put(3, 3, 6, "C")                                  # the cube, on the shelf
box(0, 0, 3, 4, 6, 7, "W")                         # a panel behind the shelf
box(W - 1, W - 1, 1, 2, 3, 4, "W")                 # a panel by the start

put(8, 1, 6, "1")                                  # the merge button
put(6, 1, 2, "S")
box(5, 6, 0, 0, 11, 11, "E")                       # the exit

out = ["name: Pull request",
       "hint: The merge button wants a cube. The cube is on the shelf.",
       "story: Saturday 07:00. Hackfest awaits, [Subject-Name-here], but your PR "
       "is still open. Merge it first. Approved by: me, your badge. LGTM.",
       "size: %d %d %d" % (W, H, D),
       "facing: north"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    if all(c == "#" for r in rows for c in r):
        continue
    out += ["", "layer %d" % y] + rows
out += ["",
        "solution",
        "// Blue by the start, orange behind the shelf: up to the cube.",
        "shoot blue 11.0 2.0 3.5",
        "shoot orange 0.0 4.0 7.0",
        "walk_to 10.2 3.5",
        "walk_to 12 3.5",
        "wait 0.6",
        "// Out on the shelf: take the cube, and drop down.",
        "walk_to 2.2 6.8",
        "grab",
        "wait 0.5",
        "walk_to 5.5 6.5",
        "wait 0.8",
        "// The cube on the merge button, and through the door.",
        "walk_to 8.5 5.2",
        "look 8.5 1.3 6.5",
        "wait 0.6",
        "use",
        "wait 1.0",
        "walk_to 6.0 5.2",
        "walk_to 6.0 7.8",
        "walk_to 6.0 11.5",
        ""]
print("\n".join(out))
