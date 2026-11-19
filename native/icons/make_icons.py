#!/usr/bin/env python3
"""Draws the two app icons (512x512) in the Studio van Batenburg style (studiovanbatenburg.nl/apps):
near-black tile filled to the edges, crisp light-grey shapes, one accent colour with a gradient,
no glow. Each icon has a small illustration and its name written out.

  EmuForwarder  blue:   pixel invader in a framed tile with a forward arrow
  EFUpdater     orange: the invader inside two turning arrows (update)

  python3 make_icons.py   ->  emuforwarder.png, efupdater.png next to this script
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
FONTS = Path("/usr/share/fonts/julietaula-montserrat-fonts")
S = 2048  # drawn large, scaled down for smooth edges
BACKGROUND = (15, 16, 19)
GREY = (222, 223, 226)
DIM = (150, 153, 162)
DARK_TILE = (44, 46, 54)
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


def gradient(top, bottom, angle=0.3):
    small = Image.new("RGB", (64, 64))
    px = small.load()
    for y in range(64):
        for x in range(64):
            t = min(1.0, max(0.0, (y + x * angle) / (63 * (1 + angle))))
            px[x, y] = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3))
    return small.resize((S, S), Image.BICUBIC)


def background(accent):
    """Near-black, with a faint glow of the accent colour from the bottom right (like the website)."""
    img = Image.new("RGB", (S, S), BACKGROUND)
    glow = Image.new("L", (S, S), 0)
    ImageDraw.Draw(glow).ellipse((S * 0.45, S * 0.55, S * 1.45, S * 1.55), fill=60)
    glow = glow.filter(ImageFilter.GaussianBlur(220))
    img.paste(Image.new("RGB", (S, S), accent), (0, 0), glow)
    return img


def paint(img, mask, fill):
    """Paste a colour or gradient image through a mask, with slightly softened edges."""
    soft = mask.filter(ImageFilter.GaussianBlur(3))
    img.paste(fill if isinstance(fill, Image.Image) else Image.new("RGB", (S, S), fill), (0, 0), soft)


def new_mask():
    m = Image.new("L", (S, S), 0)
    return m, ImageDraw.Draw(m)


def invader(draw, x, y, cell):
    for row, line in enumerate(INVADER):
        for col, c in enumerate(line):
            if c == "X":
                draw.rounded_rectangle((x + col * cell, y + row * cell, x + (col + 1) * cell - cell * 0.12,
                                        y + (row + 1) * cell - cell * 0.12), radius=cell * 0.14, fill=255)


def straight_arrow(draw, p0, p1, width, head_len, head_width):
    """One polygon: shaft from p0 to p1, head ending exactly at p1."""
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length      # along
    nx, ny = -uy, ux                        # across
    base = (p1[0] - ux * head_len, p1[1] - uy * head_len)
    w, hw = width / 2, head_width / 2
    pts = [
        (p0[0] + nx * w, p0[1] + ny * w), (base[0] + nx * w, base[1] + ny * w),
        (base[0] + nx * hw, base[1] + ny * hw), p1,
        (base[0] - nx * hw, base[1] - ny * hw), (base[0] - nx * w, base[1] - ny * w),
        (p0[0] - nx * w, p0[1] - ny * w),
    ]
    draw.polygon(pts, fill=255)


def arc_arrow(draw, cx, cy, r, width, start, end, head_len, head_width):
    """A ring segment from start to end degrees (clockwise on screen) with an arrow head that follows
    the circle and ends in a point on it."""
    head_angle = head_len / r
    a0, a1 = math.radians(start), math.radians(end) - head_angle
    steps = 90
    ring = lambda radius, i: (cx + radius * math.cos(a0 + (a1 - a0) * i / steps),
                              cy + radius * math.sin(a0 + (a1 - a0) * i / steps))
    outer = [ring(r + width / 2, i) for i in range(steps + 1)]
    inner = [ring(r - width / 2, i) for i in range(steps, -1, -1)]
    draw.polygon(outer + inner, fill=255)
    # head: base across the ring at a1, sides curving along the circle to a point on it
    hw = head_width / 2
    point = lambda radius, a: (cx + radius * math.cos(a), cy + radius * math.sin(a))
    side_out = [point(r + hw * (1 - k / 8), a1 + head_angle * k / 8) for k in range(9)]
    side_in = [point(r - hw * (1 - k / 8), a1 + head_angle * k / 8) for k in range(8, -1, -1)]
    draw.polygon(side_out + side_in, fill=255)


def title(img, sub_img, first, second, sub, accent_fill):
    """The name: first part in grey, second part in the accent gradient, a spaced subtitle below."""
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    size = 300
    while True:
        f = ImageFont.truetype(str(FONTS / "Montserrat-ExtraBold.otf"), size)
        w1, w2 = probe.textlength(first, font=f), probe.textlength(second, font=f)
        if w1 + w2 <= S - 300:
            break
        size -= 4
    x = (S - w1 - w2) / 2
    y = 1395
    m1, d1 = new_mask()
    d1.text((x, y), first, font=f, fill=255)
    m2, d2 = new_mask()
    d2.text((x + w1, y), second, font=f, fill=255)
    paint(img, m1, GREY)
    paint(img, m2, accent_fill)
    fs = ImageFont.truetype(str(FONTS / "Montserrat-SemiBold.otf"), 76)
    spaced = " ".join("  ".join(sub.split(" ")))
    ws = probe.textlength(spaced, font=fs)
    m3, d3 = new_mask()
    d3.text(((S - ws) / 2, y + size * 1.2), spaced, font=fs, fill=255)  # clear of the p's descender
    paint(sub_img, m3, DIM)


def framed(img, box, radius, edge=40):
    """A light-grey rounded frame with a dark inside."""
    outer, d = new_mask()
    d.rounded_rectangle(box, radius=radius, fill=255)
    inner, d2 = new_mask()
    x0, y0, x1, y1 = box
    d2.rounded_rectangle((x0 + edge, y0 + edge, x1 - edge, y1 - edge), radius=radius - edge * 0.6, fill=255)
    paint(img, outer, GREY)
    paint(img, inner, (24, 25, 30))


class Layers:
    """Where each part is drawn: all on one tile (the icon), or each on its own transparent canvas
    (the parts of the animated intro, see make_intro.py)."""

    def __init__(self, tile=None):
        self.tile, self.parts = tile, {}

    def __call__(self, name):
        if self.tile is not None:
            return self.tile
        return self.parts.setdefault(name, Image.new("RGBA", (S, S), (0, 0, 0, 0)))


def forwarder(layers):
    accent = gradient((79, 194, 247), (27, 106, 219))
    card = layers("card")
    framed(card, (552, 299, 1496, 1198), 138, 45)
    inv, di = new_mask()
    invader(di, 1024 - 5.5 * 47, 690 - 4 * 47, 47)
    paint(card, inv, GREY)
    arrow, da = new_mask()
    straight_arrow(da, (1024, 910), (1024, 1256), 120, 150, 270)
    paint(layers("arrow"), arrow, accent)
    title(layers("title"), layers("subtitle"), "Emu", "Forwarder", "EMULATOR FORWARDER", accent)


def updater(layers):
    accent = gradient((255, 182, 56), (232, 88, 20))
    cx, cy, r = S // 2, 760, 500
    arrows, da = new_mask()
    arc_arrow(da, cx, cy, r, 96, 200, 335, 200, 270)
    arc_arrow(da, cx, cy, r, 96, 20, 155, 200, 270)
    paint(layers("ring"), arrows, accent)
    inv, di = new_mask()
    invader(di, cx - 5.5 * 58, cy - 4 * 58, 58)
    paint(layers("invader"), inv, GREY)
    title(layers("title"), layers("subtitle"), "EF", "Updater", "UPDATE TOOL", accent)


def game_tile(layers):
    """The icon of a game tile without a cover (until one is chosen with Options): just the game card."""
    card = layers("card")
    framed(card, (424, 324, 1624, 1724), 150, 60)
    inv, di = new_mask()
    invader(di, S / 2 - 5.5 * 84, S / 2 - 4 * 84 + 40, 84)
    paint(card, inv, GREY)


APPS = {"emuforwarder": (forwarder, (20, 90, 200)), "efupdater": (updater, (230, 100, 20))}

if __name__ == "__main__":
    for name, (make, glow_colour) in list(APPS.items()) + [("tile", (game_tile, (20, 90, 200)))]:
        tile = background(glow_colour)
        make(Layers(tile))
        tile.resize((512, 512), Image.LANCZOS).save(HERE / f"{name}.png", optimize=True)
        print(HERE / f"{name}.png")
