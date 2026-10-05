#!/usr/bin/env python3
"""Chambers' solutions as a film: frames from the game's own renderer with
its textures, its own sound, the HUD's story line, a title card for each
chamber and an end card. Several chambers make one film, the music playing
on from one to the next.

    tools/make_movie.py DEMO [DEMO...] [--mp4 OUT.mp4] [--gif OUT.gif] [--chambers DIR] [--work DIR]

A DEMO is a demo name (a chamber file's id, such as 12-redirection); a
chamber from DIR, as the badge reads them from the SD card, needs its
solution in the file. Needs Pillow and ffmpeg. `make movie` builds the
recorder (tests/host_movie.c) first.
"""

import argparse
import json
import glob
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

FPS = 10
SCALE = 2  # 800 x 480 drawn at 1600 x 960, so the text stays sharp
STORY_W, STORY_LINES = 70, 3  # as main.c lays the story line out
FADE_S = 0.6  # cards and chambers fade in and out this fast
YELLOW, RED = (0xFF, 0xE0, 0x8A), (0xFF, 0x7A, 0x6A)


def font(names, size):
    for n in names:
        try:
            return ImageFont.truetype(n, size)
        except OSError:
            pass
    return ImageFont.load_default()


def story_lines(story, shown):
    """The story typed out to `shown` letters, in lines of whole words."""
    lines, at = [], 0
    while at < shown and len(lines) < STORY_LINES:
        end = min(at + STORY_W, len(story))
        if end < len(story):
            k = story.rfind(" ", at + 1, end + 1)
            if k > at:
                end = k
        lines.append(story[at:min(end, shown)])
        at = end
        while at < len(story) and story[at] == " ":
            at += 1
    return lines


def card(w, h, big, small, f_big, f_small):
    im = Image.new("RGB", (w, h), (8, 9, 12))
    d = ImageDraw.Draw(im)
    bw = d.textlength(big, font=f_big)
    d.text(((w - bw) / 2, h / 2 - f_big.size), big, font=f_big, fill=(235, 235, 230))
    for i, line in enumerate(small):
        sw = d.textlength(line, font=f_small)
        d.text(((w - sw) / 2, h / 2 + 0.4 * f_big.size + i * 1.5 * f_small.size), line, font=f_small,
               fill=(150, 160, 175))
    return im


def fade(im, k):
    return Image.blend(Image.new("RGB", im.size, (0, 0, 0)), im, max(0.0, min(1.0, k)))


def runs(frames):
    """The film's frames in stretches of one kind: the opening titles, the
    play -- every chamber, one after the other, as the game goes -- and the
    closing titles. Each fades in and out."""
    out = []
    for f in frames:
        if not out or out[-1][0] != f["kind"]:
            out.append([f["kind"], []])
        out[-1][1].append(f)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("demos", nargs="+", help="demo names: chamber ids, filmed one after the other")
    ap.add_argument("--mp4", help="the film, with sound")
    ap.add_argument("--gif", help="a GIF of it, without sound")
    ap.add_argument("--chambers", help="a directory of chamber files, as on the SD card")
    ap.add_argument("--seconds", type=float, default=120.0, help="each chamber at most this long")
    ap.add_argument("--work", help="where the frames go (default build/movie): one each, to film several at once")
    ap.add_argument("--tas", action="store_true",
                    help="tool-assisted runs (tools/tas.py): as they go, GLaDOS cut off as in the game, with a timer")
    a = ap.parse_args()
    if not a.mp4 and not a.gif:
        ap.error("--mp4 and/or --gif")

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    recorder = os.path.join(root, "build", "movie", "host_movie")
    build = os.path.abspath(a.work) if a.work else os.path.join(root, "build", "movie")
    shots = os.path.join(build, "shots")
    tex = os.path.join(build, "tex")
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    os.makedirs(tex, exist_ok=True)
    for f in glob.glob(os.path.join(root, "textures", "*.png")):
        Image.open(f).convert("RGB").save(os.path.join(tex, os.path.basename(f)[:-4] + ".ppm"))

    env = dict(os.environ, HOST_SHOT_TEXTURES=tex, BUILD=build, HOST_MOVIE_TAS="1" if a.tas else "0")
    if a.chambers:
        env["PORTALS_CHAMBERS"] = os.path.abspath(a.chambers)
    subprocess.run([recorder, str(a.seconds)] + a.demos, env=env, check=True, stdout=subprocess.DEVNULL)

    with open(os.path.join(build, "movie.txt")) as f:
        lines = [line.rstrip("\n").split("\t") for line in f]
    w, h = 800 * SCALE, 480 * SCALE
    f_hud = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 13 * SCALE)
    f_big = font(["DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/DejaVuSans-Bold.ttf"], 46 * SCALE)
    f_small = font(["DejaVuSans.ttf", "/usr/share/fonts/truetype/DejaVuSans.ttf"], 14 * SCALE)

    # The film's frames, each with what the HUD shows on it.
    frames, chamber, before, done = [], {}, 0.0, 0.0
    for line in lines:
        if line[0] == "S":
            before += done  # the runs so far, to their exits
            done = 0.0
            chamber = {"name": line[1], "hint": line[2], "story": line[3] if len(line) > 3 else ""}
        elif line[0] == "P":
            now, exit_t = float(line[5]), float(line[6])
            run = exit_t if exit_t >= 0 else now
            done = run
            frames.append({"kind": "P", "pic": int(line[1]), "typed": int(line[2]), "turret": line[3],
                           "message": line[4], "run": run, "total": before + run, **chamber})
        else:
            frames.append({"kind": line[0]})
    total = before + done
    names = [l[1] for l in lines if l[0] == "S"]

    # The game's version, on the titles: which game this is a film of.
    with open(os.path.join(root, "metadata", "metadata.json")) as f:
        version = "Portals " + json.load(f)["version"]

    # The titles: a chamber's own name for one; for several, the game's.
    if len(names) == 1:
        number, _, rest = names[0].partition(" ")
        if number.isdigit() and rest.strip():
            opening = card(w, h, rest.strip().upper(), ["chamber %d" % int(number), version + ", on the Tanmatsu"],
                           f_big, f_small)
        else:
            opening = card(w, h, names[0].upper(), ["a test chamber for " + version, "on the Tanmatsu"], f_big, f_small)
    else:
        opening = card(w, h, "PORTALS", ["%d test chambers" % len(names), version + ", on the Tanmatsu"], f_big, f_small)
    closing = card(w, h, "PORTALS", [version + " for Tanmatsu", "github.com/annejan/tanmatsu-portals-grace"], f_big,
                   f_small)
    if a.tas:
        clock = "%d:%05.2f" % (total // 60, total % 60)
        opening = card(w, h, "PORTALS", ["a tool-assisted run of " + version, "%d test chambers in %s" % (len(names), clock)],
                       f_big, f_small)
        closing = card(w, h, clock, ["%d chambers, tool-assisted, at 50 steps a second" % len(names),
                                     version + " for Tanmatsu  -  github.com/annejan/tanmatsu-portals-grace"],
                       f_big, f_small)
    f_name = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 15 * SCALE)
    f_hint = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 11 * SCALE)
    f_msg = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 22 * SCALE)
    f_timer = font(["DejaVuSansMono-Bold.ttf", "/usr/share/fonts/truetype/DejaVuSansMono-Bold.ttf"], 26 * SCALE)

    def hud(f):
        """A frame of play, with the HUD on it as main.c draws it."""
        im = Image.open(os.path.join(shots, "movie_%05d.ppm" % f["pic"])).convert("RGB").resize((w, h), Image.LANCZOS)
        d = ImageDraw.Draw(im)
        d.text((8 * SCALE, 6 * SCALE), f["name"], font=f_name, fill=(255, 255, 255))
        d.text((8 * SCALE, 26 * SCALE), f["hint"], font=f_hint, fill=(160, 160, 160))
        cx, cy = w // 2, h // 2
        d.rectangle([cx - SCALE, cy - SCALE, cx + SCALE - 1, cy + SCALE - 1], fill=(255, 255, 255))
        if f["message"]:
            mw = d.textlength(f["message"], font=f_msg)
            d.text((cx - mw / 2, cy - 70 * SCALE), f["message"], font=f_msg, fill=(255, 255, 255))
        typed = story_lines(f["story"], f["typed"]) if f["typed"] > 0 else []
        for m, text in enumerate(typed):
            d.text((16 * SCALE, h - (44 + (len(typed) - 1 - m) * 18) * SCALE), text, font=f_hud, fill=YELLOW)
        if f["turret"]:
            d.text((16 * SCALE, h - (44 + len(typed) * 18) * SCALE), "Turret: " + f["turret"], font=f_hud, fill=RED)
        if a.tas:
            # The timer: this chamber, and the run so far, stopped at each exit.
            big = "%d:%05.2f" % (f["total"] // 60, f["total"] % 60)
            small = "%.2f" % f["run"]
            bw = d.textlength(big, font=f_timer)
            d.rectangle([w - bw - 28 * SCALE, 44 * SCALE, w - 8 * SCALE, 98 * SCALE], fill=(0, 0, 0))
            d.text((w - bw - 18 * SCALE, 46 * SCALE), big, font=f_timer, fill=(120, 255, 140))
            sw = d.textlength(small, font=f_hud)
            d.text((w - sw - 18 * SCALE, 80 * SCALE), small, font=f_hud, fill=(200, 200, 200))
        return im

    mp4 = a.mp4 or os.path.join(build, "film.mp4")
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
                           "%dx%d" % (w, h), "-framerate", str(FPS), "-i", "-", "-i",
                           os.path.join(build, "movie.wav"), "-c:v", "libx264", "-preset", "medium", "-crf", "20",
                           "-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-b:a", "160k", "-shortest",
                           "-movflags", "+faststart", mp4], stdin=subprocess.PIPE)
    n, fade_n = 0, FADE_S * FPS
    for kind, run in runs(frames):
        for j, f in enumerate(run):
            im = opening if kind == "C" else closing if kind == "E" else hud(f)
            ff.stdin.write(fade(im, min(j, len(run) - 1 - j) / fade_n).tobytes())
            n += 1
    ff.stdin.close()
    if ff.wait() != 0:
        sys.exit("ffmpeg failed")
    if a.gif:
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", mp4, "-vf",
                        "fps=%d,scale=360:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];"
                        "[b][p]paletteuse=dither=none:diff_mode=rectangle" % FPS,
                        a.gif], check=True)
    print("%s: %d frames, %.1f s" % (a.mp4 or a.gif, n, n / FPS))


if __name__ == "__main__":
    sys.exit(main())
