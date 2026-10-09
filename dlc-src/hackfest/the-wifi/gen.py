#!/usr/bin/env python3
# Generates the "the-wifi" chamber (Hackfest pack: geeky chores).
# g[y][z][x]; row 0 of a printed layer is z = D-1 (the far side).
#   python3 gen.py > the-wifi.txt
#
# The hall: floor at y=1, roof at y=9. The truss crosses it wall to wall,
# six metres up (deck top y=7, x1..18, z14..17), with a hatch in it right
# over a white patch on the floor; the access point (the exit) is at the
# truss's west end. Nothing reaches it from the floor.
# An excursion funnel blows across the hall, waist high, from the west wall
# into a white panel on the east wall. A portal there and one on the patch
# turn the funnel upright: it comes up out of the floor, through the hatch,
# to the roof. Steer off it onto the deck and walk to the access point.
# (The funnel comes out half a cell off the floor portal's middle, so it
# rises in one of the hatch's four cells whichever way that portal lies.)

W, H, D = 20, 10, 20
g = [[['#' for x in range(W)] for z in range(D)] for y in range(H)]


def box(x0, x1, y0, y1, z0, z1, ch):
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                g[y][z][x] = ch


# The hall: air from layer 1 to 8.
box(1, W - 2, 1, H - 2, 1, D - 2, '.')

# The funnel: an emitter low in the west wall, at z=8, blowing east right
# across the hall, waist high, into a white panel on the east wall.
g[1][8][0] = '%'
box(W - 1, W - 1, 1, 2, 8, 8, 'W')

# The truss: a deck six metres up (top y=7), wall to wall, z14..17, with a
# hatch in it (x9..10, z15..16) right over a white patch on the floor.
box(1, W - 2, 6, 6, 14, 17, '#')
box(9, 10, 6, 6, 15, 16, '.')
box(9, 10, 0, 0, 15, 16, 'W')
# The access point: the exit, at the west end of the truss.
box(2, 3, 6, 6, 15, 16, 'E')

g[1][4][10] = 'S'

HEAD = """name: The Wi-Fi
hint: The funnel goes where the portals send it. The truss is six metres up.
story: Hackfest, 17:00. Doors at 18:00, and the Wi-Fi is down, [Subject-Name-here]. The access point hangs from the truss. Have you tried turning it off and on again?
done: Access point rebooted. 400 laptops reconnect at once and nobody says thanks. That is how you know it works. SSID: hackfest. Password: hackfest.
size: %d %d %d
facing: north
""" % (W, H, D)

SOLUTION = """\
// The east wall, where the funnel ends: one portal.
shoot blue 19.0 2.0 8.5
// The patch on the floor under the hatch: the other. Looking north, the
// portal lies along the hall, so the funnel comes up in the hatch.
shoot orange 9.5 1.0 16.0
// Step into the funnel: it carries you east, through, and up.
walk_to 10.5 7.3
face 0 0
walk 0.4
wait 5.5
// At the roof, over the deck: steer west, onto it, and on to the access point.
walk_to 7.5 15.5
walk_to 2.5 15.5
wait 1
"""


def layers():
    out = []
    for y in range(H):
        out.append('layer %d' % y)
        for z in range(D - 1, -1, -1):
            out.append(''.join(g[y][z]))
        out.append('')
    return '\n'.join(out)


print(HEAD + '\n' + layers() + 'solution\n' + SOLUTION, end='')
