#!/usr/bin/env bash
# Boot the firmware with a ROM in Espressif QEMU (ESP32-C3), print the console for N seconds. Runs in the IDF container:
#   tools/idf/idf.sh tools/qemu.sh [rom.gb] [seconds]
# The panel is not emulated: expect epd timeouts, the point is boot, ROM check, heap and fps lines.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
ROM=${1:-tests/roms/libbet.gb}
SECS=${2:-25}
B=build-qemu
rm -f $B/sdkconfig
idf.py -B $B -DSDKCONFIG=$B/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.qemu" build >$B.log 2>&1 || { tail -30 $B.log; exit 1; }
ROMOFF=$(python3 tools/part_offset.py partitions.csv rom)
ROM=$(realpath "$ROM")
(cd $B
read -r -a OPTS < <(head -n1 flash_args)
python -m esptool --chip esp32c3 merge_bin --fill-flash-size 4MB -o qemu.bin "${OPTS[@]}" \
    $(tail -n +2 flash_args | sort -g | tr '\n' ' ') "$ROMOFF" "$ROM" >/dev/null)
timeout "$SECS" qemu-system-riscv32 -nographic -M esp32c3 -m 4M -drive file=$B/qemu.bin,if=mtd,format=raw \
    -global driver=timer.esp32c3.timg,property=wdt_disable,value=true || true
