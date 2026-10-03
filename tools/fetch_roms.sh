#!/usr/bin/env bash
# Download the free homebrew test ROMs into tests/roms/ (gitignored). See tests/ROMS.md.
set -euo pipefail
D=$(cd "$(dirname "$0")/.." && pwd)/tests/roms
mkdir -p "$D"
get() { [ -s "$D/$2" ] || curl -fsSL -o "$D/$2" "$1"; }
get https://github.com/pinobatch/libbet/releases/download/v0.08/libbet.gb libbet.gb
get https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb dmg-acid2.gb
sha256sum "$D"/*.gb
