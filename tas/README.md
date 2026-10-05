# Tool-assisted runs

Each file here is a chamber's fastest known route: a solution script, in
the language of `chambers/README.md`, for the chamber of the same name.
`make tas` plays each at 50 steps a second, as a tool-assisted run is
played, against the chamber's own solution; `make tas TASFILM=tas.mp4`
films them one after the other, with a timer.

The routes were found by trimming the solutions and then by random search
(each step dropped, nudged, a jump put in, kept only if the run still
reaches the exit sooner), and take whatever the physics allows: chamber 5
never touches its cube, chamber 17 runs the gauntlet before the crushers
get going, chamber 19 drops onto the exit past a turret that never looks
up.

```
chamber                   solution       TAS
01-gap                        1.74      1.74
02-ledge                      2.34      1.34
03-fling                      2.66      2.48
04-button                     5.32      2.26
05-delivery                   7.14      2.60
06-faith-plate                1.54      1.54
07-the-grill                 11.16      4.18
08-through-the-glass          2.46      1.94
09-the-ferry                  6.30      5.34
10-two-buttons               10.16      4.02
11-against-the-clock          9.40      5.94
12-redirection               16.20      4.54
13-hard-light                 2.98      2.36
14-repulsion                  6.80      2.26
15-catch                      5.88      3.36
16-relay                      6.22      6.14
17-gauntlet                   5.42      2.40
18-edgeless                   8.40      3.48
19-sentry                     4.82      0.90
20-excursion                  7.12      3.14
total                       124.06     61.96
```
