#!/usr/bin/env bash
set -euo pipefail
native=$(cd -- "$(dirname -- "$0")" && pwd)
tools=$(realpath "$native/../../../work/forwarder")
oo="$tools/OpenOrbis/PS4Toolchain"
llvm="$tools/compiler/root/usr"
out=$(realpath -m "${1:-$native/out}")
stage=$(mktemp -d)
trap 'python3 -c "import shutil,sys; shutil.rmtree(sys.argv[1])" "$stage"' EXIT
export OO_PS4_TOOLCHAIN="$oo"
export LD_LIBRARY_PATH="$llvm/lib64:$tools/openssl/usr/lib/x86_64-linux-gnu"
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
bin="$oo/bin/linux"
common="$oo/samples/_common"
# One number for everything: build 13 = v0.13 = APP_VER 01.73.
build=$(tr -dc 0-9 < "$native/VERSION")
label="v0.$build"
app_ver=$(printf "01.%02d" $((60 + build)))

mkdir -p "$stage/sce_sys/about" "$stage/sce_module" "$stage/assets/fonts" "$stage/.build" "$out"
cp "$oo/samples/font/sce_module/"{libc,libSceFios2}.prx "$stage/sce_module/"
cp "$oo/samples/font/sce_sys/about/right.sprx" "$stage/sce_sys/about/"
cp "$oo/samples/font/assets/fonts/"{Gontserrat-Regular.ttf,OFL.txt} "$stage/assets/fonts/"
cp "$native/icon0.png" "$stage/sce_sys/"
for source in "$native/main.cpp" "$native/crash.cpp" "$native/graphics.cpp"; do
    "$llvm/bin/clang++" --target=x86_64-pc-freebsd12-elf -std=gnu++17 -D_GNU_SOURCE -fPIC -O2 \
        -funwind-tables -DEF_BUILD=$build -DEF_VERSION="\"$label\"" -DGRAPHICS_USES_FONT -DTEGELMAKER_PS4 -isysroot "$oo" -isystem "$oo/include" \
        -isystem "$oo/include/c++/v1" -I"$common" -c "$source" \
        -o "$stage/.build/$(basename "$source" .cpp).o"
done
# SQLite (official amalgamation) to rename tiles in the PS4's app database.
sqlite="$tools/../sqlite/sqlite-amalgamation-3470200/sqlite3.c"
"$llvm/bin/clang" --target=x86_64-pc-freebsd12-elf -fPIC -O2 -funwind-tables -isysroot "$oo" -isystem "$oo/include" \
    -DSQLITE_OS_UNIX=1 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -DSQLITE_DQS=0 -DHAVE_USLEEP=1 -DSQLITE_OMIT_DEPRECATED=1 -c "$sqlite" -o "$stage/.build/sqlite3.o"
"$llvm/bin/ld.lld" -m elf_x86_64 -pie --script "$oo/link.x" --eh-frame-hdr -L"$oo/lib" \
    "$oo/lib/crt1.o" "$stage/.build/"*.o -lc -lkernel -lc++ -lc++abi \
    -lSceVideoOut -lSceFreeType -lSceSysmodule -lScePad -lSceUserService -lSceSystemService -lSceNet -lSceNetCtl \
    -lSceAppInstUtil -lSceBgft -lSceHttp -lSceSsl -lSceImeDialog \
    -o "$stage/.build/app.elf"
mkdir -p "$out" && cp "$stage/.build/app.elf" "$out/app.elf"
"$bin/create-fself" -in "$stage/.build/app.elf" -out "$stage/.build/app.oelf" \
    --eboot "$stage/emuforwarder.self" --paid 0x3800000000000011
cp "$stage/emuforwarder.self" "$out/emuforwarder.self"
# The starter (eboot.bin) starts the newest EmuForwarder: a downloaded update or the one above.
"$llvm/bin/clang" --target=x86_64-pc-freebsd12-elf -fPIC -O2 -funwind-tables -Wall -Werror -DEF_BUILD=$build \
    -isysroot "$oo" -isystem "$oo/include" -c "$native/loader.c" -o "$stage/.build/loader.o"
"$llvm/bin/ld.lld" -m elf_x86_64 -pie --script "$oo/link.x" --eh-frame-hdr -L"$oo/lib" \
    "$oo/lib/crt1.o" "$stage/.build/loader.o" -lc -lkernel -lSceSystemService -o "$stage/.build/loader.elf"
"$bin/create-fself" -in "$stage/.build/loader.elf" -out "$stage/.build/loader.oelf" \
    --eboot "$stage/eboot.bin" --paid 0x3800000000000011
printf "build=%s\nversion=%s\napp_ver=%s\n" "$build" "$label" "$app_ver" > "$out/build-info.txt"

content_id=IV0000-TGMA00001_00-TEGELMAKERPS4000
sfo="$stage/sce_sys/param.sfo"
"$bin/PkgTool.Core" sfo_new "$sfo"
field() {
    "$bin/PkgTool.Core" sfo_setentry "$sfo" "$1" --type "$2" --maxsize "$3" --value "$4" >/dev/null
}
field APP_TYPE Integer 4 1
field APP_VER Utf8 8 "$app_ver"
field ATTRIBUTE Integer 4 0
field CATEGORY Utf8 4 gd
field CONTENT_ID Utf8 48 "$content_id"
field DOWNLOAD_DATA_SIZE Integer 4 0
field SYSTEM_VER Integer 4 0
field TITLE Utf8 128 "EmuForwarder"
field TITLE_ID Utf8 12 TGMA00001
field VERSION Utf8 8 "$app_ver"
(
    cd "$stage"
    "$bin/create-gp4" -out project.gp4 --content-id="$content_id" --files "eboot.bin"
    python3 "$native/../offline-slot/gp4_files.py"
    "$bin/PkgTool.Core" pkg_build project.gp4 "$out"
)
"$bin/PkgTool.Core" pkg_validate "$out/$content_id.pkg"
