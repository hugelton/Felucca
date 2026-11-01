#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build Felucca (see BUILDING.md).
#   ./build.sh [--release X.Y.Z]   the FM-1 firmware: build/felucca.fwsc (needs the JieLi toolchain, the SDK, Docker)
#   ./build.sh gen                 the generated headers only, build/gen (Python 3, Pillow, fontTools)
#   ./build.sh web | sdl | libretro   a host platform (platforms/*/build.sh), after gen
#   ./build.sh host                gen, then every host platform whose tools are there
#   ./build.sh test                tests/run_tests.sh
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"

pillow_ok() {
    "$PY" -c 'import PIL, fontTools' 2>/dev/null || { echo "build.sh: $PY needs Pillow and fontTools (pip3 install Pillow fonttools)"; exit 1; }
}
case "${1:-}" in
gen)
    pillow_ok
    exec "$PY" tools/build.py --gen ;;
web|sdl|libretro)
    [ -f build/gen/felucca_tables.h ] || { pillow_ok; "$PY" tools/build.py --gen; }
    exec sh "platforms/$1/build.sh" ;;
host)
    pillow_ok
    "$PY" tools/build.py --gen
    if command -v emcc >/dev/null 2>&1; then sh platforms/web/build.sh; else echo "build.sh: no emcc: not the web build"; fi
    if command -v sdl2-config >/dev/null 2>&1; then sh platforms/sdl/build.sh; else echo "build.sh: no SDL2: not the desktop build"; fi
    exec sh platforms/libretro/build.sh ;;
test)
    exec sh tests/run_tests.sh ;;
""|--*) ;;
*)
    echo "build.sh: $1? (gen, web, sdl, libretro, host, test, or the firmware: no argument or --release X.Y.Z)"; exit 2 ;;
esac

pillow_ok
[ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || { echo "build.sh: no toolchain in $JIELI_TOOLCHAIN (run tools/get_toolchain.sh)"; exit 1; }
[ -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no JieLi AC79 SDK in $AC79_SDK"; exit 1; }
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running"; exit 1; }
fi
exec "$PY" tools/build.py "$@"
