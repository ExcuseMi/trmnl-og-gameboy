#!/usr/bin/env bash
# Write a .gb file into the 'rom' partition over USB (esptool in .venv, offset from partitions.csv).
#   tools/load_rom.sh <rom.gb> [port, default /dev/ttyACM0]
set -euo pipefail
ROM=${1:?usage: load_rom.sh rom.gb [port]}
PORT=${2:-/dev/ttyACM0}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
read -r OFF SIZE < <(awk -F'[ ,]+' '$1=="rom" {print $4, $5}' "$ROOT/partitions.csv")
[ "$(stat -c %s "$ROM")" -le $((SIZE)) ] || { echo "ROM larger than the rom partition ($SIZE bytes)" >&2; exit 1; }
if [ ! -x "$ROOT/.venv/bin/esptool.py" ]; then
    python3 -m venv "$ROOT/.venv" && "$ROOT/.venv/bin/pip" -q install esptool
fi
"$ROOT/.venv/bin/esptool.py" --chip esp32c3 -p "$PORT" -b 460800 write_flash "$OFF" "$ROM"
