#!/usr/bin/env python3
"""Ghost races (main/ghost.h): swap ghosts between badges, through this PC.

    tools/ghosts.py fetch [DIR]        a badge's own ghosts (/sd/portals/ghosts/*.txt) to DIR (build/ghosts/mine)
    tools/ghosts.py push DIR NAME      DIR's ghosts onto a badge as rival NAME (/sd/portals/ghosts/rivals/NAME/)

The badge in BadgeLink mode (lsusb: 16d0:0f9a). `make ghosts-fetch` and
`make ghosts-push FROM=DIR NAME=nick` run these. A ghost only races on the
same release of Portals, in the chamber as it was: fetch from and push to
badges running the same version.
"""

import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BL = os.path.join(ROOT, "badgelink", "tools", "badgelink.sh")
GHOSTS = "/sd/portals/ghosts"


def run(*args):
    out = subprocess.run([BL] + list(args), capture_output=True, text=True, cwd=os.path.dirname(BL))
    # badgelink.py says what went wrong on stdout, not stderr.
    return out.returncode, out.stdout, (out.stdout + out.stderr).strip()


def badgelink(*args, tries=5, missing_ok=False):
    """A badgelink command, tried a few times (it times out now and then) --
    but not again on an answer that will not change. None for a path that
    is not there, with missing_ok."""
    msg = ""
    for _ in range(tries):
        code, stdout, msg = run(*args)
        if code == 0:
            return stdout
        low = msg.lower()
        if "badge not found" not in low and "not found" in low:
            if missing_ok:
                return None
            break
        time.sleep(2)
    sys.exit("badgelink %s: %s" % (" ".join(args), msg[-300:]))


def mkdir(d, tries=5):
    """Made until it lists: a mkdir that timed out would leave nothing to
    upload into."""
    for _ in range(tries):
        run("fs", "mkdir", d)
        if run("fs", "list", d)[0] == 0:
            return
        time.sleep(2)
    sys.exit("could not make %s on the badge (badgelink timed out?)" % d)


def fetch(dest):
    dest = os.path.abspath(dest)
    listing = badgelink("fs", "list", GHOSTS, missing_ok=True)
    if listing is None:
        print("0 ghosts: this badge has none yet (race a chamber with Ghost races on first)")
        return
    files = re.findall(r"^file\s*\|\s*(\S+\.txt)", listing, re.M)
    # Into a fresh folder, swapped in once it is all there: an earlier
    # badge's ghosts must not stay and be pushed under another name.
    tmp = tempfile.mkdtemp(prefix=".ghosts-", dir=os.path.dirname(dest) or ".")
    for f in files:
        badgelink("fs", "download", GHOSTS + "/" + f, os.path.join(tmp, f))
        print("  " + f)
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    os.rename(tmp, dest)
    print("%d ghosts in %s" % (len(files), dest))


def push(src, name):
    src = os.path.abspath(src)
    # A folder name the badge lists and FAT keeps apart: no leading dot (the
    # badge skips those), no trailing one (FAT drops it), at most 23.
    if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9_.-]{0,21}[A-Za-z0-9_-])?", name):
        sys.exit("NAME: letters, digits, _ . - (not first or last a dot), at most 23")
    files = sorted(glob.glob(os.path.join(src, "*.txt")))
    if not files:
        sys.exit("no ghosts in " + src)
    mkdir(GHOSTS)
    mkdir(GHOSTS + "/rivals")
    # FAT does not tell Bob from bob: one rival under two names would merge.
    listing = badgelink("fs", "list", GHOSTS + "/rivals") or ""
    for other in re.findall(r"^dir\s*\|\s*(\S+)", listing, re.M):
        if other.lower() == name.lower() and other != name:
            sys.exit("rival %r is already on the badge: FAT ignores case, use that name" % other)
    mkdir(GHOSTS + "/rivals/" + name)
    for f in files:
        badgelink("fs", "upload", GHOSTS + "/rivals/%s/%s" % (name, os.path.basename(f)), f)
        print("  " + os.path.basename(f))
    print("%d ghosts of %s's on the badge" % (len(files), name))


def main():
    a = sys.argv[1:]
    if a and a[0] == "fetch":
        fetch(a[1] if len(a) > 1 else os.path.join(ROOT, "build", "ghosts", "mine"))
    elif len(a) == 3 and a[0] == "push":
        push(a[1], a[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
