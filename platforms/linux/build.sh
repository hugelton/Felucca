#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca on a desktop with SDL2 (platforms/linux/felucca_sdl.c): Linux, and macOS for development.
#   platforms/linux/build.sh   ->  build/linux/felucca
# Run after one ./build.sh (build/gen: the tables, the fonts, the CC0 samples). Needs a C compiler and SDL2
# (sdl2-config; Debian / Ubuntu: libsdl2-dev), and for MIDI ALSA (libasound2-dev) or, on macOS, CoreMIDI.
# -ffp-contract=off as the host build. LINUX_OPT replaces -O2, LINUX_CFLAGS adds flags.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/felucca_tables.h ] || { echo "linux: run ./build.sh once first (build/gen)"; exit 1; }
command -v sdl2-config >/dev/null 2>&1 || { echo "linux: no sdl2-config (SDL2)"; exit 1; }
VERSION=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c | head -n 1)
# MIDI (platforms/linux/midi.c): CoreMIDI on macOS, ALSA where pkg-config finds it (libasound2-dev), else none
case "$(uname -s)" in
Darwin) MIDI="-framework CoreMIDI -framework CoreFoundation" ;;
*) if pkg-config --exists alsa 2>/dev/null; then MIDI="-DFELUCCA_ALSA $(pkg-config --cflags --libs alsa)"; else MIDI=""; echo "linux: no ALSA (libasound2-dev): no MIDI"; fi ;;
esac
mkdir -p build/linux
# shellcheck disable=SC2046,SC2086
${CC:-cc} ${LINUX_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${LINUX_CFLAGS:-} \
    "-DFELUCCA_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 \
    $(sdl2-config --cflags) -o build/linux/felucca platforms/linux/felucca_sdl.c $(sdl2-config --libs) $MIDI -lm
echo "linux: build/linux/felucca ($VERSION)"
