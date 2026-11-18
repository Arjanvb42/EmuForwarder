#pragma once
#include "lang.h"
// Artwork downloads for EmuForwarder: plain HTTP (both sources serve it), DNS through the
// PS4 resolver. GameCube discs use GameTDB, Retrocade games use the libretro thumbnails.

#include <algorithm>
#include <ctype.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <vector>

#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

struct ArtSource {
    std::string label;  // shown in the editor, e.g. "GameTDB hoes (US)"
    std::string host;
    std::string path;   // already URL-encoded
};

namespace art {

static bool network_ready()
{
    static int state = 0;  // 0 untried, 1 ready, -1 failed
    if (!state) {
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
        sceNetInit();
        sceNetCtlInit();
        state = 1;
    }
    return state > 0;
}

static bool resolve(const char *host, OrbisNetInAddr *address)
{
    static int pool = -1;
    if (pool < 0)
        pool = sceNetPoolCreate("EmuForwarder DNS", 4 * 1024, 0);
    if (pool < 0)
        return false;
    const OrbisNetId resolver = sceNetResolverCreate("EmuForwarder", pool, 0);
    if (resolver < 0)
        return false;
    const int result = sceNetResolverStartNtoa(resolver, host, address, 5 * 1000 * 1000, 2, 0);
    sceNetResolverDestroy(resolver);
    return result >= 0;
}

// GET http://host/path into body. Returns the HTTP status, or -1 when the network fails.
static int get(const std::string &host, const std::string &path, std::vector<uint8_t> &body,
               size_t limit = 8 * 1024 * 1024)
{
    body.clear();
    if (!network_ready())
        return -1;
    OrbisNetInAddr address{};
    if (!resolve(host.c_str(), &address))
        return -1;
    const int fd = sceNetSocket("EmuForwarder art", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    const timeval timeout{8, 0};
    sceNetSetsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sceNetSetsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in target{};
    target.sin_len = sizeof(target);
    target.sin_family = ORBIS_NET_AF_INET;
    target.sin_port = sceNetHtons(80);
    memcpy(&target.sin_addr, &address, sizeof(address));
    if (sceNetConnect(fd, reinterpret_cast<const OrbisNetSockaddr *>(&target), sizeof(target)) != 0) {
        sceNetSocketClose(fd);
        return -1;
    }
    // HTTP/1.0: no chunked replies, the server closes the connection at the end.
    const std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host +
                                "\r\nUser-Agent: EmuForwarder/0.3\r\nConnection: close\r\n\r\n";
    for (size_t sent = 0; sent < request.size();) {
        const int n = sceNetSend(fd, request.data() + sent, request.size() - sent, 0);
        if (n <= 0) {
            sceNetSocketClose(fd);
            return -1;
        }
        sent += n;
    }
    std::vector<uint8_t> reply;
    uint8_t buffer[16384];
    for (;;) {
        const int n = sceNetRecv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0)
            break;
        reply.insert(reply.end(), buffer, buffer + n);
        if (reply.size() > limit + 16384)
            break;
    }
    sceNetSocketClose(fd);
    static const char separator[] = "\r\n\r\n";
    auto end = std::search(reply.begin(), reply.end(), separator, separator + 4);
    if (reply.size() < 12 || end == reply.end() || memcmp(reply.data(), "HTTP/1.", 7))
        return -1;
    const int status = atoi(reinterpret_cast<const char *>(reply.data()) + 9);
    body.assign(end + 4, reply.end());
    return body.size() > limit ? -1 : status;
}

static std::string encode(const std::string &text)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (isalnum(c) || strchr("-_.~", c)) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// GameTDB region folder for the region letter in a disc ID.
static const char *gametdb_region(char letter)
{
    switch (letter) {
    case 'E': return "US";
    case 'J': return "JA";
    case 'K': return "KO";
    case 'D': return "DE";
    case 'F': return "FR";
    case 'S': return "ES";
    case 'I': return "IT";
    case 'U': return "AU";
    default: return "EN";
    }
}

static std::vector<ArtSource> gametdb_sources(const char *disc)
{
    const char *preferred = gametdb_region(disc[3]);
    const char *regions[] = {preferred, "EN", "US"};
    const char *kinds[][2] = {{"cover", TR("cover", "hoes")}, {"coverfullHQ", TR("full cover HQ", "volledige hoes HQ")},
                              {"coverfull", TR("full cover", "volledige hoes")}, {"cover3D", TR("3D cover", "3D-hoes")}, {"disc", "disc"}};
    std::vector<ArtSource> sources;
    for (auto &kind : kinds)
        for (const char *region : regions) {
            bool seen = false;
            for (const ArtSource &s : sources)
                seen |= s.path == std::string("/wii/") + kind[0] + "/" + region + "/" + disc + ".png";
            if (!seen)
                sources.push_back({std::string("GameTDB ") + kind[1] + " (" + region + ")", "art.gametdb.com",
                                   std::string("/wii/") + kind[0] + "/" + region + "/" + disc + ".png"});
        }
    return sources;  // tried in order; the editor keeps only the first hit per kind
}

// libretro's name for a Retrocade console folder.
static const char *libretro_system(const char *folder)
{
    static const char *const map[][2] = {
        {"gamecube", "Nintendo - GameCube"}, {"nds", "Nintendo - Nintendo DS"}, {"3ds", "Nintendo - Nintendo 3DS"},
        {"gba", "Nintendo - Game Boy Advance"}, {"gb", "Nintendo - Game Boy"},
        {"gbc", "Nintendo - Game Boy Color"}, {"nes", "Nintendo - Nintendo Entertainment System"},
        {"snes", "Nintendo - Super Nintendo Entertainment System"}, {"n64", "Nintendo - Nintendo 64"},
        {"md", "Sega - Mega Drive - Genesis"}, {"genesis", "Sega - Mega Drive - Genesis"},
        {"sms", "Sega - Master System - Mark III"}, {"gg", "Sega - Game Gear"},
        {"dc", "Sega - Dreamcast"}, {"psp", "Sony - PlayStation Portable"},
        {"ps1", "Sony - PlayStation"}, {"psx", "Sony - PlayStation"},
        {"pce", "NEC - PC Engine - TurboGrafx 16"},
    };
    for (auto &entry : map)
        if (!strcasecmp(folder, entry[0]))
            return entry[1];
    return nullptr;
}

static std::vector<ArtSource> libretro_sources(const char *folder, const char *rom_name)
{
    std::vector<ArtSource> sources;
    const char *system = libretro_system(folder);
    if (!system)
        return sources;
    std::string name = rom_name;
    for (char &c : name)  // libretro thumbnail rule
        if (strchr("&*/:`<>?\\|\"", c))
            c = '_';
    const char *kinds[][2] = {{"Named_Boxarts", "boxart"}, {"Named_Logos", "logo"},
                              {"Named_Titles", TR("title screen", "titelscherm")}, {"Named_Snaps", "screenshot"}};
    for (auto &kind : kinds)
        sources.push_back({std::string("libretro ") + kind[1], "thumbnails.libretro.com",
                           "/" + encode(system) + "/" + kind[0] + "/" + encode(name) + ".png"});
    return sources;
}

// libretro names GameCube discs after No-Intro: "Name (USA) (En,Fr,Es)". Disc files are often
// "Name (USA) (En Fr Es) [GKME41].iso": drop the ID and extension, and try commas in the
// language list as well.
static std::vector<ArtSource> libretro_gamecube_sources(const char *file_name)
{
    std::string name = file_name;
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos)
        name.erase(dot);
    const size_t id = name.rfind(" [");
    if (id != std::string::npos && name.back() == ']')
        name.erase(id);
    std::string commas = name;
    const size_t open = commas.rfind('('), close = commas.rfind(')');
    if (open != std::string::npos && close != std::string::npos && close > open) {
        bool languages = true;
        for (size_t i = open + 1; i < close; ++i)
            languages &= isalpha(static_cast<unsigned char>(commas[i])) || commas[i] == ' ';
        if (languages)
            for (size_t i = open + 1; i < close; ++i)
                if (commas[i] == ' ')
                    commas[i] = ',';
    }
    std::vector<ArtSource> sources = libretro_sources("gamecube", commas.c_str());
    if (commas != name)
        for (ArtSource &source : libretro_sources("gamecube", name.c_str()))
            sources.push_back(source);
    return sources;
}

}  // namespace art
