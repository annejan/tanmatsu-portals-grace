#!/usr/bin/env python3
"""Hackfest chores: "Loading the van".

A street in front of a hackerspace. A box van stands on it, its back doors
open, its cargo floor 2 m above the street: too high to climb. The gear (a
cube) is up in the hackerspace's attic, 3 m up the west wall. A white panel
on the street wall and one up in the attic fetch it; a faith plate behind
the van throws it in, onto the cube button by the cab. The button opens the
gate to the road.

    python3 gen.py > loading-the-van.txt
"""

W, H, D = 18, 10, 20
grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    assert 0 <= x < W and 0 <= y < H and 0 <= z < D, (x, y, z)
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


# The street: air x 1..14, z 1..18, layers 1..9, walls round it.
box(0, W - 1, 0, 0, 0, D - 1, "#")                 # the street
for y in range(1, H):
    for x in range(W):
        put(x, y, 0, "#")
        put(x, y, D - 1, "#")
    for z in range(D):
        put(0, y, z, "#")
        put(15, y, z, "#")
        put(16, y, z, "#")
        put(17, y, z, "#")

# The gate to the road, east, and the road out: the exit.
box(15, 15, 1, 2, 2, 3, "a")
box(16, 16, 1, 2, 2, 3, ".")
box(16, 16, 0, 0, 2, 3, "E")

# The van: chassis x 6..10, z 12..18, cargo floor at 4 (out of a held
# cube's reach from the street); box walls up to 7, roof at 8; back doors
# open (south, z 12); the cab north of the box.
box(6, 10, 1, 3, 12, 17, "#")                      # chassis and wheels
box(6, 6, 4, 7, 12, 17, "#")                       # side walls
box(10, 10, 4, 7, 12, 17, "#")
box(6, 10, 4, 7, 17, 17, "#")                      # the bulkhead to the cab
box(6, 10, 8, 8, 12, 17, "#")                      # the roof
box(6, 10, 1, 6, 18, 18, "#")                      # the cab
put(8, 3, 16, "K")                                 # the cargo button, by the bulkhead
put(8, 4, 16, "1")
put(8, 4, 15, "T")                                 # where the plate throws

# The faith plate, behind the van.
put(8, 0, 6, "J")

# The attic: a loft along the west wall, floor at 4, with the gear in it.
box(1, 3, 1, 3, 10, 18, "#")
put(2, 4, 16, "C")                                 # back from the lip: no grabbing it from the street
box(0, 0, 4, 5, 14, 14, "W")                       # the attic's white panel
box(0, 0, 1, 2, 4, 4, "W")                         # the street's

put(12, 1, 3, "S")

SOLUTION = """
// Portals: the street wall to the attic.
shoot blue 1.0 2.0 4.5
shoot orange 1.0 5.0 14.5
walk_to 0.5 4.5
wait 0.3
walk_to 2.5 15.2
grab
wait 0.4
look 0.0 5.3 14.5
wait 0.5
walk_to 0.5 14.5
wait 0.4
// The gear onto the plate, and into the van.
walk_to 8.5 5.2
look 8.5 1.6 8.5
wait 0.4
use
wait 3
walk_to 14 2.9
walk_to 16.5 2.9
"""

out = ["name: Loading the van",
       "hint: The gear is in the attic. The van's floor is too high. The plate is not.",
       "story: Beep! A friend has room in the van to Hackfest, [Subject-Name-here]. Gear first. Lift with your legs. Or better: with a faith plate.",
       "size: %d %d %d" % (W, H, D),
       "facing: west"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    out += ["", "layer %d" % y] + rows
out += ["", "solution", SOLUTION.strip(), ""]
print("\n".join(out))
