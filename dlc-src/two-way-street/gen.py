#!/usr/bin/env python3
"""Generator for the After hours chamber "Two-way street".

Two floors with a goo pit between them, six metres across. On the near
one, where you start, an excursion funnel stands up out of the floor into
the ceiling, and the way out is behind door a, which wants a cube on
button 1. On the far one: the only cube, cube button 2, and in a cupboard
in the north wall, behind door b, which button 2 opens, pedestal button 3.
The chamber has `funnel: 3`: while the pedestal ticks, the funnel runs
backwards, towards its emitter.

The only white is two patches in the ceiling, one over the funnel and one
over the far floor. A portal in each: the funnel goes up at home and comes
down over there, onto button 2. That is the way out, and the only one.
The way back is the same street the other way:

  1. Ride the funnel out.
  2. Put the cube in the funnel: it comes down on button 2, and the
     cupboard opens.
  3. Press the pedestal. The funnel turns round, lifts the cube off the
     button (the door shuts; the pedestal keeps ticking) and takes it
     home. Step in after it.
  4. At home, take the cube off the emitter before the time runs out --
     or the street turns round under it and it goes back out, onto
     button 2 again -- and put it on button 1.

Cheese hunted, none proven: the pit is six metres (a running jump makes
three); the far ceiling is six metres up (a cube and a jump make two);
the pedestal does not press through its shut door; the only white is the
two patches, and nothing walks into a ceiling portal, so the funnel is
the only way across either way, and only the pedestal turns it round. A
cube lost in the goo comes back over there, where it started.

The first draft (flawed.txt) had button 2 off the funnel, a metre from
the cupboard door, and you carried the cube home. Two flaws, neither a
cheese:
  - a soft-lock: stand in the doorway (a door does not shut on you), take
    the cube off button 2 from there, put it on the pedestal and step
    out. The door shuts on a cube no one can reach again.
  - a carried cube rose through the far ceiling portal only if you faced
    north on the way up: facing any other way, the cube, held 1.3 m in
    front of you, met the ceiling beside the portal and was knocked out
    of your hands, and you arrived home without it.
The final has button 2 under the funnel, three metres and more from the
doorway: the funnel takes the cube home by itself, centred, and no one
in the doorway can reach it. The draft also started you right behind the
funnel, which hid the room.

x goes east, y up, z north; one cell a metre. g[y][z][x]; row 0 of a
written layer is z = D-1 (the far, north side), as the game reads it.

    python3 gen.py
        writes flawed.txt (the first draft) and fixed.txt here, and the
        fixed chamber to dlc/after-hours/two-way-street.txt, each with its
        intended solution;
    python3 gen.py VARIANT SOLUTION.txt OUT.txt
        writes VARIANT (`fixed` or `flawed`) with another solution.
"""
import os
import sys

W, H, D = 15, 9, 25
CEIL = H - 1     # the ceiling layer
FLOOR = 1        # the floor layer of both floors: you walk on y = 2
HOME = (1, 8)    # z of the near floor
PIT = (9, 14)    # z of the goo pit
FAR = (15, 21)   # z of the far floor; the north wall, with the cupboard, at 22
EX, EZ = 8, 4    # the funnel emitter, in the near floor, open upwards
FX, FZ = 8, 18   # the far ceiling patch: (FX, FZ) and (FX, FZ + 1)

NAME = "Two-way street"
FACING = "north"
TIMER = 15
FUNNEL = 3
HINT = "The funnel runs both ways. Not at the same time, and not for long."
STORY = ("After hours the traffic runs one way, [Subject-Name-here]: out. Coming back is a privilege. "
         "We keep it in a cupboard.")

# Where each variant puts things: the cube, button 2, the cupboard (x in
# the north wall), the start (x, z 1).
LAYOUT = {
    # Button 2 under the funnel where it comes down over there (with both
    # portals shot looking north, as from home), three metres and more
    # from the cupboard's doorway.
    'fixed': dict(cube=(4, 16), b2=(FX, FZ), cx=9, sx=10),
    'flawed': dict(cube=(11, 17), b2=(5, 20), cx=6, sx=8),
}

SOLUTION = """// One: a portal over the funnel, one over the far floor. The funnel
// goes up at home and comes down over there.
shoot blue 8.5 8.0 5.0
shoot orange 8.5 8.0 19.0
walk_to 8.5 4.5
wait 2.0
// Two: the cube into the funnel: it comes down on button 2, and the
// cupboard opens.
walk_to 5.5 17.0
grab
walk_to 8.5 17.2
look 8.5 3.0 18.5
wait 0.3
use
wait 1.5
// Three: the pedestal. The funnel turns round, and takes the cube home.
walk_to 9.5 21.4
look 9.5 3.1 23.5
wait 0.2
use
// Four: and you after it.
walk_to 8.5 18.5
wait 3.6
walk_to 7.2 4.5
grab
// Five: the cube on button 1, and out.
walk_to 5.4 5.5
look 4.5 2.6 5.5
wait 0.5
use
wait 0.5
walk_to 3.5 3.5
walk_to 0.5 3.5
"""

# The draft's: the cube carried home, through the far ceiling portal --
# facing north, the one way it got through.
DRAFT_SOLUTION = """// One: a portal over the funnel, one over the far floor. The funnel
// goes up at home and comes down over there.
shoot blue 8.5 8.0 5.0
shoot orange 8.5 8.0 19.0
walk_to 8.5 4.5
wait 2.0
// Two: the cube on button 2: the cupboard opens. Press the pedestal: the
// funnel turns round.
walk_to 10.5 17.5
grab
walk_to 6.4 20.5
look 5.5 2.6 20.5
wait 0.5
use
wait 0.5
walk_to 6.5 21.4
look 6.5 3.1 23.5
wait 0.2
use
// Three: the cube off the button again, and home the way you came.
walk_to 6.4 20.5
grab
walk_to 8.5 18.5
face 0 0
wait 4.5
// Four: the cube on button 1, and out.
walk_to 5.4 5.5
look 4.5 2.6 5.5
wait 0.5
use
wait 0.5
walk_to 3.5 3.5
walk_to 0.5 3.5
"""


def solution(variant):
    return DRAFT_SOLUTION if variant == 'flawed' else SOLUTION


def build(variant):
    lay = LAYOUT[variant]
    g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]

    def box(x0, x1, y0, y1, z0, z1, ch):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                for x in range(x0, x1 + 1):
                    g[y][z][x] = ch

    # The room: x 3..13, air from the floor to the ceiling, both floors
    # and the pit.
    box(3, 13, FLOOR + 1, CEIL - 1, HOME[0], FAR[1], '.')
    # The pit: a metre down to the goo.
    box(3, 13, FLOOR, FLOOR, PIT[0], PIT[1], '.')
    box(3, 13, 0, 0, PIT[0], PIT[1], '~')

    # The funnel, up out of the near floor; the patch over it.
    g[FLOOR][EZ][EX] = '%'
    box(EX, EX, CEIL, CEIL, EZ, EZ + 1, 'W')
    # The far patch.
    box(FX, FX, CEIL, CEIL, FZ, FZ + 1, 'W')

    # The way out: door a in the west wall, the exit behind it; button 1.
    box(2, 2, FLOOR + 1, FLOOR + 2, 3, 3, 'a')
    box(1, 1, FLOOR + 1, FLOOR + 2, 3, 3, '.')
    g[FLOOR][3][1] = 'E'
    g[FLOOR][5][4] = 'K'
    g[FLOOR + 1][5][4] = '1'

    # Over there: the cube, button 2, and the cupboard with the pedestal
    # behind door b, in the north wall.
    cx, cz = lay['cube']
    g[FLOOR + 1][cz][cx] = 'C'
    bx, bz = lay['b2']
    g[FLOOR][bz][bx] = 'K'
    g[FLOOR + 1][bz][bx] = '2'
    x = lay['cx']
    box(x, x, FLOOR + 1, FLOOR + 2, FAR[1] + 1, FAR[1] + 1, 'b')
    g[FLOOR + 1][FAR[1] + 2][x] = 'I'
    g[FLOOR + 2][FAR[1] + 2][x] = str(FUNNEL)

    g[FLOOR + 1][1][lay['sx']] = 'S'
    return g


def text(g, solution):
    head = ("name: %s\nhint: %s\nstory: %s\nsize: %d %d %d\nfacing: %s\ntimer: %d\nfunnel: %d\n"
            % (NAME, HINT, STORY, W, H, D, FACING, TIMER, FUNNEL))
    out = [head]
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    body = '\n'.join(out)
    return body + '\nsolution\n' + solution.strip() + '\n'


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    if len(sys.argv) > 3:
        with open(sys.argv[3], 'w') as f:
            f.write(text(build(sys.argv[1]), open(sys.argv[2]).read()))
        return
    for variant in ('flawed', 'fixed'):
        with open(os.path.join(here, variant + '.txt'), 'w') as f:
            f.write(text(build(variant), solution(variant)))
    with open(os.path.join(here, '..', '..', 'dlc', 'after-hours', 'two-way-street.txt'), 'w') as f:
        f.write(text(build('fixed'), solution('fixed')))


if __name__ == '__main__':
    main()
