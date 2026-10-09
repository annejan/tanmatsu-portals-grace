#!/usr/bin/env python3
"""Hackfest chores: "Loading the van".

A street in front of a hackerspace. A box van stands on it, its back doors
open. It is packed: crates to the roof by the cab, with one gap left on top
of them, 3 m above the street -- the cube button. The gear (a cube) comes
down a chute into the hackerspace's attic, 3 m up the west wall. A white
panel on the street wall and one up in the attic fetch it; a faith plate
behind the van throws it in, over the gap and down into it. The button
opens the gate to the road.

No gear can get stuck in the van out of reach. The gap is the only top in
it: the crates either side of the gap reach the roof, and behind them the
van has no floor but the street. A gear that falls short (thrown wrong, let
go of too early on the plate) drops back to the street, to be picked up
again. A fizzler hangs in front of the gap, at its height: a gear that
comes in low -- held up to it or jumped up with from the street below, or
left on the lip of the gap -- is fizzled, and the chute drops a new one in
the attic. Only a gear coming down from above lands in the gap.

    python3 gen.py > loading-the-van.txt
"""

W, H, D = 18, 10, 20
grid = [[["." for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    assert 0 <= x < W and 0 <= y < H and 0 <= z < D, (x, y, z)
    grid[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


# The street: air x 1..14, z 1..18, layers 1..9, walls round it.
box(0, W - 1, 0, 0, 0, D - 1, "#")                 # the street
for y in range(1, H):
    for x in range(W):
        put(x, y, 0, "#")
        put(x, y, D - 1, "#")
    for z in range(D):
        put(0, y, z, "#")
        put(15, y, z, "#")
        put(16, y, z, "#")
        put(17, y, z, "#")

# The gate to the road, east, and the road out: the exit.
box(15, 15, 1, 2, 2, 3, "a")
box(16, 16, 1, 2, 2, 3, ".")
box(16, 16, 0, 0, 2, 3, "E")

# The van: x 6..10, z 12..17, open at the back (south, z 12); the cab north
# of it. Sides and bulkhead stand on the street; the roof is in the top
# layer, so nothing rests on it, and is high enough for a gear thrown to
# the target over the gap. Inside, x 7..9 z 12..14 has no floor but the
# street: a held gear can be lifted 3 m up from there with a jump, so
# nothing there may hold one up out of reach.
box(6, 6, 1, 8, 12, 17, "#")                       # side walls
box(10, 10, 1, 8, 12, 17, "#")
box(6, 10, 1, 8, 17, 17, "#")                      # the bulkhead to the cab
box(6, 10, 9, 9, 12, 17, "#")                      # the roof
box(6, 10, 1, 9, 18, 18, "#")                      # the cab, up to the roof: no ledge on it to strand the gear
# The crates, z 15..16: 3 m high under the gap, to the roof either side of
# it and behind it, so the only top in the van to land on is the gap: the
# button. (The gap at z 15 rather than by the bulkhead: a gear let go of
# riding the plate still drops into it a little later in the flight.)
box(7, 9, 1, 2, 15, 16, "#")
box(7, 9, 3, 8, 16, 16, "#")
box(7, 7, 3, 8, 15, 15, "#")
box(9, 9, 3, 8, 15, 15, "#")
put(8, 3, 15, "K")                                 # the cargo button, in the gap
put(8, 4, 15, "1")
put(8, 5, 15, "T")                                 # the plate throws in over the gap
put(8, 4, 14, "F")                                 # in front of the gap: fizzles a gear on its lip or lifted in

# The faith plate, behind the van.
put(8, 0, 6, "J")

# The attic: a loft along the west wall, floor at 4. The gear drops into it
# from a chute in the top layer, back from the lip: no grabbing it from the
# street.
box(1, 3, 1, 3, 10, 18, "#")
put(2, 9, 16, "V")                                 # the chute: a new gear when one is fizzled
box(0, 0, 4, 5, 14, 14, "W")                       # the attic's white panel
box(0, 0, 1, 2, 4, 4, "W")                         # the street's

put(12, 1, 3, "S")

SOLUTION = """
// Portals: the street wall to the attic.
shoot blue 1.0 2.0 4.5
shoot orange 1.0 5.0 14.5
walk_to 0.5 4.5
wait 0.3
walk_to 2.5 15.2
grab
wait 0.4
look 0.0 5.3 14.5
wait 0.5
walk_to 0.5 14.5
wait 0.4
// The gear onto the plate, and into the van.
walk_to 8.5 5.2
look 8.5 1.6 8.5
wait 0.4
use
wait 3
walk_to 14 2.9
walk_to 16.5 2.9
"""

out = ["name: Loading the van",
       "hint: The gear is in the attic. The gap in the van is too high. The plate is not.",
       "story: Beep! A friend has room in the van to Hackfest, [Subject-Name-here]. Gear first. Lift with your legs. Or better: with a faith plate.",
       "size: %d %d %d" % (W, H, D),
       "facing: west"]
for y in range(H):
    rows = ["".join(grid[y][z]) for z in range(D - 1, -1, -1)]
    out += ["", "layer %d" % y] + rows
out += ["", "solution", SOLUTION.strip(), ""]
print("\n".join(out))
