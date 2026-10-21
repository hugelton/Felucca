/* SPDX-License-Identifier: GPL-3.0-only */
/* DCO engine test (firmware/src/eng_dco.c, dco_dsp.c) on the Mac, through hostsim.c as regress.c.
 *   build/host/dco_test [DEMODIR]          (run_tests.sh: build/dco_demo)
 * 1. pitch: SAW, PLS and P+S at RANGE 16' 8' 4' (A4: 220 / 440 / 880 Hz) and the SUB alone (an octave down), the
 *    filter open: the rising zero crossings over 2 s within 0.5 %.
 * 2. no DC: every preset, 8 notes held for 1 s each: |mean| < 1 % of a voice's full scale.
 * 3. bounded: the corners of WAVE, CUT, RES, KEY, SUB, NOIS and HPF, 6 notes from C1 to C6 (DCO_POLY sound) at velocity
 *    127, LEVEL 127: no
 *    voice sum beyond 2 x VOICE_FS a voice and no mix sample at full scale.
 * 4. a resonance alone (no wave, sub or noise, RES 127) starts ringing by itself, and stays bounded.
 * 5. the HPF: a 55 Hz saw's fundamental, against FLAT: BOOST +5 .. +12 dB, 236 Hz and 754 Hz down (754 by > 15 dB).
 * 6. KEY: with KEY 127 the spectral centroid follows the key (an octave up: x 1.5 at least), with KEY 0 less so.
 * 7. a retrigger of a sounding voice does not click: no step in a closed-filter tone beyond 1.5 x its own slope.
 * 8. voices: at most DCO_POLY (4) of 8 held notes sound (the engine's cap); UNISON's voices start at different phases.
 * 9. determinism: the same state (the tracks, the noise seeds) renders the same samples again (every preset).
 * 10. cost: host instructions per sample per voice (6 notes held, the idle mix taken off) for every preset and the
 *     heaviest corner; fails above DCO_COST_MAX (device estimate 1.7 % per 100, as drum_test's).
 * 11. demos into DEMODIR: every preset playing its suggested pattern. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define DCO_COST_MAX 330.0        /* host instructions per sample and voice */
#define NFFT 8192

static int bad;
static void check(const char *what, int ok)
{
    printf("dco: %-88s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static const int16_t *pe(uint32_t pi)
{
    static int16_t e[8];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        e[i] = ENG_DCO.presets[pi].e[i];
    return e;
}

/* track 0 DCO with e[0..7], RANGE rg, a flat envelope (ATK 0, SUS full), no ENV / LFO amounts, no sends, POLY */
static void setup(const int16_t *e, int rg)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_DCO, 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_DRNG] = (int16_t)rg;
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 10;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_LD_PIT] = trk[0].p[P_LD_FLT] = trk[0].p[P_LD_SHP] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}

static double Y[4 * FS];

/* the voices' sum (track_render, before the mix) of note for n samples, after 0.1 s */
static void voice_render(uint32_t note, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k;
    trk_note_on(&trk[0], note, 100);
    for (i = 0; i < FS / 10u / CTL; i++)
        track_render(&trk[0], b, CTL);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k] / 32768.0;
    }
}

static double rms(const double *y, uint32_t n, double *mean)
{
    double s = 0, m = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        m += y[i];
    m /= n;
    for (i = 0; i < n; i++)
        s += (y[i] - m) * (y[i] - m);
    if (mean)
        *mean = m;
    return sqrt(s / n);
}

/* ------------------------------------------------------------ spectra --- */
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (i = 0; i < n; i += len)
            for (j = 0; j < len / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j);
                double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                re[i + j + len / 2] = re[i + j] - xr, im[i + j + len / 2] = im[i + j] - xi;
                re[i + j] += xr, im[i + j] += xi;
            }
    }
}

/* the power spectrum (Hann) of NFFT samples of y into ps[NFFT / 2] */
static void spectrum(const double *y, double *ps)
{
    static double re[NFFT], im[NFFT];
    uint32_t i;
    for (i = 0; i < NFFT; i++) {
        re[i] = y[i] * (0.5 - 0.5 * cos(2 * M_PI * i / NFFT));
        im[i] = 0;
    }
    fft(re, im, NFFT);
    for (i = 0; i < NFFT / 2; i++)
        ps[i] = re[i] * re[i] + im[i] * im[i];
}

static double band_db(const double *ps, double lo, double hi)
{
    double p = 1e-30;
    uint32_t i;
    for (i = (uint32_t)(lo / FS * NFFT); i <= (uint32_t)(hi / FS * NFFT); i++)
        p += ps[i];
    return 10 * log10(p);
}

static double centroid(const double *ps)
{
    double s = 0, w = 0;
    uint32_t i;
    for (i = 1; i < NFFT / 2; i++) {
        s += ps[i];
        w += ps[i] * i * (double)FS / NFFT;
    }
    return s > 0 ? w / s : 0;
}

/* --------------------------------------------------------------- pitch --- */
/* rising zero crossings a second, with a hysteresis of a tenth of the RMS */
static double crossing_hz(const double *y, uint32_t n)
{
    double h = rms(y, n, 0) * 0.1;
    uint32_t i, c = 0;
    int below = 0;
    for (i = 0; i < n; i++) {
        if (y[i] < -h)
            below = 1;
        else if (below && y[i] > h) {
            c++;
            below = 0;
        }
    }
    return (double)c * FS / n;
}

static void pitch(void)
{
    static const char *const WN[4] = {"OFF", "PLS", "SAW", "P+S"};
    double worst = 0;
    uint32_t w, rg;
    char what[160], at[48] = "";
    for (w = 1; w <= 3u; w++)
        for (rg = 0; rg < 3u; rg++) {
            int16_t e[8] = {(int16_t)w, 0, 0, 0, 127, 0, 0, 1};
            double f, want = 440.0 * pow(2.0, (double)rg - 1.0), err;
            setup(e, (int)rg);
            voice_render(69, Y, 2 * FS);
            f = crossing_hz(Y, 2 * FS);
            err = fabs(f / want - 1);
            if (getenv("VERBOSE"))
                printf("dco:   %s %s: %.2f Hz (%.0f)\n", WN[w], rg == 0 ? "16'" : rg == 1 ? "8'" : "4'", f, want);
            if (err > worst) {
                worst = err;
                snprintf(at, sizeof at, "%s at %.0f Hz: %.2f", WN[w], want, f);
            }
        }
    {   /* the SUB alone: an octave down */
        int16_t e[8] = {0, 0, 127, 0, 127, 0, 0, 1};
        double f, err;
        setup(e, 1);
        voice_render(69, Y, 2 * FS);
        f = crossing_hz(Y, 2 * FS);
        err = fabs(f / 220.0 - 1);
        if (err > worst) {
            worst = err;
            snprintf(at, sizeof at, "SUB at 220 Hz: %.2f", f);
        }
    }
    snprintf(what, sizeof what, "pitch: SAW PLS P+S at 16' 8' 4', SUB an octave down: worst %.2f %% %s", worst * 100,
             worst > 0.005 ? at : "");
    check(what, worst <= 0.005);
}

/* --------------------------------------------------------- DC, bounds --- */
static void dc_and_bounds(void)
{
    static int32_t o[2 * CTL], b[CTL];
    uint32_t pi, i, k, worst_p = 0;
    double worst = 0;
    int32_t vmax = 0, omax = 0;
    char what[160];
    for (pi = 0; pi < ENG_DCO.npresets; pi++) {
        double mean = 0, m1;
        for (k = 0; k < 8u; k++) {
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], ENGI_DCO, pi);
            trk[0].p[P_ATK] = 0;
            trk[0].p[P_SUS] = 127;
            voice_render(36 + 6 * k, Y, FS);
            rms(Y, FS, &m1);
            mean += m1 / 8;
        }
        if (fabs(mean) * 32768.0 / VOICE_FS > worst)
            worst = fabs(mean) * 32768.0 / VOICE_FS, worst_p = pi;
    }
    snprintf(what, sizeof what, "no DC: worst |mean| %.3f %% of a voice's full scale (%s)", worst * 100,
             ENG_DCO.presets[worst_p].name);
    check(what, worst < 0.01);
    for (k = 0; k < 512u; k++) {                      /* WAVE x CUT x RES x KEY x SUB x NOIS x HPF corners */
        int16_t e[8] = {(int16_t)(k & 3u), 0, k & 64u ? 127 : 0, k & 128u ? 127 : 0, k & 4u ? 127 : 0,
                        k & 8u ? 127 : 0, k & 16u ? 127 : 0, (int16_t)(k & 32u ? (k & 256u ? 0 : 3) : 1)};
        setup(e, 1);
        trk[0].p[P_LEVEL] = 127;
        for (i = 0; i < 6u; i++)
            trk_note_on(&trk[0], 24 + 12 * i, 127);
        for (i = 0; i < FS / 2u / CTL; i++) {
            uint32_t j;
            track_render(&trk[0], b, CTL);
            for (j = 0; j < CTL; j++)
                vmax = abs(b[j]) > vmax ? abs(b[j]) : vmax;
        }
        setup(e, 1);
        trk[0].p[P_LEVEL] = 127;
        for (i = 0; i < 6u; i++)
            trk_note_on(&trk[0], 24 + 12 * i, 127);
        for (i = 0; i < FS / 2u / CTL; i++) {
            uint32_t j;
            mix_block(o, CTL);
            for (j = 0; j < 2u * CTL; j++)
                omax = abs(o[j]) > omax ? abs(o[j]) : omax;
        }
    }
    snprintf(what, sizeof what, "bounded at the corners: voice sum peak %d (%d voices, limit %d), mix peak %d", vmax,
             DCO_POLY, DCO_POLY * 2 * VOICE_FS, omax);
    check(what, vmax < DCO_POLY * 2 * VOICE_FS && omax < 32767);
}

static void resonance(void)
{
    int16_t e[8] = {0, 0, 0, 0, 64, 127, 64, 1};
    double r, m, peak = 0;
    uint32_t i;
    char what[160];
    setup(e, 1);
    voice_render(60, Y, 2 * FS);
    r = rms(Y + FS, FS, &m);
    for (i = 0; i < 2u * FS; i++)
        peak = fabs(Y[i]) > peak ? fabs(Y[i]) : peak;
    snprintf(what, sizeof what, "a resonance alone (RES 127, no wave) rings by itself: RMS %.1f dB, peak %.2f", 20 * log10(r + 1e-12),
             peak * 32768.0 / VOICE_FS);
    check(what, 20 * log10(r + 1e-12) > -40 && peak * 32768.0 < 2.0 * VOICE_FS);
}

static double PS[NFFT / 2];

static void hpf(void)
{
    double db[4];
    uint32_t m;
    char what[200];
    for (m = 0; m < 4u; m++) {
        int16_t e[8] = {2, 0, 0, 0, 127, 0, 0, (int16_t)m};
        setup(e, 1);
        voice_render(33, Y, NFFT);                    /* A1: 55 Hz */
        spectrum(Y, PS);
        db[m] = band_db(PS, 50, 60);
    }
    snprintf(what, sizeof what, "HPF at 55 Hz against FLAT: BOOST %+.1f dB, 236 %+.1f dB, 754 %+.1f dB", db[0] - db[1],
             db[2] - db[1], db[3] - db[1]);
    check(what, db[0] - db[1] >= 5 && db[0] - db[1] <= 12 && db[2] < db[1] && db[3] < db[1] - 15);
}

static void keytrack(void)
{
    double r[2];
    uint32_t k;
    char what[160];
    for (k = 0; k < 2u; k++) {                       /* CUT 64: ~800 Hz at middle C; C2 and C4 */
        int16_t e[8] = {2, 0, 0, 0, 64, 0, (int16_t)(k ? 127 : 0), 1};
        double c0, c1;
        setup(e, 1);
        voice_render(36, Y, NFFT);
        spectrum(Y, PS);
        c0 = centroid(PS);
        setup(e, 1);
        voice_render(60, Y, NFFT);
        spectrum(Y, PS);
        c1 = centroid(PS);
        r[k] = c1 / c0;
    }
    snprintf(what, sizeof what, "KEY: the centroid two octaves up x %.2f at KEY 127 (tracking), x %.2f at KEY 0", r[1],
             r[0]);
    check(what, r[1] >= 3.0 && r[1] > r[0] * 1.3);
}

/* the largest sample step of a closed-filter saw just after the same note again, against its own steady steps */
static void retrigger(void)
{
    static int32_t b[CTL];
    int16_t e[8] = {2, 0, 0, 0, 30, 20, 0, 1};
    int32_t prev = 0, steady = 0, at = 0;
    uint32_t i, j;
    char what[160];
    setup(e, 1);
    trk[0].p[P_ATK] = 40;                              /* a soft attack: the envelope's restart is no step either */
    trk_note_on(&trk[0], 45, 100);
    for (i = 0; i < FS / CTL; i++) {
        if (i == FS / 2u / CTL)
            trk_note_on(&trk[0], 45, 100);            /* the same note again: the sounding voice, retriggered */
        track_render(&trk[0], b, CTL);
        for (j = 0; j < CTL; j++) {
            int32_t d = abs(b[j] - prev);
            prev = b[j];
            if (i > FS / 4u / CTL && i < FS / 2u / CTL)
                steady = d > steady ? d : steady;
            else if (i >= FS / 2u / CTL && i < FS / 2u / CTL + 4u)
                at = d > at ? d : at;
        }
    }
    snprintf(what, sizeof what, "a retrigger keeps the voice going: largest step %d after it, %d steady", at, steady);
    check(what, at <= steady * 3 / 2 + 8);
}

static void voices(void)
{
    static int32_t b[CTL];
    int16_t e[8] = {2, 0, 0, 0, 100, 0, 0, 1};
    uint32_t i, n = 0, same = 1;
    char what[120];
    setup(e, 1);
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], 48 + 2 * i, 100);
    track_render(&trk[0], b, CTL);
    for (i = 0; i < NVOICE; i++)
        n += trk[0].v[i].active != 0;
    snprintf(what, sizeof what, "8 notes held: %u voices sound (the cap of %d)", n, DCO_POLY);
    check(what, n == DCO_POLY);
    setup(e, 1);
    trk[0].p[P_VOICE] = V_UNISON;
    trk[0].p[P_DETUNE] = 20;
    trk_note_on(&trk[0], 57, 100);
    for (i = 1; i < DCO_POLY; i++)
        same &= trk[0].v[i].ph[0] == trk[0].v[0].ph[0];
    check("UNISON: its voices start at different phases", !same);
}

/* the noise (the DCO's NOISE and the VCF's thermal noise) is one running xorshift per part, as fm-106's runs on across
 * notes: a sound is the same again from the same state (power-on: regress.c's golden renders), not from any silence */
static void fresh(uint32_t pi)
{
    uint32_t i;
#if FELUCCA_CPU2
    for (i = 0; i < NTRK; i++)
        dco_nz[i] = 0x3C6EF372;
#else
    dco_nz = 0x3C6EF372;
#endif
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_DCO, pi);
    (void)i;
}

static void determinism(void)
{
    static int32_t a[FS / 2], b[CTL];
    uint32_t pi, i, k, pass, same = 1;
    for (pi = 0; pi < ENG_DCO.npresets; pi++)
        for (pass = 0; pass < 2u; pass++) {
            fresh(pi);
            trk_note_on(&trk[0], 62, 100);
            for (i = 0; i < FS / 2 / CTL; i++) {
                track_render(&trk[0], b, CTL);
                for (k = 0; k < CTL; k++) {
                    if (pass)
                        same &= a[i * CTL + k] == b[k];
                    a[i * CTL + k] = b[k];
                }
            }
        }
    check("the same state renders the same samples again (every preset)", same);
}

/* ---------------------------------------------------------------- cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

static double mix_cost(int32_t pi, uint32_t notes)
{
    static const int16_t HEAVY[8] = {3, 64, 127, 127, 90, 120, 127, 0};   /* P+S, SUB, NOISE, RES high, BOOST */
    static int32_t o[2 * CTL];
    uint32_t i, nb = FS * 2u / CTL;
    uint64_t i0;
    if (pi < 0) {
        setup(HEAVY, 1);
        trk[0].p[P_LD_SHP] = 30;
    } else {
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        host_preset(&trk[0], ENGI_DCO, (uint32_t)pi);
    }
    for (i = 0; i < notes; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 100);
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
    i0 = instr_now();
    for (i = 0; i < nb; i++)
        mix_block(o, CTL);
    return (double)(instr_now() - i0) / (nb * CTL);
}

static void cost(void)
{
    double idle, c, worst = 0;
    int32_t pi;
    char what[160];
    if (!instr_now()) {
        printf("dco: cost: no instruction counter on this host (proc_pid_rusage); not measured\n");
        return;
    }
    idle = mix_cost(-1, 0);
    printf("dco: cost, host instructions per sample and voice (%d held; device estimate 1.7 %% per 100):\n", DCO_POLY);
    for (pi = -1; pi < (int32_t)ENG_DCO.npresets; pi++) {
        c = (mix_cost(pi, DCO_POLY) - idle) / DCO_POLY;
        if (getenv("VERBOSE") || pi < 0)
            printf("dco:   %-10s %5.0f  ~%.2f %%\n", pi >= 0 ? ENG_DCO.presets[pi].name : "(corner)", c, c * 0.017);
        worst = c > worst ? c : worst;
    }
    snprintf(what, sizeof what, "cost: at most %.0f instructions a sample and voice (~%.1f %% on the device; limit %.0f)",
             worst, worst * 0.017, DCO_COST_MAX);
    check(what, worst <= DCO_COST_MAX);
}

/* --------------------------------------------------------------- demos --- */
static void demo(const char *dir, uint32_t pi)
{
    static int32_t o[2 * CTL];
    const preset_t *pr = &ENG_DCO.presets[pi];
    uint32_t pat = pr->pat ? pr->pat - 1u : 4u, step = FS / 8u, s, i, held = 0, frames = 0;
    char path[512], nm[32];
    FILE *w;
    for (i = 0; pr->name[i] && i < 31u; i++)
        nm[i] = pr->name[i] == ' ' ? '_' : pr->name[i];
    nm[i] = 0;
    snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, pi, nm);
    if (!(w = fopen(path, "wb")))
        return;
    wav_hdr(w, 0);
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_DCO, pi);
    for (s = 0; s < 48u; s++) {                       /* three times through the pattern's 16 steps, 120 BPM 1/16 */
        uint32_t k = s % 16u, note = PATTERNS[pat].note[k], fl = PATTERNS[pat].flags[k];
        if (!(fl & 4u) && held) {
            trk_note_off(&trk[0], held);
            held = 0;
        }
        if (note && !(fl & 4u)) {
            trk_note_on(&trk[0], note, fl & 1u ? 120 : 90);
            held = note;
        }
        for (i = 0; i < step; i += CTL) {
            uint32_t j;
            mix_block(o, CTL);
            for (j = 0; j < CTL; j++)
                wav_put(w, o[2 * j], o[2 * j + 1]);
            frames += CTL;
        }
    }
    if (held)
        trk_note_off(&trk[0], held);
    for (i = 0; i < FS * 2u; i += CTL) {
        uint32_t j;
        mix_block(o, CTL);
        for (j = 0; j < CTL; j++)
            wav_put(w, o[2 * j], o[2 * j + 1]);
        frames += CTL;
    }
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
}

int main(int argc, char **argv)
{
    uint32_t pi;
    pitch();
    dc_and_bounds();
    resonance();
    hpf();
    keytrack();
    retrigger();
    voices();
    determinism();
    cost();
    if (argc > 1) {
        for (pi = 0; pi < ENG_DCO.npresets; pi++)
            demo(argv[1], pi);
        printf("dco: demos in %s/\n", argv[1]);
    }
    if (bad)
        printf("dco: %d check(s) FAILED\n", bad);
    return bad ? 1 : 0;
}
