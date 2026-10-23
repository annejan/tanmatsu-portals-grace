#!/usr/bin/env python3
"""The story packs as a web page's data: what scene.rs/stories serves.

    tools/stories_site.py OUT_DIR

For each story pack (`make dlc-zip` first: the card folders in build/dlc/
and their zips), OUT_DIR/packs/ gets
    <pack>.zip    to unzip into /sd/portals/dlc/ by hand
    <pack>.json   every file of the card folder, base64, for the page to
                  write onto a badge over WebUSB (BadgeLink)
    <pack>.jpg    a still from the pack's film, if build/dlc has one
    <pack>.mp4    a few silent seconds of that film, to loop on its card
    <pack>/       the card folder's files as they are, with files.txt
                  listing them (`<bytes>\t<path>` a line): what the Portals
                  Stories launcher plugin fetches over WiFi
plus trailer.mp4 and trailer.jpg, a silent montage of the films for the
page's top, and OUT_DIR/index.json lists the packs -- name, author, blurb,
music, rounds, its chambers in order, sizes, when it last changed -- with a
hash of each bundle, so a page (or, one day, the game) can tell what
changed. OUT_DIR/index.txt is that list for the plugin, plain text: a
pack a line, tab-separated -- id, sha256, bytes, files, name, about. The
game reads packs from /sd/portals/dlc/<pack>/
(main/pack.h); dlc/README.md says what is in one.
"""

import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARD = os.path.join(ROOT, "build", "dlc")
# Each pack's film: how far in its still is, and where its looping clip
# starts (a chamber in play, both).
FILMS = {
    "hackfest": ("hackfest-story-badge-henk.mp4", 116.0, 113.0),
    "human-in-the-loop": ("human-in-the-loop-story-badge.mp4", 366.0, 372.0),
    "after-hours": ("after-hours-badge.mp4", 286.0, 388.0),
}
CLIP = 6.0
# The trailer: (film, start) shots of TRAILER_SHOT seconds each, in order.
TRAILER = [
    ("after-hours-badge.mp4", 46.0),
    ("after-hours-badge.mp4", 285.0),
    ("hackfest-story-badge-henk.mp4", 114.0),
    ("after-hours-badge.mp4", 209.0),
    ("human-in-the-loop-story-badge.mp4", 228.0),
    ("after-hours-badge.mp4", 318.0),
    ("after-hours-badge.mp4", 390.0),
]
TRAILER_SHOT = 3.5
FFMPEG = ["ffmpeg", "-y", "-loglevel", "error"]
# Small, silent and quick to start in a browser.
H264 = ["-an", "-c:v", "libx264", "-preset", "slow", "-pix_fmt", "yuv420p", "-movflags", "+faststart"]


def chamber_head(path):
    """A chamber file's name and hint."""
    head = {}
    with open(path) as f:
        for line in f:
            key, _, value = line.strip().partition(":")
            if key in ("name", "hint") and key not in head:
                head[key] = value.strip()
            if key == "size":
                break
    return head


def pack_info(folder):
    info, rounds, contents = {}, 0, []
    with open(os.path.join(folder, "pack.txt")) as f:
        for line in f:
            key, _, value = line.strip().partition(":")
            value = value.strip()
            if key == "round":
                rounds += 1
            if key in ("chamber", "round") and value != "needs-update":
                head = chamber_head(os.path.join(folder, value + ".txt"))
                contents.append({"name": head.get("name", value), "hint": head.get("hint", "")})
            elif key == "route":
                # A desk story's fork: the rounds after it are that route's.
                label = value.split("|")
                contents.append({"route": label[1].strip() if len(label) > 1 else value})
            elif key == "join":
                contents.append({"route": None})
            elif key and key not in info:
                info[key] = value
    info["rounds"] = rounds
    info["contents"] = contents
    return info


def updated(pid):
    """When the pack's sources last changed, from git: an ISO 8601 time, or None."""
    r = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%cI", "--", "dlc/" + pid],
                       capture_output=True, text=True)
    return r.stdout.strip() or None


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
    subprocess.run(FFMPEG + ["-ss", str(film[1]), "-i", os.path.join(CARD, film[0]),
                             "-frames:v", "1", "-vf", "scale=800:480:flags=area", "-q:v", "3", out], check=True)
    return True


def clip(pid, out):
    film = FILMS.get(pid)
    if film is None or not os.path.exists(os.path.join(CARD, film[0])):
        return False
    subprocess.run(FFMPEG + ["-ss", str(film[2]), "-t", str(CLIP), "-i", os.path.join(CARD, film[0]),
                             "-vf", "scale=800:480:flags=area,fps=30"] + H264 + ["-crf", "30", out], check=True)
    return True


def trailer(out_dir):
    """The page's top: a montage of the films, and its first frame for a poster."""
    shots = [(f, t) for f, t in TRAILER if os.path.exists(os.path.join(CARD, f))]
    if not shots:
        return False
    args, chains = [], []
    for k, (film, start) in enumerate(shots):
        args += ["-ss", str(start), "-t", str(TRAILER_SHOT), "-i", os.path.join(CARD, film)]
        # Without the HUD: the chamber's name at the top, its story at the bottom.
        chains.append("[%d:v]crop=iw*0.8:ih*0.74:iw*0.1:ih*0.1,scale=1280:720:flags=area,fps=30,setsar=1[v%d]"
                      % (k, k))
    graph = ";".join(chains) + ";" + "".join("[v%d]" % k for k in range(len(shots))) + \
        "concat=n=%d:v=1:a=0[out]" % len(shots)
    mp4 = os.path.join(out_dir, "trailer.mp4")
    subprocess.run(FFMPEG + args + ["-filter_complex", graph, "-map", "[out]"] + H264 + ["-crf", "32", mp4],
                   check=True)
    subprocess.run(FFMPEG + ["-i", mp4, "-frames:v", "1", "-q:v", "4", os.path.join(out_dir, "trailer.jpg")],
                   check=True)
    return True


def one_line(text):
    return re.sub(r"\s+", " ", text or "").strip()


# The paths the plugin takes: no spaces, no "..", nothing to quote in a URL.
SAFE_PATH = re.compile(r"^(?!/)(?!.*\.\.)[A-Za-z0-9._/-]+$")


def raw(folder, files, out):
    """The card folder's files as they are, under `out`, with files.txt. The
    plugin reads each as text and checks its length, so a NUL in one would
    read short: refused here."""
    if os.path.isdir(out):
        shutil.rmtree(out)
    lines = []
    for f in files:
        if not SAFE_PATH.match(f["path"]):
            sys.exit("%s: %s -- a path the plugin will not take" % (folder, f["path"]))
        data = base64.b64decode(f["data"])
        if b"\0" in data:
            sys.exit("%s: %s has a NUL byte in it" % (folder, f["path"]))
        dest = os.path.join(out, f["path"])
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as g:
            g.write(data)
        lines.append("%d\t%s\n" % (len(data), f["path"]))
    with open(os.path.join(out, "files.txt"), "w") as g:
        g.write("".join(lines))


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
        raw(folder, files, os.path.join(packs_dir, pid))
        has_still = still(pid, os.path.join(packs_dir, pid + ".jpg"))
        has_clip = clip(pid, os.path.join(packs_dir, pid + ".mp4"))
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
            "still": "packs/%s.jpg" % pid if has_still else None,
            "clip": "packs/%s.mp4" % pid if has_clip else None,
            "contents": info["contents"],
            "updated": updated(pid),
            "sha256": hashlib.sha256(body.encode()).hexdigest(),
        })
        print("%-20s %2d files %7d bytes%s" % (pid, len(files), index[-1]["bytes"], "  + still" if has_still else ""))
    # The newest pack first: what a visitor is most likely here for.
    index.sort(key=lambda p: p["updated"] or "", reverse=True)
    has_trailer = trailer(packs_dir)
    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"game": "Portals", "install_to": "/sd/portals/dlc",
                   "trailer": "packs/trailer.mp4" if has_trailer else None,
                   "poster": "packs/trailer.jpg" if has_trailer else None,
                   "packs": index}, f, indent=1)
        f.write("\n")
    with open(os.path.join(out, "index.txt"), "w") as f:
        f.write("# Portals stories, for the Portals Stories launcher plugin. Tab-separated:\n"
                "# id, sha256, bytes, files, name, about. Each pack's files: packs/<id>/files.txt\n")
        for p in index:
            f.write("\t".join([p["id"], p["sha256"], str(p["bytes"]), str(p["files"]),
                                one_line(p["name"]), one_line(p["about"])]) + "\n")
    print("%d packs: %s" % (len(index), os.path.join(out, "index.json")))


if __name__ == "__main__":
    main()
