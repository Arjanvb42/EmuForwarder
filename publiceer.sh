#!/usr/bin/env bash
# Bouwt EmuForwarder en zet het pakket op GitHub. Elke PS4 met EmuForwarder ziet de update bij het
# starten en downloadt het pakket naar /data/pkg (installeren via GoldHEN > Package Installer).
#   ./publiceer.sh "wat er nieuw is"
# Het versienummer staat in native/VERSION en gaat er elke keer 1 omhoog.
set -euo pipefail
cd "$(dirname "$0")"
notes=${1:-}
build=$(( $(tr -dc 0-9 < native/VERSION) + 1 ))
echo "$build" > native/VERSION
bash native/build.sh
git rm -q --cached --ignore-unmatch update/emuforwarder.self update/emuforwarder.prx
rm -f update/emuforwarder.self update/emuforwarder.prx
cp native/out/IV0000-TGMA00001_00-TEGELMAKERPS4000.pkg update/EmuForwarder.pkg          # eerste installatie
cp native/out/update/IV0000-TGMA00001_00-TEGELMAKERPS4000.pkg update/EmuForwarder-update.pkg   # update (patch)
cp update/EmuForwarder-update.pkg "EmuForwarder-v0.$build-update.pkg"
cp native/out/IV0000-TGMA00001_00-TEGELMAKERPS4000.pkg "EmuForwarder-v0.$build.pkg"
cp native/out/app.elf "EmuForwarder-v0.$build.elf"   # om crash.log-adressen te lezen
python3 - "$build" "$notes" <<'PY'
import sys
build, notes = sys.argv[1], sys.argv[2]
data = open("update/EmuForwarder-update.pkg", "rb").read()
h = 1469598103934665603
for b in data:
    h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
open("update/latest.txt", "w").write(
    f"build={build}\nversion=v0.{build}\nsize={len(data)}\nfnv={h:016x}\nnotes={notes.replace(chr(10), ' ')}\n")
PY
git add -A
git commit -m "EmuForwarder v0.$build${notes:+: $notes}" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
echo "v0.$build staat op GitHub. Pakket voor een nieuwe installatie: EmuForwarder-v0.$build.pkg"
