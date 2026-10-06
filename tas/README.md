# Tool-assisted runs

Each file here is a chamber's fastest known route: a solution script, in
the language of `chambers/README.md`, for the chamber of the same name.

They are made for the badge as it now draws: 20 to 35 frames a second,
each frame one step of its own length. A route must reach the exit at 20,
25, 30 and 35 frames a second and on frames of uneven length in that range
(`build/host_tas -jitter SEED`), and is scored on the uneven ones; on
frames it has never seen, a whole run finishes about 49 times in 50.

On the PC, `make tas` plays each against the chamber's own solution, and `make tas TASFILM=tas.mp4` films them one after the
other, with a timer. On the badge, `make tas-upload` puts them on the card
as one recording, `/sd/portals/recordings/tas.txt` (`main/recording.h`),
and Esc -> Watch a recording plays it back, timed by the game's own clock;
the times go to `/sd/portals/tas-times.txt`, and `make tas-result` fetches
them.

The routes were found by trimming the solutions, then by random search: a
step dropped, nudged, a jump put in, kept only if the run still reaches
the exit, sooner. They take what the physics allows: chamber 5 skips its
cube over the wall, 17 runs the gauntlet before the crushers get going, 19
drops onto the exit past a turret that never looks up.

```
chamber                   solution       TAS   (steps of 0.033333 s: 30 frames a second)
01-gap                        1.77      1.77
02-ledge                      2.37      1.37
03-fling                      2.70      2.57
04-button                     5.47      2.07
05-delivery                   7.37      3.03
06-faith-plate                1.57      1.57
07-the-grill                 11.40      3.27
08-through-the-glass          2.63      1.93
09-the-ferry                  6.33      5.37
10-two-buttons               10.43      4.03
11-against-the-clock          9.63      5.63
12-redirection               16.37      4.77
13-hard-light                 3.03      2.37
14-repulsion                  6.87      2.27
15-catch                      5.90      3.37
16-relay                      6.23      6.20
17-gauntlet                   5.47      2.40
18-edgeless                   8.67      3.37
19-sentry                     4.67      0.90
20-excursion                  7.17      3.17
total                       126.05     61.43
```
