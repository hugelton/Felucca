#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca in the browser: the host (platforms/host/felucca_host.c: the firmware against a simulated FM-1) compiled to
# WebAssembly with Emscripten, its API (felucca_host.h) as the wasm's exports, plus the page (index.html, the
# AudioWorklet worklet.js). After X0X's web/emu/build.sh (charlesvestal/fm1-x0x, GPL-3.0).
#   platforms/web/build.sh   ->  build/web/{index.html, worklet.js, felucca.wasm, fonts/}
# After ./build.sh gen (build/gen). WEB_OPT replaces -O2, WEB_CFLAGS adds flags (WEB_CFLAGS=-msimd128: let the
# compiler vectorise; every browser with AudioWorklet has wasm SIMD). The page shows the version (FELUCCA_VERSION)
# marked "(preview)" before its release; WEB_PREVIEW=0 drops the mark.
set -e
cd "$(dirname "$0")/../.."
. tools/hostflags.sh
host_gen_ok web
command -v emcc >/dev/null 2>&1 || { echo "web: no emcc (Emscripten)"; exit 1; }
mkdir -p build/web/fonts
# shellcheck disable=SC2086
emcc ${WEB_OPT:--O2} $HOST_CFLAGS ${WEB_CFLAGS:-} "-DFELUCCA_VERSION=\"$VERSION\"" $HOST_INC \
    '-DFH_API=__attribute__((used, visibility("default")))' -DFELUCCA_PLATFORM=FE_PLATFORM_WEB \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/web/felucca.wasm platforms/host/felucca_host.c
[ "${WEB_PREVIEW:-1}" = 0 ] && PREVIEW='' || PREVIEW=' (preview)'
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PREVIEW@/$PREVIEW/g" platforms/web/index.html > build/web/index.html
cp platforms/web/worklet.js build/web/
cp platforms/web/fonts/DotGothic16-subset.woff platforms/web/fonts/OFL.txt build/web/fonts/
echo "web: build/web ($(wc -c < build/web/felucca.wasm | tr -d ' ') B wasm, $VERSION)"
