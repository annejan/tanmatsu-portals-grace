# Chamber files

Every chamber in Portals is a text file. The files in this folder are built
into the app. You can add your own chambers on the SD card in
`/sd/portals/chambers/`. Chamber select shows them after the built-in
chambers, in file-name order. If a file has a mistake, the game skips it and
writes the line number to the log.

## An example

```
name: 06  Two rooms
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
#.A..C.#
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
| `name` | The name in Chamber select and on the screen. Start it with a number to keep the list in order. |
| `hint` | One line under the name. |
| `size` | The width (x), height (y) and depth (z) in cells. One cell is 1 m. The maximum is 24 16 24. |
| `facing` | The direction the player faces at the start: `north` (+z), `east` (+x), `south`, `west`, or degrees. |

## Layers

Write `layer N` before each height, starting at 0 for the bottom. After it come
exactly `depth` lines of exactly `width` characters. Each layer is a map seen
from above:

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
| `a` `b` `c` `d` | door cells. Each letter makes one door. The door must be a box that is one cell thick. |
| `A` `B` `C`… | a button. Put the button letter in the air cell above the solid cell it sits on. Button `A` opens door `a`. |
| `C` | a cube. Put it in the air cell where the cube starts. |
| `S` | the player's start. Use exactly one `S`. |

(`C` is the cube, not button C. The game supports buttons and doors `a`–`d`,
but chambers usually use only `A` and `B`.)

A portal needs two white cells next to each other, with air in front of both.
On a wall the two cells are one above the other. On a floor or a ceiling, they
lie in the direction the player looks. The player is 1.75 m tall, so a door
must be two cells high.

## The solution

The `solution` section is optional. It is a scripted run through the chamber.
`make check` plays the solution of every built-in chamber. If a solution does
not reach the exit, the build stops. This means a broken chamber cannot ship.
The test kit also uses solutions, to play chambers on the badge.

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

Lines that start with `//` are comments. You can write them anywhere except
between the lines of a layer.
