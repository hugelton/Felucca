# SPDX-License-Identifier: GPL-3.0-only
# felucca_m5.c is the Felucca sources as one translation unit (web/emu/felucca_web.c's way); its headers come from this
# tree (three folders up) and from its build/gen (platforms/gen.sh).
set(F ${CMAKE_CURRENT_LIST_DIR}/../../..)
if(NOT EXISTS ${F}/build/gen/felucca_tables.h)
    message(FATAL_ERROR "run platforms/gen.sh first (build/gen)")
endif()
idf_component_register(SRCS "felucca_m5.c" "cores3_hw.c" "usb_audio.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2c esp_driver_i2s esp_driver_spi esp_driver_gpio esp_timer esp_psram
                       LDFRAGMENTS "linker.lf")
file(STRINGS ${F}/firmware/src/felucca.c VER REGEX "^#define FELUCCA_VERSION \"")
string(REGEX REPLACE ".*\"(.*)\".*" "\\1" VER "${VER}")
set_source_files_properties(felucca_m5.c PROPERTIES
    INCLUDE_DIRECTORIES "${F};${F}/build/gen;${F}/firmware/src;${F}/firmware/hal;${F}/firmware/bsp/chip;${F}/firmware/bsp/boards/fm1"
    COMPILE_OPTIONS "-std=gnu99;-w;-O2;-ffp-contract=off;-DFELUCCA_VERSION=\"${VER}-m5\"")
