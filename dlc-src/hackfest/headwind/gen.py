#!/usr/bin/env python3
# Generates the "headwind" chamber (Hackfest pack: geeky chores).
# g[y][z][x]; row 0 of a printed layer is z = D-1 (the far side).
#   python3 gen.py > headwind.txt
#
# The bike path runs north on a high bank (top y=5) and ends at an open
# bridge: seven metres of canal (goo) and a four metre drop to the far bank,
# where the exit is. Walking pace, even with a jump, falls short. Orange gel
# drips, out of reach, onto a white island in the canal. A portal on the
# island and one in the white strip over the path pour the gel onto the
# path; three patches make a run-up, and a jump at its end flings you over
# the canal. (Running off the end without a jump is not enough: the last
# step, off the paint, takes the edge off the speed.)

W, H, D = 16, 9, 24
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The hall: air from layer 1 to 7, goo on the floor (the canal and its banks).
box(1, W - 2, 1, H - 2, 1, D - 2, '.')
box(1, W - 2, 0, 0, 1, D - 2, '~')

# The bike path: x1..5, z1..11, top y=5 (layers 0..4 solid).
box(1, 5, 0, 4, 1, 11, '#')

# The far bank: the whole width, z19..22, top y=1; the exit at the back.
box(1, W - 2, 0, 0, 19, 22, '#')
box(2, 4, 0, 0, 21, 22, 'E')

# The island where the gel drips: x13..14, z6..8, white, at goo level,
# seven metres of goo from the path: too far to jump down to.
box(13, 14, 0, 0, 6, 8, 'W')
g[H - 1][7][13] = 'Z'

# White strip in the roof over the path, up to its very end.
box(2, 4, H - 1, H - 1, 2, 11, 'W')

g[5][2][3] = 'S'

HEAD = """name: 160 km of headwind
hint: Orange gel makes you fast. Pour a run-up on the path, and jump at its end.
story: Route to Hackfest: 160 km, all headwind, and the bridge is open. Again. Badge suggests orange gel and a run-up, [Subject-Name-here]. Pedal harder.
size: %d %d %d
facing: north
""" % (W, H, D)

SOLUTION = """\
// The island: a portal where the gel drips.
shoot blue 13.6 1.0 7.5
// The roof over the end of the path. Looking east, the portal lies
// across the path, so the gel lands right under the cell shot.
walk_to 3.5 10.5
shoot orange 4.4 8.0 10.5
wait 1.6
// Move it back twice: nine metres of orange in a row.
walk_to 3.5 7.5
shoot orange 4.4 8.0 7.5
wait 1.6
walk_to 3.5 4.5
shoot orange 4.4 8.0 4.5
wait 1.6
// Back to the start, full speed down the path, and jump at the end.
// (The jump lands from 0.9 s to 1.3 s of running.)
walk_to 3.5 1.6
wait 0.5
face 0 0
walk 1.1
jump
walk_to 3.5 22.5
wait 1
"""


def layers():
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    return '\n'.join(out)


print(HEAD + '\n' + layers() + 'solution\n' + SOLUTION, end='')
