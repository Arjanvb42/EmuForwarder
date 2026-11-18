// PC preview of EmuForwarder's screens: same ui.h, a fake Scene2D that draws into memory. Renders the
// app screens for the promo video (make_promo.py).
//   g++ -O1 -Iinc promo_screens.cpp /usr/lib64/libfreetype.so.6 -o promo_screens
//   ./promo_screens <art> <out/screens> <font.ttf> <native/icons/emuforwarder.png> <chosen icons>
// inc/ holds the freetype headers, OpenOrbis' proto-include.h and stb_image(_write).h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <proto-include.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct Color { uint8_t r, g, b; };
struct Scene2D {
    std::vector<uint8_t> px = std::vector<uint8_t>(1920 * 1080 * 3);
    void DrawPixel(int x, int y, Color c) { uint8_t *p = &px[(y * 1920 + x) * 3]; p[0] = c.r; p[1] = c.g; p[2] = c.b; }
    void DrawRectangle(int x, int y, int w, int h, Color c) {
        if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > 1920 || y + h > 1080)
            fprintf(stderr, "BUITEN BEELD: DrawRectangle(%d, %d, %d, %d)\n", x, y, w, h);
        for (int j = std::max(0, y); j < std::min(1080, y + h); ++j)
            for (int i = std::max(0, x); i < std::min(1920, x + w); ++i) DrawPixel(i, j, c);
    }
};
static Scene2D *scene;
static std::vector<uint32_t> canvas_buffer(1920 * 1080);
extern "C" int ef_last_rect[4]; int ef_last_rect[4];
#include "../../native/ui.h"

static void to_rgb(Scene2D &s)
{
    for (size_t i = 0; i < canvas_buffer.size(); ++i) {
        s.px[i * 3] = canvas_buffer[i] >> 16 & 255; s.px[i * 3 + 1] = canvas_buffer[i] >> 8 & 255; s.px[i * 3 + 2] = canvas_buffer[i] & 255;
    }
}

static std::vector<uint8_t> square(const char *file, int size, float zoom = 1)
{
    int w, h, c;
    unsigned char *p = stbi_load(file, &w, &h, &c, 3);
    std::vector<uint8_t> out(size * size * 3, 20);
    if (!p) return out;
    const float s = std::max(float(size) / w, float(size) / h) * zoom;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int sx = int((x + 0.5f - size / 2.0f) / s + w / 2.0f), sy = int((y + 0.5f - size / 2.0f) / s + h / 2.0f);
            if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
            memcpy(&out[(y * size + x) * 3], &p[(sy * w + sx) * 3], 3);
        }
    stbi_image_free(p);
    return out;
}


static void save(Scene2D &s, const std::string &path) { to_rgb(s); stbi_write_png(path.c_str(), 1920, 1080, 3, s.px.data(), 1920 * 3); }

int main(int argc, char **argv)
{
    // promo_screens <art> <out> <font> <app icon> <ps4 icons folder>
    const std::string art = argv[1], out = argv[2], icons = argv[5];
    FT_Library lib; FT_Init_FreeType(&lib);
    auto font = [&](int size) { FT_Face f; FT_New_Face(lib, argv[3], 0, &f); FT_Set_Pixel_Sizes(f, 0, size); return f; };
    ui::font_title = font(58); ui::font_large = font(36); ui::font_body = font(28); ui::font_small = font(22);
    Scene2D s; scene = &s;
    ui::canvas = canvas_buffer.data();
    ui::on_offscreen = [](const char *, int, int, int, int) {};

    // the library as it is on the PS4: chosen icons where there is one, else the cover
    struct G { std::string file; const char *title, *badge; bool usb, tile; };
    std::vector<G> games = {
        {icons + "/371f58b92fdf3ed3.png", "Mario Kart DS", "NDS", false, false},
        {icons + "/b83aceb9591f7990.png", "Pokemon - Emerald Version", "GBA", false, true},
        {icons + "/629cbbeb3a75b802.png", "Crash Nitro Kart", "GAMECUBE", false, true},
        {icons + "/e820c027c6d1a458.png", "Prince of Persia - The Two Thrones", "GAMECUBE", false, true},
        {icons + "/8f683f8e0d54c6b5.png", "Super Mario Sunshine", "GAMECUBE", false, false},
        {icons + "/427c3ca440cce7ef.png", "Disney's Chicken Little", "GAMECUBE", false, false},
        {icons + "/c1717786ead90bc2.png", "Disney's Donald Duck - Quack Attack", "GAMECUBE", false, false},
        {art + "/G9RP7D.png", "Crash Tag Team Racing", "GAMECUBE", true, false},
        {art + "/GP2EAF.png", "Pac-Man World 2", "GAMECUBE", true, false},
        {art + "/GP3E78.png", "The Polar Express", "GAMECUBE", true, false},
        {art + "/GYRE41.png", "TMNT", "GAMECUBE", true, false},
        {art + "/GKME41.png", "Prince of Persia (USB copy)", "GAMECUBE", true, false}};
    std::vector<std::vector<uint8_t>> thumbs;
    for (auto &g : games) thumbs.push_back(square(g.file.c_str(), ui::ThumbSize));
    std::vector<uint8_t> icon = square(argv[4], 96);

    ui::LibraryView v;
    v.version = "Emulator game forwarder  -  v1.0.0";
    v.app_icon = icon.data();
    v.tabs = {"Alles", "GameCube", "NDS", "GBA"};
    for (size_t i = 0; i < games.size(); ++i)
        v.cards.push_back({games[i].title, games[i].badge, thumbs[i].data(), games[i].usb, games[i].tile});
    v.selected = 0;
    v.detail_title = "Mario Kart DS";
    v.detail_line = "NDS  -  Intern  -  Nog geen tegel";
    v.status = "12 games: 7 intern, 5 op USB.";
    ui::library(v);
    save(s, out + "/lib.png");

    v.menu_open = true;
    v.menu_title = "Mario Kart DS";
    v.menu_items = {"Tegel maken en installeren", "Icoon kiezen", "Standaardicoon terugzetten", "Annuleren"};
    ui::library(v);
    save(s, out + "/menu.png");

    v.menu_title = "De tegel krijgt de naam:  \"Mario Kart DS\"";
    v.menu_items = {"Ja, tegel maken", "Naam wijzigen", "Annuleren"};
    ui::library(v);
    save(s, out + "/name.png");

    v.menu_open = false;
    v.status_is_news = true;
    const int steps[] = {12, 47, 83, 100};
    for (int i = 0; i < 4; ++i) {
        char line[128];
        snprintf(line, sizeof line, "Mario Kart DS installeren... %d%%", steps[i]);
        v.status = line;
        ui::library(v);
        save(s, out + "/install" + std::to_string(i) + ".png");
    }
    v.cards[0].tile_installed = true;
    v.detail_line = "NDS  -  Intern  -  Tegel: Mario Kart DS (op je PS4)";
    v.status = "Mario Kart DS staat klaar. Herstart de PS4 om de naam en het icoon op je home screen te zien.";
    ui::library(v);
    save(s, out + "/installed.png");

    // icon editor: SteamGridDB grid, then moving and zooming the chosen one
    int w, h, c;
    const std::string chosen = icons + "/371f58b92fdf3ed3.png";
    unsigned char *full = stbi_load(chosen.c_str(), &w, &h, &c, 3);
    std::vector<uint8_t> result = square(chosen.c_str(), 512, 1.15f);
    ui::EditorView e;
    e.game = "Mario Kart DS";
    e.source_rgb = full; e.source_w = w; e.source_h = h;
    e.icon = result.data();
    e.crop_side = w / 1.15f; e.crop_left = (w - e.crop_side) / 2; e.crop_top = (h - e.crop_side) / 2;
    e.zoom_percent = 115;
    {
        ui::EditorView g = e;
        static std::vector<std::vector<uint8_t>> cells;
        for (int i = 0; i < 24; ++i) {
            char f[256]; snprintf(f, sizeof f, "%s/sgdb/%02d.png", out.c_str(), i % 19);
            cells.push_back(square(f, ui::GridCellSize));
            char l[64]; snprintf(l, sizeof l, "%s %d  (1024x1024)", i < 8 ? "app-icoon" : i < 14 ? "icoon" : i < 18 ? "hoes" : "banner", i + 1);
            g.sources.push_back(l);
        }
        cells[0] = square(chosen.c_str(), ui::GridCellSize);
        for (auto &cell : cells) g.cells.push_back(cell.data());
        g.grid = true; g.current = 0; g.grid_first = 0;
        g.status = "271 afbeelding(en) van SteamGridDB, GameTDB en libretro.";
        ui::editor_grid(g);
        save(s, out + "/grid.png");
        g.current = 9;
        ui::editor_grid(g);
        save(s, out + "/grid2.png");
        e.sources = g.sources;
    }
    e.current = 0;
    e.adjusting = true;
    e.status = "Verplaats en zoom de afbeelding; X slaat hem op als icoon.";
    ui::editor(e);
    save(s, out + "/editor.png");
    puts("ok");
}
