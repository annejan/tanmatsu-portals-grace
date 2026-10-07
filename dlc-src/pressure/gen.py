#!/usr/bin/env python3
# Generates the "Pressure" chamber: pressure.txt (for the SD card) and
# work/pressure.txt (the same, with the solution, for host_tas).
import os, sys

W, H, D = 16, 8, 21
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


def put(x, y, z, ch):
    g[y][z][x] = ch


# --- The press: a tunnel along the south wall (z1). A long crusher fills
# it; the dropper's cube sits on a white shelf at its far (west) end.
box(1, 8, 2, 4, 1, 1, '.')            # tunnel air, layers 2..4
box(1, 2, 2, 2, 1, 1, 'W')            # the shelf, 1 m high, white on top
box(2, 7, 4, 4, 1, 1, 'Y')            # the crusher: one box, x2..7
put(1, 5, 1, 'V')                     # the dropper, over the shelf
box(8, 8, 2, 3, 2, 2, '.')            # the tunnel's mouth, into the hall

# --- The hall: x1..13, z3..8, layers 2..6.
box(1, 13, 2, 6, 3, 8, '.')
box(0, 0, 4, 5, 4, 5, 'W')            # high on the west wall: 2 x 2 white
put(14, 4, 4, 'P')                    # pellet launcher, firing west
put(14, 4, 5, 'c')                    # the shutter in front of ...
put(15, 4, 5, 'Q')                    # ... the receiver
put(15, 5, 5, '2')                    # its button: opens door b
put(6, 2, 6, '3')                     # floor button: opens the shutter
put(3, 2, 8, 'I')                     # the pedestal by the dock
put(3, 3, 8, '1')
put(10, 2, 4, 'S')

# glass between the hall and the pit, with the dock's gap at x1..2
box(1, 13, 2, 6, 9, 9, 'G')
box(1, 2, 2, 3, 9, 9, '.')

# --- East: a nook, door b, the corridor with its fizzler, onto the ledge.
box(14, 14, 2, 3, 8, 8, '.')
box(14, 14, 2, 3, 9, 9, 'b')
box(14, 14, 2, 3, 10, 13, '.')
box(14, 14, 2, 3, 11, 11, 'F')
box(13, 13, 2, 3, 13, 13, '.')

# --- The pit: goo, x1..12, z10..16; the ledge x10..12, z13..16.
box(1, 12, 0, 0, 10, 16, '~')
box(1, 12, 1, 6, 10, 16, '.')
box(10, 12, 0, 1, 13, 16, '#')
box(1, 2, 1, 1, 10, 11, 'M')          # the ferry, 2 x 2
put(1, 1, 15, 'N')                    # ... runs north to z15..16
put(10, 2, 16, 't')                   # the turret
put(10, 1, 13, 'K')                   # its button, on the edge over the pit
put(10, 2, 13, '1')

# --- North: door a, and the exit behind it.
box(1, 2, 2, 3, 17, 17, 'a')
box(1, 2, 2, 3, 18, 19, '.')
box(1, 2, 1, 1, 18, 19, 'E')

HEAD = """name: Pressure
hint: The ferry sails when every button is down, and not a second longer.
story: This test measures performance under pressure. We have bolted the pressure to the ceiling. The turret has volunteered to help, [Subject-Name-here]. Let it.
size: %d %d %d
facing: north
timer: 5.5
platform: 1
""" % (W, H, D)


def chamber():
    out = HEAD
    for y in range(H):
        out += "\nlayer %d\n" % y + "\n".join("".join(g[y][z]) for z in reversed(range(D))) + "\n"
    return out


here = os.path.dirname(os.path.abspath(__file__))
sol = open(os.path.join(here, "solution.txt")).read()
open(os.path.join(here, "pressure.txt"), "w").write(chamber())
os.makedirs(os.path.join(here, "work"), exist_ok=True)
open(os.path.join(here, "work", "pressure.txt"), "w").write(chamber() + "\nsolution\n" + sol)
