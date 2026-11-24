/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI bytes from a computer's port into the device (after felucca_host.c): running status, channel messages only
 * (notes, CC, program change, pressure, pitch bend: fh_midi); real-time bytes and SysEx are skipped (not the clock or
 * the editor yet). One parser a port. */
typedef struct { uint8_t st, d[2], nd, sysex; uint32_t count; } mpar_t;

static void mpar_byte(mpar_t *m, uint8_t b)
{
    if (b >= 0xF8u)                                     /* (real time: the clock, not yet) */
        return;
    if (b >= 0x80u) {
        m->sysex = b == 0xF0u;
        m->st = b < 0xF0u ? b : 0;
        m->nd = 0;
        return;
    }
    if (m->sysex || !m->st)
        return;
    m->d[m->nd++] = b;
    if (m->nd == ((m->st & 0xE0u) == 0xC0u ? 1u : 2u)) {  /* (program change, channel pressure: one byte) */
        m->count++;
        fh_midi(m->st, m->d[0], m->nd > 1u ? m->d[1] : 0u);
        m->nd = 0;
    }
}
