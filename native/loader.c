// EmuForwarder's starter (eboot.bin). Starts the newest EmuForwarder: a downloaded update when it is
// newer than the one in this package, else the package's own (/app0/emuforwarder.self).
//
// Starting a program outside /app0 does not return when it fails (seen 2026-11-18 with a /data path:
// the app just closed). So each way is tried once: the starter leaves a "starting" mark, the app
// removes it after its first screen and writes down the way that worked (route.txt). A mark that is
// still there at the next start means that way failed; the next way is tried, and after the last one
// the package's own version is used. Every step goes to /data/PS4Tegelmaker/update/starter.log.
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FOLDER "/data/PS4Tegelmaker/update"
#define BUILT_IN "/app0/emuforwarder.self"

// The ways to reach the downloaded program, as the system (outside the app's sandbox) sees them.
// EmuForwarder copies the download to both places.
static const char *const Routes[] = {
    "/user/app/TGMA00001/update/emuforwarder.self",
    "/user/data/PS4Tegelmaker/update/emuforwarder.self",
};
#define ROUTES 2

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

static int read_number(const char *path, int fallback)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return fallback;
    char line[32] = "";
    const bool ok = fgets(line, sizeof(line), file) != NULL;
    fclose(file);
    return ok ? atoi(line) : fallback;
}

static void write_numbers(const char *path, int a, int b)
{
    FILE *file = fopen(path, "w");
    if (file) {
        fprintf(file, "%d %d\n", a, b);
        fclose(file);
    }
}

int main(void)
{
    const int build = read_number(FOLDER "/build.txt", 0);
    note("starter: gedownloade build / ingebouwde build", (long)build * 1000 + EF_BUILD);
    if (build > EF_BUILD && present(FOLDER "/emuforwarder.self")) {
        // failed.txt: "<build> <bitmask of ways that failed for that build>"
        int failed_build = 0, failed = 0;
        FILE *file = fopen(FOLDER "/failed.txt", "r");
        if (file) {
            if (fscanf(file, "%d %d", &failed_build, &failed) != 2)
                failed_build = failed = 0;
            fclose(file);
        }
        if (failed_build != build)
            failed = 0;
        if (present(FOLDER "/starting")) {
            const int tried = read_number(FOLDER "/attempt.txt", -1);
            if (tried >= 0 && tried < ROUTES)
                failed |= 1 << tried;
            unlink(FOLDER "/starting");
            note("vorige poging startte niet; manier", tried);
            write_numbers(FOLDER "/failed.txt", build, failed);
        }
        int route = read_number(FOLDER "/route.txt", -1);  // a way that worked before
        if (route < 0 || route >= ROUTES || (failed & (1 << route))) {
            route = -1;
            for (int i = 0; i < ROUTES; ++i)
                if (!(failed & (1 << i))) {
                    route = i;
                    break;
                }
        }
        if (route >= 0) {
            write_numbers(FOLDER "/attempt.txt", route, 0);
            FILE *mark = fopen(FOLDER "/starting", "w");
            if (mark)
                fclose(mark);
            note(Routes[route], route);
            const int32_t rc = sceSystemServiceLoadExec(Routes[route], NULL);
            note("  starten mislukt", (unsigned)rc);
            unlink(FOLDER "/starting");
            write_numbers(FOLDER "/failed.txt", build, failed | (1 << route));
        } else {
            note("geen manier werkte: ingebouwde versie", failed);
        }
    }
    note("ingebouwde versie starten", 0);
    const int32_t rc = sceSystemServiceLoadExec(BUILT_IN, NULL);
    note("ingebouwde versie starten mislukt", (unsigned)rc);
    return 1;
}
