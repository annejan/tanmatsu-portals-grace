#!/usr/bin/env python3
"""Generator for the After hours chamber "Courier".

A sorting office. High in its north wall, behind glass, the post box: a
mail slot one metre square -- a cube goes through it, you do not -- and
under it a chute with a cube button at its bottom. The parcel (the cube)
is on a pillar no one can reach. An excursion funnel runs along the foot
of the south wall into a white panel low in the east wall. Send the funnel
out of the panel over the pillar, and it takes the parcel across the room
and through the slot. Cut the funnel and the parcel drops down the chute
onto the button; the door beside the slot opens, and the same funnel, sent
out of another panel, takes you through it.

g[y][z][x]; row 0 of a layer is z = D-1.

    python3 gen.py SOLUTION.txt OUT.txt   -> the chamber with that solution
    COURIER_DRAFT=1 python3 gen.py ...    -> the first draft (flawed.txt)

The first draft had a metal floor all the way. Cut the funnel while the
parcel was over the room and it landed at your feet: you could pick it up
and post it yourself, into the funnel where it runs along the floor
(cheese-handled.txt). The final has goo under the parcel's whole way
across, round the pillar and five metres wide: a parcel cut loose is lost
(the dropper sends a new one), and falls too far from any floor to be
caught on the way down.
"""
import os
import sys

DRAFT = os.environ.get('COURIER_DRAFT') == '1'
W, H, D = 17, 10, 25
NZ = D - 5       # the north wall: the office is z1..NZ-1, the post box and the exit behind it
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The sorting office: x1..15, air layers 1..8.
box(1, 15, 1, 8, 1, NZ - 1, '.')

# The funnel: low in the west wall, along the foot of the south wall, into
# a white column low in the east wall (portal A).
FZ = 1
g[1][FZ][0] = '%'
box(16, 16, 1, 2, FZ, FZ, 'W')

# The pillar, 4 m, off the south wall; the parcel on it, from a dropper.
SX = 4
box(SX, SX, 1, 4, 2, 4, '#')
g[9][3][SX] = 'V'
# Behind it, the white column the funnel can come out of (portal B): out
# at y = 5.5, over the pillar's top, north to the slot.
box(SX, SX, 5, 6, 0, 0, 'W')

# FIX: goo under the parcel's way, from the pillar to the north wall and
# two metres either side of it: a parcel cut loose on the way is lost (the
# dropper sends another), and no floor is near enough to catch it from.
if not DRAFT:
    box(SX - 2, SX + 2, 0, 0, 2, NZ - 1, '~')
    box(SX, SX, 0, 0, 2, 4, '#')       # (under the pillar)

# The post box, behind glass: the mail slot, one metre square, and under
# it the chute, with a cube button at its bottom where you can see it. The
# funnel through the slot ends on the chute's back wall, over the button.
box(SX - 1, SX + 1, 3, 6, NZ, NZ, 'G')
g[5][NZ][SX] = '.'                     # the slot
g[5][NZ + 1][SX] = '.'                 # behind it, the top of the chute
g[4][NZ + 1][SX] = '1'                 # the chute, with the button in it
g[3][NZ + 1][SX] = 'K'

# The way out: door a in the north wall, over the room, glass either side
# of it; behind it, the exit. Its own white column (portal C) on the south
# wall. The funnel carries you with your middle on its middle, 4.6 to
# 6.4 m up: the door is three metres high, its sill a metre under the
# slot's.
DX = 11
box(DX - 1, DX + 1, 4, 6, NZ, NZ, 'G')
box(DX, DX, 4, 6, NZ, NZ, 'a')
box(DX - 1, DX + 1, 4, 7, NZ + 1, NZ + 3, '.')
box(DX - 1, DX + 1, 3, 3, NZ + 1, NZ + 3, 'E')
box(DX, DX, 5, 6, 0, 0, 'W')

g[1][4][10] = 'S'

HEAD = """name: Courier
hint: Do not handle the parcel. That is what the funnel is for.
story: After hours, [Subject-Name-here], the post still goes out. The parcel travels first class. You follow by freight. The parcel is insured.
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
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'courier.txt')
    with open(out, 'w') as f:
        f.write(body)


if __name__ == '__main__':
    main()
