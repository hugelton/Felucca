/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A panel drawn beside the screen, for the platforms that have no controls of their own (platforms/linux, and
 * platforms/libretro after it): 8 knobs, 14 buttons, 27 keys, their LEDs, and the ways to play them, a pointer
 * (mouse, touch) and a cursor (arrows, a gamepad). It runs on web/emu/felucca_web.c's calls (web_buttons, web_keys,
 * web_enc, web_master, web_lit_* ...), included after it. It is drawn as the screen is, in its pixels, by the
 * firmware's own canvas (src/gfx.c: cv_rrect, cv_text with the UI's fonts, the palette's colours): the screen and
 * the panel one picture (panel_render), scaled to the platform's screen (panel_scale: sharp, then smooth), so a
 * window and a framebuffer alike show it. Nothing of the firmware changes for it. (An interim panel: the UI for
 * larger screens is its own step, memos' plan.)
 *   - two layouts on a canvas in the screen's pixels: WIDE (the screen left, the panel right, 520 x 240) and TALL
 *     (the screen above the panel, 272 x 500); NONE: the screen alone (240 x 240);
 *   - the pointer: a key or a button held while pressed (a key: drag across to the next), a knob turned by a drag up
 *     or down or by the wheel, MASTER set by the drag; the second button (right click) latches a key or a button
 *     (held until it is clicked again: two hands with one mouse);
 *   - the cursor: a focus moved between the controls, PRESS holds the one it is on, LATCH latches it, TURN turns
 *     the knob it is on (SELECT when it is on no knob);
 *   - the stick: an arrow moved over the canvas (an analog stick, for a gamepad without a touch screen) that clicks
 *     as the pointer does (panel_stick_*). */

enum { PNL_NONE, PNL_WIDE, PNL_TALL };
enum { PC_KEY, PC_BTN, PC_KNOB };
#define PC_MASTER NE                     /* (the knob ids: EN_SELECT .. EN_K4, then MASTER) */
#define PNL_NCTL (27 + NB + NE + 1)

typedef struct { int16_t x, y, w, h; uint8_t kind, id; } pnl_ctl_t;
typedef struct {
    int layout, cw, ch;                  /* the canvas */
    int lcd_x, lcd_y, lcd_s;             /* the screen on it */
    pnl_ctl_t c[PNL_NCTL];
    int n;
    /* the pointer */
    int ptr, ptr_x, ptr_y, ptr_acc;      /* ptr: the control held (-1: none) */
    uint32_t ptr_keys, ptr_btns, latch_keys, latch_btns;
    /* the cursor */
    int cursor, focus;
    uint32_t cur_keys, cur_btns;
    int stick;                           /* the arrow shown (the stick moved last) */
    float sx, sy;                        /* the arrow, in canvas pixels */
    int16_t turns[NE + 1];               /* the knobs' marks (the encoders have no end: a picture only) */
} panel_ui_t;

static const char *const PNL_KNOB_NAME[NE + 1] = {"SEL", "ALGO", "PRST", "K1", "K2", "K3", "K4", "MSTR"};
static const uint8_t PNL_KNOB_ORDER[NE + 1] = {PC_MASTER, EN_SELECT, EN_PRESET, EN_ALGO, EN_K1, EN_K2, EN_K3, EN_K4};
static const uint8_t PNL_ROW1[6] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO};
static const uint8_t PNL_ROW2[6] = {B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC};
static const uint8_t PNL_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t PNL_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static const uint8_t PNL_BLACK_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each follows */

static void pnl_add(panel_ui_t *p, int x, int y, int w, int h, int kind, int id)
{
    pnl_ctl_t *c = &p->c[p->n++];
    c->x = (int16_t)x, c->y = (int16_t)y, c->w = (int16_t)w, c->h = (int16_t)h;
    c->kind = (uint8_t)kind, c->id = (uint8_t)id;
}

/* the controls at (x0, y0), 268 x 236: the knobs (each with its label under it), two rows of buttons, OCT, the keys
 * (black after white: a hit finds the black first, pnl_hit looks from the end) */
static void pnl_controls(panel_ui_t *p, int x0, int y0, int key_h)
{
    int i;
    for (i = 0; i < NE + 1; i++)
        pnl_add(p, x0 + 1 + i * 33, y0, 33, 40, PC_KNOB, PNL_KNOB_ORDER[i]);
    for (i = 0; i < 6; i++) {
        pnl_add(p, x0 + i * 45, y0 + 46, 42, 20, PC_BTN, PNL_ROW1[i]);
        pnl_add(p, x0 + i * 45, y0 + 70, 42, 20, PC_BTN, PNL_ROW2[i]);
    }
    pnl_add(p, x0, y0 + 94, 42, 20, PC_BTN, B_OCTDN);
    pnl_add(p, x0 + 45, y0 + 94, 42, 20, PC_BTN, B_OCTUP);
    for (i = 0; i < 16; i++)
        pnl_add(p, x0 + 6 + i * 16, y0 + 122, 15, key_h, PC_KEY, PNL_WHITE[i]);
    for (i = 0; i < 11; i++)
        pnl_add(p, x0 + 6 + (PNL_BLACK_AFTER[i] + 1) * 16 - 5, y0 + 122, 10, key_h * 3 / 5, PC_KEY, PNL_BLACK[i]);
}

static void panel_layout(panel_ui_t *p, int layout)
{
    int i;
    memset(p->c, 0, sizeof p->c);
    p->n = 0;
    p->layout = layout;
    p->ptr = -1;
    if (layout == PNL_WIDE) {
        p->cw = 520, p->ch = 240;
        p->lcd_x = 0, p->lcd_y = 0;
        pnl_controls(p, 248, 2, 112);
    } else if (layout == PNL_TALL) {
        p->cw = 272, p->ch = 500;
        p->lcd_x = 16, p->lcd_y = 4;
        pnl_controls(p, 2, 252, 122);
    } else {
        p->cw = p->ch = 240;
        p->lcd_x = p->lcd_y = 0;
    }
    p->lcd_s = 240;
    for (i = 0; i < p->n && p->c[i].kind != PC_BTN; i++)
        ;
    p->focus = i < p->n ? i : 0;         /* (the cursor starts on FX) */
    p->sx = (float)p->cw / 2, p->sy = (float)p->ch / 2;
}

/* the layout that shows the panel largest on a w x h screen */
static int panel_fit(int w, int h)
{
    double sw = w / 520.0 < h / 240.0 ? w / 520.0 : h / 240.0;
    double st = w / 272.0 < h / 500.0 ? w / 272.0 : h / 500.0;
    return sw >= st ? PNL_WIDE : PNL_TALL;
}

static int pnl_hit(const panel_ui_t *p, int x, int y)
{
    int i;
    for (i = p->n - 1; i >= 0; i--) {
        const pnl_ctl_t *c = &p->c[i];
        if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h)
            return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ playing --- */
static uint32_t panel_keys(const panel_ui_t *p) { return p->ptr_keys | p->latch_keys | p->cur_keys; }
static uint32_t panel_buttons(const panel_ui_t *p) { return p->ptr_btns | p->latch_btns | p->cur_btns; }

static void pnl_turn(panel_ui_t *p, int id, int n)
{
    if (id == PC_MASTER) {
        int32_t v = (int32_t)web_master_adc + 16 * n;
        web_master((uint32_t)(v < 0 ? 0 : v));
    } else {
        web_enc((uint32_t)id, n);
    }
    p->turns[id] = (int16_t)(p->turns[id] + n);
}

static void pnl_hold(const pnl_ctl_t *c, uint32_t *keys, uint32_t *btns)
{
    *keys = c && c->kind == PC_KEY ? 1u << c->id : 0u;
    *btns = c && c->kind == PC_BTN ? 1u << c->id : 0u;
}

/* the pointer, in canvas pixels: down (button 1 or 3), move, up; the wheel over the last place it moved */
static void panel_pointer_down(panel_ui_t *p, int x, int y, int second)
{
    int i = pnl_hit(p, x, y);
    p->ptr_x = x, p->ptr_y = y, p->ptr_acc = 0;
    if (i < 0)
        return;
    if (second) {                        /* latch (a knob: nothing) */
        if (p->c[i].kind == PC_KEY)
            p->latch_keys ^= 1u << p->c[i].id;
        else if (p->c[i].kind == PC_BTN)
            p->latch_btns ^= 1u << p->c[i].id;
        return;
    }
    p->ptr = i;
    pnl_hold(&p->c[i], &p->ptr_keys, &p->ptr_btns);
}
static void panel_pointer_move(panel_ui_t *p, int x, int y)
{
    int dy = p->ptr_y - y;
    p->ptr_x = x;
    if (p->ptr < 0) {
        p->ptr_y = y;
        return;
    }
    if (p->c[p->ptr].kind == PC_KNOB) {  /* 3 px a step, up = clockwise */
        p->ptr_acc += dy;
        p->ptr_y = y;
        while (p->ptr_acc >= 3)
            pnl_turn(p, p->c[p->ptr].id, 1), p->ptr_acc -= 3;
        while (p->ptr_acc <= -3)
            pnl_turn(p, p->c[p->ptr].id, -1), p->ptr_acc += 3;
        return;
    }
    p->ptr_y = y;
    if (p->c[p->ptr].kind == PC_KEY) {   /* across the keys: the next one */
        int i = pnl_hit(p, x, y);
        if (i >= 0 && p->c[i].kind == PC_KEY && i != p->ptr) {
            p->ptr = i;
            pnl_hold(&p->c[i], &p->ptr_keys, &p->ptr_btns);
        }
    }
}
static void panel_pointer_up(panel_ui_t *p)
{
    p->ptr = -1;
    p->ptr_keys = p->ptr_btns = 0;
}
static void panel_wheel(panel_ui_t *p, int n)
{
    int i = pnl_hit(p, p->ptr_x, p->ptr_y);
    pnl_turn(p, i >= 0 && p->c[i].kind == PC_KNOB ? p->c[i].id : EN_SELECT, n);
}

/* the cursor: on (shown) or off; moved to the nearest control that way (dx, dy: -1 0 1) */
static void panel_cursor_on(panel_ui_t *p, int on)
{
    p->cursor = on && p->n > 0;
    if (!p->cursor)
        p->cur_keys = p->cur_btns = 0;
}
static void panel_cursor_move(panel_ui_t *p, int dx, int dy)
{
    const pnl_ctl_t *f = &p->c[p->focus];
    int fx = f->x + f->w / 2, fy = f->y + (f->kind == PC_KEY ? f->h / 4 : f->h / 2), i, best = -1;
    long bd = 0;
    for (i = 0; i < p->n; i++) {
        const pnl_ctl_t *c = &p->c[i];
        int cx = c->x + c->w / 2, cy = c->y + (c->kind == PC_KEY ? c->h / 4 : c->h / 2);
        int a = (cx - fx) * dx + (cy - fy) * dy;               /* along the way */
        int b = dx ? cy - fy : cx - fx;                         /* across it */
        long d;
        if (i == p->focus || a <= 0)
            continue;
        d = (long)a * a + 4L * b * b;                           /* (across counts more: stay in the row) */
        if (best < 0 || d < bd)
            best = i, bd = d;
    }
    if (best >= 0)
        p->focus = best;
    if (p->cur_keys | p->cur_btns)                             /* (held: the new one held) */
        pnl_hold(&p->c[p->focus], &p->cur_keys, &p->cur_btns);
}
static void panel_cursor_press(panel_ui_t *p, int down)
{
    if (!p->cursor)
        return;
    if (down)
        pnl_hold(&p->c[p->focus], &p->cur_keys, &p->cur_btns);
    else
        p->cur_keys = p->cur_btns = 0;
}
static void panel_cursor_latch(panel_ui_t *p)
{
    const pnl_ctl_t *c = &p->c[p->focus];
    if (!p->cursor)
        return;
    if (c->kind == PC_KEY)
        p->latch_keys ^= 1u << c->id;
    else if (c->kind == PC_BTN)
        p->latch_btns ^= 1u << c->id;
}
static void panel_cursor_turn(panel_ui_t *p, int n)
{
    const pnl_ctl_t *c = &p->c[p->focus];
    pnl_turn(p, p->cursor && c->kind == PC_KNOB ? c->id : EN_SELECT, n);
}
/* the stick: moved by (dx, dy) canvas pixels (the arrow comes on, the cursor goes), clicked where it is */
static void panel_stick_move(panel_ui_t *p, float dx, float dy)
{
    p->sx += dx, p->sy += dy;
    p->sx = p->sx < 0 ? 0 : p->sx > p->cw - 1 ? (float)(p->cw - 1) : p->sx;
    p->sy = p->sy < 0 ? 0 : p->sy > p->ch - 1 ? (float)(p->ch - 1) : p->sy;
    if (!p->stick)
        panel_cursor_on(p, 0);
    p->stick = 1;
    panel_pointer_move(p, (int)p->sx, (int)p->sy);
}
static void panel_stick_click(panel_ui_t *p, int down, int second)
{
    if (down)
        panel_pointer_down(p, (int)p->sx, (int)p->sy, second);
    else if (!second)
        panel_pointer_up(p);
}
static void panel_stick_off(panel_ui_t *p) { p->stick = 0; }

static void panel_release_all(panel_ui_t *p)
{
    p->ptr = -1;
    p->ptr_keys = p->ptr_btns = p->latch_keys = p->latch_btns = p->cur_keys = p->cur_btns = 0;
}

/* ------------------------------------------------------------------ drawing --- */
/* the panel in the firmware's canvas (cv_px, at most CV_MAX pixels a piece), the pieces into the picture. The canvas
 * and its state are the UI's between its frames: kept and given back around the panel's drawing */
#define PNL_GREEN RGB(58, 209, 90)       /* PLAY's green LED */

static uint16_t pnl_frame_px[272 * 500]; /* the picture (as the panel gets it: the high byte first) */
static uint16_t pnl_cv_keep[CV_MAX];
static panel_ui_t *pnl_now;

static void pnl_put(int x, int y)        /* the canvas into the picture at (x, y) */
{
    uint32_t i, j;
    for (j = 0; j < cv_h; j++)
        for (i = 0; i < cv_w; i++) {
            int px = x + (int)i, py = y + (int)j;
            if (px >= 0 && py >= 0 && px < pnl_now->cw && py < pnl_now->ch)
                pnl_frame_px[py * pnl_now->cw + px] = cv_px[j * cv_w + i];
        }
}

static void pnl_disc(int cx, int cy, int r, uint16_t c, uint16_t under)   /* anti-aliased: 4 x 4 samples a pixel */
{
    int x, y, i, j;
    for (y = cy - r - 1; y <= cy + r; y++)
        for (x = cx - r - 1; x <= cx + r; x++) {
            int n = 0;
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++) {
                    int dx = (x - cx) * 8 + i * 2 + 1, dy = (y - cy) * 8 + j * 2 + 1;   /* 1/8 px from the middle */
                    n += dx * dx + dy * dy <= r * r * 64;
                }
            if (n)
                cv_pset(x, y, n >= 16 ? c : ux_mix(under, c, n * 100 / 16));
        }
}

static void pnl_label(int cx, int y, const char *s, uint16_t fg)
{
    cv_text(cx - text_w(&AF_S, s) / 2, y, &AF_S, s, fg);
}

static void pnl_ring(const pnl_ctl_t *c, uint16_t col)          /* a 1 px frame round a control, on the picture */
{
    uint16_t sc = swap16(col);
    int i, cw = pnl_now->cw, ch = pnl_now->ch, x0 = c->x - 2, y0 = c->y - 2, x1 = c->x + c->w + 1, y1 = c->y + c->h + 1;
    for (i = x0; i <= x1; i++) {
        if (i >= 0 && i < cw && y0 >= 0) pnl_frame_px[y0 * cw + i] = sc;
        if (i >= 0 && i < cw && y1 < ch) pnl_frame_px[y1 * cw + i] = sc;
    }
    for (i = y0; i <= y1; i++) {
        if (i >= 0 && i < ch && x0 >= 0) pnl_frame_px[i * cw + x0] = sc;
        if (i >= 0 && i < ch && x1 < cw) pnl_frame_px[i * cw + x1] = sc;
    }
}

/* the screen and the panel, cw x ch, into argb (0xAARRGGBB) */
static void panel_render(panel_ui_t *p, uint32_t *argb)
{
    uint32_t lb = web_lit_buttons(), db = web_dim_buttons() | web_breath_buttons();
    uint32_t lk = web_lit_keys(), dk = web_dim_keys() | web_breath_keys(), mk = web_mid_keys();
    uint32_t keys = panel_keys(p) | web_key_mask, btns = panel_buttons(p) | web_btn_labels, i, n = (uint32_t)(p->cw * p->ch);
    uint32_t keep_w = cv_w, keep_h = cv_h;
    uint16_t keep_bg = cv_bg;
    uint8_t keep_scroll = cv_scroll, keep_flip = cv_flip;
    int32_t keep_oy = cv_oy;
    int16_t keep_cy0 = cv_cy0, keep_cy1 = cv_cy1;
    int k, kx0 = 9999, ky0 = 9999, kx1 = 0, ky1 = 0;
    pnl_now = p;
    if (p->layout != PNL_NONE) {
        memcpy(pnl_cv_keep, cv_px, sizeof pnl_cv_keep);
        cv_oy = 0, cv_flip = 0, cv_scroll = 0;
        for (i = 0; i < n; i++)
            pnl_frame_px[i] = swap16(T_BG);
        for (k = 0; k < p->n; k++) {
            const pnl_ctl_t *c = &p->c[k];
            if (c->kind == PC_KNOB) {
                int r = 12, cx = c->w / 2, cy = r + 1;
                double a = c->id == PC_MASTER ? (web_master_adc / 1023.0 * 1.5 - 0.75) * 3.14159265
                                              : p->turns[c->id] * (3.14159265 / 12.0);
                cv_begin((uint32_t)c->w, (uint32_t)c->h, T_BG);
                pnl_disc(cx, cy, r, T_SURF, T_BG);
                pnl_disc(cx + (int)lround(sin(a) * (r - 4)), cy - (int)lround(cos(a) * (r - 4)), 2, T_TEXT, T_SURF);
                pnl_label(cx, 26, PNL_KNOB_NAME[c->id], T_MID);
                pnl_put(c->x, c->y);
            } else if (c->kind == PC_BTN) {
                uint32_t on = (lb >> c->id) & 1u, green = c->id == B_PLAY && (lb >> 14) & 1u, dim = (db >> c->id) & 1u;
                uint16_t face = on ? T_SEL : green ? PNL_GREEN : dim ? T_RAISE : T_SURF;
                uint16_t ink = on ? T_INK : green ? T_BG : dim ? T_TEXT : T_MID;
                if ((btns >> c->id) & 1u)                       /* (held: the accent) */
                    face = T_ACCENT, ink = T_BG;
                cv_begin((uint32_t)c->w, (uint32_t)c->h, T_BG);
                cv_rrect(0, 0, c->w, c->h, 4, face, T_BG);
                cv_bg = face;
                pnl_label(c->w / 2, (c->h - 15) / 2 + 1, B_NAME[c->id], ink);
                if ((p->latch_btns >> c->id) & 1u)
                    pnl_disc(4, 4, 2, T_ACCENT, face);
                pnl_put(c->x, c->y);
            } else {
                if (c->x < kx0) kx0 = c->x;
                if (c->y < ky0) ky0 = c->y;
                if (c->x + c->w > kx1) kx1 = c->x + c->w;
                if (c->y + c->h > ky1) ky1 = c->y + c->h;
            }
        }
        /* the keys, one piece: each with a slit, the key's LED (what the firmware lights on a key) */
        cv_begin((uint32_t)(kx1 - kx0), (uint32_t)(ky1 - ky0), T_BG);
        for (k = 0; k < p->n; k++) {
            const pnl_ctl_t *c = &p->c[k];
            int black = c->w < 15, x = c->x - kx0, y = c->y - ky0;
            uint32_t b = 1u << c->id;
            uint16_t face = keys & b ? T_SEL : black ? T_SURF : ux_mix(T_RAISE, T_TEXT, 10);
            uint16_t slit = lk & b ? T_THEME : mk & b ? ux_mix(T_LINE, T_THEME, 60) : dk & b ? ux_mix(T_LINE, T_THEME, 35) : T_LINE;
            if (c->kind != PC_KEY)
                continue;
            if (black)                                          /* (a gap round it, over the white keys) */
                cv_rect(x - 1, y, c->w + 2, c->h + 1, T_BG);
            cv_rrect(x, y, c->w, c->h, 3, face, T_BG);
            cv_rrect(x + c->w / 2 - 1, y + 4, 2, black ? c->h - 8 : c->h / 3, 1, slit, face);
            if ((p->latch_keys >> c->id) & 1u)
                pnl_disc(x + c->w / 2, y + c->h - 6, 2, T_ACCENT, face);
        }
        pnl_put(kx0, ky0);
        memcpy(cv_px, pnl_cv_keep, sizeof pnl_cv_keep);
        cv_w = keep_w, cv_h = keep_h, cv_bg = keep_bg, cv_scroll = keep_scroll, cv_flip = keep_flip, cv_oy = keep_oy;
        cv_cy0 = keep_cy0, cv_cy1 = keep_cy1;
        if (p->cursor)
            pnl_ring(&p->c[p->focus], T_ACCENT);
    }
    for (k = 0; k < 240 * 240; k++)                             /* the screen */
        pnl_frame_px[(p->lcd_y + k / 240) * p->cw + p->lcd_x + k % 240] = web_fb[k];
    if (p->stick) {                                             /* the arrow: a wedge from its tip, outlined */
        int x = (int)p->sx, y = (int)p->sy, r, j;
        for (r = -1; r < 16; r++)
            for (j = -1; j <= r * 2 / 3 + 1; j++) {
                int px = x + j, py = y + r, edge = r < 0 || r == 15 || j < 0 || j > r * 2 / 3;
                if (px >= 0 && py >= 0 && px < p->cw && py < p->ch)
                    pnl_frame_px[py * p->cw + px] = swap16(edge ? T_BG : T_ACCENT);
            }
    }
    for (i = 0; i < n; i++) {
        uint16_t c = swap16(pnl_frame_px[i]);
        uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
        argb[i] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
    }
}

/* the picture (sw x sh) to the platform's screen (dw x dh), as large as it fits, centred, black around it; sharp
 * bilinear: each source pixel a flat square of its colour (the screen's pixels stay crisp) blended over the last
 * output pixel at its edge (no uneven pixels at any scale); round it the panel's background. *box: where it went
 * (x, y, w, h), for the pointer */
static void panel_scale(const uint32_t *src, int sw, int sh, uint32_t *dst, int dw, int dh, int *box)
{
    double s = (double)dw / sw < (double)dh / sh ? (double)dw / sw : (double)dh / sh;
    int w = (int)(sw * s), h = (int)(sh * s), ox = (dw - w) / 2, oy = (dh - h) / 2, x, y;
    static int *ix0, *iy0, cap_x, cap_y;
    static uint8_t *fx, *fy;
    double k = s < 1.0 ? 1.0 : floor(s), range = 0.5 - 0.5 / k;
    uint32_t bg = src[0] | 0xFF000000u;                         /* round it: the canvas's corner */
    if (dw > cap_x) ix0 = realloc(ix0, sizeof *ix0 * dw), fx = realloc(fx, dw), cap_x = dw;
    if (dh > cap_y) iy0 = realloc(iy0, sizeof *iy0 * dh), fy = realloc(fy, dh), cap_y = dh;
    box[0] = ox, box[1] = oy, box[2] = w, box[3] = h;
    for (x = 0; x < w; x++) {                                   /* per column and row: the two texels, the blend */
        double t = (x + 0.5) / s, fl = floor(t), c = t - fl - 0.5, q;
        c = (c - (c < -range ? -range : c > range ? range : c)) * k + 0.5;
        q = fl + c - 0.5;
        ix0[x] = (int)floor(q);
        fx[x] = (uint8_t)((q - floor(q)) * 255.0 + 0.5);
    }
    for (y = 0; y < h; y++) {
        double t = (y + 0.5) / s, fl = floor(t), c = t - fl - 0.5, q;
        c = (c - (c < -range ? -range : c > range ? range : c)) * k + 0.5;
        q = fl + c - 0.5;
        iy0[y] = (int)floor(q);
        fy[y] = (uint8_t)((q - floor(q)) * 255.0 + 0.5);
    }
    for (y = 0; y < dh; y++) {
        uint32_t *o = dst + (size_t)y * dw;
        if (y < oy || y >= oy + h) {
            for (x = 0; x < dw; x++)
                o[x] = bg;
            continue;
        }
        {
            int r0 = iy0[y - oy], r1 = r0 + 1;
            uint32_t b = fy[y - oy], a = 255u - b;
            const uint32_t *s0 = src + (r0 < 0 ? 0 : r0 >= sh ? sh - 1 : r0) * sw;
            const uint32_t *s1 = src + (r1 < 0 ? 0 : r1 >= sh ? sh - 1 : r1) * sw;
            for (x = 0; x < dw; x++) {
                int c0, c1;
                uint32_t d, e, f, g, ch, out = 0xFF000000u, sh8;
                if (x < ox || x >= ox + w) {
                    o[x] = bg;
                    continue;
                }
                c0 = ix0[x - ox], c1 = c0 + 1;
                c0 = c0 < 0 ? 0 : c0 >= sw ? sw - 1 : c0;
                c1 = c1 < 0 ? 0 : c1 >= sw ? sw - 1 : c1;
                d = fx[x - ox], e = 255u - d;
                for (sh8 = 0; sh8 < 24u; sh8 += 8u) {
                    f = ((s0[c0] >> sh8) & 255u) * e + ((s0[c1] >> sh8) & 255u) * d;
                    g = ((s1[c0] >> sh8) & 255u) * e + ((s1[c1] >> sh8) & 255u) * d;
                    ch = (f * a + g * b + 32512u) / 65025u;
                    out |= ch << sh8;
                }
                o[x] = out;
            }
        }
    }
}
