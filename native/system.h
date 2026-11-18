#pragma once
#include "lang.h"
// EmuForwarder's system access on a PS4 with GoldHEN:
//  - leave the sandbox (GoldHEN SDK syscall 500, command 2), so /user/appmeta, the app database and
//    the installer are reachable (same call as Retrocade and the GoldHEN plugins SDK);
//  - install a tile package straight from the app (BGFT, like GoldHEN's Package Installer);
//  - give an installed tile the game's name (app database, like itemzflow's "change name").
// Everything is logged to /data/PS4Tegelmaker/emuforwarder.log.

#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

#include <orbis/AppInstUtil.h>
#include <orbis/Bgft.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include "sqlite3.h"

namespace sys {

static void log(const char *format, ...)
{
    FILE *file = fopen("/data/PS4Tegelmaker/emuforwarder.log", "a");
    if (!file)
        return;
    time_t now = time(nullptr);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(file, "[%s] ", stamp);
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}

extern "C" long ef_ps4_syscall(long num, ...);
__asm__(
    ".text\n"
    ".globl ef_ps4_syscall\n"
    "ef_ps4_syscall:\n"
    "  movq $0, %rax\n"  // syscall 0: the real number is the first argument
    "  movq %rcx, %r10\n"
    "  syscall\n"
    "  jb 1f\n"
    "  retq\n"
    "1:\n"
    "  movq $-1, %rax\n"
    "  retq\n");

struct JailbreakBackup {  // struct jailbreak_backup from GoldHEN.h
    uint32_t cr_uid, cr_ruid, cr_rgid, cr_groups;
    uint64_t cr_paid;
    uint64_t cr_caps[2];
    void *cr_prison;
    void *fd_cdir;
    void *fd_jdir;
    void *fd_rdir;
};

// Drawing to the screen faulted while the app was out of the sandbox (crash in DrawRectangle,
// 2026-11-18). So the app leaves the sandbox only around the calls that need it, and comes back
// right after (GoldHEN SDK: 2 = jailbreak, 3 = restore with the same backup).
static int elevate_depth = 0;
static JailbreakBackup elevate_backup{};
static bool elevated_ok = false;

static long goldhen(long command, JailbreakBackup *backup)
{
    struct stat goldhen_dir{};
    if (stat("/data/GoldHEN", &goldhen_dir) != 0)
        return -1;
    // Without GoldHEN's syscall FreeBSD would send SIGSYS (12; 31 in OpenOrbis' Linux headers).
    void (*old12)(int) = signal(12, SIG_IGN);
    void (*old31)(int) = signal(31, SIG_IGN);
    const long rc = ef_ps4_syscall(500, command, backup);
    signal(31, old31);
    signal(12, old12);
    return rc;
}

struct Elevated {
    Elevated()
    {
        if (elevate_depth++ == 0) {
            const long rc = goldhen(2, &elevate_backup);
            elevated_ok = rc == 0;
            static int logged = 0;
            if (logged++ < 3)
                log("zandbak verlaten: %s (%ld)", elevated_ok ? "ja" : "nee", rc);
        }
    }
    ~Elevated()
    {
        if (--elevate_depth == 0 && elevated_ok) {
            const long rc = goldhen(3, &elevate_backup);
            static int logged = 0;
            if (logged++ < 3 || rc != 0)
                log("zandbak hersteld: %s (%ld)", rc == 0 ? "ja" : "nee", rc);
            elevated_ok = false;
        }
    }
    Elevated(const Elevated &) = delete;
    Elevated &operator=(const Elevated &) = delete;
};

static int32_t user()
{
    int32_t id = -1;
    if (sceUserServiceGetForegroundUser(&id) != 0 || id < 0)
        sceUserServiceGetInitialUser(&id);
    return id;
}

// ---- Installing ----------------------------------------------------------------------------
static bool install_ready = false;

static bool init_install(std::string &error)
{
    if (install_ready)
        return true;
    Elevated elevated;
    log("installer: modules laden");
    const uint32_t m1 = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
    const uint32_t m2 = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);
    log("installer: AppInstUtil 0x%08x, Bgft 0x%08x; AppInstUtil starten", m1, m2);
    int32_t rc = sceAppInstUtilInitialize();
    log("installer: sceAppInstUtilInitialize 0x%08x; BGFT starten", unsigned(rc));
    if (rc != 0) {
        error = "AppInstUtil " + std::to_string(rc);
        log("sceAppInstUtilInitialize: 0x%08x", unsigned(rc));
        return false;
    }
    static OrbisBgftInitParams params{};
    params.heapSize = 0x100000;
    params.heap = calloc(1, params.heapSize);
    rc = sceBgftServiceIntInit(&params);
    if (rc != 0) {
        char text[32];
        snprintf(text, sizeof(text), "BGFT 0x%08x", unsigned(rc));
        error = text;
        log("sceBgftServiceIntInit: 0x%08x", unsigned(rc));
        return false;
    }
    log("installer: klaar");
    install_ready = true;
    return true;
}

static bool app_installed(const char *title_id)
{
    std::string error;
    int32_t exists = 0;
    Elevated elevated;
    if (init_install(error) && sceAppInstUtilAppExists(title_id, &exists) == 0)
        return exists != 0;
    char path[64];
    snprintf(path, sizeof(path), "/user/app/%s", title_id);
    struct stat info{};
    return stat(path, &info) == 0;
}

enum class Install { Done, AlreadyThere, Failed };

// The PS4 writes 64-bit sizes here; OpenOrbis' OrbisBgftTaskProgress has 32-bit ones and is too small,
// so the call wrote past it and broke the stack (crash after a failed install, 2026-11-18).
struct BgftProgress {
    uint32_t bits;
    int32_t error;
    uint64_t length;
    uint64_t transferred;
    uint64_t length_total;
    uint64_t transferred_total;
    uint32_t num_index, num_total, rest_sec, rest_sec_total;
    int32_t preparing_percent, local_copy_percent;
    uint8_t spare[128];
};

// APP_VER from a param.sfo in memory ("01.11"), or "" when there is none.
static std::string sfo_app_ver(const std::vector<uint8_t> &sfo)
{
    if (sfo.size() < 20 || memcmp(sfo.data(), "\0PSF", 4))
        return "";
    auto u32 = [&](size_t at) { return at + 4 <= sfo.size() ? uint32_t(sfo[at] | sfo[at + 1] << 8 | sfo[at + 2] << 16 | sfo[at + 3] << 24) : 0u; };
    auto u16 = [&](size_t at) { return at + 2 <= sfo.size() ? uint32_t(sfo[at] | sfo[at + 1] << 8) : 0u; };
    const uint32_t keys = u32(8), data = u32(12), count = u32(16);
    for (uint32_t i = 0; i < count && i < 64; ++i) {
        const size_t entry = 20 + size_t(i) * 16;
        const size_t key = keys + u16(entry);
        if (key + 8 > sfo.size() || memcmp(&sfo[key], "APP_VER", 8))
            continue;
        const size_t value = data + u32(entry + 12), length = u32(entry + 4);
        if (value + length > sfo.size())
            return "";
        return std::string(reinterpret_cast<const char *>(&sfo[value]), strnlen(reinterpret_cast<const char *>(&sfo[value]), length));
    }
    return "";
}

static std::vector<uint8_t> read_part(const char *path, long offset, size_t size)
{
    std::vector<uint8_t> data;
    FILE *file = fopen(path, "rb");
    if (!file)
        return data;
    if (size > (1u << 20) || fseek(file, offset, SEEK_SET) != 0) {
        fclose(file);
        return data;
    }
    data.resize(size);
    data.resize(fread(data.data(), 1, size, file));
    fclose(file);
    return data;
}

// APP_VER inside a package: the PARAM_SFO entry (id 0x1000) of the big-endian entry table.
static std::string package_version(const char *package)
{
    auto be = [](const std::vector<uint8_t> &d, size_t at) {
        return at + 4 <= d.size() ? uint32_t(d[at] << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3]) : 0u;
    };
    const std::vector<uint8_t> header = read_part(package, 0, 0x20);
    if (header.size() < 0x20 || memcmp(header.data(), "\x7f" "CNT", 4))
        return "";
    const uint32_t count = be(header, 0x10), table = be(header, 0x18);
    const std::vector<uint8_t> entries = read_part(package, table, size_t(std::min(count, 64u)) * 32);
    for (size_t i = 0; i + 32 <= entries.size(); i += 32)
        if (be(entries, i) == 0x1000)
            return sfo_app_ver(read_part(package, be(entries, i + 16), be(entries, i + 20)));
    return "";
}

static std::string installed_version(const char *title_id)
{
    char path[80];
    snprintf(path, sizeof(path), "/system_data/priv/appmeta/%s/param.sfo", title_id);
    Elevated elevated;
    FILE *file = fopen(path, "rb");
    if (!file)
        return "";
    std::vector<uint8_t> sfo(64 * 1024);
    sfo.resize(fread(sfo.data(), 1, sfo.size(), file));
    fclose(file);
    return sfo_app_ver(sfo);
}

// Installs (or updates) a package and waits until the PS4 has it. progress(percent) is called meanwhile.
// Parameters as itemzflow uses them for packages on the console's own storage.
template <typename Progress>
static Install install(const char *package, const char *title_id, const char *name, std::string &error,
                       Progress progress)
{
    if (!init_install(error))
        return Install::Failed;
    static std::string url, title;  // BGFT reads these while registering
    // The installer runs outside the app's sandbox: there the app's /data is /user/data
    // (GoldHEN's Package Installer passes /user/data/pkg/... too).
    url = strncmp(package, "/data/", 6) == 0 ? std::string("/user") + package : std::string(package);
    title = name;
    OrbisBgftDownloadParamEx params{};
    params.params.entitlementType = 5;
    params.params.id = "";
    params.params.contentUrl = url.c_str();
    params.params.contentName = title.c_str();
    params.params.iconPath = "/update/fakepic.png";
    params.params.playgoScenarioId = "0";
    params.params.option = ORBIS_BGFT_TASK_OPT_DISABLE_CDN_QUERY_PARAM;
    params.slot = 0;
    OrbisBgftTaskId task = -1;
    int32_t rc = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        {
            Elevated elevated;
            rc = sceBgftServiceIntDownloadRegisterTaskByStorageEx(&params, &task);
        }
        log("installeren %s: registreren 0x%08x, taak %d", package, unsigned(rc), int(task));
        if (uint32_t(rc) != 0x80990088 && uint32_t(rc) != 0x80990015)
            break;
        // Installed already: keep it when it is as new as ours, else remove it and install ours.
        const std::string have = installed_version(title_id), ours = package_version(package);
        log("al geinstalleerd: versie %s, pakket %s", have.c_str(), ours.c_str());
        if (attempt || ours.empty() || (!have.empty() && have >= ours))
            return Install::AlreadyThere;
        {
            Elevated elevated;
            rc = sceAppInstUtilAppUnInstall(title_id);
        }
        log("oude versie verwijderen: 0x%08x", unsigned(rc));
        if (rc != 0)
            return Install::AlreadyThere;
    }
    if (rc != 0) {
        char text[64];
        snprintf(text, sizeof(text), TR("install failed (0x%08x)", "installeren mislukt (0x%08x)"), unsigned(rc));
        error = text;
        return Install::Failed;
    }
    {
        Elevated elevated;
        rc = sceBgftServiceDownloadStartTask(task);
    }
    if (rc != 0) {
        char text[64];
        snprintf(text, sizeof(text), TR("couldn't start the install (0x%08x)", "installatie starten mislukt (0x%08x)"), unsigned(rc));
        error = text;
        log("starten: 0x%08x", unsigned(rc));
        return Install::Failed;
    }
    // Wait until everything is copied (at most 15 minutes).
    for (int tick = 0; tick < 15 * 60 * 4; ++tick) {
        BgftProgress state{};
        {
            Elevated elevated;
            rc = sceBgftServiceDownloadGetProgress(task, reinterpret_cast<OrbisBgftTaskProgress *>(&state));
        }
        if (rc != 0) {  // the task is gone: finished
            log("voortgang 0x%08x na %d tikken: klaar", unsigned(rc), tick);
            break;
        }
        if (state.error != 0) {
            char text[64];
            snprintf(text, sizeof(text), TR("install failed (0x%08x)", "installatie mislukt (0x%08x)"), unsigned(state.error));
            error = text;
            log("fout tijdens installeren: 0x%08x", unsigned(state.error));
            return Install::Failed;
        }
        const int percent = state.length ? int(state.transferred * 100 / state.length) : 0;
        if (tick % 20 == 0)
            log("voortgang: %d%% (%llu/%llu, bits 0x%x)", percent, (unsigned long long)state.transferred,
                (unsigned long long)state.length, state.bits);
        progress(std::min(percent, 100));
        if (state.length && state.transferred >= state.length) {
            log("alles gekopieerd (%llu bytes)", (unsigned long long)state.length);
            break;
        }
        usleep(250 * 1000);
    }
    // Let the installer finish, then check the app is there.
    for (int wait = 0; wait < 40 && !app_installed(title_id); ++wait)
        usleep(250 * 1000);
    log("na installeren: %s %s", title_id, app_installed(title_id) ? "staat erop" : "niet gevonden");
    return Install::Done;
}

// ---- SQLite on the PS4 ----------------------------------------------------------------------
// OpenOrbis' libc fills struct stat in the kernel's FreeBSD 9 layout while its headers describe
// another one (Retrocade found this: wrong file sizes), and its lstat always fails (ENOSYS).
// SQLite gets its own stat/fstat/lstat that read the real layout, so it sees the right sizes.
static void freebsd_stat_to_header(const unsigned char *b, struct stat *out)
{
    uint32_t dev, ino, uid, gid, rdev, blksize;
    uint16_t mode, nlink;
    int64_t mtime, size, blocks;
    memcpy(&dev, b + 0, 4);
    memcpy(&ino, b + 4, 4);
    memcpy(&mode, b + 8, 2);
    memcpy(&nlink, b + 10, 2);
    memcpy(&uid, b + 12, 4);
    memcpy(&gid, b + 16, 4);
    memcpy(&rdev, b + 20, 4);
    memcpy(&mtime, b + 40, 8);
    memcpy(&size, b + 72, 8);
    memcpy(&blocks, b + 80, 8);
    memcpy(&blksize, b + 88, 4);
    memset(out, 0, sizeof(*out));
    out->st_dev = dev;
    out->st_ino = ino;
    out->st_mode = mode;
    out->st_nlink = nlink;
    out->st_uid = uid;
    out->st_gid = gid;
    out->st_rdev = rdev;
    out->st_size = size;
    out->st_blocks = blocks;
    out->st_blksize = blksize;
    out->st_mtime = mtime;
}

static int stat_fixed(const char *path, struct stat *out)
{
    alignas(16) unsigned char b[256] = {0};
    if (stat(path, reinterpret_cast<struct stat *>(b)) != 0)
        return -1;
    freebsd_stat_to_header(b, out);
    return 0;
}

static int sqlite_fstat(int fd, struct stat *out)
{
    alignas(16) unsigned char b[256] = {0};
    if (fstat(fd, reinterpret_cast<struct stat *>(b)) != 0)
        return -1;
    freebsd_stat_to_header(b, out);
    return 0;
}

static int sqlite_lstat(const char *path, struct stat *out) { return stat_fixed(path, out); }  // no symlinks here

static sqlite3_vfs *sqlite_vfs()
{
    // Without locking: fcntl's locks may also have another layout in this libc. One short update.
    sqlite3_vfs *vfs = sqlite3_vfs_find("unix-none");
    if (!vfs)
        vfs = sqlite3_vfs_find(nullptr);
    static bool patched = false;
    if (vfs && !patched) {
        vfs->xSetSystemCall(vfs, "stat", reinterpret_cast<sqlite3_syscall_ptr>(stat_fixed));
        vfs->xSetSystemCall(vfs, "fstat", reinterpret_cast<sqlite3_syscall_ptr>(sqlite_fstat));
        vfs->xSetSystemCall(vfs, "lstat", reinterpret_cast<sqlite3_syscall_ptr>(sqlite_lstat));
        patched = true;
    }
    return vfs;
}

// A copy of the app database before EmuForwarder changes it (once per start).
static bool backup_app_db()
{
    static bool done = false;
    if (done)
        return true;
    FILE *in = fopen("/system_data/priv/mms/app.db", "rb");
    if (!in)
        return false;
    FILE *out = fopen("/data/PS4Tegelmaker/app.db.bak", "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buffer[64 * 1024];
    size_t n, total = 0;
    bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        ok &= fwrite(buffer, 1, n, out) == n;
        total += n;
    }
    fclose(in);
    ok &= fclose(out) == 0;
    log("reservekopie app.db: %s (%zu bytes)", ok ? "gemaakt" : "mislukt", total);
    done = ok;
    return ok;
}

// ---- The tile's name ------------------------------------------------------------------------
// The system copies TITLE from the installed app's param.sfo back into the app database (seen on
// hardware: our database change was undone a few seconds later). So the title is changed there too,
// in place: TITLE has a fixed room (128 bytes) in the file.
static bool set_sfo_title(const char *title_id, const char *title)
{
    char path[80];
    snprintf(path, sizeof(path), "/system_data/priv/appmeta/%s/param.sfo", title_id);
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    std::vector<uint8_t> sfo(64 * 1024);
    sfo.resize(fread(sfo.data(), 1, sfo.size(), file));
    fclose(file);
    if (sfo.size() < 20 || memcmp(sfo.data(), "\0PSF", 4))
        return false;
    auto u32 = [&](size_t at) { return at + 4 <= sfo.size() ? uint32_t(sfo[at] | sfo[at + 1] << 8 | sfo[at + 2] << 16 | sfo[at + 3] << 24) : 0u; };
    auto u16 = [&](size_t at) { return at + 2 <= sfo.size() ? uint32_t(sfo[at] | sfo[at + 1] << 8) : 0u; };
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) sfo[at + i] = uint8_t(v >> (8 * i)); };
    const uint32_t keys = u32(8), data = u32(12), count = u32(16);
    bool changed = false;
    for (uint32_t i = 0; i < count && i < 64; ++i) {
        const size_t entry = 20 + size_t(i) * 16;
        const size_t key = keys + u16(entry);
        if (key + 6 > sfo.size() || memcmp(&sfo[key], "TITLE", 5) || (sfo[key + 5] && sfo[key + 5] != '_'))
            continue;  // TITLE and TITLE_xx (other languages), not TITLE_ID
        if (key + 9 <= sfo.size() && !memcmp(&sfo[key], "TITLE_ID", 9))
            continue;
        const size_t value = data + u32(entry + 12), room = u32(entry + 8);
        if (u16(entry + 2) != 0x204 || room < 2 || value + room > sfo.size())
            continue;
        const size_t length = std::min(strlen(title), room - 1);
        memset(&sfo[value], 0, room);
        memcpy(&sfo[value], title, length);
        put32(entry + 4, uint32_t(length + 1));
        changed = true;
    }
    if (!changed)
        return false;
    char pending[96];
    snprintf(pending, sizeof(pending), "%s.tmp", path);
    file = fopen(pending, "wb");
    if (!file)
        return false;
    bool ok = fwrite(sfo.data(), 1, sfo.size(), file) == sfo.size();
    ok = fclose(file) == 0 && ok;
    ok = ok && rename(pending, path) == 0;
    if (!ok)
        unlink(pending);
    return ok;
}

// The name the app database has for a tile now ("" when unknown).
static std::string db_title(const char *title_id);

// The home screen shows titleName from tbl_appbrowse_0<user> in the app database.
static bool set_title_in_db(const char *title_id, const char *title);

// Switched off since 2026-11-18: writing the PS4's app database (app.db) while the system has it open
// corrupted it and made the PS4 rebuild it (all homebrew left the home screen). Tiles now carry their
// name and icon in their own package, so EmuForwarder never writes app.db or a system param.sfo.
static bool set_title(const char *title_id, const char *title)
{
    log("naam van %s niet in de database gezet (uitgeschakeld): \"%s\"", title_id, title);
    return false;
}

// Uninstalls an app (a tile that is built again), so the new package is a clean install.
static bool uninstall(const char *title_id)
{
    std::string error;
    if (!init_install(error))
        return false;
    Elevated elevated;
    const int32_t rc = sceAppInstUtilAppUnInstall(title_id);
    log("verwijderen %s: 0x%08x", title_id, unsigned(rc));
    return rc == 0;
}

[[maybe_unused]] static bool set_title_in_db(const char *title_id, const char *title)
{
    Elevated elevated;
    log("naam in param.sfo van %s: %s", title_id, set_sfo_title(title_id, title) ? "gezet" : "niet gelukt");
    if (!backup_app_db()) {
        log("geen reservekopie van app.db: naam niet aangepast");
        return false;
    }
    sqlite3_vfs *vfs = sqlite_vfs();
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2("/system_data/priv/mms/app.db", &db, SQLITE_OPEN_READWRITE, vfs ? vfs->zName : nullptr) != SQLITE_OK) {
        log("app.db openen mislukt: %s (%d)", db ? sqlite3_errmsg(db) : "?", db ? sqlite3_extended_errcode(db) : -1);
        if (db)
            sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 3000);
    // Only change a database SQLite reads as healthy.
    std::string check;
    sqlite3_stmt *quick = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &quick, nullptr) == SQLITE_OK) {
        if (sqlite3_step(quick) == SQLITE_ROW)
            check = reinterpret_cast<const char *>(sqlite3_column_text(quick, 0));
        sqlite3_finalize(quick);
    }
    if (check != "ok") {
        log("app.db controle: \"%s\" (%s): naam niet aangepast", check.c_str(), sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    // Every user's table, so the name is right for whoever looks.
    std::vector<std::string> tables;
    sqlite3_stmt *list = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_appbrowse_%'", -1,
                           &list, nullptr) == SQLITE_OK) {
        while (sqlite3_step(list) == SQLITE_ROW)
            tables.push_back(reinterpret_cast<const char *>(sqlite3_column_text(list, 0)));
        sqlite3_finalize(list);
    }
    int changed = 0;
    for (const std::string &table : tables) {
        const std::string sql = "UPDATE " + table + " SET titleName=?1 WHERE titleId=?2";
        sqlite3_stmt *update = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &update, nullptr) != SQLITE_OK)
            continue;
        sqlite3_bind_text(update, 1, title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(update, 2, title_id, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(update) == SQLITE_DONE)
            changed += sqlite3_changes(db);
        sqlite3_finalize(update);
    }
    // The title in the app's own information too (used by some menus).
    sqlite3_stmt *info = nullptr;
    if (sqlite3_prepare_v2(db, "UPDATE tbl_appinfo SET val=?1 WHERE titleId=?2 AND (key='TITLE' OR key GLOB 'TITLE_[0-9]*')", -1, &info, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_text(info, 1, title, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(info, 2, title_id, -1, SQLITE_TRANSIENT);
        sqlite3_step(info);
        sqlite3_finalize(info);
    }
    const int committed = sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    if (committed != SQLITE_OK)
        log("app.db opslaan mislukt: %s (%d)", sqlite3_errmsg(db), sqlite3_extended_errcode(db));
    sqlite3_close(db);
    log("naam van %s -> \"%s\": %d rij(en) in %zu tabel(len)", title_id, title, changed, tables.size());
    return changed > 0;
}

static std::string db_title(const char *title_id)
{
    Elevated elevated;
    sqlite3_vfs *vfs = sqlite_vfs();
    sqlite3 *db = nullptr;
    std::string name;
    if (sqlite3_open_v2("/system_data/priv/mms/app.db", &db, SQLITE_OPEN_READONLY, vfs ? vfs->zName : nullptr) == SQLITE_OK) {
        sqlite3_busy_timeout(db, 2000);
        sqlite3_stmt *query = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT val FROM tbl_appinfo WHERE titleId=?1 AND key='TITLE'", -1, &query, nullptr) ==
            SQLITE_OK) {
            sqlite3_bind_text(query, 1, title_id, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(query) == SQLITE_ROW && sqlite3_column_text(query, 0))
                name = reinterpret_cast<const char *>(sqlite3_column_text(query, 0));
            sqlite3_finalize(query);
        }
    }
    if (db)
        sqlite3_close(db);
    return name;
}

}  // namespace sys
