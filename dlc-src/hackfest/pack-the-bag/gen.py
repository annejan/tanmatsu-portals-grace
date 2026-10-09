#!/usr/bin/env python3
"""Generator for the Hackfest chore chamber: "Pack the bag".

Home, the morning you leave for Hackfest. The badge will not open the front
door until both backpack compartments (two cube buttons, both '1') hold a
gadget. Two ceiling droppers bring the gadgets, and both land out of reach:

  * the top shelf (west): a block 2 m high, the gadget at its back (x 1.5,
    out of reach of a jump from the floor). Its front face is white, and so
    is the wall above it: a portal pair takes you up there.
  * the junk drawer (east): a pit 2 m deep, 3 m long, with a white floor.
    The gadget rests in the middle of it; a portal under it drops it out
    of the shelf's front face.

The shelf's front face serves both: first it spits the drawer's gadget out,
then it takes you up to the shelf.

    python3 gen.py > pack-the-bag.txt
"""

W, H, D = 14, 10, 14
FLOOR = 3          # the room's floor surface: layers 0..2 are solid
g = {}             # (x, y, z) -> char; missing = '#'


def put(x, y, z, c):
    assert 0 <= x < W and 0 <= y < H and 0 <= z < D, (x, y, z)
    g[(x, y, z)] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


def get(x, y, z):
    return g.get((x, y, z), '#')


# --- The living room: x 1..12, z 1..9, air from the floor to the ceiling ----
box(1, 12, FLOOR, H - 2, 1, 9, '.')

# --- The top shelf (west): solid x 1..3, z 5..9, 2 m high -------------------
box(1, 3, FLOOR, FLOOR + 1, 5, 9, '#')
box(3, 3, FLOOR, FLOOR + 1, 7, 7, 'W')            # its white front face
box(0, 0, FLOOR + 2, FLOOR + 3, 7, 7, 'W')        # the wall above it
put(1, H - 2, 8, 'V')                             # gadget one: back of the shelf

# --- The junk drawer (east): a pit 2 m deep, 3 long, white at the bottom ----
box(10, 10, 1, FLOOR - 1, 6, 8, '.')
box(10, 10, 0, 0, 6, 8, 'W')
put(10, H - 2, 7, 'V')                            # gadget two drops here

# --- The backpack: two cube buttons, one door --------------------------------
for bx in (5, 8):
    put(bx, FLOOR - 1, 3, 'K')
    put(bx, FLOOR, 3, '1')

# --- The front door (north wall z 10) and the exit hall ----------------------
box(6, 7, FLOOR, FLOOR + 1, 10, 10, 'a')
box(5, 8, FLOOR, FLOOR + 1, 11, 12, '.')
box(6, 7, FLOOR - 1, FLOOR - 1, 11, 12, 'E')

put(6, FLOOR, 1, 'S')

HEADER = """name: Pack the bag
hint: Two gadgets, two pockets. One sits up high, one down in the drawer.
story: Hackfest won't pack itself, [Subject-Name-here]. Two gadgets, two pockets. I am not opening the front door for a half-packed bag. Beep.
size: {W} {H} {D}
facing: north
"""

SOLUTION = """
// Gadget two first: a portal under it in the drawer, one on the shelf's face.
wait 0.5
walk_to 10.5 5.3
shoot blue 10.5 1.0 7.3
shoot orange 4.0 4.0 7.5
wait 1.5
walk_to 6.5 6.0
grab
wait 0.4
walk_to 5.5 2.2
look 5.5 3.3 3.5
wait 0.6
use
wait 0.5
// Gadget one: the wall above the shelf, and in through its face.
walk_to 8.5 6.0
shoot blue 1.0 6.2 7.5
walk_to 2.5 8.0
wait 0.5
grab
wait 0.4
walk_to 5.5 8.5
wait 0.5
walk_to 8.5 2.2
look 8.5 3.3 3.5
wait 0.6
use
wait 1.0
// Out of the front door.
walk_to 6.9 8.5
walk_to 6.9 11.5
"""


def main():
    out = [HEADER.format(W=W, H=H, D=D)]
    for y in range(H):
        out.append("layer %d" % y)
        for z in range(D - 1, -1, -1):
            out.append("".join(get(x, y, z) for x in range(W)))
        out.append("")
    out.append("solution")
    out.append(SOLUTION.strip())
    print("\n".join(out))


if __name__ == "__main__":
    main()
