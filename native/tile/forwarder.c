// The starter inside each tile. GameCube tiles can load a shared Dolphin executable.
// It reads /app0/forward.txt, tells the installed emulator which game to start and opens it:
//   emu=DLPH00010   DolphinPS4 (title ID as installed): the game path goes to /data/DolphinPS4/launch.txt
//   emu=EMSN00001   EmuSuNi (any EMSN title ID): start-again.txt with why/title/console/game/attempt
//   game=<path of the game>
// Configurations with direct=dolphin start the shared executable inside the tile.
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include "runtimes.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool dutch;

static void log_line(const char *text)
{
    FILE *f = fopen("/data/PS4Tegelmaker/tile.log", "a");
    if (f) {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}

static int fail(const char *english, const char *nederlands, uint32_t code)
{
    char text[256];
    snprintf(text, sizeof(text), "%s", dutch ? nederlands : english);
    if (code) {
        const size_t used = strlen(text);
        snprintf(text + used, sizeof(text) - used, " (%08x)", (unsigned)code);
    }
    log_line(text);
    OrbisNotificationRequest request;
    memset(&request, 0, sizeof(request));
    request.targetId = -1;
    snprintf(request.message, sizeof(request.message), "%s", text);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}

static bool read_value(const char *text, const char *key, char *out, size_t size)
{
    const size_t key_length = strlen(key);
    for (const char *line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
        if (strncmp(line, key, key_length) || line[key_length] != '=')
            continue;
        const char *value = line + key_length + 1;
        const size_t length = strcspn(value, "\r\n");
        if (length >= size)
            return false;
        memcpy(out, value, length);
        out[length] = 0;
        return length > 0;
    }
    return false;
}

static bool write_file(const char *path, const char *text)
{
    char pending[128];
    snprintf(pending, sizeof(pending), "%s.tmp", path);
    FILE *f = fopen(pending, "w");
    if (!f)
        return false;
    bool ok = fputs(text, f) >= 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(pending, path) != 0) {
        unlink(pending);
        return false;
    }
    return true;
}

static int launch_dolphin(const char *game)
{
    FILE *source = fopen(EF_DOLPHIN_RUNTIME, "rb");
    if (!source)
        return fail("Shared Dolphin is missing. Update the tile in EmuForwarder.",
                    "Gedeelde Dolphin ontbreekt. Werk de tegel bij in EmuForwarder.", errno);
    fclose(source);
    mkdir("/data/DolphinPS4", 0777);
    // A cancelled launch can leave a request behind. Replace it atomically with this tile's game.
    char line[1040];
    snprintf(line, sizeof(line), "%s\n", game);
    if (!write_file(EF_DOLPHIN_REQUEST, line))
        return fail("Couldn't save the Dolphin launch request.",
                    "Kan de Dolphin-startopdracht niet opslaan.", 0);

    log_line("direct: DolphinPS4");
    sceSystemServiceHideSplashScreen();
    const int32_t rc = sceSystemServiceLoadExec(EF_DOLPHIN_RUNTIME, NULL);
    unlink(EF_DOLPHIN_REQUEST);
    return fail("Couldn't start shared Dolphin.", "Kan gedeelde Dolphin niet starten.", (uint32_t)rc);
}

#ifndef EF_COLLECTION
static bool emusuni_finishing_restart(void)
{
    FILE *file = fopen(EF_EMUSUNI_REQUEST, "r");
    if (!file)
        return false;
    char request[1536], why[32];
    request[fread(request, 1, sizeof(request) - 1, file)] = 0;
    fclose(file);
    return read_value(request, "why", why, sizeof(why)) &&
           (!strcmp(why, "stuck-again") || !strcmp(why, "crash-again") || !strncmp(why, "end-", 4));
}

#endif

static int launch_emusuni(const char *game, bool keep_request)
{
    FILE *source = fopen(EF_EMUSUNI_RUNTIME, "rb");
    if (!source)
        return fail("Shared EmuSuNi is missing. Update the tile in EmuForwarder.",
                    "Gedeelde EmuSuNi ontbreekt. Werk de tegel bij in EmuForwarder.", errno);
    fclose(source);

    // If the watchdog restarted this tile with a terminal failure, let EmuSuNi show it.
    const bool wrote_request = !keep_request;
    if (wrote_request) {
        const char *filename = strrchr(game, '/');
        char request[1536];
        snprintf(request, sizeof(request), "why=stuck\ntitle=%s\ngame=%s\nattempt=1\n",
                 filename ? filename + 1 : game, game);
        if (!write_file(EF_EMUSUNI_REQUEST, request))
            return fail("Couldn't save the EmuSuNi launch request.",
                        "Kan de EmuSuNi-startopdracht niet opslaan.", 0);
    }

    log_line("direct: EmuSuNi");
    sceSystemServiceHideSplashScreen();
    const int32_t rc = sceSystemServiceLoadExec(EF_EMUSUNI_RUNTIME, NULL);
    if (wrote_request)
        unlink(EF_EMUSUNI_REQUEST);
    return fail("Couldn't start shared EmuSuNi.", "Kan gedeelde EmuSuNi niet starten.", (uint32_t)rc);
}

#ifdef EF_COLLECTION
static const char CollectionRun[] = "/data/PS4Tegelmaker/collection-run.txt";

int ef_launch_collection(const char *system, const char *game, const char *id, bool use_dutch)
{
    dutch = use_dutch;
    log_line(game);
    if (!strcmp(system, "gamecube")) {
        unlink(CollectionRun);
        return launch_dolphin(game);
    }

    char context[1050];
    snprintf(context, sizeof(context), "id=%s\ngame=%s\n", id, game);
    write_file(CollectionRun, context);
    return launch_emusuni(game, false);
}

void ef_resume_collection(const char *id, bool use_dutch)
{
    char context[1050], owner[16], game[1024];
    FILE *file = fopen(CollectionRun, "r");
    if (!file)
        return;
    context[fread(context, 1, sizeof(context) - 1, file)] = 0;
    fclose(file);
    if (!read_value(context, "id", owner, sizeof(owner)) || strcmp(id, owner))
        return;
    unlink(CollectionRun);
    file = fopen(EF_EMUSUNI_REQUEST, "r");
    if (!file)
        return;
    char request[1536], pending[1024], console[32];
    request[fread(request, 1, sizeof(request) - 1, file)] = 0;
    fclose(file);
    // A watchdog retry has a console and the same path. Manual requests made by this tile have neither a
    // retry marker nor a console field. Terminal notifications belong to the last launch and expire here.
    if (read_value(request, "game", pending, sizeof(pending)) &&
        read_value(context, "game", game, sizeof(game)) && !strcmp(game, pending) &&
        read_value(request, "console", console, sizeof(console))) {
        dutch = use_dutch;
        write_file(CollectionRun, context);
        launch_emusuni(game, true);
    }
}
#else
// GoldHEN's jailbreak call (as in the launch test that worked on the PS4)
long ef_sc(long num, ...);
__asm__(".text\n.globl ef_sc\nef_sc:\n  movq $0, %rax\n  movq %rcx, %r10\n  syscall\n  jb 1f\n  retq\n1:\n  movq $-1, %rax\n  retq\n");
struct JB { uint32_t a, b, c, d; uint64_t e; uint64_t f[2]; void *g, *h, *i, *j; };

int main(void)
{
    char settings[512] = "";
    FILE *s = fopen("/data/PS4Tegelmaker/settings.ini", "r");
    if (s) {
        settings[fread(settings, 1, sizeof(settings) - 1, s)] = 0;
        fclose(s);
        dutch = strstr(settings, "taal=nl") != NULL;
    }
    char config[2048] = "";
    FILE *c = fopen("/app0/forward.txt", "r");
    if (!c)
        return fail("This tile has no game. Make it again in EmuForwarder.", "Deze tegel heeft geen game. Maak hem opnieuw in EmuForwarder.", 0);
    config[fread(config, 1, sizeof(config) - 1, c)] = 0;
    fclose(c);
    char emu[16], game[1024];
    if (!read_value(config, "emu", emu, sizeof(emu)) || !read_value(config, "game", game, sizeof(game)))
        return fail("This tile has no game. Make it again in EmuForwarder.", "Deze tegel heeft geen game. Maak hem opnieuw in EmuForwarder.", 0);
    log_line(game);
    struct stat st;
    if (stat(game, &st) != 0)
        return fail("The game is missing. Make the tile again in EmuForwarder.", "De game ontbreekt. Maak de tegel opnieuw in EmuForwarder.", 0);

    char direct[16];
    if (read_value(config, "direct", direct, sizeof(direct))) {
        if (!strcmp(direct, "dolphin"))
            return launch_dolphin(game);
        if (!strcmp(direct, "emusuni"))
            return launch_emusuni(game, emusuni_finishing_restart());
    }

    if (!strncmp(emu, "EMSN", 4)) {
        // EmuSuNi starts a game at once from start-again.txt; the game must be in its library
        // (EmuForwarder adds the folder). Title and console come from .../<console>/<title>.<ext>.
        char console[32] = "", title[256] = "";
        const char *file = strrchr(game, '/');
        if (file && file > game) {
            const char *folder = file - 1;
            while (folder > game && *folder != '/')
                --folder;
            snprintf(console, sizeof(console), "%.*s", (int)(file - folder - 1), folder + 1);
            snprintf(title, sizeof(title), "%s", file + 1);
            char *dot = strrchr(title, '.');
            if (dot)
                *dot = 0;
        }
        mkdir("/data/EmuSuNi", 0777);
        unlink("/data/EmuSuNi/launch.txt");
        unlink("/data/EmuSuNi/start.txt");  // EmuSuNi's own "last start didn't finish" marker
        char request[1536];
        snprintf(request, sizeof(request), "why=stuck\ntitle=%s\nconsole=%s\ngame=%s\nattempt=1\n", title, console, game);
        if (!write_file("/data/EmuSuNi/start-again.txt", request))
            return fail("Couldn't save the start request for EmuSuNi.", "Kan de startopdracht voor EmuSuNi niet opslaan.", 0);
    } else {
        mkdir("/data/DolphinPS4", 0777);
        char request[1040];
        snprintf(request, sizeof(request), "%s\n", game);
        if (!write_file(EF_DOLPHIN_REQUEST, request))
            return fail("Couldn't save the start request for DolphinPS4.", "Kan de startopdracht voor DolphinPS4 niet opslaan.", 0);
    }

    sceUserServiceInitialize(NULL);
    int32_t user = -1;
    if (sceUserServiceGetForegroundUser(&user) != 0 || user < 0)
        sceUserServiceGetInitialUser(&user);
    if (stat("/data/GoldHEN", &st) == 0) {
        signal(12, SIG_IGN);
        signal(31, SIG_IGN);
        struct JB jb;
        memset(&jb, 0, sizeof(jb));
        ef_sc(500, 2, &jb);
    }
    LncAppParam param;
    memset(&param, 0, sizeof(param));
    param.size = sizeof(param);
    param.user_id = (uint32_t)user;
    param.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;
    typedef int32_t (*LaunchApp)(const char *, const char **, void *);
    const LaunchApp system_launch = (LaunchApp)(void *)sceSystemServiceLaunchApp;
    sceSystemServiceHideSplashScreen();
    char line[96];
    const int32_t rc = system_launch(emu, NULL, &param);
    snprintf(line, sizeof(line), "launch %s: %08x", emu, (unsigned)rc);
    log_line(line);
    // 0x80940010: the PS4 asks to close this tile first, then starts the emulator.
    if (rc != 0 && (uint32_t)rc != 0x80940010u) {
        // Not started: don't leave a request that the emulator would act on the next time it opens.
        unlink(strncmp(emu, "EMSN", 4) ? EF_DOLPHIN_REQUEST : "/data/EmuSuNi/start-again.txt");
        if (!strncmp(emu, "EMSN", 4))
            return fail("Couldn't open EmuSuNi. Is it installed?", "Kan EmuSuNi niet openen. Is het geinstalleerd?", (uint32_t)rc);
        return fail("Couldn't open DolphinPS4. Is it installed?", "Kan DolphinPS4 niet openen. Is het geinstalleerd?", (uint32_t)rc);
    }
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}

#endif
