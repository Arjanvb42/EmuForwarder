#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game.h"

// The emulator bundled in this tile. DolphinPS4 by default; EmuSuNi tiles set these in game.h.
// Both read the game to start from <EMU_DIR>/launch.txt.
#ifndef EMU_DIR
#define EMU_DIR "/data/DolphinPS4"
#endif
#ifndef EMU_EXECUTABLE
#define EMU_EXECUTABLE "/app0/dolphin.self"
#endif
#ifndef EMU_NAME
#define EMU_NAME "DolphinPS4"
#endif
#ifndef GAME_ROOT
#define GAME_ROOT EMU_DIR "/games/"
#endif

#define REQUEST_PATH EMU_DIR "/launch.txt"
#define LOG_PATH "/data/" TILE_ID ".log"
#if defined(SLOT_PATH)
#define SLOT_FILE SLOT_PATH
#elif defined(DISC_ID)
#define SLOT_FILE "/data/PS4Tegelmaker/mappings/" DISC_ID ".txt"
#else
#define SLOT_FILE "/data/PS4Tegelmaker/slot01.txt"
#endif

static void write_log(const char *mode, const char *message)
{
    FILE *log = fopen(LOG_PATH, mode);
    if (log) {
        fprintf(log, "%s\n", message);
        fclose(log);
    }
}

static int announce_failure(const char *message)
{
    write_log("a", message);
    OrbisNotificationRequest request = {0};
    request.targetId = -1;
    snprintf(request.message, sizeof(request.message), "%s: %s", GAME_TITLE, message);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}

#ifdef EMU_START_FILE
// EmuSuNi starts a game at once from <EMU_DIR>/start-again.txt (its own "start this game again"
// file): key=value lines why, title, console, game, attempt. why "stuck" or "crash" makes EmuSuNi
// start the game again at once ("attempt 2"); "stuck-again"/"crash-again" means it failed twice and
// shows "the game's file may be damaged". start.txt is EmuSuNi's own crash marker: not left behind.
// The game must be in EmuSuNi's library; EmuForwarder adds the folder for that.
static int start_with_start_file(const char *game_path)
{
    const char *file = strrchr(game_path, '/');
    char console[32] = "", title[256] = "";
    if (file && file > game_path) {
        const char *folder = file - 1;
        while (folder > game_path && *folder != '/')
            --folder;
        snprintf(console, sizeof(console), "%.*s", (int)(file - folder - 1), folder + 1);
        snprintf(title, sizeof(title), "%s", file + 1);
        char *dot = strrchr(title, '.');
        if (dot)
            *dot = 0;
    }
    if (mkdir(EMU_DIR, 0777) < 0 && errno != EEXIST)
        return announce_failure("Kan de " EMU_NAME "-map niet openen.");
    unlink(EMU_DIR "/launch.txt");  // left behind by earlier tiles; EmuSuNi doesn't read it
    unlink(EMU_DIR "/start.txt");   // written by earlier tiles; EmuSuNi read it as a failed start
    FILE *start = fopen(EMU_START_FILE ".tmp", "w");
    if (!start)
        return announce_failure("Kan de startopdracht voor " EMU_NAME " niet opslaan.");
    bool ok = fprintf(start, "why=stuck\ntitle=%s\nconsole=%s\ngame=%s\nattempt=1\n", title, console, game_path) > 0;
    ok = fclose(start) == 0 && ok;
    if (!ok || rename(EMU_START_FILE ".tmp", EMU_START_FILE) != 0) {
        unlink(EMU_START_FILE ".tmp");
        return announce_failure("Kan de startopdracht voor " EMU_NAME " niet opslaan.");
    }
    write_log("a", "Start " EMU_NAME " vanuit hetzelfde pakket (" EMU_START_FILE ").");
    sceSystemServiceHideSplashScreen();
    const int32_t result = sceSystemServiceLoadExec(EMU_EXECUTABLE, NULL);
    unlink(EMU_START_FILE);
    char message[160];
    snprintf(message, sizeof(message), EMU_NAME " start niet (%08x).", (unsigned)result);
    return announce_failure(message);
}
#endif

int main(void)
{
    write_log("w", "Zelfstandige tegel gestart (" EMU_NAME ")");
#ifndef EMU_START_FILE
    if (access(REQUEST_PATH, F_OK) == 0 || access(EMU_DIR "/to-menu.txt", F_OK) == 0) {
        // The emulator is restarting itself to load a state or return to its menu.
        sceSystemServiceHideSplashScreen();
        sceSystemServiceLoadExec(EMU_EXECUTABLE, NULL);
        return announce_failure("Kan de meegeleverde " EMU_NAME " niet herstarten.");
    }
#endif
    char game_path[1024];
#ifdef GAME_PATH
    // The game path is baked into this tile at build time; no mapping file on the PS4 is needed.
    strncpy(game_path, GAME_PATH, sizeof(game_path) - 1);
    game_path[sizeof(game_path) - 1] = 0;
    const bool read = true;
#else
    FILE *assignment = fopen(SLOT_FILE, "r");
    if (!assignment)
        return announce_failure("Wijs eerst een game toe in PS4 Tegelmaker.");
    const bool read = fgets(game_path, sizeof(game_path), assignment) != NULL;
    fclose(assignment);
#endif
    if (!read)
        return announce_failure("De tegel heeft geen game toegewezen.");
    const size_t eol = strcspn(game_path, "\r\n");
    game_path[eol] = 0;
    if (strncmp(game_path, GAME_ROOT, sizeof(GAME_ROOT) - 1) != 0 ||
        strstr(game_path, "/../") || eol < sizeof(GAME_ROOT) || eol > sizeof(game_path) - 2)
        return announce_failure("Ongeldig pad. Wijs de game opnieuw toe.");
    FILE *rom = fopen(game_path, "rb");
    if (!rom)
        return announce_failure("De game ontbreekt. Wijs hem opnieuw toe in PS4 Tegelmaker.");
#ifdef DISC_ID
    unsigned char header[32];
    bool matches = fread(header, 1, sizeof(header), rom) == sizeof(header) &&
        memcmp(header, DISC_ID, 6) == 0 && memcmp(header + 28, "\xc2\x33\x9f\x3d", 4) == 0;
    fclose(rom);
    if (!matches)
        return announce_failure("Dit bestand hoort bij een andere game of kan niet worden gelezen.");
#else
    fclose(rom);
#endif
    write_log("a", game_path);
#ifdef EMU_START_FILE
    return start_with_start_file(game_path);
#endif
    game_path[eol] = '\n';
    game_path[eol + 1] = 0;

    if (mkdir(EMU_DIR, 0777) < 0 && errno != EEXIST)
        return announce_failure("Kan de " EMU_NAME "-map niet openen.");

    // Preserve a pending request created by the emulator or another forwarder.
    int fd = open(REQUEST_PATH, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        if (errno == EEXIST)
            return announce_failure("Open en sluit " EMU_NAME " om de vorige startopdracht af te ronden.");
        return announce_failure("Kan de startopdracht voor " EMU_NAME " niet opslaan.");
    }

    const size_t length = eol + 1;
    ssize_t written = write(fd, game_path, length);
    int closed = close(fd);
    if (written != (ssize_t)length || closed < 0) {
        unlink(REQUEST_PATH);
        return announce_failure("Kan de startopdracht voor " EMU_NAME " niet opslaan.");
    }

    write_log("a", "Start " EMU_NAME " vanuit hetzelfde pakket.");
    // Hide the system splash/backdrop before handing over to the emulator; otherwise the
    // tile's image stays on top of the game while it runs.
    sceSystemServiceHideSplashScreen();
    const int32_t result = sceSystemServiceLoadExec(EMU_EXECUTABLE, NULL);
    unlink(REQUEST_PATH);
    char message[160];
    snprintf(message, sizeof(message), EMU_NAME " start niet (%08x).", (unsigned)result);
    return announce_failure(message);
}
