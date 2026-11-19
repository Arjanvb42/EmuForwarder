#include "lang.h"
// EFUpdater installs EmuForwarder updates. It runs as a separate
// app because a full package can only replace an installation after the old app is closed.
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
#include "ui.h"

#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "intro.h"

#include <sstream>
std::stringstream debugLogStream;  // used by graphics.cpp (OpenOrbis sample code)

extern "C" {
int ef_last_rect[4]{};
}

static Scene2D *scene;
static int frame;

enum class UpdateScreen { Searching, Installing, Done, NoUpdate, Failed };

static void show(UpdateScreen screen, const char *title, const char *message, const char *detail = "", int percent = 0)
{
    ui::canvas = scene->Pixels();
    ui::background();
    ui::header("EFUpdater", "EmuForwarder", nullptr, "v" EF_UPDATER_VERSION);

    constexpr int x = 456, y = 300, w = 1008, h = 428, inset = 52;
    const Color edge = screen == UpdateScreen::Done ? ui::Good :
                       screen == UpdateScreen::Failed ? ui::Warn : ui::Accent;
    ui::round_rect(x - 4, y - 4, w + 8, h + 8, 34, edge, ui::background_at(y));
    ui::round_rect(x, y, w, h, 30, ui::Card, edge);
    ui::text(title, ui::font_title, x + inset, y + 96, x + w - inset, ui::Card, ui::Text, true);

    int baseline = y + 166;
    for (const std::string &line : ui::wrap(message, ui::font_large, w - 2 * inset)) {
        ui::text(line.c_str(), ui::font_large, x + inset, baseline, x + w - inset, ui::Card, ui::Text);
        baseline += 45;
    }

    if (screen == UpdateScreen::Installing) {
        constexpr int bar_w = w - 2 * inset;
        const int progress = std::max(0, std::min(percent, 100));
        ui::round_rect(x + inset, y + 280, bar_w, 14, 7, ui::Line, ui::Card);
        if (progress)
            ui::round_rect(x + inset, y + 280, bar_w * progress / 100, 14, 7, ui::Accent, ui::Line);
        char label[8];
        snprintf(label, sizeof(label), "%d%%", progress);
        ui::text(label, ui::font_body, x + inset, y + 337, x + w - inset, ui::Card, ui::Sub);
    } else if (*detail) {
        ui::text(detail, ui::font_body, x + inset, y + 337, x + w - inset, ui::Card, ui::Sub);
    }

    scene->SubmitFlip(frame);
    scene->FrameWait(frame);
    scene->FrameBufferSwap();
    ++frame;
}

static constexpr char TitleId[] = "TGMA00001";
static constexpr char ContentId[] = "IV0000-TGMA00001_00-TEGELMAKERPS4000";
static constexpr char PackagePrefix[] = "EmuForwarder-v";

static std::string newest_package(std::string &version)
{
    std::string best;
    DIR *dir = opendir("/data/pkg");
    if (!dir)
        return best;
    while (dirent *entry = readdir(dir)) {
        const char *name = entry->d_name;
        const size_t length = strlen(name);
        if (strncmp(name, PackagePrefix, sizeof(PackagePrefix) - 1) || length < 5 || strcmp(name + length - 4, ".pkg") || strstr(name, "-update"))
            continue;
        const std::string path = std::string("/data/pkg/") + name, have = sys::package_version(path.c_str(), ContentId);
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
    lang::load();
    scene = new Scene2D(1920, 1080, 4);
    if (!scene->Init(0x4000000, 2))
        return 1;
    scene->SetActiveFrameBuffer(0);
    const char *font = "/app0/assets/fonts/Gontserrat-Regular.ttf";
    if (!scene->InitFont(&ui::font_title, font, 58) || !scene->InitFont(&ui::font_large, font, 36) ||
        !scene->InitFont(&ui::font_body, font, 28))
        return 1;
    sceUserServiceInitialize(NULL);
    int32_t user = -1;
    sceUserServiceGetInitialUser(&user);
    scePadInit();
    const int pad = scePadOpen(user, 0, 0, NULL);
    sceSystemServiceHideSplashScreen();
    intro::play(
        "efupdater", [] { return scene->Pixels(); },
        [] {
            scene->SubmitFlip(frame);
            scene->FrameWait(frame);
            scene->FrameBufferSwap();
            ++frame;
        },
        [pad] {
            OrbisPadData input{};
            return scePadReadState(pad, &input) >= 0 && (input.buttons & (ORBIS_PAD_BUTTON_CROSS | ORBIS_PAD_BUTTON_CIRCLE));
        });
    show(UpdateScreen::Searching, TR("Searching...", "Zoeken..."), TR("For updates in /data/pkg", "Naar updates in /data/pkg"));

    const std::string installed = sys::installed_version(TitleId);
    std::string version = installed;
    std::string package;
    {
        sys::Elevated elevated;
        package = newest_package(version);
    }
    if (package.empty()) {
        show(UpdateScreen::NoUpdate, TR("No update", "Geen update"), TR("No newer package in /data/pkg.", "Er staat geen nieuwer pakket in /data/pkg."),
             TR("X = close", "X = sluiten"));
        wait_for_cross(pad);
        sceSystemServiceLoadExec("exit", NULL);
        return 0;
    }
    sys::log("updater: pakket %s (%s), geinstalleerd %s", package.c_str(), version.c_str(), installed.c_str());
    char line[160];
    snprintf(line, sizeof(line), "%s %s > %s", "EmuForwarder",
             installed.empty() ? "-" : installed.c_str(), version.c_str());
    const char *updating = TR("Installing update", "Update installeren");
    show(UpdateScreen::Installing, updating, line, "", 0);
    std::string error;
    const sys::Install result = sys::install(package.c_str(), TitleId, "EmuForwarder", error, [&](int percent) {
        show(UpdateScreen::Installing, updating, line, "", percent);
    });
    const std::string now = sys::installed_version(TitleId);
    sys::log("updater: resultaat %d (%s), nu %s", int(result), error.c_str(), now.c_str());
    if (result == sys::Install::Done && now == version) {
        unlink(package.c_str());
        snprintf(line, sizeof(line), TR("%s is updated.", "%s is bijgewerkt."), "EmuForwarder");
        show(UpdateScreen::Done, TR("Done!", "Klaar!"), line,
             TR("Returning to the home screen...", "Terug naar het beginscherm..."));
        usleep(1500 * 1000);
        sceSystemServiceLoadExec("exit", NULL);
        return 0;
    } else {
        snprintf(line, sizeof(line), TR("Update failed%s%s.", "Bijwerken lukte niet%s%s."), error.empty() ? "" : ": ", error.c_str());
        show(UpdateScreen::Failed, TR("Didn't work", "Niet gelukt"), line, TR("The package is still in /data/pkg. X = close", "Het pakket staat nog in /data/pkg. X = sluiten"));
    }
    wait_for_cross(pad);
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}
