#!/usr/bin/env bash
set -euo pipefail

: "${OO_PS4_TOOLCHAIN:?Set OO_PS4_TOOLCHAIN to the OpenOrbis PS4Toolchain folder}"
project_dir=$(cd -- "$(dirname -- "$0")" && pwd)
out_dir=$(realpath -m -- "${1:-$project_dir/out}")
stage=$(mktemp -d)
trap 'python3 -c "import shutil,sys; shutil.rmtree(sys.argv[1])" "$stage"' EXIT
tools="$OO_PS4_TOOLCHAIN/bin/linux"
version=01.02
title_id="${TILE_ID:?}"
content_id="IV0000-${title_id}_00-DOLPHINFORWARD00"
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1

mkdir -p "$out_dir"
: "${DOLPHIN_APP_ROOT:?Set DOLPHIN_APP_ROOT to the unpacked DolphinPS4 app}"
cp -a "$DOLPHIN_APP_ROOT/." "$stage/"
mv -- "$stage/eboot.bin" "$stage/${EMU_SELF:-dolphin.self}"
python3 -c 'from pathlib import Path; import sys; (Path(sys.argv[1]) / "sce_sys/keystone").unlink(missing_ok=True)' "$stage"
cp "$project_dir/icon0.png" "$stage/sce_sys/"
if [[ -f "$project_dir/pic1.png" ]]; then
    cp "$project_dir/pic1.png" "$stage/sce_sys/"
fi

"${PS4_CC:-clang}" --target=x86_64-pc-freebsd12-elf \
    -fPIC -funwind-tables -O2 -Wall -Wextra -Werror \
    -isysroot "$OO_PS4_TOOLCHAIN" -isystem "$OO_PS4_TOOLCHAIN/include" \
    -c "$project_dir/forwarder.c" -o "$stage/forwarder.o"
"${PS4_LD:-ld.lld}" -m elf_x86_64 -pie --script "$OO_PS4_TOOLCHAIN/link.x" \
    --eh-frame-hdr -L"$OO_PS4_TOOLCHAIN/lib" "$OO_PS4_TOOLCHAIN/lib/crt1.o" \
    "$stage/forwarder.o" -lc -lkernel -lSceSystemService \
    -o "$stage/forwarder.elf"
"$tools/create-fself" -in "$stage/forwarder.elf" -out "$stage/forwarder.oelf" \
    --eboot "$stage/eboot.bin" --paid 0x3800000000000011

sfo="$stage/sce_sys/param.sfo"
"$tools/PkgTool.Core" sfo_new "$sfo"
mkdir -p "$stage/.build"
mv -- "$stage/forwarder.o" "$stage/forwarder.elf" "$stage/forwarder.oelf" "$stage/.build/"
field() {
    "$tools/PkgTool.Core" sfo_setentry "$sfo" "$1" --type "$2" --maxsize "$3" --value "$4" >/dev/null
}
field APP_TYPE Integer 4 1
field APP_VER Utf8 8 "$version"
field ATTRIBUTE Integer 4 0
field CATEGORY Utf8 4 gd
field CONTENT_ID Utf8 48 "$content_id"
field DOWNLOAD_DATA_SIZE Integer 4 0
field SYSTEM_VER Integer 4 0
field TITLE Utf8 128 "${TILE_TITLE:?}"
field TITLE_ID Utf8 12 "$title_id"
field VERSION Utf8 8 "$version"

(
    cd "$stage"
    "$tools/create-gp4" -out project.gp4 --content-id="$content_id" --files "eboot.bin"
    python3 "$project_dir/gp4_files.py"
    "$tools/PkgTool.Core" pkg_build project.gp4 "$out_dir"
)
"$tools/PkgTool.Core" pkg_validate "$out_dir/$content_id.pkg"
