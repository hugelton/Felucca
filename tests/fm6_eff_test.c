/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6 effective-patch cache lifecycle (#419): the patch the audio side renders (fm6_eff, the patch through the macros)
 * must equal the one the main loop published (fm6_patch) after a supported engine switch, however many patch updates
 * were accepted while the track ran another engine.
 *
 * Path under test: an accepted editor ED_FM6_PUT (or a project load, or a factory slot load) on a track that runs
 * another engine -> the track fades out of FM6 -> that many updates (enough that an eight-bit generation counter
 * returns to the value the cache was made at) -> the normal UI selection of FM6 (ui.c set_engine_of: the first preset
 * loads, one more update) -> the audio blocks switch the engine -> the first note. The effective patch is then checked
 * against one rebuilt from the published patch and the track's macros, and the note against the same note from a
 * cache rebuilt first.
 *
 * Build: the editor_test.c flags; -DFELUCCA_CPU2=0 for a build without the second core. With the second core, the
 * cases run once with a thread as core 1 (tests/c1_host.h) and once on one core (MENU > SYSTEM > 2ND CORE OFF).
 *   build/host/fm6_eff_test */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define FELUCCA_OTA 1
#define FELUCCA_FLASH 0
#define FELUCCA_VERSION "TEST"
#define FELUCCA_CDC 1
#if !defined(FELUCCA_CPU2) || FELUCCA_CPU2
#include "c1_host.h"
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t host_progress = 1;
static uint8_t host_wire[4096];
static uint32_t host_wire_n;
static void host_drain(void)
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ], cin = p & 15u, i;
        uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n && host_wire_n < sizeof host_wire; i++)
            host_wire[host_wire_n++] = (uint8_t)(p >> (8u * (i + 1u)));
    }
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { host_drain(); fm1_ms++; }
static void fm1_wdt_feed(void)
{
    fm1_ms++;
    if (host_progress && transport_req == 2u) { seq_stop(); transport_req = 0; }
}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static uint32_t flash_ok = 1;
static void audio_silence(void) {}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint8_t *host_flash_ptr(uint32_t off) { return &host_samples[0][0] + off - SMP_USER_BASE; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    memset(host_flash_ptr(off), 0xFF, 4096); *took = 0; return 0;
}
static int fl_write(uint32_t off, const void *p, uint32_t n)
{
    memcpy(host_flash_ptr(off), p, n); return 0;
}
static int st_read(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
#include "../firmware/src/storage.c"
#include "../firmware/src/editor.c"

static int bad, nchecks;
static void check(const char *what, int ok)
{
    nchecks++;
    if (!ok)
        printf("fm6_eff: %-100s FAIL\n", what);
    bad += !ok;
}

static void reset(void)
{
    uint32_t t, i;
    memset(&song, 0, sizeof song); memset(trk, 0, sizeof trk);
    memset(&chain, 0, sizeof chain); chain_defaults(&chain_config);
    memset(&ed_w, 0, sizeof ed_w); memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings); settings_init();
    memset(proj_slot, 0, sizeof proj_slot); memset(up_bank, 0, sizeof up_bank);
    memset(&um, 0, sizeof um); memset(usr_nz, 0, sizeof usr_nz);
    memset(eng_mem, 0, sizeof eng_mem); memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    memset(fm6_eff, 0, sizeof fm6_eff); memset(fm6_lfo, 0, sizeof fm6_lfo);
    host_progress = 1; host_wire_n = 0;
    transport_req = panic_req = 0; sx_ready = sx_collect = sx_busy = 0;
    so_r = so_w = mi_r = mi_w = 0; midi_in_overflow = 0; usb.config = 1;
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    song.master_q12 = 4096;
    for (t = 0; t < NTRK; t++) {
        track_defaults(&trk[t]); set_engine_of(&trk[t], 0); apply_preset_to(&trk[t], 0);
        trk[t].engine = trk[t].eng_req; track_defaults_steps(&trk[t]);
        trk[t].p[P_CHOR] = trk[t].p[P_DLY] = trk[t].p[P_REV] = trk[t].p[P_DIST] = 0;
        trk[t].p[P_VOICE] = V_POLY;
    }
}

static uint32_t request(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i;
    host_wire_n = 0; ota_frame_done();
    sysex_byte(0xF0); sysex_byte(ED_HDR0); sysex_byte(ED_HDR1); sysex_byte(ED_HDR2); sysex_byte((uint8_t)cmd);
    for (i = 0; i < n; i++) sysex_byte(a[i]);
    sysex_byte(0xF7); ed_service(); host_drain();
    return host_wire_n;
}

/* one accepted ED_FM6_PUT of the packed patch pk into track tr (rc 0 in the reply) */
static int fm6_put(uint32_t tr, const uint8_t *pk)
{
    uint8_t a[2 + FM6_PACKED];
    a[0] = ED_FM6_TRACK; a[1] = (uint8_t)tr;
    memcpy(a + 2, pk, FM6_PACKED);
    request(ED_FM6_PUT, a, sizeof a);
    return host_wire_n == 9u && host_wire[7] == 0;
}

static void blocks(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
}

/* a distinct packed patch: factory f, named by n */
static void make_pk(uint8_t *pk, uint32_t f, uint32_t n)
{
    uint32_t i;
    memcpy(pk, FM6_FACTORY[f % FM6_NFACTORY], FM6_PACKED);
    for (i = 0; i < 4u; i++, n /= 10u)
        pk[118 + 6u - i] = (uint8_t)('0' + n % 10u);
}

/* the effective patch rebuilt from the published one (a cold cache), against the one the audio side holds */
static int eff_matches(uint32_t tr)
{
    typeof(fm6_eff[0]) held = fm6_eff[tr];
    fm6_lfo_t lfo = fm6_lfo[tr];
    int ok;
    memset(&fm6_eff[tr], 0, sizeof fm6_eff[tr]);
    fm6_sync(&trk[tr]);
    ok = held.ok && !memcmp(held.p, fm6_eff[tr].p, FP_SIZE) && held.alg == fm6_eff[tr].alg &&
         held.fb == fm6_eff[tr].fb && !memcmp(held.dt, fm6_eff[tr].dt, sizeof held.dt) &&
         !memcmp(held.e, fm6_eff[tr].e, sizeof held.e);
    fm6_eff[tr] = held;
    fm6_lfo[tr] = lfo;
    return ok;
}

/* First-note engine PCM from identical track state (as fm6_test.c). Engine switching above uses mix_block;
 * keep unrelated mixer/FX tails out of this comparison. Assert nonzero audio so a muted mixer cannot hide the bug. */
static uint32_t note_hash(uint32_t tr, int cold)
{
    uint32_t i, k, h = 2166136261u, nonzero = 0;
    track_t saved = trk[tr];
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &trk[tr].v[i];
        v->active = v->gate = v->env = v->env_out = 0;
        v->stage = 0;
    }
    memset(ENG_MEM(tr), 0, sizeof(fm6_note_t) * FM6_POLY);
    if (cold)
        memset(&fm6_eff[tr], 0, sizeof fm6_eff[tr]);
    fm6_lfo[tr] = (fm6_lfo_t){0};
    trk_note_on(&trk[tr], 57, 100);
    for (i = 0; i < 8u; i++) {
        int32_t o[CTL];
        track_render(&trk[tr], o, CTL);
#if FELUCCA_CPU2
        track_lfo_end(&trk[tr]);
#endif
        for (k = 0; k < CTL; k++) {
            nonzero |= o[k] != 0;
            h = (h ^ (uint32_t)o[k]) * 16777619u;
        }
    }
    check("  the first-note comparison contains audio", nonzero);
    trk[tr] = saved;
    return h;
}

enum { VIA_PUT, VIA_PROJECT, VIA_SLOT };
static const char *VIA_NAME[] = {"editor PUT", "project load", "factory slot"};

static void update(uint32_t via, uint32_t tr, uint32_t n)
{
    uint8_t pk[FM6_PACKED];
    if (via == VIA_PUT) {
        make_pk(pk, 1u + n % 5u, n);
        check("  the PUT is accepted (rc 0)", fm6_put(tr, pk));
    } else if (via == VIA_SLOT) {
        fm6_load_slot(tr, 1u + n % 5u);
        fm6_adopt(tr);
    } else {
        static project_t p;
        project_capture(&p);
        make_pk(pk, 1u + n % 5u, n);
        memcpy(p.fm6[tr], pk, FM6_PACKED);
        proj_bound(&p);
        check("  the project loads", !project_restore_runtime(&p));
    }
}

/* ntr tracks on FM6 with patch A; they leave FM6; `extra` + enough updates to bring an eight-bit generation back
 * to the cache's; back by the UI; the first note. macros: 0 none (E0..E6 as the first preset has them), 1 nonzero and
 * dialled back to what the cache was made with, 2 nonzero and left at the preset's (a control: the macros differ) */
static void scenario(uint32_t ntr, uint32_t macros, uint32_t via, uint32_t extra, uint32_t core)
{
    uint32_t tr, i, n = 0;
    uint8_t pa[FM6_PACKED];
    char what[160];
    static const int16_t X[7] = {0, 2, -9, 3, -12, 2, 20};
    uint32_t alias = 0;
    reset();
#if FELUCCA_CPU2
    c1h_off = !core;
#else
    (void)core;
#endif
    for (tr = 0; tr < ntr; tr++) {
        set_engine_of(&trk[tr], ENGI_FM6);
        trk[tr].engine = trk[tr].eng_req;
        eng_mem_claim(&trk[tr]);
    }
    blocks(2);
    for (tr = 0; tr < ntr; tr++) {                    /* patch A (not the first preset's), macros as asked */
        make_pk(pa, 6u + tr, 900u + tr);
        check("  patch A accepted", fm6_put(tr, pa));
        if (macros)
            for (i = 0; i < 7u; i++)
                trk[tr].p[P_E0 + i] = X[i];
    }
    for (tr = 0; tr < ntr; tr++)
        trk_note_on(&trk[tr], 60, 100);
    blocks(6);
    for (tr = 0; tr < ntr; tr++)
        check("  the cache holds A", fm6_eff[tr].ok && eff_matches(tr) && !memcmp(fm6_eff[tr].p + FP_NAME, fm6_patch[tr] + FP_NAME, 10));
    for (tr = 0; tr < ntr; tr++) {                    /* away: ANALOG, notes held through the fade */
        set_engine_of(&trk[tr], 0);
    }
    blocks(14);
    for (tr = 0; tr < ntr; tr++) {
        check("  the track runs ANALOG after the fade", trk[tr].engine == 0u);
        check("  the audio side dropped the cache outside FM6", !fm6_eff[tr].ok);
    }
    for (tr = 0; tr < ntr; tr++) {
        uint32_t g0 = fm6_eff[tr].gen;
        n = 0;
        /* latent updates until the first preset's load (one more) brings the generation to the cache's, +256 * extra */
        while (((uint8_t)(fm6_pgen[tr] + 1u - g0)) != 0u || n < extra * 256u) {
            update(via, tr, n++);
            if ((n & 63u) == 0)
                blocks(1);                            /* (the audio side runs meanwhile; ANALOG calls no fm6_sync) */
            if (n > 2000u)
                break;
        }
    }
    for (tr = 0; tr < ntr; tr++) {                    /* back by the normal selection: the first preset loads */
        set_engine_of(&trk[tr], ENGI_FM6);
        alias += (uint8_t)(fm6_pgen[tr] - fm6_eff[tr].gen) == 0u;
        if (macros == 1u)                             /* the macros dialled back to what the cache was made with */
            for (i = 0; i < 7u; i++)
                trk[tr].p[P_E0 + i] = X[i];
    }
    snprintf(what, sizeof what, "  calibration: the generation aliased on all %u tracks (%s, +%u x 256)", ntr, VIA_NAME[via], extra);
    check(what, alias == ntr);
    blocks(8);                                        /* the engine switches back; no note yet */
    for (tr = 0; tr < ntr; tr++) {
        uint32_t h1, h2;
        check("  the track runs FM6 again", trk[tr].engine == ENGI_FM6);
        h1 = note_hash(tr, 0);
        check("  the effective patch is the published one through the macros", eff_matches(tr));
        check("  the effective name is the published name, not A's", !memcmp(fm6_eff[tr].p + FP_NAME, fm6_patch[tr] + FP_NAME, 10) &&
              memcmp(fm6_eff[tr].p + FP_NAME, pa + 118, 10));
        h2 = note_hash(tr, 1);
        check("  the first note equals the note from a rebuilt cache", h1 == h2);
    }
}

/* the same track on FM6 throughout: many accepted PUTs, a sound after each batch */
static void repeated(uint32_t ntr, uint32_t macros, uint32_t core)
{
    uint32_t tr, n, i;
    uint8_t pk[FM6_PACKED];
    reset();
#if FELUCCA_CPU2
    c1h_off = !core;
#else
    (void)core;
#endif
    for (tr = 0; tr < ntr; tr++) {
        set_engine_of(&trk[tr], ENGI_FM6);
        trk[tr].engine = trk[tr].eng_req;
        eng_mem_claim(&trk[tr]);
        if (macros)
            for (i = 0; i < 7u; i++)
                trk[tr].p[P_E0 + i] = (int16_t)(i * 3u);
    }
    blocks(1);
    /* No render between these writes: an eight-bit generation aliases even without an engine switch. */
    for (n = 0; n < 256u; n++)
        for (tr = 0; tr < ntr; tr++) {
            make_pk(pk, n + 1u, n + 1000u);
            check("  active burst PUT accepted", fm6_put(tr, pk));
        }
    blocks(1);
    for (tr = 0; tr < ntr; tr++)
        check("  256 active PUTs: the effective patch follows", eff_matches(tr) &&
              !memcmp(fm6_eff[tr].p + FP_NAME, fm6_patch[tr] + FP_NAME, 10));
    for (n = 0; n < 600u; n++) {
        for (tr = 0; tr < ntr; tr++) {
            make_pk(pk, n, n);
            check("  repeated PUT accepted", fm6_put(tr, pk));
        }
        if (n % 37u == 0 || n >= 590u) {
            blocks(1);
            for (tr = 0; tr < ntr; tr++)
                check("  repeated PUTs: the effective patch follows", eff_matches(tr) && !memcmp(fm6_eff[tr].p + FP_NAME, fm6_patch[tr] + FP_NAME, 10));
        }
    }
}

int main(void)
{
    uint32_t ntr, macros, via, extra, core, ncore = FELUCCA_CPU2 ? 2u : 1u;
    printf("fm6_eff: CPU2 build %d, eng_ok(FM6) %d\n", FELUCCA_CPU2, eng_ok(ENGI_FM6));
    for (core = 0; core < ncore; core++)
        for (ntr = 1; ntr <= NTRK; ntr++)
            for (macros = 0; macros < 3u; macros++) {
                for (via = 0; via < 3u; via++)
                    for (extra = 0; extra < (via == VIA_PUT ? 2u : 1u); extra++)
                        scenario(ntr, macros, via, extra, core);
                repeated(ntr, macros, core);
            }
    printf("fm6_eff: %d checks\n", nchecks);
    printf("%s\n", bad ? "FM6 EFF TEST FAILED" : "fm6 effective-cache test passed");
    return bad != 0;
}
