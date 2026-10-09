#!/usr/bin/env python3
# "Speed cameras": the A1 to Enschede, two speed cameras (turrets) at the
# far end of the road. The van's cargo hold is too low to climb into, but
# its floor takes portals, and so does the ceiling over each camera: drop
# a crate on each of them from above. Prints the chamber with its solution.

W, H, D = 6, 6, 24
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


def put(x, y, z, ch):
    g[y][z][x] = ch


# The road: four lanes' worth of tarmac, x1..4, z1..22, four metres high.
box(1, 4, 1, 4, 1, 22, '.')

# The van, parked on the hard shoulder: x1..2, z1..4, two metres high.
# Its cargo hold (layer 1, z1..3) is one metre high and open to the east.
box(1, 2, 1, 2, 1, 4, '#')
box(1, 2, 1, 1, 1, 3, '.')
# A bulkhead splits it into two bays, one crate each, both open east.
box(1, 2, 1, 1, 2, 2, '#')
# Two crates, at the back of the hold, each on a pair of white floor cells
# that only pair up across the van (x), so a portal under one is sure.
for z in (1, 3):
    box(1, 2, 0, 0, z, z, 'W')
    put(1, 1, z, 'C')

# The speed cameras, one per side of the road, looking south.
put(1, 1, 19, 't')
put(4, 1, 20, 't')
# The ceiling over each takes a portal: the pair whose crate lands on it.
box(1, 1, 5, 5, 18, 19, 'W')
box(4, 4, 5, 5, 19, 20, 'W')

# Enschede: the exit, across the road, past the cameras.
box(1, 4, 0, 0, 22, 22, 'E')

# The player, by the van, out of the cameras' 15 m reach.
put(4, 1, 2, 'S')

HEAD = """name: Speed cameras
hint: They watch the road, not the sky. Your cargo can take the scenic route.
story: A1 to Enschede, [Subject-Name-here]. Two speed cameras, one van, two crates of badges. Gravity has no speed limit. I checked.
size: %d %d %d
facing: north
""" % (W, H, D)

SOLUTION = """// Drop the first crate on the west camera: the ceiling over it, then
// the floor of the hold under the crate.
shoot orange 1.5 5.0 19.3
shoot blue 1.6 1.0 1.5
wait 2.5
// The second crate on the east camera.
shoot orange 4.5 5.0 20.3
shoot blue 1.6 1.0 3.5
wait 2.5
// Both down: drive on.
walk_to 3.0 12.0
walk_to 2.7 22.5
"""


def chamber():
    out = HEAD
    for y in range(H):
        out += "\nlayer %d\n" % y + "\n".join("".join(g[y][z]) for z in reversed(range(D))) + "\n"
    return out + "\nsolution\n" + SOLUTION


print(chamber(), end="")
