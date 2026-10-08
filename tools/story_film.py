#!/usr/bin/env python3
"""A desk story's day as a film: the desk between the rounds, screen by
screen, and each round as it was played -- every flaw cheesed, every final
solved as meant, the broken draft repaired -- then the outro's walk, its
cards and its reveal.

    tools/story_film.py dlc/human-in-the-loop --mp4 build/dlc/human-in-the-loop-story.mp4

tests/host_review.c plays the day (`host_review story PACK OUTDIR`) and
writes it out; tools/make_movie.py films the rounds, the desk's screens
spliced in between. Needs Pillow and ffmpeg; `make story-film` builds the
tools first.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 1600, 960
GREEN, DIM, BG = (0x7C, 0xFF, 0x8A), (0x2E, 0x6B, 0x38), (5, 8, 6)
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


def screen(path, f_mono, cell_w, row_h):
    """A desk screen file (host_review's) as a picture: the terminal."""
    with open(path) as f:
        head = f.readline().split()
        rows = [f.readline().rstrip("\n") for _ in range(25)]
    secs, cur_row, cur_col, hl = float(head[0]), int(head[1]), int(head[2]), int(head[3])
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    x0, y0 = (W - cell_w * 80) / 2, (H - row_h * 25) / 2
    for r, text in enumerate(rows):
        y = y0 + r * row_h
        if r == hl:
            d.rectangle([x0, y, x0 + cell_w * 80, y + row_h], fill=GREEN)
        d.text((x0, y + 4), text, font=f_mono, fill=BG if r == hl else GREEN)
    if cur_row >= 0:
        x = x0 + cell_w * cur_col
        d.rectangle([x, y0 + row_h * (cur_row + 1) - 7, x + cell_w, y0 + row_h * (cur_row + 1) - 2], fill=DIM)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack", help="a desk story's folder, dlc/<pack>")
    ap.add_argument("--mp4", required=True)
    ap.add_argument("--work", help="where it is made (default build/story-film/<pack>)")
    a = ap.parse_args()

    folder = os.path.abspath(a.pack)
    pid = os.path.basename(folder.rstrip("/"))
    work = os.path.abspath(a.work or os.path.join(ROOT, "build", "story-film", pid))
    shutil.rmtree(work, ignore_errors=True)
    day, chambers, pics = os.path.join(work, "day"), os.path.join(work, "chambers"), os.path.join(work, "pics")
    for d in (chambers, pics):
        os.makedirs(d)
    info, rounds = pack_info(folder)

    # The day, played.
    subprocess.run([os.path.join(ROOT, "build", "host_review"), "story", folder, day], check=True,
                   stdout=subprocess.DEVNULL)
    with open(os.path.join(day, "rounds.tsv")) as f:
        played = [line.rstrip("\n").split("\t") for line in f]

    # Each round a chamber: its name the round's, its story GLaDOS's -- the
    # chamber's own on a first draft, what she said of the last on the next.
    ids, said = [], {}
    for n, (rnd, file_id, draft, route, headline, line) in enumerate(played, 1):
        with open(os.path.join(day, "r%02d.txt" % n)) as f:
            text = f.read()
        name = header(text, "name")
        title = "Round %s: %s" % (rnd, name) if draft == "1" else "Round %s: %s, draft %s" % (rnd, name, draft)
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

    # The desk's screens, before each round (and before the walk).
    f_mono = font(MONO, 32)
    cell_w = f_mono.getlength("M")
    row_h = 37
    inter = {}
    for name in sorted(os.listdir(os.path.join(day, "screens"))):
        seg = str(int(name.split("-")[0]))
        im, secs = screen(os.path.join(day, "screens", name), f_mono, cell_w, row_h)
        p = os.path.join(pics, "desk-" + name[:-4] + ".png")
        im.save(p)
        inter.setdefault(seg, []).append([p, secs])
    inter["end"] = cards(os.path.join(folder, "desk"), pics)
    with open(os.path.join(work, "interludes.json"), "w") as f:
        json.dump(inter, f)

    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_movie.py")] + ids + [
        "--chambers", chambers, "--work", os.path.join(work, "movie"), "--mp4", os.path.abspath(a.mp4),
        "--title", info.get("name", pid), "--about", info.get("about", ""),
        "--interludes", os.path.join(work, "interludes.json"),
        "--ending", "A fan story in honour of Valve's Portal. Aperture Science, GLaDOS, Chell and Doug Rattmann "
                    "are Valve's."], check=True)
    shutil.rmtree(os.path.join(work, "movie", "shots"), ignore_errors=True)


if __name__ == "__main__":
    main()
