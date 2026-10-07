# DLC: story packs

A story pack is a folder of chambers played one after another, with a
`pack.txt` saying what it is. **Spoilers:** each chamber file here ends
with its solution.

| Pack | |
|---|---|
| `after-hours/` | **After hours**: the tests are over; the testing is not. *Overtime* (everything you have learned, in the right order), then *Momentum* (nothing in there gives you speed: make your own, then pass it on). |

## Making one

A folder with the chamber files (`chambers/README.md`; the badge's editor
makes them too) and a `pack.txt`:

```
name: After hours
author: annejan
about: The tests are over. The testing is not.
ending: That concludes your overtime, [Subject-Name-here].
chamber: overtime
chamber: momentum
```

`chamber:` lines give the order (each a file in the folder, without
`.txt`); without them the files go by name. `ending` is what GLaDOS says
when the last chamber is done; `[Subject-Name-here]` is the badge owner's
nickname, as in a chamber's `story:`.

## Sharing one

Copy the folder to `/sd/portals/dlc/` on the badge's card. **Stories** on
the title screen lists the packs there; Continue picks up inside one.

`make dlc-upload` does it for every pack here: it checks every chamber is
solved (twelve times, at the badge's random 20 to 35 frames a second),
then puts each pack in `/sd/portals/dlc/<pack>/` without its solutions;
the solutions go in the pack's own `replays/` folder, so they travel with
it: Esc → Watch a recording lists them as "<pack>: the solutions".

## Replays

A pack's `replays/` folder holds recordings (`main/recording.h`) to watch
with it: the solutions, a speedrun, your own run (Record a run saves to
`/sd/portals/recordings/`; move it in). In a pack's replay a chamber may
be named by its file alone (`chamber: overtime`); elsewhere it is
`<pack>/<file>` (`chamber: after-hours/overtime`).

`make dlc-movies` films each pack, the whole story, its name to open and
its ending to close: `build/dlc/<pack>.mp4`.
