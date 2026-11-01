# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# What every host build shares (platforms/*/build.sh, tests/run_tests.sh), sourced from the tree's top:
#   HOST_INC     the include path of the firmware against the simulated FM-1 (platforms/host)
#   HOST_CFLAGS  -std=gnu99 -ffp-contract=off (no fused multiply-adds: every platform renders the same audio) -w
#   VERSION      the firmware's FELUCCA_VERSION (firmware/src/felucca.c), for -DFELUCCA_VERSION
#   host_gen_ok  stops with a message when build/gen is missing (./build.sh gen makes it)
HOST_INC="-Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1"
HOST_CFLAGS="-std=gnu99 -ffp-contract=off -w"
VERSION=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c | head -n 1)
host_gen_ok() {
    [ -f build/gen/felucca_tables.h ] || { echo "$1: no build/gen (run ./build.sh gen first)"; exit 1; }
}
