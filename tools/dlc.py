#!/usr/bin/env python3
"""The extra chambers in dlc/: each a chamber file with its solution (a
spoiler). For the SD card they go without it; their solutions go as one
recording, to watch on the badge (Esc -> Watch a recording); and each is
filmed, as the game's chambers are (tools/make_movie.py).

    tools/dlc.py check          every one solved, at 20-35 frames a second (tests/host_tas.c)
    tools/dlc.py card           build/dlc/chambers/*.txt (no solutions) and build/dlc/dlc-solutions.txt
    tools/dlc.py films [ID...]  build/dlc/<id>.mp4, a film of each

`make dlc`, `make dlc-upload` and `make dlc-movies` run these.
"""

import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "dlc")


def chambers():
    return sorted(glob.glob(os.path.join(ROOT, "dlc", "*.txt")))


def split(path):
    """A chamber file's chamber, and its solution's steps (or "")."""
    with open(path) as f:
        text = f.read()
    cut = text.find("\nsolution")
    if cut < 0:
        return text, ""
    return text[:cut + 1], text[text.index("\n", cut + 1) + 1:]


def check():
    """Each solved at random 20-35 fps frames, a dozen times."""
    tool = os.path.join(ROOT, "build", "host_tas")
    env = dict(os.environ, PORTALS_CHAMBERS=os.path.join(ROOT, "dlc"))
    bad = []
    for path in chambers():
        cid = os.path.basename(path)[:-4]
        times = []
        for seed in range(1, 13):
            out = subprocess.run([tool, "-jitter", str(seed), cid], env=env, capture_output=True, text=True).stdout
            times.append(out.split("\t")[-1].strip() if out else "FAIL")
        ok = all(t and not t.startswith("FAIL") for t in times)
        print("%-24s %s  %s" % (cid, "ok  " if ok else "FAIL", " ".join(times)))
        if not ok:
            bad.append(cid)
    if bad:
        sys.exit("not solved: " + " ".join(bad))


def card():
    """For the SD card: the chambers without their solutions, and the
    solutions as one recording."""
    os.makedirs(os.path.join(OUT, "chambers"), exist_ok=True)
    with open(os.path.join(ROOT, "metadata", "metadata.json")) as f:
        version = json.load(f)["version"]
    build_id = subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=10"], cwd=ROOT,
                              capture_output=True, text=True).stdout.strip() or "unknown"
    runs = []
    for path in chambers():
        cid = os.path.basename(path)[:-4]
        chamber, steps = split(path)
        with open(os.path.join(OUT, "chambers", cid + ".txt"), "w") as f:
            f.write(chamber)
        if steps.strip():
            runs.append("\nchamber: %s\n%s" % (cid, steps))
    with open(os.path.join(OUT, "dlc-solutions.txt"), "w") as f:
        f.write("name: DLC solutions, Portals %s\nversion: %s %s\n" % (version, version, build_id))
        f.write("".join(runs))
    print("%s: %d chambers, and their solutions as dlc-solutions.txt" % (OUT, len(chambers())))


def films(ids):
    """A film of each: as the game's own chambers are filmed."""
    os.makedirs(OUT, exist_ok=True)
    for path in chambers():
        cid = os.path.basename(path)[:-4]
        if ids and cid not in ids:
            continue
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_movie.py"), cid, "--chambers",
                        os.path.join(ROOT, "dlc"), "--mp4", os.path.join(OUT, cid + ".mp4"), "--work",
                        os.path.join(OUT, "work-" + cid)], check=True)


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "check"
    if what == "check":
        check()
    elif what == "card":
        card()
    elif what == "films":
        films(sys.argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
