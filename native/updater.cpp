// EFUpdater (TGMA00002): installs a downloaded EmuForwarder package over the installed
// app. EmuForwarder can't do that itself: the PS4 only installs a full package after the old one is
// removed, and an app can't remove itself while it runs. (A patch package, like a game update,
// would install over it, but the toolchain builds patches the PS4 refuses: "unable to install
// update", 2026-11-18.) This app removes the old EmuForwarder and installs the new one, the same way
// EmuForwarder installs its tiles. Your data in /data/PS4Tegelmaker stays.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <orbis/Pad.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>

#include "graphics.h"
#include "system.h"

#include <sstream>
std::stringstream debugLogStream;  // used by graphics.cpp (OpenOrbis sample code)

static Scene2D *scene;
static FT_Face font_title, font_body;
static int frame;
static const Color Background{14, 16, 26}, Text{235, 238, 245}, Dim{150, 156, 175}, Accent{90, 170, 255};

static void show(const char *title, const char *line1, const char *line2 = "", int percent = -1)
{
    scene->DrawRectangle(0, 0, 1920, 1080, Background);
    scene->DrawText(const_cast<char *>("EFUpdater"), font_body, 160, 200, Background, Dim);
    scene->DrawText(const_cast<char *>(title), font_title, 160, 330, Background, Text);
    scene->DrawText(const_cast<char *>(line1), font_body, 160, 440, Background, Text);
    scene->DrawText(const_cast<char *>(line2), font_body, 160, 500, Background, Dim);
    if (percent >= 0) {
        scene->DrawRectangle(160, 580, 1600, 16, Color{40, 44, 60});
        scene->DrawRectangle(160, 580, 16 * std::min(percent, 100), 16, Accent);
    }
    scene->SubmitFlip(frame);
    scene->FrameWait(frame);
    scene->FrameBufferSwap();
    ++frame;
}

// The newest full EmuForwarder package in /data/pkg ("EmuForwarder-v0.26.pkg").
static std::string newest_package(std::string &version)
{
    std::string best;
    DIR *dir = opendir("/data/pkg");
    if (!dir)
        return best;
    while (dirent *entry = readdir(dir)) {
        const char *name = entry->d_name;
        const size_t length = strlen(name);
        if (strncmp(name, "EmuForwarder-v", 14) || length < 5 || strcmp(name + length - 4, ".pkg") || strstr(name, "-update"))
            continue;
        const std::string path = std::string("/data/pkg/") + name, have = sys::package_version(path.c_str());
        if (!have.empty() && have > version) {
            version = have;
            best = path;
        }
    }
    closedir(dir);
    return best;
}

static void wait_for_cross(int pad)
{
    uint32_t previous = ~0u;
    for (int tick = 0; tick < 60 * 60 * 10; ++tick) {
        OrbisPadData input{};
        const uint32_t buttons = scePadReadState(pad, &input) >= 0 ? input.buttons : 0;
        if (buttons & ~previous & (ORBIS_PAD_BUTTON_CROSS | ORBIS_PAD_BUTTON_CIRCLE))
            return;
        previous = buttons;
        usleep(16 * 1000);
    }
}

int main()
{
    mkdir("/data/PS4Tegelmaker", 0777);
    sys::log("Updater " EF_VERSION " start");
    scene = new Scene2D(1920, 1080, 4);
    if (!scene->Init(0x4000000, 2))
        return 1;
    scene->SetActiveFrameBuffer(0);
    const char *font = "/app0/assets/fonts/Gontserrat-Regular.ttf";
    if (!scene->InitFont(&font_title, font, 58) || !scene->InitFont(&font_body, font, 30))
        return 1;
    sceUserServiceInitialize(NULL);
    int32_t user = -1;
    sceUserServiceGetInitialUser(&user);
    scePadInit();
    const int pad = scePadOpen(user, 0, 0, NULL);
    show("Zoeken...", "Naar een nieuwe EmuForwarder in /data/pkg");
    sceSystemServiceHideSplashScreen();

    std::string version;
    std::string package;
    {
        sys::Elevated elevated;  // for the installed version (system_data)
        package = newest_package(version);
    }
    const std::string installed = sys::installed_version("TGMA00001");
    sys::log("updater: pakket %s (%s), geinstalleerd %s", package.c_str(), version.c_str(), installed.c_str());
    if (package.empty() || (!installed.empty() && installed >= version)) {
        show("Geen update", "Er staat geen nieuwere EmuForwarder in /data/pkg.",
             "Kies in EmuForwarder: Vierkant > Instellingen > updates zoeken.  X = sluiten");
        wait_for_cross(pad);
        sceSystemServiceLoadExec("exit", NULL);
        return 0;
    }
    char line[160];
    snprintf(line, sizeof(line), "Van %s naar %s. Sluit EmuForwarder als hij nog open staat.",
             installed.empty() ? "-" : installed.c_str(), version.c_str());
    show("EmuForwarder bijwerken", line, "", 0);
    std::string error;
    const sys::Install result = sys::install(package.c_str(), "TGMA00001", "EmuForwarder", error, [&](int percent) {
        show("EmuForwarder bijwerken", line, "", percent);
    });
    const std::string now = sys::installed_version("TGMA00001");
    sys::log("updater: resultaat %d (%s), nu %s", int(result), error.c_str(), now.c_str());
    if (result == sys::Install::Done && now == version) {
        unlink(package.c_str());
        show("Klaar!", "EmuForwarder is bijgewerkt. Je instellingen, iconen en muziek zijn bewaard.",
             "X = sluiten, en start EmuForwarder op je home screen");
    } else {
        snprintf(line, sizeof(line), "Bijwerken lukte niet%s%s.", error.empty() ? "" : ": ", error.c_str());
        show("Niet gelukt", line, "Het pakket staat nog in /data/pkg (GoldHEN > Package Installer kan ook).  X = sluiten");
    }
    wait_for_cross(pad);
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}
