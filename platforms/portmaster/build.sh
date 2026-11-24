#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca for PortMaster (research, not run on a handheld): the SDL2 frontend (platforms/sdl) built for aarch64 Linux
# in Docker (platforms/sdl/build.sh's command line), with the launcher, into a folder laid out as PortMaster's ports.
#   platforms/portmaster/build.sh   ->  build/portmaster/ports/Felucca.sh, build/portmaster/ports/felucca/
# After platforms/gen.sh (build/gen). Needs Docker (an arm64 image: native on an arm64 host, emulated elsewhere).
# Debian bookworm's glibc (2.36) and SDL2 headers (PM_IMAGE picks another image); the device's own SDL2 is used at
# run time. No ALSA: no MIDI in.
set -e
cd "$(dirname "$0")/../.."
. tools/hostflags.sh
host_gen_ok portmaster
docker run --rm --platform linux/arm64 -v "$PWD:/src" -w /src "${PM_IMAGE:-debian:bookworm}" sh -c '
    apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends gcc libc6-dev libsdl2-dev >/dev/null &&
    . tools/hostflags.sh && mkdir -p build/portmaster/ports/felucca &&
    cc -O2 -march=armv8-a $HOST_CFLAGS "-DFELUCCA_VERSION=\"$VERSION\"" $HOST_INC $(sdl2-config --cflags) \
        -o build/portmaster/ports/felucca/felucca platforms/sdl/felucca_sdl.c $(sdl2-config --libs) -lm -ldl'
cp platforms/portmaster/Felucca.sh build/portmaster/ports/Felucca.sh
cp platforms/portmaster/README.md build/portmaster/ports/felucca/README.md
chmod +x build/portmaster/ports/Felucca.sh build/portmaster/ports/felucca/felucca
echo "portmaster: build/portmaster/ports ($VERSION)"
