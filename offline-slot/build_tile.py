#!/usr/bin/env python3
"""Build a standalone GameCube tile: fetch the cover, apply it, then build the PKG.

Command line example:
  DOLPHIN_APP_ROOT=/path/to/unpacked/DolphinPS4 python3 build_tile.py \
      --disc GKME41 --title "Prince of Persia - The Two Thrones" \
      --tile-id TGCG00008 --version 01.03 --out ./out \
      --game-path "/data/DolphinPS4/games/Prince of Persia - The Two Thrones (USA) (En Fr Es) [GKME41].iso"

The cover is fetched from GameTDB unless a cover image is given. The square icon is always
applied. The wide banner (pic1.png) is only added with banner=True / --banner, because the
system draws it as a backdrop over the running game.

--game-path bakes the game location into the tile, so the PS4 needs no mapping file.
Without it the tile reads /data/PS4Tegelmaker/mappings/<disc>.txt on the PS4.

The forwarder source is copied to a temporary folder, so the project's files stay as they are.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from artwork import banner_png, fetch_cover, icon_png

HERE = Path(__file__).resolve().parent
FILES = ("forwarder.c", "build.sh", "gp4_files.py")
DEFAULT_TOOLCHAIN = HERE.parents[2] / "work/forwarder/OpenOrbis/PS4Toolchain"


def check(disc, tile_id, version, dolphin_root, toolchain, title):
    if not re.fullmatch(r"[A-Z0-9]{6}", disc):
        raise ValueError("Disc-ID moet 6 tekens zijn, bijvoorbeeld GKME41.")
    if not re.fullmatch(r"[A-Z]{4}\d{5}", tile_id):
        raise ValueError("Tegel-ID moet 4 letters en 5 cijfers hebben, bijvoorbeeld TGCG00008.")
    if not re.fullmatch(r"\d\d\.\d\d", version):
        raise ValueError("Versie moet de vorm 01.02 hebben.")
    if not dolphin_root or not (Path(dolphin_root) / "eboot.bin").is_file():
        raise ValueError("Geen DolphinPS4-app gevonden. Geef de map op met eboot.bin.")
    if not (Path(toolchain) / "bin/linux/PkgTool.Core").is_file():
        raise ValueError(f"PS4Toolchain niet gevonden in {toolchain}.")
    if len(title.encode()) > 126:
        raise ValueError("De titel is te lang (max. 126 bytes).")


EMULATORS = {
    # name: (data folder, bundled program name, allowed game folder)
    "dolphin": ("/data/DolphinPS4", "dolphin.self", "/data/DolphinPS4/games/"),
    "emusuni": ("/data/EmuSuNi", "emusuni.self", "/data/"),
}


def make_tile(disc, title, tile_id, out_dir, *, dolphin_root, version="01.02",
              toolchain=DEFAULT_TOOLCHAIN, cover=None, banner=None, game_path=None, slot_path=None,
              emulator="dolphin"):
    """Build the PKG and return (status text, path of the PKG).

    cover: bytes of an image for the icon; if None, the cover is fetched from GameTDB.
    banner: True to make a banner from the cover, or PNG bytes of a finished 1920x1080 banner,
            or None/False for no banner (the default; a banner is drawn as a backdrop).
    """
    toolchain, dolphin_root = Path(toolchain), Path(dolphin_root)
    check(disc, tile_id, version, dolphin_root, toolchain, title)
    if game_path and (any(c in game_path for c in '"\\\n\r') or not game_path.startswith("/data/DolphinPS4/games/")):
        raise ValueError("Het spelpad moet onder /data/DolphinPS4/games/ liggen.")

    if cover is None:
        cover = fetch_cover(disc)
        if cover is None:
            raise ValueError(f"Geen hoes gevonden op GameTDB voor {disc}. Geef zelf een afbeelding op.")
        source = f"GameTDB ({disc})"
    else:
        source = "eigen afbeelding"

    with tempfile.TemporaryDirectory(prefix="tegel-build-") as temporary:
        work = Path(temporary) / "offline-slot"
        work.mkdir()
        for name in FILES:
            shutil.copy2(HERE / name, work / name)
        defines = [
            f'#define GAME_TITLE "{title}"',
            f'#define TILE_ID "{tile_id}"',
        ]
        emu_dir, emu_self, game_root = EMULATORS[emulator]
        if emulator != "dolphin":
            defines += [
                f'#define EMU_DIR "{emu_dir}"',
                f'#define EMU_EXECUTABLE "/app0/{emu_self}"',
                '#define EMU_NAME "EmuSuNi"',
                f'#define EMU_START_FILE "{emu_dir}/start-again.txt"',
                f'#define GAME_ROOT "{game_root}"',
            ]
        if slot_path:
            # A general tile: plays whatever game the PS4 app writes into this slot file.
            defines.append(f'#define SLOT_PATH "{slot_path}"')
        else:
            defines.append(f'#define DISC_ID "{disc}"')
        if game_path:
            defines.append(f'#define GAME_PATH "{game_path}"')
        (work / "game.h").write_text("\n".join(defines) + "\n")
        (work / "icon0.png").write_bytes(icon_png(cover))
        if banner is True:
            (work / "pic1.png").write_bytes(banner_png(cover))
        elif banner:
            (work / "pic1.png").write_bytes(banner)

        env = os.environ.copy()
        env.update({
            "OO_PS4_TOOLCHAIN": str(toolchain),
            "DOLPHIN_APP_ROOT": str(dolphin_root),
            "TILE_ID": tile_id,
            "TILE_TITLE": title,
            "EMU_SELF": emu_self,
            "DOTNET_SYSTEM_GLOBALIZATION_INVARIANT": "1",
        })
        llvm = toolchain.parent.parent / "compiler/root/usr"
        if (llvm / "bin/clang").exists():
            env.setdefault("PS4_CC", str(llvm / "bin/clang"))
            env.setdefault("PS4_LD", str(llvm / "bin/ld.lld"))
            env["LD_LIBRARY_PATH"] = ":".join(p for p in (
                str(llvm / "lib64"),
                str(toolchain.parent.parent / "openssl/usr/lib/x86_64-linux-gnu"),
                env.get("LD_LIBRARY_PATH", ""),
            ) if p)

        script = re.sub(r"^version=\S+$", f"version={version}", (work / "build.sh").read_text(), count=1, flags=re.M)
        (work / "build.sh").write_text(script)
        stage = Path(temporary) / "pkg"
        stage.mkdir()
        result = subprocess.run(["bash", str(work / "build.sh"), str(stage)], env=env,
                                capture_output=True, text=True, timeout=900)
        if result.returncode:
            raise RuntimeError((result.stderr or result.stdout).strip()[-2000:])
        pkg = next(stage.glob("*.pkg"))
        out_dir = Path(out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        target = out_dir / f"{tile_id}-{disc}-v{version}.pkg"
        shutil.copy2(pkg, target)
    status = f"icoon uit {source}" + (" en banner" if banner else " (geen banner)")
    if banner and not isinstance(banner, bool):
        status = f"icoon uit {source} en eigen banner"
    return status, target


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--disc", required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--tile-id", required=True)
    parser.add_argument("--version", default="01.02")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--cover", type=Path, help="Use this image instead of GameTDB")
    parser.add_argument("--banner", action="store_true", help="Also add the wide banner (backdrop)")
    parser.add_argument("--game-path", help="Bake the game path into the tile")
    parser.add_argument("--dolphin-root", default=os.environ.get("DOLPHIN_APP_ROOT"), type=Path)
    parser.add_argument("--toolchain", default=os.environ.get("OO_PS4_TOOLCHAIN", str(DEFAULT_TOOLCHAIN)), type=Path)
    args = parser.parse_args()
    try:
        status, target = make_tile(
            args.disc, args.title, args.tile_id, args.out,
            dolphin_root=args.dolphin_root, version=args.version, toolchain=args.toolchain,
            cover=args.cover.read_bytes() if args.cover else None,
            banner=True if args.banner else None, game_path=args.game_path,
        )
    except (ValueError, RuntimeError) as error:
        sys.exit(str(error))
    print(f"Tegel {args.tile_id} ({args.disc}) gebouwd met {status}.")
    print(target)


if __name__ == "__main__":
    main()
