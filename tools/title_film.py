#!/usr/bin/env python3
"""The badge starting up, as a film: the three splash screens -- the
engine's, Graceloader's and the game's, `make splash`'s pictures -- then
the title screen, its menu over the attract mode, with the menu music and
the splashes' own sounds. Only what the badge draws: the title is
tests/host_title.c running main.c's title path, its menu main/menu.c on
the engine's se_ui (tests/title_ui.c), its 3D the game's render.c through
the attract mode's camera, at a quarter of the pixels as the badge draws
it, every picture in RGB565 as the badge's framebuffer holds it.

    tools/title_film.py [--seconds S] [--seed N] [--keys SECONDS:K,...] [--continue ID]
                        [--end-on-fade] [--mp4 OUT.mp4] [--mp4-3x OUT3.mp4] [--stills T,T,...]

--keys are pressed on the title through main/menu.c's own event handling
(u/d a cursor key, o Enter, b Esc), its clicks and all. The default is a
slow tour: a beat on the title, down a row at a time to Stories, into it
(each pack's blurb under the list), the second pack, back; down to
Settings, into it, down to Menu music, a beat, a couple more rows, back;
the title until the chamber behind it fades.

The film is 30 frames a second: the splashes' pictures (10 a second)
each held three frames, the title a frame for each of the attract mode's
steps. OUT.mp4 is the badge's 800 x 480 pixels doubled (1600 x 960),
OUT3.mp4 tripled (2400 x 1440), both nearest-neighbour. --stills writes
the film's frame at each time (seconds) as a PNG beside them, to look at.
`make title-film` builds the recorder and the splashes first. Needs
ffmpeg, and Pillow for --stills.
"""

import argparse
import json
import os
import subprocess
import sys

FPS = 30
W, H = 800, 480
FRAME = W * H * 3
# The default tour of the menus (--keys): seconds after the title is up.
TOUR = ",".join([
    "3.0:d", "4.0:d", "5.0:o",                     # New game, Stories; into Stories
    "8.0:d", "10.5:b",                             # the second pack; back
    "11.5:d", "12.4:d", "13.3:d", "14.2:d", "15.1:d", "16.1:o",  # down to Settings; into it
    "17.1:d", "17.9:d", "18.7:d", "19.5:d",        # down to Menu music
    "21.0:d", "21.8:d", "23.6:b",                  # a couple more; back to the title
])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=27.0, help="the title screen this long")
    ap.add_argument("--end-on-fade", action="store_true",
                    help="end the title where a run has faded out nearest --seconds, not in the middle of one")
    ap.add_argument("--seed", default="58", help="the attract mode's shuffle (attract_seed)")
    ap.add_argument("--continue", dest="cont", default="12-redirection",
                    help="the chamber Continue goes back to (\"\": no Continue, a first start)")
    ap.add_argument("--packs", default="dlc", help="the story packs on the card, for Stories (\"\": none)")
    ap.add_argument("--keys", default=TOUR, help="keys on the title, SECONDS:K after it comes up, K one of u d "
                    "(cursor), o (Enter), b (Esc)")
    ap.add_argument("--splash", default=None, help="make splash's splash.json (default build/splash/splash.json)")
    ap.add_argument("--work", default=None, help="where the sound and the intermediate go (default build/title)")
    ap.add_argument("--mp4", default=None, help="the film at 1600 x 960 (default build/title/startup.mp4)")
    ap.add_argument("--mp4-3x", dest="mp4_3x", default=None,
                    help="the film at 2400 x 1440 (default build/title/startup-2400x1440.mp4)")
    ap.add_argument("--stills", default="", help="seconds into the film: a PNG of the frame at each")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    work = os.path.abspath(a.work or os.path.join(root, "build", "title"))
    os.makedirs(work, exist_ok=True)
    recorder = os.path.join(root, "build", "title", "host_title")
    mp4 = os.path.abspath(a.mp4 or os.path.join(work, "startup.mp4"))
    mp4_3x = os.path.abspath(a.mp4_3x or os.path.join(work, "startup-2400x1440.mp4"))

    with open(a.splash or os.path.join(root, "build", "splash", "splash.json")) as f:
        splash = json.load(f)
    count = {k: sum(1 for p, _ in splash if os.path.basename(p).startswith(k + "_"))
             for k in ("engine", "loader", "portals")}
    if sum(count.values()) != len(splash) or any(abs(float(s) - 0.1) > 1e-6 for _, s in splash):
        sys.exit("%s: not make splash's (engine_, loader_ and portals_ pictures, a tenth of a second each)" % a.splash)

    env = dict(os.environ, HOST_TITLE_OUT=work, HOST_TITLE_SEED=str(a.seed), HOST_TITLE_CONTINUE=a.cont,
               HOST_TITLE_PACKS=a.packs if a.packs else os.path.join(work, "no-packs"), HOST_TITLE_KEYS=a.keys)
    args = [recorder, str(count["engine"]), str(count["loader"]), str(count["portals"])]

    seconds = a.seconds
    if a.end_on_fade:
        # A run through without pictures: when each of the attract mode's
        # runs begins. The film ends on the black before the one nearest.
        dry = subprocess.run(args + [str(seconds + 40.0), "-1"], env=env, cwd=root, check=True,
                             stderr=subprocess.PIPE, text=True).stderr.splitlines()
        starts = [float(l.split("s", 1)[0]) for l in dry if l.strip() and l.strip()[0].isdigit() and " s  " in l]
        title_at = starts[0]
        best = min(starts[1:], key=lambda t: abs(t - title_at - seconds))
        seconds = round(best - title_at, 4)  # over as that run begins: the last frame the fade's black
        print("the title for %.2f s: up to the run beginning at %.2f s" % (seconds, best))

    stills = sorted(float(t) for t in a.stills.split(",") if t.strip())
    still_at = {int(round(t * FPS)): t for t in stills}

    # Pass 1: every picture, losslessly, at 800 x 480.
    lossless = os.path.join(work, "startup-800x480.mkv")
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
                           "%dx%d" % (W, H), "-framerate", str(FPS), "-i", "-", "-c:v", "ffv1", lossless],
                          stdin=subprocess.PIPE)
    n = 0

    def put(raw):
        nonlocal n
        ff.stdin.write(raw)
        if n in still_at:
            from PIL import Image
            out = os.path.join(work, "still-%06.2f.png" % still_at[n])
            Image.frombytes("RGB", (W, H), raw).save(out)
            print(out)
        n += 1

    from_png = {}
    for png, secs in splash:
        if png not in from_png:
            from_png.clear()
            data = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", png, "-f", "rawvideo", "-pix_fmt", "rgb24",
                                   "-"], check=True, stdout=subprocess.PIPE).stdout
            if len(data) != FRAME:
                sys.exit("%s: not %d x %d" % (png, W, H))
            from_png[png] = data
        for _ in range(int(round(float(secs) * FPS))):
            put(from_png[png])

    r, w = os.pipe()
    rec = subprocess.Popen(args + [str(seconds), str(w)], env=env, cwd=root, pass_fds=[w],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    os.close(w)
    with os.fdopen(r, "rb") as pics:
        while True:
            raw = pics.read(FRAME)
            if len(raw) < FRAME:
                break
            put(raw)
    log = rec.stderr.read()
    if rec.wait() != 0:
        sys.exit(log + "host_title failed")
    sys.stderr.write(log)
    ff.stdin.close()
    if ff.wait() != 0:
        sys.exit("ffmpeg failed")

    # Pass 2: with the sound, doubled and tripled, each pixel a square.
    wav = os.path.join(work, "title.wav")
    for out, k in ((mp4, 2), (mp4_3x, 3)):
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", lossless, "-i", wav, "-vf",
                        "scale=%d:%d:flags=neighbor" % (W * k, H * k), "-c:v", "libx264", "-preset", "slow",
                        "-crf", "16", "-pix_fmt", "yuv420p", "-r", str(FPS), "-c:a", "aac", "-b:a", "192k",
                        "-shortest", "-movflags", "+faststart", out], check=True)
        print("%s: %d frames, %.1f s" % (out, n, n / FPS))


if __name__ == "__main__":
    sys.exit(main())
