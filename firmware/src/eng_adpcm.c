/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ugotworms */
/* ADPCM: short 4-bit ADPCM (BRR) waves through a Gaussian interpolator, stepped ADSR / GAIN envelopes, a noise
 * source and an echo with an 8-tap FIR (adpcm_dsp.c, ported from snes_spc). Off by default: FELUCCA_ADPCM=1 builds
 * it. Each part is one DSP: its voices share the rate counter and the noise, and it has its echo. The DSP runs at
 * 32 kHz; its samples are linearly interpolated to 44.1 kHz.
 * The note sets the 14-bit pitch register (at most two octaves above a wave's root, and coarse in the bass), the
 * velocity the voice volume, ATK DEC SUS the ADSR (AR, DR, SL), SR its sustain rate. REL 0 is the DSP's key-off (a
 * linear fade of 8 ms); above it a key-off switches the voice to GAIN's exponential decrease at that rate, a
 * longer release. The echo: one per part, mono, at most 192 ms (the original's 240 ms does not fit the RAM:
 * adp_eline). The waves and presets: tools/gen_adpcm.py.
 * Not here: pitch modulation, the voices' stereo volumes (a part is mono). */
#include "adpcm_dsp.c"                  /* the DSP (LGPL-2.1-or-later) */
#define ADP_PRESETS                     /* (felucca_adpcm.h: the preset table too) */
#include "felucca_adpcm.h"              /* tools/gen_adpcm.py: the waves and presets */

#define ADP_RATE_Q16 47555u             /* 32000 / 44100, Q16: DSP samples per output sample */
#define ADP_ONE (1u << 16)              /* one DSP sample in that phase, Q16 */
/* the voice volume register: the velocity / 4 (0..32), kept low: the DSP sums the voices into the echo in 16 bits,
 * and 8 voices at full volume clip it hard (a crunch on every chord). Felucca's mix (32 bits) makes the level up
 * after the DSP */
#define ADP_VOL_SH 2

typedef struct {                        /* a voice of the engine: the DSP's, and the key */
    adp_chv_t c;
    uint8_t inst, down;                 /* the wave (ADP_INST) at key-on; the key held */
    int16_t o0, o1;                     /* the last two DSP samples (to 44.1 kHz) */
} adp_voice_t;

#define ADP_TICKS 24u                   /* DSP samples in a block of CTL, at most (32 x 0.7256 = 23.2) */
typedef struct {                        /* a part's DSP: what its voices share */
    int32_t noise;                      /* the noise shift register */
    int16_t rc[32];                     /* the rate counter, as one countdown per rate: 0 = it fires */
    uint32_t ph, ph0;                   /* the 32 kHz clock against the output's, Q16; .. as the block began */
    uint32_t fire[ADP_TICKS];           /* this block's DSP samples: the rates that fire (bit r), */
    int16_t nz[ADP_TICKS];              /* .. the noise; the voices read them in step */
    int16_t ein[ADP_TICKS];             /* .. and add their outputs here: the echo's input */
    adp_echo_t ec;
    int16_t e0, e1;                     /* the echo's last two outputs (to 44.1 kHz) */
    uint32_t blk;                       /* mix_blocks at this part's last block: a gap = another engine ran */
    uint16_t quiet;                     /* DSP samples the echo has stayed flat (its FIR output and what it */
    int16_t qlo[2], qhi[2];             /* writes within 8 of a level, the range in qlo .. qhi) */
    uint8_t init, eown, edirty;         /* the echo line is ours and cleared; it holds sound */
} adp_dsp_t;

static adp_voice_t adp_v[NTRK][NVOICE] __attribute__((section(".pool")));
static adp_dsp_t adp_dsp[NTRK];
static uint32_t adp_eclips;             /* DSP samples the echo's input clipped (16 bits, as the original): tests */

/* the echo line: PHYS's memory of the part (eng_phys.c phys_mem: a part plays one engine at a time), 12.9 KB:
 * up to EDL 12 (192 ms; the original's 15, 240 ms, would need 15 KB a part, more than the RAM has to spare) */
#define ADP_EDL_MAX 12u
_Static_assert(sizeof phys_mem[0].shared >= ADP_EDL_MAX * 512u * 2u, "the ADPCM echo line in PHYS's memory");
static int16_t *adp_eline(uint32_t part) { return phys_mem[part % NPART].shared; }


/* ---------------------------------------------------- the engine --- */
/* FIR settings: none, a low-pass, a high-pass, a band-pass (C0 .. C7) */
static const int8_t ADP_FIRS[4][8] = {
    {127, 0, 0, 0, 0, 0, 0, 0},
    {0x0C, 0x21, 0x2B, 0x2B, 0x13, -0x02, -0x0D, -0x07},
    {0x58, -0x41, -0x25, -0x10, -0x02, 0x07, 0x0C, 0x0C},
    {0x34, 0x33, 0x00, -0x27, -0x1B, 0x01, -0x04, -0x15},
};
static const char *const N_ADP_FIR[] = {"FLAT", "LOW", "HIGH", "BAND"};
static const char *const N_ADP_EDL[] = {"0", "16", "32", "48", "64", "80", "96", "112", "128", "144", "160", "176",
                                         "192"};
_Static_assert(NELEM(N_ADP_EDL) == ADP_EDL_MAX + 1u, "a DELAY name per EDL");

static adp_voice_t *adp_vo(const track_t *t, const voice_t *v)
{
    return &adp_v[(uint32_t)(t - trk) % NTRK][(uint32_t)(v - t->v) % NVOICE];
}

/* ATK DEC SUS (0..127) -> ADSR's AR (15 = at once), DR, SL; SR (P_E2) as it is */
static void adp_adsr(const track_t *t, adp_voice_t *sv)
{
    const int16_t *p = t->p;
    uint32_t ar = 15u - ((uint32_t)p[P_ATK] * 15u + 63u) / 127u, dr = 7u - ((uint32_t)p[P_DEC] * 7u + 63u) / 127u;
    uint32_t sl = ((uint32_t)p[P_SUS] * 7u + 63u) / 127u;
    sv->c.adsr0 = (uint8_t)(0x80u | dr << 4 | ar);
    sv->c.adsr1 = (uint8_t)(sl << 5 | ((uint32_t)p[P_E2] & 31u));
}

/* the pitch register of the note: 0x1000 at the wave's root, 14 bits; the voice's bend / glide / LFO in pitch16 */
static uint32_t adp_pitch(const track_t *t, const adp_voice_t *sv, const vmod_t *m)
{
    int32_t d16 = clamp(m->pitch16 + t->p[P_E1] * 16 - ADP_INST[sv->inst].root16, -3072, 384);
    int32_t pr = (int32_t)(pow2_q16(d16) >> 4);
    if (m->fine)
        pr += (pr * m->fine) >> 12;
    return (uint32_t)clamp(pr, 0, 0x3FFF);
}

static void adp_note_on(track_t *t, voice_t *v)        /* KON */
{
    adp_voice_t *sv = adp_vo(t, v);
    sv->inst = (uint8_t)((uint32_t)t->p[P_E0] % ADP_NINST);
    sv->c.ram = ADP_BRR;
    sv->c.start = ADP_INST[sv->inst].start;             /* (each wave loops whole: its start is its loop) */
    adp_adsr(t, sv);
    sv->c.kon_delay = 5;
    sv->c.env_mode = SE_ATTACK;
    sv->down = 1;
}

/* the part's DSP samples of this block (SPC_DSP misc_30, once for all its voices): the rate counter
 * (one countdown per rate, from SPC_DSP's reset value 0), the rates firing, the noise at NOISE's rate */
static void adp_block(track_t *t)
{
    adp_dsp_t *d = &adp_dsp[(uint32_t)(t - trk) % NTRK];
    uint32_t i, r, k = 0, nrate = (uint32_t)t->p[P_E3] & 31u;
    if (!d->init) {
        for (r = 0; r < 32u; r++)
            d->rc[r] = (int16_t)adp_ctr(0, r);
        d->noise = 0x4000;
        d->init = 1;
    }
    if (d->blk + 1u != mix_blocks)                      /* the part played another engine: PHYS may have */
        d->eown = 0;                                    /* written the echo line (adp_post clears it) */
    d->blk = mix_blocks;
    for (i = 0; i < ADP_TICKS; i++)
        d->ein[i] = 0;
    d->ph0 = d->ph;
    for (i = 0; i < CTL; i++)
        if ((d->ph += ADP_RATE_Q16) >= ADP_ONE) {
            uint32_t m = 0;
            d->ph -= ADP_ONE;
            for (r = 1; r < 32u; r++) {                  /* (rate 0 never fires) */
                d->rc[r] = (int16_t)(d->rc[r] ? d->rc[r] - 1 : ADP_CTR_RATE[r] - 1);
                m |= (uint32_t)!d->rc[r] << r;
            }
            if (m >> nrate & 1u)
                adp_noise_step(&d->noise);
            if (k < ADP_TICKS) {
                d->fire[k] = m;
                d->nz[k++] = (int16_t)d->noise;
            }
        }
}

/* the voice has gone silent for good: its envelope at 0 after the key-on, not rising (voice.c, done) */
static int adp_done(track_t *t, voice_t *v)
{
    const adp_voice_t *sv = adp_vo(t, v);
    return !sv->c.kon_delay && !sv->c.env && (sv->c.env_mode != SE_ATTACK || !sv->down);
}

static void adp_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    adp_voice_t *sv = adp_vo(t, v);
    adp_dsp_t *d = &adp_dsp[(uint32_t)(t - trk) % NTRK];
    int32_t vol = (v->vel + 2) >> ADP_VOL_SH, non = t->p[P_E3] != 0, eon = t->p[P_E4] != 0, koff = 0;
    uint32_t i, k = 0, ph = d->ph0, pitch = adp_pitch(t, sv, m);
    if (n != CTL)                                       /* (adp_block clocked CTL samples) */
        return;
    if (!v->gate && sv->down) {                         /* key-off: the DSP's, or GAIN's decrease at REL */
        int32_t rel = t->p[P_REL];
        sv->down = 0;
        if (!rel)
            koff = 1;
        else {
            sv->c.adsr0 &= 0x7Fu;
            sv->c.gain = (uint8_t)(0xA0 | (31 - ((rel - 1) * 30 + 63) / 126));
        }
    } else if (sv->down) {
        adp_adsr(t, sv);                               /* the knobs move a held note's envelope */
    }
    for (i = 0; i < n; i++) {
        int32_t s;
        if ((ph += ADP_RATE_Q16) >= ADP_ONE) {          /* a DSP sample: the block's k-th */
            ph -= ADP_ONE;
            if (k < ADP_TICKS) {
                sv->o0 = sv->o1;
                sv->o1 = (int16_t)((adp_tick(&sv->c, d->fire[k], d->nz[k], pitch, non, koff) * vol) >> 7);
                if (eon) {                              /* EON: into the echo (SPC_DSP voice_output) */
                    int32_t e = d->ein[k] + sv->o1;
                    adp_eclips += (int16_t)e != e;
                    d->ein[k] = (int16_t)adp_clamp16(e);
                }
                k++;
                koff = 0;
            }
        }
        s = sv->o0 + (((sv->o1 - sv->o0) * (int32_t)(ph >> 1)) >> 15);
        out[i] += voice_amp(s, m, i) << ADP_VOL_SH;     /* (the headroom, made up) */
    }
}

static void adp_echo_clear(adp_dsp_t *d, uint32_t part)
{
    int16_t *b = adp_eline(part);
    uint32_t i;
    for (i = 0; i < ADP_EDL_MAX * 512u; i++)
        b[i] = 0;
    memset(&d->ec, 0, sizeof d->ec);
    d->e0 = d->e1 = 0;
    d->quiet = 0;
    d->edirty = 0;
}

/* after an echo sample: is it still moving? Its FIR output and the sample written are watched; within a range of
 * 8 (and below 1024) they are flat, a window that a real tail leaves within a line (16 ms) even at 50 Hz */
static void adp_echo_flat(adp_dsp_t *d)
{
    int32_t x[2] = {d->ec.in, d->ec.wr}, k, moved = 0;
    if (x[0] || x[1])
        d->edirty = 1;
    for (k = 0; k < 2; k++) {
        if (!d->quiet) {
            d->qlo[k] = d->qhi[k] = (int16_t)x[k];
            continue;
        }
        if (x[k] < d->qlo[k])
            d->qlo[k] = (int16_t)x[k];
        if (x[k] > d->qhi[k])
            d->qhi[k] = (int16_t)x[k];
        moved |= d->qhi[k] - d->qlo[k] > 8 || x[k] >= 1024 || x[k] <= -1024;
    }
    if (moved) {                                        /* a new window from here */
        d->qlo[0] = d->qhi[0] = (int16_t)x[0];
        d->qlo[1] = d->qhi[1] = (int16_t)x[1];
        d->quiet = 1;
    } else if (d->quiet < 0xFFFFu) {
        d->quiet++;
    }
}

/* the part's echo, after its voices: ECHO (EVOL; 0 = off, the line forgotten), DELAY (EDL), FDBK (EFB), FIR; the
 * voices' sum came in ein. 1 while it still sounds */
static uint32_t adp_post(track_t *t, int32_t *out, uint32_t n)
{
    uint32_t part = (uint32_t)(t - trk) % NTRK, i, k = 0, ph, any = 0;
    adp_dsp_t *d = &adp_dsp[part];
    const int16_t *p = t->p;
    int32_t evol = clamp(p[P_E4], 0, 127), efb = clamp(p[P_E6] * 2, -128, 127);
    const int8_t *fir = ADP_FIRS[(uint32_t)p[P_E7] & 3u];
    uint32_t edl = (uint32_t)clamp(p[P_E5], 0, ADP_EDL_MAX);
    int16_t *line = adp_eline(part);
    if (n != CTL)
        return 0;
    if (!d->eown) {                                     /* the line from silence: ours now */
        adp_echo_clear(d, part);
        d->eown = 1;
    }
    if (!evol) {
        if (d->edirty)
            adp_echo_clear(d, part);
        return 0;
    }
    for (i = 0; i < ADP_TICKS; i++)
        any |= (uint32_t)(d->ein[i] != 0);
    if (!any && !d->edirty)                             /* nothing in, nothing in the line: idle */
        return 0;
    for (i = 0, ph = d->ph0; i < n; i++) {
        int32_t s;
        if ((ph += ADP_RATE_Q16) >= ADP_ONE) {
            ph -= ADP_ONE;
            if (k < ADP_TICKS) {
                d->e0 = d->e1;
                d->e1 = (int16_t)adp_echo_tick(&d->ec, line, d->ein[k++], evol, efb, fir, edl, 1);
                adp_echo_flat(d);
            }
        }
        s = d->e0 + (((d->e1 - d->e0) * (int32_t)(ph >> 1)) >> 15);
        out[i] += mulq15(s, VOICE_FS) << ADP_VOL_SH;
    }
    /* flat for a whole line and the FIR's history: done. The echo rounds towards -inf, so with feedback
     * a decayed echo settles on a few units of DC rather than 0; that is let go, the line cleared */
    if (d->edirty && d->quiet > (uint32_t)d->ec.len + 16u)
        adp_echo_clear(d, part);
    return d->edirty;
}


static const engine_t ENG_ADPCM = {
    .name = "ADPCM",
    .page_title = {"VOICE", "ECHO"},
    .edit = {
        {"INST", F_ENUM, 0, ADP_NINST - 1, 0, ADP_INST_NAMES, 0},   /* the wave */
        {"TUNE", F_SEMI, -24, 24, 0, 0, 0},
        {"SR", F_INT, 0, 31, 0, 0, 0},                  /* ADSR's sustain rate: the decay while held, 0 = none */
        {"NOISE", F_INT, 0, 31, 0, 0, 0},               /* 0 = the wave, else the noise at this rate */
        {"ECHO", F_PCT, 0, 127, 0, 0, 0},               /* EVOL; 0 = no echo (EON off) */
        {"DELAY", F_ENUM, 0, ADP_EDL_MAX, 4, N_ADP_EDL, "ms"},   /* EDL */
        {"FDBK", F_BIPCT, -64, 63, 0, 0, 0},            /* EFB / 2 */
        {"FIR", F_ENUM, 0, 3, 1, N_ADP_FIR, 0},
    },
    .presets = ADP_PRESET_TABLE,
    .npresets = NELEM(ADP_PRESET_TABLE),
    .note_on = adp_note_on,
    .render = adp_render,
    .knob = {P_E0, P_E2, P_ATK, P_REL},
    .sampled = 1,
    .block = adp_block,
    .ownenv = 1,
    .done = adp_done,
    .post = adp_post,
};

