/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware's sound side for a host build (a compiler of the computer, not the FM-1's): libc, the engines, the
 * voices, the FX, MIDI's parsers and the sequencer, in firmware/src/felucca.c's order. platforms/host/felucca_host.c
 * builds the rest of the firmware on it (the UI, main.c, storage), and so do the host tests (tests/hostsim.c and the
 * tests built on it). The FM-1's HAL headers are not here: what these files use of it, the includer gives first
 * (fm1_in below; SMP_USER_XIP, FELUCCA_* as it wants them). Include path: build/gen, firmware/src, firmware/hal,
 * firmware/bsp/chip, firmware/bsp/boards/fm1 (tools/hostflags.sh). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)                                /* (the FM-1's sections and attributes: none here) */
#define memset felucca_memset                           /* the firmware's libc, beside the computer's */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../../firmware/bsp/chip/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;   /* the panel's scan (bsp fm1_input.h): keys, buttons */
#include "../../firmware/src/core.h"
#include "../../firmware/src/engines.c"
#include "../../firmware/src/params.c"
#include "../../firmware/src/mod.c"
#include "../../firmware/src/voice.c"
#include "../../firmware/src/slicer.c"
#include "../../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../../firmware/bsp/chip/usb.c"
#include "../../firmware/src/midi_uart.c"               /* TRS MIDI IN: its parser (um_byte) feeds midi_in_q */
#include "../../firmware/src/song_chain.c"
#include "../../firmware/src/seq.c"
