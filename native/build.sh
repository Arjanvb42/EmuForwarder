#!/usr/bin/env bash
# Builds EmuForwarder (TGMA00001) with EFUpdater (TGMA00002) inside it.
set -euo pipefail
native=$(cd -- "$(dirname -- "$0")" && pwd)
tools=$(realpath "$native/../../../work/forwarder")
oo="$tools/OpenOrbis/PS4Toolchain"
llvm="$tools/compiler/root/usr"
out=$(realpath -m "${1:-$native/out}")
work=$(mktemp -d)
trap 'python3 -c "import shutil,sys; shutil.rmtree(sys.argv[1])" "$work"' EXIT
export OO_PS4_TOOLCHAIN="$oo"
export LD_LIBRARY_PATH="$llvm/lib64:$tools/openssl/usr/lib/x86_64-linux-gnu"
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
bin="$oo/bin/linux"
common="$oo/samples/_common"
# The release (native/RELEASE, e.g. 1.0.0) is what people see; the build number (native/VERSION) goes
# up with every publish and is what the update check compares.
# APP_VER (what the PS4 and EFUpdater compare): 1.0.0 -> 10.00, 1.2.3 -> 12.03 (before 1.0.0: 01.xx).
build=$(tr -dc 0-9 < "$native/VERSION")
release=$(tr -dc 0-9. < "$native/RELEASE")
IFS=. read -r major minor patch <<< "$release"
if (( minor > 9 || patch > 99 )); then echo "RELEASE $release past niet in APP_VER" >&2; exit 1; fi
label="v$release"
app_ver=$(printf "%02d.%02d" $((major * 10 + minor)) "$patch")
# The Updater only changes now and then; EmuForwarder reinstalls it when this goes up.
updater_ver=$(tr -dc 0-9. < "$native/UPDATER_VERSION")
objects="$work/objects"
mkdir -p "$objects" "$out"

cxx() {
    "$llvm/bin/clang++" --target=x86_64-pc-freebsd12-elf -std=gnu++17 -D_GNU_SOURCE -fPIC -O2 \
        -funwind-tables -DEF_BUILD=$build -DEF_VERSION="\"$label\"" -DEF_UPDATER_VERSION="\"$updater_ver\"" \
        -DGRAPHICS_USES_FONT -DTEGELMAKER_PS4 \
        -isysroot "$oo" -isystem "$oo/include" -isystem "$oo/include/c++/v1" -I"$common" -c "$1" -o "$2"
}
cc() {
    "$llvm/bin/clang" --target=x86_64-pc-freebsd12-elf -fPIC -O2 -funwind-tables -isysroot "$oo" -isystem "$oo/include" "$@"
}
link() {  # link <output.elf> <objects...>
    local output=$1
    shift
    "$llvm/bin/ld.lld" -m elf_x86_64 -pie --script "$oo/link.x" --eh-frame-hdr -L"$oo/lib" \
        "$oo/lib/crt1.o" "$@" -lc -lkernel -lc++ -lc++abi \
        -lSceVideoOut -lSceFreeType -lSceSysmodule -lScePad -lSceUserService -lSceSystemService -lSceNet -lSceNetCtl \
        -lSceAppInstUtil -lSceBgft -lSceHttp -lSceSsl -lSceImeDialog -lSceAudioOut \
        -o "$output"
}
# The intros' parts and the boot splash (pic1), drawn from the icons.
python3 "$native/icons/make_intro.py" "$work/intro" >/dev/null
# A package folder with the font, the system modules, the icon, the intro and the boot splash:
# new_stage <folder> <app: emuforwarder or efupdater>
new_stage() {
    mkdir -p "$1/sce_sys/about" "$1/sce_module" "$1/assets/fonts"
    cp "$oo/samples/font/sce_module/"{libc,libSceFios2}.prx "$1/sce_module/"
    cp "$oo/samples/font/sce_sys/about/right.sprx" "$1/sce_sys/about/"
    cp "$oo/samples/font/assets/fonts/"{Gontserrat-Regular.ttf,OFL.txt} "$1/assets/fonts/"
    cp "$native/icons/$2.png" "$1/sce_sys/icon0.png"
    cp "$work/intro/$2/pic1.png" "$1/sce_sys/pic1.png"
    mkdir -p "$1/assets/intro"
    cp -r "$work/intro/$2" "$1/assets/intro/"
    rm "$1/assets/intro/$2/pic1.png"
}
# make_package <stage> <content id> <title id> <title> <app version> <output folder>
make_package() {
    local stage=$1 content_id=$2 title_id=$3 title=$4 version=$5 output=$6
    local sfo="$stage/sce_sys/param.sfo"
    mkdir -p "$output"
    "$bin/PkgTool.Core" sfo_new "$sfo"
    field() { "$bin/PkgTool.Core" sfo_setentry "$sfo" "$1" --type "$2" --maxsize "$3" --value "$4" >/dev/null; }
    field APP_TYPE Integer 4 1
    field APP_VER Utf8 8 "$version"
    field ATTRIBUTE Integer 4 0
    field CATEGORY Utf8 4 gd
    field CONTENT_ID Utf8 48 "$content_id"
    field DOWNLOAD_DATA_SIZE Integer 4 0
    field SYSTEM_VER Integer 4 0
    field TITLE Utf8 128 "$title"
    field TITLE_ID Utf8 12 "$title_id"
    field VERSION Utf8 8 01.00
    (
        cd "$stage"
        "$bin/create-gp4" -out project.gp4 --content-id="$content_id" --files "eboot.bin"
        python3 "$native/../offline-slot/gp4_files.py"
        "$bin/PkgTool.Core" pkg_build project.gp4 "$output"
    )
    "$bin/PkgTool.Core" pkg_validate "$output/$content_id.pkg"
}

# Shared code.
cc -c "$native/vorbis.c" -o "$objects/vorbis.o"  # Ogg Vorbis for the music (stb_vorbis, public domain)
cc -DSQLITE_OS_UNIX=1 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -DSQLITE_DQS=0 -DHAVE_USLEEP=1 -DSQLITE_OMIT_DEPRECATED=1 \
    -c "$tools/../sqlite/sqlite-amalgamation-3470200/sqlite3.c" -o "$objects/sqlite3.o"
for source in main crash graphics updater; do
    cxx "$native/$source.cpp" "$objects/$source.o"
done

# EFUpdater (TGMA00002).
updater_stage="$work/updater"
new_stage "$updater_stage" efupdater
link "$objects/updater.elf" "$objects/updater.o" "$objects/graphics.o" "$objects/sqlite3.o"
"$bin/create-fself" -in "$objects/updater.elf" -out "$objects/updater.oelf" \
    --eboot "$updater_stage/eboot.bin" --paid 0x3800000000000011
make_package "$updater_stage" IV0000-TGMA00002_00-EMUFWDUPDATER000 TGMA00002 "EFUpdater" \
    "$updater_ver" "$work/updater-out"

# EmuForwarder (TGMA00001), with the Updater's package inside.
stage="$work/app"
new_stage "$stage" emuforwarder
cp "$work/updater-out/IV0000-TGMA00002_00-EMUFWDUPDATER000.pkg" "$stage/updater.pkg"
# The program of the tiles EmuForwarder builds (it opens the installed emulator), and their fallback icon.
mkdir -p "$stage/tile"
cc -Wall -Wextra -Werror -c "$native/tile/forwarder.c" -o "$objects/forwarder.o"
"$llvm/bin/ld.lld" -m elf_x86_64 -pie --script "$oo/link.x" --eh-frame-hdr -L"$oo/lib" \
    "$oo/lib/crt1.o" "$objects/forwarder.o" -lc -lkernel -lSceSystemService -lSceLncUtil -lSceUserService -o "$objects/forwarder.elf"
"$bin/create-fself" -in "$objects/forwarder.elf" -out "$objects/forwarder.oelf" \
    --eboot "$stage/tile/eboot.bin" --paid 0x3800000000000011
cp "$native/icons/tile.png" "$stage/tile/icon0.png"
cc -Wall -Wextra -Werror -DEF_COLLECTION -c "$native/tile/forwarder.c" -o "$objects/collection-forwarder.o"
cxx "$native/tile/collection.cpp" "$objects/collection.o"
link "$objects/collection.elf" "$objects/collection.o" "$objects/collection-forwarder.o" "$objects/graphics.o"
"$bin/create-fself" -in "$objects/collection.elf" -out "$objects/collection.oelf" \
    --eboot "$stage/tile/collection.bin" --paid 0x3800000000000011
if [[ -n ${DOLPHIN_TILE_SOURCE:-} ]]; then
    dolphin=$(realpath "$DOLPHIN_TILE_SOURCE")
    test "$(cat "$dolphin/xmb/version.txt")" = 04.19
    test -f "$dolphin/Sys/totaldb.dsy"
    cp "$dolphin/eboot.bin" "$stage/tile/dolphin.bin"
    python3 "$native/tile/pack_emulator_assets.py" "$dolphin" "$stage/tile/dolphin-assets.bin" Sys xmb
fi
if [[ -n ${EMUSUNI_TILE_SOURCE:-} ]]; then
    emusuni=$(realpath "$EMUSUNI_TILE_SOURCE")
    test -f "$emusuni/xmb/assets.zip"
    python3 "$native/tile/pack_emulator_assets.py" "$emusuni" "$stage/tile/emusuni-assets.bin" xmb ppsspp fbneo
fi
link "$objects/app.elf" "$objects/main.o" "$objects/crash.o" "$objects/graphics.o" "$objects/sqlite3.o" "$objects/vorbis.o"
cp "$objects/app.elf" "$out/app.elf"
"$bin/create-fself" -in "$objects/app.elf" -out "$objects/app.oelf" \
    --eboot "$stage/eboot.bin" --paid 0x3800000000000011
printf "build=%s\nversion=%s\napp_ver=%s\nupdater=%s\n" "$build" "$label" "$app_ver" "$updater_ver" > "$out/build-info.txt"
make_package "$stage" IV0000-TGMA00001_00-TEGELMAKERPS4000 TGMA00001 EmuForwarder "$app_ver" "$out"
