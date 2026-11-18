#!/usr/bin/env python3
"""Draws the two app icons (512x512): EmuForwarder (blue) and EFUpdater (orange).

Same family (full-tile gradient, white pixel invader, Montserrat wordmark), but a different colour
(blue / orange), symbol and name, so the Updater is not opened by mistake.
  python3 make_icons.py   ->  emuforwarder.png, efupdater.png next to this script
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
FONTS = Path("/usr/share/fonts/julietaula-montserrat-fonts")
S = 2048  # drawn large, scaled down for smooth edges
INVADER = [
    "..X.....X..",
    "...X...X...",
    "..XXXXXXX..",
    ".XX.XXX.XX.",
    "XXXXXXXXXXX",
    "X.XXXXXXX.X",
    "X.X.....X.X",
    "...XX.XX...",
]


def font(name, size):
    return ImageFont.truetype(str(FONTS / name), size)


def shadow(layer, radius=26, offset=18, alpha=150):
    """The layer with a soft dark shadow under it, so white art stands out on the colour."""
    a = layer.split()[3].point(lambda v: v * alpha // 255)
    dark = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    dark.putalpha(a)
    dark = dark.transform(layer.size, Image.AFFINE, (1, 0, -offset * 0.4, 0, 1, -offset)).filter(ImageFilter.GaussianBlur(radius))
    return Image.alpha_composite(dark, layer)


def background(top, bottom):
    """The whole tile in a diagonal gradient, with a soft light top left and darker edges."""
    small = Image.new("RGB", (256, 256))
    px = small.load()
    for y in range(256):
        for x in range(256):
            t = min(1.0, (x * 0.45 + y * 0.85) / 330)
            c = [int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)]
            dx, dy = (x - 70) / 256, (y - 60) / 256
            light = max(0.0, 1 - math.hypot(dx, dy) * 1.6) * 0.35
            edge = max(abs(x - 128), abs(y - 128)) / 128
            dark = max(0.0, edge - 0.7) * 0.6
            px[x, y] = tuple(max(0, min(255, int(v + (255 - v) * light - v * dark))) for v in c)
    return small.resize((S, S), Image.BICUBIC).convert("RGBA")


def invader(draw, x, y, cell, colour):
    for row, line in enumerate(INVADER):
        for col, c in enumerate(line):
            if c == "X":
                draw.rectangle((x + col * cell, y + row * cell, x + (col + 1) * cell - 6, y + (row + 1) * cell - 6),
                               fill=colour)


def wordmark(img, text, sub):
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    size = 300
    f = font("Montserrat-BlackItalic.otf", size)
    while d.textlength(text, font=f) > S - 260:  # fit the width, with room at the edges
        size -= 6
        f = font("Montserrat-BlackItalic.otf", size)
    w = d.textlength(text, font=f)
    d.text(((S - w) / 2, 1430), text, font=f, fill=(255, 255, 255, 255))
    spaced = "  ".join(sub.split(" "))
    spaced = " ".join(spaced)
    fs = font("Montserrat-Bold.otf", 84)
    ws = d.textlength(spaced, font=fs)
    d.text(((S - ws) / 2, 1430 + size + 40), spaced, font=fs, fill=(255, 255, 255, 215))
    return Image.alpha_composite(img, shadow(layer, 20, 12, 120))


def forwarder():
    img = background((40, 150, 255), (10, 30, 120))
    art = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(art)
    white, soft = (255, 255, 255, 255), (255, 255, 255, 110)
    # the game: invader on a card
    d.rounded_rectangle((210, 380, 900, 1120), radius=80, fill=(255, 255, 255, 40), outline=white, width=30)
    invader(d, 300, 560, 46, white)
    # the home screen: a grid of tiles, one lit
    d.rounded_rectangle((1150, 300, 1850, 1000), radius=80, fill=(255, 255, 255, 40), outline=white, width=30)
    for r in range(2):
        for c in range(3):
            x, y = 1235 + c * 200, 410 + r * 260
            lit = (r, c) == (0, 1)
            d.rounded_rectangle((x, y, x + 150, y + 200), radius=22, fill=white if lit else soft)
    # the forward arrow from game to tile
    d.line([(860, 1120), (1010, 1010), (1160, 860)], fill=white, width=64, joint="curve")
    d.polygon([(1290, 720), (1110, 760), (1240, 900)], fill=white)
    img = Image.alpha_composite(img, shadow(art))
    return wordmark(img, "EmuForwarder", "EMULATOR FORWARDER")


def updater():
    img = background((255, 170, 40), (190, 50, 0))
    art = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(art)
    white = (255, 255, 255, 255)
    cx, cy, r = S // 2, 760, 560
    # two circular arrows around the invader (refresh / update)
    for start, end in ((200, 335), (20, 155)):
        d.arc((cx - r, cy - r, cx + r, cy + r), start=start, end=end, fill=white, width=80)
        a = math.radians(end)
        tip = (cx + r * math.cos(a), cy + r * math.sin(a))
        tangent, normal = a + math.pi / 2, a
        p1 = (tip[0] + 150 * math.cos(tangent), tip[1] + 150 * math.sin(tangent))
        p2 = (tip[0] - 120 * math.cos(normal) - 40 * math.cos(tangent), tip[1] - 120 * math.sin(normal) - 40 * math.sin(tangent))
        p3 = (tip[0] + 120 * math.cos(normal) - 40 * math.cos(tangent), tip[1] + 120 * math.sin(normal) - 40 * math.sin(tangent))
        d.polygon([p1, p2, p3], fill=white)
    invader(d, cx - 5.5 * 56, cy - 4 * 56, 56, white)
    img = Image.alpha_composite(img, shadow(art))
    return wordmark(img, "EFUpdater", "UPDATE TOOL")


for name, make in (("emuforwarder.png", forwarder), ("efupdater.png", updater)):
    make().resize((512, 512), Image.LANCZOS).convert("RGB").save(HERE / name, optimize=True)
    print(HERE / name)
