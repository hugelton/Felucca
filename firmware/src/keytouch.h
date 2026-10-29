/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (fm1-base lib/sense.c, lib/touch.c: the driven readings, MONK-1's pressure)
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca's port: pressure only) */
/* AFTERTOUCH (1.8), the scan's side: felucca.c includes this before bsp/boards/fm1/fm1_input.h, whose analog rows
 * then read the keys (keytouch.c turns them on). While kt_on, a key held on row PA7 or PA8 (F3 .. C5) is read with its
 * row driven (FM1_INPUT_DRIVE: the pill's resistance, not the matrix diode), its column's LEDs dark on those ticks
 * (a lit LED lifts the reading), and the reading kept for the main loop (kt_d). The notes still come from the digital
 * scan as ever: only the pressure is read here. */
#define KT_ROWKEYS 20u                    /* the keys of the analog rows: notes 0..19 */
static volatile uint8_t kt_on;            /* the readings are on (keytouch.c kt_poll) */
static volatile uint16_t kt_d[KT_ROWKEYS];   /* each key's last driven reading (1023 open, lower pressed harder) */
static volatile uint8_t kt_dn[KT_ROWKEYS];   /* driven readings since the main loop last saw the key up (to 255) */

static inline void kt_sample(int32_t n, uint32_t v, uint32_t driven)
{
    if (driven && n >= 0 && n < (int32_t)KT_ROWKEYS) {
        kt_d[n] = (uint16_t)v;
        if (kt_dn[n] < 255u)
            kt_dn[n]++;
    }
}
#define KT_HELD(col, rowbit) (kt_on && FM1_KEYMAP[rowbit][col] >= 14 && \
                              ((fm1_in.notes >> (FM1_KEYMAP[rowbit][col] - 14)) & 1u))
#define FM1_INPUT_DRIVE(col, rowbit) KT_HELD(col, rowbit)
#define FM1_INPUT_LED_OK(col, rowbit) (!KT_HELD(col, rowbit))
#define FM1_INPUT_ANALOG(col, rowbit, v, driven) kt_sample(FM1_KEYMAP[rowbit][col] - 14, v, driven)
