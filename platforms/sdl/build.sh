#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca on a desktop with SDL2 (platforms/sdl/felucca_sdl.c): macOS and Linux.
#   platforms/sdl/build.sh   ->  build/sdl/felucca
# After ./build.sh gen (build/gen). Needs a C compiler and SDL2 (sdl2-config; Debian / Ubuntu: libsdl2-dev), and for MIDI
# ALSA (libasound2-dev) or, on macOS, CoreMIDI. SDL_OPT replaces -O2, SDL_CFLAGS adds flags.
set -e
cd "$(dirname "$0")/../.."
. tools/hostflags.sh
host_gen_ok sdl
command -v sdl2-config >/dev/null 2>&1 || { echo "sdl: no sdl2-config (SDL2)"; exit 1; }
# MIDI (platforms/sdl/midi.c): CoreMIDI on macOS, ALSA where pkg-config finds it (libasound2-dev), else none
case "$(uname -s)" in
Darwin) MIDI="-framework CoreMIDI -framework CoreFoundation" ;;
*) if pkg-config --exists alsa 2>/dev/null; then MIDI="-DFELUCCA_ALSA $(pkg-config --cflags --libs alsa)"; else MIDI=""; echo "sdl: no ALSA (libasound2-dev): no MIDI"; fi ;;
esac
mkdir -p build/sdl
# shellcheck disable=SC2046,SC2086
${CC:-cc} ${SDL_OPT:--O2} $HOST_CFLAGS ${SDL_CFLAGS:-} "-DFELUCCA_VERSION=\"$VERSION\"" $HOST_INC \
    $(sdl2-config --cflags) -o build/sdl/felucca platforms/sdl/felucca_sdl.c $(sdl2-config --libs) $MIDI -lm \
    $([ "$(uname -s)" = Darwin ] || echo -ldl)
echo "sdl: build/sdl/felucca ($VERSION)"
