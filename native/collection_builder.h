#pragma once

// Included after the shared tile builder and the EmuSuNi library setup.
static constexpr char CollectionRoot[] = "/data/PS4Tegelmaker/collections";

static collection::List collection_draft;
static std::vector<collection::List> stored_collections;
static std::vector<uint8_t> collection_icon;

static void reset_collection_draft()
{
    collection_draft = {};
    collection_icon.clear();
}

static std::string collection_path(const std::string &id, const char *extension)
{
    return std::string(CollectionRoot) + "/" + id + extension;
}

static void read_collections()
{
    stored_collections.clear();
    DIR *dir = opendir(CollectionRoot);
    if (!dir)
        return;
    while (dirent *entry = readdir(dir)) {
        const std::string file = entry->d_name;
        if (file.size() != 13 || file.substr(9) != ".txt")
            continue;
        collection::List saved = collection::read((std::string(CollectionRoot) + "/" + file).c_str());
        if (saved.id == file.substr(0, 9))
            stored_collections.push_back(std::move(saved));
    }
    closedir(dir);
    std::sort(stored_collections.begin(), stored_collections.end(), [](const auto &a, const auto &b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
}

static void add_selection_action(const std::string &label, int action)
{
    menu.items.push_back(label);
    menu.actions.push_back(action);
}

static void open_selection_menu()
{
    menu = Menu{};
    menu.open = true;
    menu.question = marked_paths.empty() ? TR("Multiselect", "Multiselect")
        : std::to_string(marked_paths.size()) + TR(" games selected", " games geselecteerd");
    if (!collection_draft.id.empty())
        add_selection_action(TR("Edit current collection", "Deze collectie bijwerken"), ResumeCollectionAction);
    if (!marked_paths.empty()) {
        add_selection_action(TR("Install selected tiles", "Geselecteerde tegels installeren"), BatchTilesAction);
        add_selection_action(TR("Make a collection tile", "Collectietegel maken"), NewCollectionAction);
    }
    add_selection_action(TR("Manage collections", "Collecties beheren"), BrowseCollectionsAction);
    add_selection_action(TR("Back", "Terug"), BackToSelectionAction);
}

static void open_collection_menu()
{
    menu = Menu{};
    menu.open = true;
    menu.question = collection_draft.name + "  ·  " + std::to_string(marked_paths.size()) + " games";
    add_selection_action(TR("Install collection tile", "Collectietegel installeren"), CollectionSaveAction);
    add_selection_action(TR("Change name", "Naam wijzigen"), CollectionNameAction);
    add_selection_action(collection_icon.empty() ? TR("Choose own icon (PNG/JPG)", "Eigen icoon kiezen (PNG/JPG)")
                                                 : TR("Change own icon (PNG/JPG)", "Eigen icoon wijzigen (PNG/JPG)"),
                         CollectionIconAction);
    add_selection_action(TR("Back to game selection", "Terug naar gameselectie"), BackToSelectionAction);
}

static const Game *find_marked_game(const std::string &path)
{
    for (int i = 0; i < count; ++i)
        if (games[i].internal && path == games[i].path)
            return &games[i];
    return nullptr;
}

static void batch_install()
{
    const int total = int(marked_paths.size());
    std::vector<std::string> failed;
    std::string first_error;
    for (size_t i = 0; i < marked_paths.size(); ++i) {
        const Game *game = find_marked_game(marked_paths[i]);
        if (game) {
            snprintf(status, sizeof(status), TR("Tile %d of %d: %.125s", "Tegel %d van %d: %.125s"),
                     int(i) + 1, total, game->name);
            draw();
            if (prepare_tile(*game))
                continue;
        } else {
            snprintf(status, sizeof(status), "%s", TR("A game was not found. Search for games again.",
                                                     "Een game is niet gevonden. Zoek de games opnieuw."));
        }
        if (first_error.empty())
            first_error = status;
        failed.push_back(marked_paths[i]);
    }
    refresh_tiles();
    marked_paths.swap(failed);
    if (marked_paths.empty()) {
        multi_select = false;
        snprintf(status, sizeof(status), TR("%d tiles installed.", "%d tegels geïnstalleerd."), total);
    } else {
        snprintf(status, sizeof(status), TR("%d of %d installed. %.190s", "%d van %d geïnstalleerd. %.190s"),
                 total - int(marked_paths.size()), total, first_error.c_str());
    }
}

static bool save_collection_file(const std::string &path, const void *data, size_t size)
{
    const std::string pending = path + ".tmp";
    FILE *file = fopen(pending.c_str(), "wb");
    if (!file)
        return false;
    bool ok = fwrite(data, 1, size, file) == size && fflush(file) == 0 && fsync(fileno(file)) == 0;
    ok = fclose(file) == 0 && ok;
    if (ok && rename(pending.c_str(), path.c_str()) == 0)
        return true;
    unlink(pending.c_str());
    return false;
}

static std::string next_collection_id()
{
    int highest = 0;
    for (const auto &saved : stored_collections)
        highest = std::max(highest, atoi(saved.id.c_str() + 4));
    char id[16];
    while (++highest <= 99999) {
        snprintf(id, sizeof(id), "EFCL%05d", highest);
        if (!exists(collection_path(id, ".txt").c_str()) && !sys::app_installed(id))
            return id;
    }
    return "";
}

static bool install_collection()
{
    collection::List list{collection_draft.id, collection_draft.name, {}};
    std::vector<std::string> discs, systems;
    if (marked_paths.empty()) {
        snprintf(status, sizeof(status), "%s", TR("Mark at least one game.", "Markeer minimaal één game."));
        return false;
    }
    for (const std::string &path : marked_paths) {
        const Game *game = find_marked_game(path);
        if (!game || !exists(path.c_str())) {
            snprintf(status, sizeof(status), "%s", TR("A selected game is missing. Search again and remove it from the selection.",
                                                     "Een geselecteerde game ontbreekt. Zoek opnieuw en haal hem uit de selectie."));
            return false;
        }
        const std::string system = game->system[0] ? game->system : "gamecube";
        if (!collection::field(game->name) || !collection::field(path)) {
            snprintf(status, sizeof(status), "%s", TR("A game name or path contains a line break or tab.",
                                                     "Een gamenaam of pad bevat een regeleinde of tab."));
            return false;
        }
        if (game->system[0]) {
            if (!ensure_library_folder(*game))
                return false;
            if (std::find(systems.begin(), systems.end(), system) == systems.end())
                systems.push_back(system);
        } else {
            char disc[7];
            if (!read_disc(game->path, disc)) {
                snprintf(status, sizeof(status), TR("Unreadable disc: %s", "Onleesbare disc: %s"), game->name);
                return false;
            }
            discs.emplace_back(disc);
        }
        list.entries.push_back({system, game->name, path});
    }
    if (!systems.empty() && !exists(EF_EMUSUNI_RUNTIME)) {
        snprintf(status, sizeof(status), "%s", TR("Shared EmuSuNi is missing.", "Gedeelde EmuSuNi ontbreekt."));
        return false;
    }
    if (!discs.empty() && !exists(EF_DOLPHIN_RUNTIME) &&
        !copy_file("/app0/tile/dolphin.bin", EF_DOLPHIN_RUNTIME, true, false, 0755)) {
        snprintf(status, sizeof(status), "%s", TR("Couldn't install shared Dolphin. Check free space.",
                                                 "Kan gedeelde Dolphin niet installeren. Controleer de vrije ruimte."));
        return false;
    }
    if (list.id.empty()) {
        read_collections();
        list.id = next_collection_id();
        if (list.id.empty()) {
            snprintf(status, sizeof(status), "%s", TR("Couldn't assign a collection ID.", "Kan geen collectie-ID toewijzen."));
            return false;
        }
    }

    const std::string version = next_version(sys::installed_version(list.id.c_str()));
    pkgbuild::Project project = tile_project(list.id, version, list.name, "EMUFWDCOLLECT000");
    if (!add_tile_files(project, true))
        return false;
    if ((!discs.empty() && !add_emulator_assets(project, DolphinAssets, true, discs)) ||
        (!systems.empty() && !add_emulator_assets(project, EmuSuNiAssets, false, systems))) {
        snprintf(status, sizeof(status), "%s", TR("Couldn't prepare the emulator files.", "Kan de emulatorbestanden niet voorbereiden."));
        return false;
    }
    const std::string manifest = collection::serialize(list);
    project.files.push_back({"collection.txt", std::vector<uint8_t>(manifest.begin(), manifest.end())});
    std::vector<uint8_t> icon = collection_icon;
    if (icon.empty()) {
        char custom[96];
        custom_icon_path(*find_marked_game(marked_paths.front()), custom);
        if (!read_all(custom, icon) && !read_all("/app0/tile/icon0.png", icon)) {
            snprintf(status, sizeof(status), "%s", TR("The collection icon is missing.", "Het collectie-icoon ontbreekt."));
            return false;
        }
    }
    project.files.push_back({"sce_sys/icon0.png", std::move(icon)});
    char package[96];
    if (!write_tile_package(project, list.id, list.name, package))
        return false;

    mkdir(CollectionRoot, 0777);
    if ((!collection_icon.empty() &&
         !save_collection_file(collection_path(list.id, ".png"), collection_icon.data(), collection_icon.size())) ||
        !save_collection_file(collection_path(list.id, ".txt"), manifest.data(), manifest.size())) {
        unlink(package);
        snprintf(status, sizeof(status), "%s", TR("Couldn't save the collection. Check free space.",
                                                 "Kan de collectie niet opslaan. Controleer de vrije ruimte."));
        return false;
    }
    collection_draft = list;
    if (sys::app_installed(list.id.c_str()))
        sys::uninstall(list.id.c_str());
    const bool installed = place_tile(list.id.c_str(), package, list.name.c_str(), false, nullptr, nullptr);
    unlink(package);
    if (installed)
        snprintf(status, sizeof(status), TR("%s is on your home screen.", "%s staat op je beginscherm."), list.name.c_str());
    return installed;
}

static bool set_collection_name()
{
    std::string name = collection_draft.name;
    if (!online::keyboard(TR("Collection name", "Naam van de collectie"), name, [] { draw(); usleep(16000); }))
        return false;
    const size_t begin = name.find_first_not_of(' '), end = name.find_last_not_of(' ');
    name = begin == std::string::npos ? "" : utf8_cut(name.substr(begin, end - begin + 1), 127);
    if (!collection::field(name)) {
        snprintf(status, sizeof(status), "%s", TR("Enter a name on one line.", "Geef de collectie een naam op één regel."));
        return false;
    }
    collection_draft.name = std::move(name);
    return true;
}

static void choose_collection_icon()
{
    std::string file = "/mnt/usb0/";
    if (!online::keyboard(TR("Path to a PNG or JPG (e.g. /mnt/usb0/icon.png)", "Pad naar PNG of JPG (bijv. /mnt/usb0/icoon.png)"),
                          file, [] { draw(); usleep(16000); }))
        return;
    if (!file.empty() && file.front() != '/')
        file = "/mnt/usb0/" + file;
    struct stat st{};
    const char *temporary = "/data/PS4Tegelmaker/collection-icon.tmp.png";
    std::vector<uint8_t> icon;
    if (stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
        make_icon(file.c_str(), temporary) && read_all(temporary, icon)) {
        collection_icon = std::move(icon);
        snprintf(status, sizeof(status), "%s", TR("Icon selected.", "Icoon gekozen."));
    } else {
        snprintf(status, sizeof(status), "%s", TR("Couldn't read that PNG or JPG.", "Kan die PNG of JPG niet lezen."));
    }
    unlink(temporary);
}

static void run_selection_action(int action)
{
    const int item = menu.selected;
    menu.open = false;
    switch (action) {
    case BatchTilesAction:
        batch_install();
        break;
    case NewCollectionAction:
        if (!collection_draft.id.empty())
            reset_collection_draft();
        if (!collection_draft.name.empty() || set_collection_name())
            open_collection_menu();
        else
            open_selection_menu();
        break;
    case BrowseCollectionsAction:
        read_collections();
        menu = Menu{};
        menu.open = true;
        menu.question = stored_collections.empty() ? TR("No collections yet", "Nog geen collecties")
                                                  : TR("Choose a collection to edit", "Kies een collectie om aan te passen");
        for (const auto &saved : stored_collections)
            add_selection_action(saved.name, EditCollectionAction);
        add_selection_action(TR("Back", "Terug"), BackToSelectionAction);
        break;
    case EditCollectionAction: {
        collection_draft = stored_collections[item];
        collection_icon.clear();
        read_all(collection_path(collection_draft.id, ".png").c_str(), collection_icon);
        marked_paths.clear();
        for (const auto &entry : collection_draft.entries)
            if (find_marked_game(entry.path))
                marked_paths.push_back(entry.path);
        snprintf(status, sizeof(status), "%s", TR("Mark or unmark games. Options opens the collection actions.",
                                                 "Vink games aan of uit. Open de collectieacties met Options."));
        break;
    }
    case ResumeCollectionAction:
        open_collection_menu();
        break;
    case CollectionNameAction:
        set_collection_name();
        open_collection_menu();
        break;
    case CollectionIconAction:
        choose_collection_icon();
        open_collection_menu();
        break;
    case CollectionSaveAction:
        if (install_collection()) {
            marked_paths.clear();
            reset_collection_draft();
            multi_select = false;
        }
        break;
    case BackToSelectionAction:
        break;
    }
}
