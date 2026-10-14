#!/usr/bin/env python3
# Generates the Night shift DLC chamber. g[y][z][x]; row 0 of a layer is z = D-1.
#   python3 gen.py [--flawed] [SOLUTION.txt [OUT.txt]]
# writes the chamber with the solution after it: by default fixed.txt (copy
# it to dlc/after-hours/night-shift.txt), with --flawed the first draft,
# flawed.txt. Without SOLUTION.txt it keeps the solution OUT already has.
import sys, os

args = sys.argv[1:]
FLAWED = '--flawed' in args
args = [a for a in args if a != '--flawed']

W, H, D = 20, 9, 30
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The start: x1..18, z1..5, floor top y=2, air up to layer 7.
box(1, 18, 2, 7, 1, 5, '.')
# The guard hall: a goo pit, x1..13, z6..22, air from layer 1.
box(1, 13, 1, 7, 6, 22, '.')
box(1, 13, 0, 0, 6, 22, '~')
# The duct, behind the dividing wall (x=14): goo, x15..18, air layers 1..4.
box(15, 18, 1, 4, 6, 22, '.')
box(15, 18, 0, 0, 6, 22, '~')
# The far side, z23..28, floor top y=2. East of the dividing wall (x=14,
# on to z=24), the duct's landing. West of it, the watch's post: a step a
# metre up (z23..24), too high to climb from the pit; a goo moat behind it,
# and the exit in a booth on an island in the far corner, behind door b.
box(1, 18, 2, 7, 23, 28, '.')
box(14, 14, 2, 7, 23, 24, '#')
box(1, 13, 2, 2, 23, 24, '#')
box(4, 14, 1, 1, 25, 28, '.')
box(4, 14, 0, 0, 25, 28, '~')
box(1, 3, 1, 1, 28, 28, 'E')
box(1, 3, 2, 3, 27, 27, 'b')
# Its button, for a cube -- or a turret. In the draft it was in the middle
# of the island, so a turret put on it faced the way to the door.
bx = 2 if FLAWED else 1
g[1][26][bx] = 'K'
g[2][26][bx] = '2'
# Door a, across the duct: it stops the bridge too.
box(15, 18, 1, 4, 19, 19, 'a')
# A laser field along the post's front, above the bridges' height: no
# climbing up to the watch from the pit, with a cube or without. (Not in
# the draft: a cube held up to one turret while the other's fire is
# outrun, then stood on to climb the post, is a way past them -- not
# proven, as a solution cannot strafe.)
if not FLAWED:
    box(1, 13, 3, 4, 22, 22, '*')
# The watch: two turrets on the post, facing the start.
g[3][23][8] = 't'
g[3][23][11] = 't'
# A white panel on the landing's east wall, behind the watch, and a stub
# of wall at the duct's mouth that hides it from the duct: the bridge
# behind the watch is laid from the landing, not from the start. (In the
# draft it could be shot up the open duct from the start, and the duct
# skipped by walking into the dividing wall's portal: cheese-duct-skip.txt.)
box(19, 19, 2, 3, 25, 25, 'W')
if not FLAWED:
    box(17, 18, 2, 7, 23, 24, '#')
# The guard post: a pillar in the pit, with the cube in a niche in its
# west side.
box(6, 8, 1, 7, 11, 13, '#')
g[2][11][6] = 'C'

# The light bridge: out of the hall's west wall, east across the near end
# of the pit, onto a white panel on the dividing wall.
g[2][7][0] = 'H'
box(14, 14, 2, 3, 7, 7, 'W')
# White panels low on the start's south wall: one in line with the pit,
# beside the pillar, and one in line with the duct.
box(5, 5, 2, 3, 0, 0, 'W')
box(16, 16, 2, 3, 0, 0, 'W')

# The cube's button, in the start's floor.
g[1][3][11] = 'K'
g[2][3][11] = '1'

g[2][2][2] = 'S'

NAME = "Night shift"
HINT = "The night watch only looks one way. Send the bridge the other way."
STORY = ("The facility never sleeps, [Subject-Name-here]. Neither do the turrets: they are on the night shift. "
         "So are you. Only one of you gets a pension.")
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
if args:
    sol = open(args[0]).read()
    if '\nsolution\n' in sol:  # a chamber file: its solution
        sol = sol.split('\nsolution\n', 1)[1]
else:
    sol = open(out).read().split('\nsolution\n', 1)[1]
body = HEAD + '\n' + layers()
with open(out, 'w') as f:
    f.write(body + '\nsolution\n' + sol)
