#!/usr/bin/env python3
"""The story packs in dlc/: each a folder, dlc/<pack>/, of chamber files with
their solutions (spoilers) and a pack.txt giving its name, author, blurb,
ending and the chambers' order (main/pack.h). On the badge's card a pack is
the same folder, /sd/portals/dlc/<pack>/, without the solutions; their
solutions go as one recording each, to watch (Esc -> Watch a recording).

    tools/dlc.py check            every chamber solved, at 20-35 frames a second (tests/host_tas.c)
    tools/dlc.py card             build/dlc/<pack>/ for the card, and build/dlc/<pack>-solutions.txt
    tools/dlc.py films [PACK...]  build/dlc/<pack>.mp4: the whole story, filmed (tools/make_movie.py)

`make dlc`, `make dlc-upload` and `make dlc-movies` run these.
"""

import glob
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "dlc")


def packs():
    return sorted(d for d in glob.glob(os.path.join(ROOT, "dlc", "*")) if os.path.isdir(d))


def pack_info(folder):
    """A pack's pack.txt as a dict, and its chambers' ids in order (as
    main/pack.c reads it: "chamber:" lines, else the files by name)."""
    info, order = {"name": os.path.basename(folder)}, []
    path = os.path.join(folder, "pack.txt")
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                key, _, value = line.strip().partition(":")
                if key == "chamber":
                    order.append(value.strip())
                elif key in ("name", "author", "about", "ending"):
                    info[key] = value.strip()
    if not order:
        order = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(folder, "*.txt"))
                       if os.path.basename(p) != "pack.txt")
    return info, order


def split(path):
    """A chamber file's chamber, and its solution's steps (or "")."""
    with open(path) as f:
        text = f.read()
    cut = text.find("\nsolution")
    if cut < 0:
        return text, ""
    return text[:cut + 1], text[text.index("\n", cut + 1) + 1:]


def check():
    """Each chamber solved at random 20-35 fps frames, a dozen times."""
    tool = os.path.join(ROOT, "build", "host_tas")
    bad = []
    for folder in packs():
        info, order = pack_info(folder)
        print("%s (%s)" % (info["name"], os.path.basename(folder)))
        env = dict(os.environ, PORTALS_CHAMBERS=folder)
        for cid in order:
            times = []
            for seed in range(1, 13):
                out = subprocess.run([tool, "-jitter", str(seed), cid], env=env, capture_output=True, text=True).stdout
                times.append(out.split("\t")[-1].strip() if out else "FAIL")
            ok = all(t and not t.startswith("FAIL") for t in times)
            print("  %-22s %s  %s" % (cid, "ok  " if ok else "FAIL", " ".join(times)))
            if not ok:
                bad.append("%s/%s" % (os.path.basename(folder), cid))
    if bad:
        sys.exit("not solved: " + " ".join(bad))


def card():
    """For the card: each pack's folder without the solutions, and the
    solutions as a recording."""
    with open(os.path.join(ROOT, "metadata", "metadata.json")) as f:
        version = json.load(f)["version"]
    build_id = subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=10"], cwd=ROOT,
                              capture_output=True, text=True).stdout.strip() or "unknown"
    for folder in packs():
        pid = os.path.basename(folder)
        info, order = pack_info(folder)
        out = os.path.join(OUT, pid)
        shutil.rmtree(out, ignore_errors=True)
        os.makedirs(out)
        if os.path.exists(os.path.join(folder, "pack.txt")):
            shutil.copy(os.path.join(folder, "pack.txt"), out)
        runs = []
        for cid in order:
            chamber, steps = split(os.path.join(folder, cid + ".txt"))
            with open(os.path.join(out, cid + ".txt"), "w") as f:
                f.write(chamber)
            if steps.strip():
                runs.append("\nchamber: %s\n%s" % (cid, steps))
        # Its replays travel with it: in its own replays/ (main/pack.h), the
        # chambers named by their files alone.
        os.makedirs(os.path.join(out, "replays"))
        with open(os.path.join(out, "replays", "solutions.txt"), "w") as f:
            f.write("name: the solutions, Portals %s\nversion: %s %s\n" % (version, version, build_id))
            f.write("".join(runs))
        print("%s: %s, %d chambers, and replays/solutions.txt" % (out, info["name"], len(order)))


def films(wanted):
    """Each pack filmed, the whole story: its chambers one after another,
    the music playing on, its name to open and its ending to close."""
    os.makedirs(OUT, exist_ok=True)
    for folder in packs():
        pid = os.path.basename(folder)
        if wanted and pid not in wanted:
            continue
        info, order = pack_info(folder)
        cmd = [sys.executable, os.path.join(ROOT, "tools", "make_movie.py")] + order + [
            "--chambers", folder, "--mp4", os.path.join(OUT, pid + ".mp4"), "--work",
            os.path.join(OUT, "work-" + pid), "--title", info["name"], "--about", info.get("about", "")]
        if info.get("ending"):
            cmd += ["--ending", info["ending"].replace("[Subject-Name-here]", "test subject")]
        subprocess.run(cmd, check=True)
        shutil.rmtree(os.path.join(OUT, "work-" + pid, "shots"), ignore_errors=True)


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
