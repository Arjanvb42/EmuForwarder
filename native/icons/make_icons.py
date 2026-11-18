#!/usr/bin/env python3
"""Draws the two app icons (512x512) in the style of the Studio van Batenburg apps
(studiovanbatenburg.nl/apps: NCStep, ASentra): a near-black tile, a geometric monogram of slanted
bars in light grey, and one accent colour with a gradient. No text, no glow.

  EmuForwarder  "EF" with a blue accent: the F's middle bar is an arrow pointing forward.
  EFUpdater     the same "EF" with an orange accent: a third stem, an arrow pointing up.

  python3 make_icons.py   ->  emuforwarder.png, efupdater.png next to this script
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
S = 2048             # drawn large, scaled down for smooth edges
SLANT = 0.32         # italic slant, like the NC and A monograms
BACKGROUND = (16, 17, 20)
GREY = (222, 223, 226)


def slanted(points, cy=S / 2):
    return [(x + (cy - y) * SLANT, y) for x, y in points]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def gradient(top, bottom):
    """A vertical gradient the size of the canvas (with a slight diagonal)."""
    small = Image.new("RGB", (64, 64))
    px = small.load()
    for y in range(64):
        for x in range(64):
            t = min(1.0, max(0.0, (y * 0.85 + x * 0.25) / 63 / 1.1))
            px[x, y] = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3))
    return small.resize((S, S), Image.BICUBIC)


def rounded(mask, radius=12):
    """Rounds the corners of a mask a little, like the soft corners in the NC and A monograms."""
    return mask.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: 255 if v > 127 else 0).filter(
        ImageFilter.GaussianBlur(2))


def draw_icon(accent_shapes, f_top_end, f_middle, top, bottom):
    """accent_shapes(t) -> polygons (unslanted); everything gets the same italic slant."""
    t = 150  # bar thickness
    grey_shapes = [
        rect(470, 560, 470 + t, 1488),            # E
        rect(470, 560, 960, 560 + t),
        rect(470, 949, 900, 949 + t),
        rect(470, 1488 - t, 960, 1488),
        rect(1060, 560, 1060 + t, 1488),          # F
        rect(1060, 560, f_top_end, 560 + t),
    ]
    if f_middle:  # the F's middle bar in grey (in EmuForwarder it is the blue arrow)
        grey_shapes.append(rect(1060, 949, 1060 + f_middle, 949 + t))
    polys = accent_shapes(t)
    pts = [pt for poly in grey_shapes + polys for pt in slanted(poly)]
    xs, ys = [x for x, _ in pts], [y for _, y in pts]
    dx = S / 2 - (min(xs) + max(xs)) / 2
    dy = S / 2 - (min(ys) + max(ys)) / 2
    move = lambda poly: [(x + dx, y + dy) for x, y in slanted(poly)]

    img = Image.new("RGB", (S, S), BACKGROUND)
    grey = Image.new("L", (S, S), 0)
    for poly in grey_shapes:
        ImageDraw.Draw(grey).polygon(move(poly), fill=255)
    accent = Image.new("L", (S, S), 0)
    for poly in polys:
        ImageDraw.Draw(accent).polygon(move(poly), fill=255)
    img.paste(Image.new("RGB", (S, S), GREY), (0, 0), rounded(grey))
    img.paste(gradient(top, bottom), (0, 0), rounded(accent))
    return img.resize((512, 512), Image.LANCZOS)


def forward_arrow(t):
    # the F's middle bar, in the accent colour, ending in an arrow head pointing forward
    y0, y1, mid = 949, 949 + t, 949 + t / 2
    x1 = 1470
    return [rect(1060 + t + 40, y0, x1 + 10, y1),
            [(x1, y0 - 120), (x1 + 270, mid), (x1, y1 + 120)]]


def up_arrow(t):
    # a third stem next to the F, in the accent colour, with an arrow head pointing up
    x0 = 1620
    top = 560 + 170
    return [rect(x0, top - 10, x0 + t, 1488),
            [(x0 - 120, top), (x0 + t / 2, top - 270), (x0 + t + 120, top)]]


for name, shape, f_top_end, f_middle, top, bottom in (
    ("emuforwarder.png", forward_arrow, 1560, 0, (40, 190, 235), (16, 98, 196)),   # blue, like ASentra
    ("efupdater.png", up_arrow, 1460, 360, (255, 176, 46), (226, 84, 20)),         # orange
):
    draw_icon(shape, f_top_end, f_middle, top, bottom).save(HERE / name, optimize=True)
    print(HERE / name)
