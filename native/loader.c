// EmuForwarder's starter (eboot.bin). EmuForwarder itself is a module (emuforwarder.prx) that the
// starter loads into its own process and runs (ef_main): a downloaded update when it is newer than
// the one in this package, else the package's own.
//
// Starting another program (sceSystemServiceLoadExec) only works from /app0: with a downloaded
// program the app just closed (2026-11-18, three paths tried). Loading a module returns an error
// instead, so the starter can fall back. When a downloaded module did not get as far as its first
// screen last time ("starting" mark still there), it is put aside.
// Every step goes to /data/PS4Tegelmaker/update/starter.log.
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FOLDER "/data/PS4Tegelmaker/update"
#define UPDATE FOLDER "/emuforwarder.prx"
#define BUILT_IN "/app0/emuforwarder.prx"

static void note(const char *message, long value)
{
    mkdir("/data/PS4Tegelmaker", 0777);
    mkdir(FOLDER, 0777);
    FILE *log = fopen(FOLDER "/starter.log", "a");
    if (log) {
        fprintf(log, "[%ld] %s (%lx)\n", (long)time(NULL), message, value);
        fclose(log);
    }
}

// access() is not to be trusted in this libc; opening the file is.
static bool present(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    fclose(file);
    return true;
}

// GoldHEN SDK: syscall 500, 2 = leave the sandbox, 3 = come back with the same backup.
long ef_loader_syscall(long num, ...);
__asm__(
    ".text\n"
    ".globl ef_loader_syscall\n"
    "ef_loader_syscall:\n"
    "  movq $0, %rax\n"
    "  movq %rcx, %r10\n"
    "  syscall\n"
    "  jb 1f\n"
    "  retq\n"
    "1:\n"
    "  movq $-1, %rax\n"
    "  retq\n");

struct JailbreakBackup {
    uint32_t cr_uid, cr_ruid, cr_rgid, cr_groups;
    uint64_t cr_paid;
    uint64_t cr_caps[2];
    void *cr_prison, *fd_cdir, *fd_jdir, *fd_rdir;
};

static long goldhen(long command, struct JailbreakBackup *backup)
{
    unsigned char info[256];  // only the result matters (this libc's struct stat layout is off)
    if (stat("/data/GoldHEN", (struct stat *)info) != 0)
        return -1;  // no GoldHEN: its syscall would kill the process
    void (*old12)(int) = signal(12, SIG_IGN);
    void (*old31)(int) = signal(31, SIG_IGN);
    const long rc = ef_loader_syscall(500, command, backup);
    signal(31, old31);
    signal(12, old12);
    return rc;
}

typedef int (*EntryPoint)(void);

// Load a module and find ef_main in it; NULL when that does not work.
static EntryPoint load(const char *path)
{
    const int handle = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
    note(path, handle);
    if (handle < 0)
        return NULL;
    void *entry = NULL;
    const int rc = sceKernelDlsym(handle, "ef_main", &entry);
    note("  ef_main", rc == 0 ? 0 : rc);
    return rc == 0 ? (EntryPoint)entry : NULL;
}

static EntryPoint load_update(void)
{
    EntryPoint entry = load(UPDATE);
    if (entry)
        return entry;
    // Maybe the module loader only reaches it from outside the sandbox.
    struct JailbreakBackup backup = {0};
    const long rc = goldhen(2, &backup);
    note("  zandbak verlaten", rc);
    entry = load("/user/data/PS4Tegelmaker/update/emuforwarder.prx");
    if (!entry)
        entry = load(UPDATE);
    if (rc == 0)
        goldhen(3, &backup);
    return entry;
}

static int read_build(void)
{
    FILE *file = fopen(FOLDER "/build.txt", "r");
    if (!file)
        return 0;
    char line[32] = "";
    const bool ok = fgets(line, sizeof(line), file) != NULL;
    fclose(file);
    return ok ? atoi(line) : 0;
}

int main(void)
{
    const int build = read_build();
    note("starter: gedownloade build / ingebouwde build", (long)build * 1000 + EF_BUILD);
    if (build > EF_BUILD && present(UPDATE)) {
        if (present(FOLDER "/starting")) {
            rename(UPDATE, UPDATE ".kapot");
            unlink(FOLDER "/starting");
            note("update kwam vorige keer niet tot het eerste scherm: ingebouwde versie", build);
        } else {
            FILE *mark = fopen(FOLDER "/starting", "w");  // EmuForwarder removes it after its first screen
            if (mark)
                fclose(mark);
            EntryPoint entry = load_update();
            if (entry)
                return entry();
            unlink(FOLDER "/starting");
            note("update laden lukte niet: ingebouwde versie", build);
        }
    }
    EntryPoint entry = load(BUILT_IN);
    if (entry)
        return entry();
    // Last resort: the package's program on its own.
    const int32_t rc = sceSystemServiceLoadExec("/app0/emuforwarder.self", NULL);
    note("ingebouwde versie starten mislukt", (unsigned)rc);
    return 1;
}
