#!/usr/bin/env python3
# Generates the "the-dyke" chamber (Hackfest pack). g[y][z][x]; row 0 of a
# layer is z = D-1 (the far side).   python3 gen.py > the-dyke.txt
#
# South: the polder, where you start. A dyke (3 m high, 3 deep) runs across
# the whole room; on its far side a 7 m canal of goo, then the far bank with
# the exit. Blue gel drips into a corner of the polder; a portal pair (white
# floor under the drip, white ceiling in front of the dyke) puts it at the
# foot of the dyke, to bounce up on. A laser field keeps you out of
# the gel corner itself. On top, the cycle path leads east to a
# faith plate (the "ferry") that throws you over the canal.

W, H, D = 16, 10, 21
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The room: air layers 1..8, floor layer 0, ceiling layer 9 (headroom
# for the ferry's arc, which peaks 2.5 m above the dyke).
box(1, 14, 1, 8, 1, 19, '.')
# The gel corner: white floor under a blue gel dispenser.
box(1, 3, 0, 0, 3, 5, 'W')
g[9][4][2] = 'U'
# A laser field fences the gel corner off, up to the air cell under the
# dispenser, one cell wider than the painted patch: shots and gel go in,
# the player cannot. Without it you could bounce on the drip's own patch
# and skip the portals.
box(1, 4, 1, 7, 2, 6, '*')
# White ceiling at the foot of the dyke.
box(7, 8, 9, 9, 6, 7, 'W')
# The dyke: solid, wall to wall, top at y = 4.
box(1, 14, 1, 3, 8, 10, '#')
# The ferry: a faith plate on the dyke's cycle path, east end.
g[3][9][13] = 'J'
# The canal.
box(1, 14, 0, 0, 11, 17, '~')
# The far bank: Hackfest.
box(12, 14, 0, 0, 18, 19, 'E')
g[1][18][13] = 'T'
# Start.
g[1][2][8] = 'S'

HEAD = """name: The dyke
hint: Bikes cannot climb a dyke. Bring the blue gel to its foot, and bounce.
story: Route recalculated, [Subject-Name-here]: the cycle path to Hackfest goes over the dyke. Your bike cannot climb. You can bounce. Mind the canal.
size: %d %d %d
facing: north
""" % (W, H, D)

SOLUTION = """solution
// Floor portal under the drip, ceiling portal at the foot of the dyke.
shoot blue 2.5 1.0 4.6
shoot orange 7.9 9.0 7.0
// Let the gel come through and paint the floor by the dyke.
wait 2.5
walk_to 7.9 7.3
wait 1.5
face 0 0
jump
walk_to 7.9 9.5
// Cycle east along the top of the dyke to the ferry.
walk_to 13.5 8.5
// Step north onto the ferry; walking on the way it throws you does no harm.
face 0 0
walk 0.3
wait 4
"""


def layers():
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    return '\n'.join(out)


print(HEAD + '\n' + layers() + SOLUTION, end='')
