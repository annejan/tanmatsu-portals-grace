#!/usr/bin/env python3
"""Generator for the Human in the loop round "Second opinion" (broken).

The waiting room of an Aperture infirmary. Benches, a reception desk, and
along the north side a consulting floor three metres up: the consulting
room (the exit) behind door a, opened by a cube button out on the floor.
Under the consulting floor, behind the pharmacy window, the dispensary;
over it, in a glass column, the cube dispenser. A hatch in the consulting
floor drops into the dispensary; a white panel in the dispensary's back
wall is the way out again.

The draft is broken in two places:

- the dispenser's column ends in a fizzler (the quality-control grill, in
  the floor of the consulting floor): every cube it drops is fizzled on
  the way down, and dropped again, for ever.
- nothing on the consulting floor takes a portal: the panel GLaDOS meant
  for its back wall is metal. Nothing in the waiting room is higher than a
  jump, so there is no way up.

Either one alone leaves it unsolvable: no cube, no door a; and without
the way up there is no way to the dispensary either (its window is glass,
its hatch is upstairs). The debugger allows three changes, `#W` and `F.`:
the grill out (one), the back-wall panel in (two).

Repaired, the meant way: a portal on the waiting room's west wall and one
on the new panel; walk up. Drop through the hatch into the dispensary,
take the cube, shoot the dispensary's own panel with the colour that is
downstairs (keep the one upstairs) and walk out with it onto the
consulting floor. The cube on button 1, door a opens, the exit.

Tried and failed: with only the panel repaired, grabbing the falling cube
from the dispensary through the grill (tried-grab-through-grill.txt): it
is picked up, but the only way down is through the grill, and it fizzles.
With only the grill repaired there is nothing white above jump height, and
the benches are a metre high (a jump is 0.83 m); the cube lies behind the
pharmacy window's glass, out of reach.

g[y][z][x]; row 0 of a layer is z = D-1.

    python3 gen.py        -> ../../dlc/human-in-the-loop/second-opinion.txt
"""
import os

W, H, D = 16, 9, 16
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# --- The waiting room: x1..14, z1..9, floor top y = 1, 7 m high ----------
box(1, 14, 1, 7, 1, 9, '.')
# Benches, a metre high (no one jumps a metre), in two rows.
for z in (5, 7):
    box(3, 6, 1, 1, z, z, '#')
    box(9, 12, 1, 1, z, z, '#')
# The reception desk in the south-east corner, a glass screen on it.
box(11, 14, 1, 1, 2, 2, '#')
box(11, 11, 1, 1, 1, 1, '#')
box(12, 14, 2, 2, 2, 2, 'G')
# A white panel low on the west wall: room for one portal, or two.
box(0, 0, 1, 2, 3, 4, 'W')

# --- The consulting floor: x1..14, z10..14, its floor top y = 4 ---------
box(1, 14, 4, 7, 10, 14, '.')
# A glass balustrade along its edge, over the pharmacy window.
box(11, 14, 4, 4, 10, 10, 'G')
# The back-wall panel GLaDOS meant (metal in the draft: the repair).
PANEL = (8, 5, 6, 15)  # x, y0, y1, z

# The consulting room, the exit: x1..4, z11..14, walled off, door a.
box(1, 5, 4, 7, 10, 10, '#')
box(5, 5, 4, 7, 10, 14, '#')
box(5, 5, 4, 5, 12, 12, 'a')
box(5, 5, 5, 5, 13, 14, 'G')   # a window onto it from the floor
box(2, 3, 5, 5, 10, 10, 'G')   # and one onto the waiting room
box(2, 3, 3, 3, 12, 13, 'E')
# The cube button out on the floor.
g[3][12][7] = 'K'
g[4][12][7] = '1'

# --- The dispensary, under the floor: x10..13, z11..13, 2 m high -------
box(10, 13, 1, 2, 11, 13, '.')
box(10, 13, 1, 2, 10, 10, 'G')  # the pharmacy window
g[1][11][14] = 'W'               # its one way out: one portal, no more
g[2][11][14] = 'W'
g[3][12][13] = '.'               # the hatch in the floor above
g[3][13][13] = '.'

# The dispenser: a glass column on the floor, the dropper at its top, and
# under it, in the floor, the grill that fizzles every cube (the break).
box(10, 12, 4, 7, 11, 13, 'G')
box(11, 11, 4, 7, 12, 12, '.')
g[8][12][11] = 'V'
GRILL = (11, 3, 12)
g[3][12][11] = 'F'

g[1][2][7] = 'S'

NAME = "Second opinion"
HINT = "Two faults. One is in the pipe. The other is where you would aim."
STORY = ("My verifier says this draft cannot be solved. I would like a second opinion, "
         "[Subject-Name-here]. If it agrees with the first, I will get a third.")
assert len(NAME) <= 31 and len(HINT) <= 79 and len(STORY) <= 159, (len(NAME), len(HINT), len(STORY))

HEAD = """name: %s
hint: %s
story: %s
review: broken
debug: 3 #W F.
repair: %d %d %d F.
repair: %d %d-%d %d #W
size: %d %d %d
facing: north
""" % (NAME, HINT, STORY, GRILL[0], GRILL[1], GRILL[2], PANEL[0], PANEL[1], PANEL[2], PANEL[3], W, H, D)


def layers():
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    return '\n'.join(out)


here = os.path.dirname(os.path.abspath(__file__))
sol = open(os.path.join(here, 'solution.txt')).read()
with open(os.path.join(here, '..', '..', 'dlc', 'human-in-the-loop', 'second-opinion.txt'), 'w') as f:
    f.write(HEAD + '\n' + layers() + '\nsolution\n' + sol)
print('story', len(STORY), 'hint', len(HINT))
