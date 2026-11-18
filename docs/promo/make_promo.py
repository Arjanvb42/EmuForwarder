#!/usr/bin/env python3
"""Builds the EmuForwarder promo video (1920x1080, 30 fps) in the app's own look (native/ui.h): the dark
blue gradient, the app's font, one blue accent, the header with the icon, the tab bar and the footer with button
hints. No Nintendo artwork.

  python3 make_promo.py <material> <music.mp3> <out.mp4>

<material> holds:
  screens/   app screens rendered by promo_screens.cpp from the real ui.h (lib, menu, grid, editor,
             name, install0-3, installed)
  intro/     the app's intro, frame by frame (f_emuforwarder_000.png ...), from intro.h
  icons/     game icons as chosen on the PS4 (<hash>.png)
  art/       box art of the library's USB games (GameTDB, by disc ID)
and the app icons come from native/icons. The timing follows voiceover-script.md.
"""
import math
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ICONS = HERE.parents[1] / "native" / "icons"
APP_FONT = HERE.parents[3] / "work/forwarder/OpenOrbis/PS4Toolchain/samples/font/assets/fonts/Gontserrat-Regular.ttf"
W, H, FPS = 1920, 1080, 30
# the app's colours (native/ui.h)
BG, BG_LOW = (9, 12, 22), (14, 20, 38)
CARD, CARD_HI = (22, 28, 46), (30, 40, 66)
ACCENT, ACCENT_DEEP = (38, 132, 255), (20, 64, 150)
WHITE, GREY = (240, 244, 252), (146, 160, 190)
WARN, LINE = (255, 196, 92), (40, 50, 78)
SITE_GRADIENT = ACCENT          # the highlighted words: the app's accent
BLUE = [(56, 196, 240), (18, 104, 205)]
ORANGE = [(255, 182, 56), (232, 88, 20)]

MATERIAL = Path(sys.argv[1])
_fonts = {}


def font(weight, size):
    """The app's own font (one weight, like on the PS4)."""
    if size not in _fonts:
        _fonts[size] = ImageFont.truetype(str(APP_FONT), size)
    return _fonts[size]


def clamp(t):
    return 0.0 if t < 0 else 1.0 if t > 1 else t


def phase(t, a, b):
    return clamp((t - a) / (b - a))


def ease(t):
    return 1 - (1 - t) ** 3


def ease_back(t):
    c1 = 1.70158
    return 1 + (c1 + 1) * (t - 1) ** 3 + c1 * (t - 1) ** 2


def gradient_strip(colours, w, h):
    """A horizontal gradient through the colours."""
    strip = Image.new("RGB", (len(colours) * 64, 1))
    px = strip.load()
    n = strip.width
    for x in range(n):
        f = x / (n - 1) * (len(colours) - 1)
        i = min(int(f), len(colours) - 2)
        k = f - i
        px[x, 0] = tuple(int(colours[i][c] + (colours[i + 1][c] - colours[i][c]) * k) for c in range(3))
    return strip.resize((max(1, w), max(1, h)), Image.BILINEAR)


def text_image(parts, size, weight="ExtraBold", tracking=0):
    """parts: [(text, colour or [gradient colours])] on one line, as one RGBA image."""
    f = font(weight, size)
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    widths = []
    for text, _ in parts:
        if tracking:
            widths.append(sum(probe.textlength(ch, font=f) + tracking for ch in text) - tracking)
        else:
            widths.append(probe.textlength(text, font=f))
    ascent, descent = f.getmetrics()
    img = Image.new("RGBA", (int(sum(widths)) + 8, ascent + descent + 8), (0, 0, 0, 0))
    x = 4
    for (text, fill), w in zip(parts, widths):
        mask = Image.new("L", img.size, 0)
        d = ImageDraw.Draw(mask)
        if tracking:
            cx = x
            for ch in text:
                d.text((cx, 4), ch, font=f, fill=255)
                cx += probe.textlength(ch, font=f) + tracking
        else:
            d.text((x, 4), text, font=f, fill=255)
        if isinstance(fill, list):
            colour = Image.new("RGB", img.size)
            colour.paste(gradient_strip(fill, int(w) + 2, img.height), (int(x), 0))
        else:
            colour = Image.new("RGB", img.size, fill)
        img.paste(colour, (0, 0), mask)
        x += w
    return img.crop(img.getbbox() or (0, 0, 1, 1))


def put(frame, img, x, y, alpha=1.0, scale=1.0, anchor="lt"):
    """Paste an RGBA image with alpha and scale; anchor 'lt' (left top) or 'mm' (centre)."""
    if alpha <= 0.003 or scale <= 0.01:
        return
    if scale != 1.0:
        img = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), Image.BILINEAR)
    if anchor == "mm":
        x, y = x - img.width / 2, y - img.height / 2
    if alpha < 0.999:
        a = img.getchannel("A").point(lambda v: int(v * alpha))
        img = img.copy()
        img.putalpha(a)
    frame.alpha_composite(img, (round(x), round(y)))


def rounded(img, radius, border=None, shadow=True):
    """An image with rounded corners, an optional 2px border and a soft shadow (returns RGBA, larger)."""
    img = img.convert("RGBA")
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, img.width - 1, img.height - 1), radius=radius, fill=255)
    img.putalpha(mask)
    pad = 60 if shadow else 0
    out = Image.new("RGBA", (img.width + 2 * pad, img.height + 2 * pad), (0, 0, 0, 0))
    if shadow:
        sh = Image.new("L", out.size, 0)
        ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 18, pad + img.width, pad + img.height + 18), radius=radius, fill=170)
        sh = sh.filter(ImageFilter.GaussianBlur(26))
        out.paste(Image.new("RGBA", out.size, (0, 0, 0, 255)), (0, 0), sh)
    out.alpha_composite(img, (pad, pad))
    if border:
        ImageDraw.Draw(out).rounded_rectangle((pad, pad, pad + img.width - 1, pad + img.height - 1), radius=radius,
                                              outline=border, width=2)
    return out


def background():
    """The app's background: BgLow at the top to Bg at the bottom."""
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        k = y / H
        d.line([(0, y), (W, y)], fill=tuple(int(BG_LOW[c] + (BG[c] - BG_LOW[c]) * k) for c in range(3)))
    return img.convert("RGBA")


BACKGROUND = background()


# ---- material ------------------------------------------------------------------------------
def screen(name, width=1260):
    img = Image.open(MATERIAL / "screens" / f"{name}.png").convert("RGB")
    img = img.resize((width, round(width * 9 / 16)), Image.LANCZOS)
    return rounded(img, 18, border=LINE)


def app_icon(name, size):
    return rounded(Image.open(ICONS / f"{name}.png").resize((size, size), Image.LANCZOS), size // 9, border=None)


def game_icon(file, size, shadow=True):
    return rounded(Image.open(MATERIAL / "icons" / file).convert("RGB").resize((size, size), Image.LANCZOS),
                   size // 12, shadow=shadow)


# game icons as chosen on the PS4 (no Nintendo games)
PRINCE, CRASH, CHICKEN, DONALD = ("e820c027c6d1a458.png", "629cbbeb3a75b802.png", "427c3ca440cce7ef.png",
                                  "c1717786ead90bc2.png")


# ---- scene parts ---------------------------------------------------------------------------
STEPS = ["Find", "Artwork", "Name", "Install", "Play"]
_header_icon = None


def pill(text, fill, colour, size=26, pad=(22, 9)):
    t = text_image([(text, colour)], size)
    img = Image.new("RGBA", (t.width + 2 * pad[0], t.height + 2 * pad[1]), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle((0, 0, img.width - 1, img.height - 1), radius=img.height // 2, fill=fill)
    img.alpha_composite(t, pad)
    return img


class Heading:
    """The app's header: icon, title and a grey line under it, top left; on the step scenes the tab bar
    (L1 Find Artwork Name Install Play R1) with the current step as the lit tab."""

    def __init__(self, step, heading_parts, sub, y=40, size=64):
        global _header_icon
        if _header_icon is None:
            _header_icon = Image.open(ICONS / "emuforwarder.png").convert("RGB").resize((96, 96), Image.LANCZOS).convert("RGBA")
        self.heading = text_image(heading_parts, size)
        self.sub = text_image([(sub, GREY)], 30) if sub else None
        self.step = step
        self.tabs = None
        if step is not None:
            parts = [pill("L1", CARD_HI, GREY, 20, (12, 6))]
            for k, name in enumerate(STEPS):
                parts.append(pill(name, WHITE + (255,), BG, 28) if k == step else text_image([(name, GREY)], 28))
            parts.append(pill("R1", CARD_HI, GREY, 20, (12, 6)))
            width = sum(p.width for p in parts) + 30 * (len(parts) - 1)
            tabs = Image.new("RGBA", (width, max(p.height for p in parts)), (0, 0, 0, 0))
            x = 0
            for p in parts:
                tabs.alpha_composite(p, (x, (tabs.height - p.height) // 2))
                x += p.width + 30
            self.tabs = tabs
        self.y = y

    def draw(self, frame, t, alpha=1.0):
        p = ease(phase(t, 0.0, 0.5))
        put(frame, _header_icon, 96, self.y + 10, p * alpha)
        p = ease(phase(t, 0.1, 0.6))
        put(frame, self.heading, 216 + 20 * (1 - p), self.y + 6, p * alpha)
        if self.sub:
            p = ease(phase(t, 0.25, 0.75))
            put(frame, self.sub, 218 + 20 * (1 - p), self.y + 16 + self.heading.height, p * alpha)
        if self.tabs:
            p = ease(phase(t, 0.2, 0.7))
            put(frame, self.tabs, 96, self.y + 136, p * alpha)


VERSION_TAG = text_image([("Emulator game forwarder  -  v1.0.1", GREY)], 22)
_hint = None


def version_tag(frame, alpha):
    """The app's footer: a line, the version left, a button hint right."""
    global _hint
    if _hint is None:
        label = text_image([("Next", WHITE)], 24)
        _hint = Image.new("RGBA", (label.width + 52, 34), (0, 0, 0, 0))
        d = ImageDraw.Draw(_hint)
        d.ellipse((2, 2, 31, 31), outline=(110, 150, 235), width=3)
        d.line((10, 10, 23, 23), fill=(110, 150, 235), width=3)
        d.line((23, 10, 10, 23), fill=(110, 150, 235), width=3)
        _hint.alpha_composite(label, (44, 4))
    a = int(255 * alpha)
    if a <= 0:
        return
    ImageDraw.Draw(frame).line([(96, 1000), (W - 96, 1000)], fill=LINE + (a,), width=2)
    put(frame, VERSION_TAG, 96, 1022, alpha)
    put(frame, _hint, W - 96 - _hint.width, 1018, alpha)


def screens_scene(heading, shots, duration):
    """A heading with app screens below; shots: [(start time, screen image)] cross-fading."""
    def draw(frame, t):
        out = 1 - phase(t, duration - 0.35, duration)
        heading.draw(frame, t, out)
        p = ease(phase(t, 0.35, 1.0))
        zoom = 1.0 + 0.035 * t / duration
        for i, (start, shot) in enumerate(shots):
            nxt = shots[i + 1][0] if i + 1 < len(shots) else None
            a = phase(t, start, start + 0.35) if i else 1.0
            if nxt is not None and t >= nxt + 0.35:
                continue
            put(frame, shot, W / 2, 268 + shot.height / 2 - 60 + 40 * (1 - p), p * a * out, zoom, anchor="mm")
        version_tag(frame, out)
    return draw


# ---- scenes ---------------------------------------------------------------------------------
def scene_intro():
    frames = sorted((MATERIAL / "intro").glob("f_emuforwarder_*.png"))
    images = [Image.open(f).convert("RGBA") for f in frames]

    def draw(frame, t):
        i = int(t * FPS)
        if i < len(images):
            frame.paste(images[i], (0, 0))
    return draw, 4.0  # the intro (2.9 s), then a moment of black


def cover_square(path, size):
    """A box art's middle as a square (this also drops the band at the top of the box)."""
    img = Image.open(path).convert("RGB")
    side = min(img.size)
    left, top = (img.width - side) // 2, (img.height - side) // 2
    return rounded(img.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS), size // 12)


def badge(text, fill=ACCENT_DEEP, colour=WHITE):
    return pill(text, fill + (255,), colour, 20, (12, 5))


def title_block(frame, t, out, icon, y):
    """Icon, name and the app's subtitle, centred."""
    p = ease_back(phase(t, 0.0, 0.8))
    put(frame, icon, W / 2, y, clamp(p * 1.3) * out, 0.85 + 0.15 * p, anchor="mm")
    p = ease(phase(t, 0.4, 1.0))
    put(frame, TITLE_NAME, W / 2, y + 190 + 16 * (1 - p), p * out, anchor="mm")
    put(frame, TITLE_SUB, W / 2, y + 268 + 16 * (1 - p), p * out, anchor="mm")


TITLE_NAME = text_image([("EmuForwarder", WHITE)], 104)
TITLE_SUB = text_image([("Emulator game forwarder  -  v1.0.1", GREY)], 34)


def scene_title():
    icon = app_icon("emuforwarder", 230)
    line = text_image([("Your emulator games, right on your ", WHITE), ("PS4 home screen.", ACCENT)], 46)
    duration = 6.0

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        title_block(frame, t, out, icon, 300)
        p = ease(phase(t, 1.1, 1.8))
        put(frame, line, W / 2, 760 + 16 * (1 - p), p * out, anchor="mm")
    return draw, duration


def scene_library():
    h = Heading(0, [("Every game in ", WHITE), ("one library", ACCENT)],
                "GameCube games and everything Retrocade downloaded.")
    return screens_scene(h, [(0, screen("lib")), (4.6, screen("menu"))], 8.0), 8.0


def scene_artwork():
    h = Heading(1, [("Pick the ", WHITE), ("perfect icon", ACCENT)],
                "From SteamGridDB, GameTDB and libretro. Move and zoom until it's right.")
    return screens_scene(h, [(0, screen("grid")), (2.4, screen("grid2")), (4.8, screen("editor"))], 9.0), 9.0


def scene_name():
    h = Heading(2, [("Its ", WHITE), ("own name", ACCENT)], "Keep the game's name, or type your own.")
    return screens_scene(h, [(0, screen("name"))], 7.0), 7.0


def scene_install():
    h = Heading(3, [("One press. ", WHITE), ("Installed.", ACCENT)],
                "EmuForwarder builds and installs the tile, right on the console.")
    shots = [(0, screen("install0")), (1.4, screen("install1")), (2.6, screen("install2")), (3.6, screen("install3")),
             (4.6, screen("installed"))]
    return screens_scene(h, shots, 8.0), 8.0


def scene_home():
    """The home screen: the new tile slides in, gets focus and opens."""
    duration = 10.0
    size, big, gap, y = 196, 300, 26, 520
    tiles = [app_icon("emuforwarder", size), app_icon("efupdater", size)] + [game_icon(f, size) for f in (PRINCE, CHICKEN, DONALD)]
    hero_big = game_icon(CRASH, big)
    hero_full = Image.open(MATERIAL / "icons" / CRASH).convert("RGB").resize((W, W), Image.LANCZOS)
    title = text_image([("Crash Nitro Kart", WHITE)], 40)
    hint_text = text_image([("Starting in DolphinPS4...", WHITE)], 30)
    hint = Image.new("RGBA", (hint_text.width + 64, hint_text.height + 32), (0, 0, 0, 0))
    ImageDraw.Draw(hint).rounded_rectangle((0, 0, hint.width - 1, hint.height - 1), radius=hint.height // 2,
                                           fill=CARD + (235,), outline=ACCENT, width=3)
    hint.alpha_composite(hint_text, (32, 16))
    caption = Heading(4, [("Straight into ", WHITE), ("the game", ACCENT)],
                      "No menus in between: the tile opens the game in its emulator.")

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        caption.draw(frame, t, out * (1 - phase(t, 6.4, 6.9)))
        slide = ease_back(phase(t, 1.0, 1.8))
        focus = ease(phase(t, 2.2, 2.9))
        launch = ease(phase(t, 6.4, 7.6))
        x = 140
        for i, tile in enumerate([None] + tiles):
            if i == 0:
                w = size + (big - size) * focus
                if slide > 0:
                    sc = w / big                                   # hero_big is big + 2*60 shadow pad
                    yy = y - (w - size) + 80 * (1 - slide)         # grows upward, bottom stays in line
                    a = clamp(slide * 1.4) * (1 - launch) * out
                    put(frame, hero_big, x - 60 * sc, yy - 60 * sc, a, sc)
                    if focus > 0:  # the app's selection: an accent frame
                        ImageDraw.Draw(frame).rounded_rectangle((x - 8, yy - 8, x + w + 8, yy + w + 8), radius=int(w / 12) + 8,
                                                                outline=ACCENT + (int(255 * focus * a),), width=6)
                        put(frame, title, x, y + size + 30, focus * (1 - launch) * out)
                x += (w + gap) * clamp(slide * 1.2)
            else:
                put(frame, tile, x - 60, y - 60, out * (1 - launch))
                x += size + gap
        if launch > 0:
            # the game's artwork grows from the tile to the screen, like a game starting
            s = big / W + (1 - big / W) * launch
            img = hero_full.resize((round(W * s), round(W * s)), Image.BILINEAR).convert("RGBA")
            cx = 140 + big / 2 + (W / 2 - 140 - big / 2) * launch
            cy = y - (big - size) + big / 2 + (H / 2 - (y - (big - size) + big / 2)) * launch
            put(frame, img, cx, cy, out, anchor="mm")
            frame.alpha_composite(Image.new("RGBA", (W, H), (0, 0, 0, int(110 * phase(t, 7.6, 8.2)))))
            put(frame, hint, W / 2, H - 140, phase(t, 7.8, 8.4) * out, anchor="mm")
        else:
            version_tag(frame, out)
    return draw, duration


def scene_emulators():
    duration = 8.0
    h = Heading(None, [("Two ", WHITE), ("emulators", ACCENT), (" today", WHITE)], "What EmuForwarder 1.0.1 works with.")

    def card(name, systems, selected):
        c = Image.new("RGBA", (660, 330), (0, 0, 0, 0))
        d = ImageDraw.Draw(c)
        d.rounded_rectangle((0, 0, 659, 329), radius=22, fill=CARD_HI if selected else CARD,
                            outline=ACCENT if selected else LINE, width=5 if selected else 2)
        c.alpha_composite(badge("EMULATOR"), (44, 44))
        c.alpha_composite(text_image([(name, WHITE)], 70), (42, 98))
        y = 210
        for line in systems:
            c.alpha_composite(text_image([(line, GREY)], 32), (44, y))
            y += 46
        return rounded(c, 22, shadow=True)
    cards = [(card("DolphinPS4", ["GameCube games"], False), card("DolphinPS4", ["GameCube games"], True)),
             (card("EmuSuNi", ["Retrocade games:", "DS, GBA and more"], False), card("EmuSuNi", ["Retrocade games:", "DS, GBA and more"], True))]

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        for i, (plain, lit) in enumerate(cards):
            p = ease(phase(t, 0.7 + i * 0.35, 1.4 + i * 0.35))
            sel = 1.0 if (t < 4.3) == (i == 0) else 0.0   # the selection moves like in the app
            x, y = W / 2 + (i - 0.5) * 740, 610 + 50 * (1 - p)
            put(frame, plain, x, y, p * out * (1 - sel), anchor="mm")
            put(frame, lit, x, y, p * out * sel, anchor="mm")
        version_tag(frame, out)
    return draw, duration


def scene_real_games():
    duration = 6.0
    h = Heading(None, [("Forwards are ", WHITE), ("real games", ACCENT)], "They even show up in apps like Itemzflow.")
    art = MATERIAL / "art"
    icons = [game_icon(f, 170) for f in (CRASH, PRINCE, CHICKEN, DONALD)] + \
            [cover_square(art / f"{c}.png", 170) for c in ("G9RP7D", "GP2EAF", "GYRE41")]
    tag = badge("GAME")

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        total = len(icons) * 196 - 26
        for i, ic in enumerate(icons):
            p = ease_back(phase(t, 0.6 + i * 0.12, 1.1 + i * 0.12))
            x = W / 2 - total / 2 + i * 196 + 85
            put(frame, ic, x, 600, clamp(p * 1.5) * out, 0.85 + 0.15 * p, anchor="mm")
            q = phase(t, 1.8 + i * 0.1, 2.2 + i * 0.1)
            put(frame, tag, x, 725, q * out, anchor="mm")
        version_tag(frame, out)
    return draw, duration


def scene_updates():
    duration = 6.0
    h = Heading(None, [("Updates ", WHITE), ("arrive by themselves", ACCENT)],
                "EmuForwarder finds them, EFUpdater installs them in one go.")
    ef, up = app_icon("emuforwarder", 260), app_icon("efupdater", 260)
    v1 = text_image([("v1.0.1", GREY)], 34)
    v2 = text_image([("v1.1.0", ACCENT)], 34)

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        p = ease(phase(t, 0.6, 1.2))
        put(frame, ef, W / 2 - 330, 600, p * out, anchor="mm")
        q = ease(phase(t, 1.0, 1.6))
        put(frame, up, W / 2 + 330, 600, q * out, anchor="mm")
        k = phase(t, 1.8, 3.0)
        if 0 < k < 1:
            d = ImageDraw.Draw(frame)
            for j in range(5):
                kk = clamp(k - j * 0.05)
                xx = W / 2 - 160 + 320 * ease(kk)
                d.ellipse((xx - 9, 591, xx + 9, 609), fill=ACCENT + (int(255 * (1 - j * 0.18)),))
        put(frame, v1, W / 2 - 330, 790, p * out * (1 - phase(t, 3.0, 3.4)), anchor="mm")
        put(frame, v2, W / 2 - 330, 790, phase(t, 3.0, 3.4) * out, anchor="mm")
        version_tag(frame, out)
    return draw, duration


def scene_roadmap():
    duration = 14.0
    h = Heading(None, [("What's ", WHITE), ("next", ACCENT)], "EmuForwarder is just getting started.")
    items = [
        (0.8, "More emulators", "Like RetroArch and Crema."),
        (4.8, "A PS5 version", "Work starts once the jailbreak for PS5 firmware 14.20 is out."),
        (8.6, "Smaller forwards", "About 200 MB today, because the emulator travels inside. Getting smaller."),
    ]
    rows = []
    for start, title, sub in items:
        c = Image.new("RGBA", (1300, 170), (0, 0, 0, 0))
        ImageDraw.Draw(c).rounded_rectangle((0, 0, 1299, 169), radius=20, fill=CARD, outline=LINE, width=2)
        rows.append((start, c, text_image([(title, WHITE)], 46), text_image([(sub, GREY)], 30)))
    numbers = [text_image([(f"{i + 1}", WHITE)], 40) for i in range(3)]

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        y = 290
        for i, (start, box, title, sub) in enumerate(rows):
            p = ease(phase(t, start, start + 0.7))
            x = 310 + 40 * (1 - p)
            put(frame, box, x, y, p * out)
            d = ImageDraw.Draw(frame)
            d.ellipse((x + 40, y + 45, x + 120, y + 125), fill=ACCENT + (int(255 * p * out),))
            put(frame, numbers[i], x + 80, y + 85, p * out, anchor="mm")
            put(frame, title, x + 160, y + 38, p * out)
            put(frame, sub, x + 162, y + 98, p * out)
            if i == 2 and p > 0:
                # the forward's size: a bar that shrinks
                k = ease(phase(t, start + 1.4, start + 4.5))
                full, now = 1000, 1000 * (1 - 0.6 * k)
                a = int(255 * p * out)
                d.rounded_rectangle((x + 160, y + 200, x + 160 + full, y + 220), radius=10, fill=CARD_HI + (a,))
                d.rounded_rectangle((x + 160, y + 200, x + 160 + now, y + 220), radius=10, fill=ACCENT + (a,))
                put(frame, text_image([("200 MB", GREY)], 26), x + 160 + full + 24, y + 196, p * out * (1 - k))
                put(frame, text_image([("smaller", ACCENT)], 26), x + 160 + full + 24, y + 196, p * out * k)
            y += 210
        version_tag(frame, out)
    return draw, duration


def scene_outro():
    duration = 8.5
    icon = app_icon("emuforwarder", 230)
    line = text_image([("It already works great, and it will ", WHITE), ("keep getting better.", ACCENT)], 44)
    link = text_image([("github.com/Arjanvb42/EmuForwarder", GREY)], 30)
    studio = text_image([("Studio van Batenburg", GREY)], 24)

    def draw(frame, t):
        out = 1 - phase(t, duration - 1.2, duration)
        title_block(frame, t, out, icon, 270)
        p = ease(phase(t, 1.2, 1.9))
        put(frame, line, W / 2, 720 + 16 * (1 - p), p * out, anchor="mm")
        p = ease(phase(t, 2.0, 2.6))
        put(frame, link, W / 2, 810, p * out, anchor="mm")
        put(frame, studio, W / 2, H - 80, p * out, anchor="mm")
    return draw, duration


def main():
    out_path = sys.argv[3]
    scenes = [scene_intro(), scene_title(), scene_library(), scene_artwork(), scene_name(), scene_install(),
              scene_home(), scene_emulators(), scene_real_games(), scene_updates(), scene_roadmap(), scene_outro()]
    total = sum(d for _, d in scenes)
    print(f"{len(scenes)} scenes, {total:.1f} s", flush=True)
    cmd = ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS),
           "-i", "-", "-ss", "0", "-i", sys.argv[2],
           "-filter_complex", f"[1:a]atrim=0:{total:.2f},afade=t=in:d=1.5,afade=t=out:st={total - 3.5:.2f}:d=3.5,volume=-6dB[a]",
           "-map", "0:v", "-map", "[a]", "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", "-shortest", out_path]
    ff = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    start = 0.0
    for n, (draw, duration) in enumerate(scenes):
        frames = round(duration * FPS)
        for i in range(frames):
            frame = BACKGROUND.copy()
            draw(frame, i / FPS)
            ff.stdin.write(frame.convert("RGB").tobytes())
        start += duration
        print(f"scene {n + 1} ends at {start:.1f} s", flush=True)
    ff.stdin.close()
    ff.wait()


if __name__ == "__main__":
    main()
