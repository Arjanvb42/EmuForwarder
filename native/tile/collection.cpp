#include <orbis/Pad.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>

#include <cctype>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "../graphics.h"
#include "../ui.h"
#include "collection.h"

std::stringstream debugLogStream;
int ef_last_rect[4]{};

extern "C" int ef_launch_collection(const char *system, const char *game, const char *id, bool dutch);
extern "C" void ef_resume_collection(const char *id, bool dutch);

int main()
{
    lang::load();
    const collection::List list = collection::read("/app0/collection.txt");
    if (!list.id.empty())
        ef_resume_collection(list.id.c_str(), lang::dutch);

    Scene2D *scene = new Scene2D(ui::ScreenW, ui::ScreenH, 4);
    if (!scene->Init(0x4000000, 2))
        return 1;
    scene->SetActiveFrameBuffer(0);
    const char *font = "/app0/assets/fonts/Gontserrat-Regular.ttf";
    if (!scene->InitFont(&ui::font_title, font, 58) || !scene->InitFont(&ui::font_large, font, 36) ||
        !scene->InitFont(&ui::font_body, font, 28) || !scene->InitFont(&ui::font_small, font, 22))
        return 1;

    sceUserServiceInitialize(nullptr);
    int32_t user = -1;
    if (sceUserServiceGetForegroundUser(&user) != 0 || user < 0)
        sceUserServiceGetInitialUser(&user);
    scePadInit();
    const int pad = scePadOpen(user, 0, 0, nullptr);
    if (pad < 0)
        return 1;
    sceSystemServiceHideSplashScreen();

    constexpr int Visible = 9;
    const int total = int(list.entries.size());
    uint32_t previous = 0;
    unsigned frame = 0;
    int selected = 0, top = 0;
    bool changed = true;
    const char *status = total ? "" : TR("Open EmuForwarder to rebuild this collection.",
                                         "Open EmuForwarder om deze collectie opnieuw te maken.");
    for (;;) {
        OrbisPadData input{};
        if (scePadReadState(pad, &input) >= 0) {
            const uint32_t buttons = input.buttons;
            const uint32_t pressed = buttons & ~previous;
            previous = buttons;
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                sceSystemServiceLoadExec("exit", nullptr);
                return 0;
            }
            if (total && pressed) {
                const int from = selected;
                if (pressed & ORBIS_PAD_BUTTON_UP)
                    selected = std::max(0, selected - 1);
                if (pressed & ORBIS_PAD_BUTTON_DOWN)
                    selected = std::min(total - 1, selected + 1);
                if (pressed & ORBIS_PAD_BUTTON_L1)
                    selected = std::max(0, selected - Visible);
                if (pressed & ORBIS_PAD_BUTTON_R1)
                    selected = std::min(total - 1, selected + Visible);
                if (selected != from)
                    status = "";
                top = std::clamp(top, std::max(0, selected - Visible + 1), selected);
                if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                    const collection::Entry &game = list.entries[selected];
                    struct stat st{};
                    if (stat(game.path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                        return ef_launch_collection(game.system.c_str(), game.path.c_str(), list.id.c_str(), lang::dutch);
                    status = TR("The game file is missing.", "Het gamebestand ontbreekt.");
                }
                changed = true;
            }
        }
        if (!changed) {
            usleep(16 * 1000);
            continue;
        }

        ui::canvas = scene->Pixels();
        ui::background();
        const std::string count = std::to_string(total) + (total == 1 ? " game" : " games");
        ui::header(total ? list.name.c_str() : TR("Collection", "Collectie"),
                   "EmuForwarder", nullptr, count.c_str());
        for (int i = top; i < std::min(total, top + Visible); ++i) {
            const collection::Entry &game = list.entries[i];
            const int x = 96, y = 185 + (i - top) * 78;
            const bool active = i == selected;
            const Color fill = active ? ui::AccentDeep : ui::Card;
            ui::round_rect(x, y, 1728, 65, 17, fill, ui::background_at(y));
            if (active)
                ui::round_outline(x, y, 1728, 65, 17, 3, ui::Accent, ui::background_at(y));
            ui::text(game.title.c_str(), ui::font_large, x + 24, y + 44, 1490, fill, ui::Text, active);
            std::string system = game.system == "gamecube" ? "GameCube" : game.system;
            if (game.system != "gamecube")
                for (char &c : system)
                    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            const int width = ui::text_width(system.c_str(), ui::font_body);
            ui::text(system.c_str(), ui::font_body, 1796 - width, y + 42, 1796, fill, ui::Sub);
        }
        const int footer = 1008;
        ui::text(status, ui::font_body, 96, footer + 10, 1100, ui::background_at(footer), ui::Warn);
        if (total)
            ui::hints({{ui::Button::Cross, TR("Play", "Starten")}, {ui::Button::Circle, TR("Close", "Sluiten")}},
                      1824, footer, ui::background_at(footer));
        else
            ui::hints({{ui::Button::Circle, TR("Close", "Sluiten")}}, 1824, footer, ui::background_at(footer));
        scene->SubmitFlip(frame);
        scene->FrameWait(frame);
        scene->FrameBufferSwap();
        ++frame;
        changed = false;
    }
}
