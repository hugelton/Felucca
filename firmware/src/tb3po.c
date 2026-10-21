/* SPDX-License-Identifier: GPL-3.0-only
 * Part of Felucca: free software under the GNU General Public License, version 3 only (see LICENSE) */
/* TB-3PO: the 303 engine's line generator (eng_303.c), what ARP opens on a 303 track (ui_tb3po.c). Ported from X0X's
 * seq/tb3po.c (github.com/charlesvestal/fm1-x0x at 54194c5, GPL-3.0-only), itself a port of schwung-tb3po (Charles
 * Vestal), after the Phazerville Hemisphere Suite's TB_3PO applet (djphazer and contributors, GPL-3.0). Included by
 * seq.c (auto MUTATE as a pass starts), used by the UI and project.c.
 *
 * Integer only (Felucca's floats are the 303's: build.py float_check), and the same line as X0X's for the same seed and
 * settings: its random draws in the same order (xorshift32, u = 24 bits), each float comparison of u / 2^24 against a
 * float d / 100.0f as an exact integer one against that float x 2^30 (TB3_THR: every such float is a whole number of
 * 2^-30 from 0.01 up), each (int)(u / 2^24 * len) with the float product's rounding (tb3_pick). tests/tb3po_test.c
 * checks it against X0X's float code over many seeds and settings.
 *
 * The line goes into the track's steps (tb3_write): a NOTE step one note, ACC / SLD, velocity 96; a rest a REST step that
 * keeps its note in note[0] (n 0: it plays nothing), which a MUTATE may turn into a note again, as X0X's rests keep
 * theirs. A MUTATE reads the steps back (tb3_read: hand edits included, a chord as its first note, a TIE as a TIE) and
 * writes only the steps it changed, their other fields (RATCH, NUDGE, CHANCE, VEL) kept. LEN is the track's.
 * The settings and the seed per track (tb3_cfg): a project keeps them in a 303 track's FM6 area (tb3_pack, project.c),
 * 0 = X0X's defaults (70 % / 40 % / 25 %, 2 octaves, A minor from A1, seed BEEF). */
#define TB3_NSCALES 6
#define TB3_REST 0u              /* a line's step: gate (X0X's G_REST G_NOTE G_TIE) | ACC | SLD */
#define TB3_NOTE 1u
#define TB3_TIE 2u
#define TB3_GATE 3u
#define TB3_ACC 4u
#define TB3_SLD 8u
typedef struct {                 /* a track's TB-3PO settings (X0X tb3po_cfg_t): the line follows from them and seed */
    uint8_t density, accent, slide;   /* 0..100 % */
    uint8_t oct_range;           /* 1..3 */
    uint8_t root, scale;         /* 0 = C .. 11 = B; TB3_SCALE_NAMES */
    uint8_t base_oct;            /* 0..4: the root's octave, note 12 (base_oct + 1) + root */
    uint8_t mutate_bars;         /* 1..16: MUTATE every that many passes of the line while playing, 0 off */
    uint32_t seed;
} tb3_cfg_t;
typedef struct { uint8_t note, flags; } tb3_step_t;
typedef struct {                 /* a track's TB-3PO state that is not stored */
    uint32_t rng;                /* auto MUTATE's random stream (X0X mut_rng; 0: from the seed) */
    uint32_t passes;             /* passes of the line since PLAY */
    uint8_t stale;               /* a setting changed since the line was written (the screen's NEW LINE *) */
} tb3_rt_t;
static tb3_cfg_t tb3_cfg[NTRK];
static tb3_rt_t tb3_rt[NTRK];
static uint32_t tb3_auto_n;      /* auto MUTATEs so far (the screen redraws on one: ui_graph.c graph_signature) */

typedef struct { int8_t deg[7]; uint8_t len; } tb3_scale_t;
static const tb3_scale_t TB3_SCALES[TB3_NSCALES] = {   /* X0X's (schwung-tb3po's), same order */
    {{0, 2, 3, 5, 7, 8, 10}, 7},                       /* MIN */
    {{0, 1, 3, 5, 7, 8, 10}, 7},                       /* PHRY */
    {{0, 2, 3, 5, 7, 8, 11}, 7},                       /* HARM */
    {{0, 3, 5, 7, 10, 0, 0}, 5},                       /* MPEN */
    {{0, 2, 3, 5, 7, 9, 10}, 7},                       /* DOR */
    {{0, 2, 4, 5, 7, 9, 11}, 7},                       /* MAJ */
};
static const char *const TB3_SCALE_NAMES[TB3_NSCALES] = {"MIN", "PHRY", "HARM", "MPEN", "DOR", "MAJ"};
/* (float)d / 100.0f x 2^30, d = 0..100 (exact: tools-free, tests/tb3po_test.c computes them from the floats) */
static const uint32_t TB3_THR[101] = {
    0, 10737418, 21474836, 32212254, 42949672, 53687092, 64424508, 75161928, 85899344, 96636768, 107374184,
    118111600, 128849016, 139586432, 150323856, 161061280, 171798688, 182536112, 193273536, 204010944, 214748368,
    225485776, 236223200, 246960624, 257698032, 268435456, 279172864, 289910304, 300647712, 311385120, 322122560,
    332859968, 343597376, 354334816, 365072224, 375809632, 386547072, 397284480, 408021888, 418759296, 429496736,
    440234144, 450971552, 461708992, 472446400, 483183808, 493921248, 504658656, 515396064, 526133504, 536870912,
    547608320, 558345728, 569083136, 579820608, 590558016, 601295424, 612032832, 622770240, 633507648, 644245120,
    654982528, 665719936, 676457344, 687194752, 697932160, 708669632, 719407040, 730144448, 740881856, 751619264,
    762356672, 773094144, 783831552, 794568960, 805306368, 816043776, 826781184, 837518592, 848256064, 858993472,
    869730880, 880468288, 891205696, 901943104, 912680576, 923417984, 934155392, 944892800, 955630208, 966367616,
    977105088, 987842496, 998579904, 1009317312, 1020054720, 1030792128, 1041529600, 1052267008, 1063004416,
    1073741824,
};
#define TB3_THR_35 375809632u    /* 0.35f, 0.25f, 0.5f x 2^30 */
#define TB3_THR_25 268435456u
#define TB3_THR_50 536870912u

static void tb3_defaults(tb3_cfg_t *g, uint32_t seed)   /* X0X tb3po_defaults */
{
    g->density = 70;
    g->accent = 40;
    g->slide = 25;
    g->oct_range = 2;
    g->root = 9;
    g->scale = 0;
    g->base_oct = 1;
    g->mutate_bars = 0;
    g->seed = seed ? seed : 0xBEEF;
}

static uint32_t tb3_u(uint32_t *r)                   /* a draw: xorshift32, its low 24 bits (X0X rng_f's u / 2^24) */
{
    uint32_t x = *r ? *r : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *r = x;
    return x & 0xFFFFFFu;
}
static uint32_t tb3_pct(uint32_t v) { return TB3_THR[v <= 100u ? v : 100u]; }
/* (int)(u / 2^24 * len), len 1..7, as the float product rounds: u * len to 24 significant bits (nearest, ties to
 * even), then its whole part; len - 1 at most */
static uint32_t tb3_pick(uint32_t u, uint32_t len)
{
    uint32_t p = u * len, s = 0, q, rem, half;
    while ((p >> s) >= (1u << 24))
        s++;
    if (s) {
        q = p >> s;
        rem = p & ((1u << s) - 1u);
        half = 1u << (s - 1u);
        if (rem > half || (rem == half && (q & 1u)))
            q++;
        p = q << s;
    }
    p >>= 24;
    return p >= len ? len - 1u : p;
}
static uint32_t tb3_clampi(int32_t v, int32_t lo, int32_t hi) { return (uint32_t)(v < lo ? lo : v > hi ? hi : v); }
static const tb3_scale_t *tb3_scale(const tb3_cfg_t *g) { return &TB3_SCALES[g->scale < TB3_NSCALES ? g->scale : 0]; }
static uint8_t tb3_note(const tb3_cfg_t *g, uint32_t degree, uint32_t oct)
{
    return (uint8_t)tb3_clampi(12 * (g->base_oct + 1) + g->root + tb3_scale(g)->deg[degree] + 12 * (int32_t)oct, 0, 127);
}
static uint8_t tb3_kind(uint32_t *r, const tb3_cfg_t *g)   /* X0X roll_kind: NOTE, ACC, SLD (not both) */
{
    uint8_t f = TB3_NOTE;
    if ((tb3_u(r) << 6) < tb3_pct(g->accent))
        f = TB3_NOTE | TB3_ACC;
    if ((tb3_u(r) << 6) < tb3_pct(g->slide))
        f = TB3_NOTE | TB3_SLD;
    return f;
}
static void tb3_slides(tb3_step_t *b, uint32_t len)  /* a slide into a rest does nothing */
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if ((b[i].flags & TB3_SLD) && (b[(i + 1u) % len].flags & TB3_GATE) == TB3_REST)
            b[i].flags = TB3_NOTE;
}

/* a whole line (len steps, NSTEP written) from g and its seed (X0X tb3po_generate) */
static void tb3_generate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len)
{
    const tb3_scale_t *sc = tb3_scale(g);
    uint32_t range = tb3_clampi(g->oct_range, 1, 3), r = g->seed ? g->seed : 1u, i, deg, oct, any = 0;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    for (i = 0; i < NSTEP; i++) {
        b[i].flags = TB3_REST;
        b[i].note = tb3_note(g, 0, 0);
    }
    for (i = 0; i < len; i++) {
        if ((tb3_u(&r) << 6) > tb3_pct(g->density))
            continue;                                /* a rest */
        if (i % 4u == 0u && (tb3_u(&r) << 6) < TB3_THR_35)
            deg = 0;                                 /* on the beat: the root, often */
        else
            deg = tb3_pick(tb3_u(&r), sc->len);
        oct = tb3_pick(tb3_u(&r), range);
        b[i].note = tb3_note(g, deg, oct);
        b[i].flags = tb3_kind(&r, g);
    }
    tb3_slides(b, len);
    for (i = 0; i < len; i++)
        any |= (b[i].flags & TB3_GATE) != TB3_REST;
    if (!any) {
        b[0].note = tb3_note(g, 0, 0);
        b[0].flags = TB3_NOTE;
    }
}

/* re-roll ~25 % of the steps, the random stream on from *rng (0: from the seed; X0X tb3po_mutate) */
static void tb3_mutate(const tb3_cfg_t *g, tb3_step_t *b, uint32_t len, uint32_t *rng)
{
    const tb3_scale_t *sc = tb3_scale(g);
    uint32_t range = tb3_clampi(g->oct_range, 1, 3), r = *rng ? *rng : g->seed ? g->seed : 1u, i, deg;
    len = tb3_clampi((int32_t)len, 1, NSTEP);
    for (i = 0; i < len; i++) {
        if ((tb3_u(&r) << 6) >= TB3_THR_25)
            continue;
        if ((tb3_u(&r) << 6) < TB3_THR_50) {         /* the step */
            if ((tb3_u(&r) << 6) > tb3_pct(g->density))
                b[i].flags = TB3_REST;
            else
                b[i].flags = tb3_kind(&r, g);
        } else {                                     /* its note */
            deg = tb3_pick(tb3_u(&r), sc->len);
            b[i].note = tb3_note(g, deg, tb3_pick(tb3_u(&r), range));
        }
    }
    tb3_slides(b, len);
    *rng = r;
}

static uint32_t tb3_new_seed(uint32_t entropy)       /* X0X tb3po_new_seed */
{
    uint32_t r = entropy * 2654435761u + 0x9E3779B9u;
    (void)tb3_u(&r);
    (void)tb3_u(&r);
    return r ? r : 1u;
}

/* ------------------------------------------------- the track's steps --- */
static void tb3_read(const step_t *s, tb3_step_t *b)  /* the steps as a line (what a MUTATE starts from) */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        uint8_t f = s[i].time == ST_TIE ? TB3_TIE : s[i].time == ST_NOTE && s[i].n ? TB3_NOTE : TB3_REST;
        if (f != TB3_REST)
            f |= (uint8_t)((s[i].flags & SF_ACCENT ? TB3_ACC : 0u) | (s[i].flags & SF_SLIDE ? TB3_SLD : 0u));
        b[i].note = s[i].note[0] & 127u;
        b[i].flags = f;
    }
}
/* line step b into step s: all of it (fresh: OCT+), else its note, time and ACC / SLD (the rest of the step kept) */
static void tb3_put(step_t *s, const tb3_step_t *b, int fresh)
{
    uint32_t g = b->flags & TB3_GATE;
    if (fresh) {
        s->flags = 0;
        s->vel = 0;
        s->probability = 0;
    }
    s->hit = s->acc = 0;
    s->note[0] = b->note;
    s->flags = (uint8_t)((s->flags & ~(SF_ACCENT | SF_SLIDE)) | (g != TB3_REST && (b->flags & TB3_ACC) ? SF_ACCENT : 0u) |
                         (g != TB3_REST && (b->flags & TB3_SLD) ? SF_SLIDE : 0u));
    if (g == TB3_REST) {
        s->time = ST_REST;
        s->n = 0;                                    /* (its note kept in note[0]: a later MUTATE may play it) */
    } else {
        s->time = g == TB3_TIE ? ST_TIE : ST_NOTE;
        s->n = s->n && !fresh ? s->n : 1u;
        if (!s->vel)
            s->vel = 96;
    }
}
/* the line b into the steps: fresh (every step), else only those that differ from old (tb3_read of them) */
static void tb3_write(step_t *s, const tb3_step_t *b, const tb3_step_t *old)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (!old || b[i].note != old[i].note || b[i].flags != old[i].flags)
            tb3_put(&s[i], &b[i], !old);
}

/* the steps track t plays from its TB-3PO (OCT+: a new line, else OCT- / auto MUTATE with *rng); the caller keeps the
 * audio ISR out (the UI: IRQs off, tens of us) or is it (seq_enter) */
static void tb3_apply(track_t *t, int fresh, uint32_t *rng)
{
    uint32_t k = trk_index(t), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    tb3_step_t b[NSTEP], old[NSTEP];
    if (fresh) {
        tb3_generate(&tb3_cfg[k], b, len);
        tb3_write(t->step, b, 0);
    } else {
        tb3_read(t->step, old);
        memcpy(b, old, sizeof b);
        tb3_mutate(&tb3_cfg[k], b, len, rng);
        tb3_write(t->step, b, old);
    }
}

/* a pass of t's line starts (seq.c seq_enter, step 1; top: PLAY's or a section's first, which counts from 0): every
 * mutate_bars passes a MUTATE before it plays, X0X's auto_mutate (not in a song: its steps are a slot's then) */
static __attribute__((noinline)) void tb3_pass(track_t *t, uint32_t top)
{
    uint32_t k = trk_index(t), m = tb3_cfg[k].mutate_bars;
    if (top) {
        tb3_rt[k].passes = 0;
        return;
    }
    tb3_rt[k].passes++;
    if (m && !chain.running && t->eng_req % NENGINES == ENGI_303 && tb3_rt[k].passes % m == 0u) {
        tb3_apply(t, 0, &tb3_rt[k].rng);             /* (NEW LINE's star stays as it was, as X0X's) */
        tb3_auto_n++;
    }
}

/* ------------------------------------------------------------ project --- */
/* a 303 track's FM6 area in a project (project.c: its FM6 patch is the init one then): "TB3P", version 1, the eight
 * settings, the seed in five 7-bit bytes, zeros; every byte 0..127 (proj_pack) */
#define TB3_PK_VER 1u
static void tb3_pack(uint32_t k, uint8_t *pk)
{
    const tb3_cfg_t *g = &tb3_cfg[k % NTRK];
    uint32_t i;
    memset(pk, 0, FM6_PACKED);
    pk[0] = 'T'; pk[1] = 'B'; pk[2] = '3'; pk[3] = 'P';
    pk[4] = TB3_PK_VER;
    pk[5] = g->density; pk[6] = g->accent; pk[7] = g->slide; pk[8] = g->oct_range;
    pk[9] = g->root; pk[10] = g->scale; pk[11] = g->base_oct; pk[12] = g->mutate_bars;
    for (i = 0; i < 5u; i++)
        pk[13u + i] = (uint8_t)((g->seed >> (7u * i)) & 127u);
}
/* track k's settings from a project's FM6 area: 1 = it holds them (each value into its range), else the defaults, 0 */
static int tb3_unpack(uint32_t k, const uint8_t *pk)
{
    tb3_cfg_t *g = &tb3_cfg[k % NTRK];
    uint32_t i, seed = 0;
    tb3_rt[k % NTRK].rng = tb3_rt[k % NTRK].passes = tb3_rt[k % NTRK].stale = 0;
    if (pk[0] != 'T' || pk[1] != 'B' || pk[2] != '3' || pk[3] != 'P' || pk[4] != TB3_PK_VER) {
        tb3_defaults(g, 0);
        return 0;
    }
    g->density = (uint8_t)tb3_clampi(pk[5], 0, 100);
    g->accent = (uint8_t)tb3_clampi(pk[6], 0, 100);
    g->slide = (uint8_t)tb3_clampi(pk[7], 0, 100);
    g->oct_range = (uint8_t)tb3_clampi(pk[8], 1, 3);
    g->root = (uint8_t)tb3_clampi(pk[9], 0, 11);
    g->scale = (uint8_t)tb3_clampi(pk[10], 0, TB3_NSCALES - 1);
    g->base_oct = (uint8_t)tb3_clampi(pk[11], 0, 4);
    g->mutate_bars = (uint8_t)tb3_clampi(pk[12], 0, 16);
    for (i = 0; i < 5u; i++)
        seed |= (uint32_t)(pk[13u + i] & 127u) << (7u * i);
    g->seed = seed ? seed : 1u;
    return 1;
}
static void tb3_reset(void)                           /* power-on: every track X0X's defaults */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        tb3_defaults(&tb3_cfg[k], 0);
        tb3_rt[k].rng = tb3_rt[k].passes = tb3_rt[k].stale = 0;
    }
}
