#!/usr/bin/env python3
"""Generator for the Human in the loop round "Wet floor" (flawed).

Facilities has mopped the test wing with blue repulsion gel. The gel is in
a mop cupboard under the mezzanine: one metre wide, a white floor, a
dispenser dripping onto its front cell. The cube button is up on the
mezzanine, three metres over the hall; the exit is in the store room under
it, behind door a. Nothing takes a portal but the cupboard's floor and a
white strip in the hall's ceiling.

The meant way (solution.txt): a portal in the cupboard's floor, under the
drip, and one in the ceiling strip over the hall in front of the mezzanine.
The gel falls through and mops a patch of floor there blue. Take the cube
and a running jump off the patch: 3.7 m up, cube and all, onto the
mezzanine. The cube on its button opens the store room.

GLaDOS's draft has two flaws, both proven:

- a, cheese-splash.txt: the drip lands on the cupboard's front cell, and a
  splash paints 3 x 3: the hall floor at the cupboard's door is blue from
  the start. Stand in that puddle with the cube and jump. Fixed by glass:
  the three cells of floor at the cupboard's door take no paint.
- b, cheese-mop-sink.txt: the ceiling strip runs on over the mezzanine.
  Portal the cupboard's floor and the ceiling over the button, hold the
  cube out over the cupboard's floor and let go: it drops out of the
  ceiling onto the button, and the player never leaves the floor. Fixed by
  a fizzler across the cupboard's door: the gel still goes in, nothing
  you carry does.

Unlisted, in the draft: walking into the cupboard's floor yourself, cube
and all, and dropping out over the mezzanine. The drip lands where you do,
so you bounce between the ceiling and the cupboard a while before you come
down on the mezzanine: a novel exploit ("no cube portal" keeps it from
passing as the meant way). The fizzler closes it too. Tried on the fixed chamber and failed: carrying the cube in through
the fizzler; seeing the ceiling strip from inside the cupboard (it is a
metre wide: no line out of it reaches the strip); a cube held up and let go
at the top of a plain jump reaches the mezzanine's lip (z 11.9-12.4), but
the button is two metres in and the player still needs the gel to follow.

g[y][z][x]; row 0 of a layer is z = D-1.

    python3 gen.py      -> dlc/human-in-the-loop/wet-floor.txt, with
                           solution.txt and the cheese files as its routes
"""
import os

W, H, D = 14, 8, 18
CEIL = 7          # hall: air layers 1..CEIL-1, floor top y = 1, ceiling 6 m up
HX = 10           # the hall: x 1..HX
MZ = 12           # the mezzanine's south face (z); its top 3 m up (y = 4)
CX = 3            # the mop cupboard: x = CX, z MZ..MZ+1, two metres high
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The test wing: a hall, and at its north end a mezzanine three metres up.
box(1, HX, 1, CEIL - 1, 1, D - 2, '.')
box(1, HX, 1, 3, MZ, D - 2, '#')

# The floor drain: a gutter of goo along the west wall, where the mop
# water was meant to go.
box(1, 1, 0, 0, 2, MZ - 3, '~')

# The mop cupboard, under the mezzanine: one metre wide, two deep, two
# high, a white floor. Blue gel drips from its ceiling onto the front cell
# -- and splashes the floor at its door.
box(CX, CX, 1, 2, MZ, MZ + 1, '.')
box(CX, CX, 0, 0, MZ, MZ + 1, 'W')
g[3][MZ][CX] = 'U'

# The store room under the mezzanine: door a, and the exit at the back.
box(6, 7, 1, 2, MZ, MZ + 3, '.')
box(6, 7, 1, 2, MZ, MZ, 'a')
box(6, 7, 0, 0, MZ + 2, MZ + 3, 'E')

# A glass rail along the mezzanine over the store room's door.
box(5, 7, 4, 4, MZ, MZ, 'G')
# Shelving at the back of the mezzanine: glass cases.
box(1, 2, 4, 5, D - 2, D - 2, 'G')
box(5, 6, 4, 5, D - 2, D - 2, 'G')

# Pilasters along the hall's walls, either side of the office window and
# at the foot of the mezzanine.
for x, z in ((HX, 2), (HX, MZ - 2), (1, MZ - 2), (1, 1)):
    box(x, x, 1, CEIL - 1, z, z, '#')

# The cube button, up on the mezzanine.
g[3][MZ + 2][9] = 'K'
g[4][MZ + 2][9] = '1'

# Facilities' office: a booth up in the east wall, behind glass.
box(HX + 2, HX + 2, 3, 5, 3, 9, '.')
box(HX + 1, HX + 1, 3, 5, 3, 9, 'G')

# The white strip in the hall's ceiling, running on over the mezzanine.
box(8, 9, CEIL, CEIL, 5, D - 3, 'W')

# The cube dropper.
g[CEIL][3][4] = 'V'

g[1][2][5] = 'S'

HEAD = """name: Wet floor
hint: The mop water is in the cupboard. The floor that needs it is out in the hall.
story: Facilities has mopped the test wing, [Subject-Name-here]. With repulsion gel. Facilities does not read labels. Caution: wet floor. Extremely wet. Upwards.
review: flawed
intended: gel portal, bounce, no cube portal, button 1 by cube, door a
flaw: a bounce, no gel portal -- The cupboard splashed its own doorstep: you jumped off that puddle, and no gel went through a portal.
flaw: b cube portal, no bounce -- Posted the cube down the mop sink: the ceiling dropped it on the button. You never left the floor.
fix: a 2-4 0 11 #G
fix: b 3 1-2 12 .F
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
    here = os.path.dirname(os.path.abspath(__file__))
    body = HEAD + '\n' + layers()

    def sect(name, fn):
        p = os.path.join(here, fn)
        return '\n' + name + '\n' + open(p).read().strip() + '\n' if os.path.exists(p) else ''

    body += sect('solution', 'solution.txt')
    body += sect('cheese a', 'cheese-splash.txt')
    body += sect('cheese b', 'cheese-mop-sink.txt')
    out = os.path.join(here, '..', '..', 'dlc', 'human-in-the-loop', 'wet-floor.txt')
    with open(os.path.normpath(out), 'w') as f:
        f.write(body)


if __name__ == '__main__':
    main()
