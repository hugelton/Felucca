/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of 1.5.5's PHASE FOLD (Discussion #104; src/eng_phase.c, E7), same sources as the firmware (hostsim.c):
 *   build/host/fold155_test [DEMO_DIR]      (run_tests.sh builds and runs it; the demo in build/fold155_demo/)
 * 1. the parameter: E7 "FOLD" 0..127, default 0; every factory PHASE preset at FOLD 0.
 * 2. FOLD 0 bit for bit: the engine against a copy of 1.5.1's render loop (ref_render below), every WAVE, WAVE2,
 *    DCW, ENV, DTN, LINE, SUB over low / middle / high notes (and the golden renders: regress.c).
 * 3. bounded: FOLD 1..127 on every WAVE, MIX and RING, SUB 0 / 127, notes 12..127: the voice's sample within
 *    +-65535 before voice_amp (s x amp in 32 bits; 1.5.5's FOLD with SUB wrapped there: sign-flipped spikes). Checked
 *    against a reference of the amp stage that cannot wrap: the same render at 1/8 the amplitude, scaled back (a wrap
 *    shows as a difference of a full scale or more; a wrapped peak itself can look small), and that reference's peak
 *    within the bound (the peaks printed).
 * 4. no DC: |mean| under 5 % of full scale at FOLD 1..127 (FOLD 0 itself has up to ~80 % on PLS and RING).
 * 5. brightness: sqrt(mean square slope / mean square) at FOLD 0, 32, 64, 96, 127 each higher (and at most a small
 *    sag between folds, every 4 steps) on a sine (DCW 0) and on SAW / SQR at DCW 30; above A4 the fold gain falls
 *    with the pitch (alias-aware).
 * 5b. FOLD going on and off (0 <-> 1, 0 <-> 127: a knob, LFO / MOD, automation) on a held note: no step at the switch
 *    (FOLD 0 has no DC blocker, the fold's output none of the wave's DC; 1.5.5 stepped up to ~half full scale and
 *    took the blocker's state from the last FOLD, however old): the largest sample step around each switch against
 *    the largest of the same note held at either FOLD (the steps printed).
 * 6. the cost: PHASE 8 voices (BRASS) at FOLD 0 and 127, instructions per sample of the whole mix.
 * 7. demo (DEMO_DIR): phase_fold.wav, a PHASE bass line with FOLD swept 0 -> 127 -> 0 over 8 s, then BRASS chords
 *    at FOLD 0, 40 and 90. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define ENGI_PHASE 2u                                /* engines.c ENGINES[] */
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("fold155: %-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* 1.5.1's phase_render, verbatim (the reference of FOLD 0) */
static void ref_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)p[P_E0] & 7u, w2 = (uint32_t)p[P_E1], i;
    int32_t depth = (p[P_E2] << 8) + m->cutoff + mulq15(m->envq15, p[P_E3] * 256);
    uint32_t dcw, inc = m->inc, inc2;
    int32_t det = p[P_E4], line2 = det != 0 || p[P_E5], ring = p[P_E5], sub = p[P_E6] * 200;
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t tg0 = v->s[0], tg1 = v->s[1];
    pd_t b1, b2;
    depth = clamp(depth + (m->shape - (64 << 8)), 0, 127 << 8);
    dcw = (uint32_t)depth * 65535u / (127u << 8);
    dcw = (dcw * 56000u) >> 16;
    inc2 = pitch_inc(clamp(m->pitch16 + d16, 0, 2047));
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    pd_setup(&b1, w1, dcw);
    pd_setup(&b2, w2 ? w2 - 1u : w1, dcw);
    for (i = 0; i < n; i++) {
        uint32_t old = ph0;
        int32_t s;
        s = pd_wave(tg0 ? &b2 : &b1, ph0 >> 16);
        ph0 += inc;
        if (ph0 < old)
            tg0 ^= 1;
        if (line2) {
            uint32_t old2 = ph1;
            int32_t s2 = pd_wave(tg1 ? &b2 : &b1, ph1 >> 16);
            ph1 += inc2;
            if (ph1 < old2)
                tg1 ^= 1;
            s = ring ? mulq15(s, s2) : (s + s2) >> 1;
        }
        if (sub) {
            ph2 += inc >> 1;
            s += mulq15(osc_sine(ph2), sub);
        }
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = tg0;
    v->s[1] = tg1;
}

/* ------------------------------------------------- one voice, the engine alone --- */
#define NV (FS / 4u / CTL * CTL)                     /* a quarter second */
static int32_t buf[NV], buf2[NV];
static track_t tt;
static int32_t vr_amp = 32767;                       /* voice_run's amplitude (Q15) */

static void setp(int32_t w, int32_t w2, int32_t dcw, int32_t env, int32_t dtn, int32_t line, int32_t sub, int32_t fold)
{
    memset(&tt, 0, sizeof tt);
    tt.engine = tt.eng_req = ENGI_PHASE;
    tt.p[P_E0] = (int16_t)w; tt.p[P_E1] = (int16_t)w2; tt.p[P_E2] = (int16_t)dcw; tt.p[P_E3] = (int16_t)env;
    tt.p[P_E4] = (int16_t)dtn; tt.p[P_E5] = (int16_t)line; tt.p[P_E6] = (int16_t)sub; tt.p[P_E7] = (int16_t)fold;
}
/* note at full amplitude, the envelope value env (Q15) as a decaying ramp: into o[] (NV samples) */
static void voice_run(void (*render)(track_t *, voice_t *, int32_t *, uint32_t, const vmod_t *), uint32_t note, int32_t *o)
{
    voice_t v;
    vmod_t m;
    uint32_t k;
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    ENG_PHASE.note_on(&tt, &v);
    m.pitch16 = (int32_t)note * 16;
    m.inc = pitch_inc((uint32_t)clamp(m.pitch16, 0, 2047));
    m.shape = 64 << 8;
    memset(o, 0, NV * sizeof o[0]);
    for (k = 0; k < NV / CTL; k++) {
        m.amp0 = m.amp1 = vr_amp;
        m.envq15 = 32767 - (int32_t)(k * 32767u / (NV / CTL));
        m.cutoff = (int32_t)(k & 7u) * 64;               /* a little movement of the bend */
        render(&tt, &v, o + k * CTL, CTL, &m);
    }
}
typedef struct { double mean, rms, peak, bright; } st_t;
static st_t stats(const int32_t *o)
{
    st_t s = {0};
    double d2 = 0;
    uint32_t i, from = NV / 8u;
    for (i = from; i < NV; i++) {
        double x = o[i], d = (double)o[i] - o[i - 1];
        s.mean += x;
        s.rms += x * x;
        d2 += d * d;
        s.peak = fabs(x) > s.peak ? fabs(x) : s.peak;
    }
    s.bright = s.rms > 0 ? d2 / s.rms : 0;
    s.mean /= NV - from;
    s.rms = sqrt(s.rms / (NV - from));
    return s;
}

static double late_mean(const int32_t *o)
{
    double m = 0;
    uint32_t i;
    for (i = NV / 2u; i < NV; i++)
        m += o[i];
    return m / (NV - NV / 2u);
}

static void test_param(void)
{
    const param_desc_t *d = &ENG_PHASE.edit[7];
    uint32_t k, zero = 1;
    for (k = 0; k < ENG_PHASE.npresets; k++)
        zero &= ENG_PHASE.presets[k].e[7] == 0;
    check("E7 is FOLD: 0..127 (%), default 0", str_eq(d->label, "FOLD") && d->fmt == F_PCT && d->min == 0 &&
          d->max == 127 && d->def == 0);
    check("every factory PHASE preset at FOLD 0 (the presets as they were)", zero);
}

static void test_identical(void)
{
    static const uint8_t NOTE[4] = {24, 60, 96, 127};
    static const uint8_t DCW[3] = {0, 60, 127}, DTN[3] = {0, 7, 127}, SUB[2] = {0, 127};
    uint32_t w, w2, a, b, c, l, s, nt, runs = 0, diff = 0;
    for (w = 0; w < 8u; w++)
        for (w2 = 0; w2 < 9u; w2 += 4u)
            for (a = 0; a < 3u; a++)
                for (b = 0; b < 3u; b++)
                    for (l = 0; l < 2u; l++)
                        for (s = 0; s < 2u; s++)
                            for (nt = 0; nt < 4u; nt++) {
                                c = (a + b + nt) & 1u ? 90u : 0u;
                                setp((int32_t)w, (int32_t)w2, DCW[a], (int32_t)c, DTN[b], (int32_t)l, SUB[s], 0);
                                voice_run(ENG_PHASE.render, NOTE[nt], buf);
                                voice_run(ref_render, NOTE[nt], buf2);
                                diff += memcmp(buf, buf2, sizeof buf) != 0;
                                runs++;
                            }
    {
        char msg[160];
        snprintf(msg, sizeof msg, "FOLD 0 bit for bit with 1.5.1's loop (%u renders: waves, DCW, ENV, DTN, LINE, SUB, notes)", runs);
        check(msg, !diff);
    }
}

static void test_bounded_dc(void)
{
    static const uint8_t NOTE[5] = {12, 36, 60, 96, 127};
    static const uint8_t FOLD[4] = {1, 40, 90, 127};
    /* full scale: |s| 32767, << 1 after voice_amp. The bound with FOLD: the voice's sample within +-65535 before
     * voice_amp (at the amplitude 32767 the product then fits 32 bits); FOLD 0 reaches 32767 + 127 * 200. The reference
     * (ref): the render at amplitude 4096 (s x 4096 cannot wrap) x 32767 / 4096, within its rounding (~12, x8 of the
     * product's two roundings and the << 1) of the render at 32767. The DC: SUB 0, notes 36 and up (note 12's, or SUB's
     * octave down, few periods: their mean is not DC), over the second half (the blocker settled: 125 ms after the
     * note-on) */
    const double lim0 = 2.0 * VOICE_FS * 65535.0 / 32767.0 + 2, fs = 2.0 * VOICE_FS;
    double worst = 0, worst_sub = 0, dc = 0, dc0 = 0, wdiff = 0;
    uint32_t w, l, s, nt, f, i, ok = 1, wraps = 0;
    for (w = 0; w < 8u; w++)
        for (l = 0; l < 2u; l++)
            for (s = 0; s < 2u; s++)
                for (nt = 0; nt < 5u; nt++) {
                    setp((int32_t)w, 0, 70, 40, 0, (int32_t)l, s ? 127 : 0, 0);
                    voice_run(ENG_PHASE.render, NOTE[nt], buf);
                    if (nt && !s)
                        dc0 = fabs(late_mean(buf)) / fs > dc0 ? fabs(late_mean(buf)) / fs : dc0;
                    for (f = 0; f < 4u; f++) {
                        double pk = 0, d;
                        setp((int32_t)w, 0, 70, 40, 0, (int32_t)l, s ? 127 : 0, FOLD[f]);
                        vr_amp = 4096;
                        voice_run(ENG_PHASE.render, NOTE[nt], buf2);
                        vr_amp = 32767;
                        voice_run(ENG_PHASE.render, NOTE[nt], buf);
                        for (i = 0; i < NV; i++) {
                            double ref = buf2[i] * (32767.0 / 4096.0);
                            d = fabs(buf[i] - ref);
                            wdiff = d > wdiff ? d : wdiff;
                            wraps += d > 64.0;
                            pk = fabs(ref) > pk ? fabs(ref) : pk;
                        }
                        if (s)
                            worst_sub = pk > worst_sub ? pk : worst_sub;
                        else
                            worst = pk > worst ? pk : worst;
                        ok &= pk <= lim0;
                        if (nt && !s)
                            dc = fabs(late_mean(buf)) / fs > dc ? fabs(late_mean(buf)) / fs : dc;
                    }
                }
    {
        char msg[220];
        snprintf(msg, sizeof msg, "no wrap: FOLD 1..127 against the amp stage's reference: %u samples off (worst %.2f full "
                 "scale; <= 64 LSB is rounding)", wraps, wdiff / fs);
        check(msg, !wraps);
        snprintf(msg, sizeof msg, "bounded: FOLD 1..127, 8 waves, MIX / RING, notes 12..127: peak %.2f (SUB 127: %.2f) "
                 "<= %.2f full scale (|s| < 65536)", worst / fs, worst_sub / fs, lim0 / fs);
        check(msg, ok);
        snprintf(msg, sizeof msg, "no DC: |mean| at most %.3f of full scale with FOLD (FOLD 0: up to %.3f; limit 0.05: the blocker trails the DC the envelope moves)", dc, dc0);
        check(msg, dc <= 0.05);
    }
}

static double bright_at(int32_t w, int32_t dcw, int32_t fold, uint32_t note, double *rms)
{
    st_t s;
    setp(w, 0, dcw, 0, 0, 0, 0, fold);
    voice_run(ENG_PHASE.render, note, buf);
    s = stats(buf);
    if (rms)
        *rms = s.rms;
    return s.bright;
}

static void test_bright(void)
{
    static const int8_t CASE[3][2] = {{0, 0}, {0, 30}, {1, 30}};   /* WAVE, DCW: a sine, SAW, SQR */
    static const char *const NAME[3] = {"sine (DCW 0)", "SAW DCW 30", "SQR DCW 30"};
    uint32_t c, f;
    /* brightness: sqrt(mean square slope / mean square), a spectral centroid's measure. A sine folder's brightness
     * climbs a fold at a time: between two folds it may sag a little (the new fold still flat), so: FOLD 0, 32, 64, 96,
     * 127 each brighter than the last, and every 4 steps never under 80 % of the brightest so far (a square folds in steps: its flat tops land on the sine at once) */
    for (c = 0; c < 3u; c++) {
        double runmax = 0, ripple = 1, coarse = 0, b0 = 0, b127 = 0, r0 = 0, r127 = 0;
        uint32_t mono = 1;
        char msg[220];
        for (f = 0; f <= 128u; f += 4u) {
            int32_t fold = f > 127u ? 127 : (int32_t)f;
            double rms, b = sqrt(bright_at(CASE[c][0], CASE[c][1], fold, 48, &rms));
            if (getenv("FOLD_VERBOSE"))
                printf("fold155:   %s FOLD %3d brightness %.4f\n", NAME[c], fold, b);
            runmax = b > runmax ? b : runmax;
            ripple = b / runmax < ripple ? b / runmax : ripple;
            if (!(f & 31u) || fold == 127) {
                mono &= b > coarse * 1.1;
                coarse = b;
            }
            if (!fold) {
                b0 = b;
                r0 = rms;
            }
            if (fold == 127) {
                b127 = b;
                r127 = rms;
            }
        }
        snprintf(msg, sizeof msg, "brightness rises with FOLD on %s: x%.1f at 127, sag between folds %.0f %% (level %+.1f dB)",
                 NAME[c], b127 / b0, (1 - ripple) * 100, 20 * log10(r127 / r0));
        check(msg, mono && ripple >= 0.8 && b127 > b0 * 4);
    }
    {
        uint32_t g60 = pd_fold_gain(127, pitch_inc(60 * 16)), g69 = pd_fold_gain(127, pitch_inc(69 * 16)),
                 g81 = pd_fold_gain(127, pitch_inc(81 * 16)), g105 = pd_fold_gain(127, pitch_inc(105 * 16));
        char msg[200];
        snprintf(msg, sizeof msg, "alias-aware: FOLD 127's gain x%.2f at C4 and A4, x%.2f at A5, x%.2f at A7 (x0.64 at FOLD 1)",
                 g60 / 256.0, g81 / 256.0, g105 / 256.0);
        /* the part above x0.64 halves per octave above A4 */
        check(msg, g60 == g69 && g69 > 2000u && abs((int)(g81 - PD_FOLD_G0) * 2 - (int)(g69 - PD_FOLD_G0)) < 4 &&
              abs((int)(g105 - PD_FOLD_G0) * 8 - (int)(g69 - PD_FOLD_G0)) < 16 && pd_fold_gain(1, pitch_inc(60 * 16)) < 256u);
    }
}

/* ------------------------------------------------- FOLD going on / off --- */
/* one held note (DCW 70: the waves' DC as played), blocks at FOLD fold[b / SEG] (SEG blocks each, nseg of them) into
 * sw_o; the largest sample step |y[i] - y[i - 1]| over blocks b0 .. b1 - 1 */
#define SEG 40u
static int32_t sw_o[SEG * 8u * CTL];
static void sw_run(int32_t w, int32_t line, int32_t sub, uint32_t note, const int16_t *fold, uint32_t nseg)
{
    voice_t v;
    vmod_t m;
    uint32_t b;
    setp(w, 0, 70, 0, line ? 7 : 0, line, sub, fold[0]);
    memset(&v, 0, sizeof v);
    memset(&m, 0, sizeof m);
    ENG_PHASE.note_on(&tt, &v);
    m.pitch16 = (int32_t)note * 16;
    m.inc = pitch_inc((uint32_t)m.pitch16);
    m.shape = 64 << 8;
    m.amp0 = m.amp1 = 32767;
    memset(sw_o, 0, sizeof sw_o);
    for (b = 0; b < nseg * SEG; b++) {
        tt.p[P_E7] = fold[b / SEG];
        ENG_PHASE.render(&tt, &v, sw_o + b * CTL, CTL, &m);
    }
}
static double sw_step(uint32_t b0, uint32_t b1)
{
    double m = 0, d;
    uint32_t i;
    for (i = b0 * CTL; i < b1 * CTL; i++)
        if (i && (d = fabs((double)sw_o[i] - sw_o[i - 1])) > m)
            m = d;
    return m;
}
/* the note held at FOLD f: its largest step once settled */
static double sw_held(int32_t w, int32_t line, int32_t sub, uint32_t note, int16_t f)
{
    int16_t one[2] = {f, f};
    sw_run(w, line, sub, note, one, 2u);
    return sw_step(SEG, 2u * SEG);
}
static void test_switch(void)
{
    /* on, off, on again (the blocker's state from the last FOLD), the same at 127 */
    static const int16_t SEQ1[8] = {0, 1, 0, 1, 0, 127, 0, 127};
    static const int8_t CASE[6][3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 0, 127}, {0, 1, 0}, {5, 1, 0}};   /* WAVE LINE SUB */
    static const uint8_t NOTE[3] = {36, 48, 72};
    const double fs = 2.0 * VOICE_FS;
    double worst = 0, wf = 0;
    uint32_t c, nt, j;
    for (c = 0; c < 6u; c++)
        for (nt = 0; nt < 3u; nt++) {
            double h0 = sw_held(CASE[c][0], CASE[c][1], CASE[c][2], NOTE[nt], 0),
                   h1 = sw_held(CASE[c][0], CASE[c][1], CASE[c][2], NOTE[nt], 1),
                   h127 = sw_held(CASE[c][0], CASE[c][1], CASE[c][2], NOTE[nt], 127), cw = 0;
            sw_run(CASE[c][0], CASE[c][1], CASE[c][2], NOTE[nt], SEQ1, 8u);
            for (j = 1; j < 8u; j++) {                  /* the switch into segment j: a block before, two after */
                double hj = SEQ1[j] | SEQ1[j - 1u] ? (SEQ1[j] | SEQ1[j - 1u]) == 1 ? h1 : h127 : h0,
                       held = hj > h0 ? hj : h0, click = sw_step(j * SEG - 1u, j * SEG + 2u) - held;
                cw = click > cw ? click : cw;
                if (click > worst) {
                    worst = click;
                    wf = held;
                }
            }
            if (getenv("FOLD_VERBOSE"))
                printf("fold155:   switch WAVE %d LINE %d SUB %d note %u: step over the held note's %.3f of full scale\n",
                       CASE[c][0], CASE[c][1], CASE[c][2], NOTE[nt], cw / fs);
        }
    {
        char msg[220];
        snprintf(msg, sizeof msg, "FOLD 0 <-> 1 / 127 on a held note: a switch steps %.3f of full scale over the held note's "
                 "largest step (%.3f there; limit 0.05: a block's crossfade; 1.5.5: 1.75)", worst / fs, wf / fs);
        check(msg, worst <= 0.05 * fs);
    }
}

/* ------------------------------------------------------------------ cost --- */
static int32_t out_buf[2 * CTL];
static void blocks(uint32_t n)
{
    while (n--)
        mix_block(out_buf, CTL);
}
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static void fresh(uint32_t pi)                         /* the boot state, track 1 = PHASE preset pi */
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_latched = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    memset(trk, 0, sizeof trk);
    MOD_W_CLEAR();
    memset(sl, 0, sizeof sl);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : ENGI_PHASE, k ? 0u : pi);
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
}
static double cost(int32_t fold)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    track_t *t = &trk[0];
    uint32_t i, rep;
    double best = 1e30;
    fresh(0);
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    t->p[P_E7] = (int16_t)fold;
    for (i = 0; i < 8u; i++)
        trk_note_on(t, NOTES[i], 100);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) {
        uint64_t i0 = instr_now();
        double ipc;
        blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u);
        best = ipc < best ? ipc : best;
    }
    return best;
}
static void test_cost(void)
{
    double c0, c1;
    if (!instr_now()) {
        printf("fold155: cost: no instruction counter on this host (proc_pid_rusage), skipped\n");
        return;
    }
    c0 = cost(0);
    c1 = cost(127);
    printf("fold155: cost: PHASE BRASS 8 voices, the whole mix: FOLD 0 %.0f, FOLD 127 %.0f (%+.1f %%) instructions / sample\n",
           c0, c1, (c1 - c0) * 100 / c0);
    check("cost: FOLD 127 within 20 % of FOLD 0 on an 8-voice part (the mix included)", c1 < c0 * 1.20);
}

/* ------------------------------------------------------------------ demo --- */
static int demo(const char *dir)
{
    static const uint8_t LINE[16] = {36, 36, 48, 36, 39, 36, 46, 43, 36, 48, 36, 51, 36, 43, 46, 48};
    static const uint8_t CH[3][4] = {{48, 55, 60, 64}, {45, 52, 57, 64}, {41, 48, 53, 60}};
    static const int16_t FOLDC[3] = {0, 40, 90};
    char path[512];
    FILE *f;
    track_t *t = &trk[0];
    uint32_t k, j, i, st = FS / 8u / CTL, chord = 2u * FS / CTL, total = 64u * st + 3u * chord + FS / CTL;
    snprintf(path, sizeof path, "%s/phase_fold.wav", dir);
    if (!(f = fopen(path, "wb")))
        return 0;
    wav_hdr(f, total * CTL);
    fresh(0);                                         /* BRASS as a bass: SAW, DCW low, MONO */
    t->p[P_E2] = 20;
    t->p[P_E3] = 40;
    t->p[P_E6] = 40;                                  /* SUB under the folds */
    t->p[P_LEVEL] = 110;
    for (k = 0; k < 64u; k++) {                       /* FOLD 0 -> 127 -> 0 over 8 s */
        t->p[P_E7] = (int16_t)((k < 32u ? k : 63u - k) * 127u / 31u);
        trk_note_on(t, LINE[k & 15u], (k & 3u) ? 90 : 120);
        for (j = 0; j < st; j++) {
            if (j == st / 2u)
                trk_note_off(t, LINE[k & 15u]);
            mix_block(out_buf, CTL);
            for (i = 0; i < CTL; i++)
                wav_put(f, out_buf[2u * i], out_buf[2u * i + 1u]);
        }
    }
    host_preset(t, ENGI_PHASE, 0);                     /* BRASS chords at FOLD 0, 40, 90 */
    t->p[P_VOICE] = V_POLY;
    for (k = 0; k < 3u; k++) {
        t->p[P_E7] = FOLDC[k];
        for (i = 0; i < 4u; i++)
            trk_note_on(t, CH[k][i], 100);
        for (j = 0; j < chord; j++) {
            if (j == chord * 3u / 4u)
                for (i = 0; i < 4u; i++)
                    trk_note_off(t, CH[k][i]);
            mix_block(out_buf, CTL);
            for (i = 0; i < CTL; i++)
                wav_put(f, out_buf[2u * i], out_buf[2u * i + 1u]);
        }
    }
    for (j = 0; j < FS / CTL; j++) {
        mix_block(out_buf, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(f, out_buf[2u * i], out_buf[2u * i + 1u]);
    }
    fclose(f);
    printf("fold155: demo %s\n", path);
    return 1;
}

int main(int argc, char **argv)
{
    test_param();
    test_identical();
    test_bounded_dc();
    test_bright();
    test_switch();
    test_cost();
    if (argc > 1)
        check("demo written", demo(argv[1]));
    printf("fold155: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
