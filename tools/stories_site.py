#!/usr/bin/env python3
"""The story packs as a web page's data: what scene.rs/stories serves.

    tools/stories_site.py OUT_DIR

For each story pack (`make dlc-zip` first: the card folders in build/dlc/
and their zips), OUT_DIR/packs/ gets
    <pack>.zip    to unzip into /sd/portals/dlc/ by hand
    <pack>.json   every file of the card folder, base64, for the page to
                  write onto a badge over WebUSB (BadgeLink)
    <pack>.png    a still from the pack's film, if build/dlc has one
and OUT_DIR/index.json lists them -- name, author, blurb, music, rounds,
sizes -- with a hash of each bundle, so a page (or, one day, the game)
can tell what changed. The game reads packs from /sd/portals/dlc/<pack>/
(main/pack.h); dlc/README.md says what is in one.
"""

import base64
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARD = os.path.join(ROOT, "build", "dlc")
# A still for each pack: its film, and how far in (a chamber in play).
FILMS = {
    "hackfest": ("hackfest-story-badge-henk.mp4", 52.0),
    "human-in-the-loop": ("human-in-the-loop-story-badge.mp4", 60.0),
    "after-hours": ("after-hours.mp4", 37.0),
}


def pack_info(folder):
    info, rounds = {}, 0
    with open(os.path.join(folder, "pack.txt")) as f:
        for line in f:
            key, _, value = line.strip().partition(":")
            if key == "round":
                rounds += 1
            elif key and key not in info:
                info[key] = value.strip()
    info["rounds"] = rounds
    return info


def bundle(folder):
    files = []
    for dirpath, _, names in os.walk(folder):
        for n in sorted(names):
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, folder).replace(os.sep, "/")
            with open(full, "rb") as f:
                data = f.read()
            files.append({"path": rel, "size": len(data), "data": base64.b64encode(data).decode("ascii")})
    files.sort(key=lambda f: (f["path"].count("/"), f["path"]))
    return files


def still(pid, out):
    film = FILMS.get(pid)
    if film is None or not os.path.exists(os.path.join(CARD, film[0])):
        return False
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-ss", str(film[1]), "-i", os.path.join(CARD, film[0]),
                    "-frames:v", "1", "-vf", "scale=400:240:flags=area", out], check=True)
    return True


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = os.path.abspath(sys.argv[1])
    packs_dir = os.path.join(out, "packs")
    os.makedirs(packs_dir, exist_ok=True)
    index = []
    for pid in sorted(os.listdir(CARD)):
        folder = os.path.join(CARD, pid)
        if not os.path.isfile(os.path.join(folder, "pack.txt")):
            continue
        zipped = os.path.join(CARD, pid + ".zip")
        if not os.path.exists(zipped):
            sys.exit("%s: no zip -- make dlc-zip first" % pid)
        info = pack_info(folder)
        files = bundle(folder)
        body = json.dumps({"id": pid, "files": files}, separators=(",", ":"))
        with open(os.path.join(packs_dir, pid + ".json"), "w") as f:
            f.write(body)
        shutil.copy(zipped, os.path.join(packs_dir, pid + ".zip"))
        has_still = still(pid, os.path.join(packs_dir, pid + ".png"))
        index.append({
            "id": pid,
            "name": info.get("name", pid),
            "author": info.get("author", ""),
            "about": info.get("about", ""),
            "music": info.get("music", "").split(" ")[0] or "calm",
            "desk": info.get("frame") == "desk",
            "rounds": info["rounds"],
            # The chambers: the folder's own chamber files.
            "chambers": sum(1 for f in files if "/" not in f["path"] and f["path"].endswith(".txt")
                            and f["path"] not in ("pack.txt", "needs-update.txt")),
            "files": len(files),
            "bytes": sum(f["size"] for f in files),
            "zip": "packs/%s.zip" % pid,
            "bundle": "packs/%s.json" % pid,
            "still": "packs/%s.png" % pid if has_still else None,
            "sha256": hashlib.sha256(body.encode()).hexdigest(),
        })
        print("%-20s %2d files %7d bytes%s" % (pid, len(files), index[-1]["bytes"], "  + still" if has_still else ""))
    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"game": "Portals", "install_to": "/sd/portals/dlc", "packs": index}, f, indent=1)
        f.write("\n")
    print("%d packs: %s" % (len(index), os.path.join(out, "index.json")))


if __name__ == "__main__":
    main()
