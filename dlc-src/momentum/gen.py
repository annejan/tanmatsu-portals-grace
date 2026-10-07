#!/usr/bin/env python3
# Generates the Momentum DLC chamber. g[y][z][x]; row 0 of a layer is z = D-1.
#   python3 gen.py SOLUTION.txt  -> momentum.txt (no solution), chk/momentum.txt (with it)
import sys, os

W, H, D = 12, 12, 19
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# Balcony: x1..10, z1..7, floor top y=5 (layers 0..4 solid), air up to layer 10.
box(1, 10, 5, 10, 1, 7, '.')
# Parapet along z=8, one metre high, the whole width; air above it.
box(1, 10, 6, 10, 8, 8, '.')
# The lane: x=1, walled off at x=2 (one metre high) for z=3..7.
box(2, 2, 5, 5, 3, 7, '#')
# White strip in the roof over the lane.
box(1, 1, 11, 11, 1, 7, 'W')
# High panel on the south wall: x5..7, layers 9..10.
box(5, 7, 9, 10, 0, 0, 'W')
# Orange gel drips from the roof onto a white patch.
g[11][6][7] = 'Z'
box(7, 7, 4, 4, 5, 6, 'W')
# Cube dropper in the roof.
g[11][1][4] = 'V'

# The shaft: x=10, z1..2, air layers 5..9, white floor (layer 4) and ceiling
# (layer 10); door a across its mouth (x=9). Its north wall at z=3.
box(9, 10, 5, 10, 1, 3, '#')
box(10, 10, 5, 9, 1, 2, '.')
box(10, 10, 4, 4, 1, 2, 'W')
box(10, 10, 10, 10, 1, 2, 'W')
box(9, 9, 5, 9, 1, 2, 'a')

# Pit: z9..16, goo floor, air layers 1..10.
box(1, 10, 1, 10, 9, 16, '.')
box(1, 10, 0, 0, 9, 16, '~')

# The cube's ledge: one cell wide, two deep, against the north wall, top y=2;
# a cube button at the back, where a cube landing on it slides to.
box(1, 1, 0, 1, 15, 16, '#')
g[1][16][1] = 'K'
g[2][16][1] = '1'

# The deck: x5..10, z14..16, top y=6 (layers 0..5 solid); the exit on it.
box(5, 10, 0, 5, 14, 16, '#')
box(9, 10, 5, 5, 15, 16, 'E')

g[5][2][5] = 'S'

HEAD = """name: Momentum
hint: Nothing in here gives you speed. Make your own, then pass it on.
story: Today's test: the conservation of momentum. Momentum will be conserved. You, [Subject-Name-here], are not covered by that law.
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


sol = open(sys.argv[1]).read() if len(sys.argv) > 1 else ''
body = HEAD + '\n' + layers()
outdir = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(outdir, 'momentum.txt'), 'w') as f:
    f.write(body)
os.makedirs(os.path.join(outdir, 'chk'), exist_ok=True)
with open(os.path.join(outdir, 'chk', 'momentum.txt'), 'w') as f:
    f.write(body + '\nsolution\n' + sol)
