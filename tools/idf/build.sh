#!/usr/bin/env bash
# Runs inside the IDF container (idf.sh): build.sh [bayer|threshold]. Output: build/dist/{gameboy-merged.bin,gameboy.bin,partition-table.bin,bootloader.bin}
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=$ROOT/build
cd "$ROOT"
# defaults only fill options missing from an existing sdkconfig: restart it when they changed
SIG=$(cat sdkconfig.defaults partitions.csv main/Kconfig.projbuild | sha256sum | cut -c1-16)
if [ "$(cat "$B/.defaults.sig" 2>/dev/null)" != "$SIG" ]; then rm -f sdkconfig; fi
mkdir -p "$B" && echo "$SIG" > "$B/.defaults.sig"
MODE=${1:-bayer}
case $MODE in bayer|threshold) ;; *) echo "GB_SHADE_MODE: bayer or threshold" >&2; exit 1 ;; esac
idf.py -B "$B" -DGB_SHADE_MODE="$MODE" build
D=$B/dist
rm -rf "$D" && mkdir -p "$D"
cd "$B"
read -r -a OPTS < <(head -n1 flash_args)
python -m esptool --chip esp32c3 merge_bin -o "$D/gameboy-merged.bin" "${OPTS[@]}" $(tail -n +2 flash_args | sort -g | tr '\n' ' ') >/dev/null
cp bootloader/bootloader.bin partition_table/partition-table.bin "$D/"
cp trmnl-og-gameboy.bin "$D/gameboy.bin"
ls -l "$D"
