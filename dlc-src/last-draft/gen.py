#!/usr/bin/env python3
"""Human in the loop, the last round: "The last draft".

GLaDOS's finale: a nave 31 m long. You start on a balcony at the south
end; a moat of goo runs across the hall; at the north end, behind a glass
wall four metres high, is the sanctum, with a turret on a column two
metres high against the far wall, over the exit door. The only way in is
a gate in the glass, in line with the turret, and the only way across is
the light bridge -- turned north by a white panel in the east wall and
one low in the south wall. Crossing it, the turret sees you for about
twelve metres: you die.

The meant way: hold the cube out over the faith plate on the balcony and
let go. The plate aims at the top of the turret: the cube flies over the
goo and the glass, lands on the turret's head and knocks it over (the
wall behind stops the cube there, so it lands on the turret at any frame
rate). Then the bridge, the gate, the cube down off the monument, onto
button 1; door a; out. Intended: cube launch, topple, bridge, door a, no
launch.

The draft's two flaws:

  a. The plate throws whatever stands on it. Step on it yourself: it lands
     you on the turret, which falls over; carry the fallen turret to the
     button (it presses a cube button). Fix: a laser field in the air
     over the plate -- a cube passes through it, a person does not.
  b. The gate is open. Carry the cube over the bridge held low, between
     you and the turret, and in at the gate; under its column it cannot
     see you (it sees no lower than 53 degrees off level). Fix: the gate
     is a grill, and a cube carried in goes.

Hunted on the final and closed: a cube thrown by hand over the glass (300
throws: run-ups, jumps, look angles, from the bridge and the lip; the
highest a cube's bottom got near the wall was 3.3 m, short of its 4); the
cube as a shield to the gate, then a dash for the column's foot (the
turret kills you 2 m short of it, every time); portals into the sanctum
(no white surface in or beyond the glass; the only two take one portal
each). Not a soft-lock: a cube lost in the goo comes back from its
dropper, and from the lip you can turn the bridge back south again.

    python3 dlc-src/last-draft/gen.py > dlc/human-in-the-loop/last-draft.txt

x goes east, y up, z north; one cell a metre. g[y][z][x]; row 0 of a
written layer is z = D-1 (the far, north side), as the game reads it.
"""

W, H, D = 21, 12, 31
F = 3       # the floors' top: layers 0..F-1 are solid under the balcony and the dais
TOP = 10    # the last air layer; the roof is layer 11
MID = 10    # the hall's middle line, x

BAL = (1, 5)        # the balcony, z
MOAT = (6, 16)      # the goo, z
LIP, GATE = 17, 18  # the dais: its lip, and the glass wall with the gate in it
SANCT = (19, 27)    # the sanctum, z
COL = 27            # the monument's column, against the north wall
NWALL = 28          # the sanctum's north wall, door a in it
BOOTH = 29          # the exit booth, and the gallery over it

g = [[["#" for _ in range(W)] for _ in range(D)] for _ in range(H)]


def put(x, y, z, c):
    g[y][z][x] = c


def box(x0, x1, y0, y1, z0, z1, c):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, z, c)


box(1, W - 2, F, TOP, BAL[0], SANCT[1], ".")      # the hall
box(1, W - 2, 1, F - 1, MOAT[0], MOAT[1], ".")    # the moat ...
box(1, W - 2, 0, 0, MOAT[0], MOAT[1], "~")        # ... and its goo

# The monument: a turret on a column two metres high, against the north
# wall over the door. The plate's target is on top of the turret; the wall
# behind it stops the thrown cube there, whatever the frame rate.
box(MID, MID, F, F + 1, COL, COL, "#")
put(MID, F + 2, COL, "t")
put(MID, F + 3, COL, "T")
put(7, F - 1, 26, "K")
put(7, F, 26, "1")

# The door, and the booth behind it: glass either side of the door, and a
# gallery high over it, behind glass, for whoever watches.
box(MID - 1, MID + 1, F, F + 1, NWALL, NWALL, "a")
box(MID - 3, MID + 3, F, F + 1, BOOTH, BOOTH, ".")
box(MID - 1, MID + 1, F - 1, F - 1, BOOTH, BOOTH, "E")
box(MID - 3, MID - 2, F, F + 1, NWALL, NWALL, "G")
box(MID + 2, MID + 3, F, F + 1, NWALL, NWALL, "G")
box(MID - 5, MID + 5, F + 3, F + 5, NWALL, NWALL, "G")
box(MID - 5, MID + 5, F + 3, F + 5, BOOTH, BOOTH, ".")
# Two pillars flank the monument.
for x in (MID - 4, MID + 4):
    box(x, x, F, TOP, COL, COL, "#")

# The sanctum's front: glass four metres high -- higher than anything can
# be thrown by hand -- and a gate in it, between two posts, where the
# bridge comes in. Open, in the draft.
box(1, W - 2, F, F + 3, GATE, GATE, "G")
box(MID - 1, MID + 1, F, F + 3, GATE, GATE, ".")
box(MID - 2, MID - 2, F, TOP, GATE, GATE, "#")
box(MID + 2, MID + 2, F, TOP, GATE, GATE, "#")

# Buttresses down into the goo along both walls, and a cornice along the
# top of them: a nave, not a box.
for z in (9, 12, 15):
    box(1, 1, 0, TOP, z, z, "#")
    box(W - 2, W - 2, 0, TOP, z, z, "#")
box(1, 1, TOP, TOP, BAL[0], SANCT[1], "#")
box(W - 2, W - 2, TOP, TOP, BAL[0], SANCT[1], "#")

# The faith plate, a row back from the balcony's edge, west of the
# bridge's line; a glass parapet along the edge, open where the plate
# throws and the bridge goes.
put(MID - 1, F - 1, BAL[1] - 1, "J")
box(1, MID - 3, F, F, BAL[1], BAL[1], "G")
box(MID + 3, W - 2, F, F, BAL[1], BAL[1], "G")

# The light bridge: its emitter in the west wall, along the moat's south
# edge into a white panel in the east wall; the other white panel low in
# the south wall, on the hall's middle line.
put(0, F, MOAT[0], "H")
box(W - 1, W - 1, F, F + 1, MOAT[0], MOAT[0], "W")
box(MID, MID, F, F + 1, 0, 0, "W")

# The cube dropper and the start.
put(3, H - 1, 4, "V")
put(4, F, 2, "S")

FIX_A = (MID - 1, F, BAL[1] - 1)               # the air over the plate
FIX_B = ((MID - 1, MID + 1), (F, F + 3), GATE)  # the gate's grill

HEAD = [
    "name: The last draft",
    "hint: The plate aims at the turret. Give it something to throw.",
    "story: My last draft tonight, and my best: everything you taught me is in it. It is not for you, [Subject-Name-here]. This one is for her.",
    "review: flawed",
    "intended: cube launch, topple, bridge, door a, no launch",
    "flaw: a launch -- The plate threw you, not the cube: over the goo, down on the turret's head. No bridge.",
    "flaw: b bridge, no cube launch -- The cube walked in at the gate in your arms, held up to the turret as a shield all the way.",
    "fix: a %d %d %d .*" % FIX_A,
    "fix: b %d-%d %d-%d %d .F" % (FIX_B[0][0], FIX_B[0][1], FIX_B[1][0], FIX_B[1][1], FIX_B[2]),
    "size: %d %d %d" % (W, H, D),
    "facing: north",
]

# Script coordinates.
P = dict(
    jx=MID - 0.5, jz_stand=BAL[1] - 1.8, jz_look=BAL[1] - 0.4,  # at the plate
    bz=1.0, ax=W - 1.0, az=MOAT[0] + 0.5,                     # the white panels
    mx=MID + 0.5, colz=COL - 0.4,                              # the column's foot
    cubez=COL + 0.6, cubey=F + 3.1,                            # the cube on the turret
    kx=7.5, kz=26.5, kpx=8.9,                                  # button 1
    dx=MID - 0.5, dz=NWALL - 0.5, ez=BOOTH + 0.5,              # the door, the booth
    shz=COL - 1.2, colz2=COL + 0.4,
)

THROW = """wait 0.8
walk_to 3.5 3.5
grab
wait 0.3
walk_to {jx} {jz_stand}
look {jx} 3.6 {jz_look}
wait 0.6
use
wait 2.2"""

BRIDGE = """shoot blue {mx} 4.0 {bz}
shoot orange {ax} 4.0 {az}
wait 0.3"""

FINISH = """look {mx} {cubey} {cubez}
use
wait 0.4
walk_to {kpx} {kz}
look {kx} 3.0 {kz}
wait 0.6
use
wait 1.2
walk_to {dx} {dz}
walk_to {dx} {ez}"""

SOLUTION = ("""
solution
// One: the cube from the dropper, held out over the plate and let go.
// It flies over the goo and the glass and knocks the turret off its feet.
""" + THROW + """
// Two: the bridge, turned north across the moat, in at the gate.
""" + BRIDGE + """
walk_to {mx} 3.0
walk_to {mx} {colz}
// Three: the cube down off the monument, onto its button; out.
""" + FINISH + "\n").format(**P)

CHEESES = ("""
cheese a
// No cube: onto the plate yourself. It lands you on the turret; carry
// the fallen turret to the button.
walk_to {jx} 3.0
face 0 0
walk 0.4
wait 3.0
walk_to {mx} {colz}
look {mx} 5.45 {colz2}
use
wait 0.4
walk_to {kpx} {kz}
look {kx} 3.0 {kz}
wait 0.6
use
wait 1.2
walk_to {dx} {dz}
walk_to {dx} {ez}

cheese b
// The cube carried over the bridge and in at the gate, held low between
// you and the turret.
wait 0.8
walk_to 3.5 3.5
grab
wait 0.3
""" + BRIDGE + """
walk_to {mx} 3.0
face 0 6
walk_to {mx} {shz}
// Under its column it cannot see you: the cube on its button, and out.
walk_to {kpx} {colz}
look {kx} 3.0 {kz}
wait 0.6
use
wait 1.2
walk_to {dx} {dz}
walk_to {dx} {ez}
""").format(**P)


def layers():
    out = []
    for y in range(H):
        out.append("")
        out.append("layer %d" % y)
        for z in range(D - 1, -1, -1):
            out.append("".join(g[y][z]))
    return out


print("\n".join(HEAD + layers()) + "\n" + SOLUTION + CHEESES)
