# DLC: story packs

A story pack is a folder of chambers played one after another, with a
`pack.txt` saying what it is. **Spoilers:** each chamber file here ends
with its solution.

| Pack | |
|---|---|
| `human-in-the-loop/` | **Human in the loop**: a desk story. You are an RLHF specialist at an Aperture terminal, and GLaDOS's test chamber drafts are your queue: cheese them so she can patch them, debug the broken one, approve the finals. Mail, a calendar, a cake. *Hold the door* (the tutorial), *Pressure*, *Light work* (broken), *Momentum*, and the walk out. `dlc-src/` has the generators. |
| `after-hours/` | **After hours**: the tests are over; the testing is not. *Overtime* (everything you have learned, in the right order), *Momentum* (nothing in there gives you speed: make your own, then pass it on), then *Spire*, 100 x 32 x 40 m (a fall keeps its speed through a portal, and only the top is high enough). `tools/spire_gen.py` makes Spire. |

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

## A story told from a desk

A pack can be a story of review rounds instead (`main/review.h`,
`chambers/README.md` § Review rounds): GLaDOS's drafts, which you cheese,
debug or approve from an Aperture terminal.

```
name: Human in the loop
frame: desk
round: hold-the-door
round: pressure
outro: walkout
```

`round:` lines give the rounds, each a chamber file with review keys and,
after its solution, a `cheese X` route for each flaw; `outro:` is the
ending's scene; `desk/` holds the terminal's files and mail. A build that
does not know desk stories reads only `chamber:` lines: the card copy gets
a `chamber: needs-update`, a room that says to update.

`make dlc` judges every round as the game will, at the badge's frame
rates: a flawed round's solution is the meant way, each cheese finds its
flaw, that flaw's fix closes it, and the meant way survives every fix; a
final round is solved the meant way; a broken one only after its repairs.
The cheese routes go in the pack's `replays/cheeses.txt`, to watch.
`make dlc-zip` makes `build/dlc/<pack>.zip`, to unzip into
`/sd/portals/dlc/`.

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
