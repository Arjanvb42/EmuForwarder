# EmuForwarder 1.1.10

**Your emulator games as real tiles on the PS4 home screen.** EmuForwarder runs on the PS4 itself (GoldHEN). Pick a game, pick its artwork, and EmuForwarder builds and installs a tile that starts that game in its emulator. No menus in between, no PC needed.

**▶ [Watch the promo video](docs/promo/EmuForwarder-promo.mp4)** (1:34). Music: "Dreamy Spring" by Danger Lion X (Uppbeat).

## What it does

- **Finds your games**: GameCube games in `/data/DolphinPS4/games` (also from USB) and the games Retrocade downloaded to `/data/retrocade/games/<console>/`.
- **Builds a tile per game, on the PS4 itself**: about 7 MB, with the game's own name (you can change it before the tile is made) and an icon you choose. As many tiles as you like.
- **Artwork from the internet**: SteamGridDB (with your free API key), GameTDB and libretro thumbnails. Browse them as a list or a full-screen grid, then move and zoom until the icon looks right.
- **Several at once, or one collection tile**: press **L2** to select games (X marks them), then **Options**: install all their tiles in one go, or make one *collection tile* with its own name and icon that holds several games.
- **Installs the tile itself**: straight from the app. The tile opens the game in the emulator you have installed. The PS4 first asks to close the tile; press OK and the game starts.
- **Real games to the PS4**: forwards show up as games, also in apps like **Itemzflow**.
- **Updates itself**: see [Updating](#updating).
- **English or Dutch**: English by default; switch to Dutch under **Settings → Language**.
- **Extras**: background music (your own files) and an animated intro.

## Supported emulators (1.1.10)

| Emulator | Systems |
| --- | --- |
| **DolphinPS4** | GameCube |
| **EmuSuNi** | Nintendo DS, Game Boy Advance and the other Retrocade systems |

The emulator must be installed on the PS4: the tiles open it, they don't carry it.

## Install

1. A PS4 with **GoldHEN**.
2. Download [`update/EmuForwarder.pkg`](update/EmuForwarder.pkg) and install it with **GoldHEN → Package Installer**.
3. Start **EmuForwarder** (blue icon). It also puts **EFUpdater** (orange icon) on your home screen.

## Updating

EmuForwarder checks GitHub at start. When there is a new version, choose **Download**: the package goes to `/data/pkg`. Close EmuForwarder and open **EFUpdater**, which removes the old version and installs the new one in one go. Your settings, icons, names and music stay. (Settings: **Square**.)

## Using it

| Button | Library |
| --- | --- |
| D-pad or left stick | Browse your games (hold to keep moving) |
| L1 / R1 | Systems (All, GameCube, NDS, GBA, ...) |
| X | Game menu: make or update the tile, choose an icon, copy from USB |
| Options | Choose the icon |
| L2 | Select several games (X marks, Options: install the tiles or make a collection tile) |
| Square | Settings (SteamGridDB key, sources, music, updates, language, clean up old tiles) |
| Triangle | Search again |
| Circle (2x) | Close |

Making a tile: **X → Make tile**. EmuForwarder shows the name the tile gets; keep it or change it, then it builds the tile with your icon and installs it. A new name or icon: choose **Update tile**, and EmuForwarder builds the tile again.

Icon editor: **L1/R1** or the D-pad to browse, **Square** for list or grid, **X** to pick a picture, then move with the D-pad or left stick, zoom with **L2/R2** or the right stick, **X** to save. **Circle** goes back.

Music: put `.ogg` or 16-bit `.wav` files in `/data/PS4Tegelmaker/muziek/`.

Coming from 1.0.x? **Settings → Clean up the old tiles from before 1.1** removes the old 200 MB tile templates and uninstalls the old GameCube/Retro tiles (the normal PS4 uninstall). Your icons and names stay; make the small tiles again with X.

## Roadmap

- **More emulators**, such as **RetroArch** and **Crema**.
- **PS5**: once the jailbreak for PS5 firmware 14.20 is out, work starts on a PS5 version.
- **Smaller forwards**: done in 1.1.0. A tile used to be about 200 MB because the emulator was inside it; now it is about 7 MB and opens the installed emulator.

It already works great, and it will keep getting better.

## For developers

- `native/`: the PS4 app (`main.cpp`) and EFUpdater (`updater.cpp`), OpenOrbis toolchain. `native/build.sh` builds both.
- `native/tile/forwarder.c`: the small program inside every tile (it opens the installed emulator).
- `native/pkg/`: the package builder EmuForwarder uses on the PS4 (a C++ port of LibOrbisPkg's PkgBuilder; it writes the same bytes as PkgTool).
- `offline-slot/`: the old tiles with the emulator inside (before 1.1.0).
- `native/icons/`: `make_icons.py` (app icons) and `make_intro.py` (the intro's parts).
- Translations: every screen text is `TR("English", "Nederlands")` (`native/lang.h`); `python3 tools/check_lang.py` checks that both have the same placeholders.
- Releasing: `./publiceer.sh 1.1.1 "what's new"` builds, tags `v1.1.1` and puts the package on GitHub; every PS4 with EmuForwarder sees it at its next start.

Made by Studio van Batenburg.
