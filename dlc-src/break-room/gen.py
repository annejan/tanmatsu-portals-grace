#!/usr/bin/env python3
"""Human in the loop, the break room: Henry's birthday, at half past four.
Not a draft to cheese: a final round, played straight. `intended: door a`
-- the only way to the exit is through it, so any honest way out counts.

The break room is a test chamber dressed as one. A counter with wall
cabinets and a sink of something brown; a cabinet that drops a cube onto
the counter; a fridge (the only white panel at floor height); two tables
and a bench; the door you came in by, shut for good (a door with no
button). In the middle, the cake: red velvet, with a candle (a laser
emitter, firing up into the ceiling), on a plinth, in a glass case from
the floor to the ceiling. Nobody gets the cake.

At the foot of the far wall a panel is missing: a gap one metre high, two
deep, too low to walk through. Through it, orange: Rattmann's den, a metre
lower, behind the wall. On its back wall, in white panels, LIE. Shoot a
letter through the gap and the fridge from the room, carry the cube in
through the fridge, put it on the button in the den, and the den's side
door opens onto the exit. A pellet lamp in the den's corner (a launcher
over a receiver, caught at once; it is on door a's button too) glows
orange, and a drip of orange gel stains the floor under the letters.

No soft-locks: every tall thing reaches the ceiling, so no cube is left
out of reach; a cube lost in the sink comes back from its cabinet; a cube
left in the gap is in reach from the room. The den is a metre lower so the
gap is at its eye height: from in there the room's south wall shows through
it, so a letter and that wall join the den back to the room, whatever was
done with the portals (proven: in without the cube, both portals moved
inside, out again by the south wall).

    python3 dlc-src/break-room/gen.py > dlc/human-in-the-loop/break-room.txt
"""

W, H, D = 16, 7, 21
grid = [[["#" for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


# --- The break room: x 1-14, z 2-11, four metres high, its floor a metre
# above the den's. Its cells are written a layer low: rput and rbox lift
# them by the one.
def rput(x, y, z, c):
    put(x, y + 1, z, c)


def rbox(x0, x1, y0, y1, z0, z1, c):
    box(x0, x1, y0 + 1, y1 + 1, z0, z1, c)


rbox(1, 14, 1, 4, 2, 11, ".")
# Its walls: a metal skirting, white panels above it.
for y in range(2, 5):
    for z in range(1, 13):
        rput(0, y, z, "W")
        rput(15, y, z, "W")
    for x in range(0, 16):
        rput(x, y, 1, "W")
        rput(x, y, 12, "W")
# The door you came in by: it has no button, so it stays shut.
rbox(7, 8, 1, 2, 1, 1, "b")
# The counter along the west wall, wall cabinets over it up to the ceiling,
# and in them the cabinet the cube comes out of, onto the counter.
rbox(1, 1, 1, 1, 5, 10, "#")
rbox(1, 1, 3, 4, 5, 10, "#")
rput(1, 3, 7, "V")
rput(1, 1, 9, "~")          # the sink, or the coffee: either way, not for cubes
# The fridge, in the north-east corner: white doors, metal up to the ceiling.
rbox(14, 14, 1, 2, 10, 11, "W")
rbox(14, 14, 3, 4, 10, 11, "#")
# Two tables (formica, blue-grey) and a bench (olive) by the door.
rbox(4, 5, 1, 1, 5, 6, "K")
rbox(4, 5, 1, 1, 9, 9, "K")
rbox(10, 12, 1, 1, 2, 2, "Q")
# The cake: on a plinth, in a glass case from the floor to the ceiling.
# Red velvet, with a candle: the cake is a laser emitter, firing up into
# the ceiling, all of it inside the glass.
rbox(10, 12, 1, 1, 6, 8, "#")
rbox(10, 12, 2, 4, 6, 8, "G")
rput(11, 2, 7, "L")
rbox(11, 11, 3, 4, 7, 7, ".")
# The loose panel: a gap a metre high at the foot of the north wall, two
# cells deep -- the wall is white on the room's side, metal on the den's.
# The den's floor is a metre lower: from the room you look down through
# the gap at the den's back wall; from the den, at eye height, across the
# room to its south wall.
rbox(7, 8, 1, 1, 12, 13, ".")
rbox(7, 8, 2, 2, 12, 12, "#")   # its frame, metal

# --- The den, behind the north wall: x 3-11, z 14-19, five metres. -------
N = 14  # its first row
box(3, 11, 1, 5, N, N + 5, ".")
# LIE, in white panels on its back wall, read from inside.
LIE = ["L..I.EE",
       "L..I.E.",
       "L..I.EE",
       "L..I.E.",
       "LL.I.EE"]
for row, line in enumerate(LIE):
    for i, ch in enumerate(line):
        if ch != ".":
            put(5 + i, 5 - row, N + 6, "W")
# The lamp in the north-west corner: a launcher over a receiver, its pellet
# caught at once; the receiver's lenses glow orange from then on. It is on
# door a's button too, so the door wants it -- and it is down at once.
box(3, 4, 3, 5, N + 4, N + 5, "#")
put(3, 3, N + 5, "P")
put(3, 1, N + 5, "Q")
put(3, 2, N + 5, "1")
# Orange gel, dripping from a pipe in the ceiling onto the floor, in sight
# through the gap.
put(8, 6, N + 4, "Z")
# A mattress, along the south wall.
box(10, 11, 1, 1, N, N, "Q")
# The button, and the side door onto the exit.
put(10, 0, N + 3, "K")
put(10, 1, N + 3, "1")
box(12, 12, 1, 2, N + 2, N + 3, "a")
box(13, 14, 1, 2, N + 2, N + 3, ".")
box(14, 14, 0, 0, N + 2, N + 3, "E")

rput(8, 1, 3, "S")

out = ["name: Break room",
       "hint: A panel is missing low in the far wall. Somebody lives behind it. Bring a cube.",
       "story: The cake is being prepared. Please wait in the break room, [Subject-Name-here]. "
       "It is a normal break room. Do not test it.",
       "review: final",
       "intended: door a",
       "done: Approved. The cake has been postponed. Happy birthday, Henry.",
       "size: %d %d %d" % (W, H, D),
       "facing: north"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    if all(c == "#" for r in rows for c in r):
        continue
    out.append("")
    out.append("layer %d" % y)
    out += rows
out += ["",
        "solution",
        "// Through the gap at the foot of the north wall: the I of LIE, on",
        "// the den's back wall. Then the fridge.",
        "look 8.5 1.2 20.0",
        "wait 0.5",
        "shoot blue 8.5 1.2 20.0",
        "// The cube, off the counter, under the cabinet it came out of.",
        "walk_to 3.0 7.5",
        "grab",
        "wait 0.4",
        "// The fridge; into it, and out of the I, in the den.",
        "walk_to 12.5 10.5",
        "shoot orange 14.0 2.6 10.5",
        "look 14.0 2.5 10.5",
        "wait 0.4",
        "walk_to 15.0 10.5",
        "wait 0.5",
        "// The cube on the button; out by the side door.",
        "walk_to 10.5 16.4",
        "look 10.5 1.0 17.5",
        "wait 0.5",
        "use",
        "wait 1.2",
        "walk_to 13.5 16.8",
        "walk_to 14.6 16.8",
        ""]
print("\n".join(out))
