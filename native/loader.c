// EmuForwarder's starter (eboot.bin). Starts the newest EmuForwarder: a downloaded update in
// /data/PS4Tegelmaker/update/ when it is newer than the one in this package, else the package's own.
// When a downloaded update did not get as far as its first screen last time, it is put aside.
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define FOLDER "/data/PS4Tegelmaker/update"
#define BUILT_IN "/app0/emuforwarder.self"

static void note(const char *message, unsigned code)
{
    mkdir("/data/PS4Tegelmaker", 0777);
    mkdir(FOLDER, 0777);
    FILE *log = fopen(FOLDER "/starter.log", "a");
    if (log) {
        fprintf(log, "%s %08x\n", message, code);
        fclose(log);
    }
}

int main(void)
{
    int build = 0;
    FILE *file = fopen(FOLDER "/build.txt", "r");
    if (file) {
        if (fscanf(file, "%d", &build) != 1)
            build = 0;
        fclose(file);
    }
    if (build > EF_BUILD && access(FOLDER "/emuforwarder.self", F_OK) == 0) {
        if (access(FOLDER "/starting", F_OK) == 0) {
            // Last time it never reached its first screen: use the package's own version.
            rename(FOLDER "/emuforwarder.self", FOLDER "/emuforwarder.self.kapot");
            unlink(FOLDER "/starting");
            note("update startte vorige keer niet; ingebouwde versie", (unsigned)build);
        } else {
            FILE *mark = fopen(FOLDER "/starting", "w");
            if (mark)
                fclose(mark);
            // The system resolves the path outside the app's sandbox, where /data is /user/data.
            const char *paths[] = {"/user/data/PS4Tegelmaker/update/emuforwarder.self", FOLDER "/emuforwarder.self"};
            for (int i = 0; i < 2; ++i) {
                sceSystemServiceHideSplashScreen();
                const int32_t rc = sceSystemServiceLoadExec(paths[i], NULL);
                note(paths[i], (unsigned)rc);
            }
            unlink(FOLDER "/starting");
        }
    }
    sceSystemServiceHideSplashScreen();
    const int32_t rc = sceSystemServiceLoadExec(BUILT_IN, NULL);
    note("ingebouwde versie starten mislukt", (unsigned)rc);
    return 1;
}
