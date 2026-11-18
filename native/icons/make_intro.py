#!/usr/bin/env python3
"""Renders the parts of the animated intros (1920x1080) from the icon drawing (make_icons.py), and the
still splash the PS4 shows before the app starts (pic1.png, the intro's empty background).

  python3 make_intro.py <out folder>
  -> <out>/<app>/bg.png, <part>.png and layout.txt ("name x y w h" in screen pixels), <out>/<app>/pic1.png

intro.h plays them: parts slide, fade, grow and turn into place over the background.
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

import make_icons as icons

W, H = 1920, 1080
SCALE = 0.55                      # icon space (2048) -> screen
OX = (W - icons.S * SCALE) / 2    # the icon's square, centred
OY = H / 2 - 1010 * SCALE         # the drawing's middle (y 1010 in icon space) at the screen's middle


def screen_background(glow_colour):
    """Near-black with a faint glow of the accent colour from the bottom right, like the icon."""
    img = Image.new("RGB", (W, H), icons.BACKGROUND)
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse((W * 0.55, H * 0.45, W * 1.35, H * 1.6), fill=60)
    glow = glow.filter(ImageFilter.GaussianBlur(200))
    img.paste(Image.new("RGB", (W, H), glow_colour), (0, 0), glow)
    return img


def main(out):
    for app, (make, glow_colour) in icons.APPS.items():
        folder = Path(out) / app
        folder.mkdir(parents=True, exist_ok=True)
        bg = screen_background(glow_colour)
        bg.save(folder / "bg.png", optimize=True)
        bg.save(folder / "pic1.png", optimize=True)
        layers = icons.Layers()
        make(layers)
        lines = []
        for name, part in layers.parts.items():
            small = part.resize((round(icons.S * SCALE), round(icons.S * SCALE)), Image.LANCZOS)
            box = small.getbbox()
            if not box:
                continue
            if name == "ring":  # turns around its centre: crop a square around it
                cx, cy = 1024 * SCALE, 760 * SCALE
                half = math.ceil(max(cx - box[0], box[2] - cx, cy - box[1], box[3] - cy)) + 2
                box = (round(cx - half), round(cy - half), round(cx + half), round(cy + half))
            crop = small.crop(box)
            crop.save(folder / f"{name}.png", optimize=True)
            lines.append(f"{name} {round(OX + box[0])} {round(OY + box[1])} {crop.width} {crop.height}")
        (folder / "layout.txt").write_text("\n".join(lines) + "\n")
        print(folder, ", ".join(layers.parts))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "intro")
