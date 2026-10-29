/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (fm1-base lib/touch.c: MONK-1's pressure, its constants)
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca's port: integer, per track SENS) */
/* AFTERTOUCH (1.8), the main loop's side (keytouch.h the scan's). The readings are on while the selected track's SENS
 * is (or a key still presses a track that has it). Every 5 ms each held key of F3 .. C5 gets a pressure, as MONK-1
 * counts it: from where the key rests, not where it landed (a key goes on settling after the note), so the zero
 * follows the driven reading down for KT_SETTLE ms, then creeps after it, and comes back up at once when the key is
 * eased. The lean is the pill's resistance below its rest on a log scale: KT_DEAD of an octave is noise, the span
 * (by the key's track's SENS: 1 0.77 of an octave .. 10 0.15) more is 127; smoothed over ~30 ms. Into seq.c kt_p (the
 * audio ISR: the track's AT, MIDI OUT).
 *
 * The converter is the scan's while it reads the keys: MASTER comes from it (fm1_an.master), the battery from a short
 * pause of the readings once a second (kt_adc_batt). */
#define KT_DBASE 185u            /* the driven reading with no pill resistance: the diode and the 595 */
#define KT_SETTLE 250u           /* ms */
#define KT_DEAD 164              /* 0.04 of an octave (Q12) */
static int32_t kt_base[KT_ROWKEYS];   /* the key's rest (the driven reading - KT_DBASE, Q16) */
static int32_t kt_s[KT_ROWKEYS];      /* the pressure, smoothed (Q15) */
static uint32_t kt_t0[KT_ROWKEYS], kt_ms;
static uint8_t kt_down[KT_ROWKEYS];

/* log2(x) in Q12, x > 0: the exponent, and log2(1 + f) ~ f + 0.343 f (1 - f) (within 0.006 of an octave) */
static int32_t kt_lg12(uint32_t x)
{
    uint32_t n = 31u - (uint32_t)__builtin_clz(x), f;
    f = (n >= 16u ? x >> (n - 16u) : x << (16u - n)) & 0xFFFFu;
    f += (((f * (65536u - f)) >> 16) * 22487u) >> 16;
    return (int32_t)(n << 12) + (int32_t)(f >> 4);
}

/* the span of SENS s (1..10) in Q12 octaves: 0.77 .. 0.15 */
static int32_t kt_span(int32_t s) { return 3154 - (clamp(s, 1, 10) - 1) * 282; }

static void kt_set(int on)
{
    uint32_t k;
    if (on == kt_on)
        return;
    for (k = 0; k < KT_ROWKEYS; k++) {
        kt_dn[k] = 0;
        kt_down[k] = 0;
        kt_p[k] = 0;
    }
    kt_seq++;
    if (on) {
        fm1_an.drive = 1;
        fm1_an.probe = 0;
        kt_on = 1;
        fm1_an.con = (uint16_t)FM1_ADC_CON(7, 2);
    } else {
        fm1_an.con = 0;
        kt_on = 0;
    }
}

/* the main loop, every pass: the readings on or off, and every 5 ms the pressures */
static void kt_poll(uint32_t now)
{
    uint32_t k, want = trk[song.sel].p[P_ATSNS] > 0, ch = 0;
    for (k = 0; k < KT_ROWKEYS && !want; k++)
        want = kt_p[k] != 0;
    kt_set((int)want);
    if (!kt_on || now - kt_ms < 5u)
        return;
    kt_ms = now;
    for (k = 0; k < KT_ROWKEYS; k++) {
        uint32_t t = kb_trk[k] % NTRK, d;
        int32_t s = trk[t].p[P_ATSNS], d16, lean, x, p;
        if (!((fm1_in.notes >> k) & 1u) || !kt_dn[k] || !kb_chn[k] || !s) {
            if (!((fm1_in.notes >> k) & 1u))
                kt_dn[k] = 0;
            kt_down[k] = 0;
            kt_s[k] = 0;
            if (kt_p[k]) {
                kt_p[k] = 0;
                ch = 1;
            }
            continue;
        }
        d = kt_d[k];
        d16 = (int32_t)((d > KT_DBASE + 1u ? d - KT_DBASE : 1u) << 16);
        if (!kt_down[k]) {
            kt_down[k] = 1;
            kt_base[k] = d16;
            kt_t0[k] = now;
        }
        if (d16 > kt_base[k])
            kt_base[k] = d16;                            /* eased off: that is the rest now */
        else
            kt_base[k] += (d16 - kt_base[k]) >> (now - kt_t0[k] < KT_SETTLE ? 2 : 10);
        lean = kt_lg12((uint32_t)kt_base[k]) - kt_lg12((uint32_t)d16);
        x = clamp((lean - KT_DEAD) * 32768 / kt_span(s), 0, 32767);
        kt_s[k] += ((x - kt_s[k]) * 11) >> 6;
        p = (kt_s[k] * 127 + 16384) >> 15;
        if (p != kt_p[k]) {
            kt_p[k] = (uint8_t)p;
            ch = 1;
        }
    }
    if (ch)
        kt_seq++;
}

/* MASTER: from the scan while it reads the keys */
static int32_t kt_adc_master(void) { return kt_on ? (int32_t)fm1_an.master : fm1_adc_read(FM1_ADC_MASTER); }

/* the battery (main.c, every pass): with the readings on, once a second, the scan paused for the one conversion */
static int32_t kt_adc_batt(uint32_t now)
{
    static uint32_t last;
    uint16_t con = fm1_an.con;
    int32_t b;
    uint32_t t0;
    if (!con)
        return fm1_adc_read(FM1_ADC_BATT);
    if (now - last < 1000u)
        return -1;
    last = now;
    fm1_an.con = 0;                                      /* (the tick starts none now; the one it started: taken) */
    for (t0 = fm1_ticks(); fm1_an.pend && fm1_ticks() - t0 < 24000u;)
        ;
    b = fm1_adc_read(FM1_ADC_BATT);
    fm1_an.con = con;
    return b;
}
