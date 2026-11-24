#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca as a libretro core (platforms/libretro/felucca_libretro.c): Linux, macOS, Windows (MinGW).
#   platforms/libretro/build.sh   ->  build/libretro/felucca_libretro.{so,dylib,dll}
# After ./build.sh gen (build/gen). Needs a C compiler only. LIBRETRO_OPT replaces -O2, LIBRETRO_CFLAGS adds flags.
set -e
cd "$(dirname "$0")/../.."
. tools/hostflags.sh
host_gen_ok libretro
case "$(uname -s)" in
Darwin) EXT=dylib; LINK="-dynamiclib" ;;
MINGW*|MSYS*|CYGWIN*) EXT=dll; LINK="-shared -static-libgcc" ;;
*) EXT=so; LINK="-shared -Wl,--no-undefined -ldl" ;;
esac
mkdir -p build/libretro
# shellcheck disable=SC2086
${CC:-cc} ${LIBRETRO_OPT:--O2} $HOST_CFLAGS -fPIC -fvisibility=hidden ${LIBRETRO_CFLAGS:-} \
    "-DFELUCCA_VERSION=\"$VERSION\"" $HOST_INC -DFELUCCA_PLATFORM=FE_PLATFORM_LIBRETRO \
    $LINK -o build/libretro/felucca_libretro.$EXT platforms/libretro/felucca_libretro.c -lm
sed "s/@VERSION@/${VERSION#v}/" platforms/libretro/felucca_libretro.info > build/libretro/felucca_libretro.info
echo "libretro: build/libretro/felucca_libretro.$EXT ($VERSION)"
