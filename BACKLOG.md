# Backlog

Known debts and ideas not yet done, with where they live. Newest first
within each part.

## Lifts between chambers

What there is (0.12.1): at a chamber's exit the player rises in a glass
tube as the screen fades, the next chamber loads in the dark, and they come
down onto its start (`main/main.c` `s_lift`, `main/render.c`
`submit_lift`). Settings → Lifts between chambers.

- **Lifts as places.** The tube appears where the player stands on the
  exit; it is not part of the chamber. Portal's lifts are rooms you walk
  into: a shaft at the start and at the exit, doors that close behind you,
  the ride seen through the glass. That needs a chamber glyph for a lift,
  room above the exit and below the start in every chamber, and the
  editor to place them.
- **No lift in Watch a recording.** Watch (`main/watch.c`) still pauses
  on "Chamber complete" (`REC_HOLD_S`) between chambers. The lift could
  play in that pause; the run's timer leaves the pause out already, so
  times would not change.
- **None in story packs' last chamber or desk-story rounds**, by design:
  the story's ending or the desk comes next.
- **A carried cube rides up outside the glass**: it is held 1.3 m ahead,
  the tube is 0.72 m across.
- **The pause menu does not open during the lift** (2.2 s). Anything
  that sends play elsewhere stops the lift, so it could open; it is
  blocked to keep the lift's state simple.

## The first frame after a chamber loads

The frame after a chamber loads comes with the loading in its length.
`game_step()` takes no step longer than 0.1 s, so the game's clock jumps up
to 0.1 s before the player can move.

- **Watch** counts that frame from when the chamber was there (b090fad).
  Before that fix the TAS lost its way in 04-button on the badge.
- **Normal play** does not yet. Players hardly see it, but a recording or
  a ghost captures that long first step, so every run starts 0.1 s in.
  The fix belongs in one place: `app_load_chamber()` notes the time, and
  the main loop shortens the next frame to the time since.

## Ghost races

- **Every release retires every ghost.** A ghost's signature is the
  release and a hash of the chamber (`main/ghost.c` `level_sig`,
  `ghost_begin`), so 0.12.1, whose physics is 0.12.0's, races none of
  0.12.0's ghosts or rivals. A physics version would be better: bumped
  only when `game.c`, `player.c`, `physics.c` or `portal.c` change how a
  run plays. The TAS times are a fingerprint of the physics and could
  check that version in `make check`: if they change, the version must.

## The TAS

- **The harness is not in the repo.** The routes were found with a
  search and evaluation script (eval on training frame runs, validation
  on unseen ones, exact 1/30 and 1/35 s steps, random search). It lives in
  a session's scratch space; it belongs in `tools/`.
- **`host_tas -jitter` models only 20 to 35 frames a second.** The badge
  also has long frames: the first after a load (above) and frames where a
  portal shot re-meshes the level. Options for a long first frame and for
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
