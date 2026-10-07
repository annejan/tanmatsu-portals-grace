#!/usr/bin/env python3
"""Generator for the MACHINE chamber: "Shop Floor".

A factory hall 112 m long. A gallery in the middle, a distributor tower on it
whose east face carries three white panels. West of the gallery: three
machines (a cube press under a ceiling dropper, a pellet launcher, a laser),
each firing at a white spot. East of the gallery, 30 m of slag, then the
dock with the receiver, the catcher, the cube bin and the final door.

    python3 gen.py            -> writes sol/shop-floor.txt (with solution)
                                  and sd/shop-floor.txt (without)
"""
import os
import sys

W, H, D = 112, 26, 48
HERE = os.path.dirname(os.path.abspath(__file__))

g = {}  # (x, y, z) -> char; missing = '#'


def put(x, y, z, c):
    assert 0 <= x < W and 0 <= y < H and 0 <= z < D, (x, y, z)
    g[(x, y, z)] = c


def box(x0, x1, y0, y1, z0, z1, c):
    """Fill the inclusive box with c."""
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


def get(x, y, z):
    return g.get((x, y, z), '#')


# --- The hall ---------------------------------------------------------------
# Interior air x 1..103, y 1..24, z 1..46; the floor is slag (goo).
box(1, 103, 1, 24, 1, 46, '.')
box(1, 103, 0, 0, 1, 46, '~')

# Pilasters along the long walls and girders under the roof: the hall's
# rhythm, and a sense of its length.
for x in range(8, 104, 16):
    box(x, x, 1, 24, 1, 1, '#')
    box(x, x, 1, 24, 46, 46, '#')
    box(x, x, 24, 24, 2, 45, '#')

# Crane rails along the long walls.
box(1, 103, 17, 17, 1, 1, '#')
box(1, 103, 17, 17, 46, 46, '#')

# --- The gallery: a block 10 m high in the middle ---------------------------
GX0, GX1, GZ0, GZ1, GTOP = 48, 63, 14, 33, 10
box(GX0, GX1, 0, GTOP - 1, GZ0, GZ1, '#')
# The distributor tower, on the gallery, x 53..55, z 22..24.
TX0, TX1 = 53, 55
box(TX0, TX1, GTOP, 22, 22, 24, '#')
# The reject chute: a slot in the deck in front of the tower's east face,
# down to the slag. Nothing stands in front of the panels.
# FIX: wide enough (z20..26, round the tower) that no deck is within a
# cube's reach (2 m) of the low panels: a cube that pops out cannot be caught.
box(TX0 - 1, GX1, 1, GTOP - 1, 20, 26, '.')
box(TX0 - 1, GX1, 0, 0, 20, 26, '~')
box(TX0, TX1, 0, GTOP - 1, 22, 24, '#')   # the tower stands on the slag
# The three panels on the tower's east face.
PEL_Z, CUBE_Z, LAS_Z = 22, 23, 24
box(TX1, TX1, GTOP, GTOP + 1, PEL_Z, PEL_Z, 'W')    # pellet out
box(TX1, TX1, 20, 21, CUBE_Z, CUBE_Z, 'W')          # cube out, high
box(TX1, TX1, 16, 17, CUBE_Z, CUBE_Z, 'W')          # ... and not so high
box(TX1, TX1, GTOP, GTOP + 1, LAS_Z, LAS_Z, 'W')    # laser out
# Start: on the deck's west side, looking at the machines.
put(50, GTOP, 20, 'S')

# --- The press: a ceiling dropper 24 m over a white die, crushers round it ---
box(24, 35, 0, 0, 17, 29, '#')
box(29, 30, 0, 0, 23, 23, 'W')      # the die
for (px, pz) in ((24, 17), (35, 17), (24, 29), (35, 29)):
    box(px, px, 1, 5, pz, pz, '#')
box(24, 35, 6, 6, 17, 29, '#')      # the press head's frame
box(29, 30, 6, 6, 23, 23, '.')      # with the cube's way through it
put(29, H - 1, 23, 'V')             # the dropper, in the hall's roof
box(28, 31, 4, 5, 25, 27, 'Y')      # crushers: north,
box(28, 31, 4, 5, 19, 21, 'Y')      # south,
box(25, 27, 4, 5, 22, 24, 'Y')      # west

# --- The pellet machine (north-west) -----------------------------------------
box(6, 22, 0, 0, 34, 45, '#')
box(16, 18, 1, 2, 39, 41, '#')
put(16, 1, 40, 'P')                 # fires west
box(10, 12, 1, 4, 37, 43, '#')
box(12, 12, 1, 2, 40, 40, 'W')      # where it strikes

# --- The laser (south-west) ----------------------------------------------------
box(6, 22, 0, 0, 2, 13, '#')
box(16, 18, 1, 2, 6, 8, '#')
put(16, 1, 7, 'L')                  # fires west
box(10, 12, 1, 4, 4, 10, '#')
box(12, 12, 1, 2, 7, 7, 'W')        # where it strikes

# Pipes over the slag, from each machine to the gallery: what feeds what.
box(19, 50, 1, 1, 40, 40, '#')
box(50, 50, 1, 1, 34, 39, '#')
box(19, 50, 1, 1, 7, 7, '#')
box(50, 50, 1, 1, 8, 13, '#')
box(36, 47, 1, 1, 23, 23, '#')

# --- The dock (east) -------------------------------------------------------------
DX0, DX1, DZ0, DZ1 = 86, 103, 12, 35
box(DX0, DX1, 0, GTOP - 1, DZ0, DZ1, '#')
put(DX0, GTOP, PEL_Z, 'Q')          # the pellet receiver
put(DX0, GTOP + 1, PEL_Z, '1')
put(DX0, GTOP, LAS_Z, 'O')          # the laser catcher
put(DX0, GTOP + 1, LAS_Z, '1')
BIN_X = int(os.environ.get('BIN_X', '90'))
put(BIN_X, GTOP - 1, CUBE_Z, 'K')   # the cube bin: a cube button ...
put(BIN_X, GTOP, CUBE_Z, '1')
box(BIN_X + 1, BIN_X + 1, GTOP, GTOP + 4, CUBE_Z, CUBE_Z, '#')  # ... against a post
# The end wall, the door, the exit.
box(104, 110, 1, 24, 1, 46, '#')
box(104, 104, GTOP, GTOP + 1, 23, 24, 'a')
box(105, 109, GTOP, GTOP + 2, 21, 26, '.')
box(105, 109, GTOP - 1, GTOP - 1, 21, 26, 'E')

# --- The catapults: deck to dock, and back ---------------------------------------
put(61, GTOP - 1, 16, 'J')
put(90, GTOP, 16, 'T')
put(90, GTOP - 1, 31, 'J')
put(61, GTOP, 31, 'T')

HEADER = """name: Shop Floor
hint: Too far to walk, too far to shoot. Plan it from the gallery.
story: Welcome to the production floor, [Subject-Name-here]. The machines do all the work. They only need someone to point them. You are cheaper than a robot.
size: {W} {H} {D}
facing: west
"""


def render(solution):
    out = [HEADER.format(W=W, H=H, D=D)]
    for y in range(H):
        out.append("layer %d" % y)
        for z in range(D - 1, -1, -1):
            out.append("".join(get(x, y, z) for x in range(W)))
        out.append("")
    if solution:
        out.append("solution")
        out.append(solution.strip())
        out.append("")
    return "\n".join(out)


def main():
    sol_path = os.path.join(HERE, "solution.txt")
    solution = open(sol_path).read() if os.path.exists(sol_path) else ""
    name = "shop-floor.txt"
    with open(os.path.join(HERE, "sol", name), "w") as f:
        f.write(render(solution))
    with open(os.path.join(HERE, "sd", name), "w") as f:
        f.write(render(""))
    print("wrote", name, file=sys.stderr)


if __name__ == "__main__":
    main()
