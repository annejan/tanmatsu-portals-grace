#!/usr/bin/env python3
"""Chambers' solutions as a film: frames from the game's own renderer with
its textures, its own sound, the HUD's story line, a title card for each
chamber and an end card. Several chambers make one film, the music playing
on from one to the next.

    tools/make_movie.py DEMO [DEMO...] [--mp4 OUT.mp4] [--gif OUT.gif] [--chambers DIR] [--work DIR]

--badge films only what the badge itself shows: no title cards and no
fades of this tool's, the game's own HUD (main/hud.c, drawn by the
recorder) on every picture, lifts between the chambers and into the
first, the film over at the top of the last ride up. With --story (a
desk story's rounds and outro, tools/story_film.py --badge) no lifts: as
main.c plays a desk story's rounds, each with its message and verdict,
and the outro as main/outro.c plays it. With --badge nothing is silent
that is not on the badge: the splash screens (--splash) have their own
sounds and the menu music (tests/host_title.c, `make host_title`), and a
story's desk (--interludes) the story's tune, played on by the recorder.

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
import textwrap

from PIL import Image, ImageDraw, ImageFont

FPS = 10
SCALE = 2  # 800 x 480 drawn at 1600 x 960, so the text stays sharp
STORY_W, STORY_LINES = 70, 3  # as hud.c lays the story line out
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


def splash_sound(splash_json, n_frames):
    """The splash screens' sound as the badge plays it, n_frames long:
    tests/host_title.c's start-up -- the engine's splash silent, then
    Graceloader's ticks and the game's shots, the menu music on -- with
    as many pictures of each as `make splash` made (it checks they agree).
    Raw 16-bit stereo, as movie.wav."""
    import tempfile
    import wave
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    recorder = os.path.join(root, "build", "title", "host_title")
    if not os.path.exists(recorder):
        sys.exit("%s: none -- `make host_title` first (the splash screens' sound)" % recorder)
    with open(splash_json) as f:
        pics = json.load(f)
    count = [sum(1 for p, _ in pics if os.path.basename(p).startswith(k + "_")) for k in ("engine", "loader", "portals")]
    if sum(count) != len(pics):
        sys.exit("%s: not make splash's (engine_, loader_ and portals_ pictures)" % splash_json)
    with tempfile.TemporaryDirectory(dir=os.path.join(root, "build")) as out:
        env = dict(os.environ, HOST_TITLE_OUT=out, HOST_TITLE_CONTINUE="", HOST_TITLE_KEYS="")
        subprocess.run([recorder] + [str(c) for c in count] + ["0", "-1"], env=env, cwd=root, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with wave.open(os.path.join(out, "title.wav")) as src:
            step = src.getsampwidth() * src.getnchannels()
            return src.readframes(int(round(n_frames * src.getframerate() / FPS)))[:int(round(
                n_frames * src.getframerate() / FPS)) * step]


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
    global SCALE
    ap = argparse.ArgumentParser()
    ap.add_argument("demos", nargs="*", help="demo names: chamber ids, filmed one after the other")
    ap.add_argument("--recording", help="film a recording (main/recording.h) instead of demos: a run recorded on "
                    "the badge, as Watch a recording plays it, with its timer (implies --badge)")
    ap.add_argument("--packs", help="story packs for a recording's chambers, as /sd/portals/dlc")
    ap.add_argument("--ghost", help="another recording of the same chambers, raced as a ghost (its runner drawn "
                    "as the game draws a ghost)")
    ap.add_argument("--recorder", help="the film's recorder (default build/movie/host_movie): another physics' "
                    "build, `make board-tools`")
    ap.add_argument("--scale", type=int, choices=(1, 2), default=SCALE,
                    help="the film's size: 2 (the default) 1600 x 960, each badge pixel doubled; 1 the badge's 800 x 480")
    ap.add_argument("--mp4", help="the film, with sound")
    ap.add_argument("--gif", help="a GIF of it, without sound")
    ap.add_argument("--chapters", help="a JSON file of where each run (S line) starts and ends in the film, "
                    "in seconds, in the recording's order")
    ap.add_argument("--crf", type=int, default=20, help="x264 quality of the film (lower: better, bigger)")
    ap.add_argument("--preset", default="medium", help="x264 preset of the film")
    ap.add_argument("--tune", help="x264 tune of the film (animation suits the badge's flat pixels)")
    ap.add_argument("--chambers", help="a directory of chamber files, as on the SD card")
    ap.add_argument("--seconds", type=float, default=120.0, help="each chamber at most this long")
    ap.add_argument("--work", help="where the frames go (default build/movie): one each, to film several at once")
    ap.add_argument("--keep-shots", action="store_true",
                    help="keep the recorder's frames (WORK/shots, about 2 GB a minute of film) once the film is made")
    ap.add_argument("--title", help="the opening card's title: a story pack's name, say")
    ap.add_argument("--about", help="under it: what it is")
    ap.add_argument("--ending", help="the closing card's words")
    ap.add_argument("--interludes", help="a JSON file: {\"K\": [[png, seconds], ...], \"end\": [...]}, pictures "
                    "shown before the K-th chamber (from 0) and before the closing card, in silence "
                    "(tools/story_film.py: the desk between rounds)")
    ap.add_argument("--desk-sound", action="store_true",
                    help="the interludes before the chambers are a desk story's desk: the tune plays on through them "
                    "in the desk's mood, as on the badge (main.c's MODE_DESK), not silence (--badge always does)")
    ap.add_argument("--smooth", action="store_true",
                    help="blend the game's 800 x 480 pixels up to the film's size, instead of doubling each")
    ap.add_argument("--lifts", action="store_true",
                    help="from chamber to chamber by lift, as the game goes with Settings -> Lifts (main/lift.h)")
    ap.add_argument("--splash", help="a JSON list [[picture, seconds], ...] shown before the opening card: the "
                    "splash screens as the badge starts (build/host_splash)")
    ap.add_argument("--badge", action="store_true",
                    help="only the fades and text the badge shows: its own HUD (main/hud.c, less the frame rate), "
                    "no title cards, no fades but the lifts' (implies --lifts)")
    ap.add_argument("--story", help="with --badge: a desk story's rounds and outro, a line for each demo "
                    "(tests/host_movie.c, HOST_MOVIE_STORY; tools/story_film.py --badge writes it): no lifts")
    ap.add_argument("--music", help="the tune, a story pack's `music:` line (main/pack.h, main/sound.h); "
                    "default the game's own")
    ap.add_argument("--tas", action="store_true",
                    help="tool-assisted runs (tools/tas.py): as they go, GLaDOS cut off as in the game, with a timer")
    a = ap.parse_args()
    SCALE = a.scale
    if not a.mp4 and not a.gif:
        ap.error("--mp4 and/or --gif")
    if a.recording:
        a.badge = True
        if a.demos:
            ap.error("a recording, or demos: not both")
    elif not a.demos:
        ap.error("which demos?")
    if a.story and not a.badge:
        ap.error("--story wants --badge")
    if a.badge and not a.story:
        a.lifts = True

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    recorder = os.path.abspath(a.recorder) if a.recorder else os.path.join(root, "build", "movie", "host_movie")
    build = os.path.abspath(a.work) if a.work else os.path.join(root, "build", "movie")
    shots = os.path.join(build, "shots")
    tex = os.path.join(build, "tex")
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    os.makedirs(tex, exist_ok=True)
    for f in glob.glob(os.path.join(root, "textures", "*.png")):
        Image.open(f).convert("RGB").save(os.path.join(tex, os.path.basename(f)[:-4] + ".ppm"))

    env = dict(os.environ, HOST_SHOT_TEXTURES=tex, BUILD=build, HOST_MOVIE_TAS="1" if a.tas else "0",
               HOST_MOVIE_LIFTS="1" if a.lifts else "0", HOST_MOVIE_BADGE="1" if a.badge else "0")
    if a.tas:
        env["HOST_MOVIE_TICK"] = "0.033333"  # 30 frames a second: the badge's TAS (tools/tas.py)
    if a.chambers:
        env["PORTALS_CHAMBERS"] = os.path.abspath(a.chambers)
    if a.story:
        env["HOST_MOVIE_STORY"] = os.path.abspath(a.story)
    env["HOST_MOVIE_MUSIC"] = a.music or ""
    if a.recording:
        env["HOST_MOVIE_RECORDING"] = os.path.abspath(a.recording)
        # A script's steps as host_runcheck takes them: 1/30 s, to the float
        # (`make tas`'s 0.033333 drifts from it by a millisecond a minute).
        env["HOST_MOVIE_TICK"] = "%.17g" % (1 / 30)
    if a.ghost:
        env["HOST_MOVIE_GHOST"] = os.path.abspath(a.ghost)
    if a.packs:
        env["HOST_MOVIE_PACKS"] = os.path.abspath(a.packs)
    inter = {}
    if a.interludes:
        with open(a.interludes) as f:
            inter = json.load(f)

    def frames_of(key):
        return sum(max(1, int(round(float(secs) * FPS))) for _, secs in inter.get(key, []))
    if a.badge or a.desk_sound:
        # The desks between a story's rounds: on the badge its tune plays on
        # there, so the recorder plays it on through each (HOST_MOVIE_DESKS).
        env["HOST_MOVIE_DESKS"] = ",".join(str(frames_of(str(k))) for k in range(len(a.demos)))
    subprocess.run([recorder, str(a.seconds)] + a.demos, env=env, check=True, stdout=subprocess.DEVNULL)

    with open(os.path.join(build, "movie.txt")) as f:
        lines = [line.rstrip("\n").split("\t") for line in f]
    w, h = 800 * SCALE, 480 * SCALE
    f_hud = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 13 * SCALE)
    f_big = font(["DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/DejaVuSans-Bold.ttf"], 46 * SCALE)
    f_small = font(["DejaVuSans.ttf", "/usr/share/fonts/truetype/DejaVuSans.ttf"], 14 * SCALE)

    # The film's frames, each with what the HUD shows on it.
    frames, chamber, before, done, ci = [], {}, 0.0, 0.0, -1
    for line in lines:
        if line[0] == "S":
            before += done  # the runs so far, to their exits
            done = 0.0
            ci += 1
            chamber = {"name": line[1], "hint": line[2], "story": line[3] if len(line) > 3 else "", "ci": ci}
        elif line[0] == "P":
            now, exit_t = float(line[5]), float(line[6])
            run = exit_t if exit_t >= 0 else now
            done = exit_t if exit_t >= 0 else 0.0  # the total counts a run to its exit, as watch.c's
            frames.append({"kind": "P", "pic": int(line[1]), "typed": int(line[2]), "turret": line[3],
                           "message": line[4], "run": run, "exit": exit_t, "total": before + run, **chamber})
        else:
            frames.append({"kind": line[0]})
    total = before + done
    names = [l[1] for l in lines if l[0] == "S"]

    # Interludes: pictures between the chambers, the sound stopped for them
    # -- with --badge, the sound they have on the badge instead (below).
    silences = []  # (at the original frame, for so many frames[, the sound for them])
    if a.splash:
        with open(a.splash) as f:
            inter["start"] = json.load(f)
    if inter:
        out, k, closing_seen = [], 0, False
        def pictures(key):
            got = []
            for png, secs in inter.get(key, []):
                got += [{"kind": "I", "png": png}] * max(1, int(round(float(secs) * FPS)))
            return got
        fi = 0  # into frames, which has one per P, C or E line
        opened = False
        if a.badge:
            # No opening card: the splash screens, then the game.
            opened = True
            out += pictures("start")
            if out:
                silences.append((0, len(out), splash_sound(a.splash, len(out))))
        for line in lines:
            if line[0] == "C" and not opened:
                opened = True  # the splash screens, before the opening card, with their sound
                extra = pictures("start")
                if extra:
                    silences.append((fi, len(extra), splash_sound(a.splash, len(extra)) if a.splash else b""))
                out += extra
            if line[0] == "S":
                extra = pictures(str(k))
                if extra and not (a.badge or a.desk_sound):  # else the recorder played the desk's sound
                    silences.append((fi, len(extra)))
                out += extra
                k += 1
            else:  # every other line is a frame (as read above)
                if line[0] == "E" and not closing_seen:
                    closing_seen = True
                    extra = pictures("end")
                    if extra:
                        silences.append((fi, len(extra)))
                    out += extra
                out.append(frames[fi])
                fi += 1
        if a.badge and not closing_seen:
            extra = pictures("end")
            if extra:
                silences.append((fi, len(extra)))
            out += extra
        frames = out

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
    # A story pack's film: its own name and blurb, and its ending to close.
    if a.title:
        opening = card(w, h, a.title.upper(), [a.about or "", "a story for %s, on the Tanmatsu" % version], f_big,
                       f_small)
    if a.ending:
        closing = card(w, h, "THE END", textwrap.wrap(a.ending, 64) + ["", version + " for Tanmatsu"], f_big, f_small)
    f_name = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 15 * SCALE)
    f_hint = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 11 * SCALE)
    f_msg = font(["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"], 22 * SCALE)
    f_timer = font(["DejaVuSansMono-Bold.ttf", "/usr/share/fonts/truetype/DejaVuSansMono-Bold.ttf"], 26 * SCALE)

    def hud(f):
        """A frame of play, with the HUD on it as hud.c draws it."""
        if a.badge:  # drawn by hud.c itself: the badge's pixels, each doubled
            return Image.open(os.path.join(shots, "movie_%05d.ppm" % f["pic"])).convert("RGB").resize((w, h),
                                                                                                      Image.NEAREST)
        # The badge's pixels, each doubled, as it shows them (--smooth: blended); the text over them sharp.
        im = Image.open(os.path.join(shots, "movie_%05d.ppm" % f["pic"])).convert("RGB").resize(
            (w, h), Image.LANCZOS if a.smooth else Image.NEAREST)
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

    wav = os.path.join(build, "movie.wav")
    if silences:
        # The sound, with silence where the interludes go.
        import wave
        with wave.open(wav) as src:
            params = src.getparams()
            audio = src.readframes(src.getnframes())
        step = params.sampwidth * params.nchannels
        per_frame = params.framerate / FPS
        pieces, last = [], 0
        for at, n_frames, *sound in silences:
            cut = int(round(at * per_frame)) * step
            size = int(round(n_frames * per_frame)) * step
            fill = sound[0][:size] if sound and sound[0] else b""
            pieces += [audio[last:cut], fill + b"\0" * (size - len(fill))]
            last = cut
        pieces.append(audio[last:])
        wav = os.path.join(build, "movie-interludes.wav")
        with wave.open(wav, "wb") as dst:
            dst.setparams(params)
            dst.writeframes(b"".join(pieces))
    pic_cache = {}

    def picture(f):
        if f["png"] not in pic_cache:
            pic_cache.clear()
            pic_cache[f["png"]] = Image.open(f["png"]).convert("RGB").resize(
                (w, h), Image.NEAREST if a.badge else Image.LANCZOS)
        return pic_cache[f["png"]]

    # Where each run is in the film, for a page to jump to: its first and last
    # frame, and from its last whether it reached the exit, and when (the
    # exit frame's own clock, as host_runcheck's split); no exit, no time.
    if a.chapters:
        at = {}  # run -> [first frame, after its last, its exit time or -1]
        for i, f in enumerate(frames):
            if f["kind"] == "P" and "ci" in f:
                at.setdefault(f["ci"], [i, i, -1.0])[1:] = [i + 1, f["exit"]]
        with open(a.chapters, "w") as fo:
            json.dump({"chapters": [{"name": n, "start": round(at[c][0] / FPS, 3), "end": round(at[c][1] / FPS, 3),
                                     "time": round(at[c][2], 3) if at[c][2] >= 0 else None, "exit": at[c][2] >= 0}
                                    for c, n in enumerate(names) if c in at]}, fo)

    mp4 = a.mp4 or os.path.join(build, "film.mp4")
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
                           "%dx%d" % (w, h), "-framerate", str(FPS), "-i", "-", "-i",
                           wav, "-c:v", "libx264", "-preset", a.preset, "-crf", str(a.crf)] +
                          (["-tune", a.tune] if a.tune else []) +
                          ["-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-b:a", "160k", "-shortest",
                           "-movflags", "+faststart", mp4], stdin=subprocess.PIPE)
    n, fade_n = 0, FADE_S * FPS
    for kind, run in runs(frames):
        for j, f in enumerate(run):
            im = opening if kind == "C" else closing if kind == "E" else picture(f) if kind == "I" else hud(f)
            ff.stdin.write((im if a.badge else fade(im, min(j, len(run) - 1 - j) / fade_n)).tobytes())
            n += 1
    ff.stdin.close()
    if ff.wait() != 0:
        sys.exit("ffmpeg failed")
    if not a.keep_shots:
        shutil.rmtree(shots, ignore_errors=True)  # raw frames, about 2 GB a minute: the film has them now
    if a.gif:
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", mp4, "-vf",
                        "fps=%d,scale=360:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];"
                        "[b][p]paletteuse=dither=none:diff_mode=rectangle" % FPS,
                        a.gif], check=True)
    print("%s: %d frames, %.1f s" % (a.mp4 or a.gif, n, n / FPS))


if __name__ == "__main__":
    sys.exit(main())
