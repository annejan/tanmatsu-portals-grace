#!/usr/bin/env python3
"""The tool-assisted runs: tas/NN-name.txt is a chamber's fastest known
route, as a solution script (chambers/README.md) for the chamber of the
same name. Each is played at 30 frames a second -- the middle of the
badge's 20 to 35 -- against the chamber's own solution, and the times are
printed, and again on 200 runs of uneven frames, 20 to 35 a second, as
the badge's come: their mean is what predicts the badge's time (a route
can be quicker or slower at a fixed rate than on the badge), and a route
that misses on some is counted. With --film, the runs are filmed one
after the other, with a timer (tools/make_movie.py). tools/tas_search.py
finds and checks routes.

    tools/tas.py [--film OUT.mp4]

`make tas` builds what it needs first.
"""

import argparse
import glob
import json
import os
import subprocess
import sys


STEP = "0.033333"  # s: a step at 30 frames a second, the middle of the badge's 20-35
UNEVEN = 200       # runs of uneven frames for each route


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--film", help="film the runs, with a timer")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build = os.path.join(root, "build", "tas")
    os.makedirs(build, exist_ok=True)
    for f in glob.glob(os.path.join(build, "tas-*.txt")):
        os.remove(f)

    # Each run as a chamber of its own: the map from chambers/, the route
    # from tas/, under a name of its own (the game's would be found first).
    ids = []
    for route in sorted(glob.glob(os.path.join(root, "tas", "[0-9]*.txt"))):
        cid = os.path.basename(route)[:-4]
        with open(os.path.join(root, "chambers", cid + ".txt")) as f:
            chamber = f.read()
        cut = chamber.find("\nsolution")
        if cut >= 0:
            chamber = chamber[:cut + 1]
        with open(route) as f:
            steps = f.read()
        with open(os.path.join(build, "tas-%s.txt" % cid), "w") as f:
            f.write(chamber + "\nsolution\n" + steps)
        ids.append(cid)

    # And all of them as one recording, for the badge (main/recording.h):
    # make tas-upload puts it on the card.
    with open(os.path.join(root, "metadata", "metadata.json")) as f:
        version = json.load(f)["version"]
    with open(os.path.join(root, "build", "tas-recording.txt"), "w") as f:
        # The release and the build, as the badge writes them (main/recording.h).
        build_id = subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=10"], cwd=root,
                                  capture_output=True, text=True).stdout.strip() or "unknown"
        f.write("name: TAS, Portals %s\nversion: %s %s\n" % (version, version, build_id))
        for c in ids:
            with open(os.path.join(root, "tas", c + ".txt")) as r:
                f.write("\nchamber: %s\n%s" % (c, r.read()))

    env = dict(os.environ, PORTALS_CHAMBERS=build)
    tool = os.path.join(root, "build", "host_tas")
    # At 30 frames a second: the badge draws 20 to 35, each frame a step.
    runs = subprocess.run([tool, "-dt", STEP] + ["tas-" + c for c in ids], env=env, capture_output=True,
                          text=True).stdout
    sols = subprocess.run([tool, "-dt", STEP] + ids, env=env, capture_output=True, text=True).stdout
    t_tas = dict(line.split("\t") for line in runs.splitlines())
    t_sol = dict(line.split("\t") for line in sols.splitlines())
    # And on uneven frames, as the badge's come: 200 runs, each a seed.
    uneven = {c: [] for c in ids}
    for seed in range(1000, 1000 + UNEVEN):
        out = subprocess.run([tool, "-jitter", str(seed)] + ["tas-" + c for c in ids], env=env, capture_output=True,
                             text=True).stdout
        for line in out.splitlines():
            name, v = line.split("\t")
            uneven[name[4:]].append(None if v.startswith("FAIL") else float(v))

    print("%-24s %9s %9s %9s   (steps of %s s: 30 frames a second; uneven: the mean of %d runs at 20-35)"
          % ("chamber", "solution", "TAS", "uneven", STEP, UNEVEN))
    total_sol = total_tas = total_uneven = 0.0
    failed = []
    for c in ids:
        s, t = t_sol.get(c, "?"), t_tas.get("tas-" + c, "?")
        ok = [x for x in uneven[c] if x is not None]
        misses = len(uneven[c]) - len(ok)
        mean = sum(ok) / len(ok) if ok else float("nan")
        total_uneven += mean
        print("%-24s %9s %9s %9.2f%s" % (c, s, t, mean, "   misses %d of %d" % (misses, UNEVEN) if misses else ""))
        try:
            total_sol += float(s)
            total_tas += float(t)
        except ValueError:
            failed.append(c)
    print("%-24s %9.2f %9.2f %9.2f" % ("total", total_sol, total_tas, total_uneven))
    if failed:
        sys.exit("failed: " + " ".join(failed))

    if a.film:
        subprocess.run([sys.executable, os.path.join(root, "tools", "make_movie.py")] + ["tas-" + c for c in ids] +
                       ["--chambers", build, "--mp4", a.film, "--tas"], check=True)


if __name__ == "__main__":
    main()
