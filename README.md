# Portals

Portal puzzles for the [Tanmatsu](https://nicolaielectronics.nl/), built on
[SynthEngine3D](https://github.com/nullislandspace/synthengine3D) and loaded by
[Graceloader](https://github.com/nullislandspace/tanmatsu-graceloader).

Shoot two holes in the white panels of a test chamber. Walk into one and come
out of the other, keeping your momentum. Seventeen chambers are included:

1. **The gap**: get across a pit of goo.
2. **The ledge**: get up onto a ledge that no jump reaches.
3. **The fling**: fall 10 m into a floor portal and fly out of a wall.
4. **The button**: put a cube on a button to hold a door open.
5. **Delivery**: the cube is up on a ledge and the button is down below.
   Carry the cube through a portal.
6. **Faith plate**: a plate throws you over the goo.
7. **The grill**: a fizzler destroys a cube you carry. Send the cube
   across through a portal instead.
8. **Through the glass**: you can see the exit through the glass, but you
   cannot shoot through it. Look over it.
9. **The ferry**: ride a moving platform across the goo.
10. **Two buttons**: a door that needs both its buttons pressed at once.
11. **Against the clock**: a pedestal button holds the door for three
    seconds, and the door is four seconds' walk away. A dropper supplies
    the cube for the other button.
12. **Redirection**: a laser, a catcher out of its line, and a reflection
    cube to turn the beam.
13. **Hard light**: a light bridge that points the wrong way, and a pit
    only it can cross.
14. **Repulsion**: blue gel drips in the wrong place, and a ledge too high
    to jump.
15. **Catch**: an energy pellet, and a receiver out of its line.
16. **Relay**: a platform that waits for a laser relay, and a laser field
    on the ledge you were hoping to use instead.
17. **Gauntlet**: a corridor under three crushers. Keep to the beat.

Cubes can be picked up, stood on and carried through portals. A cube
that falls in the goo comes back where it started. A door stays open while
all of its buttons are pressed, and does not shut on anything standing in
it.

## Controls

The defaults are below. Every one can be rebound in **Esc → Controls**, and
the new keys are kept in NVS.

| Key | |
|---|---|
| W A S D | walk |
| Cursor keys | look |
| Space | jump |
| Q / E | fire the blue / orange portal |
| F | pick up or put down a cube |
| G | gyroscope on or off: look round by turning the badge |
| R | restart the chamber |
| Esc | the menu (not rebindable) |
| F1 | back to the launcher |

The menu has:

- **Chamber select**.
- **Settings**: gyroscope, quarter resolution, portal depth (1 to 3), music,
  sound effects, GLaDOS voice, portal LEDs, volume, screen brightness,
  keyboard light. GLaDOS reads each chamber's story line out, in SAM's
  voice.
  With Portal LEDs on, the badge's user LEDs show the portals while you
  play: A orange while the orange portal is open, B blue for the blue one.
  The system LEDs are dark meanwhile, and come back when you quit.
- **Controls**.

Settings are kept in NVS.

Aim portals at white panels. On a wall, a portal sits on the lower of the two
panels it could use, so a shot at eye height stands on the floor, where you can
walk into it.

## The chamber editor

Esc → **Chamber editor** edits the chamber you are playing. A built-in
chamber is edited as a copy, named after its file: `my-01-gap`. To start from
an empty chamber, choose **New empty chamber** in the editor's menu. A new
chamber never overwrites a file that is already on the card.

The editor shows one layer at a time, from above. The top of the map is the
far side.

| Key | |
|---|---|
| arrows | move the cursor (hold to move faster) |
| Q / E | go one layer down / up |
| 1 – 9 | select a brush: metal, white, air, goo, exit, door, button, cube, start. Press 6 or 7 again to step through the doors `a`–`h` / their buttons `1`–`8`. |
| 0 - = T | select a brush: glass, fizzler, faith plate, plate target |
| M N | select a brush: moving platform, where it goes |
| I V K | select a brush: pedestal (put a button on top), cube dropper, cube button base (likewise) |
| L O Y H | select a brush: laser emitter, laser catcher (a button on top), reflection cube, light bridge emitter |
| U Z X | select a brush: blue, orange and white gel dispenser |
| J C | select a brush: pellet launcher, pellet receiver (a button on top) |
| G ; [ | select a brush: laser relay (a button on top), laser field, crusher |
| Space | paint. Hold it while you move to paint a line. |
| B | press it at one corner, then at the other corner, to fill a rectangle |
| Backspace | erase (set the cell to air) |
| R | turn the start direction |
| P | play-test. Esc goes back to the editor. |
| F | save to `/sd/portals/chambers/<file name>.txt` |
| Esc | the editor's menu: play-test, save, size, start facing, new, quit |

The editor works with the characters of the chamber file. The game reads
the saved file in the same way as a hand-written one. If you save a chamber
with a mistake, such as a door that is not a box, the editor shows the
parser's message and does not save. A built-in chamber's `solution` is
kept, but the editor does not check that it still works.

## Building

```sh
git clone --recursive https://github.com/annejan/tanmatsu-portals-grace.git
make badgelink     # once: the file-transfer tool
make build         # host checks, then app.so
make install run   # onto the badge, then start it
```

`make build` runs `make check` first. `make check` builds the game logic with
the host compiler and runs `tests/host_test.c`. That file tests the portal
maths and placement, and a scripted player solves every chamber with real
portal shots and real physics. A change that breaks a chamber fails the
build.

`make shots` draws the game's own `render.c` with a small software rasterizer
(`tests/host_shot.c`) into `build/shots/*.png` (needs Pillow). Use it to look
at the portal passes without a badge.

## Device tests

`main/testkit/` (from the template) runs the scripted demos on the badge.
Each chamber's `solution` is one demo; `c1walk` and `c1loop` are two more.
The tests can measure the frame rate, or save screenshots at exact moments
and compare them:

```sh
make cycle TEST="perf scene=03-fling secs=10"
make cycle TEST="shots scene=c1walk ms=1300,1500" TESTFLAGS=--fetch
```

The tests talk to the ESP32-P4's debug console. `tools/p4port.sh` finds the
console by the P4's USB hub port, which it learns during `make install`
(BadgeLink mode). The ESP32-C6's serial port is never opened, because opening
it crashes the badge: every tool asks `p4port.sh` before it opens a port. To
name the console yourself, set `P4_CONSOLE`; it is checked the same way.
`PORT` is ignored on purpose, since other projects set it to `/dev/ttyACM0`,
which on a Tanmatsu is often the C6.

**Known problem:** the console sends nothing yet, even while the game is
running, so these tests cannot connect. Use `make check` and `make shots` on
the PC instead.

## How the portals are drawn

SynthEngine3D has no stencil buffer, and the game does not need one:

- The view through a portal is drawn first. Every triangle is clipped in world
  space to the planes through the eye and the portal's edges, so the view
  paints only the portal's opening.
- `scene_begin()` starts the next pass with an empty depth buffer and leaves
  the pixels as they are.
- The room is drawn last. The two cell faces under each portal are left out of
  its mesh, so the room paints everything except the openings.

The view through a portal can contain the same portal again. These nested
views are drawn deepest first, as deep as Settings → Portal depth (1 to 3).
The deepest one gets a flat fill. See `main/render.c`.

A portal is a frame (right, up, n). Going in through A and out of B is a half
turn about `up`: A's (r, u, n) leaves as B's (−r, u, −n). Positions, velocities,
the camera basis and the clip planes all go through that one map
(`main/portal.c`).

## Layout

| File | |
|---|---|
| `main/level.*` | the cell grid, the chambers, raycast, greedy meshing |
| `main/portal.*` | placement, the A→B map, clip planes, polygon clipping |
| `main/physics.*` | box physics for the player and the cubes, the tunnel behind a portal, teleporting |
| `main/player.*` | walking, jumping, the view |
| `main/game.*` | one chamber in play: cubes, carrying, buttons, doors |
| `main/chamber.*` | the chamber file format: parsing, writing, the list of chambers |
| `main/draft.*`, `main/editor.*` | the chamber editor |
| `main/sound.*` | sound effects and music |
| `main/demo.*` | scripted runs that solve each chamber, for tests on the PC and the badge |
| `main/render.*` | the portal passes |
| `main/input.*` | bindings (se_bindings), keyboard, gyroscope |
| `main/menu.*` | the pause menu and its screens (se_ui) |
| `main/settings.*` | settings kept in NVS |
| `main/main.c` | the run loop, the HUD |
| `main/testkit/` | device tests (from the template) |
| `tests/` | host tests and host screenshots |
| `tools/make_textures.py` | the textures in `textures/` |

The project comes from
[tanmatsu-template-grace](https://github.com/nullislandspace/tanmatsu-template-grace),
which stays as the `upstream` remote: `git fetch upstream && git merge upstream/main`
brings in graceloader's symbol-export updates.

## License

MIT, like the template, except for SAM, below. The licensing of each file
is in `REUSE.toml` and `LICENSES/`, following [REUSE](https://reuse.software).
The headers in `include/` come with the Graceloader SDK and keep their own
licenses; some of them do not say which yet.

**GLaDOS's voice is SAM**, the Software Automatic Mouth: the C64's speech
program from 1982, in Sebastian Macke's reverse-engineered C port
(`third_party/sam/`). SAM has no license: Don't Ask Software, who sold it,
no longer exists, and the port's author cannot license it. It is included
as it is and is not covered by the MIT license; see
`LICENSES/LicenseRef-SAM-Abandonware.txt` and `third_party/sam/README.md`.
Settings → GLaDOS voice turns it off.
