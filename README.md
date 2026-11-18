# EmuForwarder 1.0.0

**Your emulator games as real tiles on the PS4 home screen.** EmuForwarder runs on the PS4 itself (GoldHEN). Pick a game, pick its artwork, and EmuForwarder installs a tile that starts that game straight in its emulator. No menus in between, no PC needed.

<!-- The promo video goes here: docs/EmuForwarder-promo.mp4 -->

## What it does

- **Finds your games**: GameCube games in `/data/DolphinPS4/games` (also from USB) and the games Retrocade downloaded to `/data/retrocade/games/<console>/`.
- **Makes a tile per game**: with the game's own name (you can change it before the tile is made) and an icon you choose.
- **Artwork from the internet**: SteamGridDB (with your free API key), GameTDB and libretro thumbnails. Browse them as a list or a full-screen grid, then move and zoom until the icon looks right.
- **Installs the tile itself**: straight from the app. The tile opens the game directly in its emulator.
- **Real games to the PS4**: forwards show up as games, also in apps like **Itemzflow**.
- **Updates itself**: see [Updating](#updating).
- **Extras**: background music (your own files) and an animated intro.

## Supported emulators (1.0.0)

| Emulator | Systems |
| --- | --- |
| **DolphinPS4** | GameCube |
| **EmuSuNi** | Nintendo DS, Game Boy Advance and the other Retrocade systems |

## Install

1. A PS4 with **GoldHEN**.
2. Download [`update/EmuForwarder.pkg`](update/EmuForwarder.pkg) and install it with **GoldHEN → Package Installer**.
3. Start **EmuForwarder** (blue icon). It also puts **EFUpdater** (orange icon) on your home screen.

## Updating

EmuForwarder checks GitHub at start. When there is a new version, choose **Download**: the package goes to `/data/pkg`. Close EmuForwarder and open **EFUpdater**, which removes the old version and installs the new one in one go. Your settings, icons, names and music stay. (Settings: **Square**.)

## Using it

| Button | Library |
| --- | --- |
| D-pad | Browse your games |
| L1 / R1 | Systems (All, GameCube, NDS, GBA, ...) |
| X | Game menu: make or update the tile, choose an icon, copy from USB |
| Options | Choose the icon |
| Square | Settings (SteamGridDB key, sources, music, updates) |
| Triangle | Search again |
| Circle (2x) | Close |

Making a tile: **X → Make tile**. EmuForwarder shows the name the tile gets; keep it or change it, then it installs the tile with your icon. The home screen shows a new name or icon after a PS4 restart.

Icon editor: **L1/R1** or the D-pad to browse, **Square** for list or grid, **X** to pick a picture, then move with the D-pad, zoom with **L2/R2**, **X** to save. **Circle** goes back.

Music: put `.ogg` or 16-bit `.wav` files in `/data/PS4Tegelmaker/muziek/`.

## Roadmap

- **More emulators**, such as **RetroArch** and **Crema**.
- **PS5**: once the jailbreak for PS5 firmware 14.20 is out, work starts on a PS5 version.
- **Smaller forwards**: a forward is about 200 MB now, because the emulator is still inside it. The goal is to make them smaller and smaller.

It already works great, and it will keep getting better.

## For developers

- `native/`: the PS4 app (`main.cpp`) and EFUpdater (`updater.cpp`), OpenOrbis toolchain. `native/build.sh` builds both.
- `offline-slot/`: the forwarder that goes into each tile.
- `native/icons/`: `make_icons.py` (app icons) and `make_intro.py` (the intro's parts).
- Releasing: `./publiceer.sh 1.0.1 "what's new"` builds, tags `v1.0.1` and puts the package on GitHub; every PS4 with EmuForwarder sees it at its next start.

Made by Studio van Batenburg.
