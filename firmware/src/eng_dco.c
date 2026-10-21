/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* DCO: a Juno-106 voice, after fm-106 (PatttF, GPL-3.0-only), the port of Ultramaster KR-106 (Kayrock Screenprinting,
 * GPL-3.0) to the FM-1: dco_dsp.c has the models; this file is the Felucca engine around them.
 *
 * EDIT 1 (DCO): WAVE (OFF PLS SAW P+S), PW (the pulse's width, 50 .. 95 %; the track's LFO / ENV -> SHP move it: PWM),
 *   SUB (the square an octave down), NOIS. EDIT 2 (VCF): CUT, RES (it self-oscillates near 127), KEY (key tracking,
 *   around middle C), HPF (BOOST FLAT 236 754). The track's ENV -> FLT and LFO -> FLT move the cutoff twice as far as
 *   on other engines (63 sweeps the whole range, as a Juno's ENV slider at 10), ENV -> PIT, LFO -> PIT the DCO.
 *   RANGE (16' 8' 4') is the track's P_DRNG, on a page of its own (params.c "DCO", ui.c page_visible): the common id
 *   P_ED_FX was, which no engine read; the presets set it (dco_track_loaded), any other sound load puts it back to 8'.
 * The envelope, the VCA and the LFO are the track's (voice.c), so ATK .. REL, the velocity and the LFO pages work as
 *   everywhere; the presets' Juno values are converted to them (tools/fm106_presets.py).
 * Voice state (no engine memory): ph[0] the DCO's phase (the host keeps it on a retrigger and spreads it for UNISON),
 *   ph[1] the sub's flip-flop, ph[2] the HPF's second state; s[0..3] the VCF's stages (Q24), s[4] the HPF's first
 *   state, s[5] the DCO's coupling capacitor, s[6] the output's, s[7] the noise's low-pass. A retrigger keeps them all
 *   (.keep; s[2], s[3]: note_on leaves a sounding voice's alone). The noise: one xorshift per part (dco_nz).
 * Four voices a part (.poly): a voice costs ~7.5..9 % of the device (six held: 57 % with 12 % idle, measured on an FM-1),
 * so six (the instrument's) left too little for the other tracks before voices are shed (audio.c, 85 %). */
#include "dco_dsp.c"

#define DCO_POLY 4
#define DCO_KNEE 16000           /* the voice at 3/8 of the card's level, added << 1 (a saw through an open filter peaks
                                  * near 9000: 0.55 of VOICE_FS; PLS + SAW 13500): bigger sums (+ SUB, the BOOST's bass
                                  * x 3.35) and the resonance's peaks go softly into this knee, 2 x VOICE_FS at most, then
                                  * the VCA's coupling capacitor takes out what that clipping leaves of DC */

#if FELUCCA_CPU2
_Static_assert(NTRK == 4, "dco_nz: a seed per part");
static int32_t dco_nz[NTRK] = {0x3C6EF372, 0x3C6EF372, 0x3C6EF372, 0x3C6EF372};
#define DCO_NZ(t) dco_nz[C1_PI(t)]
#else
static int32_t dco_nz = 0x3C6EF372;                     /* noise, one xorshift for all voices (one core) */
#define DCO_NZ(t) dco_nz
#endif

static const char *const N_DCO_WAVE[] = {"OFF", "PLS", "SAW", "P+S"};
static const char *const N_DCO_HPF[] = {"BOOST", "FLAT", "236", "754"};

static void dco_note_on(track_t *t, voice_t *v)
{
    if (!v->env && !v->env_out) {                       /* a fresh voice (not a retrigger): from rest */
        uint32_t i;
        v->ph[0] = v->ph[1] = v->ph[2] = 0;
        for (i = 0; i < 8u; i++)
            v->s[i] = 0;
        /* a sound of the resonance alone (no wave, sub or noise): its filter starts ringing (fm-106) */
        if (!(t->p[P_E0] & 3) && t->p[P_E2] <= 0 && t->p[P_E3] <= 0 && t->p[P_E5] > 80)
            v->s[0] = 50 << 9;
    }
}

static void dco_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t inc = m->inc, ph = v->ph[0], sub = v->ph[1] & 1u, i, epw;
    uint32_t wave = (uint32_t)clamp(p[P_E0], 0, 3), hpf = (uint32_t)clamp(p[P_E7], 0, 3);
    int32_t z[4], s1 = v->s[4], s2 = (int32_t)v->ph[2], dco = v->s[5], dcv = v->s[6], nlp = v->s[7], nst = DCO_NZ(t);
    int32_t pw, pdc, subg, noig, key, dac, e, inj = 0, rg = clamp(p[P_DRNG], 0, 2);
    dco_vcf_t c;
    if (rg == 0)                                         /* RANGE 16' */
        inc >>= 1;
    else if (rg == 2)                                    /* 4' (at most Nyquist) */
        inc = inc >= 0x40000000u ? 0x7FFFFFFFu : inc << 1;
    for (i = 0; i < 4u; i++)
        z[i] = v->s[i];
    /* the pulse's width: PW + the track's shape modulation, 50 .. 95 % (the panel's limit: 105) */
    pw = clamp(p[P_E1] + ((m->shape - (64 << 8)) >> 8), 0, 105);
    epw = 0x7FFFFFFFu - (uint32_t)pw * 18407003u;       /* the comparator's threshold (inverted: ~(0.5 + pw)) */
    pdc = (pw * 18407003) >> 16;                         /* its DC (the coupling capacitor's steady state) */
    subg = p[P_E2] > 0 ? (DCO_SUB_LEVEL[clamp(p[P_E2], 0, 127)] * 24576) >> 15 : 0;
    noig = p[P_E3] > 0 ? DCO_NOISE_LEVEL[clamp(p[P_E3], 0, 127)] : 0;
    /* the DAC code (the D7811G's sum): CUT, the track's ENV / LFO -> FLT (m->cutoff, 1/256 of a CUT step: x 2 here),
     * KEY around middle C (the pitch x 3/8 as 8.8, as the D7811G scales it) */
    key = v->pitch16 * 6 - 60 * 16 * 6;
    key = (clamp(p[P_E6], 0, 127) * 2 * (key < 0 ? -key : key) >> 8) * (key < 0 ? -1 : 1);
    dac = (clamp(p[P_E4], 0, 127) << 7) + m->cutoff + key;
    dco_vcf_coef(&c, dac, p[P_E5]);
    e = (z[0] < 0 ? -z[0] : z[0]) + (z[1] < 0 ? -z[1] : z[1]) + (z[2] < 0 ? -z[2] : z[2]) + (z[3] < 0 ? -z[3] : z[3]);
    if ((e >> 9) < 50)                                   /* near silence: the thermal noise that starts a resonance */
        inj = 327 / (1 + (e >> 11));

    for (i = 0; i < n; i++) {
        uint32_t prev = ph;
        int32_t b, x = 0, y;
        ph += inc;
        if (ph < prev)
            sub ^= 1u;                                   /* the flip-flop on the saw's reset */
        b = blep(ph, inc);
        if (wave & 2u)                                   /* SAW x 0.6 */
            x += (((int32_t)(ph >> 16) - 32768 - b) * 19660) >> 15;
        if (wave & 1u)                                   /* PLS x 0.5, band-limited at both edges, its DC out */
            x += ((ph < epw ? -32767 : 32767) - b + blep(ph - epw, inc) - pdc) >> 1;
        if (subg) {                                      /* SUB: its edge softened with the saw's reset */
            int32_t ab = b < 0 ? -b : b;
            x += ((((sub ? -32767 : 32767) * (32768 - (ab > 32768 ? 32768 : ab))) >> 15) * subg) >> 15;
        }
        if (noig) {                                      /* NOISE through 5.3 kHz, x 2.78 */
            nlp += (9300 * ((int32_t)(noise32(&nst) >> 16) - 32768 - nlp)) >> 15;
            x += (((nlp * noig) >> 15) * 45500) >> 14;
        }
        x = dco_dc(x, &dco);
        if (inj)
            x += (((int32_t)(noise32(&nst) >> 16) - 32768) * inj) >> 15;
        y = dco_hpf(clamp(dco_vcf_run(&c, z, x), -262143, 262143), hpf, &s1, &s2);
        y = dco_dc(soft_knee((y * 3) >> 3, DCO_KNEE), &dcv);
        out[i] += voice_amp(y, m, i) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = sub;
    v->ph[2] = (uint32_t)s2;
    for (i = 0; i < 4u; i++)
        v->s[i] = z[i];
    v->s[4] = s1;
    v->s[5] = dco;
    v->s[6] = dcv;
    v->s[7] = nlp;
    DCO_NZ(t) = nst;
}

#include "dco_presets.h"         /* DCO_PRESETS, DCO_PX (tools/fm106_presets.py) */
_Static_assert(NELEM(DCO_PX) == NELEM(DCO_PRESETS), "DCO_PX: a row per preset");

static const engine_t ENG_DCO = {
    .name = "DCO",
    .page_title = {"DCO", "VCF"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 2, N_DCO_WAVE, 0},
        {"PW", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_PCT, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"KEY", F_PCT, 0, 127, 64, 0, 0},
        {"HPF", F_ENUM, 0, 3, 1, N_DCO_HPF, 0},
    },
    .presets = DCO_PRESETS,
    .npresets = NELEM(DCO_PRESETS),
    .note_on = dco_note_on,
    .render = dco_render,
    .knob = {P_E4, P_E5, P_ATK, P_REL},
    .poly = DCO_POLY,
    .keep = 0xF3,
};

/* a DCO factory preset's LFO and RANGE (apply_preset_to, after the rest: preset_t has no room for them) */
static void dco_track_loaded(track_t *t, uint32_t pi)
{
    const int8_t *x;
    if (t->eng_req % NENGINES != ENGI_DCO)
        return;
    x = DCO_PX[pi % NELEM(DCO_PX)];
    t->p[P_LRATE] = x[0];
    t->p[P_LWAVE] = 1;                                   /* TRI */
    t->p[P_LFADE] = x[1];
    t->p[P_LD_PIT] = x[2];
    t->p[P_LD_FLT] = x[3];
    t->p[P_LD_SHP] = x[4];
    t->p[P_DRNG] = x[5];
}
