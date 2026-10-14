#!/usr/bin/env python3
"""Generator for the After hours chamber "Crunch time".

A hall, and out of its north-east corner the press line: a corridor three
metres wide and twenty long under four crushers, which fall one after the
other, northwards, every three seconds. At its far end a strip of safe
floor, a cube button against the end wall, and door a in the west wall,
the exit behind it. The door wants the cube on its button and the
pedestal in the far south-west corner of the hall: six seconds, ticking.
On foot, from the pedestal up the line to the door, it is more than
eight, crushers or none.

Two white columns, one portal each: low in the hall's south wall beside
the pedestal ("home"), and at the back of a one-cell recess in the press
line's east wall beside the last crusher -- out of the crushers' way, and
out of sight until the end of the line. So: carry the cube up the line a
crusher at a time (they flatten it, and you, if they catch you under
them), set it on its button, shoot the recess from under the last crusher
while it is up, go home through it, press the pedestal while the last
crusher is down, and go back through the portals: out in the recess, under
the last crusher as it lifts, and through the door. About two seconds of
the six.

    python3 gen.py [--flawed] [SOLUTION.txt [OUT.txt]]

writes the chamber with the solution after it: by default fixed.txt (copy
it to dlc/after-hours/crunch-time.txt), with --flawed the first draft,
flawed.txt. SOLUTION.txt may be a chamber file: its solution is used.
Without it, OUT keeps the solution it has.

No shortcut was proven. Hunted: walking it in time (the fastest walk,
pressed at full reach on the pedestal's north-east side, is 8.2 s to the
door with the crushers taken out of the chamber, against 6.04 -- the door
starts to shut as the clock runs out, and stops only for someone already
in it); shooting the recess from the hall or the first gaps (the walls
hide it until the gap before the last crusher); wedging the door with the
cube (the cube has to be on its button to open it, and the pedestal is
the far end of the chamber away); pressing the pedestal through a portal
(Use does not reach through one); holding the cube on its button through
the portals (the button is over three metres from the recess). The first draft's hall
was six metres narrower, the press line at x15..17: the same walk there
was 6.9 s, too close to the six to leave to the crushers, so the final
moves the press line six metres east (flawed.txt is that draft, solved
the intended way).

g[y][z][x]; row 0 of a layer is z = D-1.
"""
import os
import sys

args = sys.argv[1:]
FLAWED = '--flawed' in args
args = [a for a in args if a != '--flawed']

# Everything east of the hall's middle moves this far east in the final.
E = 0 if FLAWED else 6
W, H, D = 20 + E, 7, 30
TIMER = 6
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The hall: x1..24 (x1..18 in the draft), z1..8, air layers 1..5.
box(1, 18 + E, 1, 5, 1, 8, '.')

# The press line: x21..23 (x15..17 in the draft), from the hall (z9) to the end wall (z29), air
# layers 1..4; the crushers hang in layer 5, two metres long with three
# metres of beam between them (touching, they would be one crusher; three
# metres is room to wait in, with a cube held out in front). Each falls
# four metres. The last, z25..26; beyond it the safe strip, z27..28.
X0, X1 = 15 + E, 17 + E
box(X0, X1, 1, 4, 9, 28, '.')
CRUSH = [(10, 11), (15, 16), (20, 21), (25, 26)]
for z0, z1 in CRUSH:
    box(X0, X1, 5, 5, z0, z1, 'Y')

# The recess, one cell, in the east wall beside the last crusher, a white
# column at its back: clear of the crusher, and in sight only from the
# gap before it or from under it.
RZ = CRUSH[-1][0]
box(X1 + 1, X1 + 1, 1, 2, RZ, RZ, '.')
box(X1 + 2, X1 + 2, 1, 2, RZ, RZ, 'W')

# The cube's button, on the strip against the end wall: a second button 1,
# so door a wants it and the pedestal both.
g[0][28][X0 + 1] = 'K'
g[1][28][X0 + 1] = '1'

# Door a in the west wall of the strip, the exit room behind it.
box(X0 - 1, X0 - 1, 1, 2, 27, 28, 'a')
box(X0 - 4, X0 - 2, 1, 3, 26, 28, '.')
box(X0 - 4, X0 - 2, 0, 0, 26, 28, 'E')

# The pedestal, in the hall's far south-west corner, and the white column
# beside it, low in the south wall.
g[1][2][3] = 'I'
g[2][2][3] = '1'
box(1, 1, 1, 2, 0, 0, 'W')

# The cube, and the player, in the middle of the hall.
g[1][5][8 + E // 2] = 'C'
g[1][4][10 + E // 2] = 'S'

NAME = "Crunch time"
HINT = "The deadline is six seconds away. The commute is eight. Work from home."
STORY = ("It is crunch time, [Subject-Name-here]. The deliverable goes up the press line, then you do, in six "
         "seconds. The presses never miss a deadline. Or a subject.")
HEAD = """name: %s
hint: %s
story: %s
size: %d %d %d
facing: north
timer: %d
""" % (NAME, HINT, STORY, W, H, D, TIMER)


def layers():
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    return '\n'.join(out)


assert len(NAME) <= 31 and len(HINT) <= 79 and len(STORY) <= 159, (len(NAME), len(HINT), len(STORY))
here = os.path.dirname(os.path.abspath(__file__))
out = args[1] if len(args) > 1 else os.path.join(here, 'flawed.txt' if FLAWED else 'fixed.txt')
if args:
    sol = open(args[0]).read()
    if '\nsolution\n' in sol:
        sol = sol.split('\nsolution\n', 1)[1]
elif os.path.exists(out) and '\nsolution\n' in open(out).read():
    sol = open(out).read().split('\nsolution\n', 1)[1]
else:
    sol = ''
body = HEAD + '\n' + layers()
with open(out, 'w') as f:
    f.write(body + ('\nsolution\n' + sol.strip() + '\n' if sol.strip() else ''))
