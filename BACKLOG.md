# Backlog

Known debts and ideas not yet done, with where they live. Newest first
within each part.

## Lifts between chambers

What there is: every chamber has a lift station at its start and its
exit, worked out from the level as it loads (`main/lift.c`
`lift_sites`); the exit's lift takes the player up, through a shaft, and
down into the next chamber's start (`lift_step`, drawn by `render.c`
`submit_lift_solid`, `submit_lift_glass`). Watch rides it too.

- **Arriving from Continue or Chamber select**: play starts on the
  spawn, the start station open around it; it could come down the shaft
  first, as between chambers.
- **The start car stays**: once the player has walked out it could rise
  into its hatch.
- **The editor's play-test** ends at the exit with its own message; it
  could shut and ride, then go back to the editor. The editor could show
  both stations (and which glass stays) where it would put them.
- **A header key to place them by hand**, `lift: exit X Z | exit none |
  start none | none`, for chambers the automatic rule does badly, and a
  station that comes to the player when the exit is far wider than a
  station (change-at-amersfoort's 15 x 3 exit: the station stands in its
  middle, a run ends at its door 6.8 m off, and a car is fitted there
  instead -- the drawn station is not the one ridden).
  Older builds reject unknown keys, so a chamber using it would need
  `chamber: needs-update` or older builds to skip unknown keys first.
- **Sounds of its own**: a ding as the doors open, a hum in the shaft.
- **None in desk-story rounds**, by design: the desk comes next.
- **The pause menu does not open while riding** (about 4 s): anything
  that sends play elsewhere stops the ride, so it could.
- **The ride takes longer than the old fade** (about 4 to 5.5 s from the
  exit to play, against 2.2 s); Jump, Use or a shot runs it three times
  as fast.

## The TAS

- **The harness is not in the repo.** The routes were found with a
  search and evaluation script (eval on training frame runs, validation
  on unseen ones, exact 1/30 and 1/35 s steps, random search). It lives in
  a session's scratch space; it belongs in `tools/`.
- **`host_tas -jitter` models only 20 to 35 frames a second.** The badge
  also has long frames where a portal shot re-meshes the level. (The first
  frame after a load used to be one; it is timed from the load now.) Options for a long first frame and for
  occasional long frames would have caught the 04-button failure on the PC.
- **`make tas` prints a table at a steady 30 frames a second.** The
  badge's frames are uneven, and the number that predicts the badge is
  the mean over many uneven runs: 40.07 s against the badge's 40.05 s,
  where the table says 40.21. 12-redirection is bimodal (1.36 s, or about
  1.85 s one run in seven), so a fixed rate misjudges it most. Print both.
- **11-against-the-clock misses about one uneven run in 200.**
- **`tests/host_review.c` pins how each TAS route is judged** (intended,
  novel, a flaw). A faster route can change that verdict: 12's went from
  novel to intended in 2d87f94. Update the table with the route.

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
