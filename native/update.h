#pragma once
#include "lang.h"
// Updates from GitHub: EmuForwarder looks for a newer version at start and downloads the package
// to /data/pkg. One push to GitHub reaches every PS4 that has the app; no FTP needed.
//
// Installing: the PS4 only installs a full package after the old one is removed, and an app can't
// remove itself while it runs. EFUpdater (TGMA00002, updater.cpp, installed by
// EmuForwarder itself) removes the old EmuForwarder and installs the new one in one go.
// (A patch package would install over it, but the toolchain builds patches the PS4 refuses.)
//
// On GitHub (public, so the PS4 needs no login):
//   update/latest.txt          build / version / size / fnv (of the package) / notes
//   update/EmuForwarder.pkg    the package

#include <dirent.h>
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
static constexpr char PackageFolder[] = "/data/pkg";

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
        check_error = status < 0 ? TR("no connection to GitHub", "geen verbinding met GitHub") : TR("GitHub answered ", "GitHub antwoordde ") + std::to_string(status);
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
        check_error = TR("network not ready", "netwerk niet klaar");
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

// The folder and files the earlier self-update (v0.13-v0.21) used.
static void remove_old_files()
{
    static const char *const old[] = {"emuforwarder.self", "emuforwarder.self.kapot", "emuforwarder.prx",
                                      "emuforwarder.prx.kapot", "build.txt", "starting", "attempt.txt",
                                      "route.txt", "failed.txt", "starter.log"};
    char path[96];
    for (const char *name : old) {
        snprintf(path, sizeof(path), "/data/PS4Tegelmaker/update/%s", name);
        unlink(path);
    }
    rmdir("/data/PS4Tegelmaker/update");
}

static std::string package_path(const std::string &version)
{
    return std::string(PackageFolder) + "/EmuForwarder-" + version + ".pkg";
}

// Download the newer package to /data/pkg (older EmuForwarder packages there are removed).
static bool download(std::string &error)
{
    std::vector<uint8_t> package;
    const int status = online::get(std::string(Base) + "EmuForwarder.pkg", {}, package, 64 * 1024 * 1024);
    if (status != 200) {
        error = status < 0 ? TR("download failed", "downloaden mislukt") : TR("GitHub answered ", "GitHub antwoordde ") + std::to_string(status);
        return false;
    }
    char hash[20];
    snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)fnv64(package.data(), package.size()));
    if (package.size() != latest.size || latest.fnv != hash) {
        error = TR("the downloaded file isn't right (try again in a few minutes)", "het gedownloade bestand klopt niet (probeer het over een paar minuten opnieuw)");
        sys::log("update: %zu bytes, fnv %s; verwacht %zu, %s", package.size(), hash, latest.size, latest.fnv.c_str());
        return false;
    }
    mkdir(PackageFolder, 0777);
    const std::string target = package_path(latest.version), pending = target + ".tmp";
    FILE *file = fopen(pending.c_str(), "wb");
    if (!file) {
        error = TR("can't save to /data/pkg", "kan niet opslaan in /data/pkg");
        return false;
    }
    bool ok = fwrite(package.data(), 1, package.size(), file) == package.size();
    ok = fclose(file) == 0 && ok;
    ok = ok && rename(pending.c_str(), target.c_str()) == 0;
    if (!ok) {
        unlink(pending.c_str());
        error = TR("can't save to /data/pkg (enough space?)", "kan niet opslaan in /data/pkg (genoeg ruimte?)");
    } else if (DIR *dir = opendir(PackageFolder)) {
        const std::string keep = target.substr(strlen(PackageFolder) + 1);
        std::vector<std::string> old;
        while (dirent *entry = readdir(dir))
            if (!strncmp(entry->d_name, "EmuForwarder-v", 14) && strstr(entry->d_name, ".pkg") && keep != entry->d_name)
                old.push_back(std::string(PackageFolder) + "/" + entry->d_name);
        closedir(dir);
        for (const std::string &path : old)
            unlink(path.c_str());
    }
    sys::log("update %s (build %d) naar %s: %s", latest.version.c_str(), latest.build, target.c_str(),
             ok ? "gedownload" : error.c_str());
    return ok;
}

}  // namespace update
