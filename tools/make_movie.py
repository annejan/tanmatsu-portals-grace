#!/usr/bin/env python3
"""A chamber's solution as a film: frames from the game's own renderer with
its textures, its own sound, the HUD's story line, a title and an end card.

    tools/make_movie.py DEMO [--mp4 OUT.mp4] [--gif OUT.gif] [--chambers DIR] [--work DIR]

DEMO is a demo name (a chamber file's id, such as 12-redirection); a
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
import wave

from PIL import Image, ImageDraw, ImageFont

FPS = 10
SCALE = 2  # 800 x 480 drawn at 1600 x 960, so the text stays sharp
STORY_W, STORY_LINES = 70, 3  # as main.c lays the story line out
TITLE_S, END_S, FADE_S = 2.5, 3.0, 0.6
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("demo")
    ap.add_argument("--mp4", help="the film, with sound")
    ap.add_argument("--gif", help="a GIF of it, without sound")
    ap.add_argument("--chambers", help="a directory of chamber files, as on the SD card")
    ap.add_argument("--title", help="the title card's name (default: the chamber's)")
    ap.add_argument("--seconds", type=float, default=120.0, help="at most this long")
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
    subprocess.run([recorder, a.demo, str(a.seconds)], env=env, check=True,
                   stdout=subprocess.DEVNULL)

    with open(os.path.join(build, "movie.txt")) as f:
        chamber = f.readline().rstrip("\n")
        story = f.readline().rstrip("\n")
        hud = [line.rstrip("\n").split("\t", 1) for line in f]
    frames = sorted(glob.glob(os.path.join(shots, "movie_*.ppm")))
    w, h = 800 * SCALE, 480 * SCALE
    f_hud = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 13 * SCALE)
    f_big = font(["DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/DejaVuSans-Bold.ttf"], 46 * SCALE)
    f_small = font(["DejaVuSans.ttf", "/usr/share/fonts/truetype/DejaVuSans.ttf"], 14 * SCALE)

    # "07  The grill": a built-in chamber's number, then its name.
    number, _, rest = chamber.partition(" ")
    if number.isdigit() and rest.strip():
        name, under = rest.strip(), ["chamber %d" % int(number), "Portals, on the Tanmatsu"]
    else:
        name, under = chamber, ["a test chamber for Portals", "on the Tanmatsu"]
    title = card(w, h, (a.title or name).upper(), under, f_big, f_small)
    end = card(w, h, "PORTALS", ["for Tanmatsu", "github.com/annejan/tanmatsu-portals-grace"], f_big, f_small)

    out = os.path.join(build, "film")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    n = 0

    def put(im):
        nonlocal n
        im.save(os.path.join(out, "f_%05d.png" % n), compress_level=1)
        n += 1

    t_title, t_end = int(TITLE_S * FPS), int(END_S * FPS)
    for i in range(t_title):
        put(fade(title, min(i, t_title - 1 - i) / (FADE_S * FPS)))
    for i, path in enumerate(frames):
        im = Image.open(path).convert("RGB").resize((w, h), Image.LANCZOS)
        d = ImageDraw.Draw(im)
        shown, sub = (hud[i] + [""])[:2] if i < len(hud) else ("-1", "")
        lines = story_lines(story, int(shown)) if int(shown) > 0 else []
        for k, line in enumerate(lines):
            d.text((16 * SCALE, h - (44 + (len(lines) - 1 - k) * 18) * SCALE), line, font=f_hud, fill=YELLOW)
        if sub:
            d.text((16 * SCALE, h - (44 + len(lines) * 18) * SCALE), "Turret: " + sub, font=f_hud, fill=RED)
        put(fade(im, min(i, len(frames) - 1 - i) / (FADE_S * FPS)))
    for i in range(t_end):
        put(fade(end, min(i, t_end - 1 - i) / (FADE_S * FPS)))

    # The sound, from the end of the title card on.
    src = wave.open(os.path.join(build, "movie.wav"))
    rate, ch = src.getframerate(), src.getnchannels()
    pcm = src.readframes(src.getnframes())
    pad = lambda s: b"\0" * (int(s * rate) * ch * 2)
    with wave.open(os.path.join(build, "film.wav"), "wb") as dst:
        dst.setnchannels(ch)
        dst.setsampwidth(2)
        dst.setframerate(rate)
        dst.writeframes(pad(TITLE_S) + pcm + pad(END_S))

    if a.mp4:
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i",
                        os.path.join(out, "f_%05d.png"), "-i", os.path.join(build, "film.wav"), "-c:v", "libx264",
                        "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-b:a",
                        "160k", "-shortest", "-movflags", "+faststart", a.mp4], check=True)
    if a.gif:
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i",
                        os.path.join(out, "f_%05d.png"), "-vf",
                        "scale=360:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];"
                        "[b][p]paletteuse=dither=none:diff_mode=rectangle",
                        a.gif], check=True)
    print("%s: %d frames, %.1f s" % (a.mp4 or a.gif, n, n / FPS))


if __name__ == "__main__":
    sys.exit(main())
