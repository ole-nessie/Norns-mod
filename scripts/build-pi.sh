#!/usr/bin/env bash
# Builds the bare-metal kernel and collects everything the SD card needs in
# dist/sdcard/. Copy that folder's contents to a FAT32-formatted card.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/pi-deps"
OUT="$ROOT/dist/sdcard"

if [[ ! -f "$DEPS/circle/Config.mk" ]]; then
    echo "Run scripts/setup-pi.sh first." >&2
    exit 1
fi

export PATH="$DEPS/toolchain/bin:$PATH"
make -C "$DEPS/circle/addon/SDCard" -j"$(getconf _NPROCESSORS_ONLN)"
make -C "$DEPS/circle/addon/fatfs" -j"$(getconf _NPROCESSORS_ONLN)"
make -C "$ROOT/src/pi" -j"$(getconf _NPROCESSORS_ONLN)"

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$DEPS/firmware/"{bootcode.bin,start.elf,fixup.dat,LICENCE.broadcom} "$OUT/"
cp "$ROOT/src/pi/config.txt" "$OUT/"
cp "$ROOT/src/pi/kernel8.img" "$OUT/"

echo
echo "SD card files in $OUT:"
ls -l "$OUT"
