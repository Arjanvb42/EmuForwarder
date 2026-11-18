"""Fetch and prepare tile artwork (GameTDB covers) for a GameCube disc.

Used by build_tile.py. The cover becomes the square tile icon (icon0.png). A wide
banner (pic1.png) is only made on request: the system draws it as a backdrop over
the running game, so it is off by default.
"""

from io import BytesIO
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from PIL import Image, ImageOps

LIMIT = 12 * 1024 * 1024
BACKGROUND = "#132635"
# Region letter in the disc ID -> GameTDB region folder. Tried in order after the exact one.
REGIONS = {"E": "US", "P": "EN", "J": "JA", "D": "DE", "F": "FR", "S": "ES", "I": "IT", "U": "AU"}
FALLBACK_REGIONS = ["US", "EN", "JA", "DE", "FR", "ES", "IT", "AU"]


def cover_urls(disc_id):
    """Candidate GameTDB URLs for a GameCube disc (GameCube covers live under the wii folder)."""
    preferred = REGIONS.get(disc_id[3:4], "")
    order = [preferred] + [r for r in FALLBACK_REGIONS if r != preferred]
    seen = []
    for region in order:
        if region and region not in seen:
            seen.append(region)
    return [f"https://art.gametdb.com/wii/cover/{region}/{disc_id}.png" for region in seen]


def fetch_cover(disc_id):
    """Return the raw PNG bytes of the first cover GameTDB has, or None."""
    for url in cover_urls(disc_id):
        if urlparse(url).scheme != "https":
            continue
        request = Request(url, headers={"User-Agent": "PS4-Tegelmaker/0.2"})
        try:
            with urlopen(request, timeout=15) as response:
                data = response.read(LIMIT + 1)
        except OSError:
            continue
        if 0 < len(data) <= LIMIT:
            return data
    return None


def _fit(data, size):
    with Image.open(BytesIO(data)) as raw:
        source = ImageOps.exif_transpose(raw).convert("RGBA")
        background = Image.new("RGBA", source.size, BACKGROUND)
        background.alpha_composite(source)
        result = ImageOps.fit(background.convert("RGB"), size, Image.Resampling.LANCZOS)
    out = BytesIO()
    result.save(out, format="PNG", optimize=True)
    return out.getvalue()


def icon_png(data):
    """512x512 square icon from a cover image."""
    return _fit(data, (512, 512))


def banner_png(data):
    """1920x1080 banner from a cover image (only used when the backdrop is wanted)."""
    return _fit(data, (1920, 1080))
