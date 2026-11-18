#pragma once
// Updates from GitHub, without FTP or Package Installer.
//
// The package's eboot.bin is a small starter (loader.c): it starts the newest EmuForwarder, which is
// either the one inside the package (/app0/emuforwarder.self) or a downloaded one in
// /data/PS4Tegelmaker/update/. Updating only means downloading that one file, so the app never has
// to reinstall itself while it runs.
//
// On GitHub (public, so the PS4 needs no login):
//   update/latest.txt         build=13 / version=v0.13 / size=<bytes> / fnv=<16 hex> / notes=<text>
//   update/emuforwarder.self  the program itself

#include <pthread.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "online.h"

#ifndef EF_BUILD
#error "build.sh sets EF_BUILD"
#endif

namespace update {

static constexpr char Base[] = "https://raw.githubusercontent.com/Arjanvb42/EmuForwarder/main/update/";
static constexpr char Folder[] = "/data/PS4Tegelmaker/update";
static constexpr char SelfFile[] = "/data/PS4Tegelmaker/update/emuforwarder.self";
static constexpr char BuildFile[] = "/data/PS4Tegelmaker/update/build.txt";
static constexpr char StartingFile[] = "/data/PS4Tegelmaker/update/starting";  // set by the starter

struct Latest {
    int build = 0;
    std::string version, notes, fnv;
    size_t size = 0;
};

// Filled by the background check; read by the main loop once done is set.
static Latest latest;
static volatile bool checking = false, done = false;
static std::string check_error;

static uint64_t fnv64(const uint8_t *data, size_t size)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool parse(const std::string &text, Latest &out)
{
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
        if (key == "build")
            out.build = atoi(value.c_str());
        else if (key == "version")
            out.version = value;
        else if (key == "size")
            out.size = size_t(strtoull(value.c_str(), nullptr, 10));
        else if (key == "fnv")
            out.fnv = value;
        else if (key == "notes")
            out.notes = value;
    }
    return out.build > 0 && out.size > 0 && out.fnv.size() == 16;
}

static void *check_thread(void *)
{
    std::vector<uint8_t> body;
    const int status = online::get(std::string(Base) + "latest.txt", {}, body, 64 * 1024);
    Latest found;
    if (status == 200 && parse(std::string(body.begin(), body.end()), found))
        latest = found;
    else
        check_error = status < 0 ? "geen verbinding met GitHub" : "GitHub antwoordde " + std::to_string(status);
    checking = false;
    done = true;
    return nullptr;
}

// Look for a newer version in the background.
static void check()
{
    if (checking)
        return;
    if (!online::https_ready()) {  // set up here, on the main thread
        check_error = "netwerk niet klaar";
        done = true;
        return;
    }
    checking = true;
    done = false;
    check_error.clear();
    pthread_t thread;
    if (pthread_create(&thread, nullptr, check_thread, nullptr) == 0)
        pthread_detach(thread);
    else
        check_thread(nullptr);
}

static bool newer() { return latest.build > EF_BUILD; }

// Called after the first screen: when the starter started this program from a download, remember
// the way that worked.
static void started()
{
    FILE *mark = fopen(StartingFile, "r");
    if (!mark)
        return;
    fclose(mark);
    FILE *attempt = fopen("/data/PS4Tegelmaker/update/attempt.txt", "r");
    int route = -1;
    if (attempt) {
        if (fscanf(attempt, "%d", &route) != 1)
            route = -1;
        fclose(attempt);
    }
    FILE *good = fopen("/data/PS4Tegelmaker/update/route.txt", "w");
    if (good) {
        fprintf(good, "%d\n", route);
        fclose(good);
    }
    unlink(StartingFile);
    sys::log("gestart als update (manier %d)", route);
}

// Download and store the newer version. The starter runs it from the next start on.
static bool install(std::string &error)
{
    std::vector<uint8_t> self;
    const int status = online::get(std::string(Base) + "emuforwarder.self", {}, self, 32 * 1024 * 1024);
    if (status != 200) {
        error = status < 0 ? "downloaden mislukt" : "GitHub antwoordde " + std::to_string(status);
        return false;
    }
    char hash[20];
    snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)fnv64(self.data(), self.size()));
    if (self.size() != latest.size || latest.fnv != hash) {
        error = "het gedownloade bestand klopt niet (probeer het over een paar minuten opnieuw)";
        sys::log("update: %zu bytes, fnv %s; verwacht %zu, %s", self.size(), hash, latest.size, latest.fnv.c_str());
        return false;
    }
    mkdir(Folder, 0777);
    char pending[96];
    snprintf(pending, sizeof(pending), "%s.tmp", SelfFile);
    FILE *file = fopen(pending, "wb");
    if (!file) {
        error = "kan niet opslaan";
        return false;
    }
    bool ok = fwrite(self.data(), 1, self.size(), file) == self.size();
    ok = fclose(file) == 0 && ok;
    ok = ok && rename(pending, SelfFile) == 0;
    if (ok) {
        // The system may only start it from the app's own folder: put a copy there too.
        {
            sys::Elevated elevated;
            mkdir("/user/app/TGMA00001/update", 0777);
            FILE *copy = fopen("/user/app/TGMA00001/update/emuforwarder.self.tmp", "wb");
            bool copied = copy && fwrite(self.data(), 1, self.size(), copy) == self.size();
            copied = copy && fclose(copy) == 0 && copied;
            copied = copied && rename("/user/app/TGMA00001/update/emuforwarder.self.tmp",
                                      "/user/app/TGMA00001/update/emuforwarder.self") == 0;
            chmod("/user/app/TGMA00001/update/emuforwarder.self", 0777);
            sys::log("update: kopie in /user/app/TGMA00001/update: %s", copied ? "gezet" : "mislukt");
        }
        unlink(StartingFile);
        unlink("/data/PS4Tegelmaker/update/failed.txt");
        FILE *build = fopen(BuildFile, "w");
        ok = build && fprintf(build, "%d\n%s\n", latest.build, latest.version.c_str()) > 0;
        ok = build && fclose(build) == 0 && ok;
    }
    if (!ok) {
        unlink(pending);
        error = "kan niet opslaan";
    }
    sys::log("update %s (build %d): %s", latest.version.c_str(), latest.build, ok ? "opgeslagen" : error.c_str());
    return ok;
}

}  // namespace update
