#!/usr/bin/env python3
"""Generator for the After hours chamber "Reflections".

An office hall with the lights off. One laser is still on: high in the
west wall, it fires across the hall, over everyone's head, into a white
strip high in the east wall. The strip is two cells, so the only portal
on it is centred half a metre over the beam: out of a low panel (two
cells, from the floor) the beam comes out at 1.5 m, out of a gallery
panel at 3.5 m. Portals move a beam; they do not turn it. Cubes do.

  0. You start on the gallery by the way out, and look over the hall.
  1. The store room (south-east) holds your reflection cube. Its door b
     opens while relay 2 is lit, and no panel faces relay 2. In the middle
     of the hall stands a glass case with another reflection cube in it,
     facing north, out of reach: the beam out of the west panel Pm runs
     into the case and turns north, through relay 2.
  2. The way out is on the gallery along the north wall, two metres up:
     door a, open while relay 1 and the catcher are both lit. You go up
     by portal, cube in hand: Pm to P1, the panel at the gallery's west
     end.
  3. Then the strip again, and the beam comes out of P1 along the
     gallery, through relay 1. Set the cube down in the catcher's line,
     facing north, and the beam turns through the booth's glass onto it.
     No panel faces the catcher, and the glass is too narrow for a beam
     at 45 degrees. P3, at the gallery's east end, sends the beam the
     wrong way past relay 1.

The draft had one more low panel, west at z 11, square to relay 2: the
store room opened with no need of the case (cheese-direct.txt). The
final has metal there. Checked by brute force (every portal pair, the
cube along every beam at every facing): nothing else lights relay 2, and
the catcher lights only with the cube on the gallery, x 6..7, in the line
out of P1, facing north.

x goes east, y up, z north; one cell a metre. g[y][z][x]; row 0 of a
written layer is z = D-1 (the far, north side), as the game reads it.

    python3 gen.py
        writes flawed.txt (the first draft) and fixed.txt here, and the
        fixed chamber to dlc/after-hours/reflections.txt, each with the
        intended solution, and cheese-direct.txt (the draft, cheesed);
    python3 gen.py VARIANT SOLUTION.txt OUT.txt
        writes VARIANT (`fixed` or `flawed`) with another solution, as
        the cheese scripts were.
"""
import os
import sys

W, H, D = 30, 8, 24
E = W - 1   # the east wall

NAME = "Reflections"
FACING = "225"
HINT = "Portals move a beam. Only a cube turns one. Take a moment for reflection."
STORY = ("The lights are off for the night, [Subject-Name-here]. One laser is still on: nobody can find "
         "the switch. Put it to work. It costs nothing. Unlike you.")

SOLUTION = """// From the gallery: the laser fires across the hall into the white strip
// high in the east wall. A portal there and one in the low west panel:
// the beam runs into the glass case, the cube in it turns it north,
// through relay 2. The store room opens.
shoot blue 29.0 4.6 10.5
shoot orange 1.0 1.5 6.5
// Down into the hall: your own reflection cube is in the store room.
walk_to 25.5 11.0
walk_to 23.0 2.5
walk_to 26.4 2.5
grab
walk_to 22.0 2.5
// Up to the gallery: orange to its west end, blue low in the west wall.
shoot orange 1.0 4.0 15.5
walk_to 3.0 3.5
shoot blue 1.0 1.5 6.5
walk_to 3.0 6.5
walk_to -1.0 6.5
// Out of the beam's way, then the strip again: the beam runs out of
// the gallery's west end, along it, through relay 1.
walk_to 2.5 14.0
shoot blue 29.0 4.6 10.5
// The cube goes down in the catcher's line, facing north.
walk_to 6.5 14.2
face 0 0
wait 0.4
use
wait 0.5
// Round the cube, not through the beam, to the door.
walk_to 23.0 14.2
walk_to 23.0 21.0
"""


# The cheese on the draft: the same, but the store room opened by the
# panel square to relay 2.
CHEESE_DIRECT = SOLUTION.replace("""// From the gallery: the laser fires across the hall into the white strip
// high in the east wall. A portal there and one in the low west panel:
// the beam runs into the glass case, the cube in it turns it north,
// through relay 2. The store room opens.
shoot blue 29.0 4.6 10.5
shoot orange 1.0 1.5 6.5""", """// CHEESE: skip the glass case. The west panel at z 11 is square to
// relay 2: the beam out of it lights the relay straight, and the store
// room opens. The rest as meant.
shoot blue 29.0 4.6 10.5
shoot orange 1.0 1.5 11.5""")
assert CHEESE_DIRECT != SOLUTION


def build(variant):
    g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]

    def box(x0, x1, y0, y1, z0, z1, ch):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                for x in range(x0, x1 + 1):
                    g[y][z][x] = ch

    # The hall, x 1..E-1, z 1..12, air layers 1..6; over the gallery, z
    # 13..16, air from layer 3 (the gallery floor is two metres up).
    box(1, E - 1, 1, 6, 1, 12, '.')
    box(1, E - 1, 3, 6, 13, 16, '.')

    # The laser, high in the west wall, over the hall to the strip high in
    # the east wall.
    g[4][10][0] = 'L'
    box(E, E, 4, 5, 10, 10, 'W')

    # Low panels, two cells from the floor: Pm (west, z 6), and two more.
    box(0, 0, 1, 2, 6, 6, 'W')
    box(12, 12, 1, 2, 0, 0, 'W')
    # The first draft had one more, west at z 11: square to relay 2, it
    # opened the store room with no need of the case (cheese-direct.txt).
    if variant == 'flawed':
        box(0, 0, 1, 2, 11, 11, 'W')
    # Gallery panels, at its ends: P1 west, P3 east.
    box(0, 0, 3, 4, 15, 15, 'W')
    box(E, E, 3, 4, 15, 15, 'W')

    # The glass case, with a reflection cube in it facing north.
    box(7, 9, 1, 2, 5, 7, 'G')
    g[1][6][8] = 'R'
    # Relay 2, north of it; relay 1, on the gallery in front of P1.
    g[1][11][8] = '|'
    g[2][11][8] = '2'
    g[3][15][3] = '|'
    g[4][15][3] = '1'

    # The store room, south-east: a low box, door b in its west wall.
    box(E - 4, E - 1, 1, 3, 1, 4, '#')
    box(E - 3, E - 1, 1, 2, 1, 3, '.')
    box(E - 4, E - 4, 1, 2, 2, 3, 'b')
    g[1][2][E - 2] = 'R'

    # The booth: the catcher behind glass three cells wide, north of the
    # gallery; metal all round it, no panel in its lines.
    box(5, 7, 3, 4, 17, 17, 'G')
    box(5, 7, 3, 4, 18, 20, '.')
    g[3][19][6] = 'O'
    g[4][19][6] = '1'

    # The way out: door a in the north wall at gallery level, the exit
    # room behind it, glass either side of the door so the exit shows.
    box(E - 8, E - 5, 3, 5, 18, 21, '.')
    box(E - 8, E - 5, 3, 4, 17, 17, 'G')
    box(E - 7, E - 6, 3, 4, 17, 17, 'a')
    box(E - 7, E - 6, 2, 2, 19, 21, 'E')

    # You start on the gallery, by the way out, over the hall.
    g[3][14][E - 4] = 'S'
    return g


def text(g, solution):
    head = "name: %s\nhint: %s\nstory: %s\nsize: %d %d %d\nfacing: %s\n" % (NAME, HINT, STORY, W, H, D, FACING)
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
            f.write(text(build(variant), SOLUTION))
    with open(os.path.join(here, 'cheese-direct.txt'), 'w') as f:
        f.write(text(build('flawed'), CHEESE_DIRECT))
    with open(os.path.join(here, '..', '..', 'dlc', 'after-hours', 'reflections.txt'), 'w') as f:
        f.write(text(build('fixed'), SOLUTION))


if __name__ == '__main__':
    main()
