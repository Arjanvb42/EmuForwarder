#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <orbis/Pad.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <sstream>
#include <vector>
#include "usb_ftp.h"
#include "art.h"
#include "system.h"
#include "online.h"
#include "update.h"
#include "music.h"

extern "C" const char *ef_step;
extern "C" void ef_install_crash_handler();
#include "graphics.h"

#include <math.h>
#include <time.h>
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

std::stringstream debugLogStream;

static constexpr char Library[] = "/data/DolphinPS4/games";
static constexpr char AppData[] = "/data/PS4Tegelmaker";
static constexpr char Mappings[] = "/data/PS4Tegelmaker/mappings";
static constexpr char Templates[] = "/data/PS4Tegelmaker/tiles";
static constexpr char Installer[] = "/data/pkg";
static constexpr char Covers[] = "/data/DolphinPS4/covers";
static constexpr int Slots = 4;  // general tiles per kind: "GameCube 1..4" and "Retro 1..4"
// Games Retrocade downloaded for other consoles (one folder per console); EmuSuNi plays them.
static constexpr char RetroGames[] = "/data/retrocade/games";
static constexpr char RetroCovers[] = "/data/EmuSuNi/covers";

struct Tile {
    const char *disc;
    const char *title;
};

static constexpr Tile Catalog[] = {
#include "catalog.inc"
};
static constexpr int MaxGames = 160;
static constexpr int Rows = 9;

struct Game {
    char path[768];
    char name[160];
    const Tile *tile;
    bool internal;
    bool via_ftp;
    char system[16];  // console folder for Retrocade games ("nds", "gba", ...); empty for GameCube
    std::vector<uint8_t> thumb;  // card picture, ui::ThumbSize square
    int thumb_state = 0;         // 0 not loaded yet, 1 loaded, 2 no picture
    char tile_label[64] = "";    // the tile that plays it, "" when none
    bool tile_installed = false;
};

static Game games[MaxGames];
static void sync_icons();
static int count, selected, first, frame;
static bool confirming_move;
static std::string ftp_address;
static char status[256];
static Scene2D *scene;
#include "ui.h"

// Writes one pixel to the screen and logs it: shows which step makes the frame buffer unusable.
static void probe(const char *stage)
{
    ef_step = stage;
    scene->DrawPixel(0, 0, Color{0, 0, 0});
    sys::log("schermtest na %s: ok", stage);
}

// Defined further down.
static bool read_disc(const char *path, char (&id)[7]);
static void custom_icon_path(const Game &game, char (&path)[96]);
static const char *tile_for(const Game &game, char (&id)[16]);
static bool directory_exists(const char *path);
static void prepare_tile(const Game &game);
static void transfer_game(Game chosen, bool move);
static void open_editor(int index);
static void reset_icon(const Game &game);
static void draw();
static std::string tile_title(const Game &game);
static bool save_title(const Game &game, const std::string &name);
static bool copy_small(const char *source, const char *destination);

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool exists(const char *path)
{
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    close(fd);
    return true;
}

// ---- Icon editor -------------------------------------------------------------------------
// Choose a picture from the sources, see it as the 512x512 tile icon, then move and zoom it.

static constexpr int IconSize = 512;
static constexpr int ViewBox = 560;            // source picture area in the editor
static const Color IconBackground{18, 20, 30};  // shown where the picture does not reach

struct Picture {
    std::string label;
    std::vector<uint8_t> rgb;  // the picture (for SteamGridDB first its small version)
    int width = 0, height = 0;
    std::string thumb_url, full_url;  // SteamGridDB: loaded when shown / when chosen
    int thumb_state = 1;              // 0 not loaded yet, 1 there, 2 failed
    bool full = true;                 // rgb is the full picture
    std::vector<uint8_t> cell;        // square version for the grid
};

static struct Editor {
    bool open = false;
    int game = -1;
    std::vector<Picture> pictures;
    int current = 0;
    float zoom = 1.0f;     // 1 = the picture just fills the square
    float dx = 0, dy = 0;  // picture centre offset from the icon centre, in icon pixels
    std::vector<uint8_t> icon;  // IconSize x IconSize RGB, the result
    bool dirty = true;
    bool adjusting = false;     // false: choose a picture; true: move and zoom it
    bool grid = false;          // while choosing: all pictures as a full-screen grid instead of the list
    bool from_grid = false;     // Circle while adjusting goes back to the grid
    bool loading = false;       // small pictures still to load
} editor;

// Render the icon: the picture scaled by zoom (on top of "fill the square") and shifted by dx/dy.
static void render_icon(const Picture &picture, float zoom, float dx, float dy, std::vector<uint8_t> &out)
{
    out.assign(IconSize * IconSize * 3, 0);
    const int w = picture.width, h = picture.height;
    if (w <= 0 || h <= 0 || picture.rgb.empty()) {  // still loading
        for (size_t i = 0; i < out.size(); i += 3) {
            out[i] = IconBackground.r; out[i + 1] = IconBackground.g; out[i + 2] = IconBackground.b;
        }
        return;
    }
    const float scale = std::max(float(IconSize) / w, float(IconSize) / h) * zoom;
    const uint8_t *px = picture.rgb.data();
    for (int y = 0; y < IconSize; ++y) {
        const float sy = (y + 0.5f - IconSize / 2.0f - dy) / scale + h / 2.0f - 0.5f;
        for (int x = 0; x < IconSize; ++x) {
            const float sx = (x + 0.5f - IconSize / 2.0f - dx) / scale + w / 2.0f - 0.5f;
            uint8_t *o = &out[(y * IconSize + x) * 3];
            if (sx < -0.5f || sy < -0.5f || sx > w - 0.5f || sy > h - 0.5f) {
                o[0] = IconBackground.r; o[1] = IconBackground.g; o[2] = IconBackground.b;
                continue;
            }
            const float cx = std::min(std::max(sx, 0.0f), w - 1.0f), cy = std::min(std::max(sy, 0.0f), h - 1.0f);
            const int x0 = int(cx), y0 = int(cy), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const float fx = cx - x0, fy = cy - y0;
            for (int c = 0; c < 3; ++c) {
                const float a = px[(y0 * w + x0) * 3 + c] * (1 - fx) + px[(y0 * w + x1) * 3 + c] * fx;
                const float b = px[(y1 * w + x0) * 3 + c] * (1 - fx) + px[(y1 * w + x1) * 3 + c] * fx;
                o[c] = uint8_t(a * (1 - fy) + b * fy + 0.5f);
            }
        }
    }
}

static bool write_png(const char *path, const std::vector<uint8_t> &rgb)
{
    char pending[192];
    snprintf(pending, sizeof(pending), "%s.tmp", path);
    if (!stbi_write_png(pending, IconSize, IconSize, 3, rgb.data(), IconSize * 3) || rename(pending, path)) {
        unlink(pending);
        return false;
    }
    return true;
}

static bool load_picture(const char *label, const uint8_t *data, size_t size, const char *file, Picture &out)
{
    int w = 0, h = 0, channels = 0;
    unsigned char *pixels = file ? stbi_load(file, &w, &h, &channels, 4)
                                 : stbi_load_from_memory(data, int(size), &w, &h, &channels, 4);
    if (!pixels)
        return false;
    const bool ok = w > 0 && h > 0 && w <= 4096 && h <= 4096;
    if (ok) {
        out.label = label;
        out.width = w;
        out.height = h;
        out.rgb.resize(size_t(w) * h * 3);
        // Transparent parts (logos) show the icon background.
        const uint8_t back[3] = {IconBackground.r, IconBackground.g, IconBackground.b};
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            const unsigned alpha = pixels[i * 4 + 3];
            for (int c = 0; c < 3; ++c)
                out.rgb[i * 3 + c] = uint8_t((pixels[i * 4 + c] * alpha + back[c] * (255 - alpha) + 127) / 255);
        }
    }
    stbi_image_free(pixels);
    return ok;
}

// Square picture of any size, same framing as the icon (fill, then zoom and shift).
static void render_square(const Picture &picture, int size, std::vector<uint8_t> &out)
{
    std::vector<uint8_t> full;
    render_icon(picture, 1.0f, 0, 0, full);
    out.resize(size_t(size) * size * 3);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const uint8_t *p = &full[(size_t(y * IconSize / size) * IconSize + x * IconSize / size) * 3];
            memcpy(&out[(size_t(y) * size + x) * 3], p, 3);
        }
}

// ---- Editor pictures that load later (SteamGridDB) -----------------------------------------


// Keep small versions small: the overview can hold well over a hundred pictures.
static void shrink(Picture &picture, int longest)
{
    const int w = picture.width, h = picture.height;
    if (w <= longest && h <= longest)
        return;
    const float scale = float(longest) / std::max(w, h);
    const int nw = std::max(1, int(w * scale)), nh = std::max(1, int(h * scale));
    std::vector<uint8_t> out(size_t(nw) * nh * 3);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x)
            memcpy(&out[(size_t(y) * nw + x) * 3], &picture.rgb[(size_t(int(y / scale)) * w + int(x / scale)) * 3], 3);
    picture.rgb.swap(out);
    picture.width = nw;
    picture.height = nh;
}

// Load one small picture per frame, and only the ones on screen (the grid's page, or the part of
// the list around the selection): scrolling further loads the next ones. Loading every picture at
// once (227 for Pokemon Emerald) made the app very slow.
static void pump_editor_pictures(int first, int last)
{
    editor.loading = false;
    const int total = int(editor.pictures.size());
    int pick = -1;
    if (editor.current >= 0 && editor.current < total && editor.pictures[editor.current].thumb_state == 0)
        pick = editor.current;  // the selected one first
    for (int i = std::max(0, first); i < std::min(total, last) && pick < 0; ++i)
        if (editor.pictures[i].thumb_state == 0)
            pick = i;
    if (pick < 0)
        return;
    editor.loading = true;
    Picture &picture = editor.pictures[pick];
    std::vector<uint8_t> body;
    Picture loaded;
    if (online::get(picture.thumb_url, {}, body) == 200 && load_picture("", body.data(), body.size(), nullptr, loaded)) {
        picture.rgb.swap(loaded.rgb);
        picture.width = loaded.width;
        picture.height = loaded.height;
        shrink(picture, 360);
        picture.thumb_state = 1;
        if (pick == editor.current)
            editor.dirty = true;
    } else {
        picture.thumb_state = 2;
    }
}

// The full picture of a SteamGridDB choice, before moving and zooming it.
static void load_full_picture(Picture &picture)
{
    if (picture.full || picture.full_url.empty())
        return;
    snprintf(status, sizeof(status), "Volledige afbeelding laden...");
    draw();
    std::vector<uint8_t> body;
    Picture loaded;
    if (online::get(picture.full_url, {}, body) == 200 && load_picture("", body.data(), body.size(), nullptr, loaded)) {
        picture.rgb.swap(loaded.rgb);
        picture.width = loaded.width;
        picture.height = loaded.height;
        shrink(picture, 2048);
        picture.full = true;
        picture.thumb_state = 1;
        snprintf(status, sizeof(status), "Verplaats en zoom de afbeelding; X slaat hem op als icoon.");
    } else {
        snprintf(status, sizeof(status), "De grote versie kwam niet binnen; de kleine wordt gebruikt.");
    }
    editor.dirty = true;
}

// ---- Library state -------------------------------------------------------------------------
static std::vector<std::string> tab_keys;  // "" = everything, "gamecube", or a Retrocade console
static int tab;
static std::vector<int> view;              // indices into games[] shown under the current tab
static struct Menu {
    bool open = false;
    bool confirm_move = false;
    bool settings = false;
    bool update = false;  // "a newer version is there" question
    bool confirm_name = false;  // "the tile gets this name" question
    std::vector<std::string> items;
    std::vector<int> actions;
    int selected = 0;
} menu;
enum Action { TileAction, IconAction, ResetIconAction, CopyAction, MoveAction, ConfirmMoveAction, CancelAction,
              KeyAction, ToggleSteamGridAction, ClearKeyAction, OnlySteamGridAction, AutoUpdateAction,
              CheckUpdateAction, InstallUpdateAction, ConfirmTileAction, RenameAction, MusicAction, VolumeAction };
static time_t exit_armed = 0;
static char last_status[256];
static time_t status_time;
static std::vector<uint8_t> app_icon;
static bool thumbs_pending = false;  // covers still to load: keep drawing

static std::string console_label(const Game &game)
{
    if (!game.system[0])
        return "GAMECUBE";
    std::string label = game.system;
    for (char &c : label)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return label;
}

static void build_view()
{
    std::vector<std::string> keys{""};
    for (int i = 0; i < count; ++i) {
        const std::string key = games[i].system[0] ? games[i].system : "gamecube";
        if (std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(key);
    }
    const std::string current = tab < int(tab_keys.size()) ? tab_keys[tab] : "";
    tab_keys = keys;
    tab = 0;
    for (size_t i = 0; i < tab_keys.size(); ++i)
        if (tab_keys[i] == current)
            tab = int(i);
    view.clear();
    for (int i = 0; i < count; ++i) {
        const std::string key = games[i].system[0] ? games[i].system : "gamecube";
        if (tab_keys[tab].empty() || tab_keys[tab] == key)
            view.push_back(i);
    }
    if (!view.empty() && std::find(view.begin(), view.end(), selected) == view.end())
        selected = view[0];
}

static int view_position()
{
    for (size_t i = 0; i < view.size(); ++i)
        if (view[i] == selected)
            return int(i);
    return 0;
}

// Card pictures: the chosen icon, else the cover the emulator downloaded. A few per frame.
static void load_thumbs()
{
    sys::Elevated elevated;  // USB discs are read directly
    int budget = 2;
    thumbs_pending = false;
    const int position = view_position();
    const int first_row = std::max(0, position / ui::Columns - (ui::VisibleRows - 1));
    const int from = first_row * ui::Columns, to = std::min(int(view.size()), from + ui::Columns * (ui::VisibleRows + 1));
    for (int i = from; i < to; ++i) {
        Game &game = games[view[i]];
        if (game.thumb_state)
            continue;
        if (!budget) {
            thumbs_pending = true;
            break;
        }
        --budget;
        char file[640] = "";
        char custom[96];
        custom_icon_path(game, custom);
        if (exists(custom)) {
            snprintf(file, sizeof(file), "%s", custom);
        } else if (game.system[0]) {
            snprintf(file, sizeof(file), "%s/%s/%s.png", RetroCovers, game.system, game.name);
        } else {
            char disc[7] = "";
            if (game.tile)
                snprintf(disc, sizeof(disc), "%s", game.tile->disc);
            else if (!game.via_ftp)
                read_disc(game.path, disc);
            if (disc[0])
                snprintf(file, sizeof(file), "%s/%s.png", Covers, disc);
        }
        Picture picture;
        if (file[0] && exists(file) && load_picture("", nullptr, 0, file, picture)) {
            render_square(picture, ui::ThumbSize, game.thumb);
            game.thumb_state = 1;
        } else {
            game.thumb_state = 2;
        }
    }
}

static void refresh_tiles()
{
    sys::Elevated elevated;
    for (int i = 0; i < count; ++i) {
        Game &game = games[i];
        char id[16];
        game.tile_label[0] = 0;
        game.tile_installed = false;
        if (!game.internal || !tile_for(game, id))
            continue;
        snprintf(game.tile_label, sizeof(game.tile_label), "%s", tile_title(game).c_str());
        char app[48];
        snprintf(app, sizeof(app), "/user/app/%s", id);
        game.tile_installed = directory_exists(app);
    }
}

static void open_menu()
{
    if (view.empty())
        return;
    const Game &game = games[selected];
    menu = Menu{};
    menu.open = true;
    auto add = [](const char *label, int action) {
        menu.items.push_back(label);
        menu.actions.push_back(action);
    };
    if (!game.internal) {
        add("Naar interne schijf kopieren", CopyAction);
        add("Naar interne schijf verplaatsen", MoveAction);
    } else {
        add(game.tile_installed ? "Tegel bijwerken" : "Tegel maken en installeren", TileAction);
        add("Icoon kiezen", IconAction);
        char custom[96];
        custom_icon_path(game, custom);
        if (exists(custom))
            add("Standaardicoon terugzetten", ResetIconAction);
    }
    add("Annuleren", CancelAction);
}

static void open_settings(int keep_selected = 0)
{
    menu = Menu{};
    menu.open = menu.settings = true;
    const std::string &key = online::settings.steamgriddb_key;
    std::string key_label = "SteamGridDB API-sleutel: ";
    key_label += key.empty() ? "niet ingesteld" : "****" + key.substr(key.size() > 4 ? key.size() - 4 : 0);
    if (!key.empty() && key.size() != 32)
        key_label += " (onvolledig, " + std::to_string(key.size()) + " tekens)";
    menu.items.push_back(key_label);
    menu.actions.push_back(KeyAction);
    menu.items.push_back(std::string("SteamGridDB in de icoon-editor: ") + (online::settings.steamgriddb ? "aan" : "uit"));
    menu.actions.push_back(ToggleSteamGridAction);
    menu.items.push_back(std::string("Alleen SteamGridDB (sneller, zonder GameTDB/libretro): ") +
                         (online::settings.only_steamgriddb ? "aan" : "uit"));
    menu.actions.push_back(OnlySteamGridAction);
    if (!key.empty()) {
        menu.items.push_back("SteamGridDB-sleutel wissen");
        menu.actions.push_back(ClearKeyAction);
    }
    char music_label[160];
    snprintf(music_label, sizeof(music_label), "Achtergrondmuziek: %s  (%d nummer(s) in /data/PS4Tegelmaker/muziek)",
             online::settings.music ? "aan" : "uit", music::track_count);
    menu.items.push_back(music_label);
    menu.actions.push_back(MusicAction);
    menu.items.push_back("Volume muziek: " + std::to_string(online::settings.music_volume) + "%");
    menu.actions.push_back(VolumeAction);
    menu.items.push_back(std::string("Bij het starten naar updates zoeken: ") + (online::settings.auto_update ? "aan" : "uit"));
    menu.actions.push_back(AutoUpdateAction);
    if (update::done && update::newer()) {
        menu.items.push_back(update::latest.version + " downloaden naar /data/pkg (nu " EF_VERSION ")");
        menu.actions.push_back(InstallUpdateAction);
    } else {
        menu.items.push_back(update::checking ? "Zoeken naar updates..." : "Nu naar updates zoeken (nu " EF_VERSION ")");
        menu.actions.push_back(CheckUpdateAction);
    }
    menu.items.push_back("Sluiten");
    menu.actions.push_back(CancelAction);
    menu.selected = std::min(keep_selected, int(menu.items.size()) - 1);
}

// Download the newer package to /data/pkg; it is installed with GoldHEN's Package Installer.
static void install_update()
{
    menu.open = false;
    snprintf(status, sizeof(status), "%s downloaden...", update::latest.version.c_str());
    draw();
    std::string error;
    if (!update::download(error)) {
        snprintf(status, sizeof(status), "Downloaden lukte niet: %s.", error.c_str());
        return;
    }
    snprintf(status, sizeof(status),
             "%s staat klaar. Sluit EmuForwarder en open \"EmuForwarder Updater\" op je home screen.",
             update::latest.version.c_str());
}

// Before a tile is made: show the name it gets, with the chance to change it.
static void open_name_question()
{
    menu = Menu{};
    menu.open = menu.confirm_name = true;
    menu.items = {"Ja, tegel maken", "Naam wijzigen", "Annuleren"};
    menu.actions = {ConfirmTileAction, RenameAction, CancelAction};
}

// EmuForwarder Updater (TGMA00002) comes inside this package; install it when it is missing or older.
static void ensure_updater()
{
    const std::string have = sys::installed_version("TGMA00002");
    if (!have.empty() && have >= EF_UPDATER_VERSION)
        return;
    sys::log("updater: geinstalleerd \"%s\", meegeleverd %s: installeren", have.c_str(), EF_UPDATER_VERSION);
    static constexpr char Copy[] = "/data/PS4Tegelmaker/updater.pkg";
    if (!copy_small("/app0/updater.pkg", Copy)) {
        sys::log("updater: kopieren mislukt");
        return;
    }
    std::string error;
    snprintf(status, sizeof(status), "EmuForwarder Updater installeren...");
    draw();
    const sys::Install result = sys::install(Copy, "TGMA00002", "EmuForwarder Updater", error, [](int percent) {
        snprintf(status, sizeof(status), "EmuForwarder Updater installeren... %d%%", percent);
        draw();
    });
    unlink(Copy);
    snprintf(status, sizeof(status), result == sys::Install::Failed ? "EmuForwarder Updater installeren lukte niet (%s)."
                                                                    : "EmuForwarder Updater staat op je home screen.",
             error.c_str());
}

static void open_update_question()
{
    menu = Menu{};
    menu.open = menu.update = true;
    menu.items = {"Downloaden naar /data/pkg", "Later"};
    menu.actions = {InstallUpdateAction, CancelAction};
}

static void run_menu_action()
{
    const int action = menu.actions[menu.selected];
    const int index = selected;
    if (action == InstallUpdateAction) {
        install_update();
        return;
    }
    if (menu.update) {
        menu.open = false;
        return;
    }
    if (menu.settings) {
        const int at = menu.selected;
        menu.open = false;
        if (action == KeyAction) {
            std::string key = online::settings.steamgriddb_key;
            if (online::keyboard("SteamGridDB API-sleutel", key, [] { draw(); usleep(16 * 1000); })) {
                std::string clean;
                for (char c : key)
                    if (c != ' ')
                        clean += c;
                bool hex = true;
                for (char c : clean)
                    hex &= isxdigit(static_cast<unsigned char>(c)) != 0;
                if (!clean.empty() && (clean.size() != 32 || !hex)) {
                    snprintf(status, sizeof(status),
                             "Die sleutel klopt niet: een SteamGridDB-sleutel heeft 32 tekens (0-9 en a-f), deze heeft er %d.",
                             int(clean.size()));
                } else {
                    online::settings.steamgriddb_key = clean;
                    online::save_settings();
                    snprintf(status, sizeof(status), clean.empty() ? "SteamGridDB-sleutel verwijderd."
                                                                   : "SteamGridDB-sleutel opgeslagen. Kies een icoon met Options.");
                }
            }
        } else if (action == OnlySteamGridAction) {
            online::settings.only_steamgriddb = !online::settings.only_steamgriddb;
            online::save_settings();
        } else if (action == ToggleSteamGridAction) {
            online::settings.steamgriddb = !online::settings.steamgriddb;
            online::save_settings();
        } else if (action == MusicAction) {
            online::settings.music = !online::settings.music;
            music::enabled = online::settings.music;
            online::save_settings();
            if (online::settings.music && music::track_count == 0)
                snprintf(status, sizeof(status), "Zet .ogg- of .wav-bestanden (16-bit) in /data/PS4Tegelmaker/muziek (via FTP).");
        } else if (action == VolumeAction) {
            online::settings.music_volume = online::settings.music_volume >= 100 ? 20 : online::settings.music_volume + 20;
            music::volume = online::settings.music_volume;
            online::save_settings();
        } else if (action == AutoUpdateAction) {
            online::settings.auto_update = !online::settings.auto_update;
            online::save_settings();
        } else if (action == CheckUpdateAction) {
            update::check();
            snprintf(status, sizeof(status), "Zoeken naar updates...");
        } else if (action == ClearKeyAction) {
            online::settings.steamgriddb_key.clear();
            online::save_settings();
            snprintf(status, sizeof(status), "SteamGridDB-sleutel verwijderd.");
        }
        if (action != CancelAction)
            open_settings(at);
        return;
    }
    if (view.empty())
        return;
    sys::log("menu: \"%s\" voor %s", menu.items[menu.selected].c_str(), games[index].path);
    menu.open = false;
    switch (action) {
    case TileAction: open_name_question(); break;
    case ConfirmTileAction: prepare_tile(games[index]); break;
    case RenameAction: {
        std::string name = tile_title(games[index]);
        if (online::keyboard("Naam van de tegel", name, [] { draw(); usleep(16 * 1000); })) {
            while (!name.empty() && name.back() == ' ')
                name.pop_back();
            while (!name.empty() && name.front() == ' ')
                name.erase(0, 1);
            if (!save_title(games[index], name))
                snprintf(status, sizeof(status), "Kan de naam niet opslaan.");
            refresh_tiles();
        }
        open_name_question();
        break;
    }
    case IconAction: open_editor(index); break;
    case ResetIconAction: reset_icon(games[index]); break;
    case CopyAction: transfer_game(games[index], false); break;
    case MoveAction:
        menu = Menu{};
        menu.open = menu.confirm_move = true;
        menu.items = {"Ja, verplaatsen", "Annuleren"};
        menu.actions = {ConfirmMoveAction, CancelAction};
        return;
    case ConfirmMoveAction: transfer_game(games[index], true); break;
    default: break;
    }
    refresh_tiles();
    for (int i = 0; i < count; ++i)  // a new icon or cover may be there now
        if (games[i].thumb_state == 2 || i == index)
            games[i].thumb_state = 0;
}

static void draw()
{
    ui::canvas = scene->Pixels();
    if (strcmp(status, last_status)) {
        snprintf(last_status, sizeof(last_status), "%s", status);
        status_time = time(nullptr);
    }
    if (editor.open) {
        const int columns = ui::GridColumns, rows = ui::GridRows;
        const int grid_first = std::max(0, editor.current / columns - (rows - 1)) * columns;
        if (editor.adjusting)
            editor.loading = false;  // nothing loads while moving and zooming
        else if (editor.grid)
            pump_editor_pictures(grid_first, grid_first + columns * rows);
        else
            pump_editor_pictures(editor.current - 6, editor.current + 7);
        const Picture &picture = editor.pictures[editor.current];
        if (editor.dirty) {
            render_icon(picture, editor.zoom, editor.dx, editor.dy, editor.icon);
            editor.dirty = false;
        }
        ui::EditorView v;
        v.game = games[editor.game].name;
        for (const Picture &p : editor.pictures)
            v.sources.push_back(p.label);
        v.current = editor.current;
        v.source_rgb = picture.rgb.empty() ? nullptr : picture.rgb.data();
        v.source_w = picture.width;
        v.source_h = picture.height;
        v.icon = editor.icon.data();
        const float scale = picture.width > 0 ? std::max(float(IconSize) / picture.width, float(IconSize) / picture.height) * editor.zoom : 1;
        v.crop_side = IconSize / scale;
        v.crop_left = picture.width / 2.0f - (IconSize / 2.0f + editor.dx) / scale;
        v.crop_top = picture.height / 2.0f - (IconSize / 2.0f + editor.dy) / scale;
        v.zoom_percent = int(editor.zoom * 100 + 0.5f);
        v.adjusting = editor.adjusting;
        v.grid = editor.grid && !editor.adjusting;
        v.status = status;
        if (v.grid) {
            v.grid_first = grid_first;
            v.cells.assign(editor.pictures.size(), nullptr);
            for (int i = grid_first; i < std::min(int(editor.pictures.size()), grid_first + columns * rows); ++i) {
                Picture &cellp = editor.pictures[i];
                if (cellp.cell.empty() && !cellp.rgb.empty())
                    render_square(cellp, ui::GridCellSize, cellp.cell);
                if (!cellp.cell.empty())
                    v.cells[i] = cellp.cell.data();
            }
        }
        ef_step = "editor tekenen";
        if (v.grid)
            ui::editor_grid(v);
        else
            ui::editor(v);
    } else {
        ef_step = "hoezen laden";
        load_thumbs();
        ef_step = "bibliotheek tekenen";
        ui::LibraryView v;
        v.version = "Emulator game forwarder  -  " EF_VERSION;
        v.app_icon = app_icon.empty() ? nullptr : app_icon.data();
        for (const std::string &key : tab_keys) {
            if (key.empty())
                v.tabs.push_back("Alles");
            else if (key == "gamecube")
                v.tabs.push_back("GameCube");
            else {
                std::string label = key;
                for (char &c : label)
                    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                v.tabs.push_back(label);
            }
        }
        v.tab = tab;
        for (int index : view) {
            const Game &game = games[index];
            v.cards.push_back({game.name, console_label(game), game.thumb_state == 1 ? game.thumb.data() : nullptr,
                               !game.internal, game.tile_installed});
        }
        const int position = view_position();
        v.selected = position;
        v.first_row = std::max(0, position / ui::Columns - (ui::VisibleRows - 1));
        if (!view.empty()) {
            const Game &game = games[selected];
            v.detail_title = game.name;
            std::string line = console_label(game) + "  -  " + (game.internal ? "Intern" : "USB") + "  -  ";
            if (!game.internal)
                line += "Kopieer of verplaats hem eerst naar de interne schijf";
            else if (!game.tile_label[0])
                line += "Nog geen tegel";
            else
                line += std::string("Tegel: ") + game.tile_label + (game.tile_installed ? " (op je PS4)" : " (nog installeren)");
            v.detail_line = line;
        }
        v.status = status;
        v.status_is_news = time(nullptr) - status_time < 5;
        if (menu.open) {
            v.menu_open = true;
            v.menu_title = menu.confirm_name && !view.empty() ? "De tegel krijgt de naam:  \"" + tile_title(games[selected]) + "\""
                         : menu.update ? "Nieuwe versie: " + update::latest.version +
                                             (update::latest.notes.empty() ? "" : "  -  " + update::latest.notes)
                         : menu.settings ? "Instellingen"
                         : menu.confirm_move ? "Verplaatsen? De USB-kopie wordt gewist na controle."
                                             : (view.empty() ? "" : games[selected].name);
            v.menu_items = menu.items;
            v.menu_selected = menu.selected;
        }
        ui::library(v);
    }
    ef_step = "beeld tonen";
    scene->SubmitFlip(frame);
    scene->FrameWait(frame);
    scene->FrameBufferSwap();
    ++frame;
    ef_step = "invoer";
}

static bool image_name(const char *name)
{
    const char *suffix = strrchr(name, '.');
    return suffix && (!strcasecmp(suffix, ".iso") || !strcasecmp(suffix, ".gcm"));
}

static bool read_disc(const char *path, char (&id)[7])
{
    sys::Elevated elevated;
    FILE *iso = fopen(path, "rb");
    if (!iso)
        return false;
    unsigned char header[32];
    const bool read = fread(header, 1, sizeof(header), iso) == sizeof(header);
    fclose(iso);
    if (!read || memcmp(header + 28, "\xc2\x33\x9f\x3d", 4) != 0)
        return false;
    for (unsigned i = 0; i < 6; ++i)
        if (!(header[i] >= 'A' && header[i] <= 'Z') && !(header[i] >= '0' && header[i] <= '9'))
            return false;
    memcpy(id, header, 6);
    id[6] = 0;
    return true;
}

static bool walk(const char *folder, bool internal, int depth, FILE *log)
{
    DIR *dir = opendir(folder);
    if (!dir) {
        if (log) fprintf(log, "%s: %s\n", folder, strerror(errno));
        return false;
    }
    if (log) fprintf(log, "%s: zichtbaar\n", folder);
    struct dirent *entry;
    while (count < MaxGames && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || strpbrk(entry->d_name, "\r\n"))
            continue;
        char path[768];
        int result = snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
        if (result < 0 || result >= static_cast<int>(sizeof(path)))
            continue;
        if (entry->d_type != DT_DIR && image_name(entry->d_name)) {
            char disc[7];
            if (!read_disc(path, disc))
                continue;
            const Tile *tile = nullptr;
            for (const Tile &candidate : Catalog) {
                if (!strcmp(disc, candidate.disc)) {
                    tile = &candidate;
                    break;
                }
            }
            Game &game = games[count++];
            game.system[0] = 0;
            snprintf(game.path, sizeof(game.path), "%s", path);
            const char *name = tile ? tile->title : entry->d_name;
            if (!tile && !strcasecmp(name, "game.iso"))
                name = basename_of(folder);
            snprintf(game.name, sizeof(game.name), "%s", name);
            game.tile = tile;
            game.internal = internal;
            game.via_ftp = false;
        } else if (depth && (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN)) {
            walk(path, internal, depth - 1, nullptr);
        }
    }
    closedir(dir);
    return true;
}

static bool walk_ftp(UsbFtp &ftp, const std::string &folder, int depth)
{
    if (!ftp.start("LIST", folder.c_str(), ftp_address))
        return false;
    std::string rows;
    char buffer[4096];
    int n;
    while ((n = ftp.reading(buffer, sizeof(buffer))) > 0) {
        if (rows.size() + n > 1024 * 1024)
            break;
        rows.append(buffer, n);
    }
    const bool finished = ftp.finish();
    if (n != 0 || !finished)
        return false;

    std::vector<std::string> folders;
    for (const char *line = rows.c_str(); *line && count < MaxGames;) {
        const char *end = strchr(line, '\n');
        std::string record(line, end ? end - line : strlen(line));
        line = end ? end + 1 : line + record.size();
        if (!record.empty() && record.back() == '\r')
            record.pop_back();
        if (record.empty() || (record[0] != '-' && record[0] != 'd'))
            continue;
        const char *name = record.c_str();
        for (int i = 0; i < 8; ++i) {
            name += strcspn(name, " \t");
            name += strspn(name, " \t");
        }
        if (!*name || name[0] == '.' || strpbrk(name, "/\r\n"))
            continue;
        const std::string path = folder + "/" + name;
        if (path.size() >= sizeof(Game::path))
            continue;
        if (record[0] == 'd') {
            if (depth && folders.size() < 160)
                folders.push_back(path);
            continue;
        }
        if (!image_name(name))
            continue;
        const Tile *tile = nullptr;
        const char *suffix = strrchr(name, '.');
        if (suffix - name >= 8 && suffix[-8] == '[' && suffix[-1] == ']')
            for (const Tile &candidate : Catalog)
                if (!memcmp(suffix - 7, candidate.disc, 6)) {
                    tile = &candidate;
                    break;
                }
        Game &game = games[count++];
        game.system[0] = 0;
        snprintf(game.path, sizeof(game.path), "%s", path.c_str());
        snprintf(game.name, sizeof(game.name), "%s", tile ? tile->title : name);
        game.tile = tile;
        game.internal = false;
        game.via_ftp = true;
    }
    for (const auto &next : folders)
        if (count < MaxGames)
            walk_ftp(ftp, next, depth - 1);
    return true;
}

static bool retro_file(const char *name)
{
    if (name[0] == '.')
        return false;
    const char *suffix = strrchr(name, '.');
    if (!suffix || suffix == name)
        return false;
    static const char *const skip[] = {".part", ".tmp", ".json", ".txt", ".ini", ".img", ".png", ".jpg", ".sav", ".srm"};
    for (const char *ending : skip)
        if (!strcasecmp(suffix, ending))
            return false;
    return true;
}

// Retrocade keeps downloads in /data/retrocade/games/<console>/; GameCube goes through DolphinPS4.
static void walk_retro(FILE *log)
{
    DIR *consoles = opendir(RetroGames);
    if (!consoles) {
        if (log) fprintf(log, "%s: %s\n", RetroGames, strerror(errno));
        return;
    }
    struct dirent *console;
    while (count < MaxGames && (console = readdir(consoles))) {
        if (console->d_name[0] == '.' || !strcasecmp(console->d_name, "gamecube") ||
            strlen(console->d_name) >= sizeof(Game::system))
            continue;
        char folder[256];
        snprintf(folder, sizeof(folder), "%s/%s", RetroGames, console->d_name);
        DIR *dir = opendir(folder);
        if (!dir)
            continue;
        int found = 0;
        struct dirent *entry;
        while (count < MaxGames && (entry = readdir(dir))) {
            if (entry->d_type == DT_DIR || !retro_file(entry->d_name) || strpbrk(entry->d_name, "\r\n"))
                continue;
            Game &game = games[count];
            if (snprintf(game.path, sizeof(game.path), "%s/%s", folder, entry->d_name) >= static_cast<int>(sizeof(game.path)))
                continue;
            snprintf(game.name, sizeof(game.name), "%s", entry->d_name);
            char *dot = strrchr(game.name, '.');
            if (dot)
                *dot = 0;
            snprintf(game.system, sizeof(game.system), "%s", console->d_name);
            game.tile = nullptr;
            game.internal = true;
            game.via_ftp = false;
            ++count;
            ++found;
        }
        closedir(dir);
        if (log) fprintf(log, "%s: %d game(s)\n", folder, found);
    }
    closedir(consoles);
}

static void scan(const char *restore = nullptr)
{
    FILE *log = fopen("/data/PS4Tegelmaker/storage.log", "w");
    sys::Elevated elevated;  // so /mnt/usbN is readable without going through FTP
    count = selected = first = 0;
    walk(Library, true, 4, log);
    walk_retro(log);
    const int internal = count;
    int mounts = 0;
    for (int i = 0; i < 8; ++i) {
        char path[32];
        int previous_count = count;
        snprintf(path, sizeof(path), "/mnt/usb%d", i);
        bool mounted = walk(path, false, 4, log);
        if (previous_count == count) {
            snprintf(path, sizeof(path), "/usb%d", i);
            mounted = walk(path, false, 4, log) || mounted;
        }
        mounts += mounted;
    }
    bool ftp_connected = false;
    if (!mounts) {
        ftp_address = UsbFtp::console_address();
        UsbFtp ftp;
        ftp_connected = ftp.login(ftp_address);
        if (log)
            fprintf(log, "GoldHEN FTP op deze PS4: %s\n", ftp_connected ? ftp.host().c_str() : "onbereikbaar");
        if (ftp_connected) {
            for (int i = 0; i < 8; ++i) {
                char path[32];
                snprintf(path, sizeof(path), "/mnt/usb%d", i);
                const bool visible = walk_ftp(ftp, path, 4);
                mounts += visible;
                if (log)
                    fprintf(log, "%s via FTP: %s\n", path, visible ? "zichtbaar" : "onbereikbaar");
            }
        }
    }
    if (log) {
        fprintf(log, "Intern: %d spel(len). USB: %d spel(len), %d mount(s).\n",
                internal, count - internal, mounts);
        fclose(log);
    }
    std::sort(games, games + count, [](const Game &a, const Game &b) {
        if (a.internal != b.internal)
            return !a.internal;
        const int names = strcasecmp(a.name, b.name);
        return names ? names < 0 : strcmp(a.path, b.path) < 0;
    });
    if (restore)
        for (int i = 0; i < count; ++i)
            if (!strcmp(restore, games[i].path))
                selected = i;
    if (mounts)
        snprintf(status, sizeof(status), "USB: %d game(s). Intern: %d game(s).", count - internal, internal);
    else if (ftp_connected)
        snprintf(status, sizeof(status), "GoldHEN FTP ziet geen USB-stick. Intern: %d game(s).", internal);
    else
        snprintf(status, sizeof(status), "Zet GoldHEN FTP Server aan en druk op Driehoek. Intern: %d game(s).", internal);
    for (int i = 0; i < count; ++i) {
        games[i].thumb.clear();
        games[i].thumb_state = 0;
    }
    sync_icons();
    refresh_tiles();
    build_view();
}

class Reader {
public:
    Reader(const char *path, bool remote) : via_ftp(remote)
    {
        sys::Elevated elevated;  // an open file stays readable afterwards
        ready = remote ? (ftp.login(ftp_address) && ftp.start("RETR", path, ftp_address))
                       : ((file = fopen(path, "rb")) != nullptr);
    }

    ~Reader()
    {
        if (file)
            fclose(file);
    }

    size_t read(void *buffer, size_t size)
    {
        if (!via_ftp) {
            const size_t n = fread(buffer, 1, size, file);
            failed = ferror(file);
            return n;
        }
        if (ended || failed)
            return 0;
        size_t offset = 0;
        while (offset < size) {
            const int n = ftp.reading(static_cast<unsigned char *>(buffer) + offset, size - offset);
            if (n <= 0) {
                ended = true;
                failed = (n < 0 || !ftp.finish());
                break;
            }
            offset += n;
        }
        return offset;
    }

    bool ready = false, failed = false;

private:
    FILE *file = nullptr;
    UsbFtp ftp;
    bool via_ftp, ended = false;
};

static bool gamecube_header(const unsigned char *bytes, size_t length)
{
    return length >= 32 && memcmp(bytes + 28, "\xc2\x33\x9f\x3d", 4) == 0;
}

static bool same_file(const char *first_path, const char *second_path, bool via_ftp = false)
{
    Reader a(first_path, via_ftp), b(second_path, false);
    if (!a.ready || !b.ready)
        return false;
    static unsigned char left[256 * 1024], right[256 * 1024];
    bool first_chunk = true;
    for (;;) {
        const size_t na = a.read(left, sizeof(left));
        const size_t nb = b.read(right, sizeof(right));
        if (a.failed || b.failed || na != nb || memcmp(left, right, na) ||
            (via_ftp && first_chunk && !gamecube_header(left, na)))
            return false;
        if (na < sizeof(left))
            return true;
        first_chunk = false;
    }
}

static bool copy_file(const char *source, const char *destination, bool verify, bool via_ftp = false)
{
    char pending[800];
    int needed = snprintf(pending, sizeof(pending), "%s.tegelmaker-XXXXXX", destination);
    if (needed < 0 || needed >= static_cast<int>(sizeof(pending)) || exists(destination))
        return false;

    Reader input(source, via_ftp);
    if (!input.ready)
        return false;
    const int fd = mkstemp(pending);
    if (fd < 0)
        return false;
    fchmod(fd, 0644);
    FILE *output = fdopen(fd, "wb");
    if (!output) {
        close(fd);
        unlink(pending);
        return false;
    }

    static unsigned char bytes[256 * 1024];
    uint64_t copied = 0;
    bool ok = true;
    size_t read_count;
    while ((read_count = input.read(bytes, sizeof(bytes))) > 0) {
        if (via_ftp && !copied && !gamecube_header(bytes, read_count)) {
            ok = false;
            break;
        }
        if (fwrite(bytes, 1, read_count, output) != read_count) {
            ok = false;
            break;
        }
        copied += read_count;
        if ((copied & ((32u << 20) - 1)) < read_count) {
            snprintf(status, sizeof(status), "%llu MiB gekopieerd...", static_cast<unsigned long long>(copied >> 20));
            draw();
        }
    }
    ok = ok && !input.failed && (!via_ftp || copied > 0);
    if (fflush(output) || fsync(fileno(output)))
        ok = false;
    if (fclose(output))
        ok = false;
    if (ok && verify) {
        snprintf(status, sizeof(status), "Bron en kopie volledig vergelijken...");
        draw();
        ok = same_file(source, pending, via_ftp);
    }
    if (!ok || exists(destination) || rename(pending, destination)) {
        unlink(pending);
        return false;
    }
    return true;
}

static void transfer_game(Game chosen, bool move)
{
    if (chosen.internal) {
        snprintf(status, sizeof(status), "Kies voor kopieren of verplaatsen een USB-game.");
        return;
    }
    char destination[768];
    const char *filename = basename_of(chosen.path);
    const int length = snprintf(destination, sizeof(destination), "%s/%s", Library, filename);
    if (length < 0 || length >= static_cast<int>(sizeof(destination))) {
        snprintf(status, sizeof(status), "De bestandsnaam is te lang.");
        return;
    }
    if (!strcasecmp(filename, "game.iso")) {
        snprintf(destination, sizeof(destination), "%s/%s.iso", Library, chosen.name);
    }
    if (exists(destination)) {
        snprintf(status, sizeof(status), "Bestaande interne kopie volledig vergelijken...");
        draw();
        if (!same_file(chosen.path, destination, chosen.via_ftp)) {
            snprintf(status, sizeof(status), "Naam bestaat intern met andere inhoud. USB-bestand behouden.");
            return;
        }
    } else {
        snprintf(status, sizeof(status), "Game naar interne schijf kopieren...");
        draw();
        if (!copy_file(chosen.path, destination, true, chosen.via_ftp)) {
            snprintf(status, sizeof(status), "Kopie mislukt. Controleer USB en vrije ruimte. USB-bestand behouden.");
            return;
        }
    }
    if (move) {
        UsbFtp ftp;
        sys::Elevated elevated;
        const bool removed = chosen.via_ftp ? (ftp.login(ftp_address) && ftp.remove_usb_file(chosen.path))
                                            : unlink(chosen.path) == 0;
        if (!removed) {
            snprintf(status, sizeof(status), "Interne kopie gecontroleerd. USB kon niet gewist worden.");
            return;
        }
    }
    scan(destination);
    snprintf(status, sizeof(status), "%s. Druk op X bij de interne game om een tegel klaar te zetten.",
             move ? "Verplaatst" : "Gekopieerd");
}

static bool assign(const Game &game)
{
    char path[80], pending[96];
    snprintf(path, sizeof(path), "%s/%s.txt", Mappings, game.tile->disc);
    snprintf(pending, sizeof(pending), "%s.tmp", path);
    FILE *file = fopen(pending, "wb");
    if (!file)
        return false;
    bool ok = fprintf(file, "%s\n", game.path) > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file))
        ok = false;
    if (!ok || rename(pending, path)) {
        unlink(pending);
        return false;
    }
    return true;
}

// stat through sys::stat_fixed: this libc's struct stat layout differs from what the kernel fills in.
static long long file_size(const char *path)
{
    struct stat info;
    return sys::stat_fixed(path, &info) == 0 ? static_cast<long long>(info.st_size) : -1;
}

static bool directory_exists(const char *path)
{
    struct stat info;
    return sys::stat_fixed(path, &info) == 0 && S_ISDIR(info.st_mode);
}

// Put a tile package in GoldHEN's installer folder. An older copy with other content
// (for example a tile from before the splash fix) is replaced.
static bool stage_package(const char *source, const char *destination, const char *label)
{
    if (!exists(source)) {
        snprintf(status, sizeof(status), "Het pakket voor %s ontbreekt in %s.", label, Templates);
        return false;
    }
    if (exists(destination)) {
        snprintf(status, sizeof(status), "Klaargezet pakket voor %s controleren...", label);
        draw();
        if (file_size(source) == file_size(destination) && same_file(source, destination))
            return true;
        unlink(destination);
    }
    mkdir(Installer, 0777);
    snprintf(status, sizeof(status), "Installatiepakket voor %s klaarzetten...", label);
    draw();
    if (!copy_file(source, destination, true)) {
        snprintf(status, sizeof(status), "Kan het installatiepakket niet klaarzetten. Controleer vrije ruimte.");
        return false;
    }
    return true;
}

// Turn a cover image into a 512x512 tile icon that fills the square (same as the editor's start).
static bool make_icon(const char *cover, const char *target)
{
    Picture picture;
    if (!load_picture("", nullptr, 0, cover, picture))
        return false;
    std::vector<uint8_t> icon;
    render_icon(picture, 1.0f, 0, 0, icon);
    return write_png(target, icon);
}

static bool read_line(const char *path, char *out, size_t size)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    const bool ok = fgets(out, static_cast<int>(size), file) != nullptr;
    fclose(file);
    out[strcspn(out, "\r\n")] = 0;
    return ok && out[0];
}

static bool write_line(const char *path, const char *value)
{
    char pending[96];
    snprintf(pending, sizeof(pending), "%s.tmp", path);
    FILE *file = fopen(pending, "wb");
    if (!file)
        return false;
    bool ok = fprintf(file, "%s\n", value) > 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file))
        ok = false;
    if (!ok || rename(pending, path)) {
        unlink(pending);
        return false;
    }
    return true;
}

static constexpr char IconFolder[] = "/data/PS4Tegelmaker/icons";

// The icon chosen in the editor for a game is kept here, keyed by the game's path.
static void custom_icon_path(const Game &game, char (&path)[96])
{
    uint64_t hash = 1469598103934665603ull;
    for (const char *c = game.path; *c; ++c) {
        hash ^= static_cast<uint8_t>(*c);
        hash *= 1099511628211ull;
    }
    snprintf(path, sizeof(path), "%s/%016llx.png", IconFolder, static_cast<unsigned long long>(hash));
}

static bool copy_small(const char *source, const char *destination)
{
    FILE *in = fopen(source, "rb");
    if (!in)
        return false;
    std::vector<uint8_t> data;
    uint8_t buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
        data.insert(data.end(), buffer, buffer + n);
    fclose(in);
    char pending[192];
    snprintf(pending, sizeof(pending), "%s.tmp", destination);
    FILE *out = fopen(pending, "wb");
    if (!out)
        return false;
    bool ok = fwrite(data.data(), 1, data.size(), out) == data.size();
    if (fclose(out))
        ok = false;
    if (!ok || rename(pending, destination)) {
        unlink(pending);
        return false;
    }
    return true;
}

// A name chosen for a game's tile is kept here, keyed by the game's path like the icon.
static constexpr char NameFolder[] = "/data/PS4Tegelmaker/names";
static void custom_name_path(const Game &game, char (&path)[96])
{
    char icon[96];
    custom_icon_path(game, icon);
    const char *file = strrchr(icon, '/') + 1;
    snprintf(path, sizeof(path), "%s/%.16s.txt", NameFolder, file);
}

// The name a tile gets without a chosen one: the catalog's, or the ROM's without its tags
// ("Mario Kart DS (USA) (En,Fr)" -> "Mario Kart DS").
static std::string default_title(const Game &game)
{
    if (game.tile)
        return game.tile->title;
    std::string name = game.name;
    const size_t cut = std::min(name.find(" ("), name.find(" ["));
    if (cut != std::string::npos && cut > 0)
        name.erase(cut);
    return name.substr(0, 120);
}

static std::string tile_title(const Game &game)
{
    char path[96], line[256];
    custom_name_path(game, path);
    if (read_line(path, line, sizeof(line)) && line[0])
        return std::string(line).substr(0, 120);
    return default_title(game);
}

// Store a chosen name; an empty one (or the default) goes back to the default.
static bool save_title(const Game &game, const std::string &name)
{
    char path[96];
    custom_name_path(game, path);
    if (name.empty() || name == default_title(game)) {
        unlink(path);
        return true;
    }
    mkdir(NameFolder, 0777);
    return write_line(path, name.substr(0, 120).c_str());
}

// Write a tile's icon; logs when the PS4 refuses.
// Write a tile's icon and make sure it stays: right after an install the installer may still write
// the package's own icon over it, so check a few times and write it again when needed.
static bool put_icon(const char *title_id, const char *source_png, bool render_cover, bool just_installed = false)
{
    // A cover is first made into a 512x512 icon file, so it can be compared later.
    const char *icon_file = source_png;
    if (render_cover) {
        icon_file = "/data/PS4Tegelmaker/icon-tmp.png";
        if (!make_icon(source_png, icon_file)) {
            sys::log("icoon %s: hoes %s onleesbaar", title_id, source_png);
            return false;
        }
    }
    char meta[48], icon[96];
    snprintf(meta, sizeof(meta), "/user/appmeta/%s", title_id);
    snprintf(icon, sizeof(icon), "%s/icon0.png", meta);
    bool ok = false;
    for (int round = 0; round < (just_installed ? 6 : 1); ++round) {
        if (round) {
            snprintf(status, sizeof(status), "Icoon vastzetten...");
            draw();
            usleep(2 * 1000 * 1000);
        }
        sys::Elevated elevated;
        if (round && file_size(icon) == file_size(icon_file) && same_file(icon_file, icon))
            continue;  // still ours
        mkdir(meta, 0777);
        ok = copy_small(icon_file, icon);
        sys::log("icoon %s <- %s: %s%s", title_id, source_png, ok ? "gezet" : "mislukt", round ? " (opnieuw)" : "");
    }
    sys::Elevated elevated;
    return file_size(icon) == file_size(icon_file) && same_file(icon_file, icon);
}

// Installs a tile package from the app (BGFT) and then gives the tile its name and icon.
// rename: general tiles get the game's name; catalog tiles have the right name already.
// Returns false when it could not be installed (then the package is put ready for Package Installer).
static bool place_tile(const char *title_id, const char *package, const char *title, bool rename,
                       const char *custom_icon, const char *cover)
{
    sys::log("tegel %s (\"%s\") uit %s", title_id, title, package);
    if (!exists(package)) {
        snprintf(status, sizeof(status), "Het tegelpakket ontbreekt: %s", package);
        sys::log("pakket ontbreekt");
        return false;
    }
    std::string error;
    // A tile that is already there (same or newer version) is not installed again: a new install
    // brings back the package's own name and icon, which the home screen then keeps until a restart.
    const std::string have = sys::installed_version(title_id), ours = sys::package_version(package);
    sys::Install result = sys::Install::AlreadyThere;
    if (have.empty() || ours.empty() || have < ours) {
        snprintf(status, sizeof(status), "%s installeren...", title);
        draw();
        result = sys::install(package, title_id, title, error, [&](int percent) {
            snprintf(status, sizeof(status), "%s installeren... %d%%", title, percent);
            draw();
        });
    } else {
        sys::log("tegel %s staat er al (versie %s): niet opnieuw installeren", title_id, have.c_str());
    }
    if (result == sys::Install::Failed) {
        // Fall back to GoldHEN's Package Installer.
        char destination[200];
        snprintf(destination, sizeof(destination), "%s/%s [%s].pkg", Installer, title, title_id);
        if (stage_package(package, destination, title))
            snprintf(status, sizeof(status), "Zelf installeren lukte niet (%s). Installeer \"%s\" via GoldHEN Package Installer.",
                     error.c_str(), title);
        return false;
    }
    bool named = !rename || sys::set_title(title_id, title);
    bool iconed = true;
    const bool fresh = result == sys::Install::Done;
    if (custom_icon && exists(custom_icon))
        iconed = put_icon(title_id, custom_icon, false, fresh);
    else if (cover && exists(cover))
        iconed = put_icon(title_id, cover, true, fresh);
    // The system copies the name back from the package after an install; check and set it again.
    for (int round = 0; rename && named && round < 4; ++round) {
        const std::string now = sys::db_title(title_id);
        if (now == title)
            break;
        sys::log("naam van %s staat weer op \"%s\": opnieuw zetten", title_id, now.c_str());
        snprintf(status, sizeof(status), "Naam vastzetten...");
        draw();
        usleep(2 * 1000 * 1000);
        named = sys::set_title(title_id, title);
    }
    // The home screen keeps the name and icon it read at install time until the PS4 restarts.
    snprintf(status, sizeof(status), "%s staat klaar%s%s. Herstart de PS4 om de naam en het icoon op je home screen te zien.",
             title, named ? "" : " (naam niet aangepast)", iconed ? "" : " (icoon niet aangepast)");
    return true;
}

struct SlotKind {
    const char *file_prefix;     // slot file: /data/PS4Tegelmaker/<prefix>NN.txt
    const char *app_prefix;      // title ID: <prefix>0000N
    const char *package_prefix;  // package: /data/PS4Tegelmaker/tiles/<prefix>NN.pkg
    const char *label_prefix;    // tile name: "<prefix> N"
    const char *emulator;
};
static const SlotKind GameCubeSlots{"slot", "TGCU", "SLOT", "GameCube", "DolphinPS4"};
static const SlotKind RetroSlots{"retro-slot", "TGEM", "RETRO", "Retro", "EmuSuNi"};

// The general tile this game uses: the one that already plays it, else a free one,
// else the one that was assigned longest ago.
static int choose_slot(const SlotKind &kind, const Game &game)
{
    int free_slot = 0, oldest = 0;
    time_t oldest_time = 0;
    for (int n = 1; n <= Slots; ++n) {
        char path[80], current[768];
        snprintf(path, sizeof(path), "%s/%s%02d.txt", AppData, kind.file_prefix, n);
        if (!read_line(path, current, sizeof(current))) {
            if (!free_slot)
                free_slot = n;
            continue;
        }
        if (!strcmp(current, game.path))
            return n;
        if (!free_slot && !exists(current))
            free_slot = n;
        struct stat info;
        if (sys::stat_fixed(path, &info) == 0 && (!oldest || info.st_mtime < oldest_time)) {
            oldest = n;
            oldest_time = info.st_mtime;
        }
    }
    return free_slot ? free_slot : (oldest ? oldest : 1);
}

// Give the game a general tile and put its cover on that tile. The cover comes from the
// covers the emulator downloads itself.
static void prepare_general(const SlotKind &kind, const Game &game, const char *cover)
{
    const int slot = choose_slot(kind, game);
    char slot_file[80], label[32], app[48];
    snprintf(slot_file, sizeof(slot_file), "%s/%s%02d.txt", AppData, kind.file_prefix, slot);
    snprintf(label, sizeof(label), "%s %d", kind.label_prefix, slot);
    snprintf(app, sizeof(app), "/user/app/%s%05d", kind.app_prefix, slot);
    if (!write_line(slot_file, game.path)) {
        snprintf(status, sizeof(status), "Kan de tegel %s niet toewijzen.", label);
        return;
    }
    (void)app;
    char source[96], id[16], custom[96];
    snprintf(source, sizeof(source), "%s/%s%02d.pkg", Templates, kind.package_prefix, slot);
    snprintf(id, sizeof(id), "%s%05d", kind.app_prefix, slot);
    custom_icon_path(game, custom);
    const std::string title = tile_title(game);
    place_tile(id, source, title.c_str(), true, custom, cover);
    if (!(cover && exists(cover)) && !exists(custom))
        snprintf(status + strlen(status), sizeof(status) - strlen(status),
                 " Geen hoes gevonden: kies er een met Options.");
}

// GameCube games outside the catalog: cover from DolphinPS4's covers.
static void prepare_slot(const Game &game)
{
    char disc[7];
    if (!read_disc(game.path, disc)) {
        snprintf(status, sizeof(status), "Dit bestand is geen leesbare GameCube-disc.");
        return;
    }
    char cover[128];
    snprintf(cover, sizeof(cover), "%s/%s.png", Covers, disc);
    prepare_general(GameCubeSlots, game, cover);
}

// Retrocade games (NDS, 3DS, GBA, ...): EmuSuNi plays them; cover from EmuSuNi's covers,
// which are named after the ROM file: covers/<console>/<file name without extension>.png.
static constexpr char EmuSuNiMenu[] = "/data/EmuSuNi/xmb.ini";

// EmuSuNi only starts games that are in its library. Add Retrocade's folder for this console to
// EmuSuNi's settings when that console has no game folder yet.
static bool ensure_library_folder(const Game &game)
{
    char folder[256], key[48];
    snprintf(folder, sizeof(folder), "%s/%s", RetroGames, game.system);
    snprintf(key, sizeof(key), "games_dir.%s=", game.system);
    std::string text;
    if (FILE *in = fopen(EmuSuNiMenu, "rb")) {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
            text.append(buffer, n);
        fclose(in);
    }
    size_t at = 0;
    while ((at = text.find(key, at)) != std::string::npos && at && text[at - 1] != '\n')
        ++at;
    if (at != std::string::npos) {
        const size_t start = at + strlen(key), end = text.find_first_of("\r\n", start);
        const std::string value = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (value == folder)
            return true;
        if (!value.empty()) {
            snprintf(status, sizeof(status), "EmuSuNi zoekt %s-games in %s. Zet die map in EmuSuNi op %s.",
                     game.system, value.c_str(), folder);
            return false;
        }
        text.erase(at, end == std::string::npos ? std::string::npos : end - at);  // an empty entry
    } else if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    if (at == std::string::npos)
        text += std::string(key) + folder + "\n";
    else
        text.insert(at, std::string(key) + folder);
    char pending[64];
    snprintf(pending, sizeof(pending), "%s.tmp", EmuSuNiMenu);
    FILE *out = fopen(pending, "wb");
    if (!out)
        return false;
    bool ok = fwrite(text.data(), 1, text.size(), out) == text.size();
    if (fclose(out))
        ok = false;
    if (!ok || rename(pending, EmuSuNiMenu)) {
        unlink(pending);
        snprintf(status, sizeof(status), "Kan de gamemap niet aan EmuSuNi toevoegen.");
        return false;
    }
    return true;
}

static void prepare_retro(const Game &game)
{
    sys::log("retro: %s", game.path);
    if (!ensure_library_folder(game)) {
        sys::log("retro: gamemap niet toegevoegd: %s", status);
        return;
    }
    char cover[512];
    const int length = snprintf(cover, sizeof(cover), "%s/%s/%s.png", RetroCovers, game.system, game.name);
    prepare_general(RetroSlots, game, length > 0 && length < static_cast<int>(sizeof(cover)) ? cover : nullptr);
}

// The tile that currently plays this game, as title ID; nullptr when it has none yet.
static const char *tile_for(const Game &game, char (&id)[16])
{
    if (game.tile) {
        snprintf(id, sizeof(id), "TGCG%05d", static_cast<int>(game.tile - Catalog) + 1);
        return id;
    }
    const SlotKind &kind = game.system[0] ? RetroSlots : GameCubeSlots;
    for (int n = 1; n <= Slots; ++n) {
        char path[80], current[768];
        snprintf(path, sizeof(path), "%s/%s%02d.txt", AppData, kind.file_prefix, n);
        if (read_line(path, current, sizeof(current)) && !strcmp(current, game.path)) {
            snprintf(id, sizeof(id), "%s%05d", kind.app_prefix, n);
            return id;
        }
    }
    return nullptr;
}

// Put every chosen icon on its tile, so a tile installed after choosing gets it too.
static void sync_icons()
{
    sys::Elevated elevated;
    int applied = 0;
    for (int i = 0; i < count; ++i) {
        const Game &game = games[i];
        if (!game.internal)
            continue;
        char custom[96], id[16];
        custom_icon_path(game, custom);
        if (!tile_for(game, id))
            continue;
        // Tiles carry the game's (chosen) name; the system may have put the package's name back.
        char app_dir[48];
        snprintf(app_dir, sizeof(app_dir), "/user/app/%s", id);
        if (directory_exists(app_dir)) {
            const std::string title = tile_title(game), now = sys::db_title(id);
            if (!now.empty() && now != title)
                sys::set_title(id, title.c_str());
        }
        if (!exists(custom))
            continue;
        char app[48], meta[48], icon[96];
        snprintf(app, sizeof(app), "/user/app/%s", id);
        snprintf(meta, sizeof(meta), "/user/appmeta/%s", id);
        snprintf(icon, sizeof(icon), "%s/icon0.png", meta);
        if (!directory_exists(app) || (file_size(icon) == file_size(custom) && same_file(custom, icon)))
            continue;
        mkdir(meta, 0777);
        const bool ok = copy_small(custom, icon);
        sys::log("icoon %s bijwerken: %s", id, ok ? "gezet" : "mislukt");
        applied += ok;
    }
    if (applied) {
        const size_t used = strlen(status);
        snprintf(status + used, sizeof(status) - used, " %d gekozen icoon/iconen op tegels gezet.", applied);
    }
}

static void open_editor(int index)
{
    const Game &game = games[index];
    if (!game.internal) {
        snprintf(status, sizeof(status), "Zet de game eerst op de interne schijf (X of Vierkant).");
        return;
    }
    std::vector<ArtSource> sources;
    char local[512];
    if (game.system[0]) {
        sources = art::libretro_sources(game.system, game.name);
        snprintf(local, sizeof(local), "%s/%s/%s.png", RetroCovers, game.system, game.name);
    } else {
        char disc[7];
        if (!read_disc(game.path, disc)) {
            snprintf(status, sizeof(status), "Dit bestand is geen leesbare GameCube-disc.");
            return;
        }
        sources = art::gametdb_sources(disc);
        for (const ArtSource &source : art::libretro_gamecube_sources(basename_of(game.path)))
            sources.push_back(source);
        snprintf(local, sizeof(local), "%s/%s.png", Covers, disc);
    }
    editor.pictures.clear();
    Picture picture;
    char custom[96];
    custom_icon_path(game, custom);
    if (exists(custom) && load_picture("Huidig eigen icoon", nullptr, 0, custom, picture))
        editor.pictures.push_back(std::move(picture));
    if (exists(local) && load_picture(game.system[0] ? "EmuSuNi-hoes" : "DolphinPS4-hoes", nullptr, 0, local, picture))
        editor.pictures.push_back(std::move(picture));
    std::vector<std::string> kinds_found;
    std::vector<uint8_t> body;
    int tried = 0, offline = 0;
    const bool only_steamgriddb = online::settings.only_steamgriddb && online::settings.steamgriddb &&
                                  !online::settings.steamgriddb_key.empty();
    if (only_steamgriddb)
        sources.clear();  // Settings: only SteamGridDB (faster)
    for (const ArtSource &source : sources) {
        ++tried;
        // GameTDB lists every kind for a few regions; keep the first region that has it.
        const std::string kind = source.label.substr(0, source.label.find(" ("));
        if (std::find(kinds_found.begin(), kinds_found.end(), kind) != kinds_found.end())
            continue;
        snprintf(status, sizeof(status), "Afbeeldingen ophalen... %d/%d", tried, static_cast<int>(sources.size()));
        draw();
        const int code = art::get(source.host, source.path, body);
        if (code < 0)
            ++offline;
        if (code == 200 && load_picture(source.label.c_str(), body.data(), body.size(), nullptr, picture)) {
            editor.pictures.push_back(std::move(picture));
            kinds_found.push_back(kind);
        }
    }
    // SteamGridDB (needs a free API key, see Settings): app icons, icons, covers, banners and logos.
    std::string online_note;
    if (online::settings.steamgriddb && !online::settings.steamgriddb_key.empty()) {
        snprintf(status, sizeof(status), "SteamGridDB doorzoeken...");
        draw();
        const std::string search = game.tile ? game.tile->title : tile_title(game);
        const std::vector<online::Image> found = online::steamgriddb_images(search, online_note, [](const char *kind) {
            snprintf(status, sizeof(status), "SteamGridDB doorzoeken (%s)...", kind);
            draw();
        });
        sys::log("SteamGridDB \"%s\": %zu afbeelding(en)%s%s", search.c_str(), found.size(),
                 online_note.empty() ? "" : ": ", online_note.c_str());
        for (const online::Image &image : found) {
            Picture lazy;
            lazy.label = image.label;
            lazy.thumb_url = image.thumb;
            lazy.full_url = image.url;
            lazy.thumb_state = 0;
            lazy.full = false;
            editor.pictures.push_back(std::move(lazy));
        }
    } else if (online::settings.steamgriddb_key.empty()) {
        online_note = "Meer keuze (iconen, banners): stel SteamGridDB in met Vierkant";
    }
    if (editor.pictures.empty()) {
        snprintf(status, sizeof(status), offline ? "Geen internet: er zijn geen afbeeldingen opgehaald."
                                                 : "Geen afbeeldingen gevonden voor deze game.");
        return;
    }
    editor.open = true;
    editor.adjusting = false;
    editor.grid = false;
    editor.from_grid = false;
    editor.game = index;
    editor.current = 0;
    editor.zoom = 1.0f;
    editor.dx = editor.dy = 0;
    editor.dirty = true;
    snprintf(status, sizeof(status), "%d afbeelding(en): kies met de pijltjes, Vierkant toont ze als raster.%s%s",
             static_cast<int>(editor.pictures.size()), online_note.empty() ? "" : " ", online_note.c_str());
}

static void save_editor()
{
    const Game &game = games[editor.game];
    if (editor.dirty) {
        render_icon(editor.pictures[editor.current], editor.zoom, editor.dx, editor.dy, editor.icon);
        editor.dirty = false;
    }
    mkdir(IconFolder, 0777);
    char custom[96];
    custom_icon_path(game, custom);
    if (!write_png(custom, editor.icon)) {
        snprintf(status, sizeof(status), "Kan het icoon niet opslaan.");
        return;
    }
    editor.open = false;
    char id[16];
    if (!tile_for(game, id)) {
        snprintf(status, sizeof(status), "Icoon opgeslagen. Druk op X om de tegel te maken; het icoon gaat mee.");
        return;
    }
    char app[48], meta[48], icon[96];
    snprintf(app, sizeof(app), "/user/app/%s", id);
    snprintf(meta, sizeof(meta), "/user/appmeta/%s", id);
    snprintf(icon, sizeof(icon), "%s/icon0.png", meta);
    if (!directory_exists(app)) {
        snprintf(status, sizeof(status), "Icoon opgeslagen. Installeer de tegel en druk daarna op X; dan komt het icoon erop.");
        return;
    }
    sys::Elevated elevated;
    mkdir(meta, 0777);
    if (!write_png(icon, editor.icon)) {
        snprintf(status, sizeof(status), "Icoon opgeslagen, maar het kon niet op de tegel worden gezet.");
        return;
    }
    snprintf(status, sizeof(status), "Icoon op de tegel gezet. Zie je het oude icoon nog, herstart dan de PS4.");
}

// Choosing: the list (with a preview) or a full-screen grid; Square switches and keeps the selection.
// X takes the picture into adjusting (move, zoom); Circle there goes back to where it came from.
static void editor_input(uint32_t pressed, uint32_t held)
{
    const int total = static_cast<int>(editor.pictures.size());
    auto reset = [] {
        editor.zoom = 1.0f;
        editor.dx = editor.dy = 0;
        editor.dirty = true;
    };
    if (!editor.adjusting) {
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            editor.open = false;
            snprintf(status, sizeof(status), "Icoon niet gewijzigd.");
            return;
        }
        if (pressed & ORBIS_PAD_BUTTON_SQUARE) {
            editor.grid = !editor.grid;
            editor.dirty = true;
            return;
        }
        int step = 0;
        if (editor.grid) {
            if (pressed & ORBIS_PAD_BUTTON_RIGHT) step = 1;
            if (pressed & ORBIS_PAD_BUTTON_LEFT) step = -1;
            if (pressed & ORBIS_PAD_BUTTON_DOWN) step = ui::GridColumns;
            if (pressed & ORBIS_PAD_BUTTON_UP) step = -ui::GridColumns;
            if (pressed & ORBIS_PAD_BUTTON_R1) step = ui::GridColumns * ui::GridRows;
            if (pressed & ORBIS_PAD_BUTTON_L1) step = -ui::GridColumns * ui::GridRows;
            if (step && total) {
                const int next = std::min(total - 1, std::max(0, editor.current + step));
                if (next != editor.current) {
                    editor.current = next;
                    reset();
                }
            }
        } else {
            if (pressed & (ORBIS_PAD_BUTTON_DOWN | ORBIS_PAD_BUTTON_RIGHT)) step = 1;
            if (pressed & (ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_LEFT)) step = -1;
            if (pressed & ORBIS_PAD_BUTTON_R1) step = 10;
            if (pressed & ORBIS_PAD_BUTTON_L1) step = -10;
            if (step && total) {
                editor.current = (editor.current + step % total + total) % total;
                reset();
            }
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            editor.from_grid = editor.grid;
            editor.adjusting = true;
            snprintf(status, sizeof(status), "Verplaats en zoom de afbeelding; X slaat hem op als icoon.");
            load_full_picture(editor.pictures[editor.current]);
            reset();
        }
        return;
    }
    if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
        editor.adjusting = false;
        editor.grid = editor.from_grid;
        snprintf(status, sizeof(status), "Kies een afbeelding en druk op X; Vierkant wisselt lijst en raster.");
        return;
    }
    if (pressed & ORBIS_PAD_BUTTON_CROSS) {
        save_editor();
        return;
    }
    if (pressed & ORBIS_PAD_BUTTON_TRIANGLE)
        reset();
    if (pressed & ORBIS_PAD_BUTTON_SQUARE) {  // the whole picture inside the square
        const Picture &picture = editor.pictures[editor.current];
        const float fill = std::max(float(IconSize) / picture.width, float(IconSize) / picture.height);
        const float fit = std::min(float(IconSize) / picture.width, float(IconSize) / picture.height);
        editor.zoom = fit / fill;
        editor.dx = editor.dy = 0;
        editor.dirty = true;
    }
    const float move = 6.0f;
    if (held & ORBIS_PAD_BUTTON_LEFT) { editor.dx -= move; editor.dirty = true; }
    if (held & ORBIS_PAD_BUTTON_RIGHT) { editor.dx += move; editor.dirty = true; }
    if (held & ORBIS_PAD_BUTTON_UP) { editor.dy -= move; editor.dirty = true; }
    if (held & ORBIS_PAD_BUTTON_DOWN) { editor.dy += move; editor.dirty = true; }
    if (held & ORBIS_PAD_BUTTON_L2) { editor.zoom = std::max(0.1f, editor.zoom / 1.02f); editor.dirty = true; }
    if (held & ORBIS_PAD_BUTTON_R2) { editor.zoom = std::min(8.0f, editor.zoom * 1.02f); editor.dirty = true; }
}

static void prepare_tile(const Game &game)
{
    if (game.system[0]) {
        prepare_retro(game);
        return;
    }
    if (!game.tile) {
        prepare_slot(game);
        return;
    }
    char disc[7];
    if (!read_disc(game.path, disc) || strcmp(disc, game.tile->disc)) {
        snprintf(status, sizeof(status), "Dit interne bestand is gewijzigd. Zoek de games opnieuw.");
        return;
    }
    if (!assign(game)) {
        snprintf(status, sizeof(status), "Kan het pad naar de interne game niet opslaan.");
        return;
    }

    char source[96], id[16], custom[96];
    snprintf(source, sizeof(source), "%s/%s.pkg", Templates, disc);
    snprintf(id, sizeof(id), "TGCG%05d", static_cast<int>(game.tile - Catalog) + 1);
    custom_icon_path(game, custom);
    const std::string title = tile_title(game);
    place_tile(id, source, title.c_str(), title != game.tile->title, custom, nullptr);
}

// Back to the cover the emulator has (also on the tile, when it is installed).
static void reset_icon(const Game &game)
{
    char custom[96], id[16];
    custom_icon_path(game, custom);
    unlink(custom);
    if (!tile_for(game, id)) {
        snprintf(status, sizeof(status), "Eigen icoon verwijderd.");
        return;
    }
    char cover[640] = "", icon[96];
    if (game.system[0]) {
        snprintf(cover, sizeof(cover), "%s/%s/%s.png", RetroCovers, game.system, game.name);
    } else {
        char disc[7];
        if (read_disc(game.path, disc))
            snprintf(cover, sizeof(cover), "%s/%s.png", Covers, disc);
    }
    snprintf(icon, sizeof(icon), "/user/appmeta/%s/icon0.png", id);
    sys::Elevated elevated;
    const bool done = cover[0] && exists(cover) && make_icon(cover, icon);
    snprintf(status, sizeof(status), done ? "Standaardicoon teruggezet. Zie je het oude nog, herstart dan de PS4."
                                          : "Eigen icoon verwijderd.");
}

static int ef_app_main()
{
    mkdir(AppData, 0777);
    ef_install_crash_handler();
    sys::log("EmuForwarder " EF_VERSION " start (build %d)", EF_BUILD);
    online::load_settings();
    mkdir(Mappings, 0777);
    mkdir("/data/DolphinPS4", 0777);
    mkdir(Library, 0777);
    ef_step = "scherm";
    scene = new Scene2D(1920, 1080, 4);
    if (!scene->Init(0x4000000, 2)) {
        sys::log("scherm starten mislukt");
        return 1;
    }
    // Scene2D (OpenOrbis sample code) never sets its active frame buffer: with reused heap memory the
    // first drawing went to a random "buffer" and crashed (SIGBUS, 2026-11-18). Start at buffer 0.
    scene->SetActiveFrameBuffer(0);

    const char *font_path = "/app0/assets/fonts/Gontserrat-Regular.ttf";
    if (!scene->InitFont(&ui::font_title, font_path, 58) || !scene->InitFont(&ui::font_large, font_path, 36) ||
        !scene->InitFont(&ui::font_body, font_path, 28) || !scene->InitFont(&ui::font_small, font_path, 22)) {
        sys::log("letters laden mislukt");
        return 1;
    }
    {
        Picture icon;
        if (load_picture("", nullptr, 0, "/app0/sce_sys/icon0.png", icon))
            render_square(icon, 96, app_icon);
    }
    ui::on_offscreen = [](const char *where, int x, int y, int w, int h) {
        static int reported = 0;
        if (reported++ < 20)
            sys::log("buiten beeld (%s): rechthoek x %d y %d b %d h %d", where, x, y, w, h);
    };

    sceUserServiceInitialize(NULL);
    int32_t user = -1;
    sceUserServiceGetInitialUser(&user);
    scePadInit();
    const int pad = scePadOpen(user, 0, 0, NULL);
    uint32_t previous = 0;
    ef_step = "zoeken";
    scan();
    sys::log("zoeken klaar: %d game(s)", count);
    ef_step = "eerste scherm";
    draw();
    sys::log("eerste scherm getekend");
    // The system keeps its boot splash on top until the app hides it; without this
    // the app keeps running underneath while the screen stays on the boot image.
    sceSystemServiceHideSplashScreen();
    update::remove_old_files();
    ensure_updater();
    mkdir(music::Folder, 0777);
    music::enabled = online::settings.music;
    music::volume = online::settings.music_volume;
    music::start();
    if (online::settings.auto_update)
        update::check();
    bool update_reported = false;

    for (;;) {
        // The update check runs in the background; ask once when it found something.
        if (update::done && !update_reported) {
            update_reported = true;
            if (update::newer()) {
                sys::log("update gevonden: %s (build %d)", update::latest.version.c_str(), update::latest.build);
                if (!editor.open && !menu.open)
                    open_update_question();
                else
                    snprintf(status, sizeof(status), "Nieuwe versie %s: Vierkant > Bijwerken.", update::latest.version.c_str());
                draw();
            } else if (!update::check_error.empty()) {
                sys::log("updates zoeken: %s", update::check_error.c_str());
                if (menu.open && menu.settings)
                    snprintf(status, sizeof(status), "Updates zoeken lukte niet: %s.", update::check_error.c_str());
            } else if (menu.open && menu.settings) {
                snprintf(status, sizeof(status), "Je hebt de nieuwste versie (" EF_VERSION ").");
            }
            if (menu.open && menu.settings)
                open_settings(menu.selected);
        }
        if (update::checking)
            update_reported = false;

        OrbisPadData input{};
        const uint32_t buttons = scePadReadState(pad, &input) >= 0 ? input.buttons : 0;
        const uint32_t pressed = buttons & ~previous;
        previous = buttons;
        // Draw only when something changed: a button, a held editor button, new text, a cover loading.
        const uint32_t moving = ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_DOWN | ORBIS_PAD_BUTTON_LEFT |
                                ORBIS_PAD_BUTTON_RIGHT | ORBIS_PAD_BUTTON_L2 | ORBIS_PAD_BUTTON_R2;
        auto present = [&]() {
            if (pressed || (editor.open && editor.loading) || (editor.open && editor.adjusting && (buttons & moving)) || thumbs_pending || strcmp(status, last_status) ||
                frame < 3)
                draw();
            else
                usleep(16 * 1000);
        };
        if (editor.open) {
            editor_input(pressed, buttons);
            if (!editor.open) {  // saved or closed: pictures and tiles may have changed
                refresh_tiles();
                if (editor.game >= 0 && editor.game < count)
                    games[editor.game].thumb_state = 0;
                thumbs_pending = true;
            }
            present();
            continue;
        }
        if (menu.open) {
            const int total = int(menu.items.size());
            if (pressed & ORBIS_PAD_BUTTON_UP)
                menu.selected = (menu.selected + total - 1) % total;
            if (pressed & ORBIS_PAD_BUTTON_DOWN)
                menu.selected = (menu.selected + 1) % total;
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
                menu.open = false;
            else if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                run_menu_action();
                thumbs_pending = true;
            }
            present();
            continue;
        }
        const int position = view_position(), total = int(view.size());
        int next = position;
        if (pressed & ORBIS_PAD_BUTTON_LEFT)
            next = std::max(0, position - 1);
        if (pressed & ORBIS_PAD_BUTTON_RIGHT)
            next = std::min(total - 1, position + 1);
        if (pressed & ORBIS_PAD_BUTTON_UP)
            next = position >= ui::Columns ? position - ui::Columns : position;
        if (pressed & ORBIS_PAD_BUTTON_DOWN)
            next = std::min(total - 1, position + ui::Columns);
        if (total && next != position)
            selected = view[next];
        if (pressed & (ORBIS_PAD_BUTTON_L1 | ORBIS_PAD_BUTTON_R1)) {
            const int tabs = std::max(1, int(tab_keys.size()));
            tab = (tab + ((pressed & ORBIS_PAD_BUTTON_R1) ? 1 : tabs - 1)) % tabs;
            build_view();
            if (!view.empty())
                selected = view[0];
        }
        if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) {
            char keep[768] = "";  // scan() reuses games[], so remember the selected path first
            if (total)
                snprintf(keep, sizeof(keep), "%s", games[selected].path);
            scan(keep[0] ? keep : nullptr);
        }
        if (total && (pressed & ORBIS_PAD_BUTTON_CROSS))
            open_menu();
        if (pressed & ORBIS_PAD_BUTTON_SQUARE)
            open_settings();
        if (total && (pressed & ORBIS_PAD_BUTTON_OPTIONS)) {
            if (games[selected].internal)
                open_editor(selected);
            else
                snprintf(status, sizeof(status), "Zet de game eerst op de interne schijf (X).");
        }
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            if (time(nullptr) - exit_armed < 3) {
                sceSystemServiceLoadExec("exit", NULL);
                return 0;
            }
            exit_armed = time(nullptr);
            snprintf(status, sizeof(status), "Druk nog een keer op Rondje om EmuForwarder te sluiten.");
        }
        present();
    }
}

int main() { return ef_app_main(); }
