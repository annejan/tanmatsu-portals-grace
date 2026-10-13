# Backlog

Known debts and ideas not yet done, with where they live. Newest first
within each part.

## Lifts between chambers

What there is: every chamber has a lift station at its start and its
exit, worked out from the level as it loads (`main/lift.c`
`lift_sites`); the exit's lift takes the player up, through a shaft, and
down into the next chamber's start (`lift_step`, drawn by `render.c`
`submit_lift_solid`, `submit_lift_glass`). Watch rides it too. Since
0.12.4: play begun from the menu arrives by lift, the start car goes back
up once the player is out, the play-test rides it, the editor shows both
stations, and it dings and hums.

- **A long exit's station goes by the way in**: the block of exit cells
  nearest where walkable floor or a door meets the exit. An exit open on
  every side (change-at-amersfoort's 15 x 3 platform) still gets its
  middle; for such a chamber:
- **A header key to place them by hand**, `lift: exit X Z | exit none |
  start none | none`, for chambers the automatic rule does badly, and a
  station that comes to the player when the exit is far wider than a
  station (change-at-amersfoort's 15 x 3 exit: the station stands in its
  middle, a run ends at its door 6.8 m off, and a car is fitted there
  instead -- the drawn station is not the one ridden).
  Older builds reject unknown keys, so a chamber using it would need
  `chamber: needs-update` or older builds to skip unknown keys first.
- **None in desk-story rounds**, by design: the desk comes next.
- **The pause menu does not open while riding** (about 4 s): anything
  that sends play elsewhere stops the ride, so it could.
- **The ride takes longer than the old fade** (about 4 to 5.5 s from the
  exit to play, against 2.2 s); Jump, Use or a shot runs it three times
  as fast.

## Splash screens

- **A Graceloader splash**, between SynthEngine 3D's and Portals' own: the
  loader the game runs under (nullislandspace/tanmatsu-graceloader). It has
  no logo or look of its own yet -- one would have to be made first, ideally
  with its author. Then: drawn as the engine's splash is (se_splash.c, real
  3D strokes) or as Portals' is (main/splash.c), and rendered for the films
  by tests/host_splash.c with the other two.

## Frame time on the badge

Measured 2026-10-10 with Settings -> Frame times on the TAS (the times file
now breaks each chamber's frames into update and render, and the slowest
frame's passes and triangles). Left as they are, on purpose:

- **Rendering is the frame.** 25-40 ms a frame; about 24 ms even for one
  pass of 80 triangles, so the floor is the engine's per-pixel work, not
  the scene. Each portal pass adds 5-15 ms. Faster would mean the engine's
  rasterizer (an upstream change, like SE_TRI_GLOW).
- **GLaDOS's lines cost a frame each.** speak() renders a whole sentence
  with SAM inside one update: 15-35 ms on the badge (0.3-0.55 ms on a PC),
  the 13-52 ms update spikes. Could be rendered while the chamber loads
  (hidden in the lift's dark), or on the other core; not felt much.
- **Blended fizzlers are 07-the-grill's cost.** 45 % of its frames are
  blended pixels (100 % walking through), 35 ms renders against 28; glass
  elsewhere is 0-5 %. Cut-out fizzler streaks would be cheaper but look
  different; glass must stay blended to be told from an opening.
- **The portal mesh is not a cost.** A shot re-meshes only its slices:
  microseconds.

## The TAS

`tools/tas_search.py` finds and checks routes (eval, search, robust),
`host_tas -first` / `-hitch` add the badge's long frames, and `make tas`
prints the mean of 200 uneven runs beside the fixed-rate table: that
mean is what predicts the badge (40.07 s against its 40.05 s).

- **11-against-the-clock misses about one uneven run in 600** (seed 1148
  of 1000-1199; none of 2000-2399). A search trained on that seed found
  routes 0.02-0.04 s faster that miss 4-7 of 400 unseen runs: tuned to the
  41 runs they were trained on. Train on more seeds before trusting one.
- **`tests/host_review.c` pins how each TAS route is judged** (intended,
  novel, a flaw). A faster route can change that verdict: 12's went from
  novel to intended in 2d87f94. Update the table with the route.

## The card under Graceloader

- **FatFs buffers in RAM the SD card cannot reach** (fixed in Graceloader
  2.8.0). Graceloader put TCM (SPM) and LP RAM in the heap, and FatFs
  mallocs each open file's sector buffer with plain `malloc`; when it landed
  there, the card's DMA failed: reads failed, writes failed at `fclose`
  with EIO. Graceloader 2.8.0 (in the app store since 2026-10-13) bounces
  such buffers in its SD disk layer and turns the LP RAM heap off -- its
  own fix (35b59e5), which supersedes the one offered as
  nullislandspace/tanmatsu-graceloader#1. `main/card.c`'s `card_fopen`
  (it holds that RAM while a file opens) is now only for Graceloaders
  before 2.8.0; it can go once those are gone.
- **Older Graceloader (before 2.7.0) does not retry a failed SD write.**
  Ghosts and Watch's times are written a second time if the first fails.

## Engine

- **The engine is a fork until upstream merges.** Light (`SE_TRI_GLOW`) is
  offered as nullislandspace/synthengine3D#1; `.gitmodules` points at
  annejan/synthengine3D, branch tri-glow, until it is merged. Then point it
  back at nullislandspace and bump the submodule.

## Host renders

- **Scratch render programs need setup to draw right.** A program that
  includes `tests/host_shot.c` with its `main` renamed must call
  `render_init("textures")` itself and run with `HOST_SHOT_TEXTURES` set
  (for example `build/movie/tex`). Otherwise glass and fields draw as flat
  walls. The first lift preview did.
