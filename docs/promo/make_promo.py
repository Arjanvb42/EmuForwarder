#!/usr/bin/env python3
"""Builds the EmuForwarder 1.0.0 promo video (1920x1080, 30 fps), in the Studio van Batenburg style:
near-black, bold Montserrat headings, the website's colour gradient on the key words.

  python3 make_promo.py <material> <music.mp3> <out.mp4>

<material> holds:
  screens/   app screens rendered by promo_screens.cpp from the real ui.h (lib, menu, grid, editor,
             name, install0-3, installed)
  intro/     the app's intro, frame by frame (f_emuforwarder_000.png ...), from intro.h
  icons/     game icons as chosen on the PS4 (<hash>.png)
and the app icons come from native/icons. The timing follows voiceover-script.md.
"""
import math
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ICONS = HERE.parents[1] / "native" / "icons"
FONTS = Path("/usr/share/fonts/julietaula-montserrat-fonts")
W, H, FPS = 1920, 1080, 30
BG = (11, 12, 15)
WHITE = (240, 241, 244)
GREY = (150, 153, 162)
LINE = (42, 44, 52)
SITE_GRADIENT = [(79, 209, 197), (91, 141, 239), (181, 126, 220), (240, 160, 90)]  # teal, blue, purple, orange
BLUE = [(56, 196, 240), (18, 104, 205)]
ORANGE = [(255, 182, 56), (232, 88, 20)]

MATERIAL = Path(sys.argv[1])
_fonts = {}


def font(weight, size):
    key = (weight, size)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(str(FONTS / f"Montserrat-{weight}.otf"), size)
    return _fonts[key]


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
    """Near-black with two very soft colour lights, like the website."""
    img = Image.new("RGB", (W, H), BG)
    glow = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(glow)
    d.ellipse((-300, 650, 700, 1500), fill=34)
    glow = glow.filter(ImageFilter.GaussianBlur(180))
    img.paste(Image.new("RGB", (W, H), (40, 110, 230)), (0, 0), glow)
    glow2 = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow2).ellipse((1350, 700, 2300, 1500), fill=30)
    glow2 = glow2.filter(ImageFilter.GaussianBlur(180))
    img.paste(Image.new("RGB", (W, H), (230, 120, 40)), (0, 0), glow2)
    return img.convert("RGBA")


BACKGROUND = background()


# ---- material ------------------------------------------------------------------------------
def screen(name, width=1248):
    img = Image.open(MATERIAL / "screens" / f"{name}.png").convert("RGB")
    img = img.resize((width, round(width * 9 / 16)), Image.LANCZOS)
    return rounded(img, 22, border=LINE)


def app_icon(name, size):
    return rounded(Image.open(ICONS / f"{name}.png").resize((size, size), Image.LANCZOS), size // 9, border=None)


def game_icon(file, size, shadow=True):
    return rounded(Image.open(MATERIAL / "icons" / file).convert("RGB").resize((size, size), Image.LANCZOS),
                   size // 12, shadow=shadow)


MARIO, POKEMON, PRINCE, CRASH, SUNSHINE, CHICKEN, DONALD = (
    "371f58b92fdf3ed3.png", "b83aceb9591f7990.png", "e820c027c6d1a458.png", "629cbbeb3a75b802.png",
    "8f683f8e0d54c6b5.png", "427c3ca440cce7ef.png", "c1717786ead90bc2.png")


# ---- scene parts ---------------------------------------------------------------------------
class Heading:
    """Eyebrow, heading and subline at the top, sliding up into place."""

    def __init__(self, eyebrow, heading_parts, sub, y=70, size=80):
        self.eyebrow = text_image([(eyebrow, GREY)], 24, "SemiBold", tracking=5) if eyebrow else None
        self.heading = text_image(heading_parts, size)
        self.sub = text_image([(sub, GREY)], 34, "Medium") if sub else None
        self.y = y

    def draw(self, frame, t, alpha=1.0):
        y = self.y
        if self.eyebrow:
            p = ease(phase(t, 0.0, 0.5))
            put(frame, self.eyebrow, W / 2 - self.eyebrow.width / 2, y + 16 * (1 - p), p * alpha)
            y += 52
        p = ease(phase(t, 0.1, 0.65))
        put(frame, self.heading, W / 2 - self.heading.width / 2, y + 24 * (1 - p), p * alpha)
        y += self.heading.height + 26
        if self.sub:
            p = ease(phase(t, 0.3, 0.85))
            put(frame, self.sub, W / 2 - self.sub.width / 2, y + 16 * (1 - p), p * alpha)


VERSION_TAG = text_image([("EmuForwarder 1.0.0", GREY)], 22, "SemiBold", tracking=2)


def version_tag(frame, alpha):
    d = ImageDraw.Draw(frame)
    put(frame, VERSION_TAG, 64, H - 64 - VERSION_TAG.height, 0.8 * alpha)


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
            put(frame, shot, W / 2, 330 + shot.height / 2 - 60 + 40 * (1 - p), p * a * out, zoom, anchor="mm")
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


def scene_title():
    eyebrow = text_image([("EMUFORWARDER 1.0.0  ·  FOR PS4", GREY)], 24, "SemiBold", tracking=6)
    line1 = text_image([("Your emulator games.", WHITE)], 104)
    line2 = text_image([("Right on your home screen.", SITE_GRADIENT)], 104)
    duration = 6.0

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        p = ease(phase(t, 0.0, 0.6))
        put(frame, eyebrow, W / 2 - eyebrow.width / 2, 360 + 14 * (1 - p), p * out)
        p = ease(phase(t, 0.25, 0.95))
        put(frame, line1, W / 2 - line1.width / 2, 430 + 30 * (1 - p), p * out)
        p = ease(phase(t, 0.7, 1.4))
        put(frame, line2, W / 2 - line2.width / 2, 565 + 30 * (1 - p), p * out)
    return draw, duration


def scene_library():
    h = Heading("01  ·  FIND", [("Every game in ", WHITE), ("one library", SITE_GRADIENT)],
                "GameCube games and everything Retrocade downloaded.")
    return screens_scene(h, [(0, screen("lib")), (4.6, screen("menu"))], 8.0), 8.0


def scene_artwork():
    h = Heading("02  ·  ARTWORK", [("Pick the ", WHITE), ("perfect icon", SITE_GRADIENT)],
                "From SteamGridDB, GameTDB and libretro. Move and zoom until it's right.")
    return screens_scene(h, [(0, screen("grid")), (2.4, screen("grid2")), (4.8, screen("editor"))], 9.0), 9.0


def scene_name():
    h = Heading("03  ·  NAME", [("Its ", WHITE), ("own name", SITE_GRADIENT)], "Keep the game's name, or type your own.")
    return screens_scene(h, [(0, screen("name"))], 7.0), 7.0


def scene_install():
    h = Heading("04  ·  INSTALL", [("One press. ", WHITE), ("Installed.", SITE_GRADIENT)],
                "EmuForwarder builds and installs the tile, right on the console.")
    shots = [(0, screen("install0")), (1.4, screen("install1")), (2.6, screen("install2")), (3.6, screen("install3")),
             (4.6, screen("installed"))]
    return screens_scene(h, shots, 8.0), 8.0


def scene_home():
    """A console home screen (no brand): the new tile slides in, gets focus and opens."""
    duration = 10.0
    size, big, gap, y = 196, 300, 26, 470
    tiles = [app_icon(name, size) if kind is None else game_icon(name, size)
             for kind, name in [(None, "emuforwarder"), (None, "efupdater"), (1, POKEMON), (1, PRINCE), (1, CRASH),
                                (1, SUNSHINE)]]
    mario_big = game_icon(MARIO, big)
    mario_full = Image.open(MATERIAL / "icons" / MARIO).convert("RGB").resize((W, W), Image.LANCZOS)
    title = text_image([("Mario Kart DS", WHITE)], 40, "Bold")
    hint_text = text_image([("Starting in EmuSuNi...", WHITE)], 30, "SemiBold")
    hint = Image.new("RGBA", (hint_text.width + 64, hint_text.height + 32), (0, 0, 0, 0))
    ImageDraw.Draw(hint).rounded_rectangle((0, 0, hint.width - 1, hint.height - 1), radius=hint.height // 2,
                                           fill=(11, 12, 15, 225))
    hint.alpha_composite(hint_text, (32, 16))
    sky = Image.new("RGB", (W, H))
    top, bottom = (16, 32, 72), (5, 9, 22)
    for yy in range(H):
        k = yy / H
        ImageDraw.Draw(sky).line([(0, yy), (W, yy)], fill=tuple(int(top[c] + (bottom[c] - top[c]) * k) for c in range(3)))
    sky = sky.convert("RGBA")
    caption = Heading(None, [("Straight into ", WHITE), ("the game", SITE_GRADIENT)],
                      "No menus in between: the tile opens the game in its emulator.", y=80, size=72)

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        frame.alpha_composite(sky)
        caption.draw(frame, t - 0.3, out * (1 - phase(t, 6.4, 6.9)))
        slide = ease_back(phase(t, 1.0, 1.8))
        focus = ease(phase(t, 2.2, 2.9))
        launch = ease(phase(t, 6.4, 7.6))
        x = 140
        # the new tile goes first (focused), the others move aside for it
        for i, tile in enumerate([None] + tiles):
            if i == 0:
                w = size + (big - size) * focus
                if slide > 0:
                    sc = w / big                                   # mario_big is big + 2*60 shadow pad
                    yy = y - (w - size) + 80 * (1 - slide)         # grows upward, bottom stays in line
                    put(frame, mario_big, x - 60 * sc, yy - 60 * sc, clamp(slide * 1.4) * (1 - launch) * out, sc)
                    if focus > 0:
                        put(frame, title, x, y + size + 24, focus * (1 - launch) * out)
                x += (w + gap) * clamp(slide * 1.2)
            else:
                put(frame, tile, x - 60, y - 60, out * (1 - launch))
                x += size + gap
        if launch > 0:
            # the game's artwork grows from the tile to the screen, like a game starting
            s = big / W + (1 - big / W) * launch   # from the tile's size to covering the screen
            img = mario_full.resize((round(W * s), round(W * s)), Image.BILINEAR).convert("RGBA")
            cx = 140 + big / 2 + (W / 2 - 140 - big / 2) * launch
            cy = y - (big - size) + big / 2 + (H / 2 - (y - (big - size) + big / 2)) * launch
            put(frame, img, cx, cy, out, anchor="mm")
            veil = Image.new("RGBA", (W, H), (0, 0, 0, int(110 * phase(t, 7.6, 8.2))))
            frame.alpha_composite(veil)
            put(frame, hint, W / 2, H - 140, phase(t, 7.8, 8.4) * out, anchor="mm")
    return draw, duration


def scene_emulators():
    duration = 8.0
    h = Heading("WORKS TODAY WITH", [("Two ", WHITE), ("emulators", SITE_GRADIENT)], "In EmuForwarder 1.0.0.")

    def card(name, systems, colours):
        c = Image.new("RGBA", (640, 320), (0, 0, 0, 0))
        d = ImageDraw.Draw(c)
        d.rounded_rectangle((0, 0, 639, 319), radius=28, fill=(20, 21, 26), outline=LINE, width=2)
        bar = gradient_strip(colours, 560, 8)
        c.paste(bar, (40, 40))
        c.alpha_composite(text_image([(name, WHITE)], 64), (36, 82))
        y = 190
        for line in systems:
            c.alpha_composite(text_image([(line, GREY)], 32, "Medium"), (40, y))
            y += 46
        return rounded(c, 28, shadow=True)
    cards = [card("DolphinPS4", ["GameCube"], BLUE), card("EmuSuNi", ["Nintendo DS, Game Boy Advance", "and more"], ORANGE)]

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        for i, c in enumerate(cards):
            p = ease(phase(t, 0.7 + i * 0.35, 1.4 + i * 0.35))
            put(frame, c, W / 2 + (i - 0.5) * 720, 640 + 50 * (1 - p), p * out, anchor="mm")
        version_tag(frame, out)
    return draw, duration


def scene_real_games():
    duration = 6.0
    h = Heading("REAL GAMES", [("Forwards are ", WHITE), ("games", SITE_GRADIENT), (" to your PS4", WHITE)],
                "They even show up in apps like Itemzflow.")
    icons = [game_icon(f, 170) for f in (MARIO, POKEMON, PRINCE, CRASH, SUNSHINE, CHICKEN, DONALD)]
    tag = text_image([("GAME", (11, 12, 15))], 20, "Bold", tracking=2)

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        total = len(icons) * 196 - 26
        for i, ic in enumerate(icons):
            p = ease_back(phase(t, 0.6 + i * 0.12, 1.1 + i * 0.12))
            x = W / 2 - total / 2 + i * 196 + 85
            put(frame, ic, x, 640, clamp(p * 1.5) * out, 0.85 + 0.15 * p, anchor="mm")
            q = phase(t, 1.8 + i * 0.1, 2.2 + i * 0.1)
            if q > 0:
                pill = Image.new("RGBA", (tag.width + 28, tag.height + 16), (0, 0, 0, 0))
                ImageDraw.Draw(pill).rounded_rectangle((0, 0, pill.width - 1, pill.height - 1), radius=pill.height // 2,
                                                       fill=(222, 223, 226, 255))
                pill.alpha_composite(tag, (14, 8))
                put(frame, pill, x, 760, q * out, anchor="mm")
        version_tag(frame, out)
    return draw, duration


def scene_updates():
    duration = 6.0
    h = Heading("UPDATES", [("New versions ", WHITE), ("arrive by themselves", SITE_GRADIENT)],
                "EmuForwarder finds them, EFUpdater installs them in one go.")
    ef, up = app_icon("emuforwarder", 260), app_icon("efupdater", 260)
    v1 = text_image([("1.0.0", GREY)], 34, "Bold")
    v2 = text_image([("1.0.1", ORANGE)], 34, "Bold")

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        p = ease(phase(t, 0.6, 1.2))
        put(frame, ef, W / 2 - 330, 640, p * out, anchor="mm")
        q = ease(phase(t, 1.0, 1.6))
        put(frame, up, W / 2 + 330, 640, q * out, anchor="mm")
        # a dot travelling from one to the other, then the version ticking over
        k = phase(t, 1.8, 3.0)
        if 0 < k < 1:
            d = ImageDraw.Draw(frame)
            for j in range(5):
                kk = clamp(k - j * 0.05)
                xx = W / 2 - 160 + 320 * ease(kk)
                d.ellipse((xx - 9, 631, xx + 9, 649), fill=(255, 182, 56, int(255 * (1 - j * 0.18))))
        put(frame, v1, W / 2 - 330, 830, p * out * (1 - phase(t, 3.0, 3.4)), anchor="mm")
        put(frame, v2, W / 2 - 330, 830, phase(t, 3.0, 3.4) * out, anchor="mm")
        version_tag(frame, out)
    return draw, duration


def scene_roadmap():
    duration = 14.0
    h = Heading("WHAT'S NEXT", [("Just ", WHITE), ("getting started", SITE_GRADIENT)], None)
    items = [
        (0.8, "More emulators", "Like RetroArch and Crema."),
        (4.8, "A PS5 version", "Work starts once the jailbreak for PS5 firmware 14.20 is out."),
        (8.6, "Smaller forwards", "About 200 MB today, because the emulator travels inside. Getting smaller."),
    ]
    rows = [(start, text_image([(title, WHITE)], 50, "ExtraBold"), text_image([(sub, GREY)], 30, "Medium"))
            for start, title, sub in items]
    number = [text_image([(f"0{i + 1}", SITE_GRADIENT)], 50, "ExtraBold") for i in range(3)]

    def draw(frame, t):
        out = 1 - phase(t, duration - 0.4, duration)
        h.draw(frame, t, out)
        y = 330
        for i, (start, title, sub) in enumerate(rows):
            p = ease(phase(t, start, start + 0.7))
            x = 470 + 40 * (1 - p)
            put(frame, number[i], x, y, p * out)
            put(frame, title, x + 110, y, p * out)
            put(frame, sub, x + 112, y + 70, p * out)
            if i == 2 and p > 0:
                # the forward's size: a bar that shrinks
                k = ease(phase(t, start + 1.4, start + 4.5))
                full, now = 760, 760 * (1 - 0.6 * k)
                d = ImageDraw.Draw(frame)
                d.rounded_rectangle((x + 112, y + 130, x + 112 + full, y + 150), radius=10, fill=(30, 32, 38, int(255 * p * out)))
                bar = gradient_strip(SITE_GRADIENT, max(2, int(now)), 20).convert("RGBA")
                m = Image.new("L", bar.size, 0)
                ImageDraw.Draw(m).rounded_rectangle((0, 0, bar.width - 1, 19), radius=10, fill=int(255 * p * out))
                bar.putalpha(m)
                frame.alpha_composite(bar, (round(x + 112), y + 130))
                mb = text_image([("200 MB", GREY)], 26, "SemiBold")
                smaller = text_image([("smaller", SITE_GRADIENT)], 26, "Bold")
                put(frame, mb, x + 112 + full + 24, y + 128, p * out * (1 - k))
                put(frame, smaller, x + 112 + full + 24, y + 128, p * out * k)
            y += 210
        version_tag(frame, out)
    return draw, duration


def scene_outro():
    duration = 8.5
    icon = app_icon("emuforwarder", 300)
    name = text_image([("Emu", WHITE), ("Forwarder", BLUE)], 92)
    version = text_image([("1.0.0", GREY)], 40, "Bold")
    line = text_image([("It already works great, and it will ", WHITE), ("keep getting better.", SITE_GRADIENT)], 44, "Bold")
    link = text_image([("github.com/Arjanvb42/EmuForwarder", GREY)], 30, "SemiBold")
    studio = text_image([("STUDIO VAN BATENBURG", GREY)], 22, "SemiBold", tracking=6)

    def draw(frame, t):
        out = 1 - phase(t, duration - 1.2, duration)
        p = ease_back(phase(t, 0.0, 0.8))
        put(frame, icon, W / 2, 300, clamp(p * 1.3) * out, 0.85 + 0.15 * p, anchor="mm")
        p = ease(phase(t, 0.5, 1.1))
        put(frame, name, W / 2 - (name.width + 24 + version.width) / 2, 500 + 20 * (1 - p), p * out)
        put(frame, version, W / 2 + (name.width + 24 + version.width) / 2 - version.width, 532 + 20 * (1 - p), p * out)
        p = ease(phase(t, 1.2, 1.9))
        put(frame, line, W / 2, 690 + 16 * (1 - p), p * out, anchor="mm")
        p = ease(phase(t, 2.0, 2.6))
        put(frame, link, W / 2, 790, p * out, anchor="mm")
        put(frame, studio, W / 2, H - 90, p * out, anchor="mm")
    return draw, duration


def main():
    out_path = sys.argv[3]
    scenes = [scene_intro(), scene_title(), scene_library(), scene_artwork(), scene_name(), scene_install(),
              scene_home(), scene_emulators(), scene_real_games(), scene_updates(), scene_roadmap(), scene_outro()]
    total = sum(d for _, d in scenes)
    print(f"{len(scenes)} scenes, {total:.1f} s", flush=True)
    cmd = ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS),
           "-i", "-", "-ss", "0", "-i", sys.argv[2],
           "-filter_complex", f"[1:a]atrim=0:{total:.2f},afade=t=in:d=1.5,afade=t=out:st={total - 3.5:.2f}:d=3.5,volume=-2dB[a]",
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
