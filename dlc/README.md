# DLC: story packs

A story pack is a folder of chambers played one after another, with a
`pack.txt` saying what it is. **Spoilers:** each chamber file here ends
with its solution.

| Pack | |
|---|---|
| `human-in-the-loop/` | **Human in the loop**: a desk story, from a story brief by Renze (Nicolai Electronics). You are an RLHF specialist at an Aperture terminal, and GLaDOS's test chamber drafts are your queue: cheese them so she can patch them, debug the broken ones, approve the finals. Mail, a calendar, a cake that keeps moving, and a choice: the cake or the queue. *Hold the door* (the tutorial), *Pressure*, *Light work* (broken), *Wet floor*, then *Break room* or *Second opinion* (broken), *Observation*, *Momentum*, *The last draft*, and the walk out. Eighteen rounds, 09:00 to 22:30. `dlc-src/` has the generators. |
| `hackfest/` | **Off to Hackfest**: a desk story on your own badge's launcher. Hackfest opens at noon at Sickhouse in Enschede, on Saturday 10 October 2026 ([hackfest.nl](https://hackfest.nl/)), and there are chores first: geeky ones. TODO, GO, CHAT, and ROUTE -- by train, by bike or in a friend's van, each its own two chambers. `dlc-src/` has the generators. |
| `after-hours/` | **After hours**: the tests are over; the testing is not. *Overtime* (everything you have learned, in the right order), *Momentum* (nothing in there gives you speed: make your own, then pass it on), then *Spire*, 100 x 32 x 40 m (a fall keeps its speed through a portal, and only the top is high enough). `tools/spire_gen.py` makes Spire. |

## Making one

A folder with the chamber files (`chambers/README.md`; the badge's editor
makes them too) and a `pack.txt`:

```
name: After hours
author: annejan
about: The tests are over. The testing is not.
ending: That concludes your overtime, [Subject-Name-here].
music: overtime
chamber: overtime
chamber: momentum
```

`chamber:` lines give the order (each a file in the folder, without
`.txt`); without them the files go by name. `ending` is what GLaDOS says
when the last chamber is done; `[Subject-Name-here]` is the badge owner's
nickname, as in a chamber's `story:`. `music` is its tune (below).

## Its music

A pack may name its own music, a line in `pack.txt`:

```
music: overtime
music: office key=D bpm=64 seed=7
```

The tune first, then any of `bpm=` (40 to 180), `key=` (the tonic: `C`,
`C#`, `Db` ... `B`) and `seed=` (other music from the same tune). Each is
the game's own generator (`main/sound.c`) with chords, tempo and sounds of
its own:

| Tune | | For |
|---|---|---|
| `calm` | The game's own: minor, 76-88 BPM, a sine arp over a warm pad, a kick on the one. The default. | the built-in chambers |
| `office` | A minor at 60 BPM, a wall clock ticking it (tick, tock), a dark slow pad with a Phrygian half step, a muted square blinking now and then like a cursor; in a chamber a heartbeat comes in and the clock goes. | Human in the loop |
| `hackfest` | Major, 90-97 BPM, a square arp hopping octaves, a triangle bass in eighths, offbeat hats; a kick and snare once a chore is under way; the arrival, the party. | Off to Hackfest |
| `overtime` | Minor synthwave chords at 80-86 BPM, a filtered saw bass in eighths, a detuned saw arp, a four-to-the-floor kick, a soft clap and hats as play gets going. | After hours |

A pack's tune plays to the mood: calmer at a desk and between chambers,
more in play, tense for a few seconds when a turret sees you, gone dull a
moment when you die, muffled in a moving lift and under the menu, quieter
while GLaDOS talks, low with the outro's lights. The game's own tune does
none of it, and plays as it always has. A new tune fades in over the last
in three seconds.

Without a `music:` line a pack has the game's own tune (the three packs
here know theirs even from a card's copy older than the line). A build
that does not know the line reads past it, as it does any key it does not
know: it plays the game's own tune.

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

A `file_id.diz` in the folder (at most 45 columns and 10 lines, plain
ASCII, as BBSes and scene archives want one) goes along in the zip and
onto the card: the game does not read it.

Copy the folder to `/sd/portals/dlc/` on the badge's card. **Stories** on
the title screen lists the packs there; Continue picks up inside one.
A desk story keeps where you are in `/sd/portals/saves/<pack>.txt`, until
its end: Stories marks it "in progress" and asks whether to carry on or
start over, and `RESTART` at its desk starts it over too. A chamber run
always starts at its first chamber.

Or from [scene.rs/stories](https://scene.rs/stories/): each pack there
has a button that writes it onto a Tanmatsu over USB, straight from
Chrome or Edge (WebUSB, the launcher's BadgeLink mode), and a zip.
`tools/stories_site.py OUT_DIR` makes that page's data from the packs
here (`make dlc-zip` first): a zip, a bundle of its files and a still
for each, and an `index.json` listing them.

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
