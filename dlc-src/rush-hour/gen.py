#!/usr/bin/env python3
"""Generator for the After hours chamber "Rush hour".

The last train home leaves from the upper platform, two metres up. The
escalator is out of order. On the concourse: a white panel in the floor
at the foot of the platform. Behind a glass wall, a lane one metre wide
and fifteen long, a white strip in its ceiling over the far end, a white
wall at the very end. In the concourse's east wall, a drain: orange gel
drips onto its white floor, where it does no good to anybody.

Walk into a wall portal and you come out of a floor one at the speed you
went in, straight up -- or at 6.5 m/s at least: walking, a metre and a
half. Running on orange gel, eleven metres a second, over three. So: the
drain's floor and the lane's ceiling, twice, until the last five metres
of the lane are orange right up to the wall (one patch, two metres, is
not enough: 1.3 m up). Then the end wall and the panel in the concourse
floor, and run; forward, coming up, is onto the platform.

g[y][z][x]; row 0 of a layer is z = D-1.

    python3 gen.py SOLUTION.txt OUT.txt   -> the chamber with that solution
    RUSH_DRAFT=1 python3 gen.py ...       -> the first draft (flawed.txt)

The first draft had an open drain. One flaw, proven at a slow badge's
frame rate:

- cheese-drain.txt: no gel at all. The drain's floor and the launch panel:
  walk into the drain, and out of the panel. A body falls a little way
  into a floor portal before it goes through, and comes out of the other
  floor as far past the plane, so floor to floor gives back more height
  than the fall put in -- more, the longer the frame. At 10-12 frames a
  second (host_tas -jitter N 10 12: 10 seeds of 30; -jitter 4 10 15) a
  plain walk into the drain throws you 2.1 m, onto the platform. At 20-35
  it falls short by centimetres: bouncing between the two floors peaks at
  3.02 m against the platform's 3.0, never next to it. Fixed by the
  fizzler: the drain is no portal for the player now, only for the gel,
  and the panel's only partners are a wall (walking: 1.4 m) and a ceiling.

Tried on the final and failed: walking into the end wall (6.5 m/s up, the
least a floor portal gives: 1.4 m); jumping into it (a jump comes out
sideways); one patch of gel at the end of the lane (1.3 m up, short); the
drain's own splat (it lands inside the drain); and the drain cheese, at
10-35 frames a second.
"""
import os
import sys

DRAFT = os.environ.get('RUSH_DRAFT') == '1'
W, H, D = 18, 8, 14
LEDGE = 10        # the platform's south face (z); its top 2 m over the concourse
LX = 6            # the launch panel: (LX, 0, LEDGE-2) and (LX, 0, LEDGE-1)
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The concourse: x1..10, z3..12, floor top y = 1, ceiling 6 m up.
box(1, 10, 1, 6, 3, D - 2, '.')

# The upper platform: z LEDGE..12, its top at y = 3. The train (the exit)
# waits at its back.
box(1, 10, 0, 2, LEDGE, D - 2, '#')
box(3, 8, 2, 2, D - 3, D - 2, 'E')

# The launch panel, in the concourse floor at the foot of the platform.
box(LX, LX, 0, 0, LEDGE - 2, LEDGE - 1, 'W')

# The lane: z = 1, x1..16, 3 m high; a glass wall (z = 2) between it and
# the concourse but for a gap at its west end.
box(1, W - 2, 1, 3, 1, 1, '.')
box(3, 10, 1, 3, 2, 2, 'G')
box(1, 2, 1, 2, 2, 2, '.')
# A white strip in the lane's ceiling over its east half, and the end wall.
box(9, W - 2, 4, 4, 1, 1, 'W')
box(W - 1, W - 1, 1, 2, 1, 1, 'W')

# The drain: an alcove in the concourse's east wall, one wide, two deep,
# two high, its floor white; the orange gel drips onto its back cell.
box(11, 12, 1, 2, 7, 7, '.')
box(11, 12, 0, 0, 7, 7, 'W')
g[3][7][12] = 'Z'
# FIX (fizzler): the drain is staff only. A fizzler fills it, so whoever
# walks in, or comes up out of its floor, has no portals left. In the draft
# it was open: a second floor portal for the player, and two floor portals
# give back more than a fall puts in (cheese-drain.txt).
if not DRAFT:
    box(11, 12, 1, 2, 7, 7, 'F')

g[1][5][5] = 'S'

HEAD = """name: Rush hour
hint: Walk in, and a floor portal is a step. Run in, and it is an escalator.
story: The last train home leaves the upper platform in one minute, [Subject-Name-here]. The escalator is out of order. So, as of tonight, is your season ticket.
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
                                                             'rush-hour.txt')
    with open(out, 'w') as f:
        f.write(body)


if __name__ == '__main__':
    main()
