/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* 303 engine test (firmware/src/eng_303.c, X0X's x0x/bass303.c) on the Mac, through hostsim.c as regress.c.
 *   build/host/t303_test [DEMODIR]          (run_tests.sh: build/t303_demo)
 * 1. the X0X files in x0x/ are X0X's (their hashes: changes to them belong in X0X, a new pin here).
 * 2. X0X's sound: the engine's block, sample for sample, is X0X's bass303 driven directly with the same pots and notes:
 *    notes, an accent, a SLIDE step (MONO: the sequencer's slide) and overlapping keys (LEGATO), a release, a note after
 *    the idle hold-off; the same with slide off differs (the slides reach the core).
 * 3. pitch: a saw through an open filter, A1 .. A4 (55 .. 440 Hz): the rising zero crossings within 0.5 %; the bend
 *    (+2 semitones) moves it, back to 0 exactly the note again.
 * 4. an accent: louder than the same note without.
 * 5. bounded: the corners (CUT RES ENV ACC DRV 0 / 127, SOFT and RAT, SAW and SQR), C1 .. C6: no sample at the clamp
 *    (131071) for a preset, every corner below 5 x VOICE_FS.
 * 6. the voice ends after the release (the 303 quiet), the core idles after its hold-off (cheap).
 * 7. lite (the overload guard, audio.c / t303_shed): a sounding 303 first, one a time, none when silent; its level
 *    within 1.5 dB of 2x; t303_calm: all back.
 * 8. level: the ACID pattern's RMS: the ACID preset (RAT) within 2 dB of ANALOG's ACID, CLASSIC (no drive) within 9 dB;
 *    DRV's make-up (eng_303.c T303_COMP): SOFT 0 .. 127 and RAT 16 .. 127 each within 1.5 dB.
 * 9. determinism: the same state renders the same samples again (every preset).
 * 10. demos into DEMODIR: every preset on the ACID pattern; ACID on X0X's default TB-3PO line. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#define T303_H0 0x8e0d09056030dc86ull   /* bass303.c */
#define T303_H1 0xdd315b17887f07b8ull   /* bass303.h */
#define T303_H2 0x88961d087d64ec80ull   /* fastmath.h */
#define T303_H3 0x5ed5ca4f6a1c939full   /* x0x_param.h */

static int bad;
static void check(const char *what, int ok)
{
    printf("t303: %-88s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint64_t fnv_file(const char *path)          /* FNV-1a 64 of a file's bytes */
{
    FILE *f = fopen(path, "rb");
    uint64_t h = 1469598103934665603ull;
    int c;
    if (!f)
        return 0;
    while ((c = fgetc(f)) != EOF)
        h = (h ^ (uint8_t)c) * 1099511628211ull;
    fclose(f);
    return h;
}
static void vendored(void)
{
    static const struct { const char *f; uint64_t h; } V[] = {
        {"firmware/src/x0x/bass303.c", 0}, {"firmware/src/x0x/bass303.h", 0},
        {"firmware/src/x0x/fastmath.h", 0}, {"firmware/src/x0x/x0x_param.h", 0},
    };
    static const uint64_t H[4] = {T303_H0, T303_H1, T303_H2, T303_H3};
    uint32_t i, ok = 1;
    for (i = 0; i < 4u; i++) {
        uint64_t h = fnv_file(V[i].f);
        if (h != H[i])
            printf("  %s: %016llx\n", V[i].f, (unsigned long long)h);
        ok &= h == H[i];
    }
    check("x0x/ is X0X's at 54194c5 (bass303.c .h, fastmath.h, x0x_param.h: FNV-1a 64)", ok);
}

/* track 0: the 303, preset pi (or e[] when given), no LFO / mod, no sends, mode */
static void setup(uint32_t pi, const int16_t *e, uint32_t mode)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(t303_lite, 0, sizeof t303_lite);
    eng_mem_owner[0] = 0;                           /* (a fresh core: the region zeroed again) */
    host_tracks_init();
    host_preset(&trk[0], ENGI_303, pi);
    if (e)
        for (i = 0; i < 8u; i++)
            trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_VOICE] = (int16_t)mode;
    trk[0].p[P_LD_PIT] = trk[0].p[P_LD_FLT] = trk[0].p[P_LD_SHP] = trk[0].p[P_LD_AMP] = 0;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}
static int32_t blk[CTL];
static void block(void) { track_render(&trk[0], blk, CTL); }

/* ---------------------------------------------- 2. X0X's sound --- */
/* X0X's core driven as eng_303.c drives it: init, the pots (E1..E7, SLIDE, CUT), then per block the note events */
static bass303_t ref;
static void ref_setup(const track_t *t)
{
    uint32_t k;
    bass303_init(&ref);
    for (k = 1; k < 8u; k++)
        bass303_set(&ref, T303_POT[k], clamp(t->p[P_E0 + k], 0, k == 5u || k == 7u ? 1 : 127) + (k == 7u));
    bass303_set(&ref, BASS303_SLIDE, t->p[P_GLIDE] > 0 ? t->p[P_GLIDE] : 21);
    bass303_set(&ref, BASS303_CUTOFF, t->p[P_E0]);
}
static int ref_block(uint32_t n, int32_t *diff)   /* the engine's block against X0X's next one, sample for sample */
{
    static float fb[CTL];
    uint32_t k;
    int same = 1;
    block();
    bass303_render(&ref, fb, CTL);
    for (k = 0; k < CTL; k++) {
        int32_t x = (int32_t)fm_clampf(fb[k] * T303(&trk[0])->gain, -T303_PEAK, T303_PEAK);
        if (x != T303(&trk[0])->buf[k]) {
            same = 0;
            if (diff && !*diff)
                *diff = (int32_t)n + 1;
        }
    }
    return same;
}
static int x0x_line(uint32_t mode, int slides)
{
    int same = 1;
    int32_t d = 0;
    uint32_t b, k;
    setup(0, 0, mode);
    ref_setup(&trk[0]);
    for (k = 0; k < 6u; k++, same &= ref_block(k, &d))
        ;                                            /* (silence first: the core not started) */
    trk_note_on(&trk[0], 45, 96);                   /* A1 */
    bass303_note_on(&ref, 45, 0, 0);
    for (b = 0; b < 200u; b++)
        same &= ref_block(b, &d);
    trk[0].slide_glide = (uint8_t)slides;           /* (the sequencer: a SLIDE step's next note while it holds) */
    trk_note_on(&trk[0], 57, 127);                  /* A2, accented, sliding (MONO) / held over (LEGATO) */
    trk_note_off(&trk[0], 45);
    bass303_note_on(&ref, 57, 1, mode == V_LEGATO || slides);
    for (b = 0; b < 300u; b++)
        same &= ref_block(b, &d);
    trk[0].slide_glide = 0;
    trk_note_off(&trk[0], 57);
    bass303_note_off(&ref);
    for (b = 0; b < 2000u; b++)                     /* the release, past the idle hold-off (500 ms) */
        same &= ref_block(b, &d);
    trk_note_on(&trk[0], 52, 100);                  /* from idle: a cold start, as X0X */
    bass303_note_on(&ref, 52, 0, 0);
    for (b = 0; b < 300u; b++)
        same &= ref_block(b, &d);
    if (!same)
        printf("  mode %u slides %d: first block off %d\n", mode, slides, (int)d);
    return same;
}
static void x0x_sound(void)
{
    int a = x0x_line(V_MONO, 1), l = x0x_line(V_LEGATO, 0), off;
    check("the block == X0X's bass303 sample for sample: notes, an accent, MONO's SLIDE step, a release, a cold note", a);
    check("  LEGATO: overlapping keys slide, the same", l);
    off = x0x_line(V_MONO, 0);                      /* (MONO, no slide: X0X with a slide would differ) */
    {
        int32_t d = 0;
        uint32_t b, same = 1;
        setup(0, 0, V_MONO);
        ref_setup(&trk[0]);
        trk_note_on(&trk[0], 45, 96);
        bass303_note_on(&ref, 45, 0, 0);
        for (b = 0; b < 100u; b++)
            ref_block(b, &d);
        trk_note_on(&trk[0], 57, 96);               /* a retrigger ... */
        trk_note_off(&trk[0], 45);
        bass303_note_on(&ref, 57, 0, 1);            /* ... against a slide */
        for (b = 0; b < 100u; b++)
            same &= ref_block(b, &d);
        check("  without SLIDE MONO retriggers (X0X's slide differs: the slides reach the core)", off && !same);
    }
}

/* --------------------------------------------------------- 3. pitch --- */
static double Y[4 * FS];
static void voice(uint32_t note, uint32_t vel, double *y, uint32_t n)
{
    uint32_t i, k;
    trk_note_on(&trk[0], note, vel);
    for (i = 0; i < FS / 10u / CTL; i++)
        block();
    for (i = 0; i < n; i += CTL) {
        block();
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = blk[k] / 32768.0;
    }
}
static double crossing_hz(const double *y, uint32_t n)
{
    uint32_t i, c = 0, first = 0, last = 0;
    for (i = 1; i < n; i++)
        if (y[i - 1] < 0.0 && y[i] >= 0.0) {
            if (!c)
                first = i;
            last = i;
            c++;
        }
    return c > 1u ? (double)(c - 1u) * FS / (double)(last - first) : 0.0;
}
static double rms(const double *y, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += y[i] * y[i];
    return sqrt(s / n);
}
static void pitch(void)
{
    static const int16_t E[8] = {127, 0, 0, 64, 0, 0, 0, 0};   /* open, no resonance, no env mod; SAW, no drive */
    uint32_t note, ok = 1;
    double hz, want, ok2;
    for (note = 33; note <= 69; note += 12) {
        setup(0, E, V_MONO);
        voice(note, 96, Y, FS);
        hz = crossing_hz(Y, FS);
        want = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
        if (fabs(hz / want - 1.0) > 0.005)
            printf("  note %u: %.2f Hz, want %.2f\n", note, hz, want);
        ok &= fabs(hz / want - 1.0) <= 0.005;
    }
    check("pitch: A1 .. A4 (55 .. 440 Hz) within 0.5 % (SAW, the filter open)", ok);
    setup(0, E, V_MONO);
    voice(45, 96, Y, FS / 2);
    midi_bend_target[0] = midi_bend_q8[0] = 2 * 256;   /* +2 semitones */
    for (note = 0; note < 200u; note++)
        block();
    voice(45, 96, Y, FS);                            /* (held: a legato retrigger of the same note does nothing) */
    hz = crossing_hz(Y, FS);
    ok2 = fabs(hz / (110.0 * pow(2.0, 2.0 / 12.0)) - 1.0) <= 0.01;
    midi_bend_target[0] = midi_bend_q8[0] = 0;
    for (note = 0; note < 200u; note++)
        block();
    ok2 = ok2 && T303(&trk[0])->pa == 0 &&
          T303(&trk[0])->b.osc_freq == T303(&trk[0])->b.tuning * fm_exp2f((float)(45 - 69) * (1.0f / 12.0f));
    check("  the bend: +2 semitones moves it (within 1 %), back at 0 the note's frequency exactly", ok2);
}

/* ------------------------------------------------- 4, 5, 6, 7, 8 --- */
static void accent(void)
{
    double a, n;
    uint32_t i, k;
    int acc[2];
    for (k = 0; k < 2u; k++) {                      /* the first 80 ms (an accent's sweep and boost) */
        setup(1, 0, V_MONO);                        /* (CLASSIC: no drive, which would even the levels out) */
        trk_note_on(&trk[0], 45, k ? 127 : 96);
        block();
        acc[k] = T303(&trk[0])->b.amp_acc;
        for (i = 0; i < FS * 8u / 100u; i += CTL) {
            block();
            for (uint32_t j = 0; j < CTL; j++)
                Y[i + j] = blk[j] / 32768.0;
        }
        if (k)
            a = rms(Y, FS * 8u / 100u);
        else
            n = rms(Y, FS * 8u / 100u);
    }
    printf("t303: accent: %.1f dB over the plain note\n", 20.0 * log10(a / n));
    check("an accent (velocity over 110) reaches the core, louder than the same note without (CLASSIC)",
          acc[1] && !acc[0] && a > n * 1.2);
}
static void bounded(void)
{
    static const int16_t C[4][8] = {{0, 0, 0, 0, 0, 0, 0, 0}, {127, 127, 127, 127, 127, 1, 127, 1},
                                    {127, 127, 127, 0, 127, 0, 127, 0}, {0, 127, 127, 127, 127, 1, 0, 1}};
    uint32_t c, pi, note, k, b, pre = 1;
    int32_t pk = 0, x;
    for (pi = 0; pi < ENG_303.npresets; pi++)
        for (note = 24; note <= 84; note += 12) {
            setup(pi, 0, V_MONO);
            trk_note_on(&trk[0], note, 127);
            for (b = 0; b < 300u; b++) {
                block();
                for (k = 0; k < CTL; k++)
                    pre &= T303(&trk[0])->buf[k] > -131071 && T303(&trk[0])->buf[k] < 131071;
            }
        }
    for (c = 0; c < 4u; c++)
        for (note = 24; note <= 84; note += 12) {
            setup(0, C[c], V_MONO);
            trk_note_on(&trk[0], note, 127);
            for (b = 0; b < 300u; b++) {
                block();
                for (k = 0; k < CTL; k++) {
                    x = blk[k] < 0 ? -blk[k] : blk[k];
                    pk = x > pk ? x : pk;
                }
            }
        }
    printf("t303: the corners' peak %d (%.2f x VOICE_FS)\n", (int)pk, pk / (double)VOICE_FS);
    check("bounded: every preset C1 .. C6 accented below the clamp; the corners below 5 x VOICE_FS", pre && pk < 5 * VOICE_FS);
}
static void ends(void)
{
    uint32_t b, on;
    setup(0, 0, V_MONO);
    trk_note_on(&trk[0], 45, 127);
    for (b = 0; b < 100u; b++)
        block();
    trk_note_off(&trk[0], 45);
    for (b = 0; b < FS * 6u / 10u / CTL; b++)
        block();
    on = trk[0].v[0].active;
    for (b = 0; b < FS / CTL; b++)
        block();
    check("the voice ends after an accented note's release (600 ms); the core idles after its hold-off",
          !on && T303(&trk[0])->b.idle);
}
static void lite(void)
{
    double a, l;
    int ok;
    setup(0, 0, V_MONO);
    ok = !t303_shed();                              /* (nothing sounding) */
    voice(45, 96, Y, FS / 2);
    a = rms(Y, FS / 2);
    ok &= t303_shed() && t303_lite[0] && !t303_shed();
    trk_note_off(&trk[0], 45);
    setup(0, 0, V_MONO);
    t303_lite[0] = 1;
    voice(45, 96, Y, FS / 2);
    l = rms(Y, FS / 2);
    ok &= T303(&trk[0])->b.lite_cur == 1;
    t303_calm();
    ok &= !t303_lite[0];
    printf("t303: lite: %.2f dB against 2x\n", 20.0 * log10(l / a));
    check("lite: the sounding 303 first (one a time, none silent), within 1.5 dB of 2x; calm: back to 2x",
          ok && fabs(20.0 * log10(l / a)) < 1.5);
}
static double pattern_rms_d(uint32_t e, uint32_t pi, int drv, int ty);
static double pattern_rms(uint32_t e, uint32_t pi) { return pattern_rms_d(e, pi, -1, 0); }
static double pattern_rms_d(uint32_t e, uint32_t pi, int drv, int ty)
{
    static int32_t o[2 * CTL];
    uint32_t s, i, j, held = 0, step = FS / 8u, n = 0;
    double sum = 0;
    memset(trk, 0, sizeof trk);
    eng_mem_owner[0] = 0;
    host_tracks_init();
    host_preset(&trk[0], e, pi);
    if (drv >= 0) {
        trk[0].p[P_E6] = (int16_t)drv;
        trk[0].p[P_E7] = (int16_t)ty;
    }
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    for (s = 0; s < 32u; s++) {
        uint32_t k = s % 16u, note = PATTERNS[0].note[k], fl = PATTERNS[0].flags[k];
        trk[0].slide_glide = (uint8_t)(held && (PATTERNS[0].flags[(k + 15u) % 16u] & 2u));
        if (note) {
            trk_note_on(&trk[0], note, fl & 1u ? 127 : 96);
            if (held && held != note)
                trk_note_off(&trk[0], held);
            held = note;
        }
        for (i = 0; i < step; i += CTL) {
            if (i == step / 2u && held && !(fl & 2u)) {
                trk_note_off(&trk[0], held);
                held = 0;
            }
            mix_block(o, CTL);
            for (j = 0; j < CTL; j++, n++)
                sum += (double)o[2 * j] * o[2 * j];
        }
    }
    return sqrt(sum / n);
}
static void level(void)
{
    double a = pattern_rms(ENGI_303, 0), b = pattern_rms(0, 4), c = pattern_rms(ENGI_303, 1), d = 20.0 * log10(a / b);
    double lo[2] = {1e9, 1e9}, hi[2] = {0, 0}, x;
    int ty, dv;
    printf("t303: level: 303 ACID %.0f (%.1f dB), CLASSIC %.0f (%.1f dB) against ANALOG ACID %.0f RMS\n", a, d, c,
           20.0 * log10(c / b), b);
    check("level: the 303's ACID (RAT) within 2 dB of ANALOG's ACID, CLASSIC (no drive) within 9 dB",
          fabs(d) < 2.0 && fabs(20.0 * log10(c / b)) < 9.0);
    for (ty = 0; ty < 2; ty++)
        for (dv = ty ? 16 : 0; dv <= 127; dv += 9) {
            x = pattern_rms_d(ENGI_303, 1, dv, ty);
            lo[ty] = x < lo[ty] ? x : lo[ty];
            hi[ty] = x > hi[ty] ? x : hi[ty];
        }
    printf("t303: DRV's make-up: SOFT %.1f dB, RAT %.1f dB from the quietest to the loudest\n",
           20.0 * log10(hi[0] / lo[0]), 20.0 * log10(hi[1] / lo[1]));
    check("  DRV's make-up: SOFT 0 .. 127 and RAT 16 .. 127 each within 1.5 dB",
          20.0 * log10(hi[0] / lo[0]) < 1.5 && 20.0 * log10(hi[1] / lo[1]) < 1.5);
}
static void determinism(void)
{
    static int32_t a[FS / 2];
    uint32_t pi, i, k, pass, same = 1;
    for (pi = 0; pi < ENG_303.npresets; pi++)
        for (pass = 0; pass < 2u; pass++) {
            setup(pi, 0, V_LEGATO);
            trk_note_on(&trk[0], 40, 127);
            for (i = 0; i < FS / 2 / CTL; i++) {
                block();
                for (k = 0; k < CTL; k++) {
                    if (pass)
                        same &= a[i * CTL + k] == blk[k];
                    a[i * CTL + k] = blk[k];
                }
            }
        }
    check("the same state renders the same samples again (every preset)", same);
}

/* --------------------------------------------------------- demos --- */
static void demo_line(const char *path, uint32_t pi, int tb3)
{
    static int32_t o[2 * CTL];
    uint32_t i, j, frames = 0, r = 0;
    FILE *w = fopen(path, "wb");
    if (!w)
        return;
    wav_hdr(w, 0);
    memset(trk, 0, sizeof trk);
    eng_mem_owner[0] = 0;
    host_tracks_init();
    host_preset(&trk[0], ENGI_303, pi);
    if (tb3) {
        trk[0].p[P_SLEN] = 16;
        tb3_apply(&trk[0], 1, &r);
    } else {
        for (i = 0; i < 16u; i++) {                  /* ACID (as ui.c load_pat16) */
            step_t *st = &trk[0].step[i];
            uint32_t n = PATTERNS[0].note[i], fl = PATTERNS[0].flags[i];
            st->note[0] = (uint8_t)n;
            st->n = n ? 1u : 0u;
            st->time = (fl & 4u) ? ST_TIE : n ? ST_NOTE : ST_REST;
            st->flags = (uint8_t)(n ? fl & (SF_ACCENT | SF_SLIDE) : 0u);
            st->vel = n ? 96u : 0u;
        }
        trk[0].p[P_SLEN] = 16;
    }
    song.g[G_BPM] = 128;
    song.playing = 1;
    seq_start();
    for (i = 0; i < FS * 8u; i += CTL) {
        events_block(CTL);
        mix_block(o, CTL);
        for (j = 0; j < CTL; j++)
            wav_put(w, o[2 * j], o[2 * j + 1]);
        frames += CTL;
    }
    song.playing = 0;
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
}

int main(int argc, char **argv)
{
    uint32_t pi;
    vendored();
    x0x_sound();
    pitch();
    accent();
    bounded();
    ends();
    lite();
    level();
    determinism();
    if (argc > 1) {
        char path[512];
        for (pi = 0; pi < ENG_303.npresets; pi++) {
            snprintf(path, sizeof path, "%s/%02u_%s.wav", argv[1], pi, ENG_303.presets[pi].name);
            for (char *c = path + strlen(argv[1]); *c; c++)
                if (*c == ' ')
                    *c = '_';
            demo_line(path, pi, 0);
        }
        snprintf(path, sizeof path, "%s/tb3po_ACID.wav", argv[1]);
        demo_line(path, 0, 1);
        printf("t303: demos in %s/\n", argv[1]);
    }
    if (bad)
        printf("t303: %d check(s) FAILED\n", bad);
    return bad ? 1 : 0;
}
