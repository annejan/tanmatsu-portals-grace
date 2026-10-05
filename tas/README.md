# Tool-assisted runs

Each file here is a chamber's fastest known route: a solution script, in
the language of `chambers/README.md`, for the chamber of the same name.

They are made for the badge. Its engine steps the game no more than 0.1 s
at a time (`SE_FRAME_DT_MAX`), and it draws the chambers slower than 10
frames a second, so on the badge every step is exactly 0.1 s -- and a run
there comes out to the hundredth as it does on the PC in steps of 0.1. So
the routes are made and timed in steps of 0.1 s, and must also hold at
11 a second, for a little room; at other rates many lose their way. Were
the badge to draw faster than 10 a second, they would want finding again.

On the PC, `make tas` plays each in steps of 0.1 s against the chamber's
own solution, and `make tas TASFILM=tas.mp4` films them one after the
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
chamber                   solution       TAS   (steps of 0.1 s, as on the badge)
01-gap                        1.80      1.80
02-ledge                      2.20      1.40
03-fling                      4.20      2.60
04-button                     5.90      2.30
05-delivery                   7.90      2.70
06-faith-plate                1.60      1.60
07-the-grill                 12.10      3.70
08-through-the-glass          2.20      2.10
09-the-ferry                  6.60      5.50
10-two-buttons               11.00      4.10
11-against-the-clock         10.40      5.90
12-redirection               17.20      4.70
13-hard-light                 3.10      2.50
14-repulsion                  7.20      2.30
15-catch                      6.10      3.50
16-relay                      6.40      5.40
17-gauntlet                   5.60      2.50
18-edgeless                   9.20      3.20
19-sentry                     5.00      0.90
20-excursion                  7.30      3.20
total                       133.00     61.90
```
