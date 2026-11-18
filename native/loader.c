// EmuForwarder's starter (eboot.bin). Starts the newest EmuForwarder: a downloaded update in
// /data/PS4Tegelmaker/update/ when it is newer than the one in this package, else the package's own.
// When a downloaded update did not get as far as its first screen last time, it is put aside.
// Every step goes to /data/PS4Tegelmaker/update/starter.log.
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

int main(void)
{
    int build = 0;
    FILE *file = fopen(FOLDER "/build.txt", "r");
    if (file) {
        char line[32] = "";
        if (fgets(line, sizeof(line), file))
            build = atoi(line);
        fclose(file);
    }
    note("starter: gedownloade build / ingebouwde build", (long)build * 1000 + EF_BUILD);
    if (build > EF_BUILD && present(FOLDER "/emuforwarder.self")) {
        if (present(FOLDER "/starting")) {
            // Last time it never reached its first screen: use the package's own version.
            rename(FOLDER "/emuforwarder.self", FOLDER "/emuforwarder.self.kapot");
            unlink(FOLDER "/starting");
            note("update startte vorige keer niet: ingebouwde versie", build);
        } else {
            FILE *mark = fopen(FOLDER "/starting", "w");
            if (mark)
                fclose(mark);
            // The system may resolve the path outside the app's sandbox, where /data is /user/data.
            const char *paths[] = {FOLDER "/emuforwarder.self", "/user/data/PS4Tegelmaker/update/emuforwarder.self"};
            for (int i = 0; i < 2; ++i) {
                note(paths[i], 0);
                const int32_t rc = sceSystemServiceLoadExec(paths[i], NULL);
                note("  starten mislukt", (unsigned)rc);
            }
            unlink(FOLDER "/starting");
        }
    }
    note("ingebouwde versie starten", 0);
    const int32_t rc = sceSystemServiceLoadExec(BUILT_IN, NULL);
    note("ingebouwde versie starten mislukt", (unsigned)rc);
    return 1;
}
