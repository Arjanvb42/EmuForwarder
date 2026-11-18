#pragma once
#include "lang.h"
// EmuForwarder's look: a dark, card-based library like the PS5 home screen and modern homebrew
// stores. Everything here only draws; main.cpp fills a LibraryView / EditorView each frame.
// Needs (declared before including): Scene2D *scene, struct Color, and FreeType.

#include <algorithm>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" int ef_last_rect[4];  // the rectangle being drawn, for the crash report

namespace ui {

// ---- Theme -------------------------------------------------------------------------------
static const Color Bg{9, 12, 22}, BgLow{14, 20, 38}, Card{22, 28, 46}, CardHi{30, 40, 66};
static const Color Accent{38, 132, 255}, AccentDeep{20, 64, 150}, Text{240, 244, 252}, Sub{146, 160, 190};
static const Color Good{70, 214, 150}, Warn{255, 196, 92}, Line{40, 50, 78};
static const Color Thumb{17, 22, 38};
static constexpr int ScreenW = 1920, ScreenH = 1080;
static constexpr int ThumbSize = 232;

static FT_Face font_title, font_large, font_body, font_small;

static Color mix(Color a, Color b, unsigned t)  // t: 0..255 of b
{
    return Color{uint8_t((a.r * (255 - t) + b.r * t + 127) / 255), uint8_t((a.g * (255 - t) + b.g * t + 127) / 255),
                 uint8_t((a.b * (255 - t) + b.b * t + 127) / 255)};
}

// The frame buffer of the frame being drawn (set by the app each frame): drawing writes straight into
// it, which is many times faster than a function call per pixel.
static uint32_t *canvas = nullptr;

static inline uint32_t encode(Color c) { return 0x80000000u | uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | c.b; }

static inline void pixel(int x, int y, Color c)
{
    if (x >= 0 && y >= 0 && x < ScreenW && y < ScreenH)
        canvas[y * ScreenW + x] = encode(c);
}

// Called when something would be drawn outside the screen (the PS4's DrawRectangle doesn't check).
static void (*on_offscreen)(const char *where, int x, int y, int w, int h) = nullptr;
static const char *drawing = "";

// Every rectangle goes through here: clipped to the screen, never outside the frame buffer.
static void rect(int x, int y, int w, int h, Color c)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < 0 || y < 0 || x + w > ScreenW || y + h > ScreenH) {
        if (on_offscreen)
            on_offscreen(drawing, x, y, w, h);
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(ScreenW, x + w), y1 = std::min(ScreenH, y + h);
        if (x1 <= x0 || y1 <= y0)
            return;
        x = x0, y = y0, w = x1 - x0, h = y1 - y0;
    }
    ef_last_rect[0] = x, ef_last_rect[1] = y, ef_last_rect[2] = w, ef_last_rect[3] = h;
    const uint32_t value = encode(c);
    for (int row = y; row < y + h; ++row)
        std::fill(canvas + row * ScreenW + x, canvas + row * ScreenW + x + w, value);
}

// The background is a soft vertical gradient; text on it blends with this colour.
static Color background_at(int y) { return mix(BgLow, Bg, unsigned(std::min(255, y * 255 / ScreenH))); }

static void background()
{
    for (int y = 0; y < ScreenH; ++y)
        rect(0, y, ScreenW, 1, background_at(y));
}

// Rounded rectangle with smoothed corners; `under` is what the corners blend into.
static void round_rect(int x, int y, int w, int h, int r, Color fill, Color under)
{
    r = std::min(r, std::min(w, h) / 2);
    for (int row = 0; row < h; ++row) {
        int inset = 0;
        float edge = 0;
        const int dy = row < r ? r - row : (row >= h - r ? row - (h - r - 1) : 0);
        if (dy) {
            const float span = sqrtf(float(r * r) - float((dy - 0.5f) * (dy - 0.5f)));
            inset = int(r - span);
            edge = (r - span) - inset;  // coverage of the first pixel
        }
        if (inset * 2 >= w)
            continue;
        rect(x + inset, y + row, w - inset * 2, 1, fill);
        if (dy && inset > 0) {
            const Color soft = mix(fill, under, unsigned(edge * 255));
            pixel(x + inset, y + row, soft);
            pixel(x + w - inset - 1, y + row, soft);
        }
    }
}

static void round_outline(int x, int y, int w, int h, int r, int thickness, Color color, Color under)
{
    round_rect(x, y, w, h, r, color, under);
}

// ---- Text --------------------------------------------------------------------------------
// Rendered letters, kept: FreeType renders each letter of each font only once.
struct Glyph {
    int left = 0, top = 0, advance = 0, width = 0, rows = 0;
    std::vector<uint8_t> alpha;
};

static const Glyph &glyph(FT_Face font, unsigned char c)
{
    static std::unordered_map<uint64_t, Glyph> cache;
    const uint64_t key = uint64_t(reinterpret_cast<uintptr_t>(font)) << 8 | c;
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    Glyph &g = cache[key];
    if (FT_Load_Char(font, c, FT_LOAD_RENDER) == 0) {
        const FT_GlyphSlot slot = font->glyph;
        const auto &bitmap = slot->bitmap;
        g.left = slot->bitmap_left;
        g.top = slot->bitmap_top;
        g.advance = int(slot->advance.x >> 6);
        if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY && bitmap.buffer) {
            g.width = int(bitmap.width);
            g.rows = int(bitmap.rows);
            const int pitch = bitmap.pitch < 0 ? -bitmap.pitch : bitmap.pitch;
            g.alpha.resize(size_t(g.width) * g.rows);
            for (int y = 0; y < g.rows; ++y)
                memcpy(&g.alpha[size_t(y) * g.width], bitmap.buffer + y * pitch, g.width);
        }
    }
    return g;
}

static int glyph_advance(FT_Face font, unsigned char c) { return glyph(font, c).advance; }

static unsigned char ascii(const unsigned char *&at)
{
    unsigned char value = *at;
    if (value >= 128) {  // UTF-8: show common Latin letters plainly, others as '?'
        unsigned code = 0;
        if ((value & 0xe0) == 0xc0 && at[1]) {
            code = ((value & 0x1f) << 6) | (at[1] & 0x3f);
            ++at;
        } else {
            while (at[1] && (at[1] & 0xc0) == 0x80)
                ++at;
        }
        static const char *const latin = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
        return code >= 0xc0 && code <= 0xff ? latin[code - 0xc0] : '?';
    }
    return value;
}

static int text_width(const char *label, FT_Face font)
{
    int width = 0;
    for (const unsigned char *at = reinterpret_cast<const unsigned char *>(label); *at; ++at) {
        const unsigned char c = ascii(at);
        if (c >= 32)
            width += glyph_advance(font, c);
    }
    return width;
}

// Draws `label` from `left` on `baseline`; stops with "..." before `right`. Returns the end x.
static int text(const char *label, FT_Face font, int left, int baseline, int right, Color bg, Color fg,
                bool bold = false)
{
    const int dots = glyph_advance(font, '.') * 3;
    const bool fits = left + text_width(label, font) <= right;
    int cursor = left;
    auto draw_char = [&](unsigned char c) {
        const Glyph &g = glyph(font, c);
        const int x0 = cursor + g.left, y0 = baseline - g.top;
        for (int y = 0; y < g.rows; ++y) {
            const uint8_t *row = &g.alpha[size_t(y) * g.width];
            for (int x = 0; x < g.width; ++x) {
                const unsigned a = row[x];
                if (!a)
                    continue;
                const Color shade = mix(bg, fg, a);
                pixel(x0 + x, y0 + y, shade);
                if (bold)
                    pixel(x0 + x + 1, y0 + y, shade);
            }
        }
        cursor += g.advance + (bold ? 1 : 0);
    };
    for (const unsigned char *at = reinterpret_cast<const unsigned char *>(label); *at; ++at) {
        const unsigned char c = ascii(at);
        if (c < 32)
            continue;
        if (!fits && cursor + glyph_advance(font, c) + dots > right) {
            for (int i = 0; i < 3; ++i)
                draw_char('.');
            break;
        }
        draw_char(c);
    }
    return cursor;
}

static void text_center(const char *label, FT_Face font, int center, int baseline, Color bg, Color fg, bool bold = false)
{
    const int width = text_width(label, font);
    text(label, font, center - width / 2, baseline, center + width / 2 + 40, bg, fg, bold);
}

// ---- Pictures ----------------------------------------------------------------------------
// Square RGB picture with rounded corners (no smoothing needed: the card edge hides it).
static void picture(const uint8_t *rgb, int size, int x, int y, int radius)
{
    for (int row = 0; row < size; ++row) {
        int inset = 0;
        const int dy = row < radius ? radius - row : (row >= size - radius ? row - (size - radius - 1) : 0);
        if (dy)
            inset = int(radius - sqrtf(float(radius * radius) - float((dy - 0.5f) * (dy - 0.5f))));
        const uint8_t *line = rgb + size_t(row) * size * 3;
        const int py = y + row;
        if (py < 0 || py >= ScreenH)
            continue;
        uint32_t *out = canvas + py * ScreenW;
        for (int col = std::max(inset, -x); col < size - inset && x + col < ScreenW; ++col)
            out[x + col] = 0x80000000u | uint32_t(line[col * 3]) << 16 | uint32_t(line[col * 3 + 1]) << 8 | line[col * 3 + 2];
    }
}

// ---- Controller glyphs -------------------------------------------------------------------
enum class Button { Cross, Circle, Triangle, Square, L1, R1, L2, R2, Options, DPad };

static void disc(int cx, int cy, int r, Color fill, Color under)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x) {
            const float d = sqrtf(float(x * x + y * y));
            if (d <= r - 0.5f)
                pixel(cx + x, cy + y, fill);
            else if (d <= r + 0.5f)
                pixel(cx + x, cy + y, mix(fill, under, unsigned((d - (r - 0.5f)) * 255)));
        }
}

static void thick_line(float x0, float y0, float x1, float y1, float width, Color color, int cx, int cy, int r)
{
    for (int y = cy - r; y <= cy + r; ++y)
        for (int x = cx - r; x <= cx + r; ++x) {
            const float px = x - x0, py = y - y0, dx = x1 - x0, dy = y1 - y0;
            const float t = std::max(0.0f, std::min(1.0f, (px * dx + py * dy) / (dx * dx + dy * dy)));
            const float ex = px - t * dx, ey = py - t * dy;
            if (sqrtf(ex * ex + ey * ey) <= width / 2)
                pixel(x, y, color);
        }
}

// Draws a button glyph centred at (cx, cy); returns its width.
static int button(Button b, int cx, int cy, Color under)
{
    const Color face{44, 52, 78};
    if (b == Button::L1 || b == Button::R1 || b == Button::L2 || b == Button::R2 || b == Button::Options) {
        const char *label = b == Button::L1 ? "L1" : b == Button::R1 ? "R1" : b == Button::L2 ? "L2"
                           : b == Button::R2 ? "R2" : "OPTIONS";
        const int w = text_width(label, font_small) + 24;
        round_rect(cx - w / 2, cy - 17, w, 34, 17, face, under);
        text(label, font_small, cx - w / 2 + 12, cy + 9, cx + w / 2, face, Text, true);
        return w;
    }
    const int r = 17;
    disc(cx, cy, r, face, under);
    if (b == Button::DPad) {
        rect(cx - 3, cy - 11, 6, 22, Text);
        rect(cx - 11, cy - 3, 22, 6, Text);
    } else if (b == Button::Cross) {
        const Color c{126, 176, 255};
        thick_line(cx - 7, cy - 7, cx + 7, cy + 7, 3.2f, c, cx, cy, r);
        thick_line(cx - 7, cy + 7, cx + 7, cy - 7, 3.2f, c, cx, cy, r);
    } else if (b == Button::Circle) {
        const Color c{255, 112, 124};
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                const float d = sqrtf(float(x * x + y * y));
                if (d >= 6.0f && d <= 9.2f)
                    pixel(cx + x, cy + y, c);
            }
    } else if (b == Button::Triangle) {
        const Color c{96, 226, 168};
        thick_line(cx, cy - 9, cx - 9, cy + 7, 3.0f, c, cx, cy, r);
        thick_line(cx, cy - 9, cx + 9, cy + 7, 3.0f, c, cx, cy, r);
        thick_line(cx - 9, cy + 7, cx + 9, cy + 7, 3.0f, c, cx, cy, r);
    } else if (b == Button::Square) {
        const Color c{255, 146, 222};
        for (int y = -8; y <= 8; ++y)
            for (int x = -8; x <= 8; ++x)
                if (abs(x) >= 6 || abs(y) >= 6)
                    pixel(cx + x, cy + y, c);
    }
    return r * 2;
}

struct Hint {
    Button button;
    const char *label;
};

// A row of "glyph + label" hints, right-aligned at `right`.
static void hints(const std::vector<Hint> &items, int right, int cy, Color under)
{
    int width = 0;
    std::vector<int> sizes;
    for (const Hint &h : items) {
        int glyph = 34;
        if (h.button == Button::L1 || h.button == Button::R1 || h.button == Button::L2 || h.button == Button::R2 ||
            h.button == Button::Options) {
            const char *label = h.button == Button::Options ? "OPTIONS" : "L1";
            glyph = text_width(label, font_small) + 24;
        }
        sizes.push_back(glyph);
        width += glyph + 10 + text_width(h.label, font_body) + 34;
    }
    int x = right - width;
    for (size_t i = 0; i < items.size(); ++i) {
        button(items[i].button, x + sizes[i] / 2, cy, under);
        x += sizes[i] + 10;
        x = text(items[i].label, font_body, x, cy + 11, right, under, Sub) + 34;
    }
}

// ---- Screens -----------------------------------------------------------------------------
struct CardView {
    std::string title;
    std::string badge;        // console: "GAMECUBE", "NDS", ...
    const uint8_t *thumb;     // ThumbSize x ThumbSize RGB, or null while it loads / when there is none
    bool on_usb;              // not on the internal disk yet
    bool tile_installed;
};

struct LibraryView {
    const char *version;
    const uint8_t *app_icon = nullptr;  // 96x96 RGB, may be null
    std::vector<std::string> tabs;
    int tab = 0;
    std::vector<CardView> cards;
    int selected = 0;
    int first_row = 0;
    std::string detail_title, detail_line;  // about the selected game
    std::string status;
    bool status_is_news = false;  // shown highlighted for a few seconds
    // Action menu (X)
    bool menu_open = false;
    std::string menu_title;
    std::vector<std::string> menu_items;
    int menu_selected = 0;
};

static constexpr int Columns = 6, VisibleRows = 2;
static constexpr int CardW = 264, CardH = 324, GapX = 29, GapY = 20, GridX = 96, GridY = 232;

static void header(const char *title, const char *subtitle, const uint8_t *icon, const char *right_text)
{
    int x = 96;
    if (icon) {
        picture(icon, 96, x, 40, 20);
        x += 120;
    }
    text(title, font_title, x, 100, 1500, background_at(100), Text, true);
    text(subtitle, font_body, x + 2, 132, 1500, background_at(132), Sub);
    if (right_text && *right_text) {
        const int w = text_width(right_text, font_body);
        text(right_text, font_body, 1824 - w, 100, 1830, background_at(100), Sub);
    }
}

static void menu(const LibraryView &v)
{
    const int w = 760, row = 66, h = 120 + int(v.menu_items.size()) * row + 30;
    const int x = (ScreenW - w) / 2, y = (ScreenH - h) / 2;
    round_rect(x - 6, y - 6, w + 12, h + 12, 34, Accent, background_at(y));
    round_rect(x, y, w, h, 30, Card, Accent);
    text(v.menu_title.c_str(), font_large, x + 40, y + 70, x + w - 40, Card, Text, true);
    for (size_t i = 0; i < v.menu_items.size(); ++i) {
        const int ry = y + 100 + int(i) * row;
        const bool on = int(i) == v.menu_selected;
        if (on)
            round_rect(x + 24, ry, w - 48, row - 8, 16, Accent, Card);
        text(v.menu_items[i].c_str(), font_body, x + 52, ry + 39, x + w - 40, on ? Accent : Card, on ? Text : Sub, on);
    }
}

static void library(const LibraryView &v)
{
    drawing = "achtergrond";
    background();
    drawing = "kop";
    char counts[64];
    snprintf(counts, sizeof(counts), "%d games", int(v.cards.size()));
    header("EmuForwarder", v.version, v.app_icon, counts);

    drawing = "tabs";
    // Console tabs, L1 / R1 around them.
    int tx = 96;
    const int ty = 160;
    tx += button(Button::L1, tx + 22, ty + 22, background_at(ty + 22)) + 18;
    for (size_t i = 0; i < v.tabs.size(); ++i) {
        const bool on = int(i) == v.tab;
        const int w = text_width(v.tabs[i].c_str(), font_body) + 48;
        if (on)
            round_rect(tx, ty, w, 46, 23, Text, background_at(ty));
        text(v.tabs[i].c_str(), font_body, tx + 24, ty + 31, tx + w, on ? Text : background_at(ty + 31),
             on ? Bg : Sub, on);
        tx += w + 12;
    }
    button(Button::R1, tx + 22, ty + 22, background_at(ty + 22));

    drawing = "kaarten";
    // Grid of cards.
    if (v.cards.empty()) {
        text_center(TR("No games found", "Geen games gevonden"), font_large, ScreenW / 2, 520, background_at(520), Text, true);
        text_center(TR("Plug in your USB stick or download games with Retrocade, then press Triangle.", "Sluit je USB-stick aan of download games met Retrocade, en druk op Driehoek."),
                    font_body, ScreenW / 2, 572, background_at(572), Sub);
    }
    for (int row = 0; row < VisibleRows; ++row)
        for (int col = 0; col < Columns; ++col) {
            const int index = (v.first_row + row) * Columns + col;
            if (index >= int(v.cards.size()))
                break;
            const CardView &card = v.cards[index];
            const bool on = index == v.selected;
            const int x = GridX + col * (CardW + GapX), y = GridY + row * (CardH + GapY);
            const Color under = background_at(y);
            if (on)
                round_rect(x - 7, y - 7, CardW + 14, CardH + 14, 30, Accent, under);
            const Color fill = on ? CardHi : Card;
            round_rect(x, y, CardW, CardH, 24, fill, on ? Accent : under);
            if (card.thumb) {
                picture(card.thumb, ThumbSize, x + 16, y + 16, 16);
            } else {
                round_rect(x + 16, y + 16, ThumbSize, ThumbSize, 16, Thumb, fill);
                text_center(card.badge.c_str(), font_large, x + 16 + ThumbSize / 2, y + 16 + ThumbSize / 2 + 12, Thumb, Line, true);
            }
            if (card.tile_installed) {  // a small "tile on the home screen" mark
                disc(x + CardW - 40, y + 40, 15, Good, Thumb);
                thick_line(x + CardW - 47, y + 40, x + CardW - 42, y + 46, 3.0f, Bg, x + CardW - 40, y + 40, 15);
                thick_line(x + CardW - 42, y + 46, x + CardW - 32, y + 34, 3.0f, Bg, x + CardW - 40, y + 40, 15);
            }
            text(card.title.c_str(), font_body, x + 18, y + 280, x + CardW - 16, fill, Text, on);
            const Color badge_fill = on ? AccentDeep : Line;
            const int bw = text_width(card.badge.c_str(), font_small) + 24;
            round_rect(x + 18, y + 290, bw, 26, 13, badge_fill, fill);
            text(card.badge.c_str(), font_small, x + 30, y + 311, x + 18 + bw, badge_fill, Text);
            if (card.on_usb) {
                const int ux = x + 18 + bw + 8, uw = text_width("USB", font_small) + 24;
                round_rect(ux, y + 290, uw, 26, 13, Warn, fill);
                text("USB", font_small, ux + 12, y + 311, ux + uw, Warn, Bg, true);
            }
        }
    drawing = "schuifbalk";
    // Scroll indicator.
    const int rows = (int(v.cards.size()) + Columns - 1) / Columns;
    if (rows > VisibleRows) {
        const int track = 2 * CardH + GapY, knob = std::max(60, track * VisibleRows / rows);
        const int ky = GridY + (track - knob) * v.first_row / std::max(1, rows - VisibleRows);
        round_rect(1858, GridY, 8, track, 4, Line, background_at(GridY));
        round_rect(1858, ky, 8, knob, 4, Sub, Line);
    }

    drawing = "onderbalk";
    // Footer: selected game and status on the left, controls on the right.
    const int fy = 958;
    rect(96, fy - 26, 1728, 2, Line);
    if (!v.detail_title.empty()) {
        text(v.detail_title.c_str(), font_large, 96, fy + 22, 760, background_at(fy + 22), Text, true);
        text(v.detail_line.c_str(), font_small, 96, fy + 58, 1824, background_at(fy + 58), Sub);
    }
    if (!v.status.empty()) {
        const Color sc = v.status_is_news ? Warn : Sub;
        text(v.status.c_str(), font_small, 96, fy + 92, 1824, background_at(fy + 92), sc, v.status_is_news);
    }
    hints({{Button::Cross, TR("Select", "Kiezen")}, {Button::Options, TR("Icon", "Icoon")}, {Button::Square, TR("Settings", "Instellingen")},
           {Button::Triangle, TR("Search", "Zoeken")}, {Button::Circle, TR("Close", "Sluiten")}},
          1824, fy + 10, background_at(fy + 10));
    drawing = "menu";
    if (v.menu_open)
        menu(v);
    drawing = "";
}

struct EditorView {
    std::string game;
    std::vector<std::string> sources;
    int current = 0;
    const uint8_t *source_rgb;  // the whole picture
    int source_w, source_h;
    const uint8_t *icon;        // 512x512 RGB result
    float crop_left, crop_top, crop_side;  // the icon's square in source pixels
    int zoom_percent;
    bool adjusting = false;  // false: choosing; true: moving and zooming
    bool grid = false;       // choosing in the full-screen grid
    int grid_first = 0;      // first picture shown in the grid
    std::vector<const uint8_t *> cells;  // GridCellSize square pictures, null while loading
    std::string status;
};

static constexpr int GridColumns = 8, GridRows = 3, GridCellSize = 180;

static void editor(const EditorView &v)
{
    background();
    header(TR("Choose icon", "Icoon kiezen"), v.game.c_str(), nullptr, "");

    // Sources.
    char sources_label[48];
    snprintf(sources_label, sizeof(sources_label), TR("SOURCES  %d/%d", "BRONNEN  %d/%d"), v.current + 1, int(v.sources.size()));
    text(sources_label, font_small, 96, 220, 520, background_at(220), Sub, true);
    const int shown = 13, first = std::max(0, std::min(v.current - shown / 2, int(v.sources.size()) - shown));
    for (int i = first; i < int(v.sources.size()) && i < first + shown; ++i) {
        const int y = 238 + (i - first) * 50;
        const bool on = i == v.current;
        if (on)
            round_rect(96, y, 440, 44, 22, v.adjusting ? Line : Accent, background_at(y));
        text(v.sources[i].c_str(), font_small, 118, y + 30, 520, on ? (v.adjusting ? Line : Accent) : background_at(y + 30), on ? Text : Sub, on);
    }

    // Whole picture with the icon's square marked.
    const int box = 560, bx = 560, by = 238;
    text(TR("WHOLE PICTURE", "HELE AFBEELDING"), font_small, bx, 220, bx + box, background_at(220), Sub, true);
    round_rect(bx, by, box, box, 24, Card, background_at(by));
    if (!v.source_rgb || v.source_w <= 0) {
        text_center(TR("Loading picture...", "Afbeelding laden..."), font_body, bx + box / 2, by + box / 2, Card, Sub);
    } else {
    const float scale = std::min(float(box - 32) / v.source_w, float(box - 32) / v.source_h);
    const int vw = std::max(1, int(v.source_w * scale)), vh = std::max(1, int(v.source_h * scale));
    const int vx = bx + (box - vw) / 2, vy = by + (box - vh) / 2;
    for (int y = 0; y < vh; ++y) {
        const int sy = std::min(v.source_h - 1, int(y / scale));
        for (int x = 0; x < vw; ++x) {
            const uint8_t *p = &v.source_rgb[(size_t(sy) * v.source_w + std::min(v.source_w - 1, int(x / scale))) * 3];
            const float fx = x / scale, fy = y / scale;
            const bool inside = fx >= v.crop_left && fx <= v.crop_left + v.crop_side && fy >= v.crop_top &&
                                fy <= v.crop_top + v.crop_side;
            const Color c{p[0], p[1], p[2]};
            pixel(vx + x, vy + y, inside ? c : mix(c, Bg, 150));  // outside the icon: dimmed
        }
    }
    // Accent frame around the icon's square (clipped to the box).
    const int cx0 = vx + int(v.crop_left * scale), cy0 = vy + int(v.crop_top * scale);
    const int cs = int(v.crop_side * scale);
    for (int t = 0; t < 4; ++t)
        for (int i = 0; i <= cs; ++i) {
            const int pts[4][2] = {{cx0 + i, cy0 + t}, {cx0 + i, cy0 + cs - t}, {cx0 + t, cy0 + i}, {cx0 + cs - t, cy0 + i}};
            for (auto &p : pts)
                if (p[0] >= bx && p[0] < bx + box && p[1] >= by && p[1] < by + box)
                    pixel(p[0], p[1], Accent);
        }

    }
    // The result, as a home-screen tile.
    const int ix = 1196, iy = 238;
    text(TR("HOW IT LOOKS ON YOUR PS4", "ZO KOMT HET OP JE PS4"), font_small, ix, 220, 1824, background_at(220), Sub, true);
    if (v.adjusting)  // the icon is what the controller works on now
        round_rect(ix - 20, iy - 20, 552, 552, 34, Accent, background_at(iy));
    round_rect(ix - 14, iy - 14, 540, 540, 30, Card, v.adjusting ? Accent : background_at(iy));
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x) {
            const uint8_t *p = &v.icon[(size_t(y) * 512 + x) * 3];
            pixel(ix + x, iy + y, Color{p[0], p[1], p[2]});
        }
    // Small home-screen size next to the zoom value.
    char zoom[48];
    snprintf(zoom, sizeof(zoom), "Zoom %d%%", v.zoom_percent);
    text(zoom, font_body, ix, iy + 572, ix + 300, background_at(iy + 572), Text, true);

    const int fy = 958;
    rect(96, fy - 26, 1728, 2, Line);
    text(v.status.c_str(), font_small, 96, fy + 22, 1000, background_at(fy + 22), Warn);
    if (!v.adjusting) {
        hints({{Button::DPad, TR("Choose picture", "Afbeelding kiezen")}, {Button::Square, TR("Grid", "Raster")}, {Button::Cross, TR("Use this one", "Deze gebruiken")},
               {Button::Circle, TR("Back", "Terug")}},
              1824, fy + 10, background_at(fy + 10));
    } else {
        hints({{Button::DPad, TR("Move", "Verplaatsen")}, {Button::R2, "Zoom"}, {Button::Square, TR("Whole picture", "Hele afbeelding")},
               {Button::Triangle, TR("Reset", "Opnieuw")}},
              1824, fy + 10, background_at(fy + 10));
        hints({{Button::Cross, TR("Save as icon", "Opslaan als icoon")}, {Button::Circle, TR("Other picture", "Andere afbeelding")}}, 1824, fy + 62,
              background_at(fy + 62));
    }
}

// All pictures as a full-screen grid; the selection stays the same when switching to the list.
static void editor_grid(const EditorView &v)
{
    background();
    char counter[64];
    snprintf(counter, sizeof(counter), "%d / %d", v.current + 1, int(v.sources.size()));
    header(TR("Choose icon", "Icoon kiezen"), v.game.c_str(), nullptr, counter);
    const int gap = 20, labelH = 40;
    const int x0 = (ScreenW - (GridColumns * GridCellSize + (GridColumns - 1) * gap)) / 2, y0 = 182;
    for (int row = 0; row < GridRows; ++row)
        for (int col = 0; col < GridColumns; ++col) {
            const int i = v.grid_first + row * GridColumns + col;
            if (i >= int(v.sources.size()))
                break;
            const int x = x0 + col * (GridCellSize + gap), y = y0 + row * (GridCellSize + labelH + gap);
            const bool on = i == v.current;
            const Color under = background_at(y);
            if (on)
                round_rect(x - 7, y - 7, GridCellSize + 14, GridCellSize + labelH + 10, 22, Accent, under);
            round_rect(x, y, GridCellSize, GridCellSize + labelH - 4, 18, on ? CardHi : Card, on ? Accent : under);
            if (i < int(v.cells.size()) && v.cells[i])
                picture(v.cells[i], GridCellSize, x, y, 16);
            else
                text_center(TR("loading...", "laden..."), font_small, x + GridCellSize / 2, y + GridCellSize / 2 + 8, on ? CardHi : Card, Sub);
            // "app-icoon 3  (1024x1024)" -> "app-icoon 3"
            std::string label = v.sources[i];
            const size_t cut = label.find("  (");
            if (cut != std::string::npos)
                label.erase(cut);
            text(label.c_str(), font_small, x + 10, y + GridCellSize + 28, x + GridCellSize - 6, on ? CardHi : Card,
                 on ? Text : Sub, on);
        }
    const int fy = 958;
    rect(96, fy - 26, 1728, 2, Line);
    text(v.sources[v.current].c_str(), font_body, 96, fy + 14, 900, background_at(fy + 14), Text, true);
    text(v.status.c_str(), font_small, 96, fy + 56, 1100, background_at(fy + 56), Warn);
    hints({{Button::DPad, TR("Select", "Kiezen")}, {Button::R1, TR("Page", "Pagina")}, {Button::Square, TR("List", "Lijst")}, {Button::Cross, TR("Use", "Gebruiken")},
           {Button::Circle, TR("Back", "Terug")}},
          1824, fy + 10, background_at(fy + 10));
}

}  // namespace ui
