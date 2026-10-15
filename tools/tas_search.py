#!/usr/bin/env python3
"""Finding and checking the TAS routes in tas/: what the routes were made with.

    tools/tas_search.py eval CID ROUTE [--seeds A B] [--fps LO HI] [--rec | --badge FILES] [--first S] [--hitch P S]
    tools/tas_search.py search CID START OUT ITERS RNGSEED [--ban-cube] [--fps LO HI] [--rec | --badge FILES]
    tools/tas_search.py robust [CID...] [--seeds N] [--first S] [--hitch P S]

A route is a solution script (chambers/README.md), one step per line, for
chamber CID (chambers/CID.txt). It is played by build/host_tas (make
host_tas) as the badge plays it: a step a frame, each frame its own length.

eval    the route at fixed 20, 25, 30 and 35 frames a second (and exactly 1/30
        and 1/35 s: a rounded step once hid a failure), and on uneven frames
        of 20 to 35 a second for each seed (training seeds 1..40 unless
        given). Prints JSON: each fixed time, the failures, the mean on the
        uneven frames. A route is good with 0 failures; faster is a lower
        mean. Keep a few hundred seeds no search has seen for the last word:
        --seeds 1000 1199.
search  random search from START: a step dropped, nudged or a jump put in,
        kept only if it scores better -- (failures, mean), failures first.
        --fps LO HI searches for one badge's band of frame rates (its fixed
        rates in the band only): a route for that badge, not the TAS.
        --rec scores as Watch plays a route (build/host_tasrec: inputs
        rounded, each shot its own frame) on uneven frames in --fps's band
        (120 of them in a search): routes found without it can fail there.
        --badge FILE[:FILE...] scores the badge's way instead (eval too):
        played as Watch plays it (build/host_tasrec, make host_tasrec), on
        the frames of runs kept on that badge -- the first one's in order,
        and --seeds' count drawn at random from them all.
        800-1500 iterations take a few minutes. --ban-cube keeps the cube
        out of it.
robust  every tas/ route (or those named) on N uneven runs (200 unless
        given), at the fixed rates too: what a route must survive on the
        badge (tas/README.md).

--first S makes the first frame S seconds long, --hitch P S makes each
later frame S seconds long one time in 1/P: the badge's long frames
(host_tas). A route need not survive them -- the badge times a chamber's
first frame from its load -- but they show how fragile it is.
"""

import json
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "build", "host_tas")
RATES = ["0.05", "0.04", "0.033333", "0.028571", repr(1 / 30), repr(1 / 35)]
TRAIN = list(range(1, 41))


def chamber_map(cid):
    """CID's map: chambers/CID.txt, or a story pack's chamber, "pack/file" (dlc/pack/file.txt)."""
    path = os.path.join(ROOT, "dlc" if "/" in cid else "chambers", cid + ".txt")
    with open(path) as f:
        t = f.read()
    return t[:t.find("\nsolution") + 1]


def run(cid, steps, args, work):
    """One play of the route: its time to the exit, or None."""
    with open(os.path.join(work, "tas-x.txt"), "w") as f:
        f.write(chamber_map(cid) + "\nsolution\n" + "\n".join(steps) + "\n")
    out = subprocess.run([TOOL] + args + ["tas-x"], env=dict(os.environ, PORTALS_CHAMBERS=work),
                         capture_output=True, text=True).stdout
    v = out.split("\t")[1].strip() if "\t" in out else "FAIL (parse)"
    return None if v.startswith("FAIL") else float(v)


def evaluate(cid, steps, seeds=TRAIN, fps=(20, 35), extra=(), work=None):
    if work is None:
        work = tempfile.mkdtemp(prefix="tas-")
    # The fixed rates in the band only (all of them for the badge's 20 to 35).
    fixed = {r: run(cid, steps, ["-dt", r], work) for r in RATES if fps[0] - 0.01 <= 1 / float(r) <= fps[1] + 0.01}
    js = [run(cid, steps, ["-jitter", str(s), str(fps[0]), str(fps[1])] + list(extra), work) for s in seeds]
    ok = [x for x in js if x is not None]
    return {"fixed": fixed, "fails": sum(v is None for v in fixed.values()) + sum(x is None for x in js),
            "jitter_fails": sum(x is None for x in js), "mean": round(sum(ok) / len(ok), 3) if ok else None,
            "n": len(js)}


RECTOOL = os.path.join(ROOT, "build", "host_tasrec")


def run_badge(cid, steps, frames, seed, copies, work):
    """The route played as the badge plays it (host_tasrec): on the frames of the runs kept on a badge
    (FILE:FILE...), drawn at random from the chamber's own (seed not 0; copies plays, one after the
    other) or in the first run's order (seed 0). Each play's time to the exit, or None."""
    name = cid.replace("/", "-")
    with open(os.path.join(work, "tas-%s.txt" % name), "w") as f:
        f.write(chamber_map(cid) + "\nsolution\n" + "\n".join(steps) + "\n")
    out = subprocess.run([RECTOOL, str(seed), os.devnull] + ["tas-" + name] * copies,
                         env=dict(os.environ, PORTALS_CHAMBERS=work, **({"TASREC_FRAMES": frames} if frames else {})),
                         capture_output=True, text=True).stdout
    got = [l.split("\t")[1].strip() for l in out.splitlines() if "\t" in l]
    return [None if v.startswith("FAIL") else float(v) for v in got] + [None] * (copies - len(got))


def evaluate_rec(cid, steps, fps=(20, 35), seeds=TRAIN, work=None):
    """eval, played as Watch plays it (host_tasrec: inputs rounded as a recording keeps them, each act
    a frame of its own) on uneven frames in the band: what a route must survive on the badge."""
    if work is None:
        work = tempfile.mkdtemp(prefix="tas-")
    old = os.environ.get("TASREC_FPS")
    os.environ["TASREC_FPS"] = "%g %g" % fps
    try:
        js = run_badge(cid, steps, "", seeds[0], len(seeds), work)
    finally:
        if old is None:
            del os.environ["TASREC_FPS"]
        else:
            os.environ["TASREC_FPS"] = old
    ok = [x for x in js if x is not None]
    return {"fails": sum(x is None for x in js), "jitter_fails": sum(x is None for x in js),
            "mean": round(sum(ok) / len(ok), 4) if ok else None, "n": len(js)}


def evaluate_badge(cid, steps, frames, seeds=TRAIN, work=None):
    """eval, the badge's way: the first kept run's own frames, and len(seeds) plays on frames drawn
    from the kept runs' (seeds[0] starts the draw)."""
    if work is None:
        work = tempfile.mkdtemp(prefix="tas-")
    order = run_badge(cid, steps, frames, 0, 1, work)[0]
    js = run_badge(cid, steps, frames, seeds[0], len(seeds), work)
    ok = [x for x in js if x is not None]
    return {"order": order, "fails": (order is None) + sum(x is None for x in js),
            "jitter_fails": sum(x is None for x in js), "mean": round(sum(ok) / len(ok), 4) if ok else None,
            "n": len(js)}


def lines(path):
    with open(path) as f:
        return [l.strip() for l in f.read().splitlines() if l.strip() and not l.strip().startswith("//")]


def mutate(s, rng):
    s = list(s)
    i = rng.randrange(len(s))
    w = s[i].split()
    op = rng.random()
    if op < 0.25 and len(s) > 1:
        del s[i]
    elif op < 0.33:
        s.insert(i, "jump")
    elif op < 0.92:
        nums = [k for k, x in enumerate(w) if k > 0 and x.replace(".", "", 1).replace("-", "", 1).isdigit()]
        if not nums:
            return s
        k = rng.choice(nums)
        if w[0] == "wait":
            w[k] = "%.2f" % max(0.0, float(w[k]) * rng.choice([0.0, 0.5, 0.8, 1.2]))
        elif w[0] == "face":
            w[k] = "%.1f" % (float(w[k]) + rng.gauss(0, 8))
        else:
            w[k] = "%.2f" % (float(w[k]) + rng.gauss(0, rng.choice([0.05, 0.2, 0.6])))
        s[i] = " ".join(w)
    elif w[0] == "walk_to" and i + 1 < len(s) and s[i + 1].startswith("walk_to"):
        del s[i]
    return s


def search(cid, start, out, iters, seed, ban_cube=False, fps=(20, 35), badge=None, rec=False):
    rng = random.Random(seed)
    work = tempfile.mkdtemp(prefix="tas-")

    def score(st):
        if badge:
            e = evaluate_badge(cid, st, badge, list(range(1, 101)), work)
        elif rec:
            e = evaluate_rec(cid, st, fps, list(range(1, 121)), work)
        else:
            e = evaluate(cid, st, fps=fps, work=work)
        return (e["fails"], e["mean"] if e["mean"] is not None else 1e9)

    steps = start
    best = first = score(steps)
    for _ in range(iters):
        c = mutate(steps, rng)
        if ban_cube and any(l.startswith("grab") or l == "use" for l in c):
            continue
        sc = score(c)
        if sc < best:
            best, steps = sc, c
    with open(out, "w") as f:
        f.write("\n".join(steps) + "\n")
    print(json.dumps({"start": first, "best": best, "out": out}))


def extra_args(a):
    """--first S and --hitch P S, as host_tas takes them."""
    out = []
    if "--first" in a:
        i = a.index("--first")
        out += ["-first", a[i + 1]]
    if "--hitch" in a:
        i = a.index("--hitch")
        out += ["-hitch", a[i + 1], a[i + 2]]
    return out


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if a[0] == "eval" and len(a) >= 3:
        seeds, fps = TRAIN, (20, 35)
        if "--seeds" in a:
            i = a.index("--seeds")
            seeds = list(range(int(a[i + 1]), int(a[i + 2]) + 1))
        if "--fps" in a:
            i = a.index("--fps")
            fps = (float(a[i + 1]), float(a[i + 2]))
        if "--badge" in a:
            print(json.dumps(evaluate_badge(a[1], lines(a[2]), a[a.index("--badge") + 1], seeds)))
        elif "--rec" in a:
            print(json.dumps(evaluate_rec(a[1], lines(a[2]), fps, seeds)))
        else:
            print(json.dumps(evaluate(a[1], lines(a[2]), seeds, fps, extra_args(a))))
    elif a[0] == "search" and len(a) >= 6:
        fps = (20, 35)
        if "--fps" in a:
            i = a.index("--fps")
            fps = (float(a[i + 1]), float(a[i + 2]))
        badge = a[a.index("--badge") + 1] if "--badge" in a else None
        search(a[1], lines(a[2]), a[3], int(a[4]), int(a[5]), "--ban-cube" in a, fps, badge, "--rec" in a)
    elif a[0] == "robust":
        n = 200
        if "--seeds" in a:
            n = int(a[a.index("--seeds") + 1])
        named = [x for x in a[1:] if not x.startswith("-") and not x.replace(".", "", 1).isdigit()]
        cids = named or sorted(f[:-4] for f in os.listdir(os.path.join(ROOT, "tas")) if f[:2].isdigit())
        total = 0.0
        print("%-24s %5s %8s  %s" % ("chamber", "fails", "mean", "fixed 20 25 30 35 (exact 30 35)"))
        for cid in cids:
            e = evaluate(cid, lines(os.path.join(ROOT, "tas", cid + ".txt")), range(1000, 1000 + n),
                         extra=extra_args(a))
            total += e["mean"] or 0.0
            fixed = " ".join("%.2f" % v if v is not None else "FAIL" for v in e["fixed"].values())
            print("%-24s %2d/%-3d %8.3f  %s" % (cid, e["jitter_fails"], n, e["mean"] or -1, fixed))
        print("%-24s %14.2f  (the mean of %d uneven runs each: what predicts the badge)" % ("total", total, n))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
