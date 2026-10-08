/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ugotworms */
/* The ADPCM engine (eng_adpcm.c, FELUCCA_ADPCM=1), on the host:
 *   build/host/adpcm_test REF.bin OUTDIR
 * 1. the voice against snes_spc: every scenario of adpcm_cases.h on adpcm_dsp.c's voice, compared
 *    sample for sample with REF.bin (tests/adpcm_ref.cpp: the same scenarios on SPC_DSP.cpp). The key-on
 *    and key-off are polled every other sample there and its output is a sample behind, so the
 *    key-on sample, the key-off's and the output's lag are searched (a few samples); the rate counter
 *    runs from the reset on both sides, so a match is exact or nothing.
 *    The echo cases run adpcm_dsp.c's echo too (line, FIR, feedback) against the original's.
 * 2. the engine in the mix (voice.c, fx.c): every preset, a chord held 1 s then released: its peak,
 *    no clipping, every voice free after the release, the echo idle after its tail; the pitch register
 *    of the keys; the cost of 8 voices next to other engines'. WAVs in OUTDIR. The echo line lives in
 *    PHYS's memory: ADPCM after PHYS, and PHYS after ADPCM, sound as from boot, sample for sample.
 * 3. a 4-part demo in OUTDIR/adp_demo.wav: pad, bass, lead and the noise hat. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#undef inst                                     /* hostsim's part 1: here, the instrument fields */
#include "adpcm_cases.h"

#define ENGI_ADPCM 14u
static int bad;
static void check(const char *what, int ok)
{
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    bad += !ok;
}

/* ------------------------------------------------- 1. against snes_spc --- */
static void run_case(const adp_case_t *c, int kon_s, int koff_s, int16_t *o)
{
    adp_chv_t v;
    adp_echo_t ec;
    static int16_t line[ADP_EDL_MAX * 512u];
    static uint8_t brr[ADP_BRR_LEN];
    int32_t counter = 0, noise = 0x4000, s;     /* as SPC_DSP::reset leaves them */
    memset(&v, 0, sizeof v);
    memset(&ec, 0, sizeof ec);
    memset(line, 0, sizeof line);
    adp_case_brr(c, brr);
    v.ram = brr;
    v.adsr0 = c->adsr0;
    v.adsr1 = c->adsr1;
    v.gain = c->gain;
    v.env_mode = SE_RELEASE;
    for (s = 0; s < c->n; s++) {
        int koff = 0;
        if (s == koff_s) {
            if (c->rel_gain) {
                v.adsr0 &= 0x7Fu;
                v.gain = c->rel_gain;
            } else {
                koff = 1;
            }
        }
        uint32_t fire;
        if (--counter < 0)                          /* SPC_DSP misc_30 */
            counter = ADP_CTR_RANGE - 1;
        fire = adp_fire_of(counter);
        if (fire >> c->nrate & 1u)
            adp_noise_step(&noise);
        o[s] = (int16_t)adp_tick(&v, fire, noise, c->pitch, c->non, koff);
        if (c->echo) {                              /* the reference's voice volume is -128: -o into the echo */
            int32_t e = adp_echo_tick(&ec, line, (o[s] * -128) >> 7, c->evol, c->efb, c->fir, c->edl, 1);
            o[s] = (int16_t)adp_clamp16(o[s] + e);
        }
        if (s == kon_s) {                           /* KON, seen at the end of this sample's V3c */
            v.kon_delay = 5;
            v.env_mode = SE_ATTACK;
        }
    }
}

/* the engine's rate countdowns and noise (adp_block) against SPC_DSP's counter, 3 of its periods */
static void counters(void)
{
    uint32_t b, k, ticks = 0, bad_fire = 0, bad_noise = 0;
    int32_t counter = 0, noise = 0x4000;
    char msg[160];
    memset(adp_dsp, 0, sizeof adp_dsp);
    trk[3].p[P_E3] = 17;
    for (b = 0; ticks < 3u * ADP_CTR_RANGE; b++) {
        uint32_t ph = adp_dsp[3].ph, n = 0;
        for (k = 0; k < CTL; k++)                   /* the DSP samples of this block */
            if ((ph += ADP_RATE_Q16) >= 0x10000u) {
                ph -= 0x10000u;
                n++;
            }
        adp_block(&trk[3]);
        for (k = 0; k < n; k++, ticks++) {
            uint32_t fire;
            if (--counter < 0)
                counter = ADP_CTR_RANGE - 1;
            fire = adp_fire_of(counter);
            if (fire >> 17 & 1u)
                adp_noise_step(&noise);
            bad_fire += adp_dsp[3].fire[k] != fire;
            bad_noise += adp_dsp[3].nz[k] != (int16_t)noise;
        }
    }
    snprintf(msg, sizeof msg, "rate countdowns and noise = SPC_DSP's counter over %u samples (%u, %u differ)", ticks,
             bad_fire, bad_noise);
    check(msg, !bad_fire && !bad_noise);
}

static void against_ref(const char *path)
{
    FILE *f = fopen(path, "rb");
    static int16_t ref[1 << 17], mine[1 << 17];
    uint32_t k;
    printf("1. the voice against snes_spc (%s)\n", path);
    if (!f) {
        check("reference file readable", 0);
        return;
    }
    for (k = 0; k < ADP_NCASES; k++) {
        const adp_case_t *c = &ADP_CASES[k];
        int32_t n = 0, kon, kd, lag, best = 0x7FFFFFFF, bk = 0, bd = 0, bl = 0, nz = 0, peak = 0, i;
        char msg[200];
        if (fread(&n, 4, 1, f) != 1 || n != c->n || fread(ref, 2, (size_t)n, f) != (size_t)n) {
            check("reference file matches adpcm_cases.h", 0);
            break;
        }
        for (i = 0; i < n; i++) {
            nz += ref[i] != 0;
            peak = ref[i] > peak ? ref[i] : -ref[i] > peak ? -ref[i] : peak;
        }
        for (kon = 0; kon < 3 && best; kon++)
            for (kd = 0; kd < 3 && best; kd++) {
                run_case(c, kon, c->koff_at < 0 ? -1 : c->koff_at + kd, mine);
                for (lag = 0; lag < 4 && best; lag++) {
                    int32_t diff = 0;
                    for (i = 0; i + lag < n; i++)
                        diff += mine[i] != ref[i + lag];
                    if (diff < best) {
                        best = diff;
                        bk = kon;
                        bd = kd;
                        bl = lag;
                    }
                }
            }
        snprintf(msg, sizeof msg, "%-58s %s (key-on %d, key-off +%d, lag %d; %d of %d samples sound, peak %d)",
                 c->what, best ? "DIFFERS" : "exact", bk, bd, bl, nz, n, peak);
        if (best)
            printf("       %d samples differ\n", best);
        check(msg, !best && nz > n / 20);
    }
    fclose(f);
}

/* ------------------------------------------------- 2. the engine in the mix --- */
static FILE *wav_open(const char *dir, const char *name, uint32_t frames)
{
    char p[512];
    FILE *w;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if ((w = fopen(p, "wb")))
        wav_hdr(w, frames);
    return w;
}

static uint32_t voices_on(void)
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active;
    return n;
}

static uint32_t adp_preset(const char *name)           /* an ADPCM preset's index by its name */
{
    uint32_t i;
    for (i = 0; i < ENGINES[ENGI_ADPCM]->npresets; i++)
        if (!strcmp(ENGINES[ENGI_ADPCM]->presets[i].name, name))
            return i;
    fprintf(stderr, "adpcm_test: no preset %s\n", name);
    exit(2);
}

static void presets(const char *dir)
{
    uint32_t pi;
    printf("2. the engine in the mix\n");
    for (pi = 0; pi < ENGINES[ENGI_ADPCM]->npresets; pi++) {
        const preset_t *pr = &ENGINES[ENGI_ADPCM]->presets[pi];
        static const uint8_t CH[3] = {60, 64, 67};
        uint32_t f, i, frames = 6u * FS, nk = pr->mono ? 1u : 3u, rel = FS / CTL * CTL, freed = 0, eidle = 0;   /* (a block's start) */
        int32_t peak = 0, clip = 0;
        char nm[64], msg[200];
        FILE *w;
        for (i = 0; pr->name[i] && i < 40u; i++)
            nm[i] = pr->name[i] == ' ' ? '_' : pr->name[i];
        nm[i] = 0;
        strcat(nm, ".wav");
        memset(trk, 0, sizeof trk);
        memset(adp_v, 0, sizeof adp_v);
        memset(adp_dsp, 0, sizeof adp_dsp);
        host_tracks_init();
        host_preset(&trk[0], ENGI_ADPCM, pi);
        w = wav_open(dir, nm, frames);
        for (i = 0; i < nk; i++)
            trk_note_on(&trk[0], CH[i], 100);
        for (f = 0; f < frames; f += CTL) {
            int32_t o[2 * CTL];
            if (f == rel)
                for (i = 0; i < nk; i++)
                    trk_note_off(&trk[0], CH[i]);
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                int32_t a = o[i] < 0 ? -o[i] : o[i];
                peak = a > peak ? a : peak;
                clip += a >= 30000;
            }
            if (w)
                for (i = 0; i < CTL; i++)
                    wav_put(w, o[2 * i], o[2 * i + 1]);
            if (f > rel && !freed && !voices_on())
                freed = f - rel;
            if (freed && !eidle && !adp_dsp[0].edirty)
                eidle = f - rel;
        }
        if (w)
            fclose(w);
        if (!eidle) {                                   /* what keeps the echo on */
            const adp_dsp_t *d = &adp_dsp[0];
            printf("       echo: in %d wr %d quiet %u len %u dirty %u own %u; line[0..7]:", d->ec.in, d->ec.wr, d->quiet,
                   d->ec.len, d->edirty, d->eown);
            for (i = 0; i < 8u; i++)
                printf(" %d", adp_eline(0)[i]);
            printf("\n");
        }
        for (i = 0; i < NVOICE && !freed; i++)          /* what holds a voice */
            if (trk[0].v[i].active) {
                const adp_voice_t *sv = &adp_v[0][i];
                printf("       voice %u: note %u gate %u stage %u | env %d mode %u kon %u down %u adsr0 %02X gain %02X\n",
                       i, trk[0].v[i].note, trk[0].v[i].gate, trk[0].v[i].stage, sv->c.env, sv->c.env_mode,
                       sv->c.kon_delay, sv->down, sv->c.adsr0, sv->c.gain);
            }
        snprintf(msg, sizeof msg, "%-10s peak %5d, %d >= 30000, voices free %4u ms, echo idle %4u ms after the release -> %s",
                 pr->name, peak, clip, freed * 1000u / FS, eidle * 1000u / FS, nm);
        check(msg, peak > 250 && !clip && freed && freed < 3u * FS && eidle);
    }
    {                                               /* the pitch register of the keys (SQUARE) */
        vmod_t m;
        adp_voice_t sv;
        int32_t root = ADP_INST[0].root16;
        memset(&m, 0, sizeof m);
        memset(&sv, 0, sizeof sv);
        host_preset(&trk[0], ENGI_ADPCM, adp_preset("SQUARE LEAD"));
        trk[0].p[P_E1] = 0;
        m.pitch16 = root;
        {
            uint32_t p0 = adp_pitch(&trk[0], &sv, &m);
            uint32_t p12, p24, pm24;
            m.pitch16 = root + 192;
            p12 = adp_pitch(&trk[0], &sv, &m);
            m.pitch16 = root + 384 + 16;
            p24 = adp_pitch(&trk[0], &sv, &m);
            m.pitch16 = root - 384;
            pm24 = adp_pitch(&trk[0], &sv, &m);
            char msg[160];
            snprintf(msg, sizeof msg, "pitch register: root 0x%04X, +1 oct 0x%04X, +2 oct+ 0x%04X (14-bit cap), -2 oct 0x%04X",
                     p0, p12, p24, pm24);
            check(msg, p0 >= 0xFF8 && p0 <= 0x1008 && p12 >= 0x1FF0 && p12 <= 0x2010 && p24 == 0x3FFF &&
                           pm24 >= 0x3FC && pm24 <= 0x404);
        }
    }
    {                                               /* cost: 8 sustained voices, next to the other engines' */
        static const struct { uint32_t e, p; const char *nm; } B[] = {
            {ENGI_SAMPLE, 2, "SAMPLE FLUTE (loops)"}, {0, 0, "ANALOG SAW LEAD"}, {ENGI_FM6, 4, "FM6 PAD"},
            {ENGI_ADPCM, 0, "ADPCM ECHO PAD"}, {ENGI_ADPCM, 0, "ADPCM SQUARE LEAD"}};
        uint32_t k;
        for (k = 0; k < NELEM(B); k++) {
            uint64_t t0, best = ~0ull;
            uint32_t r, f;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], B[k].e, B[k].e == ENGI_ADPCM ? adp_preset(B[k].nm + 6) : B[k].p);
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            trk[0].p[P_VOICE] = V_POLY;
            trk[0].p[P_SUS] = 127;
            for (r = 0; r < 8u; r++)
                trk_note_on(&trk[0], 48 + r * 3, 100);
            for (r = 0; r < 5; r++) {
                int32_t o[2 * CTL];
                t0 = now_ns();
                for (f = 0; f < FS / 4u; f += CTL)
                    mix_block(o, CTL);
                if (now_ns() - t0 < best)
                    best = now_ns() - t0;
            }
            printf("  info %-22s %u voices sounding at the end, host ns per output sample %.1f\n", B[k].nm,
                   voices_on(), (double)best / (FS / 4u));
        }
    }
}

/* ------------------------------------------------- the echo line in PHYS's memory --- */
/* part 1's own output (no FX buses, no master: they keep their own state) for `blocks` blocks, a note at 0
 * released at the middle; mix_blocks counted as mix_block does */
static void part_blocks(int32_t *dst, uint32_t blocks, uint32_t note)
{
    uint32_t b;
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < blocks; b++) {
        if (b == blocks / 2u)
            trk_note_off(&trk[0], note);
        mix_blocks++;
        track_render(&trk[0], dst + b * CTL, CTL);
    }
}

static void fresh(void)
{
    memset(trk, 0, sizeof trk);
    memset(adp_v, 0, sizeof adp_v);
    memset(adp_dsp, 0, sizeof adp_dsp);
    memset(&phys_mem, 0, sizeof phys_mem);
    host_tracks_init();
}

static void sharing(void)
{
    enum { NB = 2000 };                             /* 1.45 s */
    static int32_t a[NB * CTL], b[NB * CTL], junk[CTL * NB];
    uint32_t i, diff, dirty;
    int32_t pk;
    char msg[160];
    /* ADPCM (echo) after PHYS has filled the memory: the same as ADPCM from boot */
    fresh();
    host_preset(&trk[0], ENGI_ADPCM, adp_preset("ECHO PAD"));   /* echo, feedback */
    trk[0].p[P_E5] = ADP_EDL_MAX;                  /* (the whole line)*/
    part_blocks(a, NB, 60);
    fresh();
    host_preset(&trk[0], ENGI_PHYS, 7);             /* DRONE STRING: the largest state */
    for (i = 0; i < 6u; i++)
        trk_note_on(&trk[0], 48 + i * 5, 120);
    for (i = 0; i < 400u; i++) {
        mix_blocks++;
        track_render(&trk[0], junk, CTL);
    }
    for (i = dirty = 0; i < NELEM(phys_mem[0].shared); i++)
        dirty += phys_mem[0].shared[i] != 0;
    host_preset(&trk[0], ENGI_ADPCM, adp_preset("ECHO PAD"));   /* as a project load: the engine at once */
    trk[0].p[P_E5] = ADP_EDL_MAX;
    memset(trk[0].v, 0, sizeof trk[0].v);
    memset(&adp_dsp[0], 0, sizeof adp_dsp[0]);    /* (the DSP's clock from 0, as the first run's) */
    adp_dsp[0].init = 0;
    mix_blocks += 5;                                /* blocks the part was not ADPCM */
    part_blocks(b, NB, 60);
    for (i = 0, pk = 0, diff = 0; i < NB * CTL; i++) {
        diff += a[i] != b[i];
        pk = a[i] > pk ? a[i] : -a[i] > pk ? -a[i] : pk;
    }
    snprintf(msg, sizeof msg, "ADPCM with echo after PHYS left %u words in the memory = ADPCM from boot (%u differ, peak %d)",
             dirty, diff, pk);
    check(msg, !diff && dirty > 1000u && pk > 1000);
    /* PHYS after the echo has filled the memory: the same as PHYS from boot */
    fresh();
    host_preset(&trk[0], ENGI_PHYS, 7);
    rng_state = 0x1234567u;
    part_blocks(a, NB, 52);
    fresh();
    host_preset(&trk[0], ENGI_ADPCM, adp_preset("ECHO PAD"));   /* echo on */
    trk[0].p[P_E5] = ADP_EDL_MAX;
    trk[0].p[P_E6] = 60;
    part_blocks(junk, NB, 60);
    for (i = diff = 0; i < ADP_EDL_MAX * 512u; i++)
        diff += adp_eline(0)[i] != 0;
    host_preset(&trk[0], ENGI_PHYS, 7);
    memset(trk[0].v, 0, sizeof trk[0].v);
    rng_state = 0x1234567u;
    part_blocks(b, NB, 52);
    {
        uint32_t filled = diff;
        for (i = 0, pk = 0, diff = 0; i < NB * CTL; i++) {
            diff += a[i] != b[i];
            pk = a[i] > pk ? a[i] : -a[i] > pk ? -a[i] : pk;
        }
        snprintf(msg, sizeof msg, "PHYS after the echo line filled %u samples = PHYS from boot (%u differ, peak %d)", filled,
                 diff, pk);
        check(msg, !diff && filled > 1000u && pk > 1000);
    }
}

/* ------------------------------------------------- 3. a demo --- */
typedef struct { uint16_t step; uint8_t part, note, len; } ev_t;
static void demo(const char *dir)
{
    /* 12 bars, 16th steps at 120 BPM: 0 ECHO PAD (the chords), 1 SUB BASS, 2 SQUARE LEAD (the tune from bar 5),
     * 3 NOISE HAT (8ths, 16ths before the tune) */
    static const char *const PRE[4] = {"ECHO PAD", "SUB BASS", "SQUARE LEAD", "NOISE HAT"};
    static const uint8_t CHORD[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 59, 62}, {52, 55, 59}};   /* Am F G Em */
    static const uint8_t BASS[4] = {45, 41, 43, 40};
    static const uint8_t LEAD[64] = {76, 0, 0, 72, 0, 74, 76, 0, 79, 0, 77, 0, 76, 0, 72, 0,
                                     74, 0, 0, 71, 0, 72, 74, 0, 76, 0, 0, 0, 0, 0, 0, 0,
                                     72, 0, 0, 76, 0, 79, 81, 0, 79, 0, 77, 0, 76, 0, 74, 0,
                                     76, 0, 74, 0, 72, 0, 71, 0, 69, 0, 0, 0, 0, 0, 0, 0};
    ev_t ev[1200];
    uint32_t nev = 0, bar, s, f, i, step_len = FS * 60u / 120u / 4u, steps = 12u * 16u, frames = (steps + 32u) * step_len;
    int32_t peak = 0;
    FILE *w;
    for (bar = 0; bar < 12u; bar++) {
        uint32_t c = bar % 4u, b0 = bar * 16u;
        for (i = 0; i < 3u; i++)
            ev[nev++] = (ev_t){(uint16_t)b0, 0, CHORD[c][i], 15};
        for (s = 0; s < 16u; s += 2u)                   /* bass: root, its octave on the off-beats (TUNE -12) */
            ev[nev++] = (ev_t){(uint16_t)(b0 + s), 1, (uint8_t)(BASS[c] + ((s & 6u) == 4u ? 12u : 0u)), 1};
        for (s = 0; s < 16u; s += bar == 3u ? 1u : 2u)  /* hats: 8ths, 16ths into the tune */
            ev[nev++] = (ev_t){(uint16_t)(b0 + s), 3, 60, 1};
        if (bar >= 4u)
            for (s = 0; s < 16u; s++) {
                uint32_t n = LEAD[((bar - 4u) % 4u) * 16u + s], len = 1;
                if (!n)
                    continue;
                while (s + len < 16u && !LEAD[((bar - 4u) % 4u) * 16u + s + len])
                    len++;
                ev[nev++] = (ev_t){(uint16_t)(b0 + s), 2, (uint8_t)n, (uint8_t)len};
            }
    }
    memset(trk, 0, sizeof trk);
    memset(adp_v, 0, sizeof adp_v);
    memset(adp_dsp, 0, sizeof adp_dsp);
    host_tracks_init();
    for (i = 0; i < 4u; i++) {
        host_preset(&trk[i], ENGI_ADPCM, adp_preset(PRE[i]));
        trk[i].p[P_LEVEL] = i == 0 ? 75 : i == 3 ? 85 : 100;
    }
    trk[0].p[P_PAN] = -20;
    trk[2].p[P_PAN] = 15;
    w = wav_open(dir, "adp_demo.wav", frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t st = f / step_len;
        if (f % step_len < CTL)                     /* this block starts a step */
            for (i = 0; i < nev; i++) {
                if (ev[i].step == st)
                    trk_note_on(&trk[ev[i].part], ev[i].note, ev[i].part == 2 ? 110 : 100);
                if (ev[i].step + ev[i].len == st)
                    trk_note_off(&trk[ev[i].part], ev[i].note);
            }
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            peak = o[i] > peak ? o[i] : -o[i] > peak ? -o[i] : peak;
        if (w)
            for (i = 0; i < CTL; i++)
                wav_put(w, o[2 * i], o[2 * i + 1]);
    }
    if (w)
        fclose(w);
    {
        char msg[120];
        snprintf(msg, sizeof msg, "4-part demo, %u s, peak %d -> adp_demo.wav", frames / FS, peak);
        check(msg, peak > 1000 && peak < 32000);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: adpcm_test REF.bin OUTDIR\n");
        return 2;
    }
    if (ENGINES[ENGI_ADPCM] != &ENG_ADPCM) {
        fprintf(stderr, "adpcm_test: engine %u is not ADPCM\n", ENGI_ADPCM);
        return 2;
    }
    against_ref(argv[1]);
    counters();
    presets(argv[2]);
    sharing();
    printf("3. a demo\n");
    demo(argv[2]);
    printf(bad ? "adpcm_test: %d FAILED\n" : "adpcm_test: all passed\n", bad);
    return bad != 0;
}
