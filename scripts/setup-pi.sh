#!/usr/bin/env bash
# One-time setup for the bare-metal Pi build: downloads the Arm GNU toolchain,
# the Circle library and the Raspberry Pi boot firmware into pi-deps/, then
# configures and builds Circle for a Raspberry Pi 3 (AArch64).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/pi-deps"
CIRCLE_TAG="Step51.1.1"
TOOLCHAIN_VERSION="15.2.rel1"

mkdir -p "$DEPS"

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)  host="darwin-arm64" ;;
    Darwin-x86_64) host="darwin-x86_64" ;;
    Linux-x86_64)  host="x86_64" ;;
    Linux-aarch64) host="aarch64" ;;
    *) echo "unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

if [[ ! -x "$DEPS/toolchain/bin/aarch64-none-elf-gcc" ]]; then
    echo "Downloading Arm GNU toolchain $TOOLCHAIN_VERSION ($host)..."
    url="https://developer.arm.com/-/media/Files/downloads/gnu/$TOOLCHAIN_VERSION/binrel/arm-gnu-toolchain-$TOOLCHAIN_VERSION-$host-aarch64-none-elf.tar.xz"
    mkdir -p "$DEPS/toolchain"
    curl -fL "$url" | tar xJ -C "$DEPS/toolchain" --strip-components=1
fi
export PATH="$DEPS/toolchain/bin:$PATH"

if [[ ! -f "$DEPS/circle/Rules.mk" ]]; then
    echo "Downloading Circle $CIRCLE_TAG..."
    mkdir -p "$DEPS/circle"
    curl -fL "https://codeload.github.com/rsta2/circle/tar.gz/refs/tags/$CIRCLE_TAG" |
        tar xz -C "$DEPS/circle" --strip-components=1
fi

echo "Building Circle libraries..."
cd "$DEPS/circle"
# USE_USB_FIQ lets USB interrupts pre-empt the audio IRQ, which renders the synth.
./configure -r 3 -p aarch64-none-elf- -d USE_USB_FIQ -f
./makeall --nosample clean >/dev/null
./makeall --nosample

# Same firmware revision Circle's boot/Makefile pins.
firmware="$(sed -n 's/^FIRMWARE ?= //p' boot/Makefile)"
mkdir -p "$DEPS/firmware"
for f in bootcode.bin start.elf fixup.dat LICENCE.broadcom; do
    if [[ ! -s "$DEPS/firmware/$f" ]]; then
        echo "Downloading firmware $f..."
        curl -fsSL -o "$DEPS/firmware/$f" \
            "https://github.com/raspberrypi/firmware/raw/$firmware/boot/$f"
    fi
done

echo "Setup done. Build with scripts/build-pi.sh"
