#!/usr/bin/env python3
"""Generator for the After hours chamber "Lights out".

Two rooms either side of an eight-metre wall, each with a pellet launcher,
a receiver on a pillar and two white panels, all high up; the lift to the
exit runs once both receivers have a pellet. The pellets cross between the
rooms through a tube a metre square, at y 5; the player crosses through a
bent passage with a fizzler in it, which shuts both portals. Each delivery
takes two pairs of portals, one after the other, with a run between:

  launcher 1 -> top of the west strip -> B (west wall) -> the tube ->
      C (east wall) -> bottom of the east strip -> the east receiver;
  launcher 2 -> top of the east strip -> C -> the tube ->
      B -> bottom of the west strip -> the west receiver.

The top and bottom pairs of a strip overlap, so one room's portals cannot
do both ends; nothing in one room can be shot from the other (no portal
shot threads the tube from a floor, and the passage bends twice); no
portal hangs low enough for the player to walk into; and a pellet sent out
of B and bounced home off the far wall dies before it gets back.

    python3 gen.py
        writes flawed.txt (the first draft) and fixed.txt here, and the
        fixed chamber to dlc/after-hours/lights-out.txt, each with its
        solution;
    python3 gen.py VARIANT SOLUTION.txt OUT.txt
        writes VARIANT (`fixed` or `flawed`) with another solution, as
        the cheese scripts were.

x goes east, y up, z north; one cell a metre. Row 0 of a written layer is
z = D-1 (the far, north side), as the game reads it.
"""
import os
import sys

H, D = 8, 14   # W follows from the variant, below

NAME = "Lights out"
HINT = "Two portals will not get it there. The pellet will not wait while you think."
STORY = ("The lights went out at six, [Subject-Name-here]. The lift runs on two energy pellets now, "
         "and on you. Mind the fizzler. Nobody switched that off.")

SOLUTION = {
    'flawed': """// West: launcher 1's pellet into the top of the strip, out of B, high,
// down the tube to the east room.
shoot blue 4.5 5.5 13.0
shoot orange 1.0 5.5 8.5
// Through the passage behind it; the fizzler shuts both portals.
walk_to 9.5 4.5
walk_to 11.5 4.5
walk_to 11.5 6.5
walk_to 18.0 6.5
// Before it reaches the east wall: C there, and the bottom of the east
// strip, square to the receiver on the pillar.
shoot blue 25.0 5.5 8.5
shoot orange 20.5 3.5 1.0
wait 4.0
// Now launcher 2's pellet, the other way: the top of the east strip.
shoot orange 20.5 5.5 1.0
walk_to 17.6 6.5
wait 5.7
// Its next pellet goes in; behind it, west again, before it gets there.
walk_to 11.5 6.5
walk_to 11.5 4.5
walk_to 8.0 4.5
shoot blue 1.0 5.5 8.5
shoot orange 4.5 3.5 13.0
// Onto the lift: it runs once both receivers are lit.
walk_to 1.8 10.8
wait 3.0
walk_to 1.8 12.5""",
    'fixed': """// West: launcher 1's pellet into the top of the strip, out of B, high,
// down the tube to the east room.
shoot blue 5.5 5.5 13.0
shoot orange 1.0 5.5 8.5
// Through the passage behind it; the fizzler shuts both portals.
walk_to 13.5 4.5
walk_to 15.5 4.5
walk_to 15.5 6.5
walk_to 22.0 6.5
// Before it reaches the east wall: C there, and the bottom of the east
// strip, square to the receiver.
shoot blue 33.0 5.5 8.5
shoot orange 24.5 3.5 1.0
wait 4.0
// Launcher 2's pellet, the other way: the top of the east strip.
shoot orange 24.5 5.5 1.0
walk_to 21.6 6.5
wait 5.0
// Its next pellet goes in; behind it, west again, before it gets there.
walk_to 15.5 6.5
walk_to 15.5 4.5
walk_to 12.5 4.5
shoot blue 1.0 5.5 8.5
shoot orange 5.5 3.5 13.0
// Onto the lift: it runs once both receivers have their pellets.
walk_to 11.8 10.8
wait 2.5
walk_to 11.8 12.5""",
}

HERE = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
if args:
    variant, sol_path, out_path = args
    jobs = [(variant, open(sol_path).read().strip() if os.path.exists(sol_path) else '', out_path)]
else:
    jobs = [('flawed', SOLUTION['flawed'], os.path.join(HERE, 'flawed.txt')),
            ('fixed', SOLUTION['fixed'], os.path.join(HERE, 'fixed.txt')),
            ('fixed', SOLUTION['fixed'], os.path.join(HERE, '..', '..', 'dlc', 'after-hours', 'lights-out.txt'))]


def build(variant):
    global W
    g = {}  # (x, y, z) -> char; missing = '#'

    def put(x, y, z, c):
        assert 0 <= x < W and 0 <= y < H and 0 <= z < D, (x, y, z)
        g[(x, y, z)] = c

    def box(x0, x1, y0, y1, z0, z1, c):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                for x in range(x0, x1 + 1):
                    put(x, y, z, c)


    # The first draft had rooms 8 m wide and each receiver by its launcher,
    # 9 m from the strip: a 4 s race. The fix gives the pellet more road
    # (rooms 12 m wide, 32 m from B to C, a 5.3 s race) and puts each receiver
    # 2 m from its strip, so the meant way has time to spare while a pellet
    # bounced home in one room still dies before its receiver.
    RW = 8 if variant == 'flawed' else 12   # each room's width (x)
    TL = 8                                  # the wall between: the tube's length
    W = 1 + RW + TL + RW + 1
    WX0, WX1 = 1, RW                        # the west room's air
    EX0, EX1 = RW + TL + 1, RW + TL + RW    # the east room's air
    WL = 4 if variant == 'flawed' else 5    # the west launcher's x
    EL = EX0 + 3                            # the east launcher's x
    QW = 3 if variant == 'flawed' else 10   # the west receiver's z
    QE = 10 if variant == 'flawed' else 3   # the east receiver's z

    # The two rooms: air y 1..6, z 1..12.
    box(WX0, WX1, 1, 6, 1, 12, '.')
    box(EX0, EX1, 1, 6, 1, 12, '.')

    # The tube: one cell, TL long, at y 5, z 8 -- the pellet's way across.
    # Too narrow and too long to aim a portal through from either floor.
    box(RW + 1, RW + TL, 5, 5, 8, 8, '.')

    # The passage, two high, bent twice so nothing sees through it:
    # east along z 4, a jog north, east along z 6 into the east room.
    box(RW + 1, RW + 3, 1, 2, 4, 4, '.')
    box(RW + 3, RW + 3, 1, 2, 5, 5, '.')
    box(RW + 3, RW + TL, 1, 2, 6, 6, '.')
    # The fizzler across it: crossing shuts both portals.
    box(RW + 5, RW + 5, 1, 2, 6, 6, 'F')

    # Each room: a launcher in one end wall at y 4, firing at a strip of three
    # white cells (y 3..5) in the other; between them, on a pillar, a receiver
    # one cell lower, its button in the launcher's line (a pellet flies over a
    # button). A portal on the strip's top pair takes the room's own pellet in;
    # one on the bottom pair puts a pellet coming the other way onto the
    # receiver. The two pairs overlap, so no room can do both at once.
    #
    # West room: launcher 1 fires north; its receiver takes launcher 2's pellet.
    put(WL, 4, 0, 'P')
    box(WL, WL, 3, 5, 13, 13, 'W')   # the west strip, north wall
    box(WL, WL, 1, 2, QW, QW, '#')   # the pillar
    put(WL, 3, QW, 'Q')
    put(WL, 4, QW, '1')
    box(0, 0, 5, 6, 8, 8, 'W')       # B: west wall, square to the tube

    # East room: launcher 2 fires south; its receiver takes launcher 1's pellet.
    put(EL, 4, 13, 'P')
    box(EL, EL, 3, 5, 0, 0, 'W')     # the east strip, south wall
    box(EL, EL, 1, 2, QE, QE, '#')
    put(EL, 3, QE, 'Q')
    put(EL, 4, QE, '1')
    box(W - 1, W - 1, 5, 6, 8, 8, 'W')   # C: east wall, square to the tube

    # The lift, in the west room: a 2 x 2 platform flush with the floor,
    # rising 3 m to the exit ledge along the north wall. It runs once both
    # receivers have their pellets (`platform: 1`). The draft had it in the
    # far corner; the fix by the passage.
    LX = 1 if variant == 'flawed' else RW - 1
    box(LX, LX + 1, 0, 0, 10, 11, 'M')
    put(LX, 3, 10, 'N')
    box(LX, LX + 1, 1, 3, 12, 12, '#')
    box(LX, LX + 1, 3, 3, 12, 12, 'E')

    put(6, 1, 5, 'S')

    return g


def write(variant, sol, out_path):
    g = build(variant)
    head = """name: Lights out
hint: %s
story: %s
size: %d %d %d
facing: north
platform: 1
""" % (HINT, STORY, W, H, D)
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g.get((x, y, z), '#') for x in range(W)))
        out.append('')
    body = head + '\n' + '\n'.join(out)
    if sol:
        body += '\nsolution\n' + sol + '\n'
    with open(out_path, 'w') as f:
        f.write(body)


assert len(NAME) <= 31 and len(HINT) <= 79 and len(STORY) <= 159, (len(HINT), len(STORY))
for variant, sol, out_path in jobs:
    write(variant, sol, out_path)
