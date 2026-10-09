#!/usr/bin/env python3
# "Reflow": the hot-air station (a laser) sits up on the shelf over the
# desk and blows along it into a white patch on the shelf's end wall. The
# badge on the reflow plate (the laser catcher) is down at desk level. A
# portal pair brings the hot air down to the floor; it comes out of the
# west wall and runs east along the desk, past the plate. A reflection cube
# turns it north onto the plate, and the plate opens the way to Hackfest.
# Prints the chamber on stdout.

W, H, D = 17, 7, 12
cells = {}  # (x, y, z) -> char; default '#'


def put(x, y, z, ch):
    cells[(x, y, z)] = ch


def box(x0, x1, y0, y1, z0, z1, ch):
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                put(x, y, z, ch)


# The room: x 1..14, z 1..10, floor layer 0, air layers 1..5.
box(1, 14, 1, 5, 1, 10, '.')
# The shelf over the back of the desk: solid up to layer 3, its top at y 4.
box(1, 14, 1, 3, 9, 10, '#')
# The hot-air station on the shelf, in the west wall, blowing east along it,
# into the white patch on the east wall at the shelf's end.
put(0, 4, 9, 'L')
put(15, 4, 9, 'W')
put(15, 5, 9, 'W')
# The other white patch, low on the west wall, facing east.
put(0, 1, 3, 'W')
put(0, 2, 3, 'W')
# The reflow plate with the badge on it: the catcher, north of the beam's
# line along z = 3.5, so the beam passes it by.
put(11, 1, 7, 'O')
put(11, 2, 7, '1')
# The reflection cube, by the desk.
put(4, 1, 6, 'R')
# The way out: door a in the east wall, the exit beyond.
put(15, 1, 2, 'a')
put(15, 2, 2, 'a')
put(16, 1, 2, '.')
put(16, 2, 2, '.')
put(16, 0, 2, 'E')
# The start.
put(7, 1, 2, 'S')

name = "Reflow the badge"
hint = "The hot air is up on the shelf. The badge is down on the plate."
story = ("Before Hackfest I need a reflow, [Subject-Name-here]. The hot-air "
         "station is on the shelf, as always. Bring it down to me. "
         "Gently. 250 degrees is plenty.")
assert len(name) <= 31 and len(hint) <= 79 and len(story) <= 159, \
    (len(name), len(hint), len(story))

solution = """
// Fetch the reflection cube.
walk_to 4.5 5.0
grab
// Put it down in the line of the beam (z 3.5), facing north, to the plate.
walk_to 11.5 2.55
look 11.5 1.3 4.0
wait 0.4
use
wait 0.5
// Out of the beam's way, east of the cube; then the portals.
walk_to 13.5 2.0
shoot blue 14.99 5.0 9.5
shoot orange 0.99 2.0 3.5
wait 1.0
walk_to 16.5 2.5
"""


def text(sol):
    out = [f"name: {name}", f"hint: {hint}", f"story: {story}",
           f"size: {W} {H} {D}", "facing: north", ""]
    for y in range(H):
        out.append(f"layer {y}")
        for z in reversed(range(D)):
            out.append("".join(cells.get((x, y, z), '#') for x in range(W)))
        out.append("")
    if sol:
        out.append("solution")
        out.append(sol.strip())
        out.append("")
    return "\n".join(out)


print(text(solution), end="")
