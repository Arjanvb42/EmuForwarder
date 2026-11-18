#!/usr/bin/env bash
# Bouwt EmuForwarder en zet het pakket op GitHub. Elke PS4 met EmuForwarder ziet de update bij het
# starten en downloadt het pakket naar /data/pkg; EFUpdater installeert het.
#   ./publiceer.sh 1.0.1 "wat er nieuw is"
# De versie moet hoger zijn dan de vorige (native/RELEASE). Het buildnummer (native/VERSION) gaat
# er elke keer 1 omhoog; daarop vergelijkt de app of er een update is.
set -euo pipefail
cd "$(dirname "$0")"
release=${1:?"gebruik: ./publiceer.sh <versie, bv. 1.0.1> \"wat er nieuw is\""}
notes=${2:-}
[[ $release =~ ^[0-9]+\.[0-9]\.[0-9]{1,2}$ ]] || { echo "versie moet zoals 1.0.1 zijn (middelste cijfer 0-9)" >&2; exit 1; }
previous=$(cat native/RELEASE)
if [[ $release != "$previous" ]] && [[ $(printf "%s\n%s\n" "$previous" "$release" | sort -V | tail -1) != "$release" ]]; then
    echo "versie $release is niet hoger dan $previous" >&2; exit 1
fi
if [[ $release == "$previous" ]] && git rev-parse -q --verify "refs/tags/v$release" >/dev/null; then
    echo "v$release is al uitgebracht; kies een hogere versie" >&2; exit 1
fi
echo "$release" > native/RELEASE
build=$(( $(tr -dc 0-9 < native/VERSION) + 1 ))
echo "$build" > native/VERSION
bash native/build.sh
cp native/out/IV0000-TGMA00001_00-TEGELMAKERPS4000.pkg update/EmuForwarder.pkg
cp native/out/IV0000-TGMA00001_00-TEGELMAKERPS4000.pkg "EmuForwarder-v$release.pkg"
cp native/out/app.elf "EmuForwarder-v$release.elf"   # om crash.log-adressen te lezen
python3 - "$build" "v$release" "$notes" <<'PY'
import sys
build, version, notes = sys.argv[1], sys.argv[2], sys.argv[3]
data = open("update/EmuForwarder.pkg", "rb").read()
h = 1469598103934665603
for b in data:
    h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
open("update/latest.txt", "w").write(
    f"build={build}\nversion={version}\nsize={len(data)}\nfnv={h:016x}\nnotes={notes.replace(chr(10), ' ')}\n")
PY
git add -A
git commit -m "EmuForwarder v$release${notes:+: $notes}" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag -a "v$release" -m "EmuForwarder v$release"
git push origin main "v$release"
echo "v$release (build $build) staat op GitHub. Pakket voor een nieuwe installatie: EmuForwarder-v$release.pkg"
