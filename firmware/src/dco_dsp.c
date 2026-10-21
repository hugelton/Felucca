/* SPDX-License-Identifier: GPL-3.0-only
 * The models: Ultramaster KR-106 (Kayrock Screenprinting, GPL-3.0, github.com/kayrockscreenprinting/ultramaster_kr106)
 * Their fixed-point port to the FM-1: fm-106 (PatttF, GPL-3.0-only, github.com/PatttF/fm-106 at db39e04)
 * Adapted for Felucca, 2026 (coefficients once a block, 32-bit state, no divide per sample); free software under the
 * GNU General Public License, version 3 only (see LICENSE)
 *
 * The DCO engine's voice (eng_dco.c): a Juno-106 voice card, as fm-106 has it (fm106_osc.h, fm106_vcf.h):
 *   DCO  one phase for the saw, the pulse (the inverted comparator: its narrow part at the end of the ramp) and the
 *        sub (a flip-flop on the phase's wrap), polyBLEP at both edges; noise through a 5.3 kHz one-pole
 *   VCF  the IR3109 4-pole OTA ladder: four TPT one-poles, the BA662 pair's feedback through a saturation, the
 *        cutoff a 14-bit DAC code as the D7811G computes it (cutoff + modulation + key), its gain from the
 *        measured voice card profile (dco_tables.h DCO_VCF_G1); a little noise in silence so a pure resonance
 *        starts on its own (fm-106's adaptive thermal noise)
 *   HPF  the 4-position switch: 0 a bass boost shelf (+10.5 dB), 1 flat, 2 236 Hz, 3 754 Hz (one-pole); linear, so
 *        per voice here as after the voices on the instrument
 * Differences from fm-106 (the cost on one core): the VCF's coefficients (and its one divide), the DAC code and the
 * thermal noise's level once a block (CTL samples) instead of once a sample; the slews of the VCA and the cutoff left
 * to the host's amplitude ramp and the block rate; the DC blockers and the HPF in 32 bits (Q8 state, shifts and
 * mulq16) instead of 64; the OTA curve's divides as multiplies. The envelope and the VCA are Felucca's (voice.c),
 * the LFO the track's. */
#include "dco_tables.h"

/* the VCF's coefficients for a block: the 14-bit DAC code dac (0..0x3FFF), RES 0..127 */
typedef struct {
    int32_t g1, g2, g3;          /* the one-pole gain g and its powers, Q15 */
    int32_t omg;                 /* 1 - g, Q15 */
    int32_t k;                   /* the resonance feedback, Q12 */
    int32_t fb, ifb;             /* the BA662 pair's drive and its inverse, Q12 */
    int32_t invden;              /* 1 / (1 + k g^4), Q12 */
} dco_vcf_t;

static void dco_vcf_coef(dco_vcf_t *c, int32_t dac, int32_t res)
{
    uint32_t code = (uint32_t)clamp(dac, 0, 0x3FFF) >> 2;     /* the 12-bit code of the profile */
    int32_t g1 = DCO_VCF_G1[code], k, g4;
    res = clamp(res, 0, 127);
    k = DCO_RES_K[res];
    if (code > 3500u)                                    /* near Nyquist the resonance fades out (KR106VCF_OPTIMIZED) */
        k = k * (int32_t)(4095u - code) / 595;
    g1 = (g1 * (int32_t)DCO_PEAK_PULL[res]) >> 12;       /* the peak where the instrument has it */
    if (g1 > 32767)
        g1 = 32767;
    c->g1 = g1;
    c->g2 = (g1 * g1) >> 15;
    c->g3 = (c->g2 * g1) >> 15;
    g4 = (c->g2 * c->g2) >> 15;
    c->omg = 32768 - g1;
    c->k = k;
    c->fb = DCO_FB_SCALE[res];
    c->ifb = DCO_INV_FB_SCALE[res];
    c->invden = (4096 * 4096) / (4096 + ((k * g4) >> 15));
}

/* the OTA pair's saturation, |x| over 0..3.0 (Q15: 98304) on DCO_OTA_SAT's 256 steps of 384 */
static inline int32_t dco_ota_sat(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x), i, r;
    int32_t y;
    if (a >= 98304u) {
        y = 32767;
    } else {
        i = (a * 43691u) >> 24;                          /* a / 384, exact below 98304 */
        r = a - i * 384u;
        y = DCO_OTA_SAT[i] + (((DCO_OTA_SAT[i + 1u] - DCO_OTA_SAT[i]) * (int32_t)r * 683) >> 18);
    }
    return x < 0 ? -y : y;
}

/* one sample of the ladder: in Q15 (the DCO's mix), z[4] the stages' states (Q24); out Q15 (x 8.59, the passband's
 * makeup: an open filter passes the DCO at about 1.2) */
static inline int32_t dco_vcf_run(const dco_vcf_t *c, int32_t *z, int32_t in)
{
    int32_t z0 = z[0] >> 9, z1 = z[1] >> 9, z2 = z[2] >> 9, z3 = z[3] >> 9, s, d, u, uq, j;
    s = (int32_t)(((int64_t)z0 * c->g3 + (int64_t)z1 * c->g2 + (int64_t)z2 * c->g1) >> 15) + z3;   /* TPT predictor */
    s = (int32_t)(((int64_t)c->omg * s) >> 15);
    d = ((in * 2130) >> 15) - s;                         /* the pair: + the input x 0.065, - the feedback */
    d = (int32_t)(((int64_t)d * c->fb) >> 12);
    d = (dco_ota_sat(d) * c->ifb) >> 12;
    u = ((in * 4653) >> 15) + ((c->k * d) >> 12);       /* the direct path (x 0.142) + the resonance */
    u = (int32_t)(((int64_t)u * c->invden) >> 12);
    uq = u << 9;
    for (j = 0; j < 4; j++) {                            /* four one-poles, Q24, the states bounded */
        int32_t dl = (int32_t)(((int64_t)c->g1 * (uq - z[j])) >> 15), y = z[j] + dl;
        z[j] = clamp(y + dl, -134217728, 134217727);
        uq = y;
    }
    return ((uq >> 9) * 2199) >> 8;                      /* x 8.59 (the last stage unbounded: 2^20 at most) */
}

/* the coupling capacitors (1.6 Hz): x - its slow average, the state Q8 */
static inline int32_t dco_dc(int32_t x, int32_t *st)
{
    *st += ((x << 8) - *st) >> 12;
    return x - (*st >> 8);
}

/* the HPF switch, mode 0..3; x within +-2^18; s1, s2 its states (Q8; FLAT none, the cuts s1) */
static inline int32_t dco_hpf(int32_t x, uint32_t mode, int32_t *s1, int32_t *s2)
{
    int32_t v, lp, y1, y2;
    switch (mode) {
    case 0:                                              /* bass boost: two shelving stages, then + 2.35 x their LP */
        v = mulq16((x << 8) - *s1, 276u);                /* 59.4 Hz pole */
        lp = *s1 + v;
        *s1 = lp + v;
        lp >>= 8;
        y1 = lp + mulq16(x - lp, 54038u);                /* 72.0 Hz zero (x 0.8246) */
        v = mulq16((y1 << 8) - *s2, 336u);               /* 72.3 Hz pole */
        lp = *s2 + v;
        *s2 = lp + v;
        lp >>= 8;
        y2 = lp + mulq16(y1 - lp, 5958u);                /* 795.8 Hz zero (x 0.0909) */
        return x + 2 * y2 + mulq16(y2, 22928u);          /* (x 2.3499) */
    case 2:
    case 3:                                              /* a one-pole TPT high-pass, 236 / 754 Hz */
        v = mulq16((x << 8) - *s1, mode == 2u ? 1083u : 3343u);
        lp = *s1 + v;
        *s1 = lp + v;
        return x - (lp >> 8);
    default:                                             /* 1: flat */
        return x;
    }
}
