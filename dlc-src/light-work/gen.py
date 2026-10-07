#!/usr/bin/env python3
# Builds the "Light work" chamber: writes <id>.txt (no solution) and
# <id>-SPOILER-solution.txt (with), and a copy with the solution into
# chamber/ for host_tas, from a cell map set up in code.
import sys, os

W, H, D = 16, 7, 18
cells = {}  # (x, y, z) -> char; default '#'


def put(x, y, z, ch):
    cells[(x, y, z)] = ch


def box(x0, x1, y0, y1, z0, z1, ch):
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                put(x, y, z, ch)


# Laid out with the hall floor at layer 0; everything goes up a layer at
# the end, so the goo can lie a metre under the bridge.

# --- The hall: x 1..9, z 1..16, floor layer 0, air layers 1..4 ------------
box(1, 9, 1, 4, 1, 16, '.')
# The north end: a landing along the west wall, the goo pit, and the exit
# beyond it, walled off from the hall at z = 10.
box(3, 9, 1, 4, 10, 10, '#')
box(8, 9, 0, 0, 12, 15, 'E')
# The bridge: out of the west wall, east over the goo; door 'a' in front.
put(0, 1, 13, 'H')
put(2, 1, 13, 'a')
put(2, 2, 13, 'a')
# The relays on their pillars: 2 turns the funnel round, 1 is half the door.
for x, d in ((3, '2'), (7, '1')):
    put(x, 1, 5, '#')
    put(x, 2, 5, '#')
    put(x, 3, 5, '|')
    put(x, 4, 5, d)
# The laser, high on the west wall, onto a white patch on the barrier.
put(0, 4, 2, 'L')
put(10, 3, 2, 'W')
put(10, 4, 2, 'W')
# White patches: one on the west wall, one on the south wall.
put(0, 2, 5, 'W')
put(0, 3, 5, 'W')
put(7, 2, 0, 'W')
put(7, 3, 0, 'W')
# The funnel, out of the west wall, east through the hall into the lens room.
put(0, 2, 8, '%')

# --- The barrier, x = 10: glass over a metal sill, a laser-field mouth ----
for z in range(3, 15):
    for y in (2, 3, 4):
        put(10, y, z, 'G')
put(10, 2, 8, '*')
put(10, 3, 8, '*')
put(10, 2, 11, '#')  # nothing straight onto the catcher from the hall

# --- The lens room: x 11..14, z 2..14, floor layer 1, air layers 2..4 -----
box(11, 14, 2, 4, 2, 14, '.')
put(14, 2, 1, 'L')   # its own laser, north along the east side
put(12, 2, 8, 'R')   # the reflection cube, in the funnel from the start
put(11, 2, 11, 'O')  # the catcher, and its button
put(11, 3, 11, '1')

# The start.
put(2, 1, 2, 'S')

# Everything up a layer; the goo pit, a metre deep, under the bridge.
cells = {(x, y + 1, z): ch for (x, y, z), ch in cells.items()}
box(3, 7, 0, 0, 11, 16, '~')
box(3, 7, 1, 1, 11, 16, '.')

name = "Light work"
hint = "The cube is in the right place. It is facing the wrong way."
story = ("This chamber runs on light, [Subject-Name-here]. You are not light. "
         "Stay out of its way, and out of that room. The red sheet is very firm on that point.")
assert len(name) <= 31 and len(hint) <= 79 and len(story) <= 159, (len(name), len(hint), len(story))


def text(solution):
    out = [f"name: {name}", f"hint: {hint}", f"story: {story}", f"size: {W} {H} {D}", "facing: 60",
           "funnel: 2", ""]
    for y in range(H):
        out.append(f"layer {y}")
        for z in reversed(range(D)):
            out.append("".join(cells.get((x, y, z), '#') for x in range(W)))
        out.append("")
    if solution:
        out.append(solution.strip())
        out.append("")
    return "\n".join(out)


here = os.path.dirname(os.path.abspath(__file__))
sol = open(os.path.join(here, "solution.txt")).read()
cid = sys.argv[1] if len(sys.argv) > 1 else "light-work"
top = os.path.join(here, "..")
os.makedirs(os.path.join(top, "chamber"), exist_ok=True)
open(os.path.join(top, "chamber", cid + ".txt"), "w").write(text(sol))
open(os.path.join(top, cid + ".txt"), "w").write(text(None))
open(os.path.join(top, cid + "-SPOILER-solution.txt"), "w").write(text(sol))
print("story", len(story), "hint", len(hint))
