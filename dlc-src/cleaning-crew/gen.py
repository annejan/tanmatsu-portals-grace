#!/usr/bin/env python3
"""Generator for the After hours chamber "Cleaning crew".

The night cleaners have gone home and left their paint running. In the
south wall, two cupboards: in one, white gel drips down a long pipe onto a
white floor; in the other, blue gel onto another. Out in a pool of goo, an
island, and in it a drain: two white panels in the floor, under a glass
skylight. At the north end, three metres up, the ledge with the exit.
Nothing takes a portal but those three floors, and gel only ever falls.

Through a portal, falling is a matter of opinion. The white gel, dropped
nine metres into its cupboard's floor, comes up out of the drain just as
fast and paints the ceiling either side of the skylight. Now the ceiling
takes a portal: blue gel drips out of it and splashes the drain. Walk into
the blue cupboard's floor yourself, and you drop out of the ceiling onto
the island; a running jump off the drain goes 3.7 m up, onto the ledge.

g[y][z][x]; row 0 of a layer is z = D-1.

    python3 gen.py SOLUTION.txt OUT.txt   -> the chamber with that solution
    CLEAN_DRAFT=1 python3 gen.py ...      -> the first draft (flawed.txt)

The first draft had a metal island and the same 6 m ceiling all the way.
Two flaws, both proven:

- cheese-pipe.txt: no white at all. The blue drip had made its own
  cupboard floor blue, as Momentum's orange drip did its runway: jump off
  it up the pipe, put a portal in the floor under you on the way down and
  the other in the drain, and you come up out of the drain 3.6 m and steer
  onto the ledge. Fixed by the soffit: the ceiling over the drain a metre
  lower, so nothing comes up out of it higher than 3.25 m.
- cheese-splash.txt: no jump off the drain. Blue gel from the ceiling
  splashed the metal island round the drain too, right up to the foot of
  the ledge; drop out of the ceiling onto that and bounce up. Fixed by the
  glass island: the drain is the only floor out there that takes paint.

Tried on the final and failed: both of those; dropping out of the ceiling
at a run and drifting onto the ledge (the cupboard's back wall takes your
speed, and the soffit leaves a quarter of a metre to drift in); coming up
out of the drain from any cupboard floor; jumping the goo.
"""
import os
import sys

DRAFT = os.environ.get('CLEAN_DRAFT') == '1'
W, H, D = 13, 11, 20
CEIL = 7          # the hall: air layers 1..CEIL-1, floor top y = 1, ceiling 6 m up
BX, BZ = 6, 12    # the drain: white panels at (BX, 0, BZ) and (BX, 0, BZ + 1)
LEDGE = BZ + 4    # the ledge's south face
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The hall: x1..11, z3..18, air layers 1..CEIL-1.
box(1, W - 2, 1, CEIL - 1, 3, D - 2, '.')

# The goo: all of the hall from z6 up to a metre short of the ledge, but
# for an island round the drain, five metres by six, four metres out from
# the floor in front of the cupboards. The metre of floor at the foot of
# the ledge is where a jump that falls short lands.
box(1, W - 2, 0, 0, 6, LEDGE - 2, '~')
# FIX (glass floor): the island is glass, as far as gel falling from the
# ceiling over it can splash, so the only floor out there that takes paint
# is the drain itself. In the draft it was metal: white gel dripped out of
# the ceiling made floor portals of it.
box(BX - 2, BX + 2, 0, 0, BZ - 2, BZ + 3, '#' if DRAFT else 'G')
# The drain: two white panels in the floor.
box(BX, BX, 0, 0, BZ, BZ + 1, 'W')

# FIX (soffit): the ceiling over the drain and south of it a metre lower,
# 5 m. What falls out of a portal in it falls a quarter of a metre to the
# height of the ledge -- not long enough to drift onto it -- and whatever
# comes up out of the drain stops 3.25 m up. A running jump off blue gel
# on the drain is out from under it before the head gets that high.
if not DRAFT:
    box(1, W - 2, CEIL - 1, CEIL - 1, 3, BZ + 1, '#')
TOP = CEIL - 1 if not DRAFT else CEIL   # the layer of the ceiling over the drain
# A skylight over the drain, glass: the ceiling straight over the drain takes
# no paint, so the portal the blue gel comes out of is beside it, not over
# it. (One over it would catch a player jumping off the drain -- a portal
# pulls in what rises at it -- and send them into the blue cupboard.)
box(BX, BX, TOP, TOP, BZ - 1, BZ + 1, 'G')

# The ledge, three metres up, the width of the hall; the exit on it.
box(1, W - 2, 1, 3, LEDGE, D - 2, '#')
box(BX - 1, BX + 1, 3, 3, LEDGE + 1, D - 2, 'E')


def cupboard(x0, gel, top):
    """A cupboard in the south wall, 2 x 2 and 3 m high, white floor; its
    dispenser over the back cell of column x0 at layer `top`, at the top of
    a pipe one cell across."""
    box(x0, x0 + 1, 1, 3, 1, 2, '.')
    box(x0, x0 + 1, 0, 0, 1, 2, 'W')
    box(x0, x0, 4, top - 1, 1, 1, '.')
    g[top][1][x0] = gel


# Both pipes reach above the hall's ceiling: a splash paints whole cells,
# and gel shot up a pipe that ended under it would paint a cell whose
# other face is the hall's south wall.
cupboard(1, 'X', 10)    # white: a nine-metre drop, out of a seven-metre pipe
cupboard(10, 'U', 8)    # blue: a seven-metre drop

g[1][4][BX] = 'S'

HEAD = """name: Cleaning crew
hint: Gel only ever falls. A portal decides which way is down.
story: The cleaning crew went home at five, [Subject-Name-here]. They left the paint running: white for the ceiling, blue for you. Do clean up after yourself.
size: %d %d %d
facing: north
""" % (W, H, D)


def layers():
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    return '\n'.join(out)


def main():
    sol = open(sys.argv[1]).read().strip() + '\n' if len(sys.argv) > 1 else ''
    body = HEAD + '\n' + layers()
    if sol:
        body += '\nsolution\n' + sol
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                             'cleaning-crew.txt')
    with open(out, 'w') as f:
        f.write(body)


if __name__ == '__main__':
    main()
