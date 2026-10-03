#!/usr/bin/env bash
# Run a command in the ESP-IDF container (image and cache shared with tiny-paper).
#   tools/idf/idf.sh setup          one-time: build image, fetch ESP-IDF + esp32c3/esp32s3 tools + QEMU into the cache
#   tools/idf/idf.sh idf.py build   any command, run in the current directory (must be inside the repo)
# Cache: $TINY_IDF_CACHE (default ~/workspace/.cache/tiny-paper-idf-<version>), about 3 GB (+ ~1.2 GB for esp32s3).
# Re-running setup on an existing cache only adds what is missing.
set -euo pipefail
IDF_VERSION=${IDF_VERSION:-v5.5.1}
IMAGE=tiny-paper-idf:${IDF_VERSION}
CACHE=${TINY_IDF_CACHE:-$HOME/workspace/.cache/tiny-paper-idf-$IDF_VERSION}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
mkdir -p "$CACHE"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" "$HERE"
fi

run() {
    local tty=()
    [ -t 0 ] && [ -t 1 ] && tty=(-it)
    docker run --rm "${tty[@]}" --user "$(id -u):$(id -g)" \
        -v "$CACHE:/idf" -v "$ROOT:$ROOT" -w "$PWD" \
        -e IDF_VERSION="$IDF_VERSION" "$IMAGE" bash -c "$1"
}

if [ "${1:-}" = setup ]; then
    run '
        set -e
        if [ ! -f /idf/esp-idf/.done ]; then
            rm -rf /idf/esp-idf
            git clone --depth 1 --branch "$IDF_VERSION" --recursive --shallow-submodules \
                https://github.com/espressif/esp-idf.git /idf/esp-idf
            touch /idf/esp-idf/.done
        fi
        cd /idf/esp-idf
        # esp32s3 (TRMNL X) adds xtensa-esp-elf, its gdb and esp32ulp-elf (~1.2 GB); listing the target
        # is what makes export.sh put the Xtensa compiler on PATH
        ./install.sh esp32c3
        rm -rf /idf/tools/dist
    '
    exit 0
fi

[ -f "$CACHE/esp-idf/.done" ] || { echo "ESP-IDF not set up; run: $0 setup" >&2; exit 1; }
run ". /idf/esp-idf/export.sh >/dev/null && $*"
