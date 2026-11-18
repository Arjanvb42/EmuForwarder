#pragma once
// EFHelper was a test (1.0.10-1.0.15): a system daemon in /system/vsh/app/EFHL00001 that opened the
// emulator after a tile closed, to avoid the PS4's "the application will be closed" question. Starting it
// made the PS4 crash (2026-11-18), so it was dropped. What is left removes that folder: /system is
// mounted writable for a moment (as Itemzflow does), only that folder is removed, then read-only again.
#include <dirent.h>
#include <errno.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "system.h"

namespace helper {

static constexpr char Folder[] = "/system/vsh/app/EFHL00001";

struct IoVec {
    const void *base;
    size_t length;
};

// nmount(2) of the system partition, as Itemzflow does it (exfatfs on /dev/da0x4.crypt).
static long mount_system(bool writable)
{
    const char *options[][2] = {{"fstype", "exfatfs"}, {"fspath", "/system"}, {"from", "/dev/da0x4.crypt"},
                                {"large", "yes"}, {"timezone", "static"}, {"async", ""}, {"ignoreacl", ""},
                                {"dirmask", "511"}, {"mask", "511"}};
    IoVec io[18];
    int n = 0;
    for (const auto &option : options) {
        io[n++] = {option[0], strlen(option[0]) + 1};
        io[n++] = {option[1], strlen(option[1]) + 1};
    }
    const uint64_t MNT_RDONLY = 0x1, MNT_UPDATE = 0x10000;
    return sys::ef_ps4_syscall(378, io, n, writable ? MNT_UPDATE : (MNT_UPDATE | MNT_RDONLY));
}

static bool installed()
{
    sys::Elevated elevated;
    char path[96];
    snprintf(path, sizeof(path), "%s/eboot.bin", Folder);
    return access(path, F_OK) == 0;
}

static void remove_files()
{
    const char *files[] = {"eboot.bin", "version.txt", "sce_sys/param.sfo", "sce_module/libc.prx", "sce_module/libSceFios2.prx"};
    char path[128];
    for (const char *file : files) {
        snprintf(path, sizeof(path), "%s/%s", Folder, file);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/sce_sys", Folder);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/sce_module", Folder);
    rmdir(path);
    rmdir(Folder);
}

static bool uninstall(std::string &error)
{
    sys::Elevated elevated;
    if (mount_system(true) != 0) {
        error = "mount " + std::to_string(errno);
        return false;
    }
    remove_files();
    sync();
    const long readonly = mount_system(false);
    char path[96];
    snprintf(path, sizeof(path), "%s/eboot.bin", Folder);
    const bool gone = access(path, F_OK) != 0;
    sys::log("helper: verwijderd %s; /system weer alleen-lezen: %ld", gone ? "ja" : "nee", readonly);
    return gone;
}

}  // namespace helper
