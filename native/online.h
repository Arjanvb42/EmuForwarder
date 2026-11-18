#pragma once
// EmuForwarder's settings, HTTPS (the PS4's own libSceHttp + libSceSsl) and SteamGridDB, plus the
// PS4's on-screen keyboard to type the SteamGridDB API key.
//
// SteamGridDB needs a free account: the API key is made at steamgriddb.com -> profile -> preferences -> API.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include <orbis/Http.h>
#include <orbis/ImeDialog.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>

namespace online {

// ---- Settings (/data/PS4Tegelmaker/settings.ini) -------------------------------------------
struct Settings {
    std::string steamgriddb_key;
    bool steamgriddb = true;       // use it in the icon editor when there is a key
    bool only_steamgriddb = false;  // skip GameTDB and libretro (faster)
    bool auto_update = true;        // look for a newer EmuForwarder on GitHub at start
    bool music = true;              // play the files in /data/PS4Tegelmaker/muziek
    int music_volume = 60;          // percent
};

static Settings settings;
static constexpr char SettingsFile[] = "/data/PS4Tegelmaker/settings.ini";

static void load_settings()
{
    FILE *file = fopen(SettingsFile, "r");
    if (!file)
        return;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        char *equals = strchr(line, '=');
        if (!equals)
            continue;
        *equals = 0;
        const std::string key = line, value = equals + 1;
        if (key == "steamgriddb_key")
            settings.steamgriddb_key = value;
        else if (key == "steamgriddb")
            settings.steamgriddb = value != "uit";
        else if (key == "alleen_steamgriddb")
            settings.only_steamgriddb = value == "aan";
        else if (key == "updates_zoeken")
            settings.auto_update = value != "uit";
        else if (key == "muziek")
            settings.music = value != "uit";
        else if (key == "muziek_volume")
            settings.music_volume = std::max(0, std::min(100, atoi(value.c_str())));
    }
    fclose(file);
}

static bool save_settings()
{
    char pending[96];
    snprintf(pending, sizeof(pending), "%s.tmp", SettingsFile);
    FILE *file = fopen(pending, "w");
    if (!file)
        return false;
    fprintf(file, "# EmuForwarder-instellingen\nsteamgriddb_key=%s\nsteamgriddb=%s\nalleen_steamgriddb=%s\nupdates_zoeken=%s\nmuziek=%s\nmuziek_volume=%d\n",
            settings.steamgriddb_key.c_str(), settings.steamgriddb ? "aan" : "uit", settings.only_steamgriddb ? "aan" : "uit",
            settings.auto_update ? "aan" : "uit", settings.music ? "aan" : "uit", settings.music_volume);
    const bool ok = fclose(file) == 0 && rename(pending, SettingsFile) == 0;
    if (!ok)
        unlink(pending);
    return ok;
}

// ---- HTTPS ---------------------------------------------------------------------------------
static int http_context = -1;

static bool https_ready()
{
    if (http_context >= 0)
        return true;
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);
    sceNetInit();
    const int pool = sceNetPoolCreate("EmuForwarder https", 16 * 1024, 0);
    const int ssl = sceSslInit(256 * 1024);
    if (pool < 0 || ssl < 0)
        return false;
    http_context = sceHttpInit(pool, ssl, 256 * 1024);
    return http_context >= 0;
}

// The PS4's certificate store is old; the images are public, so certificates are not checked.
static int32_t accept_certificate(int32_t, uint32_t, void *const[], int32_t, void *) { return 1; }

// GET a URL (http or https). Returns the status code, or -1 when the network failed.
static int get(const std::string &url, const std::vector<std::pair<std::string, std::string>> &headers,
               std::vector<uint8_t> &body, size_t limit = 12 * 1024 * 1024)
{
    body.clear();
    if (!https_ready())
        return -1;
    int status = -1;
    const int tpl = sceHttpCreateTemplate(http_context, "EmuForwarder/0.8 (PS4)", ORBIS_HTTP_VERSION_1_1, 1);
    if (tpl < 0)
        return -1;
    sceHttpsSetSslCallback(tpl, accept_certificate, nullptr);
    sceHttpSetConnectTimeOut(tpl, 10 * 1000 * 1000);
    sceHttpSetResolveTimeOut(tpl, 10 * 1000 * 1000);
    sceHttpSetSendTimeOut(tpl, 15 * 1000 * 1000);
    const int conn = sceHttpCreateConnectionWithURL(tpl, url.c_str(), true);
    if (conn >= 0) {
        const int req = sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url.c_str(), 0);
        if (req >= 0) {
            for (const auto &h : headers)
                sceHttpAddRequestHeader(req, h.first.c_str(), h.second.c_str(), 0 /* overwrite */);
            if (sceHttpSendRequest(req, nullptr, 0) >= 0 && sceHttpGetStatusCode(req, &status) >= 0) {
                uint8_t buffer[32 * 1024];
                for (;;) {
                    const int n = sceHttpReadData(req, buffer, sizeof(buffer));
                    if (n <= 0)
                        break;
                    body.insert(body.end(), buffer, buffer + n);
                    if (body.size() > limit) {
                        body.clear();
                        status = -1;
                        break;
                    }
                }
            }
            sceHttpDeleteRequest(req);
        }
        sceHttpDeleteConnection(conn);
    }
    sceHttpDeleteTemplate(tpl);
    return status;
}

// ---- SteamGridDB ---------------------------------------------------------------------------
struct Image {
    std::string label;
    std::string url;    // the full picture
    std::string thumb;  // a small one, for the overview
    int width = 0, height = 0;
};

static std::string url_encode(const std::string &text)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (isalnum(c) || strchr("-_.~", c))
            out += char(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static std::string json_string(const std::string &object, const char *key)
{
    const std::string needle = std::string("\"") + key + "\":\"";
    const size_t at = object.find(needle);
    if (at == std::string::npos)
        return "";
    std::string value;
    for (size_t i = at + needle.size(); i < object.size() && object[i] != '"'; ++i) {
        if (object[i] == '\\' && i + 1 < object.size())
            ++i;
        value += object[i];
    }
    return value;
}

static long json_number(const std::string &object, const char *key)
{
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = object.find(needle);
    return at == std::string::npos ? -1 : strtol(object.c_str() + at + needle.size(), nullptr, 10);
}

// The objects of SteamGridDB's "data" array (each starts with {"id":).
static std::vector<std::string> json_objects(const std::string &json)
{
    std::vector<std::string> objects;
    size_t at = json.find("{\"id\":");
    while (at != std::string::npos) {
        const size_t next = json.find("{\"id\":", at + 1);
        objects.push_back(json.substr(at, next == std::string::npos ? std::string::npos : next - at));
        at = next;
    }
    return objects;
}

static int api(const std::string &path, std::string &answer)
{
    std::vector<uint8_t> body;
    const int status = get("https://www.steamgriddb.com/api/v2" + path,
                           {{"Authorization", "Bearer " + settings.steamgriddb_key}}, body, 4 * 1024 * 1024);
    answer.assign(body.begin(), body.end());
    return status;
}

// Every picture SteamGridDB has for a game, square app icons first. Only the list is fetched here
// (a few quick requests); the pictures themselves are loaded when they are shown or chosen.
template <typename Progress>
static std::vector<Image> steamgriddb_images(const std::string &title, std::string &error, Progress progress)
{
    std::vector<Image> images;
    if (settings.steamgriddb_key.empty() || !settings.steamgriddb)
        return images;
    std::string answer;
    int status = api("/search/autocomplete/" + url_encode(title), answer);
    if (status == 401 || status == 403) {
        error = "SteamGridDB weigert de sleutel";
        return images;
    }
    const std::vector<std::string> found = json_objects(answer);
    const long id = found.empty() ? -1 : json_number(found[0], "id");
    if (status != 200 || id < 0) {
        error = status < 0 ? "SteamGridDB niet bereikbaar" : "SteamGridDB kent deze game niet";
        return images;
    }
    std::vector<Image> square, icons, tall, heroes, wide, logos;
    auto collect = [&](const char *kind, int page) {
        char path[160];
        snprintf(path, sizeof(path), "/%s/game/%ld?types=static&page=%d", kind, id, page);
        progress(kind);
        if (api(path, answer) != 200)
            return 0;
        int n = 0;
        for (const std::string &object : json_objects(answer)) {
            Image image;
            image.url = json_string(object, "url");
            image.thumb = json_string(object, "thumb");
            image.width = int(json_number(object, "width"));
            image.height = int(json_number(object, "height"));
            const std::string mime = json_string(object, "mime");
            if (image.url.empty() || (mime != "image/png" && mime != "image/jpeg"))
                continue;  // .ico and webp can't be shown here
            if (image.thumb.empty())
                image.thumb = image.url;
            ++n;
            const std::string k = kind;
            if (k == "grids")
                (image.width == image.height ? square : image.height > image.width ? tall : wide).push_back(image);
            else if (k == "icons" && image.width >= 128)
                icons.push_back(image);
            else if (k == "heroes")
                heroes.push_back(image);
            else if (k == "logos")
                logos.push_back(image);
        }
        return n;
    };
    for (int page = 0; page < 4 && collect("grids", page) >= 50; ++page) {}  // 50 per page
    for (int page = 0; page < 2 && collect("icons", page) >= 50; ++page) {}
    collect("heroes", 0);
    collect("logos", 0);
    auto add = [&](std::vector<Image> &list, const char *name) {
        int n = 0;
        for (Image &image : list) {
            char label[96];
            snprintf(label, sizeof(label), "%s %d  (%dx%d)", name, ++n, image.width, image.height);
            image.label = label;
            images.push_back(image);
        }
    };
    add(square, "app-icoon");
    add(icons, "icoon");
    add(tall, "hoes");
    add(heroes, "banner");
    add(wide, "brede banner");
    add(logos, "logo");
    if (images.empty())
        error = "SteamGridDB heeft geen afbeeldingen voor deze game";
    return images;
}

// ---- The PS4's on-screen keyboard ----------------------------------------------------------
// The PS4 keyboard works with UTF-16; wchar_t is 4 bytes with this toolchain, so 16-bit buffers.
// redraw keeps the app's screen going while the keyboard is open.
template <typename Redraw>
static bool keyboard(const char *title, std::string &value, Redraw redraw)
{
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
    static uint16_t text[256], caption[100];
    memset(text, 0, sizeof(text));
    memset(caption, 0, sizeof(caption));
    for (size_t i = 0; i < value.size() && i < 255; ++i)
        text[i] = uint8_t(value[i]);
    for (size_t i = 0; title[i] && i < 99; ++i)
        caption[i] = uint8_t(title[i]);
    OrbisImeDialogSetting param;
    memset(&param, 0, sizeof(param));
    param.userId = 0xFE;
    param.type = ORBIS_TYPE_BASIC_LATIN;
    param.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    param.maxTextLength = 255;
    param.inputTextBuffer = reinterpret_cast<wchar_t *>(text);
    param.title = reinterpret_cast<const wchar_t *>(caption);
    if (sceImeDialogInit(&param, nullptr) < 0)
        return false;
    bool ok = false;
    for (;;) {
        const OrbisDialogStatus state = sceImeDialogGetStatus();
        if (state == ORBIS_DIALOG_STATUS_STOPPED) {
            OrbisDialogResult result;
            memset(&result, 0, sizeof(result));
            sceImeDialogGetResult(&result);
            if (result.endstatus == ORBIS_DIALOG_OK) {
                value.clear();
                for (size_t i = 0; i < 255 && text[i]; ++i)
                    if (text[i] < 128)
                        value += char(text[i]);
                ok = true;
            }
            break;
        }
        if (state == ORBIS_DIALOG_STATUS_NONE)
            break;
        redraw();
    }
    sceImeDialogTerm();
    return ok;
}

}  // namespace online
