#!/usr/bin/env python3
# "Daisy chain": a geeky chore at Hackfest itself. The stage has no power.
# The laser is the wall socket, the two relays are the power strips: two
# reflection cubes daisy-chain the beam through both strips into the
# stage's inlet (a catcher). With all three lit, the backstage door opens
# and the light bridge runs out over the hall, three metres up, to the
# south wall. A portal pair (stage front, low; south wall, high, where the
# bridge ends) lifts the player onto the bridge's far end; walk it back to
# the stage and the exit.
# Prints the chamber to stdout.

W, H, D = 18, 8, 18
cells = {}  # (x, y, z) -> char; default '#'


def put(x, y, z, ch):
    cells[(x, y, z)] = ch


def box(x0, x1, y0, y1, z0, z1, ch):
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                put(x, y, z, ch)


# The hall floor: x 1..16, z 1..11, floor layer 0, air layers 1..6.
box(1, 16, 1, 6, 1, 11, '.')
# The stage: x 1..16, z 12..14, solid layers 0..3, its floor at y = 4.
box(1, 16, 4, 6, 12, 14, '.')
# Backstage: door a (x 6..8, y 4..5, z 15), an alcove behind it, and the
# light bridge emitter in the alcove's back wall, opening south. With the
# door open the bridge runs over the stage and the hall, at y = 4, to the
# south wall.
box(6, 8, 4, 5, 15, 15, 'a')
box(6, 8, 4, 5, 16, 16, '.')
put(7, 4, 17, 'H')
# The exit, on the stage, east end.
box(14, 15, 3, 3, 13, 14, 'E')

# The wall socket: a laser in the stage front, firing south along x = 12,
# right across the hall: it fences the start (east) off from the rest.
put(12, 1, 12, 'L')
# Power strip 1 (relay) on z = 8; power strip 2 (relay) and the stage's
# inlet (catcher, at the foot of the stage) on x = 4. Cube 1 at (12, 8)
# turns the beam west through strip 1; cube 2 at (4, 8) turns it north
# through strip 2 into the inlet. All three lit: door a opens.
put(8, 1, 8, '|')
put(8, 2, 8, '1')
put(4, 1, 10, '|')
put(4, 2, 10, '1')
put(4, 1, 11, 'O')
put(4, 2, 11, '1')

# Two reflection cubes: the adaptors, in the east corner by the start.
put(15, 1, 4, 'R')
put(15, 1, 6, 'R')

# White panels: the stage front, low (walk in), west of the inlet, and the
# south wall, high, where the bridge ends (come out on it).
for y in (1, 2):
    put(2, y, 12, 'W')
for y in (4, 5):
    put(7, y, 0, 'W')

put(14, 1, 2, 'S')

name = "Daisy chain"
hint = "Daisy-chain the beam through both power strips to the stage. Meet the bridge."
story = ("11:15, doors at noon, and the stage is dark, [Subject-Name-here]. One socket, "
         "two power strips. Daisy-chain them. The fire marshal is at lunch. Beep.")
done = "Stage powered. The PA hums, the LEDs blink, and nobody saw the daisy chain. Doors open in half an hour."
assert len(name) <= 31 and len(hint) <= 79 and len(story) <= 159 and len(done) <= 159, \
    (len(name), len(hint), len(story), len(done))

solution = """
// Cube 1: onto the socket's line at z = 8, facing west: through power
// strip 1. The beam no longer reaches the south of the hall.
walk_to 14.5 5.5
grab
walk_to 13.8 8.5
look 10 1.3 8.5
wait 0.5
use
wait 0.3
// Cube 2: round the south of the hall to x = 4, z = 8, facing north:
// through power strip 2 into the stage inlet.
walk_to 14.5 5.5
grab
walk_to 10 6
walk_to 4.5 7.2
look 4.5 1.3 10
wait 0.5
use
wait 1.5
// Portals: the stage front, low, and the south wall, high, where the
// bridge now ends.
shoot blue 2.5 1.8 12
shoot orange 7.5 4.8 1.0
walk_to 2.5 10
walk_to 2.5 13
walk_to 7.5 4
walk_to 7.5 13
walk_to 14.5 13.5
"""


def text(sol):
    out = [f"name: {name}", f"hint: {hint}", f"story: {story}", f"done: {done}",
           f"size: {W} {H} {D}", "facing: north", ""]
    for y in range(H):
        out.append(f"layer {y}")
        for z in reversed(range(D)):
            out.append("".join(cells.get((x, y, z), '#') for x in range(W)))
        out.append("")
    out.append("solution")
    out.append(sol.strip())
    return "\n".join(out) + "\n"


print(text(solution), end="")
