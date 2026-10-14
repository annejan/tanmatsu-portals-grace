#!/usr/bin/env python3
"""Generator for the After hours chamber "Bowling night".

The night shift's bowling league. You stand on a gallery over the alley,
whose floor is goo. Down in the alley runs the lane: a strip of floor in
a glass case that stands to the roof, with two cups on it -- the near one
at z 7, the far one against the end wall at z 14. Door a, the way out,
wants a sphere in both.

The gallery's approach is a channel one cell wide along the alley's glass
rail. At its end, the foul line: a laser field over the ball return, a
shaft five metres down to a white floor. High in the alley's south wall,
in line with the lane, is one white panel.

So: orange on the panel (from the lobby: the case hides it from the
approach), blue on the floor of the ball return; carry a ball into the
approach and bowl it. It falls five metres, comes out of the panel at
nearly 13 m/s, flies over the goo into the case, over the near cup, and
rolls on into the far cup: the strike. The second ball goes the same way,
hits the first in its cup, bounces back down the lane at four tenths of
its speed and stops in the near cup: the spare. Where a ball lands
depends on how it was bowled (9.2 to 13.6 m, faster lands shorter: it
comes off the ball return's end wall); the near cup is short of all of
it and the far cup past it, and the bounce always carries back to the
near cup. Checked for every bowl from 0.5 to 5 m/s at 20 to 50 frames a
second, with either cup already full, and for two balls bowled back to
back: every ball that reaches the ball return ends in a cup.

What a ball cannot do: roll there (goo; and you cannot cross the foul
line); be carried there (you cannot leave the gallery: the only white
surfaces are the ball return and the panel, and you reach neither); be
thrown there (see below). A ball lost in the goo comes back to the rack.
A ball that trickles out of the panel (one left lying in the ball return
before the portals were open) drops into the goo short of the case.

The draft's case began at the near cup, open there: a ball thrown on a
jump from the lobby's north-west corner cleared the rail, passed south
of the case, went in at its mouth on the slant and came down in the near
cup -- the spare by hand, no bounce (cheese-spare-by-hand.txt, all
twelve jitter seeds). The final runs the case south to z 2, two metres
short of the panel: a thrown ball can only reach its mouth moving west,
and has five metres of goo to cross inside it. 1440 variants of the
throw (start, angle, run-up, release) fail on the final. Also hunted: a
jump off a ball over the rail (into the goo; the case has no top to land
on), shots through the case (glass stops them), holding a ball through
the portals (neither is within reach).

    python3 gen.py [--flawed] [SOLUTION.txt [OUT.txt]]

writes the chamber with the solution after it: by default fixed.txt (copy
it to dlc/after-hours/bowling-night.txt), with --flawed the first draft,
flawed.txt. SOLUTION.txt may be a chamber file: its solution is used
(`-` for none). Without it, OUT keeps the solution it has.

x goes east, y up, z north; one cell a metre. g[y][z][x]; row 0 of a
written layer is z = D-1 (the far, north side), as the game reads it.
"""
import os
import sys

args = sys.argv[1:]
FLAWED = '--flawed' in args
args = [a for a in args if a != '--flawed']

W, H, D = 19, 11, 18
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


FLOOR = 2          # the alley's goo is this layer: its top is y 3
BAL = 5            # the gallery's floor is this layer: its top is y 6
TOP = 9            # the last air layer; the roof is layer 10

# The alley: goo, open to the roof, x 1..8, z 1..16.
box(1, 8, FLOOR, FLOOR, 1, D - 2, '~')
box(1, 8, FLOOR + 1, TOP, 1, D - 2, '.')

# The gallery's lobby, x 10..15, z 1..3, and the approach: a channel one
# cell wide (x 10) from the lobby north, the alley's glass rail (x 9) on
# its west, a wall on its east. The rail runs the lobby's length too.
CX, C0, C1 = 10, 4, 11     # the channel; the ball return is z C1-1..C1
box(CX, 15, BAL + 1, TOP, 1, 3, '.')
box(CX, CX, BAL + 1, TOP, C0, C1, '.')
box(9, 9, BAL + 1, TOP, 1, C1, '.')
box(9, 9, BAL + 1, BAL + 1, 1, C1, 'G')

# The ball return: a shaft down to a white floor at y 1, under the foul
# line, a laser field across the channel's last two cells.
box(CX, CX, 1, BAL, C1 - 1, C1, '.')
box(CX, CX, 0, 0, C1 - 1, C1, 'W')
box(CX, CX, BAL + 1, BAL + 1, C1 - 1, C1, '*')

# The launch panel, high in the alley's south wall, in line with the lane:
# over the eye, so the rail does not hide it.
LX, LY = 6, 7
box(LX, LX, LY, LY + 1, 0, 0, 'W')

# The lane: a strip of floor at x LX in a glass case to the roof, with
# two cups on it, the near one at z L0 and the far one at the end (z L1).
# The case runs south over the goo to z T0, two metres short of the
# panel, and is open only there: the case keeps every ball out that does
# not come out of the panel. (The draft's case began at the near cup.)
L0, L1 = 7, 14
T0 = L0 if FLAWED else 2
box(LX - 1, LX + 1, FLOOR + 1, TOP, T0, L1 + 1, 'G')
box(LX, LX, FLOOR + 1, TOP, T0, L1, '.')
box(LX, LX, FLOOR, FLOOR, L0, L1, '#')
for z in (L0, L1):
    g[FLOOR][z][LX] = '@'
    g[FLOOR + 1][z][LX] = '1'

# The way out: door a in the lobby's east wall, the exit behind it.
box(16, 16, BAL + 1, BAL + 2, 1, 2, 'a')
box(17, 17, BAL + 1, BAL + 3, 1, 2, '.')
box(17, 17, BAL, BAL, 1, 2, 'E')

# The two spheres on their rack, and you.
g[BAL + 1][3][12] = 'o'
g[BAL + 1][3][14] = 'o'
g[BAL + 1][1][13] = 'S'

NAME = "Bowling night"
HINT = "Gravity bowls harder than you do. The second ball learns from the first."
STORY = ("The night shift bowls on Thursdays, [Subject-Name-here]. Two balls, two cups, and nobody "
         "crosses the foul line. Nobody crosses it twice.")
HEAD = """name: %s
hint: %s
story: %s
size: %d %d %d
facing: north
""" % (NAME, HINT, STORY, W, H, D)


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
if args and args[0] != '-':
    sol = '\n' + open(args[0]).read()
    if '\nsolution\n' in sol:
        sol = sol.split('\nsolution\n', 1)[1]
elif not args and os.path.exists(out) and '\nsolution\n' in open(out).read():
    sol = open(out).read().split('\nsolution\n', 1)[1]
else:
    sol = ''
body = HEAD + '\n' + layers()
with open(out, 'w') as f:
    f.write(body + ('\nsolution\n' + sol.strip() + '\n' if sol.strip() else ''))
