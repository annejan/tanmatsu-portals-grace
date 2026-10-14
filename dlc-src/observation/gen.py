#!/usr/bin/env python3
"""Human in the loop: "Observation". A flawed round.

A test chamber overlooked by its observation room: a long window two
metres up in the north wall, consoles behind it, nobody at them. Under
the window, the staff door (door a), with glass either side of it: the
exit room, under the observation room, shows through.

The laser is high in the west wall, over everyone's head, and fires
across the chamber into a white strip high in the east wall. The catcher
stands in a recess low in the east wall, facing west. A low white panel
in the south wall, Pw, sends the beam north at catcher height -- but
along the wrong line: the catcher is east. Portals move a beam; only a
cube turns one.

The meant way: carry the reflection cube (north-west corner) into Pw's
line, facing east, square to the catcher; then a portal in the strip and
one in Pw. Out of Pw, into the cube, east into the catcher: door a opens.
You stand west of the cube, out of both legs of the beam, and walk to
the door.

GLaDOS's draft has two flaws:

  a  A second low panel, in the west wall, square to the catcher. Strip
     and that panel: the beam goes straight into the catcher, and nobody
     carries anything. Fix: metal there.
  b  One pane of the observation window is missing, over the door, and
     the observation room's back wall is white. A portal through the
     gap: into the observation room, down the observers' stairwell into
     the exit room, and the door never opened. Fix: the pane.

Hunted on the final (both fixed), and not found: the beam into the catcher
with no cube (the strip takes the laser; Pw is the only other panel, and
it is not square to the catcher); the cube pushed into the line (it starts
facing north, and only carrying turns it); the observation room (glass
stops shots, and it is two metres up: a jump from the cube reaches 1.4 m);
through the strip to anywhere (out of it you fall onto the chamber floor).
Every way out of the final needs door a, so the catcher, so the cube
turned in the beam: a pickup. (In the draft, "pickup" is the terms' proxy
for the cube: a player who picks the cube up and then cheeses flaw a
anyway is judged intended. Crossing a leg of the beam is survivable: a
burn, not a death, so nobody is shut in the south-east corner.)

g[y][z][x]; row 0 of a layer is z = D-1. x east, y up, z north.

    python3 dlc-src/observation/gen.py
        writes dlc/human-in-the-loop/observation.txt (the draft, with the
        fixes, the solution and the cheeses) and fixed.txt here (both
        flaws fixed in the map, with the solution).
"""
import os

W, H, D = 16, 8, 18

NAME = "Observation"
HINT = "Portals bring the beam down to you. Only a cube turns it the last corner."
STORY = ("Nobody has sat in the observation room for some time, [Subject-Name-here]. "
         "They are very busy. I do the observing now. I am observing you.")

PX, PZ = 10, 6      # Pw's column, the catcher's row: the cube goes where they cross
LZ = 9              # the laser's row
GAP = 6             # the missing pane's column
CATCH_X = 14

INTENDED = "pickup, button 1 by beam, door a"
FLAWS = [
    ("a", "button 1 by beam, no pickup",
     "No cube: a panel square to the catcher, and the beam went straight in. Nobody carried anything."),
    ("b", "no door a",
     "In through a missing pane of the observation window, and down the observers' stairs. The door never opened."),
]
FIXES = [
    ("a", "0 1-2 %d" % PZ, "W#"),
    ("b", "%d 4-5 13" % GAP, ".G"),
]

SOLUTION = """// The cube, from the north-west corner, to where Pw's line crosses the
// catcher's: set down facing east, square to it.
walk_to 2.5 9.0
grab
wait 0.4
walk_to 9.2 6.5
face 90 25
wait 0.5
use
wait 0.6
// The strip high in the east wall, and Pw low in the south wall: the beam
// comes out of Pw going north, into the cube, and east into the catcher.
shoot blue 14.0 6.0 9.5
shoot orange 10.5 2.0 1.0
wait 1.0
// West of the cube, out of the beam, to the door.
walk_to 7.0 11.5
walk_to 7.0 14.5
walk_to 3.0 16.0
"""

CHEESE_A = """// CHEESE: no cube. The strip, and the low panel in the west wall square
// to the catcher: the beam goes straight in. From the north side of it.
walk_to 5.0 9.5
shoot blue 14.0 6.0 9.5
shoot orange 1.0 2.0 6.5
wait 1.0
walk_to 7.0 11.5
walk_to 7.0 14.5
walk_to 3.0 16.0
"""

CHEESE_B = """// CHEESE: over the observation deck. A portal through the missing pane
// onto the white back wall of the observation room, one in Pw: in, along
// the room, down the stairwell, and to the exit. The door stays shut.
walk_to 6.5 5.0
shoot blue 6.5 5.0 17.0
shoot orange 10.5 2.0 1.0
walk_to 10.5 1.5
walk_to 10.5 -1.0
wait 0.5
walk_to 12.0 15.5
wait 1.0
walk_to 3.0 16.0
"""


def build(flawed):
    g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]

    def box(x0, x1, y0, y1, z0, z1, ch):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                for x in range(x0, x1 + 1):
                    g[y][z][x] = ch

    # The chamber: x 1..13, z 1..12, six metres high. The east wall is two
    # thick (x 14..15), the catcher's recess in it.
    box(1, 13, 1, 6, 1, 12, '.')

    # The laser, high in the west wall; the strip it hits, high in the east.
    g[5][LZ][0] = 'L'
    box(14, 14, 5, 6, LZ, LZ, 'W')

    # Pw, low in the south wall.
    box(PX, PX, 1, 2, 0, 0, 'W')
    # The draft's panel low in the west wall, square to the catcher (flaw a).
    if flawed:
        box(0, 0, 1, 2, PZ, PZ, 'W')

    # The catcher, in its recess in the east wall, facing west.
    g[1][PZ][CATCH_X] = 'O'
    g[2][PZ][CATCH_X] = '1'

    # The exit room, under the observation room: z 14..16, two high.
    box(2, 12, 1, 2, 14, 16, '.')
    box(2, 3, 0, 0, 15, 16, 'E')
    # The staff door under the window, glass either side of it.
    box(4, 9, 1, 2, 13, 13, 'G')
    box(6, 7, 1, 2, 13, 13, 'a')

    # The observation room: floor four metres up (the slab, layer 3, is
    # the exit room's ceiling), two high, the window along the chamber.
    box(2, 12, 4, 5, 14, 16, '.')
    box(3, 10, 4, 5, 13, 13, 'G')
    if flawed:
        box(GAP, GAP, 4, 5, 13, 13, '.')        # a pane missing (flaw b)
    box(3, 10, 4, 5, 17, 17, 'W')               # its back wall
    # Consoles under the window, where they sat.
    for x in (3, 4, 8, 9):
        g[4][14][x] = 'I'
    # The observers' stairwell, down to the exit room.
    box(11, 12, 3, 3, 15, 16, '.')

    # Pilasters along the side walls, out of every line the beam can take.
    for x, z in ((1, 3), (1, 12), (13, 3), (13, 11)):
        box(x, x, 1, 6, z, z, '#')

    # You start in the south-west, facing the window. The cube is in the
    # north-west corner.
    g[1][2][3] = 'S'
    g[1][10][2] = 'R'
    return g


def layers(g):
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    return '\n'.join(out)


def text(g, review):
    head = ["name: " + NAME, "hint: " + HINT, "story: " + STORY]
    if review:
        head.append("review: flawed")
        head.append("intended: " + INTENDED)
        for f, terms, about in FLAWS:
            head.append("flaw: %s %s -- %s" % (f, terms, about))
        for f, cells, ft in FIXES:
            head.append("fix: %s %s %s" % (f, cells, ft))
    head += ["size: %d %d %d" % (W, H, D), "facing: north", ""]
    body = '\n'.join(head) + '\n' + layers(g)
    body += '\nsolution\n' + SOLUTION.strip() + '\n'
    if review:
        body += '\ncheese a\n' + CHEESE_A.strip() + '\n'
        body += '\ncheese b\n' + CHEESE_B.strip() + '\n'
    return body


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, '..', '..', 'dlc', 'human-in-the-loop', 'observation.txt'), 'w') as f:
        f.write(text(build(True), True))
    with open(os.path.join(here, 'fixed.txt'), 'w') as f:
        f.write(text(build(False), False))


if __name__ == '__main__':
    main()
