# Tool-assisted runs

Each file here is a chamber's fastest known route: a solution script, in
the language of `chambers/README.md`, for the chamber of the same name.

They are made for the badge as it now draws: 20 to 35 frames a second,
each frame one step of its own length. A route must reach the exit at 20,
25, 30 and 35 frames a second and on frames of uneven length in that range
(`build/host_tas -jitter SEED`), and is scored on the uneven ones; on
frames it has never seen, a whole run finishes 99 times in 100.

On the PC, `make tas` plays each against the chamber's own solution, and `make tas TASFILM=tas.mp4` films them one after the
other, with a timer. On the badge, `make tas-upload` puts them on the card
as one recording, `/sd/portals/recordings/tas.txt` (`main/recording.h`),
and Esc -> Watch a recording plays it back, timed by the game's own clock;
the times go to `/sd/portals/tas-times.txt`, and `make tas-result` fetches
them.

The routes were found by trimming the solutions, by random search (a step
dropped, nudged, a jump put in, kept only if the run still reaches the
exit, sooner), and then by a specialist per chamber looking for skips the
search cannot find, each checked by another on frames neither had seen.
They take what the physics allows:

- 1, 8, 18: a floor portal under the start; fall, and fly out where it
  pays -- 18 straight past the sphere, its cup and its door.
- 4, 7, 10, 11: a cube picked up and put down in the same frame keeps the
  runner's speed: it is thrown onto its button (in 7 and 10 through a
  portal), and the runner never stops.
- 5: no cube -- up to the ledge by portal, a jump over the wall.
- 9: no ferry -- floor to floor by portal.
- 12: the runner drops into a floor portal holding the cube, which is pulled
  through after them and let go in the beam; one run in seven steps into
  the corridor a frame late and loses half a second.
- 13, 16: jumps over the pit -- 16 under the laser field, no platform.
- 14: no gel -- floor to ceiling by portal, onto the ledge.
- 17: through the gauntlet before the crushers get going.
- 19: onto the exit past a turret that never looks up.
- 20: the funnel cut off mid-ride by moving its portal.

```
chamber                   solution       TAS   (steps of 0.033333 s: 30 frames a second)
01-gap                        3.90      0.53
02-ledge                      2.37      1.30
03-fling                      2.70      2.43
04-button                     5.47      1.83
05-delivery                   7.37      2.33
06-faith-plate                1.57      1.57
07-the-grill                 11.40      1.93
08-through-the-glass          2.63      1.60
09-the-ferry                  6.33      1.33
10-two-buttons               10.43      2.00
11-against-the-clock          9.63      4.37
12-redirection                6.93      1.87
13-hard-light                 3.03      2.13
14-repulsion                  6.87      1.67
15-catch                      5.90      3.33
16-relay                      6.23      3.03
17-gauntlet                   5.47      2.40
18-edgeless                   8.67      1.23
19-sentry                     4.67      0.90
20-excursion                  7.17      2.43
total                       118.74     40.21
```

On the badge's uneven frames (200 runs at 20 to 35 frames a second) the
whole run takes 40.07 s on average.
