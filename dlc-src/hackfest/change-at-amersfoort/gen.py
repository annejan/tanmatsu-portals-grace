#!/usr/bin/env python3
"""Geeky chores: change at Amersfoort. A long platform with the check-in
pole (a pedestal button) at its far west end and the train door at its
east end. The pole holds the door for two and a half seconds; the walk back is
about four. A white panel by the pole and one by the train door make the
walk one step.

    python3 gen.py > change-at-amersfoort.txt
"""

W, H, D = 20, 5, 12
grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


box(0, W - 1, 0, 0, 0, D - 1, "#")                 # the floor
box(0, W - 1, H - 1, H - 1, 0, D - 1, "#")         # the roof
for y in range(1, H - 1):                          # the outer walls
    for x in range(W):
        put(x, y, 0, "#")
        put(x, y, D - 1, "#")
    for z in range(D):
        put(0, y, z, "#")
        put(W - 1, y, z, "#")

# The platform (z 1..4) and the train (z 6..10), the carriage wall at z 5.
box(0, W - 1, 1, 3, 5, 5, "#")
box(2, 16, 2, 2, 5, 5, "G")                        # the carriage windows
box(18, 18, 1, 2, 5, 5, "a")                       # the train door
box(1, 3, 1, 3, 6, 10, "#")                        # the train is shorter than the platform
box(4, 18, 0, 0, 7, 9, "E")                        # seats; the train to Enschede

put(1, 1, 1, "I")                                  # the check-in pole
put(1, 2, 1, "1")
box(0, 0, 1, 2, 3, 3, "W")                         # white panel by the pole
box(W - 1, W - 1, 1, 2, 4, 4, "W")                 # white panel by the train door
put(15, 1, 2, "S")

out = ["name: Change at Amersfoort",
       "hint: Check in at the pole. The doors close in two and a half seconds.",
       "story: Change at Amersfoort for Enschede. Three minutes, they said. "
       "The pole gives you two and a half seconds, [Subject-Name-here]. Shortcut compiled.",
       "size: %d %d %d" % (W, H, D),
       "facing: west",
       "timer: 2.5"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    if all(c == "#" for r in rows for c in r):
        continue
    out += ["", "layer %d" % y] + rows
out += ["",
        "solution",
        "// A portal by the train door, one by the pole.",
        "look 19.0 1.6 4.5",
        "wait 0.3",
        "shoot orange 19.0 1.6 4.5",
        "walk_to 3.0 3.0",
        "look 0.0 1.6 3.5",
        "wait 0.3",
        "shoot blue 0.0 1.6 3.5",
        "// Check in, and step through.",
        "walk_to 2.0 2.4",
        "look 1.5 2.1 1.5",
        "wait 0.3",
        "use",
        "walk_to 1.2 3.5",
        "walk_to 0.0 3.5",
        "walk_to 18.5 6.0",
        "walk_to 18.5 7.5",
        "walk_to 17.0 8.5",
        ""]
print("\n".join(out))
