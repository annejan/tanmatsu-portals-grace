#!/usr/bin/env python3
"""The story packs in dlc/: each a folder, dlc/<pack>/, of chamber files with
their solutions (spoilers) and a pack.txt giving its name, author, blurb,
ending and the chambers' order (main/pack.h). On the badge's card a pack is
the same folder, /sd/portals/dlc/<pack>/, without the solutions; their
solutions go as one recording each, to watch (Esc -> Watch a recording).

    tools/dlc.py check            every chamber solved, at 20-35 frames a second (tests/host_tas.c);
                                  a desk story's rounds judged as the game judges them (tests/host_review.c)
    tools/dlc.py card             build/dlc/<pack>/ for the card, its solutions as replays
    tools/dlc.py zip              build/dlc/<pack>.zip: that folder, to unzip into /sd/portals/dlc/
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
    main/pack.c reads it: a desk story's "round:" lines, else "chamber:"
    lines, else the files by name)."""
    info, order, rounds = {"name": os.path.basename(folder)}, [], []
    path = os.path.join(folder, "pack.txt")
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                key, _, value = line.strip().partition(":")
                if key == "chamber":
                    order.append(value.strip())
                elif key == "round":
                    rounds.append(value.strip())
                elif key in ("name", "author", "about", "ending", "frame", "outro"):
                    info[key] = value.strip()
    if info.get("frame") == "desk" and rounds:
        return info, rounds
    if not order:
        order = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(folder, "*.txt"))
                       if os.path.basename(p) != "pack.txt")
    return info, order


def sections(text):
    """A chamber file's routes: {"solution": steps, "cheese a": steps, ...}."""
    out, name = {}, None
    for line in text.splitlines(keepends=True):
        s = line.strip()
        if s == "solution" or (s.startswith("cheese ") and len(s) == 8):
            name = s
            out[name] = ""
        elif name is not None:
            out[name] += line
    return out


def split(path):
    """A chamber file's chamber, and its solution's steps (or "")."""
    with open(path) as f:
        text = f.read()
    cut = text.find("\nsolution")
    if cut < 0:
        return text, ""
    return text[:cut + 1], sections(text).get("solution", "")


def review_of(path):
    """A round's review keys: its kind, and its flaws' letters."""
    kind, flaws = None, []
    with open(path) as f:
        for line in f:
            s = line.strip()
            if s.startswith("layer") or s == "solution":
                break
            key, _, value = s.partition(":")
            if key.strip() == "review":
                kind = value.strip()
            elif key.strip() == "flaw":
                flaws.append(value.strip()[:1])
    return kind, flaws


def judge(path, seed, patch="", repair=False, routes=()):
    """host_review's verdicts for `path`'s routes: {route: (time or "FAIL", verdict)}."""
    cmd = [os.path.join(ROOT, "build", "host_review"), "-jitter", str(seed)]
    if patch:
        cmd += ["-patch", patch]
    if repair:
        cmd += ["-repair"]
    out = subprocess.run(cmd + [path] + list(routes), capture_output=True, text=True)
    if out.stderr.strip():
        sys.exit(out.stderr.strip())
    got = {}
    for line in out.stdout.splitlines():
        f = line.split("\t")
        got[f[0]] = ("FAIL" if f[1].startswith("FAIL") else f[1], f[2])
    return got


def check_round(path):
    """A round, judged as the game would, at a dozen frame rates: what is
    wrong with it, or [] if nothing. Flawed: the solution is the meant way,
    each flaw's cheese finds that flaw, its fix closes it, and the meant way
    survives every fix. Final: the meant way. Broken: unsolvable until the
    debugger's repairs, then solved."""
    kind, flaws = review_of(path)
    routes = sections(open(path).read())
    bad = []
    for seed in range(1, 13):
        if kind in (None, "final", "flawed"):
            got = judge(path, seed, routes=["solution"])["solution"]
            want = "-" if kind is None else "intended"
            if got[0] == "FAIL" or got[1] != want:
                bad.append("seed %d: solution %s %s" % (seed, got[0], got[1]))
        if kind == "flawed":
            for x in flaws:
                if "cheese " + x not in routes:
                    bad.append("flaw %s: no cheese %s route" % (x, x))
                    continue
                got = judge(path, seed, routes=["cheese " + x])["cheese " + x]
                if got[0] == "FAIL" or x not in got[1].split()[1:]:
                    bad.append("seed %d: cheese %s %s %s" % (seed, x, got[0], got[1]))
                got = judge(path, seed, patch=x, routes=["cheese " + x])["cheese " + x]
                if got[0] != "FAIL" and x in got[1].split()[1:]:
                    bad.append("seed %d: flaw %s's fix leaves cheese %s working" % (seed, x, x))
            got = judge(path, seed, patch="".join(flaws), routes=["solution"])["solution"]
            if got[0] == "FAIL" or got[1] != "intended":
                bad.append("seed %d: all fixed, solution %s %s" % (seed, got[0], got[1]))
        if kind == "broken":
            if judge(path, seed, routes=["solution"])["solution"][0] != "FAIL":
                bad.append("seed %d: solved before its repair" % seed)
            if judge(path, seed, repair=True, routes=["solution"])["solution"][0] == "FAIL":
                bad.append("seed %d: not solved after its repair" % seed)
        if bad:
            break
    return kind or "chamber", bad


# What a build that does not know desk stories plays instead (pack.h).
NEEDS_UPDATE = """name: Needs an update
hint: This story needs a newer Portals: update it from the app repository.
story: This story is told from a desk your version of Portals does not have. Update Portals, then come back.
size: 7 5 7
facing: north

layer 0
#######
#WWWWW#
#WWWWW#
#WWWWW#
#WWWWW#
#WWWWW#
#######

layer 1
WWWWWWW
W.....W
W.....W
W..S..W
W.....W
W.....W
WWWWWWW

layer 2
WWWWWWW
W.....W
W.....W
W.....W
W.....W
W.....W
WWWWWWW

layer 3
WWWWWWW
W.....W
W.....W
W.....W
W.....W
W.....W
WWWWWWW
"""


def check():
    """Each chamber solved at random 20-35 fps frames, a dozen times."""
    tool = os.path.join(ROOT, "build", "host_tas")
    bad = []
    for folder in packs():
        info, order = pack_info(folder)
        print("%s (%s)" % (info["name"], os.path.basename(folder)))
        env = dict(os.environ, PORTALS_CHAMBERS=folder)
        if info.get("frame") == "desk":
            out = subprocess.run([os.path.join(ROOT, "build", "host_review"), "desk", folder], capture_output=True,
                                 text=True)
            print("  %-22s %-8s %s" % ("desk/", "desk", out.stdout.strip() if out.returncode == 0 else
                                       "FAIL: " + out.stderr.strip()))
            if out.returncode != 0:
                bad.append("%s/desk" % os.path.basename(folder))
            for cid in order + ([info["outro"]] if info.get("outro") else []):
                kind, why = check_round(os.path.join(folder, cid + ".txt"))
                print("  %-22s %-8s %s" % (cid, kind, "ok" if not why else "FAIL: " + "; ".join(why)))
                if why:
                    bad.append("%s/%s" % (os.path.basename(folder), cid))
            continue
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
        desk = info.get("frame") == "desk"
        if desk:
            # Older builds know only "chamber:" lines: theirs is a room that
            # says to update.
            with open(os.path.join(out, "pack.txt")) as f:
                text = f.read()
            if "chamber: needs-update" not in text:
                with open(os.path.join(out, "pack.txt"), "a") as f:
                    f.write("%schamber: needs-update\n" % ("" if text.endswith("\n") else "\n"))
            with open(os.path.join(out, "needs-update.txt"), "w") as f:
                f.write(NEEDS_UPDATE)
            if os.path.isdir(os.path.join(folder, "desk")):
                shutil.copytree(os.path.join(folder, "desk"), os.path.join(out, "desk"))
        runs, cheeses = [], []
        for cid in order + ([info["outro"]] if desk and info.get("outro") else []):
            path = os.path.join(folder, cid + ".txt")
            chamber, steps = split(path)
            with open(os.path.join(out, cid + ".txt"), "w") as f:
                f.write(chamber)
            if steps.strip():
                runs.append("\nchamber: %s\n%s" % (cid, steps))
            for name, route in sorted(sections(open(path).read()).items()):
                if name.startswith("cheese ") and route.strip():
                    cheeses.append("\nchamber: %s\n// %s\n%s" % (cid, name, route))
        # Its replays travel with it: in its own replays/ (main/pack.h), the
        # chambers named by their files alone.
        os.makedirs(os.path.join(out, "replays"))
        with open(os.path.join(out, "replays", "solutions.txt"), "w") as f:
            f.write("name: the solutions, Portals %s\nversion: %s %s\n" % (version, version, build_id))
            f.write("".join(runs))
        if cheeses:
            # The flaws as found: each route on the draft, unpatched.
            with open(os.path.join(out, "replays", "cheeses.txt"), "w") as f:
                f.write("name: the cheeses, Portals %s\nversion: %s %s\n" % (version, version, build_id))
                f.write("".join(cheeses))
        print("%s: %s, %d %s, and replays" % (out, info["name"], len(order), "rounds" if desk else "chambers"))


def zips():
    """Each card folder as a zip, its folder at the top: unzip it into
    /sd/portals/dlc/ (a GitHub release carries these)."""
    for folder in packs():
        pid = os.path.basename(folder)
        base = os.path.join(OUT, pid)
        if not os.path.isdir(base):
            sys.exit("%s: run tools/dlc.py card first" % base)
        made = shutil.make_archive(base, "zip", root_dir=OUT, base_dir=pid)
        print(made)


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
    elif what == "zip":
        zips()
    elif what == "films":
        films(sys.argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
