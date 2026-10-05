# Tool-assisted runs

Each file here is a chamber's fastest known route: a solution script, in
the language of `chambers/README.md`, for the chamber of the same name.

On the PC, `make tas` plays each at 50 steps a second against the
chamber's own solution, and `make tas TASFILM=tas.mp4` films them one after
the other, with a timer.

On the badge, `make tas-upload` puts them on the card as one recording,
`/sd/portals/recordings/tas.txt` (`main/recording.h`), and Esc -> Watch a
recording plays it back as the badge's frames come -- each step as long as
its frame, as a player at the keys -- and times it by the game's own clock.
The times go to `/sd/portals/tas-times.txt`; `make tas-result` fetches
them. Any recording in that directory can be watched the same way.

The routes were found by trimming the solutions, then by random search: a
step dropped, nudged, a jump put in, kept only if the run still reaches
the exit, sooner -- at 10, 12.5, 15, 20, 30 and 50 frames a second and at
frames of uneven length (`build/host_tas -jitter SEED`), so that a route
found on the PC holds on the badge, which plays them at about 10. One
does not quite: chamber 5's jump over the wall fails on some very uneven
frames, and it is kept for the joke. They take what the physics
allows: chamber 5 skips its cube over the wall, 17 runs the gauntlet
before the crushers get going, 19 drops onto the exit past a turret that
never looks up.

```
chamber                   solution       TAS
01-gap                        1.74      1.74
02-ledge                      2.34      1.40
03-fling                      2.66      2.54
04-button                     5.32      2.08
05-delivery                   7.14      2.60
06-faith-plate                1.54      1.54
07-the-grill                 11.16      4.26
08-through-the-glass          2.46      1.94
09-the-ferry                  6.30      5.36
10-two-buttons               10.16      4.02
11-against-the-clock          9.40      5.96
12-redirection               16.20      4.66
13-hard-light                 2.98      2.36
14-repulsion                  6.80      2.26
15-catch                      5.88      3.36
16-relay                      6.22      6.14
17-gauntlet                   5.42      2.40
18-edgeless                   8.40      4.14
19-sentry                     4.82      1.16
20-excursion                  7.12      3.14
total                       124.06     63.06
```
