/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* TB-3PO (firmware/src/tb3po.c, ui_tb3po.c), on the real UI sources (the stubs of tests/ui_test.c), against X0X's float
 * generator (tests/ref/: X0X's tb3po.c as it is, its own unit, x0x_tb3po_ref.c):
 * 1. the integer port's pieces: the thresholds (d / 100.0f, 0.35f, 0.25f, 0.5f x 2^30) and tb3_pick against the float
 *    product, for every 24-bit draw and every length 1..7.
 * 2. the same line as X0X's: X0X's defaults; new seeds; generate over random settings (the corners too) and lengths;
 *    chains of MUTATEs (the random stream carried on as X0X carries it).
 * 3. the line in the steps: written and read back; a MUTATE writes only the steps it changes, keeping the rest of each
 *    (VEL, CHANCE, RATCH, NUDGE, chords); a project's FM6 area round trip (pack / unpack, a bad block: the defaults).
 * 4. the UI: ARP on a 303 track opens GENERATE, then SCALE (the arp's pages on another engine); OCT+ a new line (its
 *    seed shown), OCT- a MUTATE, neither the octave; the knobs (NEW LINE's star, not MUTATE's); SAVE held undoes; in a
 *    song refused. The arp does nothing on a 303 track (its mode kept), back on another engine.
 * 5. the sequencer: auto MUTATE every N passes, before step 1 plays, as X0X's (the same steps); none at 0, in a song.
 * 6. a project: a 303 track's settings and seed saved and loaded, its FM6 patch the init one, other tracks' kept.
 * Built and run by tests/run_tests.sh (with tests/ref/x0x_tb3po_ref.c, -ffp-contract=off). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

void ref_generate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags);
void ref_mutate(const uint8_t *cfg, uint32_t seed, int len, uint8_t *notes, uint8_t *flags, uint32_t *rng);
uint32_t ref_new_seed(uint32_t entropy);
void ref_defaults(uint8_t *cfg, uint32_t *seed);

static uint32_t lcg = 0x2545F491u;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 8) % n; }

static void cfg_set(tb3_cfg_t *g, const uint8_t *c, uint32_t seed)
{
    g->density = c[0]; g->accent = c[1]; g->slide = c[2]; g->oct_range = c[3];
    g->root = c[4]; g->scale = c[5]; g->base_oct = c[6]; g->mutate_bars = c[7];
    g->seed = seed;
}
static void cfg_rand(uint8_t *c)                  /* random settings, the corners often */
{
    static const uint8_t EDGE[] = {0, 1, 99, 100};
    uint32_t k;
    for (k = 0; k < 3u; k++)
        c[k] = (uint8_t)(rnd(4) ? rnd(101) : EDGE[rnd(4)]);
    c[3] = (uint8_t)(1u + rnd(3));
    c[4] = (uint8_t)rnd(12);
    c[5] = (uint8_t)rnd(TB3_NSCALES);
    c[6] = (uint8_t)rnd(5);
    c[7] = (uint8_t)rnd(17);
}
static uint32_t seed_rand(void) { return rnd(8) ? (lcg ^ (lcg << 13)) | 1u : 1u + rnd(4); }

/* ---------------------------------------------------- 1. the pieces --- */
static int pieces(void)
{
    int bad = 0, ok = 1;
    uint32_t d, u, len;
    for (d = 0; d <= 100u; d++)
        ok &= (uint32_t)((float)d / 100.0f * 1073741824.0f) == TB3_THR[d];
    ok &= (uint32_t)(0.35f * 1073741824.0f) == TB3_THR_35 && (uint32_t)(0.25f * 1073741824.0f) == TB3_THR_25 &&
          (uint32_t)(0.5f * 1073741824.0f) == TB3_THR_50;
    bad += check("tb3po: the thresholds: (float)d / 100.0f x 2^30 (d 0..100), 0.35f 0.25f 0.5f", ok);
    ok = 1;
    for (len = 1; len <= 7u; len++)
        for (u = 0; u < (1u << 24); u++) {
            float f = (float)u / (float)0x1000000;   /* X0X rng_f, then roll_degree / roll_oct */
            int r = (int)(f * (float)len);
            r = r >= (int)len ? (int)len - 1 : r;
            if ((uint32_t)r != tb3_pick(u, len)) {
                if (ok)
                    printf("  pick u %u len %u: %d, tb3_pick %u\n", u, len, r, tb3_pick(u, len));
                ok = 0;
            }
        }
    bad += check("tb3po: tb3_pick == (int)(u / 2^24 * len) as the float rounds, every u, len 1..7", ok);
    return bad;
}

/* ----------------------------------------------- 2. X0X's lines --- */
static int same_lines(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k, n, seed, r1, r2, diffs = 0;
    uint8_t c[8], rn[64], rf[64];
    tb3_cfg_t g;
    tb3_step_t b[NSTEP];
    ref_defaults(c, &seed);
    tb3_defaults(&g, 0);
    ok = g.density == c[0] && g.accent == c[1] && g.slide == c[2] && g.oct_range == c[3] && g.root == c[4] &&
         g.scale == c[5] && g.base_oct == c[6] && g.mutate_bars == c[7] && g.seed == seed;
    bad += check("tb3po: X0X's defaults (70 / 40 / 25 %, 2 octaves, A minor from A1, seed BEEF)", ok);
    for (i = 0, ok = 1; i < 200000u; i++) {
        uint32_t e = lcg ^ (i * 2654435761u);
        rnd(2);
        ok &= tb3_new_seed(e) == ref_new_seed(e);
    }
    bad += check("tb3po: new seeds == X0X's (200000 entropies)", ok);
    for (i = 0, ok = 1; i < 40000u; i++) {          /* generate */
        int len = (int)(rnd(3) ? 1u + rnd(64) : (uint32_t[]){1, 16, 32, 64}[rnd(4)]);
        cfg_rand(c);
        seed = seed_rand();
        ref_generate(c, seed, len, rn, rf);
        cfg_set(&g, c, seed);
        tb3_generate(&g, b, (uint32_t)len);
        for (k = 0; k < NSTEP; k++)
            if (b[k].note != rn[k] || b[k].flags != rf[k]) {
                if (!diffs++)
                    printf("  generate: seed %08x len %d step %u: %u/%u, X0X %u/%u\n", seed, len, k, b[k].note,
                           b[k].flags, rn[k], rf[k]);
                ok = 0;
                break;
            }
    }
    bad += check("tb3po: generate == X0X's tb3po_generate, every step (40000 settings, seeds, lengths)", ok);
    for (i = 0, ok = 1; i < 3000u; i++) {           /* MUTATE chains */
        int len = (int)(1u + rnd(64));
        cfg_rand(c);
        seed = seed_rand();
        cfg_set(&g, c, seed);
        ref_generate(c, seed, len, rn, rf);
        tb3_generate(&g, b, (uint32_t)len);
        r1 = r2 = rnd(4) ? 0u : seed_rand();
        for (n = 0; n < 24u && ok; n++) {
            if (n == 12u) {                          /* (settings changed halfway: the knobs before an OCT-) */
                cfg_rand(c);
                cfg_set(&g, c, seed);
            }
            ref_mutate(c, seed, len, rn, rf, &r1);
            tb3_mutate(&g, b, (uint32_t)len, &r2);
            ok &= r1 == r2;
            for (k = 0; k < NSTEP; k++)
                if (b[k].note != rn[k] || b[k].flags != rf[k]) {
                    if (!diffs++)
                        printf("  mutate %u: seed %08x len %d step %u\n", n, seed, len, k);
                    ok = 0;
                    break;
                }
        }
    }
    bad += check("tb3po: MUTATE chains == X0X's tb3po_mutate (3000 lines x 24, the stream carried on)", ok);
    return bad;
}

/* ------------------------------------------------- 3. the steps --- */
static int steps(void)
{
    int bad = 0, ok = 1, ok2 = 1, ok3 = 1;
    uint32_t i, k, r;
    uint8_t c[8], pk[FM6_PACKED];
    tb3_cfg_t g;
    tb3_step_t b[NSTEP], back[NSTEP];
    step_t s[NSTEP], was[NSTEP];
    for (i = 0; i < 500u; i++) {
        uint32_t len = 1u + rnd(64);
        cfg_rand(c);
        cfg_set(&g, c, seed_rand());
        tb3_generate(&g, b, len);
        memset(s, 0x5A, sizeof s);
        tb3_write(s, b, 0);
        tb3_read(s, back);
        for (k = 0; k < NSTEP; k++) {
            ok &= back[k].note == b[k].note && back[k].flags == b[k].flags;
            ok &= s[k].probability == 0 && !s[k].hit && !(s[k].flags & ~(SF_ACCENT | SF_SLIDE)) &&
                  (s[k].time == ST_REST ? !s[k].n && !s[k].vel : s[k].n == 1u && s[k].vel == 96u);
        }
        for (k = 0; k < NSTEP; k++) {               /* the rest of the steps: VEL, CHANCE, RATCH, NUDGE, a chord */
            if (s[k].time == ST_REST)
                continue;
            s[k].vel = (uint8_t)(1u + rnd(127));
            s[k].probability = (uint8_t)rnd(101);
            step_set_ratchet(&s[k], 1u + rnd(4));
            step_set_nudge(&s[k], (int32_t)rnd(16) - 8);
            if (!rnd(4)) {
                s[k].n = 3;
                s[k].note[1] = 64;
                s[k].note[2] = 67;
            }
        }
        memcpy(was, s, sizeof s);
        tb3_read(s, back);
        memcpy(b, back, sizeof b);
        r = 0;
        tb3_mutate(&g, b, len, &r);
        tb3_write(s, b, back);
        for (k = 0; k < NSTEP; k++) {
            if (b[k].note == back[k].note && b[k].flags == back[k].flags)
                ok2 &= !memcmp(&s[k], &was[k], sizeof s[k]);   /* untouched */
            else if ((b[k].flags & TB3_GATE) != TB3_REST && was[k].time != ST_REST)
                ok2 &= s[k].vel == was[k].vel && s[k].probability == was[k].probability &&
                       step_ratchet(&s[k]) == step_ratchet(&was[k]) && step_nudge(&s[k]) == step_nudge(&was[k]);
        }
        tb3_read(s, back);
        for (k = 0; k < NSTEP; k++)
            ok2 &= back[k].note == b[k].note && back[k].flags == b[k].flags;
    }
    bad += check("tb3po: a line into the steps and back (NOTE 1 note VEL 96, REST keeps its note, n 0)", ok);
    bad += check("tb3po: a MUTATE writes only the steps it changes, keeping their VEL CHANCE RATCH NUDGE", ok2);
    for (i = 0; i < 2000u; i++) {                   /* the project's FM6 area */
        cfg_rand(c);
        cfg_set(&tb3_cfg[1], c, i ? lcg | 1u : 0xFFFFFFFFu);
        g = tb3_cfg[1];
        tb3_pack(1, pk);
        for (k = 0; k < FM6_PACKED; k++)
            ok3 &= pk[k] <= 127u;
        memset(&tb3_cfg[1], 0, sizeof tb3_cfg[1]);
        ok3 &= tb3_unpack(1, pk) && !memcmp(&tb3_cfg[1], &g, sizeof g);
        rnd(2);
    }
    pk[2] = 'X';
    ok3 &= !tb3_unpack(1, pk) && tb3_cfg[1].density == 70u && tb3_cfg[1].seed == 0xBEEFu;
    ok3 &= !tb3_unpack(1, FM6_INIT);
    bad += check("tb3po: a 303 track's FM6 area: every byte <= 127, back exactly (the 32-bit seed); else the defaults", ok3);
    return bad;
}

/* --------------------------------------------------------- 4. the UI --- */
static int line_is(const track_t *t, const tb3_cfg_t *g)   /* the steps are g's fresh line */
{
    tb3_step_t b[NSTEP], s[NSTEP];
    tb3_generate(g, b, (uint32_t)t->p[P_SLEN]);
    tb3_read(t->step, s);
    return !memcmp(b, s, sizeof b);
}
static int ui_tb3(void)
{
    int bad = 0, ok;
    uint32_t k;
    char hx[8];
    track_t *t;
    step_t before[NSTEP];
    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    frame();
    press(B_ARP);
    ok = !ui.home && cur_page()->graph == GR_TB3GEN && str_eq(cur_page()->title, "GENERATE");
    press(B_ARP);
    ok &= cur_page()->graph == GR_TB3SCL;
    press(B_ARP);
    ok &= cur_page()->graph == GR_TB3GEN;
    set_engine(0);
    frame();
    ok &= str_eq(cur_page()->title, "ARP");          /* (GENERATE hidden now: the family's first shown) */
    press(B_ARP);
    ok &= str_eq(cur_page()->title, "ARP 2");
    set_engine(ENGI_303);
    frame();
    ok &= cur_page()->graph == GR_TB3GEN;
    bad += check("tb3po ui: ARP on a 303 track: GENERATE, SCALE, GENERATE; another engine: ARP, ARP 2 (and back)", ok);

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_303);
    t->p[P_SLEN] = 16;
    press(B_ARP);
    memcpy(before, t->step, sizeof before);         /* (the steps before: an undo goes back to them) */
    press(B_OCTUP);
    for (k = 0; k < 4u; k++)
        hx[k] = "0123456789ABCDEF"[(tb3_cfg[0].seed >> (12u - 4u * k)) & 15u];
    hx[4] = 0;
    ok = line_is(t, &tb3_cfg[0]) && !seq_is_empty(t) && song.octave == 0 && tb3_cfg[0].seed != 0xBEEFu &&
         !strncmp(ui.msg, "NEW LINE ", 9) && str_eq(ui.msg + 9, hx);
    bad += check("tb3po ui: OCT+ a new line (a new seed, \"NEW LINE xxxx\"), not the octave", ok);
    turn(EN_K1, 5);
    ok = tb3_cfg[0].density == 75u && tb3_rt[0].stale && line_is(t, &(tb3_cfg_t){70, 40, 25, 2, 9, 0, 1, 0,
                                                                                        tb3_cfg[0].seed});
    press(B_ARP);
    turn(EN_K4, 2);
    ok &= tb3_cfg[0].mutate_bars == 2u && cur_page()->graph == GR_TB3SCL;
    tb3_rt[0].stale = 0;
    turn(EN_K4, 1);
    ok &= tb3_cfg[0].mutate_bars == 3u && !tb3_rt[0].stale;
    turn(EN_K1, 3);
    ok &= tb3_cfg[0].root == 11u && tb3_rt[0].stale;   /* (A + 3: ROOT 0..11 does not wrap, B) */
    bad += check("tb3po ui: the knobs set up the next line (the line stays, NEW LINE's star; MUTATE no star)", ok);
    {
        step_t line[NSTEP];
        memcpy(line, t->step, sizeof line);
        press(B_OCTDN);
        ok = msg_is("MUTATED") && memcmp(line, t->step, sizeof line) && !tb3_rt[0].stale && song.octave == 0;
    }
    hold(B_SAVE);
    ok &= !memcmp(before, t->step, sizeof before);
    bad += check("tb3po ui: OCT- a MUTATE (not the octave); SAVE held undoes it (loads in a row: one undo)", ok);
    ui_power_on();                                  /* (the undo took the 303 too: the loads in a row) */
    t = TSEL;
    set_engine(ENGI_303);
    press(B_ARP);
    chain.running = 1;
    memcpy(before, t->step, sizeof before);
    press(B_OCTUP);
    ok = msg_is("STOP TO EDIT") && !memcmp(before, t->step, sizeof before);
    chain.running = 0;
    bad += check("tb3po ui: in a song OCT+ / OCT- are refused (the steps are a slot's)", ok);

    ui_power_on();                                  /* the arp: inert on a 303 track, its mode kept */
    t = TSEL;
    set_engine(ENGI_303);
    t->engine = ENGI_303;                           /* (the audio side's switch, at once) */
    eng_mem_claim(t);
    t->p[P_AMODE] = AM_UP;
    input_on(t, 60, 100);
    ok = !t->nheld && t->v[0].active && arp_mode(t) == 0 && t->p[P_AMODE] == AM_UP;
    input_off(t, 60);
    set_engine(0);
    ok &= arp_mode(t) == AM_UP;
    bad += check("tb3po ui: the arp does nothing on a 303 track (the keys play), its mode back on another engine", ok);
    return bad;
}

/* -------------------------------------------------- 5. auto MUTATE --- */
static int auto_mutate(void)
{
    int bad = 0, ok = 1;
    uint32_t b, wraps = 0, last, r = 0, m, n0;
    tb3_step_t want[NSTEP], cur[NSTEP];
    track_t *t;
    ui_power_on();
    t = &trk[0];
    set_engine_of(t, ENGI_303);
    t->engine = ENGI_303;
    eng_mem_claim(t);
    t->p[P_SLEN] = 4;
    tb3_cfg[0].mutate_bars = 2;
    r = 0;
    tb3_apply(t, 1, &r);
    tb3_read(t->step, want);
    song.playing = 1;
    seq_start();
    last = t->seq_idx;
    n0 = tb3_auto_n;
    r = 0;
    for (b = 0; b < 20000u && wraps < 7u; b++) {
        seq_tick(t, CTL);
        if (t->seq_idx == 0u && last != 0u) {
            if (b) {                                 /* (block 0: PLAY's step 1, passes from 0) */
                wraps++;
                if (wraps % 2u == 0u)
                    tb3_mutate(&tb3_cfg[0], want, 4, &r);   /* X0X: every 2nd pass, before step 1 plays */
                tb3_read(t->step, cur);
                ok &= !memcmp(cur, want, sizeof cur);
            }
        }
        last = t->seq_idx;
    }
    m = tb3_auto_n - n0;
    bad += check("tb3po seq: MUTATE 2: a MUTATE every 2nd pass, before its step 1, X0X's steps (6 passes: 3)",
                 ok && wraps == 7u && m == 3u);
    ui_power_on();
    t = &trk[0];
    set_engine_of(t, ENGI_303);
    t->p[P_SLEN] = 2;
    tb3_cfg[0].mutate_bars = 0;
    r = 0;
    tb3_apply(t, 1, &r);
    tb3_read(t->step, want);
    song.playing = 1;
    seq_start();
    for (b = 0; b < 3000u; b++)
        seq_tick(t, CTL);
    tb3_read(t->step, cur);
    ok = !memcmp(cur, want, sizeof cur);
    tb3_cfg[0].mutate_bars = 1;
    chain.running = 1;
    for (b = 0; b < 3000u; b++)
        seq_tick(t, CTL);
    chain.running = 0;
    tb3_read(t->step, cur);
    ok &= !memcmp(cur, want, sizeof cur);
    bad += check("tb3po seq: MUTATE OFF: the line as it is; in a song no MUTATE", ok);
    return bad;
}

/* ----------------------------------------------------- 6. a project --- */
static int project(void)
{
    int bad = 0, ok;
    uint8_t v[FP_SIZE + 1u], init[FP_SIZE + 1u], own2[FP_SIZE + 1u];
    tb3_cfg_t g;
    ui_power_on();
    song.playing = 0;
    set_engine_of(&trk[0], ENGI_303);
    tb3_cfg[0] = (tb3_cfg_t){55, 66, 77, 3, 4, 5, 2, 9, 0xDEADBEEFu};
    g = tb3_cfg[0];
    tb3_cfg[1].density = 12;                        /* (track 2 not a 303: not saved, the defaults back) */
    fm6_patch[1][3] ^= 0x15;                        /* track 2's own FM6 patch */
    memcpy(own2, fm6_patch[1], FP_SIZE);
    ok = !project_save(2);
    tb3_reset();
    fm6_init();
    project_load(2);
    fm6_unpack(FM6_INIT, init);
    memcpy(v, fm6_patch[0], FP_SIZE);
    ok = ok && !memcmp(&tb3_cfg[0], &g, sizeof g) && trk[0].eng_req == ENGI_303 && !memcmp(v, init, FP_SIZE) &&
         tb3_cfg[1].density == 70u && !memcmp(fm6_patch[1], own2, FP_SIZE);
    bad += check("tb3po project: a 303 track's settings and seed back, its FM6 patch the init one, track 2's own kept", ok);
    return bad;
}

int main(void)
{
    int bad = pieces() + same_lines() + steps() + ui_tb3() + auto_mutate() + project();
    printf("%s\n", bad ? "TB-3PO TEST FAILED" : "tb3po: all passed");
    return bad != 0;
}
