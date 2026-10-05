# Chamber files

Every chamber in Portals is a text file. The files in this folder are built
into the app. You can add your own chambers on the SD card in
`/sd/portals/chambers/`. Chamber select shows them after the built-in
chambers, in file-name order. If a file has a mistake, the game skips it and
writes the line number to the log.

The list has room for 30 chambers from the card. With more files than that,
the first 30 by file name are used. A file name can be up to 59 characters
before `.txt`.

## An example

```
name: 11  Two rooms
hint: The button opens the door.
size: 8 5 9
facing: north

layer 0
########
#WWWWWW#
#WWEEWW#
#WWWWWW#
########
#WWWWWW#
#WWWWWW#
#WWWWWW#
########

layer 1
########
#......#
#......#
#......#
###aa###
#......#
#.1..C.#
#...S..#
########

layer 2
########
#......#
#......#
#......#
###aa###
#......#
#......#
#......#
########

layer 3
########
#......#
#......#
#......#
########
#......#
#......#
#......#
########

solution
walk_to 5 1.5
look 5.5 1.3 2.5
use
wait 0.5
walk_to 2.5 1.6
look 2.5 1.3 2.5
wait 0.6
use
wait 1
walk_to 4 6.5
```

## The header

Write each line as `key: value`.

| Key | What |
|---|---|
| `name` | The name in Chamber select and on the screen, up to 31 characters. Start it with a number to keep the list in order. |
| `hint` | One line under the name, up to 79 characters. |
| `size` | The width (x), height (y) and depth (z) in cells. One cell is 1 m. The maximum is 24 16 24. |
| `facing` | The direction the player faces at the start: `north` (+z), `east` (+x), `south`, `west`, or degrees. |
| `story` | Optional. A line typed out along the bottom of the screen when the chamber starts, up to 159 characters. |
| `timer` | Optional. How many seconds a pedestal button stays down, from 0.5 to 60. The default is 4. |

## Layers

Write `layer N` before each height, starting at 0 for the bottom, and give
each layer once. After it come exactly `depth` lines of exactly `width`
characters. Each layer is a map seen from above:

- The **top line is the far side** (the highest z).
- **x goes from left to right.**

If you leave a layer out, it is solid metal. If a line is short, the missing
cells are metal.

| Character | Cell |
|---|---|
| `#` | metal: a portal does not stick to it |
| `W` | white panel: portals stick to it |
| `.` or space | air |
| `~` | goo: a floor that kills |
| `E` | exit: a floor that ends the chamber |
| `a` – `h` | door cells. Each letter makes one door, so a chamber can have up to 8. Use the letters in order from `a`, without gaps. A door must be a box that is one cell thick, and two doors' boxes may not overlap. |
| `1` – `8` | a button. Put it in the air cell above the solid cell it sits on: not air, a door or a fizzler. Button `1` opens door `a`, `2` opens `b`, and so on. A door can have several buttons, and it opens only while **all** of them are pressed. |
| `C` | a cube. Put it in the air cell where the cube starts. |
| `S` | the player's start. Use exactly one `S`. |
| `G` | glass: solid and see-through. A portal shot stops at it, and no portal sticks to it. |
| `F` | a fizzler: air you can walk through. Touching it closes both portals and destroys a cube you carry. A cube that touches it comes back where it started. Portal shots pass through it. |
| `J` | a faith plate, in the floor: it throws the player or a cube to its target |
| `T` | a faith plate's target: the air cell where it lands you. The first `J` goes with the first `T`, in reading order (layer by layer upwards, each layer top line first, left to right). |
| `M` | a moving platform: a box of `M` cells where it starts. It is solid, and what stands on it rides along. |
| `I` | a pedestal: a solid block, waist high. A button digit on top of it is a pedestal button. You press it with Use, and it stays down for `timer` seconds, ticking. Standing on it or putting a cube on it does nothing. |
| `V` | a cube dropper, in the ceiling: it brings its own cube, which drops out at the start and again whenever the cube is lost. The cell under it must be air. |
| `N` | where the platform goes: the cell its lowest corner travels to, outside the `M` box. It glides there and back at 1.5 m/s and pauses for a second at each end. It does not move into you. One platform per chamber. |

Every character has exactly one meaning; `make check` tests this. Older
chamber files wrote buttons as `A`, `B` and `D`; they still load, as
buttons `1`, `2` and `4`.

A portal needs two white cells next to each other, with air in front of both.
On a wall the two cells are one above the other. On a floor or a ceiling, they
lie in the direction the player looks. The player is 1.75 m tall, so a door
must be two cells high.

## The solution

The `solution` section is optional. It is a scripted run through the chamber.
`make check` plays the solution of every built-in chamber at 10, 15, 20, 25, 30
and 50 frames a second, as slow as the badge can get. If a solution does not
reach the exit at every one of them, the build stops. This means a broken
chamber cannot ship. The test kit also uses solutions, to play chambers on the
badge.

A solution has at most 63 steps and must reach the exit within 30 seconds of
game time.

| Step | What |
|---|---|
| `shoot blue x y z` | Look at the point and fire that portal (`blue` or `orange`). |
| `shoot_view orange` | Fire along the current view. |
| `look x y z` | Look at the point. |
| `face yaw pitch` | Turn to the angles, in degrees (positive pitch looks down). |
| `walk_to x z [pace]` | Walk to the point. The step ends when you arrive, after 6 s, or when you go through a portal. |
| `walk seconds` | Walk straight ahead. |
| `step_off pace` | Walk ahead until the floor ends, then let go. Use it to fall into a floor portal. |
| `wait seconds` | Stand still. |
| `use` | Pick up a cube, or put it down. |
| `grab` | Look at the nearest cube you are not carrying, and pick it up. |

Lines that start with `//` are comments. You can write them anywhere except
between the lines of a layer.
