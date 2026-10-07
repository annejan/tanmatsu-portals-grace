#!/usr/bin/env python3
"""Generator for the TOWER chamber "Spire": a hall 98 m long, 30 m tall.

Coordinates as the game's: x east (left to right in a layer), y up (layer
number), z north (the top line of a layer is the highest z).

    west                                                          east
    x 1..12        x 13..65          x 66..85                x 86..98
    terrace y 20   goo; F (white)    tower: open foot,       lobby: start,
    cube, button,  under the         L1 deck (y 11),         J1, laser in a
    J3 -> summit   terrace face      summit (y 25) + exit,   gatehouse (door b),
                                     drop chute x 71, gel    W on the east wall,
                                     dripping down it        B + light tower

The route: blue at the chute's foot (D, white from the gel); plates up to
the summit; orange on F, 58 m off; drop 24 m down the chute, come up 24 m
out of F onto the terrace; cube on button 2 (door b); J3 throws you 60 m
back to the summit; from the top, portals on W and B send the laser up the
glass light tower through three relays to the catcher: door a, exit.

Usage: gen.py OUT_WITH_SOLUTION OUT_WITHOUT_SOLUTION [SOLUTION_FILE]
"""
import sys

W, H, D = 100, 32, 40

g = [[['#'] * W for _ in range(D)] for _ in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    """Fill the inclusive box with glyph ch."""
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


def put(x, y, z, ch):
    g[y][z][x] = ch


Z0 = 10  # the tower and its lines sit this far north of the hall's south wall


def Z(z):
    return z + Z0


# --- The hall: air above a metal floor, under a metal ceiling --------------
box(1, 98, 1, 30, 1, D - 2, '.')

# --- West: the terrace, a block 19 m high, 12 m deep, wall to wall --------
TERRACE_Y = 20  # stand height
box(1, 12, 1, TERRACE_Y - 1, 1, D - 2, '#')
# The cube and its button, north; the faith plate home, south.
put(3, TERRACE_Y - 1, Z(15), 'K')
put(3, TERRACE_Y, Z(15), '2')
put(6, TERRACE_Y, Z(15), 'C')

# --- The goo, and F: a white patch under the terrace's face --------------
box(13, 65, 0, 0, 1, D - 2, '~')
box(13, 15, 0, 0, Z(9), Z(11), 'W')
# A glass canopy high over F: gel that comes up out of F splashes on it and
# paints nothing (glass takes no gel), and dies there -- no white ceiling
# over the terrace, and no blobs going round and round.
box(11, 17, 29, 29, Z(7), Z(13), 'G')

# --- The hall's bones: pilasters along both long walls, every 8 m, and a
# row of pillars standing in the goo on each side, each taller than the
# last towards the tower. Nothing on them, nothing to reach them by.
for px in range(16, 64, 8):
    box(px, px + 1, 1, 30, 1, 1, '#')
    box(px, px + 1, 1, 30, D - 2, D - 2, '#')
    box(px, px + 1, 30, 30, 1, D - 2, '#')  # a rib across the ceiling: an arch
for k, px in enumerate(range(22, 64, 12)):
    top = 4 + 4 * k
    box(px, px + 1, 1, top, 4, 5, '#')
    box(px, px + 1, 1, top, D - 6, D - 5, '#')

# --- Tower ----------------------------------------------------------------
L1_Y = 11      # L1 stand height (floor layer 10)
SUM_Y = 25     # summit stand height (slab layers 22..24)
box(70, 85, L1_Y - 1, L1_Y - 1, Z(1), Z(18), '#')    # L1
box(66, 76, SUM_Y - 3, SUM_Y - 1, Z(1), Z(18), '#')  # the summit
# Its walls: south and north, and west below the summit.
for wz in (Z(0), Z(19)):
    box(66, 76, 1, SUM_Y - 1, wz, wz, '#')
    box(77, 85, 1, L1_Y - 1, wz, wz, '#')
box(66, 66, SUM_Y - 6, SUM_Y - 1, Z(0), Z(19), '#')   # a lintel: the base is open to the hall

# The drop chute: 1 x 2 (x 71, z 10..11) from the summit to the ground.
box(70, 72, 1, SUM_Y - 1, Z(9), Z(12), '#')
box(71, 71, 1, SUM_Y - 1, Z(10), Z(11), '.')
box(71, 71, 1, 2, Z(9), Z(9), '.')      # a doorway at its foot, facing south
put(71, 31, Z(11), 'X')                 # white gel, dripping down the shaft

# Summit: a wall that stops the long throw from the terrace ...
box(71, 71, SUM_Y, SUM_Y + 1, Z(1), Z(5), '#')
# ... and the exit room at the north end, behind door a.
box(66, 76, SUM_Y, 30, Z(13), Z(13), '#')
box(66, 66, SUM_Y, 30, Z(14), Z(18), '#')
box(76, 76, SUM_Y, 30, Z(14), Z(18), '#')
box(70, 71, SUM_Y, SUM_Y + 1, Z(13), Z(13), 'a')
box(66, 76, SUM_Y, 30, Z(19), Z(19), "#")
box(68, 74, SUM_Y - 1, SUM_Y - 1, Z(15), Z(17), 'E')

# The laser: an emitter in a gatehouse in the lobby, door b in front; it
# fires east at W on the east wall.
box(85, 88, 1, 3, Z(15), Z(17), '#')
put(86, 1, Z(16), 'L')
put(87, 1, Z(16), '.')
put(88, 1, Z(16), 'b')
put(88, 2, Z(16), 'b')
put(99, 1, Z(16), 'W')
put(99, 2, Z(16), 'W')
# B: a white strip in the floor; the light tower rises from its west cell,
# glass with relays in it, to the catcher at summit height.
CX, CZ = 94, Z(4)
put(CX, 0, CZ, 'W')
put(CX + 1, 0, CZ, 'W')
box(CX, CX, 3, SUM_Y - 1, CZ, CZ, 'G')
for ry in (7, 13, 19):
    put(CX, ry, CZ, '|')
    put(CX, ry + 1, CZ, '1')
put(CX, SUM_Y, CZ, 'O')
put(CX, SUM_Y + 1, CZ, '1')

# Faith plates (a plate's target is the target with its rank in reading
# order: layer by layer upwards, top line (high z) first, left to right).
put(90, 0, Z(12), 'J')                  # J1: lobby -> L1
put(83, L1_Y - 1, Z(8), 'J')            # J2: L1 -> summit
put(9, TERRACE_Y - 1, Z(3), 'J')        # J3: terrace -> summit
put(84, L1_Y, Z(12), 'T')               # T1 on L1
put(72, SUM_Y, Z(8), 'T')               # T2 on the summit (higher z than T3)
put(68, SUM_Y, Z(3), 'T')               # T3 on the summit

put(97, 1, Z(10), 'S')                  # the start: the whole hall ahead, through the tower's foot

# --- The solution: (op, args...) with z already in hall coordinates -------
SOL = [
    '// One: blue at the foot of the drop shaft, where the white gel lands.',
    ('walk_to', 84, Z(6)),
    ('walk_to', 71.5, Z(5.5)),
    ('shoot blue', 71.5, 1.0, Z(10.75)),
    '// Two: up. The plates throw you, lobby to L1, L1 to the summit.',
    ('walk_to', 92.5, Z(12.5)),
    ('walk_to', 84.5, Z(12.5)),
    ('walk_to', 84.5, Z(8.5)),
    ('walk_to', 72.5, Z(8.5)),
    '// Three: orange on the white patch under the terrace, 58 m away...',
    ('walk_to', 66.6, Z(10.5)),
    ('shoot orange', 14.25, 1.0, Z(10.5)),
    '// ... and down the shaft. Twenty-four metres down is twenty-four up.',
    ('walk_to', 71.5, Z(8.6)),
    ('walk_to', 71.5, Z(11.2)),
    ('walk_to', 8, Z(10)),
    '// Four: the cube on its button. Door b opens, far away.',
    ('walk_to', 6.5, Z(14.4)),
    ('grab',),
    ('walk_to', 3.5, Z(14.2)),
    ('look', 3.5, 21.1, Z(15.4)),
    ('wait', 0.3),
    ('use',),
    ('wait', 0.5),
    '// Five: the long plate, back to the summit.',
    ('walk_to', 6.5, Z(3.5)),
    ('walk_to', 68.5, Z(3.5)),
    '// Six: the laser, from the top: the wall it hits, and the floor under the glass.',
    ('walk_to', 70.5, Z(6.5)),
    ('walk_to', 76.4, Z(7)),
    ('shoot blue', 99.0, 1.6, Z(16.5)),
    ('shoot orange', 94.25, 1.0, Z(4.5)),
    ('wait', 1.5),
    ('walk_to', 70.4, Z(9)),
    ('walk_to', 70.4, Z(15.5)),
]


def solution():
    out = []
    for st in SOL:
        if isinstance(st, str):
            out.append(st)
        else:
            out.append(' '.join([st[0]] + ['%g' % v for v in st[1:]]))
    return '\n'.join(out) + '\n'


HEADER = """name: Spire
hint: A fall keeps its speed through a portal. Only the top is high enough.
story: This is our tallest test, [Subject-Name-here]. Gravity takes care of the way down, free of charge. The way back up is the test.
size: {W} {H} {D}
facing: west
"""


def layers():
    out = []
    for y in range(H):
        rows = [''.join(g[y][z]) for z in range(D - 1, -1, -1)]
        if all(set(r) == {'#'} for r in rows):
            continue  # left out: solid metal
        out.append('layer %d' % y)
        out.extend(rows)
        out.append('')
    return '\n'.join(out)


def main():
    sol = open(sys.argv[3]).read() if len(sys.argv) > 3 else solution()
    body = HEADER.format(W=W, H=H, D=D) + '\n' + layers()
    with open(sys.argv[1], 'w') as f:
        f.write(body + '\nsolution\n' + sol)
    with open(sys.argv[2], 'w') as f:
        f.write(body)


if __name__ == '__main__':
    main()
