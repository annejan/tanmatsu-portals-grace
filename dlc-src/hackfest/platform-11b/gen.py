#!/usr/bin/env python3
# "Platform 11b": the intercity to Enschede. A moving platform (the train)
# over a goo trench (the tracks), driven by a cube button (platform: 1).
# The cube (your ticket) sits on a luggage rack two metres up, reached by
# a portal pair. Prints the chamber to stdout.
W, H, D = 16, 7, 10
cells = {}  # (x, y, z) -> char; default '#'


def put(x, y, z, ch):
    cells[(x, y, z)] = ch


def box(x0, x1, y0, y1, z0, z1, ch):
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                put(x, y, z, ch)


# The station: interior x 1..14, z 1..8; floor at layer 1, air 2..5.
box(1, 14, 2, 5, 1, 8, '.')
# The tracks: a goo trench x 5..11, a metre down.
box(5, 11, 0, 0, 1, 8, '~')
box(5, 11, 1, 1, 1, 8, '.')
# The intercity: a 2 x 2 platform at the west dock, running east to x 10..11.
box(5, 6, 1, 1, 3, 4, 'M')
put(10, 1, 3, 'N')
# The far platform: the exit, the way to Hackfest.
box(13, 14, 1, 1, 2, 6, 'E')
# The departure button on the dock edge: only a cube presses it, and the
# train leaves the moment it is pressed: put the ticket down, then hop on.
put(4, 1, 3, 'K')
put(4, 2, 3, '1')
# The luggage rack: a ledge two metres up at the north end of the west
# platform, with the ticket (the cube) on it.
box(1, 4, 2, 3, 6, 8, '#')  # a cell deep more: the cube out of a jump's reach from the floor
put(3, 4, 8, 'C')  # at the back of the rack: out of reach from the floor
# White panels: the rack's back wall, and the west wall by the start.
put(2, 4, 9, 'W')
put(2, 5, 9, 'W')
put(0, 2, 2, 'W')
put(0, 3, 2, 'W')
# The start.
put(2, 2, 3, 'S')

name = "Platform 11b"
hint = "The train runs while the cube is on its button. The cube is on the rack."
story = ("Next stop Hackfest, [Subject-Name-here]! The intercity only runs with a "
         "cube on the button. Your ticket is on the luggage rack. Do not lick the tracks.")
assert len(name) <= 31 and len(hint) <= 79 and len(story) <= 159, (len(name), len(hint), len(story))

solution = """\
// A portal by the start, one on the rack's back wall: step up onto the rack.
shoot blue 1.0 3.0 2.5
shoot orange 2.5 5.0 9.0
walk_to -1 2.5
wait 0.4
grab
wait 0.4
// Down off the rack, to the dock; the ticket goes on the button.
walk_to 3.5 5.5
walk_to 3.2 3.5
wait 0.4
look 4.5 2.3 3.5
wait 0.6
use
// The train leaves at once: it is slower than you. Run and hop on.
wait 0.2
walk_to 3.4 4.4
walk_to 7.6 4.0
wait 3.2
walk_to 13.5 4
"""

out = [f"name: {name}", f"hint: {hint}", f"story: {story}",
       f"size: {W} {H} {D}", "facing: north", "platform: 1", ""]
for y in range(H):
    out.append(f"layer {y}")
    for z in range(D - 1, -1, -1):
        out.append("".join(cells.get((x, y, z), '#') for x in range(W)))
    out.append("")
out.append("solution")
out.append(solution.rstrip("\n"))
print("\n".join(out))
