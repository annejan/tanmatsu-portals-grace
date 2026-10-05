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


def runs(lines):
    """The film's frames, grouped: [kind, [lines...]] for each stretch of one
    card or one chamber's pictures."""
    out = []
    for line in lines:
        kind = line[0]
        key = (kind, line[1] if kind == "C" else None)
        if not out or out[-1][0] != key:
            out.append([key, []])
        out[-1][1].append(line)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("demos", nargs="+", help="demo names: chamber ids, filmed one after the other")
    ap.add_argument("--mp4", help="the film, with sound")
    ap.add_argument("--gif", help="a GIF of it, without sound")
    ap.add_argument("--chambers", help="a directory of chamber files, as on the SD card")
    ap.add_argument("--seconds", type=float, default=120.0, help="each chamber at most this long")
    ap.add_argument("--work", help="where the frames go (default build/movie): one each, to film several at once")
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

    env = dict(os.environ, HOST_SHOT_TEXTURES=tex, BUILD=build)
    if a.chambers:
        env["PORTALS_CHAMBERS"] = os.path.abspath(a.chambers)
    subprocess.run([recorder, str(a.seconds)] + a.demos, env=env, check=True, stdout=subprocess.DEVNULL)

    with open(os.path.join(build, "movie.txt")) as f:
        lines = [line.rstrip("\n").split("\t") for line in f]
    w, h = 800 * SCALE, 480 * SCALE
    f_hud = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 13 * SCALE)
    f_big = font(["DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/DejaVuSans-Bold.ttf"], 46 * SCALE)
    f_small = font(["DejaVuSans.ttf", "/usr/share/fonts/truetype/DejaVuSans.ttf"], 14 * SCALE)

    def title_card(chamber):
        # "07  The grill": a built-in chamber's number, then its name.
        number, _, rest = chamber.partition(" ")
        if number.isdigit() and rest.strip():
            return card(w, h, rest.strip().upper(), ["chamber %d" % int(number), "Portals, on the Tanmatsu"], f_big,
                        f_small)
        return card(w, h, chamber.upper(), ["a test chamber for Portals", "on the Tanmatsu"], f_big, f_small)

    end = card(w, h, "PORTALS", ["for Tanmatsu", "github.com/annejan/tanmatsu-portals-grace"], f_big, f_small)

    mp4 = a.mp4 or os.path.join(build, "film.mp4")
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
                           "%dx%d" % (w, h), "-framerate", str(FPS), "-i", "-", "-i",
                           os.path.join(build, "movie.wav"), "-c:v", "libx264", "-preset", "medium", "-crf", "20",
                           "-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-b:a", "160k", "-shortest",
                           "-movflags", "+faststart", mp4], stdin=subprocess.PIPE)
    n, fade_n = 0, FADE_S * FPS
    story_at = {}  # the story in force from each frame on
    frames, story = [], ""
    for line in lines:
        if line[0] == "S":
            story = line[1] if len(line) > 1 else ""
        else:
            frames.append(line)
            story_at[len(frames) - 1] = story
    k = 0
    for (kind, _), run in runs(frames):
        base = title_card(run[0][1]) if kind == "C" else end if kind == "E" else None
        for j, line in enumerate(run):
            if base is not None:
                im = base
            else:
                im = Image.open(os.path.join(shots, "movie_%05d.ppm" % int(line[1]))).convert("RGB")
                im = im.resize((w, h), Image.LANCZOS)
                d = ImageDraw.Draw(im)
                shown, sub = int(line[2]), line[3] if len(line) > 3 else ""
                typed = story_lines(story_at[k], shown) if shown > 0 else []
                for m, text in enumerate(typed):
                    d.text((16 * SCALE, h - (44 + (len(typed) - 1 - m) * 18) * SCALE), text, font=f_hud,
                           fill=YELLOW)
                if sub:
                    d.text((16 * SCALE, h - (44 + len(typed) * 18) * SCALE), "Turret: " + sub, font=f_hud,
                           fill=RED)
            ff.stdin.write(fade(im, min(j, len(run) - 1 - j) / fade_n).tobytes())
            k += 1
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
