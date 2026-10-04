/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE EDIT (FELUCCA_SLICE): the MAN slices of the selected part's source, set by hand
 * (eng_slice.c slc_man_*). Hold EDIT on SLICE's first EDIT page to open it (DIV becomes MAN);
 * EDIT or HOME closes it. KNOB 1 selects a slice, KNOB 2 moves its start, KNOB 3 its end (the next
 * slice's start; the last slice: where all slices end), a column of the view per detent (finer
 * when zoomed); KNOB 4 zooms (x1..x16). The view follows the start (KNOB 1, 2) or the end (KNOB 3)
 * of the selected slice, whichever was turned last. OCT+ splits the slice at its
 * middle, OCT- deletes its start (it joins the slice before). The keys play the slices as always
 * (those that play the selected one are lit), PLAY runs the sequencer. The waveform: the smallest
 * and largest sample per column, decoded here in the main loop when the view moves. */
#if FELUCCA_SLICE
#define SE_W 240u
#define SE_MID 52                    /* band 1: the waveform's centre row, +-48 */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* ui_input.c */
static void slc_store_save(void);                                /* project.c */
static struct {
    uint8_t on, src, sel, zoom;      /* zoom: log2 of x1..x16 */
    uint8_t focus;                   /* the view follows the slice's 0 start (KNOB 1, 2), 1 end (KNOB 3) */
    uint32_t view, span;             /* the view: first sample, samples */
    uint32_t env_view, env_span, env_len;     /* what lo / hi were decoded for (env_span 0: nothing) */
    uint8_t env_src;
    int8_t lo[SE_W], hi[SE_W];       /* per column: smallest / largest sample >> 9 */
    uint32_t sig;
} se;

/* the page that opens it: SLICE's first EDIT page of a synth part */
static int slice_edit_page(void)
{
    const page_t *pg = cur_page();
    return !ui.home && !song.seq_mode && song.sel < NPART && ENGINES[TSEL->engine] == &ENG_SLICE &&
           pg->scope == SC_ENGINE && pg->id[0] == P_E0;
}

static void slice_edit_close(void)                  /* the slices go to flash (if they changed) */
{
    se.on = 0;
    ui.force = 1;
    slc_store_save();
}

static void slice_edit_open(void)
{
    track_t *t = TSEL;
    uint32_t src = (uint32_t)t->p[P_E0] & 3u;
    if (!slc_get(src))
        src = 0;                                    /* an empty slot plays BREAK: edit that */
    if (!slc_get(src)) {
        ui_message("NO SAMPLE");
        return;
    }
    slc_man_begin(src);                             /* puts a table in use (the AUTO slices at first) */
    slc_man_commit(src);
    t->p[P_E1] = SLC_DIV_MAN;
    se.on = 1;
    se.src = (uint8_t)src;
    se.sel = 0;
    se.zoom = 0;
    se.focus = 0;
    se.env_span = 0;
    ui.force = 1;
}

/* the view: span = len >> zoom, around the selected slice's start or end (the knob last turned) */
static void slice_edit_view(const slc_src_t *s, const slc_man_t *m)
{
    uint32_t j = se.sel < m->n ? se.sel : 0u;
    uint32_t c = !se.focus ? m->pos[j] : j + 1u < m->n ? m->pos[j + 1u] : slc_man_end(s, m);
    se.span = s->len >> se.zoom;
    se.view = c > se.span / 2u ? c - se.span / 2u : 0u;
    if (se.view + se.span > s->len)
        se.view = s->len - se.span;
}

static void slice_edit_input(uint32_t pressed)
{
    const slc_src_t *s = slc_get(se.src);
    const slc_man_t *cur = s ? slc_man_of(s) : 0;
    slc_man_t *m;
    int32_t d, col;
    if (!cur || ENGINES[TSEL->engine] != &ENG_SLICE || song.sel >= NPART) {
        slice_edit_close();                         /* the slot changed (an upload) or the sound did */
        return;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u)
        transport_req = song.playing ? 2 : 1;
    if ((d = panel_enc(EN_K1)) != 0) {
        se.sel = (uint8_t)clamp((int32_t)se.sel + d, 0, (int32_t)cur->n - 1);
        se.focus = 0;
    }
    if ((d = panel_enc(EN_K4)) != 0)
        se.zoom = (uint8_t)clamp((int32_t)se.zoom + d, 0, 4);
    slice_edit_view(s, cur);
    col = (int32_t)(se.span / SE_W ? se.span / SE_W : 1u);
    if ((d = panel_enc(EN_K2)) != 0) {                  /* the start, a column of the view per detent */
        se.focus = 0;
        m = slc_man_begin(se.src);
        slc_man_move(s, m, se.sel, accel(EN_K2, d, 1000) * col);
        slc_man_commit(se.src);
    } else if ((d = panel_enc(EN_K3)) != 0) {           /* the end (the next slice's start) */
        se.focus = 1;
        m = slc_man_begin(se.src);
        slc_man_move_end(s, m, se.sel, accel(EN_K3, d, 1000) * col);
        slc_man_commit(se.src);
    } else if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
        m = slc_man_begin(se.src);
        se.sel = (uint8_t)slc_man_split(s, m, se.sel);
        se.focus = 0;
        slc_man_commit(se.src);
    } else if ((pressed >> panel.btn[B_OCTDN]) & 1u && se.sel) {
        m = slc_man_begin(se.src);
        se.sel = (uint8_t)slc_man_delete(m, se.sel);
        se.focus = 0;
        slc_man_commit(se.src);
    }
    enc_drop();                                     /* SELECT, ALGORITHM, PRESETS: not here */
}

/* lo / hi for the current view (decoded from the grid point below it) */
static void slice_edit_env(const slc_src_t *s)
{
    slc_dec_t d;
    uint32_t c, e;
    if (se.env_span == se.span && se.env_view == se.view && se.env_len == s->len && se.env_src == se.src)
        return;
    slc_dec_at(&d, se.view, slc_state_at(s, se.view));
    for (c = 0; c < SE_W; c++) {
        int32_t lo = 32767, hi = -32768;
        e = se.view + (c + 1u) * se.span / SE_W;   /* < 2^32: a slot holds < 200 K samples */
        do {                                        /* at least one sample per column */
            int32_t x = d.pos < s->len ? slc_dec_next(s, &d) : 0;
            lo = x < lo ? x : lo;
            hi = x > hi ? x : hi;
        } while (d.pos < e);
        se.lo[c] = (int8_t)(lo >> 9);
        se.hi[c] = (int8_t)(hi >> 9);
    }
    se.env_view = se.view;
    se.env_span = se.span;
    se.env_len = s->len;
    se.env_src = se.src;
}

/* "1.234 S": samples at the source rate as seconds */
static void slice_edit_time(char *b, const slc_src_t *s, uint32_t n)
{
    uint32_t hz = (s->rate >> 4) * 44100u >> 12;  /* as slc_scan */
    b[0] = '0';                                     /* fmt_fix writes ".255": "0.255" */
    fmt_fix(b + 1, (int32_t)(n * 1000u / (hz ? hz : 1u)), 3);
    str_cpy(b + str_len(b), " S", 4);
    if (b[1] != '.')
        str_cpy(b, b + 1, 16);
}

static int32_t slice_edit_x(uint32_t pos)
{
    return pos < se.view ? -1 : (int32_t)((pos - se.view) * SE_W / se.span);
}

/* the legend: a control in full colour, what it does in grey; pairs, 0-terminated */
static const char *const LEGEND_KNOBS[] = {"K1", "SEL", "K2", "START", "K3", "END", "K4", "ZOOM", 0};
static const char *const LEGEND_KEYS[] = {"OCT+", "SPLIT", "OCT-", "DEL", "EDIT", "EXIT", 0};
static void slice_edit_legend(int32_t y, const char *const *t)
{
    int32_t x = 4;
    for (; *t; t += 2) {                            /* 4 px inside a pair, 8 between: fits 232 px */
        x = cv_text(x, y, &FONT_S, t[0], C_HI) + 4;
        x = cv_text(x, y, &FONT_S, t[1], C_GRAY) + 8;
    }
}

static void draw_slice_edit(void)
{
    const slc_src_t *s = slc_get(se.src);
    const slc_man_t *m = s ? slc_man_of(s) : 0;
    uint32_t i, pass, a, b, sig;
    char t[32], u[16];
    if (!m)
        return;
    slice_edit_view(s, m);
    sig = se.src * 7u + se.sel * 131u + se.zoom * 1009u + m->n * 7919u + se.view + m->end * 104729u;
    for (i = 0; i < m->n; i++)
        sig = sig * 31u + m->pos[i];
    if (!ui.force && sig == se.sig)
        return;
    se.sig = sig;
    slice_edit_env(s);
    a = m->pos[se.sel];
    b = se.sel + 1u < m->n ? m->pos[se.sel + 1u] : slc_man_end(s, m);
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 85, 240, 240 - (H_HEAD + 1 + 124 + 85), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, "SLICE EDIT", C_HI);
    cv_text(236 - text_w(&FONT_S, N_SLC_SRC[se.src]), 1, &FONT_S, N_SLC_SRC[se.src], C_AMB);
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: two bands */
        cv_begin(240, pass ? 85u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        {   /* the selected slice, the waveform, the starts, where the view is in the whole */
            uint32_t end = slc_man_end(s, m);
            int32_t xa = slice_edit_x(a), xb = b > se.view + se.span ? (int32_t)SE_W : slice_edit_x(b);
            int32_t xs = slice_edit_x(m->pos[0]), xe = end >= se.view + se.span ? (int32_t)SE_W : slice_edit_x(end);
            xa = xa < 0 ? 0 : xa;
            if (xb > xa)
                cv_rect(xa, SE_MID - 48, xb - xa, 97, C_LINE);
            for (i = 0; i < SE_W; i++)                  /* outside the first start .. the end: dim */
                cv_line((int32_t)i, SE_MID - se.hi[i] * 3 / 4, (int32_t)i, SE_MID - se.lo[i] * 3 / 4,
                        (int32_t)i >= xs && (int32_t)i < xe ? C_HI : C_DIM);
            if (xe >= 0 && xe < (int32_t)SE_W)
                cv_line(xe, SE_MID - 50, xe, SE_MID + 50, C_GRAY);
            for (i = 0; i < m->n; i++) {
                int32_t x = slice_edit_x(m->pos[i]);
                if (x >= 0 && x < (int32_t)SE_W)
                    cv_line(x, SE_MID - 50, x, SE_MID + 50, i == se.sel ? C_WHITE : C_AMB);
            }
            {                                           /* the whole: a thin line; the view on it: a block */
                int32_t vw = (int32_t)(se.span * SE_W / s->len);
                cv_rect(0, 112, 240, 2, C_DIM);
                cv_rect((int32_t)(se.view * SE_W / s->len), 109, vw < 3 ? 3 : vw, 8, C_HI);
            }
        }
        {   /* the selected slice in numbers, the controls */
            int32_t y = 124 + 6;
            str_cpy(t, "SLICE ", sizeof t);
            fmt_int(u, (int32_t)se.sel + 1);
            str_cpy(t + str_len(t), u, sizeof t - str_len(t));
            str_cpy(t + str_len(t), "/", sizeof t - str_len(t));
            fmt_int(u, (int32_t)m->n);
            str_cpy(t + str_len(t), u, sizeof t - str_len(t));
            cv_text(4, y, &FONT_S, t, C_WHITE);
            str_cpy(t, "x", sizeof t);
            fmt_int(u, 1 << se.zoom);
            str_cpy(t + 1, u, sizeof t - 1);
            cv_text(236 - text_w(&FONT_S, t), y, &FONT_S, t, C_AMB);
            str_cpy(t, "AT ", sizeof t);
            slice_edit_time(u, s, a);
            str_cpy(t + 3, u, sizeof t - 3);
            cv_text(4, y + 18, &FONT_S, t, C_HI);
            str_cpy(t, "LEN ", sizeof t);
            slice_edit_time(u, s, b - a);
            str_cpy(t + 4, u, sizeof t - 4);
            cv_text(124, y + 18, &FONT_S, t, C_HI);
            slice_edit_legend(y + 40, LEGEND_KNOBS);
            slice_edit_legend(y + 58, LEGEND_KEYS);
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

/* ui_leds: the keys that play the selected slice (bit k = key k), as kb_map and slice_note_on map them */
static uint32_t slice_edit_keys(void)
{
    const slc_src_t *s = se.on ? slc_get(se.src) : 0;
    const track_t *t = TSEL;
    uint32_t k, n = s ? slc_count(s, SLC_DIV_MAN) : 0u, mask = 0;
    if (!n || song.sel >= NPART)
        return 0;
    for (k = 0; k < 27u; k++)
        if (slc_note_slice(t->p, kb_map(t, k), n) == se.sel)
            mask |= 1u << k;
    return mask;
}

/* ui_draw: 1 = SLICE EDIT is open and drew the screen */
static int slice_edit_draw(void)
{
    if (!se.on)
        return 0;
    draw_slice_edit();
    return 1;
}
#endif
