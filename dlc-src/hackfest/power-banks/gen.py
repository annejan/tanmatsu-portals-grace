#!/usr/bin/env python3
# "Power banks": a geeky chore before Hackfest. Two pellet launchers, two
# receivers (the power banks), one door that wants both charged. Each
# launcher fires along its own line and rests once its pellet is caught,
# so the portals must be set up twice: once per launcher.
# Prints the chamber to stdout.

W, H, D = 15, 5, 15
cells = {}  # (x, y, z) -> char; default '#'


def put(x, y, z, ch):
    cells[(x, y, z)] = ch


def box(x0, x1, y0, y1, z0, z1, ch):
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                put(x, y, z, ch)


# The living room: x 1..13, z 1..11, floor layer 0, air layers 1..3.
box(1, 13, 1, 3, 1, 11, '.')

# The front door (a) in the north wall, and the hall with the exit behind it.
box(6, 7, 1, 2, 12, 12, 'a')
box(6, 7, 1, 2, 13, 13, '.')
box(6, 7, 0, 0, 13, 13, 'E')

# Launcher 1, in the east wall, fires west along z = 3 at the west wall.
put(14, 1, 3, 'P')
# Launcher 2, in the north wall, fires south along x = 10 at the south wall.
put(10, 1, 12, 'P')

# The power banks: receivers with button 1 on each; the door wants both.
put(3, 1, 10, 'Q')
put(3, 2, 10, '1')
put(11, 1, 6, 'Q')
put(11, 2, 6, '1')

# The white panels, two cells high each: where the pellets land (west, south)
# and where they could come out facing a power bank (north, east).
for y in (1, 2):
    put(0, y, 3, 'W')    # west wall, end of launcher 1's line
    put(10, y, 0, 'W')   # south wall, end of launcher 2's line
    put(3, y, 12, 'W')   # north wall, facing the bank at x = 3
    put(14, y, 6, 'W')   # east wall, facing the bank at z = 6

# A desk in the north-east corner, out of every pellet's way.
box(12, 13, 1, 1, 9, 10, '#')

# The start, clear of both lines of fire.
put(5, 1, 6, 'S')

name = "Power banks"
hint = "One launcher per power bank. Both banks full, or the front door stays shut."
story = ("Battery at 3%, [Subject-Name-here]. Hackfest has more blinkenlights than "
         "wall sockets. Charge every power bank first. I refuse to die in a tent again.")
assert len(name) <= 31 and len(hint) <= 79 and len(story) <= 159, (len(name), len(hint), len(story))

solution = """
// Launcher 1 (east wall, z = 3) into the west wall, out of the north wall,
// down onto the bank at x = 3.
shoot blue 1.0 1.8 3.5
shoot orange 3.5 2.5 12.0
wait 4.5
// Launcher 2 (north wall, x = 10) into the south wall, out of the east
// wall, onto the bank at z = 6. Orange first, so nothing lands on bank 1.
shoot orange 14.0 2.5 6.5
shoot blue 10.5 1.8 1.0
wait 5.0
walk_to 6.5 11.0
walk_to 6.5 13.6
"""


def text(sol):
    out = [f"name: {name}", f"hint: {hint}", f"story: {story}",
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
