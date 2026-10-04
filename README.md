# Portals

Portal puzzles for the [Tanmatsu](https://nicolaielectronics.nl/), built on
[SynthEngine3D](https://github.com/nullislandspace/synthengine3D) and loaded by
[Graceloader](https://github.com/nullislandspace/tanmatsu-graceloader).

Shoot two holes in the white panels of a test chamber. Walk into one and come
out of the other, keeping your momentum. Three chambers are included:

1. **The gap**: get across a pit of goo.
2. **The ledge**: get up onto a ledge that no jump reaches.
3. **The fling**: fall 10 m into a floor portal and fly out of a wall.

## Controls

The defaults are below. Every one can be rebound in **Esc → Controls**, and
the new keys are kept in NVS.

| Key | |
|---|---|
| W A S D | walk |
| Cursor keys | look |
| Space | jump |
| Q / E | fire the blue / orange portal |
| G | gyroscope on or off: look round by turning the badge |
| R | restart the chamber |
| Esc | the menu (not rebindable) |
| F1 | back to the launcher |

The menu has:

- **Chamber select**.
- **Settings**: gyroscope, quarter resolution, portal depth (1 to 3), volume,
  screen brightness, keyboard light.
- **Controls**.

Settings are kept in NVS.

Aim portals at white panels. On a wall, a portal sits on the lower of the two
panels it could use, so a shot at eye height stands on the floor, where you can
walk into it.

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
(`tests/host_shot.c`) into `build/shots/*.png`. Use it to look at the portal
passes without a badge.

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
views are drawn deepest first, up to `P` levels. The deepest one gets a flat
fill. See `main/render.c`.

A portal is a frame (right, up, n). Going in through A and out of B is a half
turn about `up`: A's (r, u, n) leaves as B's (−r, u, −n). Positions, velocities,
the camera basis and the clip planes all go through that one map
(`main/portal.c`).

## Layout

| File | |
|---|---|
| `main/level.*` | the cell grid, the chambers, raycast, greedy meshing |
| `main/portal.*` | placement, the A→B map, clip planes, polygon clipping |
| `main/player.*` | box physics, the tunnel behind a portal, teleporting |
| `main/render.*` | the portal passes |
| `main/input.*` | bindings (se_bindings), keyboard, gyroscope |
| `main/menu.*` | the pause menu and its screens (se_ui) |
| `main/settings.*` | settings kept in NVS |
| `main/main.c` | the run loop, the HUD |
| `tests/` | host tests and host screenshots |
| `tools/make_textures.py` | the textures in `textures/` |

The project comes from
[tanmatsu-template-grace](https://github.com/nullislandspace/tanmatsu-template-grace),
which stays as the `upstream` remote: `git fetch upstream && git merge upstream/main`
brings in graceloader's symbol-export updates.

## License

MIT, like the template.
