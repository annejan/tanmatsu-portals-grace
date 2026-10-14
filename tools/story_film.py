#!/usr/bin/env python3
"""A desk story's day as a film: the desk between the rounds, screen by
screen, and each round as it was played -- every flaw cheesed, every final
solved as meant, the broken drafts repaired in the debugger
(tools/debugger_film.py) -- then the outro's walk, its cards and its
reveal. It opens as the badge starts (`make splash`), and the pack's
tune plays on at the desk, as on the badge.

    tools/story_film.py dlc/human-in-the-loop --mp4 build/dlc/human-in-the-loop-story.mp4

tests/host_review.c plays the day (`host_review story PACK OUTDIR`) and
writes it out; tools/make_movie.py films the rounds, the desk's screens
spliced in between. Needs Pillow and ffmpeg; `make story-film` builds the
tools first.

--badge films only what the badge shows, at 1600 x 960 (its 800 x 480,
each pixel doubled): the splash screens (`make splash`), the desk as
main/deskview.c draws it (tests/host_desk.c, from host_review's screens),
each round as main.c plays a desk story's -- no lifts, its message, the
story line and the verdict by main/hud.c -- cut to and from the desk, a
broken round's debugger as main/editor.c draws it (tests/host_debugger.c),
and the outro as main/outro.c plays it. No title cards, no fades of the
film's own, no text drawn here. `make story-film PACK=hackfest BADGE=1`.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import debugger_film  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 1600, 960
GREEN, DIM, BG = (0x7C, 0xFF, 0x8A), (0x2E, 0x6B, 0x38), (5, 8, 6)
# A badge's launcher (day.txt `style: badge`), as deskview.c draws it.
BADGE = {"fg": (0xED, 0xE4, 0xF5), "bg": (0x14, 0x0A, 0x1E), "bar": (0xB0, 0x38, 0x9C), "dim": (0x6A, 0x4A, 0x80)}
DOS = {"fg": GREEN, "bg": BG, "bar": GREEN, "dim": DIM}
TYPE_RATE = 24.0  # letters a second, as outro.c types the cards
FPS = 10


def font(names, size):
    for n in names:
        try:
            return ImageFont.truetype(n, size)
        except OSError:
            pass
    return ImageFont.load_default()


MONO = ["DejaVuSansMono.ttf", "/usr/share/fonts/truetype/DejaVuSansMono.ttf"]
SANS = ["DejaVuSans.ttf", "/usr/share/fonts/truetype/DejaVuSans.ttf"]
BOLD = ["DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/DejaVuSans-Bold.ttf"]


def pack_info(folder):
    info, rounds = {}, []
    with open(os.path.join(folder, "pack.txt")) as f:
        for line in f:
            key, _, value = line.strip().partition(":")
            if key == "round":
                rounds.append(value.strip())
            elif key:
                info[key] = value.strip()
    return info, rounds


def header(text, key):
    for line in text.splitlines():
        if line.startswith(key + ":"):
            return line[len(key) + 1:].strip()
    return ""


def set_header(text, key, value):
    """`key: value` in a chamber file's header, replaced or added."""
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line.startswith(key + ":"):
            lines[i] = "%s: %s" % (key, value)
            return "\n".join(lines)
    return "%s: %s\n%s" % (key, value, text)


def screen(path, f_mono, cell_w, row_h, pal=DOS):
    """A desk screen file (host_review's) as a picture: the terminal."""
    with open(path) as f:
        head = f.readline().split()
        rows = [f.readline().rstrip("\n") for _ in range(25)]
    secs, cur_row, cur_col, hl = float(head[0]), int(head[1]), int(head[2]), int(head[3])
    im = Image.new("RGB", (W, H), pal["bg"])
    d = ImageDraw.Draw(im)
    x0, y0 = (W - cell_w * 80) / 2, (H - row_h * 25) / 2
    for r, text in enumerate(rows):
        y = y0 + r * row_h
        if r == hl:
            d.rectangle([x0, y, x0 + cell_w * 80, y + row_h], fill=pal["bar"])
        d.text((x0, y + 4), text, font=f_mono, fill=(pal["fg"] if pal is BADGE else pal["bg"]) if r == hl else pal["fg"])
    if cur_row >= 0:
        x = x0 + cell_w * cur_col
        d.rectangle([x, y0 + row_h * (cur_row + 1) - 7, x + cell_w, y0 + row_h * (cur_row + 1) - 2], fill=pal["dim"])
    return im, secs


def centred(d, text, y, f, fill, chars):
    """Whole words, lines of at most `chars`, each centred on the screen."""
    words, lines = text.split(" "), [""]
    for w in words:
        if len(lines[-1]) + len(w) + 1 > chars and lines[-1]:
            lines.append(w)
        else:
            lines[-1] = (lines[-1] + " " + w).strip()
    for i, line in enumerate(lines):
        lw = d.textlength(line, font=f)
        d.text(((W - lw) / 2, y + i * f.size * 1.5), line, font=f, fill=fill)


def cards(desk_dir, out_dir):
    """The outro's cards, typed on black, the reveal last: pictures and how
    long each shows."""
    path = os.path.join(desk_dir, "outro.txt")
    shown, reveal, coda = [], None, []
    with open(path) as f:
        for line in f:
            key, _, value = line.strip().partition(":")
            if key == "card":
                shown.append(value.strip())
            elif key == "reveal":
                reveal = value.strip()
            elif key == "coda":
                coda.append(value.strip())
    f_card, f_reveal, f_coda = font(SANS, 40), font(BOLD, 96), font(SANS, 32)
    pics, n = [], 0

    def emit(im, secs):
        nonlocal n
        p = os.path.join(out_dir, "card-%04d.png" % n)
        n += 1
        im.save(p)
        pics.append([p, secs])

    for text in shown + ([reveal] if reveal else []):
        big = text == reveal
        for k in range(0, len(text) + 1, 3):  # typed, a few letters a frame
            im = Image.new("RGB", (W, H), (0, 0, 0))
            d = ImageDraw.Draw(im)
            centred(d, text[:k], H * (0.34 if big else 0.42), f_reveal if big else f_card, (255, 255, 255) if big
                    else (232, 232, 232), 24 if big else 56)
            emit(im, 3.0 / TYPE_RATE)
        im = Image.new("RGB", (W, H), (0, 0, 0))
        d = ImageDraw.Draw(im)
        centred(d, text, H * (0.34 if big else 0.42), f_reveal if big else f_card, (255, 255, 255) if big
                else (232, 232, 232), 24 if big else 56)
        if big:
            for i, line in enumerate(coda):
                centred(d, line, H * 0.6 + i * 52, f_coda, (176, 176, 176), 70)
        emit(im, 8.0 if big else 2.8)
    return pics


def debugger(folder, played, pics, inter):
    """A broken round opens in the debugger: the repairs made there, drawn by
    editor.c (tools/debugger_film.py), after the desk and before the
    play-test -- the K-th interlude, before the K-th round."""
    for n, (rnd, file_id, draft, route, headline, line) in enumerate(played, 1):
        path = os.path.join(folder, file_id + ".txt")
        with open(path) as f:
            broken = header(f.read(), "review") == "broken"
        if broken:
            inter.setdefault(str(n - 1), []).extend(debugger_film.frames(path, pics, "r%02d" % n))


def badge_film(a, folder, info, day, work, chambers, pics, played):
    """The day as the badge shows it: its splash, its desk, its rounds and
    its outro, each drawn by the game's own code."""
    splash = os.path.join(ROOT, "build", "splash", "splash.json")
    if not os.path.exists(splash):
        sys.exit("%s: none -- `make splash` first" % splash)
    with open(os.path.join(folder, "desk", "day.txt")) as f:
        style = "badge" if any(l.strip() == "style: badge" for l in f) else "dos"

    # The rounds' drafts as played, as they are; what main.c says of each.
    ids, story, last = [], [], None
    for n, (rnd, file_id, draft, route, headline, line) in enumerate(played, 1):
        cid = "r%02d" % n
        shutil.copy(os.path.join(day, cid + ".txt"), os.path.join(chambers, cid + ".txt"))
        with open(os.path.join(folder, file_id + ".txt")) as f:
            broken = header(f.read(), "review") == "broken"
        # A new file's story is told; the same file's next draft goes on
        # with the story line where it was (main.c's s_story_of).
        fresh = file_id != last
        last = file_id
        story.append("R\t%s\t%s\t%d\t%d\t%d\t%s\t%s" % (rnd, draft, fresh, broken, style == "badge", headline, line))
        ids.append(cid)
    if info.get("outro"):
        shutil.copy(os.path.join(folder, info["outro"] + ".txt"), os.path.join(chambers, "outro.txt"))
        story.append("O\t%s" % folder)
        ids.append("outro")
    story_path = os.path.join(work, "story.tsv")
    with open(story_path, "w") as f:
        f.write("\n".join(story) + "\n")

    # The desk's screens, as deskview.c draws them: each with its cursor
    # up and, where it has one, down -- it blinks by the clock, half a
    # second each way, typing or not.
    subprocess.run([os.path.join(ROOT, "build", "desk", "host_desk"), style, os.path.join(day, "screens"), pics],
                   check=True, stdout=subprocess.DEVNULL)
    for name in os.listdir(pics):
        if name.endswith(".ppm"):
            Image.open(os.path.join(pics, name)).save(os.path.join(pics, name[:-4] + ".png"))
            os.remove(os.path.join(pics, name))
    inter, clock = {}, 0.0
    for name in sorted(os.listdir(os.path.join(day, "screens"))):
        with open(os.path.join(day, "screens", name)) as f:
            secs = float(f.readline().split()[0])
        on = os.path.join(pics, name[:-4] + ".png")
        off = os.path.join(pics, name[:-4] + "-off.png")
        shots = inter.setdefault(str(int(name.split("-")[0])), [])
        for _ in range(max(1, int(round(secs * FPS)))):  # as make_movie.py counts an interlude's frames
            pic = off if os.path.exists(off) and int(clock / 0.5) % 2 else on
            if shots and shots[-1][0] == pic:
                shots[-1][1] = round(shots[-1][1] + 1.0 / FPS, 3)
            else:
                shots.append([pic, 1.0 / FPS])
            clock += 1.0 / FPS
    debugger(folder, played, pics, inter)
    with open(os.path.join(work, "interludes.json"), "w") as f:
        json.dump(inter, f)

    env = dict(os.environ)
    env.pop("HOST_MOVIE_NICK", None)
    if a.nick:
        env["HOST_MOVIE_NICK"] = a.nick
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_movie.py")] + ids + [
        "--badge", "--story", story_path, "--splash", splash, "--chambers", chambers,
        "--work", os.path.join(work, "movie"), "--mp4", os.path.abspath(a.mp4),
        "--interludes", os.path.join(work, "interludes.json"), "--music", info.get("music", "")], check=True, env=env)
    shutil.rmtree(os.path.join(work, "movie", "shots"), ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack", help="a desk story's folder, dlc/<pack>")
    ap.add_argument("--mp4", required=True)
    ap.add_argument("--work", help="where it is made (default build/story-film/<pack>, with --badge <pack>-badge)")
    ap.add_argument("--route", help="the way to go where the story branches (ROUTE at the desk); default its first")
    ap.add_argument("--badge", action="store_true",
                    help="only what the badge shows: its splash, desk, HUD and outro, drawn by the game's own code")
    ap.add_argument("--nick", help="with --badge: the badge owner's nickname, for [Subject-Name-here] as main.c "
                    "puts it in; none, and it stands as written, as on a badge without one")
    a = ap.parse_args()
    if a.nick and not a.badge:
        ap.error("--nick wants --badge")

    folder = os.path.abspath(a.pack)
    pid = os.path.basename(folder.rstrip("/"))
    work = os.path.abspath(a.work or os.path.join(ROOT, "build", "story-film", pid + ("-badge" if a.badge else "")))
    shutil.rmtree(work, ignore_errors=True)
    day, chambers, pics = os.path.join(work, "day"), os.path.join(work, "chambers"), os.path.join(work, "pics")
    for d in (chambers, pics):
        os.makedirs(d)
    info, rounds = pack_info(folder)

    # The day, played.
    subprocess.run([os.path.join(ROOT, "build", "host_review"), "story", folder, day] + ([a.route] if a.route else []),
                   check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(day, "rounds.tsv")) as f:
        played = [line.rstrip("\n").split("\t") for line in f]
    # How far into the story each is, as main.c tells the music: the round's
    # file among the pack's rounds; the outro the end (HOST_MOVIE_PROGRESS).
    last = max(1, len(rounds) - 1)
    at = [rounds.index(row[1]) / last if row[1] in rounds else 0.0 for row in played]
    if info.get("outro"):
        at.append(1.0)
    os.environ["HOST_MOVIE_PROGRESS"] = ",".join("%.4f" % x for x in at)
    if a.badge:
        badge_film(a, folder, info, day, work, chambers, pics, played)
        return

    # Each round a chamber: its name the round's, its story GLaDOS's -- the
    # chamber's own on a first draft, what she said of the last on the next.
    ids, said = [], {}
    for n, (rnd, file_id, draft, route, headline, line) in enumerate(played, 1):
        with open(os.path.join(day, "r%02d.txt" % n)) as f:
            text = f.read()
        name = header(text, "name")
        title = "Round %s: %s" % (rnd, name) if draft == "1" else "Round %s: %s, draft %s" % (rnd, name, draft)
        if len(title) > 31:  # a chamber's name holds 31: keep the draft's number
            title = "R%s: %s" % (rnd, title.partition(": ")[2])
        story = header(text, "story") if draft == "1" else said.get(file_id, "")
        text = set_header(text, "name", title[:31])
        text = set_header(text, "story", story.replace("[Subject-Name-here]", "test subject")[:159])
        text = set_header(text, "hint", "%s, %s" % ("the meant way" if route == "solution" else route, headline))
        said[file_id] = line
        cid = "r%02d" % n
        with open(os.path.join(chambers, cid + ".txt"), "w") as f:
            f.write(text)
        ids.append(cid)
    # The outro's walk, the ending said over it.
    if info.get("outro"):
        with open(os.path.join(folder, info["outro"] + ".txt")) as f:
            text = f.read()
        text = set_header(text, "story", info.get("ending", "").replace("[Subject-Name-here]", "test subject"))
        with open(os.path.join(chambers, "outro.txt"), "w") as f:
            f.write(text)
        ids.append("outro")

    # The badge starting up, to open with, if `make splash` has drawn it.
    splash = os.path.join(ROOT, "build", "splash", "splash.json")

    # The desk's screens, before each round (and before the walk).
    f_mono = font(MONO, 32)
    cell_w = f_mono.getlength("M")
    row_h = 37
    inter = {}
    with open(os.path.join(folder, "desk", "day.txt")) as f:
        pal = BADGE if any(l.strip() == "style: badge" for l in f) else DOS
    for name in sorted(os.listdir(os.path.join(day, "screens"))):
        seg = str(int(name.split("-")[0]))
        im, secs = screen(os.path.join(day, "screens", name), f_mono, cell_w, row_h, pal)
        p = os.path.join(pics, "desk-" + name[:-4] + ".png")
        im.save(p)
        inter.setdefault(seg, []).append([p, secs])
    # A broken round opens in the debugger: the repairs made there, after
    # the desk and before the play-test (tools/debugger_film.py).
    debugger(folder, played, pics, inter)
    inter["end"] = cards(os.path.join(folder, "desk"), pics)
    with open(os.path.join(work, "interludes.json"), "w") as f:
        json.dump(inter, f)

    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_movie.py")] + ids + [
        "--chambers", chambers, "--work", os.path.join(work, "movie"), "--mp4", os.path.abspath(a.mp4),
        "--title", info.get("name", pid), "--about", info.get("about", ""),
        "--interludes", os.path.join(work, "interludes.json"), "--music", info.get("music", ""), "--desk-sound",
        ] + (["--splash", splash] if os.path.exists(splash) else []) + [
        "--ending", "A fan story in honour of Valve's Portal. Aperture Science, GLaDOS, Chell and Doug Rattmann "
                    "are Valve's."], check=True)
    shutil.rmtree(os.path.join(work, "movie", "shots"), ignore_errors=True)


if __name__ == "__main__":
    main()
