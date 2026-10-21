/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* 303: a TB-303 bass voice, Open303 (Robin Schmidt, MIT) with the Devilfish ranges and the SOFT / RAT drive of
 * schwung-303 (Charles Vestal, GPL-3.0; the RAT after dm-Rat, Dave Mollen), as X0X ported it to the FM-1 (X0X,
 * github.com/charlesvestal/fm1-x0x at 54194c5, GPL-3.0-only): x0x/ holds its files as they are there (bass303.c, .h,
 * fastmath.h, x0x_param.h; tests/t303_test.c checks their hashes); this file is the Felucca engine around them.
 *
 * Felucca's only float code: the whole unit is built for the FPU (tools/build.py -mcpu=r3 -mfprev1, which changes no
 * integer instruction), and nothing but this engine may use float (build.py float_check): the floats live in the
 * general registers, which the ISR entry saves (hal/fm1_isr.S), and run in the audio ISR and the second core's job
 * only. -ffp-contract=off (build.py, and the pragma below for the host tests and the emulator): no fused multiply-add,
 * so a host render is the device's, sample for sample.
 *
 * EDIT 1 (303): CUT RES ENV (env mod) DCY (decay); EDIT 2 (DRIVE): ACC (accent) WAVE (SAW SQR) DRV DTYP (SOFT RAT; DRV 0
 *   bypasses the drive); HOME KNOB 1..4 CUT RES ENV DCY, as a 303's. The track's GLIDE is the SLIDE time (2 .. 360 ms,
 *   0: the stock 60 ms); TUNE, VOLUME and ACC DEC stay at X0X's stock values (the track's LEVEL, MENU's TUNE).
 * Notes: one voice (.poly), so POLY retriggers it as MONO does. A step's SLIDE (seq.c slide_glide) and, in LEGATO,
 *   overlapping keys slide (no retrigger, the 303's pitch slew); an ACC step (velocity 127), or a note over 110 (its
 *   own: voice_t.mvel), accents. The 303's envelopes are the voice's amplitude (.ownenv): ATK .. REL do nothing; LFO -> AMP, the matrix and
 *   the fades do. ENV -> FLT / PIT have nothing to follow (its envelope is the 303's); LFO -> FLT, LFO -> PIT, the
 *   bend, TUNE and the matrix's CUT / PITCH move the cutoff and the pitch (one block late; 0: the 303's exact path).
 * State: the part's engine memory (t303_t): the core keeps running there every block, sounding or not (.block, as X0X
 *   runs its 303s: the filters stay warm for 500 ms, then it idles, BASS303_IDLE_HOLD); the voice adds its block. The
 *   overload guard (audio.c shed_voice) first renders the 303s without oversampling (t303_lite), then sheds voices. */
#pragma STDC FP_CONTRACT OFF
#define blep b303_blep          /* (static names of x0x/bass303.c that Felucca's files use for other things) */
#define run b303_run
#define hb b303_hb
#define params b303_params
#define powers b303_powers
#define recip b303_recip
#include "x0x/bass303.c"
#undef blep
#undef run
#undef hb
#undef params
#undef powers
#undef recip

#define T303_MAGIC 0x33303301u
#define T303_GAIN 100800.0f      /* the core's output (VOLUME 96: peaks near 0.5) x 4.2 VOICE_FS: an undriven line's peaks
                                  * as ANALOG ACID's (its crest ~25 dB: the resonance) */
#define T303_PEAK 131071.0f      /* (t303_render: >> 1 before the amplitude, Q15) */
/* DRV's level, made up (X0X makes it up in its mixer): the core's gain x this, by DTYP (SOFT, RAT) and DRV 0, 8 .. 128
 * (linear between). SOFT: the undriven level (its saturation falls ~6 dB to DRV 127); RAT (+13 .. +19 dB from DRV 8): an
 * ANALOG ACID's from DRV 16, half way at 8. Measured: tests/t303_test.c level (the ACID pattern's RMS) */
static const float T303_COMP[2][17] = {
    {1.0000f, 1.0253f, 1.0296f, 1.0369f, 1.0473f, 1.0626f, 1.0831f, 1.1128f, 1.1496f, 1.1987f, 1.2586f, 1.3321f,
     1.4202f, 1.5272f, 1.6516f, 1.7936f, 1.9415f},
    {1.0000f, 0.3163f, 0.3351f, 0.2986f, 0.2780f, 0.2647f, 0.2553f, 0.2482f, 0.2427f, 0.2382f, 0.2346f, 0.2314f,
     0.2287f, 0.2264f, 0.2243f, 0.2225f, 0.2212f},
};
typedef struct {
    uint32_t magic;              /* T303_MAGIC once the core is set up (eng_mem_claim zeroes the region) */
    uint8_t pot[8];              /* the E values the core was given (0xFF: none yet) */
    uint8_t slide;               /* its SLIDE */
    uint8_t note, gate;          /* the note it plays, held */
    uint8_t pend, pnote, pacc, pslide;   /* a note_on for the next block: note, accent, slide */
    int32_t cut;                 /* the cutoff it was given, CUT << 8 + modulation */
    int32_t cm, pm, pa;          /* the voice's cutoff (1/256 of a CUT step) and pitch modulation (1/256 semitone),
                                  * the pitch modulation applied */
    int32_t peak;                /* |sample| at most in this block (t303_done) */
    float gain;                  /* the core's output -> buf: T303_GAIN x DRV's make-up (t303_gain) */
    int32_t buf[CTL];            /* this block, at VOICE_FS */
    float fb[CTL];
    bass303_t b;
} t303_t;
ENG_MEM_FITS("303", sizeof(t303_t));
#define T303(t) ((t303_t *)ENG_MEM((uint32_t)((t) - trk) % NPART))

/* the overload guard's lite mode per part: written by the audio ISR between halves (audio.c), read by the block */
static uint8_t t303_lite[NPART];

static const char *const N_303_WAVE[] = {"SAW", "SQR"};
static const char *const N_303_DTYP[] = {"SOFT", "RAT"};
/* EDIT k -> the core's pot (CUT: t303_cut, with the modulation) */
static const uint8_t T303_POT[8] = {BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY,
                                    BASS303_ACCENT, BASS303_WAVE, BASS303_DRIVE, BASS303_DRVTYPE};

static void t303_note_on(track_t *t, voice_t *v)
{
    t303_t *s = T303(t);
    s->pend = 1;
    s->pnote = v->note;
    s->pacc = v->mvel > 110;                            /* (the note's own velocity: v->vel keeps a legato's highest) */
    s->pslide = t->slide_glide && s->gate;              /* a SLIDE step's next note while the last one holds */
    v->env_out = 32767;                                 /* no ramp from 0 (voice.c): the 303's attack is its own */
}

/* the core's pitch: note + pm / 256 semitones (pm 0: bass303_note_on's own frequency, exactly) */
static void t303_pitch(bass303_t *b, uint32_t note, int32_t pm)
{
    b->osc_freq = b->tuning * fm_exp2f(((float)((int32_t)note - 69) + (float)pm * (1.0f / 256.0f)) * (1.0f / 12.0f));
    if (b->trig)                                        /* a trigger jumps there (bass303_note_on) */
        b->slew_y = b->osc_freq;
}

/* the cutoff, q = CUT << 8 + its modulation (whole steps: bass303_set's path, exactly) */
static void t303_cut(bass303_t *b, int32_t q)
{
    if (!(q & 255)) {
        bass303_set(b, BASS303_CUTOFF, q >> 8);
        return;
    }
    b->cutoff = 314.0f * fm_exp2f((float)q * (2.930586688f / (127.0f * 256.0f)));   /* (bass303_set's mapping) */
    calc_envmod(b);
}

/* the core's gain: T303_GAIN x DRV's make-up of DTYP (pots: DRV 0..127, DRVTYPE 1 SOFT 2 RAT) */
static float t303_gain(uint32_t drv, uint32_t type)
{
    const float *c = T303_COMP[type == 2u];
    uint32_t i = drv >> 3;
    float f = (float)(drv & 7u) * (1.0f / 8.0f);
    return T303_GAIN * (c[i] + (c[i < 16u ? i + 1u : 16u] - c[i]) * f);
}

/* once a block (voice.c track_render, before the voice): the parameters, the notes, the core's block */
static void t303_block(track_t *t)
{
    t303_t *s = T303(t);
    bass303_t *b = &s->b;
    const voice_t *v = &t->v[0];
    uint32_t k, on = v->active && v->gate, renote = 0;
    int32_t x, pk = 0;
    if (s->magic != T303_MAGIC) {                       /* a fresh region: the core as at power-on */
        bass303_init(b);
        for (k = 0; k < 8u; k++)
            s->pot[k] = 0xFF;
        s->slide = 0xFF;
        s->cut = -1;
        s->magic = T303_MAGIC;
    }
    for (k = 1; k < 8u; k++) {
        x = clamp(t->p[P_E0 + k], 0, k == 5u || k == 7u ? 1 : 127) + (k == 7u);   /* DTYP: SOFT RAT */
        if ((uint32_t)x != s->pot[k]) {
            s->pot[k] = (uint8_t)x;
            bass303_set(b, T303_POT[k], x);
            if (k >= 6u)
                s->gain = t303_gain(s->pot[6], s->pot[7]);
        }
    }
    x = t->p[P_GLIDE] > 0 ? clamp(t->p[P_GLIDE], 1, 127) : 21;   /* GLIDE 0: the stock slide, 60 ms */
    if ((uint32_t)x != s->slide) {
        s->slide = (uint8_t)x;
        bass303_set(b, BASS303_SLIDE, x);
    }
    x = clamp((clamp(t->p[P_E0], 0, 127) << 8) + s->cm, 0, 127 << 8);
    if (x != s->cut) {
        s->cut = x;
        t303_cut(b, x);
    }
    bass303_set_lite(b, t303_lite[(uint32_t)(t - trk) % NPART]);
    if (s->pend) {
        s->pend = 0;
        bass303_note_on(b, s->pnote, s->pacc, s->pslide);
        s->note = s->pnote;
        s->gate = 1;
        renote = 1;
    } else if (s->gate && on && v->note != s->note) {   /* LEGATO: a new pitch, held over: it slides */
        bass303_note_on(b, v->note, v->mvel > 110, 1);
        s->note = v->note;
        renote = 1;
    }
    if (s->gate && !on) {
        bass303_note_off(b);
        s->gate = 0;
    }
    if (s->pm != s->pa || (renote && s->pm)) {          /* the voice's pitch modulation (0 again: exactly the note) */
        s->pa = s->pm;
        t303_pitch(b, s->note, s->pm);
    }
    bass303_render(b, s->fb, CTL);
    for (k = 0; k < CTL; k++) {
        x = (int32_t)fm_clampf(s->fb[k] * s->gain, -T303_PEAK, T303_PEAK);
        s->buf[k] = x;
        x = x < 0 ? -x : x;
        if (x > pk)
            pk = x;
    }
    s->peak = pk;
}

static void t303_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    t303_t *s = T303(t);
    uint32_t i;
    /* for the next block: the pitch over the note (glide aside: the 303 slides itself), in 1/256 semitone (fine:
     * 1/4096 of the increment, ~0.0042 semitone), and the cutoff's */
    s->pm = (m->pitch16 - v->pitch_cur) * 16 + m->fine * 277 / 256;
    s->cm = m->cutoff;
    for (i = 0; i < n && i < CTL; i++)
        out[i] += ((s->buf[i] >> 1) * amp_at(m, i)) >> 14;
}

/* the voice ends once the 303 is let go and silent: its amplitude under -60 dB (the release: 1 ms, accented 50 ms, so
 * ~7 / ~350 ms), the block under -70 dB of VOICE_FS (the de-clicker, the drive's tail); the core runs on (t303_block) */
static int t303_done(track_t *t, voice_t *v)
{
    const t303_t *s = T303(t);
    return !v->gate && !s->pend && !s->gate && s->peak < 8 && s->b.amp_y < 1e-3f;
}

/* the overload guard (audio.c shed_voice): the next sounding 303 without oversampling, 1 = one was; t303_calm: all
 * back to 2x (a few seconds under the guard's load) */
static __attribute__((noinline)) int t303_shed(void)
{
    uint32_t p;
    for (p = 0; p < NPART; p++)
        if (trk[p].engine == ENGI_303 && trk[p].v[0].active && !t303_lite[p]) {
            t303_lite[p] = 1;
            return 1;
        }
    return 0;
}
static __attribute__((noinline)) void t303_calm(void)
{
    uint32_t p;
    for (p = 0; p < NPART; p++)
        t303_lite[p] = 0;
}

#include "t303_presets.h"        /* T303_PRESETS */

static const engine_t ENG_303 = {
    .name = "303",
    .page_title = {"303", "DRIVE"},
    .edit = {
        {"CUT", F_PCT, 0, 127, 48, 0, 0},
        {"RES", F_PCT, 0, 127, 100, 0, 0},
        {"ENV", F_PCT, 0, 127, 85, 0, 0},
        {"DCY", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 100, 0, 0},
        {"WAVE", F_ENUM, 0, 1, 0, N_303_WAVE, 0},
        {"DRV", F_PCT, 0, 127, 45, 0, 0},
        {"DTYP", F_ENUM, 0, 1, 1, N_303_DTYP, 0},
    },
    .presets = T303_PRESETS,
    .npresets = NELEM(T303_PRESETS),
    .note_on = t303_note_on,
    .render = t303_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = 1,
    .block = t303_block,
    .ownenv = 1,
    .done = t303_done,
    .mem = sizeof(t303_t),
};
