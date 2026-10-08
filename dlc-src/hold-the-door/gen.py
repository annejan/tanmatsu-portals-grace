#!/usr/bin/env python3
"""Human in the loop, round one: the cheese tutorial. A cube, a button, a
door -- and two white panels GLaDOS left where they should not be, one in
each room, the second high on the far wall, in sight over the barrier.
The fix makes that one metal.

    python3 dlc-src/hold-the-door/gen.py > dlc/human-in-the-loop/hold-the-door.txt
"""

W, H, D = 9, 6, 12
grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


box(0, W - 1, 0, 0, 0, D - 1, "#")                 # the floor
for y in range(1, H - 1):                          # the walls
    for x in range(W):
        put(x, y, 0, "#")
        put(x, y, D - 1, "#")
    for z in range(D):
        put(0, y, z, "#")
        put(W - 1, y, z, "#")
box(0, W - 1, H - 1, H - 1, 0, D - 1, "#")         # the ceiling
box(0, W - 1, 1, 2, 6, 6, "#")                     # the barrier, two high
box(4, 4, 1, 2, 6, 6, "a")                         # its door
box(3, 5, 1, 2, 0, 0, "W")                         # a white panel by the start ...
box(3, 5, 3, 4, D - 1, D - 1, "W")                 # ... and one high on the far wall
put(4, 1, 2, "S")
put(1, 1, 4, "C")
put(7, 1, 1, "1")
put(4, 0, 9, "E")

out = ["name: Hold the door",
       "hint: A draft: find a way she did not mean. The final draft: the way she did.",
       "story: A cube. A button. A door. I have tested this design on myself four thousand times. It is flawless. Prove it, or do not.",
       "review: flawed",
       "intended: button 1 by cube, door a",
       "flaw: a no door a -- Over the barrier by portal: a white panel on each side of it was all it took.",
       "fix: a 3-5 3-4 %d WG" % (D - 1),
       "size: %d %d %d" % (W, H, D),
       "facing: north"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    if all(c == "#" for r in rows for c in r):
        continue
    out += ["", "layer %d" % y] + rows
out += ["",
        "solution",
        "// The cube onto the button, and through the door while it holds.",
        "look 1.5 1.3 4.5",
        "walk_to 2.4 4.3",
        "grab",
        "wait 0.4",
        "walk_to 6.4 2.2",
        "look 7.5 1.0 1.5",
        "wait 0.5",
        "use",
        "wait 1.0",
        "walk_to 4.5 5.0",
        "walk_to 4.5 9.5",
        "",
        "cheese a",
        "// Blue by the start, orange high on the far wall: out of it, and",
        "// down, the far side of the barrier.",
        "shoot blue 4.5 2.0 1.0",
        "look 4.5 4.0 %d.0" % (D - 1),
        "wait 0.4",
        "shoot orange 4.5 4.0 %d.0" % (D - 1),
        "look 4.5 1.6 1.0",
        "wait 0.4",
        "walk_to 4.5 0.0",
        "wait 1.0",
        "walk_to 4.5 9.5",
        ""]
print("\n".join(out))
